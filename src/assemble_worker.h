#pragma once

#include "document_io.h"

#include <string>
#include <vector>

#include "fpdf_edit.h"
#include "fpdf_ppo.h"

// ---------------------------------------------------------------------------
// Page assembly workers
// ---------------------------------------------------------------------------
// splitDocument and mergeDocuments cover the two coarse cases: cut a document
// into runs, or concatenate whole documents. These two cover the rest —
// reordering, selecting and duplicating individual pages, and imposing several
// source pages onto one output sheet.

// ---------------------------------------------------------------------------
// AssemblePagesWorker — build a document from selected pages, in order
// ---------------------------------------------------------------------------
// The index list is taken literally: order is preserved and repeats produce
// repeated pages, which is what makes booklet imposition and page duplication
// possible.

class AssemblePagesWorker : public SafeAsyncWorker {
public:
  // buffer variant
  AssemblePagesWorker(Napi::Env env, std::vector<uint8_t> data,
                      std::vector<int> pages, std::string outputPath,
                      std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), pages_(std::move(pages)),
        outputPath_(std::move(outputPath)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  AssemblePagesWorker(Napi::Env env, std::string filePath,
                      std::vector<int> pages, std::string outputPath,
                      std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(filePath)), pages_(std::move(pages)),
        outputPath_(std::move(outputPath)), password_(std::move(password)),
        useFile_(true) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    std::string loadError;
    FPDF_DOCUMENT srcDoc =
        LoadDoc(bufferData_, filePath_, useFile_, password_, loadError);
    if (!srcDoc) {
      SetError(loadError);
      return;
    }

    // Report the offending index ourselves — FPDF_ImportPagesByIndex only
    // returns a bare false, which would tell the caller nothing.
    int pageCount = FPDF_GetPageCount(srcDoc);
    for (int page : pages_) {
      if (page < 0 || page >= pageCount) {
        FPDF_CloseDocument(srcDoc);
        SetError("Page index out of range: " + std::to_string(page));
        return;
      }
    }

    FPDF_DOCUMENT destDoc = FPDF_CreateNewDocument();
    if (!destDoc) {
      FPDF_CloseDocument(srcDoc);
      SetError("Failed to create new PDF document");
      return;
    }

    FPDF_BOOL ok = FPDF_ImportPagesByIndex(
        destDoc, srcDoc, pages_.data(),
        static_cast<unsigned long>(pages_.size()), 0);
    if (!ok) {
      FPDF_CloseDocument(destDoc);
      FPDF_CloseDocument(srcDoc);
      SetError("Failed to import pages");
      return;
    }

    std::string saveError;
    if (!SaveDocument(destDoc, outputPath_, resultData_, saveError)) {
      FPDF_CloseDocument(destDoc);
      FPDF_CloseDocument(srcDoc);
      SetError(saveError);
      return;
    }

    FPDF_CloseDocument(destDoc);
    FPDF_CloseDocument(srcDoc);
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
  std::vector<int> pages_;
  std::string outputPath_;
  std::string password_;
  bool useFile_;
  std::vector<uint8_t> resultData_;
};

// ---------------------------------------------------------------------------
// NUpPagesWorker — impose columns x rows source pages onto each output page
// ---------------------------------------------------------------------------

class NUpPagesWorker : public SafeAsyncWorker {
public:
  // buffer variant
  NUpPagesWorker(Napi::Env env, std::vector<uint8_t> data, int columns,
                 int rows, float width, float height, std::string outputPath,
                 std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), columns_(columns), rows_(rows),
        width_(width), height_(height), outputPath_(std::move(outputPath)),
        password_(std::move(password)), useFile_(false) {}

  // file path variant
  NUpPagesWorker(Napi::Env env, std::string filePath, int columns, int rows,
                 float width, float height, std::string outputPath,
                 std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(filePath)), columns_(columns), rows_(rows),
        width_(width), height_(height), outputPath_(std::move(outputPath)),
        password_(std::move(password)), useFile_(true) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    std::string loadError;
    FPDF_DOCUMENT srcDoc =
        LoadDoc(bufferData_, filePath_, useFile_, password_, loadError);
    if (!srcDoc) {
      SetError(loadError);
      return;
    }

    if (FPDF_GetPageCount(srcDoc) == 0) {
      FPDF_CloseDocument(srcDoc);
      SetError("Source document has no pages");
      return;
    }

    // Default the sheet to the first source page, so 2x2 of A4 lands on A4
    // rather than on some unrelated hard-coded size.
    if (width_ <= 0 || height_ <= 0) {
      FS_SIZEF size;
      if (!FPDF_GetPageSizeByIndexF(srcDoc, 0, &size)) {
        FPDF_CloseDocument(srcDoc);
        SetError("Failed to read the source page size");
        return;
      }
      if (width_ <= 0)
        width_ = size.width;
      if (height_ <= 0)
        height_ = size.height;
    }

    FPDF_DOCUMENT destDoc = FPDF_ImportNPagesToOne(
        srcDoc, width_, height_, static_cast<size_t>(columns_),
        static_cast<size_t>(rows_));
    if (!destDoc) {
      FPDF_CloseDocument(srcDoc);
      SetError("Failed to combine pages");
      return;
    }

    std::string saveError;
    if (!SaveDocument(destDoc, outputPath_, resultData_, saveError)) {
      FPDF_CloseDocument(destDoc);
      FPDF_CloseDocument(srcDoc);
      SetError(saveError);
      return;
    }

    FPDF_CloseDocument(destDoc);
    FPDF_CloseDocument(srcDoc);
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
  int columns_;
  int rows_;
  float width_;
  float height_;
  std::string outputPath_;
  std::string password_;
  bool useFile_;
  std::vector<uint8_t> resultData_;
};
