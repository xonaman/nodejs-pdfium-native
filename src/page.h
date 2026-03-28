#pragma once

#include "page_workers.h"
#include "render_worker.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>

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
            InstanceMethod<&PDFiumPage::Render>("render"),
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

  void SetPage(FPDF_PAGE page, FPDF_DOCUMENT doc, int index, float width,
               float height) {
    page_ = page;
    doc_ = doc;
    index_ = index;
    width_ = width;
    height_ = height;
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
  float width_ = 0;
  float height_ = 0;
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
   * Extracts all text from the page (async). Returns a Promise<string>.
   */
  Napi::Value GetText(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    auto *worker = new GetTextWorker(env, page_, alive_, docAlive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Renders the page to a JPEG or PNG buffer (async).
   */
  Napi::Value Render(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    // use cached dimensions (set under mutex in GetPageWorker) to avoid
    // PDFium API calls on the main thread that could race with workers
    float origWidth = width_;
    float origHeight = height_;
    double scale = 1.0;
    int renderWidth = 0;
    int renderHeight = 0;
    int format = IMAGE_FORMAT_JPEG;
    int quality = 100;
    int rotation = 0;
    bool transparent = false;
    int renderFlags = FPDF_ANNOT | FPDF_PRINTING;

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
      if (opts.Has("rotation")) {
        rotation = opts.Get("rotation").As<Napi::Number>().Int32Value();
        if (rotation != 0 && rotation != 1 && rotation != 2 && rotation != 3) {
          Napi::RangeError::New(env, "rotation must be 0, 1, 2, or 3")
              .ThrowAsJavaScriptException();
          return env.Null();
        }
      }
      if (opts.Has("transparent"))
        transparent = opts.Get("transparent").As<Napi::Boolean>().Value();
      if (opts.Has("renderAnnotations") &&
          !opts.Get("renderAnnotations").As<Napi::Boolean>().Value())
        renderFlags &= ~FPDF_ANNOT;
      if (opts.Has("grayscale") &&
          opts.Get("grayscale").As<Napi::Boolean>().Value())
        renderFlags |= FPDF_GRAYSCALE;
      if (opts.Has("lcdText") &&
          opts.Get("lcdText").As<Napi::Boolean>().Value())
        renderFlags |= FPDF_LCD_TEXT;
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

    auto *worker = new RenderWorker(env, page_, renderWidth, renderHeight,
                                    format, quality, std::move(outputPath),
                                    rotation, transparent, renderFlags, alive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Returns a page object at the given index with type and bounds (async).
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
    auto *worker = new GetObjectWorker(env, page_, idx, alive_, docAlive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Returns all links on the page (async). Returns a Promise<Link[]>.
   */
  Napi::Value GetLinks(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    auto *worker = new GetLinksWorker(env, page_, doc_, alive_, docAlive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Searches for text on the page (async). Returns a Promise<SearchMatch[]>.
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
      if (opts.Has("consecutive") &&
          opts.Get("consecutive").As<Napi::Boolean>().Value())
        flags |= FPDF_CONSECUTIVE;
    }

    auto *worker = new SearchWorker(env, page_, std::move(needle), flags,
                                    alive_, docAlive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Returns all annotations on the page (async). Returns a
   * Promise<Annotation[]>.
   */
  Napi::Value GetAnnotations(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    auto *worker = new GetAnnotationsWorker(env, page_, alive_, docAlive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Closes the page and releases its resources.
   */
  Napi::Value Close(const Napi::CallbackInfo &info) {
    CleanUp();
    return info.Env().Undefined();
  }

  /**
   * GC destructor — releases the page if the user forgot to call close().
   */
  void Finalize(Napi::Env /*env*/) override { CleanUp(); }

  void CleanUp() {
    if (page_) {
      std::lock_guard<std::mutex> lock(g_pdfium_mutex);
      alive_->store(false);
      FPDF_ClosePage(page_);
      page_ = nullptr;
      doc_ = nullptr;
    }
  }
};
