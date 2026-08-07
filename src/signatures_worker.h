#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_signature.h"

// ---------------------------------------------------------------------------
// Digital signature helpers
// ---------------------------------------------------------------------------
// PDFium reads the signature *dictionary* only — it performs no cryptographic
// verification whatsoever. These workers surface what the PDF itself declares:
// the encoding (/SubFilter), the signing reason and time, the DocMDP
// certification level and the byte ranges the digest covers. Verifying the
// PKCS#7 blob against those ranges is the caller's job;
// GetSignatureContentsWorker hands the blob over untouched.

// read a signature's /SubFilter, /M etc. — these are 7-bit ASCII, not UTF-16LE
template <typename Fn> inline std::string ReadSignatureAscii(Fn fn) {
  return ReadAscii([&](char *, unsigned long) { return fn(nullptr, 0); },
                   [&](char *buf, unsigned long len) { return fn(buf, len); });
}

// Reads the raw /Contents bytes of a signature (a DER-encoded PKCS#1 or PKCS#7
// binary) via the two-pass protocol. An empty /Contents yields an empty vector
// and success. Returns false and sets `err` on failure.
inline bool ReadSignatureContentsBytes(FPDF_SIGNATURE signature,
                                       std::vector<uint8_t> &out,
                                       std::string &err) {
  unsigned long len = FPDFSignatureObj_GetContents(signature, nullptr, 0);
  out.clear();
  if (len == 0)
    return true;

  out.resize(len);
  if (FPDFSignatureObj_GetContents(signature, out.data(), len) != len) {
    err = "Failed to read signature contents";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// GetSignaturesWorker — async signature metadata listing
// ---------------------------------------------------------------------------
// Deliberately cheap in the same sense as GetAttachmentsWorker: it reads
// dictionary entries and the byte-range array, but reports /Contents only by
// its length. Use GetSignatureContentsWorker to fetch the blob itself.

struct SignatureInfo {
  int index = 0;
  std::string subFilter;
  std::u16string reason;
  std::string time;
  unsigned int docMdpPermission = 0;
  std::vector<int> byteRange;
  unsigned long contentsLength = 0;
};

class GetSignaturesWorker : public SafeAsyncWorker {
public:
  GetSignaturesWorker(Napi::Env env, FPDF_DOCUMENT doc,
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

    int count = FPDF_GetSignatureCount(doc_);
    if (count <= 0)
      return;

    signatures_.reserve(count);
    for (int i = 0; i < count; i++) {
      FPDF_SIGNATURE signature = FPDF_GetSignatureObject(doc_, i);
      if (!signature)
        continue;

      SignatureInfo info;
      info.index = i;

      info.subFilter = ReadSignatureAscii(
          [&](char *buf, unsigned long len) {
            return FPDFSignatureObj_GetSubFilter(signature, buf, len);
          });

      info.reason = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFSignatureObj_GetReason(signature, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFSignatureObj_GetReason(signature, buf, len);
          });

      info.time = ReadSignatureAscii([&](char *buf, unsigned long len) {
        return FPDFSignatureObj_GetTime(signature, buf, len);
      });

      // 1-3 for a certification (DocMDP) signature, 0 for an ordinary one
      info.docMdpPermission =
          FPDFSignatureObj_GetDocMDPPermission(signature);

      // /ByteRange is a flat array of (offset, length) pairs; the length here
      // counts ints, not bytes
      unsigned long rangeLen =
          FPDFSignatureObj_GetByteRange(signature, nullptr, 0);
      if (rangeLen > 0) {
        info.byteRange.resize(rangeLen);
        if (FPDFSignatureObj_GetByteRange(signature, info.byteRange.data(),
                                          rangeLen) != rangeLen) {
          info.byteRange.clear();
        }
      }

      info.contentsLength =
          FPDFSignatureObj_GetContents(signature, nullptr, 0);

      signatures_.push_back(std::move(info));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, signatures_.size());
    for (uint32_t i = 0; i < signatures_.size(); i++) {
      const auto &info = signatures_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("index", Napi::Number::New(env, info.index));
      obj.Set("subFilter", Napi::String::New(env, info.subFilter));
      SetU16IfPresent(obj, "reason", env, info.reason);
      if (!info.time.empty())
        obj.Set("time", Napi::String::New(env, info.time));
      if (info.docMdpPermission > 0) {
        obj.Set("docMdpPermission",
                Napi::Number::New(env, info.docMdpPermission));
      }

      Napi::Array range = Napi::Array::New(env, info.byteRange.size());
      for (uint32_t j = 0; j < info.byteRange.size(); j++) {
        range.Set(j, Napi::Number::New(env, info.byteRange[j]));
      }
      obj.Set("byteRange", range);

      obj.Set("contentsLength",
              Napi::Number::New(env, static_cast<double>(info.contentsLength)));
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
  std::vector<SignatureInfo> signatures_;
};

// ---------------------------------------------------------------------------
// GetSignatureContentsWorker — async read of a single signature's /Contents
// ---------------------------------------------------------------------------

class GetSignatureContentsWorker : public SafeAsyncWorker {
public:
  GetSignatureContentsWorker(Napi::Env env, FPDF_DOCUMENT doc, int index,
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

    int count = FPDF_GetSignatureCount(doc_);
    if (index_ < 0 || index_ >= count) {
      SetError("Signature index out of range");
      return;
    }

    FPDF_SIGNATURE signature = FPDF_GetSignatureObject(doc_, index_);
    if (!signature) {
      SetError("Failed to get signature");
      return;
    }

    std::string err;
    if (!ReadSignatureContentsBytes(signature, data_, err)) {
      SetError(err);
      return;
    }

    if (!outputPath_.empty()) {
      if (!WriteBytesToFile(outputPath_, data_, err)) {
        SetError(err);
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
