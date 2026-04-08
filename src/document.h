#pragma once

#include "bookmarks_worker.h"
#include "page.h"

#include <atomic>
#include <memory>

#include "fpdf_transformpage.h"

// ---------------------------------------------------------------------------
// PDFiumDocument
// ---------------------------------------------------------------------------

class PDFiumDocument : public Napi::ObjectWrap<PDFiumDocument> {
public:
  static Napi::Function Init(Napi::Env env) {
    return DefineClass(
        env, "PDFiumDocument",
        {
            InstanceMethod<&PDFiumDocument::GetPage>("getPage"),
            InstanceMethod<&PDFiumDocument::GetBookmarks>("getBookmarks"),
            InstanceMethod<&PDFiumDocument::Destroy>("destroy"),
        });
  }

  PDFiumDocument(const Napi::CallbackInfo &info)
      : Napi::ObjectWrap<PDFiumDocument>(info),
        docAlive_(std::make_shared<std::atomic<bool>>(true)) {}

  void SetDocument(FPDF_DOCUMENT doc) { doc_ = doc; }

  // take ownership of buffer data (needed for FPDF_LoadMemDocument)
  void SetOwnedBuffer(std::vector<uint8_t> buf) {
    ownedBuffer_ = std::move(buf);
  }

  // register a page's alive flag so we can invalidate it on destroy
  void RegisterPageAlive(std::shared_ptr<std::atomic<bool>> flag) {
    pageAliveFlags_.push_back(std::move(flag));
  }

  // returns the document alive flag (shared with pages)
  std::shared_ptr<std::atomic<bool>> GetDocAlive() const { return docAlive_; }

  // store the page constructor so we can create instances
  static Napi::FunctionReference pageConstructor;

private:
  FPDF_DOCUMENT doc_ = nullptr;
  std::vector<uint8_t> ownedBuffer_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<std::shared_ptr<std::atomic<bool>>> pageAliveFlags_;

  void EnsureOpen(Napi::Env env) {
    if (!doc_) {
      Napi::Error::New(env, "Document is destroyed")
          .ThrowAsJavaScriptException();
    }
  }

  /**
   * Gets a page by 0-based index (async). Returns a Promise<PDFiumPage>.
   */
  Napi::Value GetPage(const Napi::CallbackInfo &info);

  /**
   * Returns the bookmark tree as a nested array (async).
   */
  Napi::Value GetBookmarks(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    auto *worker = new GetBookmarksWorker(env, doc_, docAlive_);
    auto promise = worker->Promise();
    worker->Queue();
    return promise;
  }

  /**
   * Closes the document and releases all resources.
   */
  Napi::Value Destroy(const Napi::CallbackInfo &info) {
    CleanUp();
    return info.Env().Undefined();
  }

  /**
   * GC destructor — releases the document if the user forgot to call
   * destroy().
   */
  void Finalize(Napi::Env /*env*/) override { CleanUp(); }

  void CleanUp() {
    if (doc_) {
      std::lock_guard<std::mutex> lock(g_pdfium_mutex);
      for (auto &flag : pageAliveFlags_)
        flag->store(false);
      pageAliveFlags_.clear();
      docAlive_->store(false);
      FPDF_CloseDocument(doc_);
      doc_ = nullptr;
    }
  }
};

// ---------------------------------------------------------------------------
// GetPageWorker — async page loading
// ---------------------------------------------------------------------------

class GetPageWorker : public Napi::AsyncWorker {
public:
  GetPageWorker(Napi::Env env, FPDF_DOCUMENT doc, int pageIndex,
                PDFiumDocument *docWrapper,
                std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        envAlive_(GetEnvAlive(env)), doc_(doc), pageIndex_(pageIndex),
        docWrapper_(docWrapper), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    // check that the document hasn't been destroyed while we were queued
    if (!docAlive_ || !docAlive_->load()) {
      SetError("Document was destroyed before page load completed");
      return;
    }

    // bounds check inside the worker (avoids blocking the main thread)
    int numPages = FPDF_GetPageCount(doc_);
    if (pageIndex_ < 0 || pageIndex_ >= numPages) {
      SetError("Page index out of range");
      return;
    }

    page_ = FPDF_LoadPage(doc_, pageIndex_);
    if (!page_) {
      SetError("Failed to load page");
      return;
    }
    width_ = FPDF_GetPageWidthF(page_);
    height_ = FPDF_GetPageHeightF(page_);
    objectCount_ = FPDFPage_CountObjects(page_);
    rotation_ = FPDFPage_GetRotation(page_);
    hasTransparency_ = FPDFPage_HasTransparency(page_) != 0;

    // page label (UTF-16LE)
    label_ = ReadU16(
        [&](auto *, unsigned long) {
          return FPDF_GetPageLabel(doc_, pageIndex_, nullptr, 0);
        },
        [&](FPDF_WCHAR *buf, unsigned long len) {
          return FPDF_GetPageLabel(doc_, pageIndex_, buf, len);
        });

    // optional page boxes
    float l, b, r, t;
    if (FPDFPage_GetCropBox(page_, &l, &b, &r, &t)) {
      hasCropBox_ = true;
      cropLeft_ = l;
      cropBottom_ = b;
      cropRight_ = r;
      cropTop_ = t;
    }
    if (FPDFPage_GetTrimBox(page_, &l, &b, &r, &t)) {
      hasTrimBox_ = true;
      trimLeft_ = l;
      trimBottom_ = b;
      trimRight_ = r;
      trimTop_ = t;
    }
  }

  void OnOK() override {
    CHECK_ENV();
    Napi::Env env = Env();
    Napi::Object pageObj = PDFiumDocument::pageConstructor.New({});
    PDFiumPage *pageWrapper = PDFiumPage::Unwrap(pageObj);
    pageWrapper->SetPage(page_, doc_, pageIndex_, width_, height_);
    pageWrapper->SetDocAlive(docAlive_);

    // register the page's alive flag with the document for invalidation
    docWrapper_->RegisterPageAlive(pageWrapper->GetAliveFlag());

    // set dimensions as plain JS properties (no native roundtrip on access)
    pageObj.Set("width", Napi::Number::New(env, width_));
    pageObj.Set("height", Napi::Number::New(env, height_));
    pageObj.Set("objectCount", Napi::Number::New(env, objectCount_));
    pageObj.Set("rotation", Napi::Number::New(env, rotation_));
    pageObj.Set("hasTransparency", Napi::Boolean::New(env, hasTransparency_));
    SetU16IfPresent(pageObj, "label", env, label_);
    if (hasCropBox_)
      pageObj.Set("cropBox", CreateBoundsObject(env, cropLeft_, cropBottom_,
                                                cropRight_, cropTop_));
    if (hasTrimBox_)
      pageObj.Set("trimBox", CreateBoundsObject(env, trimLeft_, trimBottom_,
                                                trimRight_, trimTop_));

    deferred_.Resolve(pageObj);
  }

  void OnError(const Napi::Error &err) override {
    CHECK_ENV();
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::shared_ptr<std::atomic<bool>> envAlive_;
  FPDF_DOCUMENT doc_;
  int pageIndex_;
  PDFiumDocument *docWrapper_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  FPDF_PAGE page_ = nullptr;
  float width_ = 0;
  float height_ = 0;
  int objectCount_ = 0;
  int rotation_ = 0;
  bool hasTransparency_ = false;
  std::u16string label_;
  bool hasCropBox_ = false;
  float cropLeft_ = 0, cropBottom_ = 0, cropRight_ = 0, cropTop_ = 0;
  bool hasTrimBox_ = false;
  float trimLeft_ = 0, trimBottom_ = 0, trimRight_ = 0, trimTop_ = 0;
};

// deferred definition of GetPage (needs GetPageWorker)
inline Napi::Value PDFiumDocument::GetPage(const Napi::CallbackInfo &info) {
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

  auto *worker = new GetPageWorker(env, doc_, pageIndex, this, docAlive_);
  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}
