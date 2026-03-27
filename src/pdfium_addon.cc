#include <cstdio>
#include <mutex>
#include <napi.h>
#include <string>
#include <vector>

#include "fpdf_annot.h"
#include "fpdf_doc.h"
#include "fpdf_edit.h"
#include "fpdf_text.h"
#include "fpdfview.h"
#include "stb_image_write.h"

static bool g_initialized = false;

// PDFium is not thread-safe — serialize all calls through a global mutex.
// This unblocks the Node.js event loop while waiting for the lock.
static std::mutex g_pdfium_mutex;

// stb write callback — appends bytes to a std::vector
static void stb_write_callback(void *context, void *data, int size) {
  auto *out = static_cast<std::vector<uint8_t> *>(context);
  auto *bytes = static_cast<uint8_t *>(data);
  out->insert(out->end(), bytes, bytes + size);
}

// ---------------------------------------------------------------------------
// RenderWorker — async page rendering (defined before PDFiumPage for use)
// ---------------------------------------------------------------------------

class RenderWorker : public Napi::AsyncWorker {
public:
  // format: 0 = jpeg, 1 = png
  RenderWorker(Napi::Env env, FPDF_PAGE page, int renderWidth, int renderHeight,
               int format, int quality, std::string outputPath)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), renderWidth_(renderWidth), renderHeight_(renderHeight),
        format_(format), quality_(quality), outputPath_(std::move(outputPath)) {
  }

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    FPDF_BITMAP bitmap = FPDFBitmap_Create(renderWidth_, renderHeight_, 0);
    if (!bitmap) {
      SetError("Failed to create bitmap");
      return;
    }

    FPDFBitmap_FillRect(bitmap, 0, 0, renderWidth_, renderHeight_, 0xFFFFFFFF);
    FPDF_RenderPageBitmap(bitmap, page_, 0, 0, renderWidth_, renderHeight_, 0,
                          FPDF_ANNOT | FPDF_PRINTING);

    // convert BGRA → RGB for stb (which expects RGB)
    void *bufferData = FPDFBitmap_GetBuffer(bitmap);
    int stride = FPDFBitmap_GetStride(bitmap);
    std::vector<uint8_t> rgb(renderWidth_ * renderHeight_ * 3);

    for (int y = 0; y < renderHeight_; y++) {
      auto *row = static_cast<uint8_t *>(bufferData) + y * stride;
      for (int x = 0; x < renderWidth_; x++) {
        int srcIdx = x * 4;
        int dstIdx = (y * renderWidth_ + x) * 3;
        rgb[dstIdx + 0] = row[srcIdx + 2]; // R ← BGRA[2]
        rgb[dstIdx + 1] = row[srcIdx + 1]; // G ← BGRA[1]
        rgb[dstIdx + 2] = row[srcIdx + 0]; // B ← BGRA[0]
      }
    }

    FPDFBitmap_Destroy(bitmap);

    // encode to JPEG or PNG
    int ok;
    if (format_ == 1) {
      ok = stbi_write_png_to_func(stb_write_callback, &encodedData_,
                                  renderWidth_, renderHeight_, 3, rgb.data(),
                                  renderWidth_ * 3);
    } else {
      ok = stbi_write_jpg_to_func(stb_write_callback, &encodedData_,
                                  renderWidth_, renderHeight_, 3, rgb.data(),
                                  quality_);
    }

    if (!ok) {
      SetError("Failed to encode image");
      return;
    }

    // write to file if output path was specified
    if (!outputPath_.empty()) {
      FILE *f = fopen(outputPath_.c_str(), "wb");
      if (!f) {
        SetError("Failed to open output file: " + outputPath_);
        return;
      }
      size_t written = fwrite(encodedData_.data(), 1, encodedData_.size(), f);
      fclose(f);
      if (written != encodedData_.size()) {
        SetError("Failed to write output file: " + outputPath_);
        return;
      }
      encodedData_.clear();
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (outputPath_.empty()) {
      auto data = Napi::Buffer<uint8_t>::Copy(env, encodedData_.data(),
                                              encodedData_.size());
      deferred_.Resolve(data);
    } else {
      deferred_.Resolve(env.Undefined());
    }
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  int renderWidth_;
  int renderHeight_;
  int format_;
  int quality_;
  std::string outputPath_;
  std::vector<uint8_t> encodedData_;
};

// ---------------------------------------------------------------------------
// GetPageWorker — async page loading (forward-declared, defined after
// PDFiumDocument)
// ---------------------------------------------------------------------------
class GetPageWorker;

// ---------------------------------------------------------------------------
// LoadDocumentWorker — async document loading (forward-declared, defined after
// PDFiumDocument)
// ---------------------------------------------------------------------------
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
      : Napi::ObjectWrap<PDFiumPage>(info) {}

  void SetPage(FPDF_PAGE page, FPDF_DOCUMENT doc, int index) {
    page_ = page;
    doc_ = doc;
    index_ = index;
  }

private:
  FPDF_PAGE page_ = nullptr;
  FPDF_DOCUMENT doc_ = nullptr;
  int index_ = -1;

  void EnsureOpen(Napi::Env env) {
    if (!page_) {
      Napi::Error::New(env, "Page is closed").ThrowAsJavaScriptException();
    }
  }

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
   * Options: { scale?, width?, height?, format?: 'jpeg'|'png', quality?: number
   * } Returns: Promise<{ data: Buffer, width, height, originalWidth,
   * originalHeight }>
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
    int format = 0; // 0 = jpeg, 1 = png
    int quality = 100;

    std::string outputPath;

    if (info.Length() > 0 && info[0].IsObject()) {
      Napi::Object opts = info[0].As<Napi::Object>();
      if (opts.Has("scale"))
        scale = opts.Get("scale").As<Napi::Number>().DoubleValue();
      if (opts.Has("width"))
        renderWidth = opts.Get("width").As<Napi::Number>().Int32Value();
      if (opts.Has("height"))
        renderHeight = opts.Get("height").As<Napi::Number>().Int32Value();
      if (opts.Has("format")) {
        std::string fmt = opts.Get("format").As<Napi::String>().Utf8Value();
        if (fmt == "png")
          format = 1;
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

    auto *worker = new RenderWorker(env, page_, renderWidth, renderHeight,
                                    format, quality, std::move(outputPath));
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

    Napi::Object bounds = Napi::Object::New(env);
    bounds.Set("left", Napi::Number::New(env, left));
    bounds.Set("bottom", Napi::Number::New(env, bottom));
    bounds.Set("right", Napi::Number::New(env, right));
    bounds.Set("top", Napi::Number::New(env, top));

    Napi::Object result = Napi::Object::New(env);
    result.Set("type", Napi::String::New(env, typeName));
    result.Set("bounds", bounds);

    // fill and stroke colors (all object types)
    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a)) {
      Napi::Object color = Napi::Object::New(env);
      color.Set("r", Napi::Number::New(env, r));
      color.Set("g", Napi::Number::New(env, g));
      color.Set("b", Napi::Number::New(env, b));
      color.Set("a", Napi::Number::New(env, a));
      result.Set("fillColor", color);
    } else {
      result.Set("fillColor", env.Null());
    }

    if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a)) {
      Napi::Object color = Napi::Object::New(env);
      color.Set("r", Napi::Number::New(env, r));
      color.Set("g", Napi::Number::New(env, g));
      color.Set("b", Napi::Number::New(env, b));
      color.Set("a", Napi::Number::New(env, a));
      result.Set("strokeColor", color);
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

    Napi::Array links = Napi::Array::New(env);
    int pos = 0;
    FPDF_LINK link;
    uint32_t idx = 0;

    while (FPDFLink_Enumerate(page_, &pos, &link)) {
      Napi::Object obj = Napi::Object::New(env);

      // bounds
      FS_RECTF rect;
      if (FPDFLink_GetAnnotRect(link, &rect)) {
        Napi::Object bounds = Napi::Object::New(env);
        bounds.Set("left", Napi::Number::New(env, rect.left));
        bounds.Set("bottom", Napi::Number::New(env, rect.bottom));
        bounds.Set("right", Napi::Number::New(env, rect.right));
        bounds.Set("top", Napi::Number::New(env, rect.top));
        obj.Set("bounds", bounds);
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
            Napi::Object rect = Napi::Object::New(env);
            rect.Set("left", Napi::Number::New(env, left));
            rect.Set("top", Napi::Number::New(env, top));
            rect.Set("right", Napi::Number::New(env, right));
            rect.Set("bottom", Napi::Number::New(env, bottom));
            rects.Set(static_cast<uint32_t>(r), rect);
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
        Napi::Object bounds = Napi::Object::New(env);
        bounds.Set("left", Napi::Number::New(env, rect.left));
        bounds.Set("bottom", Napi::Number::New(env, rect.bottom));
        bounds.Set("right", Napi::Number::New(env, rect.right));
        bounds.Set("top", Napi::Number::New(env, rect.top));
        obj.Set("bounds", bounds);
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
        Napi::Object color = Napi::Object::New(env);
        color.Set("r", Napi::Number::New(env, r));
        color.Set("g", Napi::Number::New(env, g));
        color.Set("b", Napi::Number::New(env, b));
        color.Set("a", Napi::Number::New(env, a));
        obj.Set("color", color);
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
      FPDF_ClosePage(page_);
      page_ = nullptr;
    }
    return info.Env().Undefined();
  }

  FPDF_PAGE GetPageHandle() const { return page_; }
};

// ---------------------------------------------------------------------------
// PDFiumDocument
// ---------------------------------------------------------------------------

class PDFiumDocument : public Napi::ObjectWrap<PDFiumDocument> {
public:
  static Napi::Function Init(Napi::Env env) {
    return DefineClass(
        env, "PDFiumDocument",
        {
            InstanceMethod<&PDFiumDocument::GetPageAsync>("getPage"),
            InstanceMethod<&PDFiumDocument::GetBookmarks>("getBookmarks"),
            InstanceMethod<&PDFiumDocument::Destroy>("destroy"),
        });
  }

  PDFiumDocument(const Napi::CallbackInfo &info)
      : Napi::ObjectWrap<PDFiumDocument>(info) {}

  void SetDocument(FPDF_DOCUMENT doc) { doc_ = doc; }

  // take ownership of buffer data (needed for FPDF_LoadMemDocument)
  void SetOwnedBuffer(std::vector<uint8_t> buf) {
    ownedBuffer_ = std::move(buf);
  }

  // store the page constructor so we can create instances
  static Napi::FunctionReference pageConstructor;

private:
  FPDF_DOCUMENT doc_ = nullptr;
  std::vector<uint8_t> ownedBuffer_;

  void EnsureOpen(Napi::Env env) {
    if (!doc_) {
      Napi::Error::New(env, "Document is destroyed")
          .ThrowAsJavaScriptException();
    }
  }

  /**
   * Gets a page by 0-based index (async). Returns a Promise<PDFiumPage>.
   */
  Napi::Value GetPageAsync(const Napi::CallbackInfo &info);

  /**
   * Returns the bookmark tree as a nested array.
   */
  Napi::Value GetBookmarks(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    return BuildBookmarkArray(env, nullptr);
  }

  Napi::Array BuildBookmarkArray(Napi::Env env, FPDF_BOOKMARK parent) {
    Napi::Array arr = Napi::Array::New(env);
    uint32_t idx = 0;
    FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc_, parent);

    while (child) {
      Napi::Object obj = Napi::Object::New(env);

      // title
      unsigned long titleLen = FPDFBookmark_GetTitle(child, nullptr, 0);
      if (titleLen > 2) {
        std::vector<unsigned short> titleBuf(titleLen / sizeof(unsigned short));
        FPDFBookmark_GetTitle(child, titleBuf.data(), titleLen);
        size_t charCount = titleLen / sizeof(unsigned short) - 1;
        obj.Set("title",
                Napi::String::New(
                    env, reinterpret_cast<const char16_t *>(titleBuf.data()),
                    charCount));
      } else {
        obj.Set("title", Napi::String::New(env, ""));
      }

      // destination page index
      FPDF_DEST dest = FPDFBookmark_GetDest(doc_, child);
      if (dest) {
        int pageIndex = FPDFDest_GetDestPageIndex(doc_, dest);
        obj.Set("pageIndex", Napi::Number::New(env, pageIndex));
      } else {
        // try action → dest
        FPDF_ACTION action = FPDFBookmark_GetAction(child);
        if (action) {
          FPDF_DEST actionDest = FPDFAction_GetDest(doc_, action);
          if (actionDest) {
            int pageIndex = FPDFDest_GetDestPageIndex(doc_, actionDest);
            obj.Set("pageIndex", Napi::Number::New(env, pageIndex));
          }
        }
      }

      // children (recursive)
      Napi::Array children = BuildBookmarkArray(env, child);
      if (children.Length() > 0) {
        obj.Set("children", children);
      }

      arr.Set(idx++, obj);
      child = FPDFBookmark_GetNextSibling(doc_, child);
    }
    return arr;
  }

  /**
   * Closes the document and releases all resources.
   */
  Napi::Value Destroy(const Napi::CallbackInfo &info) {
    if (doc_) {
      FPDF_CloseDocument(doc_);
      doc_ = nullptr;
    }
    return info.Env().Undefined();
  }

  FPDF_DOCUMENT GetDocHandle() const { return doc_; }
};

Napi::FunctionReference PDFiumDocument::pageConstructor;

// ---------------------------------------------------------------------------
// GetPageWorker — async page loading
// ---------------------------------------------------------------------------

class GetPageWorker : public Napi::AsyncWorker {
public:
  GetPageWorker(Napi::Env env, FPDF_DOCUMENT doc, int pageIndex)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        doc_(doc), pageIndex_(pageIndex) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    page_ = FPDF_LoadPage(doc_, pageIndex_);
    if (!page_) {
      SetError("Failed to load page");
      return;
    }
    width_ = FPDF_GetPageWidthF(page_);
    height_ = FPDF_GetPageHeightF(page_);
    objectCount_ = FPDFPage_CountObjects(page_);
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object pageObj = PDFiumDocument::pageConstructor.New({});
    PDFiumPage *pageWrapper = PDFiumPage::Unwrap(pageObj);
    pageWrapper->SetPage(page_, doc_, pageIndex_);

    // set dimensions as plain JS properties (no native roundtrip on access)
    pageObj.Set("width", Napi::Number::New(env, width_));
    pageObj.Set("height", Napi::Number::New(env, height_));
    Napi::Object sizeObj = Napi::Object::New(env);
    sizeObj.Set("width", Napi::Number::New(env, width_));
    sizeObj.Set("height", Napi::Number::New(env, height_));
    pageObj.Set("size", sizeObj);
    pageObj.Set("objectCount", Napi::Number::New(env, objectCount_));

    deferred_.Resolve(pageObj);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_DOCUMENT doc_;
  int pageIndex_;
  FPDF_PAGE page_ = nullptr;
  float width_ = 0;
  float height_ = 0;
  int objectCount_ = 0;
};

// deferred definition of GetPageAsync (needs GetPageWorker)
Napi::Value PDFiumDocument::GetPageAsync(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();
  EnsureOpen(env);
  if (env.IsExceptionPending())
    return env.Null();

  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Expected page index")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  int pageIndex = info[0].As<Napi::Number>().Int32Value();
  int numPages = FPDF_GetPageCount(doc_);
  if (pageIndex < 0 || pageIndex >= numPages) {
    Napi::RangeError::New(env, "Page index out of range")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto *worker = new GetPageWorker(env, doc_, pageIndex);
  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}

// ---------------------------------------------------------------------------
// Module-level: loadDocument (async)
// ---------------------------------------------------------------------------

static Napi::FunctionReference g_docConstructor;

static std::string GetPdfiumErrorMessage() {
  unsigned long err = FPDF_GetLastError();
  const char *msg;
  switch (err) {
  case FPDF_ERR_FILE:
    msg = "File not found or could not be opened";
    break;
  case FPDF_ERR_FORMAT:
    msg = "Not a valid PDF or corrupted";
    break;
  case FPDF_ERR_PASSWORD:
    msg = "Password required or incorrect";
    break;
  case FPDF_ERR_SECURITY:
    msg = "Unsupported security scheme";
    break;
  case FPDF_ERR_PAGE:
    msg = "Page error";
    break;
  default:
    msg = "Unknown error";
    break;
  }
  return std::string(msg) + " (error code: " + std::to_string(err) + ")";
}

// ---------------------------------------------------------------------------
// LoadDocumentWorker — async document loading
// ---------------------------------------------------------------------------

class LoadDocumentWorker : public Napi::AsyncWorker {
public:
  // buffer variant — copies data since buffer may be GC'd
  LoadDocumentWorker(Napi::Env env, std::vector<uint8_t> data,
                     std::string password)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  LoadDocumentWorker(Napi::Env env, std::string path, std::string password)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(path)), password_(std::move(password)),
        useFile_(true) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    const char *pw = password_.empty() ? nullptr : password_.c_str();

    if (useFile_) {
      doc_ = FPDF_LoadDocument(filePath_.c_str(), pw);
    } else {
      doc_ = FPDF_LoadMemDocument(bufferData_.data(),
                                  static_cast<int>(bufferData_.size()), pw);
    }

    if (!doc_) {
      SetError(GetPdfiumErrorMessage());
      return;
    }

    pageCount_ = FPDF_GetPageCount(doc_);

    // read metadata under the mutex
    const char *metaTags[] = {"Title",   "Author",   "Subject",      "Keywords",
                              "Creator", "Producer", "CreationDate", "ModDate"};
    for (int i = 0; i < 8; i++) {
      unsigned long len = FPDF_GetMetaText(doc_, metaTags[i], nullptr, 0);
      if (len > 2) {
        std::vector<unsigned short> buf(len / sizeof(unsigned short));
        FPDF_GetMetaText(doc_, metaTags[i], buf.data(), len);
        size_t charCount = len / sizeof(unsigned short) - 1;
        meta_[i] = std::u16string(
            reinterpret_cast<const char16_t *>(buf.data()), charCount);
      }
    }
    FPDF_GetFileVersion(doc_, &pdfVersion_);
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object docObj = g_docConstructor.New({});
    PDFiumDocument *docWrapper = PDFiumDocument::Unwrap(docObj);
    docWrapper->SetDocument(doc_);

    docObj.Set("pageCount", Napi::Number::New(env, pageCount_));

    // set metadata as a plain JS property
    const char *metaKeys[] = {"title",   "author",   "subject",      "keywords",
                              "creator", "producer", "creationDate", "modDate"};
    Napi::Object metaObj = Napi::Object::New(env);
    for (int i = 0; i < 8; i++) {
      if (meta_[i].empty()) {
        metaObj.Set(metaKeys[i], Napi::String::New(env, ""));
      } else {
        metaObj.Set(metaKeys[i],
                    Napi::String::New(
                        env,
                        reinterpret_cast<const char16_t *>(meta_[i].data()),
                        meta_[i].size()));
      }
    }
    metaObj.Set("pdfVersion", Napi::Number::New(env, pdfVersion_));
    docObj.Set("metadata", metaObj);

    // for buffer variant, transfer ownership of the copied data
    // the data must stay alive as long as the document is open
    if (!useFile_ && !bufferData_.empty()) {
      docWrapper->SetOwnedBuffer(std::move(bufferData_));
    }

    deferred_.Resolve(docObj);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::vector<uint8_t> bufferData_;
  std::string filePath_;
  std::string password_;
  bool useFile_;
  FPDF_DOCUMENT doc_ = nullptr;
  int pageCount_ = 0;
  std::u16string meta_[8];
  int pdfVersion_ = 0;
};

/**
 * Opens a PDF document from a Buffer or file path. Returns a
 * Promise<PDFiumDocument>.
 */
Napi::Value LoadDocument(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected a Buffer or string path argument")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Value arg = info[0];
  std::string password;
  if (info.Length() > 1 && info[1].IsString()) {
    password = info[1].As<Napi::String>().Utf8Value();
  }

  LoadDocumentWorker *worker = nullptr;

  if (arg.IsBuffer()) {
    auto buffer = arg.As<Napi::Buffer<uint8_t>>();
    // copy buffer data — original JS buffer may be GC'd before worker runs
    std::vector<uint8_t> data(buffer.Data(), buffer.Data() + buffer.Length());
    worker = new LoadDocumentWorker(env, std::move(data), std::move(password));
  } else if (arg.IsString()) {
    std::string path = arg.As<Napi::String>().Utf8Value();
    worker = new LoadDocumentWorker(env, std::move(path), std::move(password));
  } else {
    Napi::TypeError::New(env, "Expected a Buffer or string path argument")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}

// ---------------------------------------------------------------------------
// Module init
// ---------------------------------------------------------------------------

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  if (!g_initialized) {
    FPDF_InitLibrary();
    g_initialized = true;
  }

  // register classes
  Napi::Function pageCtor = PDFiumPage::Init(env);
  PDFiumDocument::pageConstructor = Napi::Persistent(pageCtor);
  PDFiumDocument::pageConstructor.SuppressDestruct();

  Napi::Function docCtor = PDFiumDocument::Init(env);
  g_docConstructor = Napi::Persistent(docCtor);
  g_docConstructor.SuppressDestruct();

  exports.Set("PDFiumDocument", docCtor);
  exports.Set("PDFiumPage", pageCtor);
  exports.Set("loadDocument", Napi::Function::New(env, LoadDocument));

  return exports;
}

NODE_API_MODULE(pdfium, Init)
