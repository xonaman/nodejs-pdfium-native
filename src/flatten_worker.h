#pragma once

#include "document_io.h"

#include <string>
#include <vector>

#include "fpdf_flatten.h"

// ---------------------------------------------------------------------------
// FlattenDocumentWorker — bake annotations and widgets into page content
// ---------------------------------------------------------------------------
// Flattening merges each annotation's appearance stream into the page's own
// content stream and drops the annotation. The result renders identically
// everywhere but is no longer interactive — the usual reason to do it is
// archiving, or handing a filled form to a system that ignores annotations.
//
// This does NOT initialise a form-fill environment, so it bakes in the
// appearance streams the document already carries rather than regenerating
// them. For any PDF whose widgets have valid /AP entries — which is what every
// mainstream producer writes — that is exactly right. A form flagged
// /NeedAppearances, meaning "viewer, please regenerate these", is the case it
// cannot help with.

class FlattenDocumentWorker : public SafeAsyncWorker {
public:
  // buffer variant
  FlattenDocumentWorker(Napi::Env env, std::vector<uint8_t> data, int flag,
                        std::vector<int> pages, std::string outputPath,
                        std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), flag_(flag), pages_(std::move(pages)),
        outputPath_(std::move(outputPath)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  FlattenDocumentWorker(Napi::Env env, std::string filePath, int flag,
                        std::vector<int> pages, std::string outputPath,
                        std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(filePath)), flag_(flag), pages_(std::move(pages)),
        outputPath_(std::move(outputPath)), password_(std::move(password)),
        useFile_(true) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    std::string loadError;
    FPDF_DOCUMENT doc =
        LoadDoc(bufferData_, filePath_, useFile_, password_, loadError);
    if (!doc) {
      SetError(loadError);
      return;
    }

    int pageCount = FPDF_GetPageCount(doc);

    std::vector<int> targets = pages_;
    if (targets.empty()) {
      targets.reserve(pageCount);
      for (int i = 0; i < pageCount; i++)
        targets.push_back(i);
    } else {
      for (int page : targets) {
        if (page < 0 || page >= pageCount) {
          FPDF_CloseDocument(doc);
          SetError("Page index out of range: " + std::to_string(page));
          return;
        }
      }
    }

    for (int index : targets) {
      FPDF_PAGE page = FPDF_LoadPage(doc, index);
      if (!page) {
        FPDF_CloseDocument(doc);
        SetError("Failed to load page " + std::to_string(index));
        return;
      }

      int result = FPDFPage_Flatten(page, flag_);
      FPDF_ClosePage(page);

      // FLATTEN_NOTHINGTODO simply means the page had no annotations, which is
      // a normal outcome rather than a failure. Only FLATTEN_FAIL is an error,
      // and PDFium gives no reason for it.
      if (result == FLATTEN_FAIL) {
        FPDF_CloseDocument(doc);
        SetError("Failed to flatten page " + std::to_string(index));
        return;
      }
    }

    std::string saveError;
    if (!SaveDocument(doc, outputPath_, resultData_, saveError)) {
      FPDF_CloseDocument(doc);
      SetError(saveError);
      return;
    }

    FPDF_CloseDocument(doc);
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (!outputPath_.empty()) {
      deferred_.Resolve(env.Undefined());
      return;
    }
    auto *vec = new std::vector<uint8_t>(std::move(resultData_));
    deferred_.Resolve(Napi::Buffer<uint8_t>::New(
        env, vec->data(), vec->size(),
        [](Napi::Env, uint8_t *, std::vector<uint8_t> *v) { delete v; }, vec));
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::vector<uint8_t> bufferData_;
  std::string filePath_;
  int flag_ = FLAT_NORMALDISPLAY;
  std::vector<int> pages_;
  std::string outputPath_;
  std::string password_;
  bool useFile_ = false;
  std::vector<uint8_t> resultData_;
};
