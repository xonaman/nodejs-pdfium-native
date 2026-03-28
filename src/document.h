#pragma once

#include "page.h"

#include <atomic>
#include <memory>

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
   * Returns the bookmark tree as a nested array.
   */
  Napi::Value GetBookmarks(const Napi::CallbackInfo &info) {
    Napi::Env env = info.Env();
    EnsureOpen(env);
    if (env.IsExceptionPending())
      return env.Null();

    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    return BuildBookmarkArray(env, nullptr, 0);
  }

  Napi::Array BuildBookmarkArray(Napi::Env env, FPDF_BOOKMARK parent,
                                 int depth) {
    Napi::Array arr = Napi::Array::New(env);
    if (depth >= MAX_BOOKMARK_DEPTH)
      return arr;

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
      Napi::Array children = BuildBookmarkArray(env, child, depth + 1);
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
      // invalidate all pages first so they can't use dangling pointers
      for (auto &flag : pageAliveFlags_) {
        flag->store(false);
      }
      pageAliveFlags_.clear();
      docAlive_->store(false);

      std::lock_guard<std::mutex> lock(g_pdfium_mutex);
      FPDF_CloseDocument(doc_);
      doc_ = nullptr;
    }
    return info.Env().Undefined();
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
        doc_(doc), pageIndex_(pageIndex), docWrapper_(docWrapper),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    // check that the document hasn't been destroyed while we were queued
    if (!docAlive_ || !docAlive_->load()) {
      SetError("Document was destroyed before page load completed");
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
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object pageObj = PDFiumDocument::pageConstructor.New({});
    PDFiumPage *pageWrapper = PDFiumPage::Unwrap(pageObj);
    pageWrapper->SetPage(page_, doc_, pageIndex_);
    pageWrapper->SetDocAlive(docAlive_);

    // register the page's alive flag with the document for invalidation
    docWrapper_->RegisterPageAlive(pageWrapper->GetAliveFlag());

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
  PDFiumDocument *docWrapper_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  FPDF_PAGE page_ = nullptr;
  float width_ = 0;
  float height_ = 0;
  int objectCount_ = 0;
};

// deferred definition of GetPageAsync (needs GetPageWorker)
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

  // FPDF_GetPageCount is not thread-safe — acquire mutex
  std::lock_guard<std::mutex> lock(g_pdfium_mutex);
  int numPages = FPDF_GetPageCount(doc_);
  if (pageIndex < 0 || pageIndex >= numPages) {
    Napi::RangeError::New(env, "Page index out of range")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto *worker = new GetPageWorker(env, doc_, pageIndex, this, docAlive_);
  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}
