#pragma once

#include "render_worker.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>

#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"

// forward declarations
class GetPageWorker;
class LoadDocumentWorker;

// ---------------------------------------------------------------------------
// PDFiumPage
// ---------------------------------------------------------------------------

class PDFiumPage : public Napi::ObjectWrap<PDFiumPage> {
public:
  static Napi::Function Init(Napi::Env env) {
    return DefineClass(
        env, "PDFiumPage",
        {
            InstanceAccessor<&PDFiumPage::GetNumber>("number"),
            InstanceMethod<&PDFiumPage::GetText>("getText"),
            InstanceMethod<&PDFiumPage::RenderAsync>("render"),
            InstanceMethod<&PDFiumPage::GetObject>("getObject"),
            InstanceMethod<&PDFiumPage::GetLinks>("getLinks"),
            InstanceMethod<&PDFiumPage::Search>("search"),
            InstanceMethod<&PDFiumPage::GetAnnotations>("getAnnotations"),
            InstanceMethod<&PDFiumPage::Close>("close"),
        });
  }

  PDFiumPage(const Napi::CallbackInfo &info)
      : Napi::ObjectWrap<PDFiumPage>(info),
        alive_(std::make_shared<std::atomic<bool>>(true)) {}

  void SetPage(FPDF_PAGE page, FPDF_DOCUMENT doc, int index) {
    page_ = page;
    doc_ = doc;
    index_ = index;
  }

  // called by document to give this page a reference to the document's alive
  // flag so we can detect doc.destroy() from page methods
  void SetDocAlive(std::shared_ptr<std::atomic<bool>> docAlive) {
    docAlive_ = std::move(docAlive);
  }

  // returns the page alive flag (shared with RenderWorker and document)
  std::shared_ptr<std::atomic<bool>> GetAliveFlag() const { return alive_; }

private:
  FPDF_PAGE page_ = nullptr;
  FPDF_DOCUMENT doc_ = nullptr;
  int index_ = -1;
  std::shared_ptr<std::atomic<bool>> alive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;

  void EnsureOpen(Napi::Env env) {
    if (!alive_->load()) {
      Napi::Error::New(env, "Page is closed").ThrowAsJavaScriptException();
      return;
    }
    if (docAlive_ && !docAlive_->load()) {
      Napi::Error::New(env, "Document is destroyed")
          .ThrowAsJavaScriptException();
      return;
    }
  }

  /**
   * Returns the 0-based page index.
   */
  Napi::Value GetNumber(const Napi::CallbackInfo &info) {
    return Napi::Number::New(info.Env(), index_);
  }

  /**
   * Extracts all text from the page.
   */
  Napi::Value GetText(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
    if (!textPage) {
      Napi::Error::New(env, "Failed to load text page")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    int charCount = FPDFText_CountChars(textPage);
    if (charCount <= 0) {
      FPDFText_ClosePage(textPage);
      return Napi::String::New(env, "");
    }

    int bufLen = charCount + 1;
    std::vector<unsigned short> textBuf(bufLen);
    FPDFText_GetText(textPage, 0, charCount, textBuf.data());
    FPDFText_ClosePage(textPage);

    return Napi::String::New(
        env, reinterpret_cast<const char16_t *>(textBuf.data()), charCount);
  }

  /**
   * Renders the page to a JPEG or PNG buffer (async).
   */
  Napi::Value RenderAsync(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    float origWidth = FPDF_GetPageWidthF(page_);
    float origHeight = FPDF_GetPageHeightF(page_);
    double scale = 1.0;
    int renderWidth = 0;
    int renderHeight = 0;
    int format = IMAGE_FORMAT_JPEG;
    int quality = 100;

    std::string outputPath;

    if (info.Length() > 0 && info[0].IsObject()) {
      Napi::Object opts = info[0].As<Napi::Object>();
      if (opts.Has("scale")) {
        scale = opts.Get("scale").As<Napi::Number>().DoubleValue();
        if (scale <= 0) {
          Napi::RangeError::New(env, "scale must be positive")
              .ThrowAsJavaScriptException();
          return env.Null();
        }
      }
      if (opts.Has("width"))
        renderWidth = opts.Get("width").As<Napi::Number>().Int32Value();
      if (opts.Has("height"))
        renderHeight = opts.Get("height").As<Napi::Number>().Int32Value();
      if (opts.Has("format")) {
        std::string fmt = opts.Get("format").As<Napi::String>().Utf8Value();
        if (fmt == "png")
          format = IMAGE_FORMAT_PNG;
      }
      if (opts.Has("quality"))
        quality = opts.Get("quality").As<Napi::Number>().Int32Value();
      if (opts.Has("output"))
        outputPath = opts.Get("output").As<Napi::String>().Utf8Value();
    }

    if (renderWidth == 0)
      renderWidth = static_cast<int>(origWidth * scale);
    if (renderHeight == 0)
      renderHeight = static_cast<int>(origHeight * scale);

    // validate dimensions
    if (renderWidth <= 0 || renderHeight <= 0) {
      Napi::RangeError::New(env, "Render dimensions must be positive")
          .ThrowAsJavaScriptException();
      return env.Null();
    }
    if (renderWidth > MAX_RENDER_DIMENSION ||
        renderHeight > MAX_RENDER_DIMENSION) {
      Napi::RangeError::New(env, "Render dimensions exceed maximum (" +
                                     std::to_string(MAX_RENDER_DIMENSION) + ")")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    // clamp quality to valid range
    quality = std::clamp(quality, 1, 100);

    auto *worker =
        new RenderWorker(env, page_, renderWidth, renderHeight, format, quality,
                         std::move(outputPath), alive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Returns a page object at the given index with type and bounds.
   */
  Napi::Value GetObject(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    if (info.Length() < 1 || !info[0].IsNumber()) {
      Napi::TypeError::New(env, "Expected object index")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    int idx = info[0].As<Napi::Number>().Int32Value();
    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page_, idx);
    if (!obj) {
      Napi::Error::New(env, "Object not found").ThrowAsJavaScriptException();
      return env.Null();
    }

    int type = FPDFPageObj_GetType(obj);
    const char *typeName;
    switch (type) {
    case FPDF_PAGEOBJ_TEXT:
      typeName = "text";
      break;
    case FPDF_PAGEOBJ_PATH:
      typeName = "path";
      break;
    case FPDF_PAGEOBJ_IMAGE:
      typeName = "image";
      break;
    case FPDF_PAGEOBJ_SHADING:
      typeName = "shading";
      break;
    case FPDF_PAGEOBJ_FORM:
      typeName = "form";
      break;
    default:
      typeName = "unknown";
      break;
    }

    float left = 0, bottom = 0, right = 0, top = 0;
    FPDFPageObj_GetBounds(obj, &left, &bottom, &right, &top);

    Napi::Object result = Napi::Object::New(env);
    result.Set("type", Napi::String::New(env, typeName));
    result.Set("bounds", CreateBoundsObject(env, left, bottom, right, top));

    // fill and stroke colors (all object types)
    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a)) {
      result.Set("fillColor", CreateColorObject(env, r, g, b, a));
    } else {
      result.Set("fillColor", env.Null());
    }

    if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a)) {
      result.Set("strokeColor", CreateColorObject(env, r, g, b, a));
    } else {
      result.Set("strokeColor", env.Null());
    }

    // text-specific properties
    if (type == FPDF_PAGEOBJ_TEXT) {
      // text content
      FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
      if (textPage) {
        unsigned long byteLen = FPDFTextObj_GetText(obj, textPage, nullptr, 0);
        if (byteLen > 0) {
          size_t charCount = byteLen / sizeof(unsigned short);
          std::vector<unsigned short> textBuf(charCount);
          FPDFTextObj_GetText(obj, textPage, textBuf.data(), byteLen);
          result.Set("text",
                     Napi::String::New(
                         env,
                         reinterpret_cast<const char16_t *>(textBuf.data()),
                         charCount - 1));
        } else {
          result.Set("text", Napi::String::New(env, ""));
        }
        FPDFText_ClosePage(textPage);
      } else {
        result.Set("text", Napi::String::New(env, ""));
      }

      // font size
      float fontSize = 0;
      if (FPDFTextObj_GetFontSize(obj, &fontSize)) {
        result.Set("fontSize", Napi::Number::New(env, fontSize));
      } else {
        result.Set("fontSize", Napi::Number::New(env, 0));
      }

      // font name
      FPDF_FONT font = FPDFTextObj_GetFont(obj);
      if (font) {
        size_t nameLen = FPDFFont_GetBaseFontName(font, nullptr, 0);
        if (nameLen > 1) {
          std::vector<char> nameBuf(nameLen);
          FPDFFont_GetBaseFontName(font, nameBuf.data(), nameLen);
          result.Set("fontName",
                     Napi::String::New(env, nameBuf.data(), nameLen - 1));
        } else {
          result.Set("fontName", Napi::String::New(env, ""));
        }
      } else {
        result.Set("fontName", Napi::String::New(env, ""));
      }
    }

    // image-specific properties
    if (type == FPDF_PAGEOBJ_IMAGE) {
      unsigned int imgWidth = 0, imgHeight = 0;
      if (FPDFImageObj_GetImagePixelSize(obj, &imgWidth, &imgHeight)) {
        result.Set("imageWidth", Napi::Number::New(env, imgWidth));
        result.Set("imageHeight", Napi::Number::New(env, imgHeight));
      } else {
        result.Set("imageWidth", Napi::Number::New(env, 0));
        result.Set("imageHeight", Napi::Number::New(env, 0));
      }
    }

    return result;
  }

  /**
   * Returns all links on the page with URL/destination and bounds.
   */
  Napi::Value GetLinks(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    Napi::Array links = Napi::Array::New(env);
    int pos = 0;
    FPDF_LINK link;
    uint32_t idx = 0;

    while (FPDFLink_Enumerate(page_, &pos, &link)) {
      Napi::Object obj = Napi::Object::New(env);

      // bounds
      FS_RECTF rect;
      if (FPDFLink_GetAnnotRect(link, &rect)) {
        obj.Set("bounds", CreateBoundsFromRect(env, rect));
      }

      // try URI action first
      FPDF_ACTION action = FPDFLink_GetAction(link);
      if (action && FPDFAction_GetType(action) == PDFACTION_URI) {
        unsigned long len = FPDFAction_GetURIPath(doc_, action, nullptr, 0);
        if (len > 0) {
          std::vector<char> buf(len);
          FPDFAction_GetURIPath(doc_, action, buf.data(), len);
          obj.Set("url", Napi::String::New(env, buf.data(), len - 1));
        }
      }

      // try destination (internal link)
      FPDF_DEST dest = FPDFLink_GetDest(doc_, link);
      if (!dest && action) {
        dest = FPDFAction_GetDest(doc_, action);
      }
      if (dest) {
        int pageIndex = FPDFDest_GetDestPageIndex(doc_, dest);
        if (pageIndex >= 0) {
          obj.Set("pageIndex", Napi::Number::New(env, pageIndex));
        }
      }

      links.Set(idx++, obj);
    }
    return links;
  }

  /**
   * Searches for text on the page. Returns array of matches with charIndex and
   * bounds.
   */
  Napi::Value Search(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    if (info.Length() < 1 || !info[0].IsString()) {
      Napi::TypeError::New(env, "Expected search string")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    std::u16string needle = info[0].As<Napi::String>().Utf16Value();
    unsigned long flags = 0;
    if (info.Length() > 1 && info[1].IsObject()) {
      Napi::Object opts = info[1].As<Napi::Object>();
      if (opts.Has("caseSensitive") &&
          opts.Get("caseSensitive").As<Napi::Boolean>().Value())
        flags |= FPDF_MATCHCASE;
      if (opts.Has("wholeWord") &&
          opts.Get("wholeWord").As<Napi::Boolean>().Value())
        flags |= FPDF_MATCHWHOLEWORD;
    }

    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
    if (!textPage) {
      return Napi::Array::New(env, 0);
    }

    FPDF_SCHHANDLE handle = FPDFText_FindStart(
        textPage, reinterpret_cast<const unsigned short *>(needle.c_str()),
        flags, 0);

    Napi::Array results = Napi::Array::New(env);
    uint32_t idx = 0;

    if (handle) {
      while (FPDFText_FindNext(handle)) {
        int charIdx = FPDFText_GetSchResultIndex(handle);
        int count = FPDFText_GetSchCount(handle);

        Napi::Object match = Napi::Object::New(env);
        match.Set("charIndex", Napi::Number::New(env, charIdx));
        match.Set("length", Napi::Number::New(env, count));

        // get bounding rectangles for the match
        int numRects = FPDFText_CountRects(textPage, charIdx, count);
        Napi::Array rects = Napi::Array::New(env, numRects > 0 ? numRects : 0);
        for (int r = 0; r < numRects; r++) {
          double left, top, right, bottom;
          if (FPDFText_GetRect(textPage, r, &left, &top, &right, &bottom)) {
            rects.Set(static_cast<uint32_t>(r),
                      CreateBoundsObject(env, left, bottom, right, top));
          }
        }
        match.Set("rects", rects);

        results.Set(idx++, match);
      }
      FPDFText_FindClose(handle);
    }

    FPDFText_ClosePage(textPage);
    return results;
  }

  /**
   * Returns all annotations on the page.
   */
  Napi::Value GetAnnotations(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    int count = FPDFPage_GetAnnotCount(page_);
    Napi::Array annotations = Napi::Array::New(env, count > 0 ? count : 0);

    for (int i = 0; i < count; i++) {
      FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page_, i);
      if (!annot)
        continue;

      Napi::Object obj = Napi::Object::New(env);

      // subtype
      FPDF_ANNOTATION_SUBTYPE subtype = FPDFAnnot_GetSubtype(annot);
      const char *typeName;
      switch (subtype) {
      case FPDF_ANNOT_TEXT:
        typeName = "text";
        break;
      case FPDF_ANNOT_LINK:
        typeName = "link";
        break;
      case FPDF_ANNOT_FREETEXT:
        typeName = "freetext";
        break;
      case FPDF_ANNOT_LINE:
        typeName = "line";
        break;
      case FPDF_ANNOT_SQUARE:
        typeName = "square";
        break;
      case FPDF_ANNOT_CIRCLE:
        typeName = "circle";
        break;
      case FPDF_ANNOT_HIGHLIGHT:
        typeName = "highlight";
        break;
      case FPDF_ANNOT_UNDERLINE:
        typeName = "underline";
        break;
      case FPDF_ANNOT_SQUIGGLY:
        typeName = "squiggly";
        break;
      case FPDF_ANNOT_STRIKEOUT:
        typeName = "strikeout";
        break;
      case FPDF_ANNOT_STAMP:
        typeName = "stamp";
        break;
      case FPDF_ANNOT_INK:
        typeName = "ink";
        break;
      case FPDF_ANNOT_POPUP:
        typeName = "popup";
        break;
      case FPDF_ANNOT_WIDGET:
        typeName = "widget";
        break;
      case FPDF_ANNOT_REDACT:
        typeName = "redact";
        break;
      default:
        typeName = "unknown";
        break;
      }
      obj.Set("type", Napi::String::New(env, typeName));

      // bounds
      FS_RECTF rect;
      if (FPDFAnnot_GetRect(annot, &rect)) {
        obj.Set("bounds", CreateBoundsFromRect(env, rect));
      }

      // contents (the /Contents key)
      unsigned long contentsLen =
          FPDFAnnot_GetStringValue(annot, "Contents", nullptr, 0);
      if (contentsLen > 2) {
        std::vector<unsigned short> contentsBuf(contentsLen /
                                                sizeof(unsigned short));
        FPDFAnnot_GetStringValue(
            annot, "Contents",
            reinterpret_cast<FPDF_WCHAR *>(contentsBuf.data()), contentsLen);
        size_t charCount = contentsLen / sizeof(unsigned short) - 1;
        obj.Set("contents",
                Napi::String::New(
                    env, reinterpret_cast<const char16_t *>(contentsBuf.data()),
                    charCount));
      } else {
        obj.Set("contents", Napi::String::New(env, ""));
      }

      // color
      unsigned int r, g, b, a;
      if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_Color, &r, &g, &b,
                             &a)) {
        obj.Set("color", CreateColorObject(env, r, g, b, a));
      } else {
        obj.Set("color", env.Null());
      }

      annotations.Set(static_cast<uint32_t>(i), obj);
      FPDFPage_CloseAnnot(annot);
    }
    return annotations;
  }

  /**
   * Closes the page and releases its resources.
   */
  Napi::Value Close(const Napi::CallbackInfo &info) {
    if (page_) {
      alive_->store(false);
      std::lock_guard<std::mutex> lock(g_pdfium_mutex);
      FPDF_ClosePage(page_);
      page_ = nullptr;
      doc_ = nullptr;
    }
    return info.Env().Undefined();
  }
};
