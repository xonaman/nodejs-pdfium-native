#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_text.h"

// ---------------------------------------------------------------------------
// SearchWorker — async text search
// ---------------------------------------------------------------------------

struct SearchMatchData {
  int charIndex;
  int length;
  std::u16string matchedText;
  struct Rect {
    double left, bottom, right, top;
  };
  std::vector<Rect> rects;
};

class SearchWorker : public Napi::AsyncWorker {
public:
  SearchWorker(Napi::Env env, FPDF_PAGE page, std::u16string needle,
               unsigned long flags,
               std::shared_ptr<std::atomic<bool>> pageAlive,
               std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), needle_(std::move(needle)), flags_(flags),
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

    FPDF_SCHHANDLE handle = FPDFText_FindStart(
        textPage, reinterpret_cast<const unsigned short *>(needle_.c_str()),
        flags_, 0);

    if (handle) {
      while (FPDFText_FindNext(handle)) {
        SearchMatchData match;
        match.charIndex = FPDFText_GetSchResultIndex(handle);
        match.length = FPDFText_GetSchCount(handle);

        // extract the matched text
        if (match.length > 0 && match.length < INT_MAX) {
          size_t textBufLen = static_cast<size_t>(match.length) + 1;
          std::vector<unsigned short> textBuf(textBufLen);
          int got = FPDFText_GetText(textPage, match.charIndex, match.length,
                                     textBuf.data());
          if (got > 0) {
            match.matchedText = std::u16string(
                reinterpret_cast<const char16_t *>(textBuf.data()),
                static_cast<size_t>(got - 1));
          }
        }

        int numRects =
            FPDFText_CountRects(textPage, match.charIndex, match.length);
        for (int r = 0; r < numRects; r++) {
          double left, top, right, bottom;
          if (FPDFText_GetRect(textPage, r, &left, &top, &right, &bottom)) {
            match.rects.push_back({left, bottom, right, top});
          }
        }
        matches_.push_back(std::move(match));
      }
      FPDFText_FindClose(handle);
    }
    FPDFText_ClosePage(textPage);
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array results = Napi::Array::New(env, matches_.size());
    for (uint32_t i = 0; i < matches_.size(); i++) {
      auto &m = matches_[i];
      Napi::Object match = Napi::Object::New(env);
      match.Set("charIndex", Napi::Number::New(env, m.charIndex));
      match.Set("length", Napi::Number::New(env, m.length));
      SetU16IfPresent(match, "matchedText", env, m.matchedText);

      Napi::Array rects = Napi::Array::New(env, m.rects.size());
      for (uint32_t r = 0; r < m.rects.size(); r++) {
        rects.Set(r, CreateBoundsObject(env, m.rects[r].left, m.rects[r].bottom,
                                        m.rects[r].right, m.rects[r].top));
      }
      match.Set("rects", rects);
      results.Set(i, match);
    }
    deferred_.Resolve(results);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  std::u16string needle_;
  unsigned long flags_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<SearchMatchData> matches_;
};
