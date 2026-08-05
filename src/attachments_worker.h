#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

#include "fpdf_attachment.h"

// ---------------------------------------------------------------------------
// Embedded-file (attachment) helpers
// ---------------------------------------------------------------------------
// PDF/A-3 documents (ZUGFeRD / Factur-X / XRechnung e-invoices) carry the
// structured invoice as an embedded XML file in the /EmbeddedFiles name tree.
// FPDFDoc_GetAttachmentCount already surfaces detection via metadata; these
// workers add the ability to enumerate attachment metadata and read the raw
// bytes so callers can pull out factur-x.xml / zugferd-invoice.xml / etc.

// read a UTF-16LE string value from an attachment's params dictionary
inline std::u16string ReadAttachmentStringValue(FPDF_ATTACHMENT attachment,
                                                 const char *key) {
  return ReadU16(
      [&](auto *, unsigned long) {
        return FPDFAttachment_GetStringValue(attachment, key, nullptr, 0);
      },
      [&](FPDF_WCHAR *buf, unsigned long len) {
        return FPDFAttachment_GetStringValue(attachment, key, buf, len);
      });
}

// ---------------------------------------------------------------------------
// GetAttachmentsWorker — async attachment metadata listing
// ---------------------------------------------------------------------------
// Deliberately cheap: reads only dictionary entries (name, subtype, dates)
// and never decodes the file streams. Use GetAttachmentDataWorker to fetch the
// bytes of a specific attachment.

struct AttachmentInfo {
  int index = 0;
  std::u16string name;
  std::u16string mimeType;
  std::u16string creationDate;
  std::u16string modDate;
};

class GetAttachmentsWorker : public SafeAsyncWorker {
public:
  GetAttachmentsWorker(Napi::Env env, FPDF_DOCUMENT doc,
                       std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        doc_(doc), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    if (!docAlive_ || !docAlive_->load()) {
      SetError("Document was destroyed");
      return;
    }

    int count = FPDFDoc_GetAttachmentCount(doc_);
    if (count <= 0)
      return;

    attachments_.reserve(count);
    for (int i = 0; i < count; i++) {
      FPDF_ATTACHMENT attachment = FPDFDoc_GetAttachment(doc_, i);
      if (!attachment)
        continue;

      AttachmentInfo info;
      info.index = i;

      info.name = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAttachment_GetName(attachment, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAttachment_GetName(attachment, buf, len);
          });

      info.mimeType = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAttachment_GetSubtype(attachment, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAttachment_GetSubtype(attachment, buf, len);
          });

      info.creationDate = ReadAttachmentStringValue(attachment, "CreationDate");
      info.modDate = ReadAttachmentStringValue(attachment, "ModDate");

      attachments_.push_back(std::move(info));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, attachments_.size());
    for (uint32_t i = 0; i < attachments_.size(); i++) {
      const auto &info = attachments_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("index", Napi::Number::New(env, info.index));
      SetU16(obj, "name", env, info.name);
      SetU16(obj, "mimeType", env, info.mimeType);
      SetU16IfPresent(obj, "creationDate", env, info.creationDate);
      SetU16IfPresent(obj, "modDate", env, info.modDate);
      arr.Set(i, obj);
    }
    deferred_.Resolve(arr);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<AttachmentInfo> attachments_;
};

// ---------------------------------------------------------------------------
// GetAttachmentDataWorker — async read of a single attachment's bytes
// ---------------------------------------------------------------------------

class GetAttachmentDataWorker : public SafeAsyncWorker {
public:
  GetAttachmentDataWorker(Napi::Env env, FPDF_DOCUMENT doc, int index,
                          std::string outputPath,
                          std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        doc_(doc), index_(index), outputPath_(std::move(outputPath)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    if (!docAlive_ || !docAlive_->load()) {
      SetError("Document was destroyed");
      return;
    }

    int count = FPDFDoc_GetAttachmentCount(doc_);
    if (index_ < 0 || index_ >= count) {
      SetError("Attachment index out of range");
      return;
    }

    FPDF_ATTACHMENT attachment = FPDFDoc_GetAttachment(doc_, index_);
    if (!attachment) {
      SetError("Failed to get attachment");
      return;
    }

    // first pass: query the decoded byte length (buffer null → size only)
    unsigned long outLen = 0;
    if (!FPDFAttachment_GetFile(attachment, nullptr, 0, &outLen)) {
      SetError("Failed to read attachment file data");
      return;
    }

    // second pass: copy the bytes (skip for a zero-length embedded file, where
    // data_.data() would be null and the copy is a no-op anyway)
    if (outLen > 0) {
      data_.resize(outLen);
      unsigned long written = 0;
      if (!FPDFAttachment_GetFile(attachment, data_.data(), outLen, &written) ||
          written != outLen) {
        SetError("Failed to read attachment file data");
        return;
      }
    }

    if (!outputPath_.empty()) {
      // verify parent directory exists (mirrors RenderWorker)
      auto slash = outputPath_.rfind('/');
      if (slash != std::string::npos && slash > 0) {
        std::string parentDir = outputPath_.substr(0, slash);
        if (access(parentDir.c_str(), F_OK) != 0) {
          SetError("Parent directory does not exist: " + parentDir);
          return;
        }
      }

      FILE *f = fopen(outputPath_.c_str(), "wb");
      if (!f) {
        SetError("Failed to open output file: " + outputPath_);
        return;
      }
      size_t total = data_.size();
      size_t wrote = total == 0 ? 0 : fwrite(data_.data(), 1, total, f);
      fclose(f);
      if (wrote != total) {
        SetError("Failed to write output file: " + outputPath_);
        return;
      }
      data_.clear();
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (!outputPath_.empty()) {
      deferred_.Resolve(env.Undefined());
      return;
    }
    auto *vec = new std::vector<uint8_t>(std::move(data_));
    auto buffer = Napi::Buffer<uint8_t>::New(
        env, vec->data(), vec->size(),
        [](Napi::Env, uint8_t *, std::vector<uint8_t> *v) { delete v; }, vec);
    deferred_.Resolve(buffer);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_DOCUMENT doc_;
  int index_;
  std::string outputPath_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<uint8_t> data_;
};
