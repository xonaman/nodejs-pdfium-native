#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"

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
    if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {
      SetError("Page or document was closed");
      return;
    }

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
    if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {
      SetError("Page or document was closed");
      return;
    }

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
    if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {
      SetError("Page or document was closed");
      return;
    }

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
      if (action && FPDFAction_GetType(action) == PDFACTION_URI) {
        unsigned long len = FPDFAction_GetURIPath(doc_, action, nullptr, 0);
        if (len > 0) {
          std::vector<char> buf(len);
          FPDFAction_GetURIPath(doc_, action, buf.data(), len);
          data.url = std::string(buf.data(), len - 1);
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
    if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {
      SetError("Page or document was closed");
      return;
    }

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
      case FPDF_ANNOT_INK:
        data.type = "ink";
        break;
      case FPDF_ANNOT_POPUP:
        data.type = "popup";
        break;
      case FPDF_ANNOT_WIDGET:
        data.type = "widget";
        break;
      case FPDF_ANNOT_REDACT:
        data.type = "redact";
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

      unsigned long contentsLen =
          FPDFAnnot_GetStringValue(annot, "Contents", nullptr, 0);
      if (contentsLen > 2) {
        std::vector<unsigned short> contentsBuf(contentsLen /
                                                sizeof(unsigned short));
        FPDFAnnot_GetStringValue(
            annot, "Contents",
            reinterpret_cast<FPDF_WCHAR *>(contentsBuf.data()), contentsLen);
        size_t charCount = contentsLen / sizeof(unsigned short) - 1;
        data.contents = std::u16string(
            reinterpret_cast<const char16_t *>(contentsBuf.data()), charCount);
      }

      unsigned int cr, cg, cb, ca;
      if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_Color, &cr, &cg, &cb,
                             &ca)) {
        data.hasColor = true;
        data.r = cr;
        data.g = cg;
        data.b = cb;
        data.a = ca;
      }

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
      if (d.contents.empty()) {
        obj.Set("contents", Napi::String::New(env, ""));
      } else {
        obj.Set("contents",
                Napi::String::New(
                    env, reinterpret_cast<const char16_t *>(d.contents.data()),
                    d.contents.size()));
      }
      if (d.hasColor) {
        obj.Set("color", CreateColorObject(env, d.r, d.g, d.b, d.a));
      } else {
        obj.Set("color", env.Null());
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
  // image-specific
  unsigned int imageWidth = 0, imageHeight = 0;
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
    if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {
      SetError("Page or document was closed");
      return;
    }

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
        unsigned long byteLen = FPDFTextObj_GetText(obj, textPage, nullptr, 0);
        if (byteLen > 0) {
          size_t charCount = byteLen / sizeof(unsigned short);
          std::vector<unsigned short> textBuf(charCount);
          FPDFTextObj_GetText(obj, textPage, textBuf.data(), byteLen);
          data_.text =
              std::u16string(reinterpret_cast<const char16_t *>(textBuf.data()),
                             charCount - 1);
        }
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
      }
    }

    if (type == FPDF_PAGEOBJ_IMAGE) {
      FPDFImageObj_GetImagePixelSize(obj, &data_.imageWidth,
                                     &data_.imageHeight);
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
      if (data_.text.empty()) {
        result.Set("text", Napi::String::New(env, ""));
      } else {
        result.Set("text", Napi::String::New(env,
                                             reinterpret_cast<const char16_t *>(
                                                 data_.text.data()),
                                             data_.text.size()));
      }
      result.Set("fontSize", Napi::Number::New(env, data_.fontSize));
      result.Set("fontName", Napi::String::New(env, data_.fontName));
    }

    if (data_.type == "image") {
      result.Set("imageWidth", Napi::Number::New(env, data_.imageWidth));
      result.Set("imageHeight", Napi::Number::New(env, data_.imageHeight));
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

      unsigned long titleLen = FPDFBookmark_GetTitle(child, nullptr, 0);
      if (titleLen > 2) {
        std::vector<unsigned short> titleBuf(titleLen / sizeof(unsigned short));
        FPDFBookmark_GetTitle(child, titleBuf.data(), titleLen);
        size_t charCount = titleLen / sizeof(unsigned short) - 1;
        data.title = std::u16string(
            reinterpret_cast<const char16_t *>(titleBuf.data()), charCount);
      }

      FPDF_DEST dest = FPDFBookmark_GetDest(doc_, child);
      if (dest) {
        data.hasPageIndex = true;
        data.pageIndex = FPDFDest_GetDestPageIndex(doc_, dest);
      } else {
        FPDF_ACTION action = FPDFBookmark_GetAction(child);
        if (action) {
          FPDF_DEST actionDest = FPDFAction_GetDest(doc_, action);
          if (actionDest) {
            data.hasPageIndex = true;
            data.pageIndex = FPDFDest_GetDestPageIndex(doc_, actionDest);
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
      if (d.title.empty()) {
        obj.Set("title", Napi::String::New(env, ""));
      } else {
        obj.Set("title",
                Napi::String::New(
                    env, reinterpret_cast<const char16_t *>(d.title.data()),
                    d.title.size()));
      }
      if (d.hasPageIndex)
        obj.Set("pageIndex", Napi::Number::New(env, d.pageIndex));
      if (!d.children.empty())
        obj.Set("children", BuildArray(env, d.children));
      arr.Set(i, obj);
    }
    return arr;
  }
};
