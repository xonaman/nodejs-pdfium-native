#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_javascript.h"

// ---------------------------------------------------------------------------
// GetJavaScriptActionsWorker — async document-level JavaScript listing
// ---------------------------------------------------------------------------
// These are the scripts in the document's /Names /JavaScript tree, which a
// viewer runs when the document opens. Surfacing them matters mostly for
// inspection and triage: a PDF arriving from outside that carries
// document-open JavaScript is worth a second look before it is rendered.
//
// pdfium-native never executes any of it — the build has V8 disabled, so the
// script comes back as inert text.

struct JavaScriptActionInfo {
  int index = 0;
  std::u16string name;
  std::u16string script;
};

class GetJavaScriptActionsWorker : public SafeAsyncWorker {
public:
  GetJavaScriptActionsWorker(Napi::Env env, FPDF_DOCUMENT doc,
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

    int count = FPDFDoc_GetJavaScriptActionCount(doc_);
    if (count <= 0)
      return;

    actions_.reserve(count);
    for (int i = 0; i < count; i++) {
      FPDF_JAVASCRIPT_ACTION action = FPDFDoc_GetJavaScriptAction(doc_, i);
      if (!action)
        continue;

      JavaScriptActionInfo info;
      info.index = i;

      info.name = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFJavaScriptAction_GetName(action, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFJavaScriptAction_GetName(action, buf, len);
          });

      info.script = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFJavaScriptAction_GetScript(action, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFJavaScriptAction_GetScript(action, buf, len);
          });

      // unlike attachments and signatures, the caller owns this handle
      FPDFDoc_CloseJavaScriptAction(action);

      actions_.push_back(std::move(info));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, actions_.size());
    for (uint32_t i = 0; i < actions_.size(); i++) {
      const auto &info = actions_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("index", Napi::Number::New(env, info.index));
      SetU16(obj, "name", env, info.name);
      SetU16(obj, "script", env, info.script);
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
  std::vector<JavaScriptActionInfo> actions_;
};
