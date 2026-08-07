#pragma once

#include "document_io.h"

#include <string>
#include <vector>

#include "fpdf_attachment.h"

// ---------------------------------------------------------------------------
// AddAttachmentsWorker — embed files into a copy of a document
// ---------------------------------------------------------------------------
// This is the one write path in the library, and PDFium's embedding API is
// much narrower than the read side. Against the pinned chromium/7920 build it
// reaches exactly three things:
//
//   /UF + /F   the file name          (FPDFDoc_AddAttachment)
//   the stream the file bytes         (FPDFAttachment_SetFile)
//   /Params    Size, CreationDate, ModDate, CheckSum
//                                     (FPDFAttachment_SetStringValue)
//
// It cannot set /Subtype (the MIME type) or /AFRelationship: both sit on the
// filespec dict or the stream dict, not in the /Params sub-dictionary that
// SetStringValue writes to, and PDFium exposes no setter for either.
// /Desc is out of reach too — FPDFAttachment_SetDescription exists upstream but
// not in chromium/7920.
//
// A file embedded this way is therefore a valid PDF attachment but NOT a
// conformant PDF/A-3 / ZUGFeRD / Factur-X e-invoice. See the note on
// addAttachments() in the docs.

struct AttachmentToAdd {
  std::u16string name;
  std::vector<uint8_t> data;
  std::u16string creationDate;
  std::u16string modDate;
};

class AddAttachmentsWorker : public SafeAsyncWorker {
public:
  // buffer variant
  AddAttachmentsWorker(Napi::Env env, std::vector<uint8_t> data,
                       std::vector<AttachmentToAdd> attachments,
                       std::string outputPath, std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), attachments_(std::move(attachments)),
        outputPath_(std::move(outputPath)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  AddAttachmentsWorker(Napi::Env env, std::string filePath,
                       std::vector<AttachmentToAdd> attachments,
                       std::string outputPath, std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(filePath)), attachments_(std::move(attachments)),
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

    for (const auto &item : attachments_) {
      FPDF_ATTACHMENT attachment =
          FPDFDoc_AddAttachment(doc, ToWideString(item.name));
      if (!attachment) {
        FPDF_CloseDocument(doc);
        // AddAttachment also rejects a name that already exists in the
        // embedded-files name tree, which is the likely cause here.
        SetError("Failed to add attachment (empty or duplicate name): " +
                 Utf8Of(item.name));
        return;
      }

      // SetFile must come first: it rebuilds the embedded-file stream and its
      // /Params, discarding anything already written there.
      if (!FPDFAttachment_SetFile(attachment, doc, item.data.data(),
                                  static_cast<unsigned long>(
                                      item.data.size()))) {
        FPDF_CloseDocument(doc);
        SetError("Failed to write attachment data: " + Utf8Of(item.name));
        return;
      }

      // SetFile stamps CreationDate with the current time; an explicit value
      // overwrites it, which is also what makes output reproducible.
      if (!item.creationDate.empty()) {
        FPDFAttachment_SetStringValue(attachment, "CreationDate",
                                      ToWideString(item.creationDate));
      }
      if (!item.modDate.empty()) {
        FPDFAttachment_SetStringValue(attachment, "ModDate",
                                      ToWideString(item.modDate));
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
  // FPDF_WIDESTRING is a NUL-terminated UTF-16LE string; std::u16string
  // already stores exactly that and guarantees the terminator.
  static FPDF_WIDESTRING ToWideString(const std::u16string &s) {
    return reinterpret_cast<FPDF_WIDESTRING>(s.c_str());
  }

  // best-effort ASCII rendering of a name, for error messages only
  static std::string Utf8Of(const std::u16string &s) {
    std::string out;
    out.reserve(s.size());
    for (char16_t c : s) {
      out.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    }
    return out;
  }

  Napi::Promise::Deferred deferred_;
  std::vector<uint8_t> bufferData_;
  std::string filePath_;
  std::vector<AttachmentToAdd> attachments_;
  std::string outputPath_;
  std::string password_;
  bool useFile_ = false;
  std::vector<uint8_t> resultData_;
};
