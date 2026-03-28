#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_formfill.h"
#include "fpdf_text.h"

// common alive-check macro for all page workers
#define CHECK_ALIVE()                                                          \
  if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {              \
    SetError("Page or document was closed");                                   \
    return;                                                                    \
  }

// ---------------------------------------------------------------------------
// GetTextWorker — async text extraction
// ---------------------------------------------------------------------------

class GetTextWorker : public Napi::AsyncWorker {
public:
  GetTextWorker(Napi::Env env, FPDF_PAGE page,
                std::shared_ptr<std::atomic<bool>> pageAlive,
                std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

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
    if (charCount > 0) {
      int bufLen = charCount + 1;
      textBuf_.resize(bufLen);
      FPDFText_GetText(textPage, 0, charCount, textBuf_.data());
      textLen_ = charCount;
    }
    FPDFText_ClosePage(textPage);
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (textLen_ == 0) {
      deferred_.Resolve(Napi::String::New(env, ""));
    } else {
      deferred_.Resolve(Napi::String::New(
          env, reinterpret_cast<const char16_t *>(textBuf_.data()), textLen_));
    }
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<unsigned short> textBuf_;
  int textLen_ = 0;
};

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
    if (!textPage)
      return;

    FPDF_SCHHANDLE handle = FPDFText_FindStart(
        textPage, reinterpret_cast<const unsigned short *>(needle_.c_str()),
        flags_, 0);

    if (handle) {
      while (FPDFText_FindNext(handle)) {
        SearchMatchData match;
        match.charIndex = FPDFText_GetSchResultIndex(handle);
        match.length = FPDFText_GetSchCount(handle);

        // extract the matched text
        int textBufLen = match.length + 1;
        std::vector<unsigned short> textBuf(textBufLen);
        int got = FPDFText_GetText(textPage, match.charIndex, match.length,
                                   textBuf.data());
        if (got > 0) {
          match.matchedText =
              std::u16string(reinterpret_cast<const char16_t *>(textBuf.data()),
                             static_cast<size_t>(got - 1));
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

// ---------------------------------------------------------------------------
// GetLinksWorker — async link extraction
// ---------------------------------------------------------------------------

struct LinkData {
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  std::string url;
  int pageIndex = -1;
  bool hasPageIndex = false;
  std::string actionType;
  // destination location
  float destX = 0, destY = 0, destZoom = 0;
  bool hasDestX = false, hasDestY = false, hasDestZoom = false;
  std::string filePath;
};

class GetLinksWorker : public Napi::AsyncWorker {
public:
  GetLinksWorker(Napi::Env env, FPDF_PAGE page, FPDF_DOCUMENT doc,
                 std::shared_ptr<std::atomic<bool>> pageAlive,
                 std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), doc_(doc), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    int pos = 0;
    FPDF_LINK link;
    while (FPDFLink_Enumerate(page_, &pos, &link)) {
      LinkData data;

      FS_RECTF rect;
      if (FPDFLink_GetAnnotRect(link, &rect)) {
        data.hasBounds = true;
        data.left = rect.left;
        data.bottom = rect.bottom;
        data.right = rect.right;
        data.top = rect.top;
      }

      FPDF_ACTION action = FPDFLink_GetAction(link);
      if (action) {
        unsigned long actionType = FPDFAction_GetType(action);
        data.actionType = ActionTypeString(actionType);

        if (actionType == PDFACTION_URI) {
          unsigned long len = FPDFAction_GetURIPath(doc_, action, nullptr, 0);
          if (len > 0) {
            std::vector<char> buf(len);
            FPDFAction_GetURIPath(doc_, action, buf.data(), len);
            data.url = std::string(buf.data(), len - 1);
          }
        }

        // file path for remote goto / launch actions
        if (actionType == PDFACTION_REMOTEGOTO ||
            actionType == PDFACTION_LAUNCH) {
          unsigned long fpLen = FPDFAction_GetFilePath(action, nullptr, 0);
          if (fpLen > 0) {
            std::vector<char> fpBuf(fpLen);
            FPDFAction_GetFilePath(action, fpBuf.data(), fpLen);
            data.filePath = std::string(fpBuf.data(), fpLen - 1);
          }
        }
      }

      FPDF_DEST dest = FPDFLink_GetDest(doc_, link);
      if (!dest && action) {
        dest = FPDFAction_GetDest(doc_, action);
      }
      if (dest) {
        int idx = FPDFDest_GetDestPageIndex(doc_, dest);
        if (idx >= 0) {
          data.hasPageIndex = true;
          data.pageIndex = idx;
        }

        FPDF_BOOL hasX, hasY, hasZoom;
        FS_FLOAT x, y, zoom;
        if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y,
                                       &zoom)) {
          if (hasX) {
            data.hasDestX = true;
            data.destX = x;
          }
          if (hasY) {
            data.hasDestY = true;
            data.destY = y;
          }
          if (hasZoom) {
            data.hasDestZoom = true;
            data.destZoom = zoom;
          }
        }
      }

      links_.push_back(std::move(data));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, links_.size());
    for (uint32_t i = 0; i < links_.size(); i++) {
      auto &d = links_[i];
      Napi::Object obj = Napi::Object::New(env);
      if (d.hasBounds)
        obj.Set("bounds",
                CreateBoundsObject(env, d.left, d.bottom, d.right, d.top));
      if (!d.url.empty())
        obj.Set("url", Napi::String::New(env, d.url));
      if (d.hasPageIndex)
        obj.Set("pageIndex", Napi::Number::New(env, d.pageIndex));
      if (!d.actionType.empty())
        obj.Set("actionType", Napi::String::New(env, d.actionType));
      if (d.hasDestX)
        obj.Set("destX", Napi::Number::New(env, d.destX));
      if (d.hasDestY)
        obj.Set("destY", Napi::Number::New(env, d.destY));
      if (d.hasDestZoom)
        obj.Set("destZoom", Napi::Number::New(env, d.destZoom));
      if (!d.filePath.empty())
        obj.Set("filePath", Napi::String::New(env, d.filePath));
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
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<LinkData> links_;
};

// ---------------------------------------------------------------------------
// GetAnnotationsWorker — async annotation extraction
// ---------------------------------------------------------------------------

struct AnnotationData {
  std::string type;
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  std::u16string contents;
  unsigned int r = 0, g = 0, b = 0, a = 0;
  bool hasColor = false;
  unsigned int ir = 0, ig = 0, ib = 0, ia = 0;
  bool hasInteriorColor = false;
  float borderHRadius = 0, borderVRadius = 0, borderWidth = 0;
  bool hasBorder = false;
  std::u16string author;
  std::u16string subject;
  std::u16string creationDate;
  std::u16string modDate;
  int flags = 0;
  struct QuadPoints {
    float x1, y1, x2, y2, x3, y3, x4, y4;
  };
  std::vector<QuadPoints> quadPoints;
};

class GetAnnotationsWorker : public Napi::AsyncWorker {
public:
  GetAnnotationsWorker(Napi::Env env, FPDF_PAGE page,
                       std::shared_ptr<std::atomic<bool>> pageAlive,
                       std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    int count = FPDFPage_GetAnnotCount(page_);
    for (int i = 0; i < count; i++) {
      FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page_, i);
      if (!annot)
        continue;

      AnnotationData data;

      FPDF_ANNOTATION_SUBTYPE subtype = FPDFAnnot_GetSubtype(annot);
      switch (subtype) {
      case FPDF_ANNOT_TEXT:
        data.type = "text";
        break;
      case FPDF_ANNOT_LINK:
        data.type = "link";
        break;
      case FPDF_ANNOT_FREETEXT:
        data.type = "freetext";
        break;
      case FPDF_ANNOT_LINE:
        data.type = "line";
        break;
      case FPDF_ANNOT_SQUARE:
        data.type = "square";
        break;
      case FPDF_ANNOT_CIRCLE:
        data.type = "circle";
        break;
      case FPDF_ANNOT_POLYGON:
        data.type = "polygon";
        break;
      case FPDF_ANNOT_POLYLINE:
        data.type = "polyline";
        break;
      case FPDF_ANNOT_HIGHLIGHT:
        data.type = "highlight";
        break;
      case FPDF_ANNOT_UNDERLINE:
        data.type = "underline";
        break;
      case FPDF_ANNOT_SQUIGGLY:
        data.type = "squiggly";
        break;
      case FPDF_ANNOT_STRIKEOUT:
        data.type = "strikeout";
        break;
      case FPDF_ANNOT_STAMP:
        data.type = "stamp";
        break;
      case FPDF_ANNOT_CARET:
        data.type = "caret";
        break;
      case FPDF_ANNOT_INK:
        data.type = "ink";
        break;
      case FPDF_ANNOT_POPUP:
        data.type = "popup";
        break;
      case FPDF_ANNOT_FILEATTACHMENT:
        data.type = "fileattachment";
        break;
      case FPDF_ANNOT_SOUND:
        data.type = "sound";
        break;
      case FPDF_ANNOT_WIDGET:
        data.type = "widget";
        break;
      case FPDF_ANNOT_REDACT:
        data.type = "redact";
        break;
      case FPDF_ANNOT_WATERMARK:
        data.type = "watermark";
        break;
      default:
        data.type = "unknown";
        break;
      }

      FS_RECTF rect;
      if (FPDFAnnot_GetRect(annot, &rect)) {
        data.hasBounds = true;
        data.left = rect.left;
        data.bottom = rect.bottom;
        data.right = rect.right;
        data.top = rect.top;
      }

      // helper to read annotation string values via ReadU16
      auto readAnnotStr = [&](const char *key) {
        return ReadU16(
            [&](auto *, unsigned long) {
              return FPDFAnnot_GetStringValue(annot, key, nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFAnnot_GetStringValue(annot, key, buf, len);
            });
      };

      data.contents = readAnnotStr("Contents");

      unsigned int cr, cg, cb, ca;
      if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_Color, &cr, &cg, &cb,
                             &ca)) {
        data.hasColor = true;
        data.r = cr;
        data.g = cg;
        data.b = cb;
        data.a = ca;
      }

      // interior color (fill color for markup annotations)
      unsigned int icr, icg, icb, ica;
      if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_InteriorColor, &icr,
                             &icg, &icb, &ica)) {
        data.hasInteriorColor = true;
        data.ir = icr;
        data.ig = icg;
        data.ib = icb;
        data.ia = ica;
      }

      // border style
      float hRadius, vRadius, bWidth;
      if (FPDFAnnot_GetBorder(annot, &hRadius, &vRadius, &bWidth)) {
        data.hasBorder = true;
        data.borderHRadius = hRadius;
        data.borderVRadius = vRadius;
        data.borderWidth = bWidth;
      }

      // quad points (for highlight, underline, squiggly, strikeout, link,
      // redact)
      if (FPDFAnnot_HasAttachmentPoints(annot)) {
        size_t qpCount = FPDFAnnot_CountAttachmentPoints(annot);
        for (size_t qi = 0; qi < qpCount; qi++) {
          FS_QUADPOINTSF qp;
          if (FPDFAnnot_GetAttachmentPoints(annot, qi, &qp)) {
            data.quadPoints.push_back(
                {qp.x1, qp.y1, qp.x2, qp.y2, qp.x3, qp.y3, qp.x4, qp.y4});
          }
        }
      }

      data.author = readAnnotStr("T");
      data.subject = readAnnotStr("Subj");
      data.creationDate = readAnnotStr("CreationDate");
      data.modDate = readAnnotStr("M");

      // annotation flags
      data.flags = FPDFAnnot_GetFlags(annot);

      annotations_.push_back(std::move(data));
      FPDFPage_CloseAnnot(annot);
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, annotations_.size());
    for (uint32_t i = 0; i < annotations_.size(); i++) {
      auto &d = annotations_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("type", Napi::String::New(env, d.type));
      if (d.hasBounds)
        obj.Set("bounds",
                CreateBoundsObject(env, d.left, d.bottom, d.right, d.top));
      SetU16(obj, "contents", env, d.contents);
      if (d.hasColor) {
        obj.Set("color", CreateColorObject(env, d.r, d.g, d.b, d.a));
      } else {
        obj.Set("color", env.Null());
      }
      if (d.hasInteriorColor) {
        obj.Set("interiorColor",
                CreateColorObject(env, d.ir, d.ig, d.ib, d.ia));
      }
      SetU16(obj, "author", env, d.author);
      SetU16(obj, "subject", env, d.subject);
      SetU16(obj, "creationDate", env, d.creationDate);
      SetU16(obj, "modDate", env, d.modDate);
      obj.Set("flags", Napi::Number::New(env, d.flags));
      if (d.hasBorder) {
        Napi::Object border = Napi::Object::New(env);
        border.Set("horizontalRadius", Napi::Number::New(env, d.borderHRadius));
        border.Set("verticalRadius", Napi::Number::New(env, d.borderVRadius));
        border.Set("width", Napi::Number::New(env, d.borderWidth));
        obj.Set("border", border);
      }
      if (!d.quadPoints.empty()) {
        Napi::Array qpArr = Napi::Array::New(env, d.quadPoints.size());
        for (uint32_t qi = 0; qi < d.quadPoints.size(); qi++) {
          auto &qp = d.quadPoints[qi];
          Napi::Object qpObj = Napi::Object::New(env);
          qpObj.Set("x1", Napi::Number::New(env, qp.x1));
          qpObj.Set("y1", Napi::Number::New(env, qp.y1));
          qpObj.Set("x2", Napi::Number::New(env, qp.x2));
          qpObj.Set("y2", Napi::Number::New(env, qp.y2));
          qpObj.Set("x3", Napi::Number::New(env, qp.x3));
          qpObj.Set("y3", Napi::Number::New(env, qp.y3));
          qpObj.Set("x4", Napi::Number::New(env, qp.x4));
          qpObj.Set("y4", Napi::Number::New(env, qp.y4));
          qpArr.Set(qi, qpObj);
        }
        obj.Set("quadPoints", qpArr);
      }
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
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<AnnotationData> annotations_;
};

// ---------------------------------------------------------------------------
// GetObjectWorker — async page object extraction
// ---------------------------------------------------------------------------

struct PageObjectData {
  std::string type;
  float left = 0, bottom = 0, right = 0, top = 0;
  unsigned int fillR = 0, fillG = 0, fillB = 0, fillA = 0;
  bool hasFillColor = false;
  unsigned int strokeR = 0, strokeG = 0, strokeB = 0, strokeA = 0;
  bool hasStrokeColor = false;
  // text-specific
  std::u16string text;
  float fontSize = 0;
  std::string fontName;
  int fontWeight = -1;
  int italicAngle = 0;
  bool hasItalicAngle = false;
  std::string renderMode;
  std::string fontFamily;
  int isEmbedded = -1; // -1 = unknown, 0 = no, 1 = yes
  int fontFlags = -1;
  // image-specific
  unsigned int imageWidth = 0, imageHeight = 0;
  float horizontalDpi = 0, verticalDpi = 0;
  unsigned int bitsPerPixel = 0;
  std::string colorspace;
  std::vector<std::string> filters;
  bool hasImageMetadata = false;
};

class GetObjectWorker : public Napi::AsyncWorker {
public:
  GetObjectWorker(Napi::Env env, FPDF_PAGE page, int index,
                  std::shared_ptr<std::atomic<bool>> pageAlive,
                  std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), index_(index), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page_, index_);
    if (!obj) {
      SetError("Object not found");
      return;
    }

    int type = FPDFPageObj_GetType(obj);
    switch (type) {
    case FPDF_PAGEOBJ_TEXT:
      data_.type = "text";
      break;
    case FPDF_PAGEOBJ_PATH:
      data_.type = "path";
      break;
    case FPDF_PAGEOBJ_IMAGE:
      data_.type = "image";
      break;
    case FPDF_PAGEOBJ_SHADING:
      data_.type = "shading";
      break;
    case FPDF_PAGEOBJ_FORM:
      data_.type = "form";
      break;
    default:
      data_.type = "unknown";
      break;
    }

    FPDFPageObj_GetBounds(obj, &data_.left, &data_.bottom, &data_.right,
                          &data_.top);

    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a)) {
      data_.hasFillColor = true;
      data_.fillR = r;
      data_.fillG = g;
      data_.fillB = b;
      data_.fillA = a;
    }
    if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a)) {
      data_.hasStrokeColor = true;
      data_.strokeR = r;
      data_.strokeG = g;
      data_.strokeB = b;
      data_.strokeA = a;
    }

    if (type == FPDF_PAGEOBJ_TEXT) {
      FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
      if (textPage) {
        data_.text = ReadU16(
            [&](auto *, unsigned long) {
              return FPDFTextObj_GetText(obj, textPage, nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFTextObj_GetText(obj, textPage, buf, len);
            });
        FPDFText_ClosePage(textPage);
      }

      float fs = 0;
      if (FPDFTextObj_GetFontSize(obj, &fs))
        data_.fontSize = fs;

      FPDF_FONT font = FPDFTextObj_GetFont(obj);
      if (font) {
        size_t nameLen = FPDFFont_GetBaseFontName(font, nullptr, 0);
        if (nameLen > 1) {
          std::vector<char> nameBuf(nameLen);
          FPDFFont_GetBaseFontName(font, nameBuf.data(), nameLen);
          data_.fontName = std::string(nameBuf.data(), nameLen - 1);
        }
        data_.fontWeight = FPDFFont_GetWeight(font);
        int angle = 0;
        if (FPDFFont_GetItalicAngle(font, &angle)) {
          data_.hasItalicAngle = true;
          data_.italicAngle = angle;
        }

        // font family name
        size_t famLen = FPDFFont_GetFamilyName(font, nullptr, 0);
        if (famLen > 1) {
          std::vector<char> famBuf(famLen);
          FPDFFont_GetFamilyName(font, famBuf.data(), famLen);
          data_.fontFamily = std::string(famBuf.data(), famLen - 1);
        }

        data_.isEmbedded = FPDFFont_GetIsEmbedded(font);
        data_.fontFlags = FPDFFont_GetFlags(font);
      }

      // text render mode
      FPDF_TEXT_RENDERMODE rm = FPDFTextObj_GetTextRenderMode(obj);
      switch (rm) {
      case FPDF_TEXTRENDERMODE_FILL:
        data_.renderMode = "fill";
        break;
      case FPDF_TEXTRENDERMODE_STROKE:
        data_.renderMode = "stroke";
        break;
      case FPDF_TEXTRENDERMODE_FILL_STROKE:
        data_.renderMode = "fillStroke";
        break;
      case FPDF_TEXTRENDERMODE_INVISIBLE:
        data_.renderMode = "invisible";
        break;
      case FPDF_TEXTRENDERMODE_FILL_CLIP:
        data_.renderMode = "fillClip";
        break;
      case FPDF_TEXTRENDERMODE_STROKE_CLIP:
        data_.renderMode = "strokeClip";
        break;
      case FPDF_TEXTRENDERMODE_FILL_STROKE_CLIP:
        data_.renderMode = "fillStrokeClip";
        break;
      case FPDF_TEXTRENDERMODE_CLIP:
        data_.renderMode = "clip";
        break;
      default:
        data_.renderMode = "unknown";
        break;
      }
    }

    if (type == FPDF_PAGEOBJ_IMAGE) {
      FPDFImageObj_GetImagePixelSize(obj, &data_.imageWidth,
                                     &data_.imageHeight);

      // rich image metadata
      FPDF_IMAGEOBJ_METADATA meta;
      if (FPDFImageObj_GetImageMetadata(obj, page_, &meta)) {
        data_.hasImageMetadata = true;
        data_.horizontalDpi = meta.horizontal_dpi;
        data_.verticalDpi = meta.vertical_dpi;
        data_.bitsPerPixel = meta.bits_per_pixel;
        switch (meta.colorspace) {
        case FPDF_COLORSPACE_DEVICEGRAY:
          data_.colorspace = "deviceGray";
          break;
        case FPDF_COLORSPACE_DEVICERGB:
          data_.colorspace = "deviceRGB";
          break;
        case FPDF_COLORSPACE_DEVICECMYK:
          data_.colorspace = "deviceCMYK";
          break;
        case FPDF_COLORSPACE_CALGRAY:
          data_.colorspace = "calGray";
          break;
        case FPDF_COLORSPACE_CALRGB:
          data_.colorspace = "calRGB";
          break;
        case FPDF_COLORSPACE_LAB:
          data_.colorspace = "lab";
          break;
        case FPDF_COLORSPACE_ICCBASED:
          data_.colorspace = "iccBased";
          break;
        case FPDF_COLORSPACE_SEPARATION:
          data_.colorspace = "separation";
          break;
        case FPDF_COLORSPACE_DEVICEN:
          data_.colorspace = "deviceN";
          break;
        case FPDF_COLORSPACE_INDEXED:
          data_.colorspace = "indexed";
          break;
        case FPDF_COLORSPACE_PATTERN:
          data_.colorspace = "pattern";
          break;
        default:
          data_.colorspace = "unknown";
          break;
        }
      }

      // compression filters
      int filterCount = FPDFImageObj_GetImageFilterCount(obj);
      for (int fi = 0; fi < filterCount; fi++) {
        unsigned long fLen = FPDFImageObj_GetImageFilter(obj, fi, nullptr, 0);
        if (fLen > 1) {
          std::vector<char> fBuf(fLen);
          FPDFImageObj_GetImageFilter(obj, fi, fBuf.data(), fLen);
          data_.filters.emplace_back(fBuf.data(), fLen - 1);
        }
      }
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object result = Napi::Object::New(env);
    result.Set("type", Napi::String::New(env, data_.type));
    result.Set("bounds", CreateBoundsObject(env, data_.left, data_.bottom,
                                            data_.right, data_.top));

    if (data_.hasFillColor) {
      result.Set("fillColor", CreateColorObject(env, data_.fillR, data_.fillG,
                                                data_.fillB, data_.fillA));
    } else {
      result.Set("fillColor", env.Null());
    }

    if (data_.hasStrokeColor) {
      result.Set("strokeColor",
                 CreateColorObject(env, data_.strokeR, data_.strokeG,
                                   data_.strokeB, data_.strokeA));
    } else {
      result.Set("strokeColor", env.Null());
    }

    if (data_.type == "text") {
      SetU16(result, "text", env, data_.text);
      result.Set("fontSize", Napi::Number::New(env, data_.fontSize));
      result.Set("fontName", Napi::String::New(env, data_.fontName));
      if (data_.fontWeight >= 0)
        result.Set("fontWeight", Napi::Number::New(env, data_.fontWeight));
      if (data_.hasItalicAngle)
        result.Set("italicAngle", Napi::Number::New(env, data_.italicAngle));
      if (!data_.renderMode.empty())
        result.Set("renderMode", Napi::String::New(env, data_.renderMode));
      if (!data_.fontFamily.empty())
        result.Set("fontFamily", Napi::String::New(env, data_.fontFamily));
      if (data_.isEmbedded >= 0)
        result.Set("isEmbedded",
                   Napi::Boolean::New(env, data_.isEmbedded != 0));
      if (data_.fontFlags >= 0)
        result.Set("fontFlags", Napi::Number::New(env, data_.fontFlags));
    }

    if (data_.type == "image") {
      result.Set("imageWidth", Napi::Number::New(env, data_.imageWidth));
      result.Set("imageHeight", Napi::Number::New(env, data_.imageHeight));
      if (data_.hasImageMetadata) {
        result.Set("horizontalDpi",
                   Napi::Number::New(env, data_.horizontalDpi));
        result.Set("verticalDpi", Napi::Number::New(env, data_.verticalDpi));
        result.Set("bitsPerPixel", Napi::Number::New(env, data_.bitsPerPixel));
        if (!data_.colorspace.empty())
          result.Set("colorspace", Napi::String::New(env, data_.colorspace));
      }
      if (!data_.filters.empty()) {
        Napi::Array fArr = Napi::Array::New(env, data_.filters.size());
        for (uint32_t fi = 0; fi < data_.filters.size(); fi++)
          fArr.Set(fi, Napi::String::New(env, data_.filters[fi]));
        result.Set("filters", fArr);
      }
    }

    deferred_.Resolve(result);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  int index_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  PageObjectData data_;
};

// ---------------------------------------------------------------------------
// GetBookmarksWorker — async bookmark extraction
// ---------------------------------------------------------------------------

struct BookmarkData {
  std::u16string title;
  int pageIndex = -1;
  bool hasPageIndex = false;
  bool open = false;
  std::string actionType;
  std::string url;
  float destX = 0, destY = 0, destZoom = 0;
  bool hasDestX = false, hasDestY = false, hasDestZoom = false;
  std::vector<BookmarkData> children;
};

class GetBookmarksWorker : public Napi::AsyncWorker {
public:
  GetBookmarksWorker(Napi::Env env, FPDF_DOCUMENT doc,
                     std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        doc_(doc), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    if (!docAlive_->load()) {
      SetError("Document was destroyed");
      return;
    }
    CollectBookmarks(nullptr, bookmarks_, 0);
  }

  void OnOK() override { deferred_.Resolve(BuildArray(Env(), bookmarks_)); }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<BookmarkData> bookmarks_;

  void CollectBookmarks(FPDF_BOOKMARK parent, std::vector<BookmarkData> &out,
                        int depth) {
    if (depth >= MAX_BOOKMARK_DEPTH)
      return;

    FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc_, parent);
    while (child) {
      BookmarkData data;

      data.title = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFBookmark_GetTitle(child, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFBookmark_GetTitle(child, buf, len);
          });

      // open/closed state: positive count = open, negative = closed
      int count = FPDFBookmark_GetCount(child);
      data.open = count > 0;

      FPDF_DEST dest = FPDFBookmark_GetDest(doc_, child);
      FPDF_ACTION action = FPDFBookmark_GetAction(child);

      if (!dest && action) {
        dest = FPDFAction_GetDest(doc_, action);
      }

      if (dest) {
        data.hasPageIndex = true;
        data.pageIndex = FPDFDest_GetDestPageIndex(doc_, dest);

        FPDF_BOOL hasX, hasY, hasZoom;
        FS_FLOAT x, y, zoom;
        if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y,
                                       &zoom)) {
          if (hasX) {
            data.hasDestX = true;
            data.destX = x;
          }
          if (hasY) {
            data.hasDestY = true;
            data.destY = y;
          }
          if (hasZoom) {
            data.hasDestZoom = true;
            data.destZoom = zoom;
          }
        }
      }

      if (action) {
        unsigned long actionType = FPDFAction_GetType(action);
        data.actionType = ActionTypeString(actionType);

        if (actionType == PDFACTION_URI) {
          unsigned long uriLen =
              FPDFAction_GetURIPath(doc_, action, nullptr, 0);
          if (uriLen > 0) {
            std::vector<char> uriBuf(uriLen);
            FPDFAction_GetURIPath(doc_, action, uriBuf.data(), uriLen);
            data.url = std::string(uriBuf.data(), uriLen - 1);
          }
        }
      }

      CollectBookmarks(child, data.children, depth + 1);
      out.push_back(std::move(data));
      child = FPDFBookmark_GetNextSibling(doc_, child);
    }
  }

  Napi::Array BuildArray(Napi::Env env,
                         const std::vector<BookmarkData> &items) {
    Napi::Array arr = Napi::Array::New(env, items.size());
    for (uint32_t i = 0; i < items.size(); i++) {
      auto &d = items[i];
      Napi::Object obj = Napi::Object::New(env);
      SetU16(obj, "title", env, d.title);
      if (d.hasPageIndex)
        obj.Set("pageIndex", Napi::Number::New(env, d.pageIndex));
      obj.Set("open", Napi::Boolean::New(env, d.open));
      if (!d.actionType.empty())
        obj.Set("actionType", Napi::String::New(env, d.actionType));
      if (!d.url.empty())
        obj.Set("url", Napi::String::New(env, d.url));
      if (d.hasDestX)
        obj.Set("destX", Napi::Number::New(env, d.destX));
      if (d.hasDestY)
        obj.Set("destY", Napi::Number::New(env, d.destY));
      if (d.hasDestZoom)
        obj.Set("destZoom", Napi::Number::New(env, d.destZoom));
      if (!d.children.empty())
        obj.Set("children", BuildArray(env, d.children));
      arr.Set(i, obj);
    }
    return arr;
  }
};

// ---------------------------------------------------------------------------
// GetFormFieldsWorker — async form field extraction
// ---------------------------------------------------------------------------

struct FormFieldOptionData {
  std::u16string label;
  bool isSelected = false;
};

struct FormFieldData {
  std::string type;
  std::u16string name;
  std::u16string value;
  std::u16string alternateName;
  std::u16string exportValue;
  int flags = 0;
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  bool isChecked = false;
  std::vector<FormFieldOptionData> options;
};

class GetFormFieldsWorker : public Napi::AsyncWorker {
public:
  GetFormFieldsWorker(Napi::Env env, FPDF_PAGE page, FPDF_DOCUMENT doc,
                      std::shared_ptr<std::atomic<bool>> pageAlive,
                      std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), doc_(doc), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    // create a temporary form fill environment for read-only field access
    FPDF_FORMFILLINFO formFillInfo{};
    formFillInfo.version = 1;
    FPDF_FORMHANDLE formHandle =
        FPDFDOC_InitFormFillEnvironment(doc_, &formFillInfo);
    if (!formHandle) {
      return;
    }

    int count = FPDFPage_GetAnnotCount(page_);
    for (int i = 0; i < count; i++) {
      FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page_, i);
      if (!annot)
        continue;

      // only process widget annotations (form fields)
      if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_WIDGET) {
        FPDFPage_CloseAnnot(annot);
        continue;
      }

      FormFieldData data;

      // field type
      int fieldType = FPDFAnnot_GetFormFieldType(formHandle, annot);
      switch (fieldType) {
      case FPDF_FORMFIELD_PUSHBUTTON:
        data.type = "pushButton";
        break;
      case FPDF_FORMFIELD_CHECKBOX:
        data.type = "checkbox";
        break;
      case FPDF_FORMFIELD_RADIOBUTTON:
        data.type = "radioButton";
        break;
      case FPDF_FORMFIELD_COMBOBOX:
        data.type = "comboBox";
        break;
      case FPDF_FORMFIELD_LISTBOX:
        data.type = "listBox";
        break;
      case FPDF_FORMFIELD_TEXTFIELD:
        data.type = "textField";
        break;
      case FPDF_FORMFIELD_SIGNATURE:
        data.type = "signature";
        break;
      default:
        data.type = "unknown";
        break;
      }

      // field name (UTF-16LE)
      data.name = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldName(formHandle, annot, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldName(formHandle, annot, buf, len);
          });

      // field value (UTF-16LE)
      data.value = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldValue(formHandle, annot, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldValue(formHandle, annot, buf, len);
          });

      // alternate name (tooltip)
      data.alternateName = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldAlternateName(formHandle, annot,
                                                       nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldAlternateName(formHandle, annot, buf,
                                                       len);
          });

      // export value (for checkboxes and radio buttons)
      data.exportValue = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldExportValue(formHandle, annot, nullptr,
                                                     0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldExportValue(formHandle, annot, buf,
                                                     len);
          });

      // field flags
      data.flags = FPDFAnnot_GetFormFieldFlags(formHandle, annot);

      // bounds
      FS_RECTF rect;
      if (FPDFAnnot_GetRect(annot, &rect)) {
        data.hasBounds = true;
        data.left = rect.left;
        data.bottom = rect.bottom;
        data.right = rect.right;
        data.top = rect.top;
      }

      // checked state — derive from value/exportValue to avoid needing
      // FORM_OnAfterLoadPage (which conflicts with page lifecycle)
      if (fieldType == FPDF_FORMFIELD_CHECKBOX) {
        // checkbox is checked when its value is not "Off"
        static const std::u16string off = u"Off";
        data.isChecked = !data.value.empty() && data.value != off;
      } else if (fieldType == FPDF_FORMFIELD_RADIOBUTTON) {
        // radio is checked when its appearance state (/AS) is not "Off"
        static const std::u16string off = u"Off";
        std::u16string as = ReadU16(
            [&](auto *, unsigned long) {
              return FPDFAnnot_GetStringValue(annot, "AS", nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFAnnot_GetStringValue(annot, "AS", buf, len);
            });
        data.isChecked = !as.empty() && as != off;
      }

      // options (combo box / list box)
      int optCount = FPDFAnnot_GetOptionCount(formHandle, annot);
      if (optCount > 0) {
        for (int j = 0; j < optCount; j++) {
          FormFieldOptionData opt;
          opt.label = ReadU16(
              [&](auto *, unsigned long) {
                return FPDFAnnot_GetOptionLabel(formHandle, annot, j, nullptr,
                                                0);
              },
              [&](FPDF_WCHAR *buf, unsigned long len) {
                return FPDFAnnot_GetOptionLabel(formHandle, annot, j, buf, len);
              });
          opt.isSelected =
              FPDFAnnot_IsOptionSelected(formHandle, annot, j) != 0;
          data.options.push_back(std::move(opt));
        }
      }

      fields_.push_back(std::move(data));
      FPDFPage_CloseAnnot(annot);
    }

    FPDFDOC_ExitFormFillEnvironment(formHandle);
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, fields_.size());
    for (uint32_t i = 0; i < fields_.size(); i++) {
      auto &d = fields_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("type", Napi::String::New(env, d.type));
      SetU16(obj, "name", env, d.name);
      SetU16(obj, "value", env, d.value);
      SetU16IfPresent(obj, "alternateName", env, d.alternateName);
      SetU16IfPresent(obj, "exportValue", env, d.exportValue);
      obj.Set("flags", Napi::Number::New(env, d.flags));

      if (d.hasBounds)
        obj.Set("bounds",
                CreateBoundsObject(env, d.left, d.bottom, d.right, d.top));

      obj.Set("isChecked", Napi::Boolean::New(env, d.isChecked));

      if (!d.options.empty()) {
        Napi::Array optArr = Napi::Array::New(env, d.options.size());
        for (uint32_t j = 0; j < d.options.size(); j++) {
          auto &opt = d.options[j];
          Napi::Object optObj = Napi::Object::New(env);
          SetU16(optObj, "label", env, opt.label);
          optObj.Set("isSelected", Napi::Boolean::New(env, opt.isSelected));
          optArr.Set(j, optObj);
        }
        obj.Set("options", optArr);
      }

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
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<FormFieldData> fields_;
};
