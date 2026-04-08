#pragma once

#include "napi_helpers.h"

#include <climits>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

#include "fpdf_edit.h"
#include "fpdf_ppo.h"
#include "fpdf_save.h"

// ---------------------------------------------------------------------------
// FPDF_FILEWRITE adapter — collects saved PDF bytes into a std::vector
// ---------------------------------------------------------------------------

struct VectorFileWrite : FPDF_FILEWRITE {
  std::vector<uint8_t> data;

  VectorFileWrite() {
    version = 1;
    WriteBlock = [](FPDF_FILEWRITE_ *self, const void *buf,
                    unsigned long size) -> int {
      auto *w = static_cast<VectorFileWrite *>(self);
      auto *bytes = static_cast<const uint8_t *>(buf);
      w->data.insert(w->data.end(), bytes, bytes + size);
      return 1;
    };
  }
};

// helper: save a document to buffer or file, returning the buffer data
static inline bool SaveDocument(FPDF_DOCUMENT doc,
                                const std::string &outputPath,
                                std::vector<uint8_t> &outData,
                                std::string &outError) {
  VectorFileWrite writer;
  if (!FPDF_SaveAsCopy(doc, &writer, 0)) {
    outError = "Failed to save PDF document";
    return false;
  }

  if (!outputPath.empty()) {
    auto slash = outputPath.rfind('/');
    if (slash != std::string::npos && slash > 0) {
      std::string parentDir = outputPath.substr(0, slash);
      if (access(parentDir.c_str(), F_OK) != 0) {
        outError = "Parent directory does not exist: " + parentDir;
        return false;
      }
    }
    FILE *f = fopen(outputPath.c_str(), "wb");
    if (!f) {
      outError = "Failed to open output file: " + outputPath;
      return false;
    }
    size_t written = fwrite(writer.data.data(), 1, writer.data.size(), f);
    fclose(f);
    if (written != writer.data.size()) {
      outError = "Failed to write output file: " + outputPath;
      return false;
    }
  } else {
    outData = std::move(writer.data);
  }
  return true;
}

// helper: load a document from buffer or file path
static inline FPDF_DOCUMENT LoadDoc(const std::vector<uint8_t> &bufferData,
                                    const std::string &filePath, bool useFile,
                                    const std::string &password,
                                    std::string &outError) {
  const char *pw = password.empty() ? nullptr : password.c_str();
  FPDF_DOCUMENT doc;

  if (useFile) {
    doc = FPDF_LoadDocument(filePath.c_str(), pw);
  } else {
    if (bufferData.size() > static_cast<size_t>(INT_MAX)) {
      outError = "FORMAT:Buffer too large";
      return nullptr;
    }
    doc = FPDF_LoadMemDocument(bufferData.data(),
                               static_cast<int>(bufferData.size()), pw);
  }

  if (!doc) {
    unsigned long err = FPDF_GetLastError();
    switch (err) {
    case FPDF_ERR_FILE:
      outError = "FILE:File not found or could not be opened";
      break;
    case FPDF_ERR_FORMAT:
      outError = "FORMAT:Not a valid PDF or corrupted";
      break;
    case FPDF_ERR_PASSWORD:
      outError = "PASSWORD:Password required or incorrect";
      break;
    case FPDF_ERR_SECURITY:
      outError = "SECURITY:Unsupported security scheme";
      break;
    default:
      outError = "UNKNOWN:Unknown error";
      break;
    }
  }
  return doc;
}

// ---------------------------------------------------------------------------
// SplitDocumentWorker — split a PDF into multiple documents at given indices
// ---------------------------------------------------------------------------

class SplitDocumentWorker : public SafeAsyncWorker {
public:
  // buffer variant
  SplitDocumentWorker(Napi::Env env, std::vector<uint8_t> data,
                      std::vector<int> splitAt,
                      std::vector<std::string> outputPaths,
                      std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), splitAt_(std::move(splitAt)),
        outputPaths_(std::move(outputPaths)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  SplitDocumentWorker(Napi::Env env, std::string filePath,
                      std::vector<int> splitAt,
                      std::vector<std::string> outputPaths,
                      std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(filePath)), splitAt_(std::move(splitAt)),
        outputPaths_(std::move(outputPaths)), password_(std::move(password)),
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

    int pageCount = FPDF_GetPageCount(srcDoc);

    // validate split points are sorted, in range, and unique
    for (size_t i = 0; i < splitAt_.size(); i++) {
      int idx = splitAt_[i];
      if (idx <= 0 || idx >= pageCount) {
        FPDF_CloseDocument(srcDoc);
        SetError("Split index out of range: " + std::to_string(idx));
        return;
      }
      if (i > 0 && idx <= splitAt_[i - 1]) {
        FPDF_CloseDocument(srcDoc);
        SetError("Split indices must be sorted in ascending order");
        return;
      }
    }

    // build page ranges: [0, splitAt[0]), [splitAt[0], splitAt[1]), ...,
    // [splitAt[n-1], pageCount)
    size_t numParts = splitAt_.size() + 1;
    resultParts_.resize(numParts);

    for (size_t part = 0; part < numParts; part++) {
      int startPage = (part == 0) ? 0 : splitAt_[part - 1];
      int endPage = (part == splitAt_.size()) ? pageCount : splitAt_[part];

      std::vector<int> indices;
      indices.reserve(endPage - startPage);
      for (int p = startPage; p < endPage; p++) {
        indices.push_back(p);
      }

      FPDF_DOCUMENT destDoc = FPDF_CreateNewDocument();
      if (!destDoc) {
        FPDF_CloseDocument(srcDoc);
        SetError("Failed to create new PDF document");
        return;
      }

      FPDF_BOOL ok = FPDF_ImportPagesByIndex(
          destDoc, srcDoc, indices.data(),
          static_cast<unsigned long>(indices.size()), 0);
      if (!ok) {
        FPDF_CloseDocument(destDoc);
        FPDF_CloseDocument(srcDoc);
        SetError("Failed to import pages for part " + std::to_string(part));
        return;
      }

      std::string outPath =
          (part < outputPaths_.size()) ? outputPaths_[part] : "";
      std::string saveError;
      if (!SaveDocument(destDoc, outPath, resultParts_[part], saveError)) {
        FPDF_CloseDocument(destDoc);
        FPDF_CloseDocument(srcDoc);
        SetError(saveError);
        return;
      }

      FPDF_CloseDocument(destDoc);
    }

    FPDF_CloseDocument(srcDoc);
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (!outputPaths_.empty()) {
      deferred_.Resolve(env.Undefined());
    } else {
      Napi::Array arr = Napi::Array::New(env, resultParts_.size());
      for (size_t i = 0; i < resultParts_.size(); i++) {
        arr.Set(static_cast<uint32_t>(i),
                Napi::Buffer<uint8_t>::Copy(env, resultParts_[i].data(),
                                            resultParts_[i].size()));
      }
      deferred_.Resolve(arr);
    }
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::vector<uint8_t> bufferData_;
  std::string filePath_;
  std::vector<int> splitAt_;
  std::vector<std::string> outputPaths_;
  std::string password_;
  bool useFile_;
  std::vector<std::vector<uint8_t>> resultParts_;
};

// ---------------------------------------------------------------------------
// MergeDocumentsWorker — combine multiple PDFs into one
// ---------------------------------------------------------------------------

struct DocInput {
  std::vector<uint8_t> bufferData;
  std::string filePath;
  std::string password;
  bool useFile;
};

class MergeDocumentsWorker : public SafeAsyncWorker {
public:
  MergeDocumentsWorker(Napi::Env env, std::vector<DocInput> inputs,
                       std::string outputPath)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        inputs_(std::move(inputs)), outputPath_(std::move(outputPath)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    FPDF_DOCUMENT destDoc = FPDF_CreateNewDocument();
    if (!destDoc) {
      SetError("Failed to create new PDF document");
      return;
    }

    std::vector<FPDF_DOCUMENT> srcDocs;
    int insertIndex = 0;

    for (size_t i = 0; i < inputs_.size(); i++) {
      auto &input = inputs_[i];
      std::string loadError;
      FPDF_DOCUMENT srcDoc = LoadDoc(input.bufferData, input.filePath,
                                     input.useFile, input.password, loadError);
      if (!srcDoc) {
        // clean up already-opened docs
        for (auto *d : srcDocs)
          FPDF_CloseDocument(d);
        FPDF_CloseDocument(destDoc);
        SetError("Failed to load document " + std::to_string(i) + ": " +
                 loadError);
        return;
      }
      srcDocs.push_back(srcDoc);

      int pageCount = FPDF_GetPageCount(srcDoc);

      // import all pages from this source
      FPDF_BOOL ok =
          FPDF_ImportPagesByIndex(destDoc, srcDoc, nullptr, 0, insertIndex);
      if (!ok) {
        for (auto *d : srcDocs)
          FPDF_CloseDocument(d);
        FPDF_CloseDocument(destDoc);
        SetError("Failed to import pages from document " + std::to_string(i));
        return;
      }

      insertIndex += pageCount;
    }

    std::string saveError;
    if (!SaveDocument(destDoc, outputPath_, resultData_, saveError)) {
      for (auto *d : srcDocs)
        FPDF_CloseDocument(d);
      FPDF_CloseDocument(destDoc);
      SetError(saveError);
      return;
    }

    for (auto *d : srcDocs)
      FPDF_CloseDocument(d);
    FPDF_CloseDocument(destDoc);
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (outputPath_.empty()) {
      deferred_.Resolve(Napi::Buffer<uint8_t>::Copy(env, resultData_.data(),
                                                    resultData_.size()));
    } else {
      deferred_.Resolve(env.Undefined());
    }
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::vector<DocInput> inputs_;
  std::string outputPath_;
  std::vector<uint8_t> resultData_;
};
