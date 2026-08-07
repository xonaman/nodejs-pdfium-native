#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// GetNamedDestinationsWorker — async named-destination listing
// ---------------------------------------------------------------------------
// Named destinations are the document's table of anchors: /Dests entries that
// GoTo actions and external links target by name rather than by page. Listing
// them turns "#Chapter2" in an incoming link into a page index, and gives a
// document's internal structure without walking every link on every page.

inline const char *DestinationViewString(unsigned long view) {
  switch (view) {
  case PDFDEST_VIEW_XYZ:
    return "xyz";
  case PDFDEST_VIEW_FIT:
    return "fit";
  case PDFDEST_VIEW_FITH:
    return "fitH";
  case PDFDEST_VIEW_FITV:
    return "fitV";
  case PDFDEST_VIEW_FITR:
    return "fitR";
  case PDFDEST_VIEW_FITB:
    return "fitB";
  case PDFDEST_VIEW_FITBH:
    return "fitBH";
  case PDFDEST_VIEW_FITBV:
    return "fitBV";
  default:
    return "unknown";
  }
}

struct NamedDestinationInfo {
  std::u16string name;
  int pageIndex = -1;
  const char *view = "unknown";
  std::vector<float> viewParams;
  bool hasX = false, hasY = false, hasZoom = false;
  float x = 0, y = 0, zoom = 0;
};

class GetNamedDestinationsWorker : public SafeAsyncWorker {
public:
  GetNamedDestinationsWorker(Napi::Env env, FPDF_DOCUMENT doc,
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

    unsigned long count = FPDF_CountNamedDests(doc_);
    if (count == 0)
      return;

    destinations_.reserve(count);
    for (unsigned long i = 0; i < count; i++) {
      NamedDestinationInfo info;

      // FPDF_GetNamedDest uses an in/out byte length rather than the usual
      // two-pass protocol, and despite the header calling the buffer a
      // wchar_t* it is written as UTF-16LE on every platform.
      long nameLen = 0;
      FPDF_DEST dest =
          FPDF_GetNamedDest(doc_, static_cast<int>(i), nullptr, &nameLen);
      if (!dest)
        continue;

      if (nameLen >= 2 && nameLen % 2 == 0) {
        std::vector<unsigned short> buf(static_cast<size_t>(nameLen) /
                                        sizeof(unsigned short));
        long outLen = nameLen;
        FPDF_GetNamedDest(doc_, static_cast<int>(i), buf.data(), &outLen);
        if (outLen == nameLen) {
          info.name = std::u16string(reinterpret_cast<const char16_t *>(
                                         buf.data()),
                                     buf.size() - 1);
        }
      }

      info.pageIndex = FPDFDest_GetDestPageIndex(doc_, dest);

      unsigned long numParams = 0;
      FS_FLOAT params[4] = {0, 0, 0, 0};
      unsigned long view = FPDFDest_GetView(dest, &numParams, params);
      info.view = DestinationViewString(view);
      if (numParams > 4)
        numParams = 4;
      info.viewParams.assign(params, params + numParams);

      FPDF_BOOL hasX = 0, hasY = 0, hasZoom = 0;
      FS_FLOAT x = 0, y = 0, zoom = 0;
      if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y,
                                     &zoom)) {
        info.hasX = hasX != 0;
        info.hasY = hasY != 0;
        info.hasZoom = hasZoom != 0;
        info.x = x;
        info.y = y;
        info.zoom = zoom;
      }

      destinations_.push_back(std::move(info));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, destinations_.size());
    for (uint32_t i = 0; i < destinations_.size(); i++) {
      const auto &info = destinations_[i];
      Napi::Object obj = Napi::Object::New(env);
      SetU16(obj, "name", env, info.name);
      if (info.pageIndex >= 0)
        obj.Set("pageIndex", Napi::Number::New(env, info.pageIndex));
      obj.Set("view", Napi::String::New(env, info.view));

      Napi::Array params = Napi::Array::New(env, info.viewParams.size());
      for (uint32_t j = 0; j < info.viewParams.size(); j++) {
        params.Set(j, Napi::Number::New(env, info.viewParams[j]));
      }
      obj.Set("viewParams", params);

      if (info.hasX)
        obj.Set("destX", Napi::Number::New(env, info.x));
      if (info.hasY)
        obj.Set("destY", Napi::Number::New(env, info.y));
      if (info.hasZoom)
        obj.Set("destZoom", Napi::Number::New(env, info.zoom));

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
  std::vector<NamedDestinationInfo> destinations_;
};
