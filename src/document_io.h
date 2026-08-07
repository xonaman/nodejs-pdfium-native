#pragma once

#include "napi_helpers.h"

#include <climits>
#include <string>
#include <vector>

#include "fpdf_save.h"

// ---------------------------------------------------------------------------
// Document load/save plumbing shared by the document-producing workers
// ---------------------------------------------------------------------------
// Split, merge, assemble and n-up all follow the same shape: open one or more
// source documents, build a new one, then hand the result back either as a
// Buffer or written to a path.

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
    return WriteBytesToFile(outputPath, writer.data, outError);
  }
  outData = std::move(writer.data);
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
    outError = GetPdfiumErrorMessage();
  }
  return doc;
}
