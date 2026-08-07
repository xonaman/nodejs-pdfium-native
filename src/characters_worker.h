#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <climits>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_text.h"

// ---------------------------------------------------------------------------
// GetCharactersWorker — async positioned text extraction
// ---------------------------------------------------------------------------
// page.getText() flattens a page to a single string and throws the geometry
// away. This worker keeps it: every character comes back with its bounding box,
// baseline origin, font and rotation, which is what layout reconstruction,
// coordinate mapping and redaction targeting all need.
//
// A dense page can hold tens of thousands of characters, so the range options
// exist to let callers page through instead of materialising everything.

inline bool IsHighSurrogate(unsigned int u) {
  return u >= 0xD800 && u <= 0xDBFF;
}
inline bool IsLowSurrogate(unsigned int u) {
  return u >= 0xDC00 && u <= 0xDFFF;
}

// Encode one text-page entry as the JS string for its `char` field.
//
// FPDFText_GetUnicode returns a single UTF-16 *code unit* per character index —
// on POSIX as well as Windows — so a non-BMP character occupies two consecutive
// indices holding its high and low surrogate. A lone surrogate cannot survive
// the trip into a JS string (V8 substitutes U+FFFD), so emitting each unit
// separately would corrupt every astral character.
//
// Instead the pair is joined onto the FIRST index and the second yields an
// empty string. That keeps both invariants callers rely on: indices still line
// up one-to-one with getText() and with search() results, and concatenating
// every `char` still reproduces getText() exactly.
inline std::u16string EncodeCharAt(const std::vector<unsigned int> &units,
                                   size_t i) {
  unsigned int u = units[i];
  if (u == 0 || u > 0x10FFFF)
    return {};

  if (IsHighSurrogate(u)) {
    if (i + 1 < units.size() && IsLowSurrogate(units[i + 1])) {
      std::u16string out;
      out.push_back(static_cast<char16_t>(u));
      out.push_back(static_cast<char16_t>(units[i + 1]));
      return out;
    }
    return {}; // unpaired high surrogate — not representable
  }

  // the low half of a pair already emitted on the previous index
  if (IsLowSurrogate(u))
    return {};

  if (u <= 0xFFFF)
    return std::u16string(1, static_cast<char16_t>(u));

  // a whole code point above the BMP, should PDFium ever report one directly
  unsigned int v = u - 0x10000;
  std::u16string out;
  out.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
  out.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
  return out;
}

struct CharacterInfo {
  int index = 0;
  unsigned int unicode = 0;
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  double originX = 0, originY = 0;
  bool hasOrigin = false;
  double fontSize = 0;
  std::string fontName;
  int fontFlags = 0;
  int fontWeight = -1;
  float angle = 0;
  bool isGenerated = false;
  bool isHyphen = false;
  bool hasUnicodeMapError = false;
};

class GetCharactersWorker : public SafeAsyncWorker {
public:
  GetCharactersWorker(Napi::Env env, FPDF_PAGE page, int start, int count,
                      std::shared_ptr<std::atomic<bool>> pageAlive,
                      std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), start_(start), count_(count),
        pageAlive_(std::move(pageAlive)), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
    if (!textPage) {
      SetError("Failed to load text page");
      return;
    }

    int charCount = FPDFText_CountChars(textPage);
    if (charCount <= 0) {
      FPDFText_ClosePage(textPage);
      return;
    }

    if (start_ >= charCount) {
      FPDFText_ClosePage(textPage);
      return;
    }

    // count_ < 0 means "to the end of the page"
    int end = charCount;
    if (count_ >= 0) {
      // start_ + count_ cannot overflow: both are non-negative and bounded by
      // the caller-side 32-bit index guard
      long long requestedEnd =
          static_cast<long long>(start_) + static_cast<long long>(count_);
      if (requestedEnd < end)
        end = static_cast<int>(requestedEnd);
    }

    characters_.reserve(static_cast<size_t>(end - start_));
    for (int i = start_; i < end; i++) {
      CharacterInfo info;
      info.index = i;
      info.unicode = FPDFText_GetUnicode(textPage, i);

      double left, right, bottom, top;
      if (FPDFText_GetCharBox(textPage, i, &left, &right, &bottom, &top)) {
        info.hasBounds = true;
        info.left = left;
        info.right = right;
        info.bottom = bottom;
        info.top = top;
      }

      double x, y;
      if (FPDFText_GetCharOrigin(textPage, i, &x, &y)) {
        info.hasOrigin = true;
        info.originX = x;
        info.originY = y;
      }

      info.fontSize = FPDFText_GetFontSize(textPage, i);
      info.fontWeight = FPDFText_GetFontWeight(textPage, i);

      int flags = 0;
      info.fontName = ReadUtf8(
          [&](char *, unsigned long) {
            return FPDFText_GetFontInfo(textPage, i, nullptr, 0, &flags);
          },
          [&](char *buf, unsigned long len) {
            return FPDFText_GetFontInfo(textPage, i, buf, len, &flags);
          });
      info.fontFlags = flags;

      // PDFium already normalizes the angle to [0, 2pi), so a negative value
      // can only be its -1 error return; report 0 instead of a bogus angle
      float angle = FPDFText_GetCharAngle(textPage, i);
      info.angle = angle < 0 ? 0.0f : angle;

      // each of these returns -1 on error, which is not "true"
      info.isGenerated = FPDFText_IsGenerated(textPage, i) == 1;
      info.isHyphen = FPDFText_IsHyphen(textPage, i) == 1;
      info.hasUnicodeMapError = FPDFText_HasUnicodeMapError(textPage, i) == 1;

      characters_.push_back(std::move(info));
    }

    FPDFText_ClosePage(textPage);
  }

  void OnOK() override {
    Napi::Env env = Env();

    // EncodeCharAt needs the next entry to pair a surrogate with its partner
    std::vector<unsigned int> units;
    units.reserve(characters_.size());
    for (const auto &info : characters_)
      units.push_back(info.unicode);

    Napi::Array arr = Napi::Array::New(env, characters_.size());
    for (uint32_t i = 0; i < characters_.size(); i++) {
      const auto &info = characters_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("index", Napi::Number::New(env, info.index));
      SetU16(obj, "char", env, EncodeCharAt(units, i));
      obj.Set("unicode", Napi::Number::New(env, info.unicode));
      if (info.hasBounds) {
        obj.Set("bounds", CreateBoundsObject(env, info.left, info.bottom,
                                             info.right, info.top));
      }
      if (info.hasOrigin) {
        obj.Set("x", Napi::Number::New(env, info.originX));
        obj.Set("y", Napi::Number::New(env, info.originY));
      }
      obj.Set("fontSize", Napi::Number::New(env, info.fontSize));
      obj.Set("fontName", Napi::String::New(env, info.fontName));
      obj.Set("fontFlags", Napi::Number::New(env, info.fontFlags));
      // -1 is PDFium's error return and 0 means the font declares no weight;
      // neither is a usable value, so omit the key rather than report a
      // meaningless 0 for every non-embedded font.
      if (info.fontWeight > 0)
        obj.Set("fontWeight", Napi::Number::New(env, info.fontWeight));
      obj.Set("angle", Napi::Number::New(env, info.angle));
      obj.Set("isGenerated", Napi::Boolean::New(env, info.isGenerated));
      obj.Set("isHyphen", Napi::Boolean::New(env, info.isHyphen));
      obj.Set("hasUnicodeMapError",
              Napi::Boolean::New(env, info.hasUnicodeMapError));
      arr.Set(i, obj);
    }
    deferred_.Resolve(arr);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  int start_;
  int count_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<CharacterInfo> characters_;
};
