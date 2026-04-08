#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <climits>
#include <memory>
#include <vector>

#include "fpdf_text.h"

// ---------------------------------------------------------------------------
// GetTextWorker — async text extraction
// ---------------------------------------------------------------------------

class GetTextWorker : public Napi::AsyncWorker {
public:
  GetTextWorker(Napi::Env env, FPDF_PAGE page,
                std::shared_ptr<std::atomic<bool>> pageAlive,
                std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        envAlive_(GetEnvAlive(env)), page_(page),
        pageAlive_(std::move(pageAlive)), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
    if (!textPage) {
      SetError("Failed to load text page");
      return;
    }

    int charCount = FPDFText_CountChars(textPage);
    if (charCount > 0 && charCount < INT_MAX) {
      size_t bufLen = static_cast<size_t>(charCount) + 1;
      textBuf_.resize(bufLen);
      FPDFText_GetText(textPage, 0, charCount, textBuf_.data());
      textLen_ = charCount;
    }
    FPDFText_ClosePage(textPage);
  }

  void OnOK() override {
    CHECK_ENV();
    Napi::Env env = Env();
    if (textLen_ == 0) {
      deferred_.Resolve(Napi::String::New(env, ""));
    } else {
      deferred_.Resolve(Napi::String::New(
          env, reinterpret_cast<const char16_t *>(textBuf_.data()), textLen_));
    }
  }

  void OnError(const Napi::Error &err) override {
    CHECK_ENV();
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::shared_ptr<std::atomic<bool>> envAlive_;
  FPDF_PAGE page_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<unsigned short> textBuf_;
  int textLen_ = 0;
};
