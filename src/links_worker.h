#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_doc.h"

// ---------------------------------------------------------------------------
// GetLinksWorker — async link extraction
// ---------------------------------------------------------------------------

struct LinkData {
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  std::string url;
  int pageIndex = -1;
  bool hasPageIndex = false;
  std::string actionType;
  // destination location
  float destX = 0, destY = 0, destZoom = 0;
  bool hasDestX = false, hasDestY = false, hasDestZoom = false;
  std::string filePath;
};

class GetLinksWorker : public SafeAsyncWorker {
public:
  GetLinksWorker(Napi::Env env, FPDF_PAGE page, FPDF_DOCUMENT doc,
                 std::shared_ptr<std::atomic<bool>> pageAlive,
                 std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), doc_(doc), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    int pos = 0;
    FPDF_LINK link;
    while (FPDFLink_Enumerate(page_, &pos, &link)) {
      LinkData data;

      FS_RECTF rect;
      if (FPDFLink_GetAnnotRect(link, &rect)) {
        data.hasBounds = true;
        data.left = rect.left;
        data.bottom = rect.bottom;
        data.right = rect.right;
        data.top = rect.top;
      }

      FPDF_ACTION action = FPDFLink_GetAction(link);
      if (action) {
        unsigned long actionType = FPDFAction_GetType(action);
        data.actionType = ActionTypeString(actionType);

        if (actionType == PDFACTION_URI) {
          unsigned long len = FPDFAction_GetURIPath(doc_, action, nullptr, 0);
          if (len > 0) {
            std::vector<char> buf(len);
            FPDFAction_GetURIPath(doc_, action, buf.data(), len);
            data.url = std::string(buf.data(), len - 1);
          }
        }

        // file path for remote goto / launch actions
        if (actionType == PDFACTION_REMOTEGOTO ||
            actionType == PDFACTION_LAUNCH) {
          unsigned long fpLen = FPDFAction_GetFilePath(action, nullptr, 0);
          if (fpLen > 0) {
            std::vector<char> fpBuf(fpLen);
            FPDFAction_GetFilePath(action, fpBuf.data(), fpLen);
            data.filePath = std::string(fpBuf.data(), fpLen - 1);
          }
        }
      }

      FPDF_DEST dest = FPDFLink_GetDest(doc_, link);
      if (!dest && action) {
        dest = FPDFAction_GetDest(doc_, action);
      }
      if (dest) {
        int idx = FPDFDest_GetDestPageIndex(doc_, dest);
        if (idx >= 0) {
          data.hasPageIndex = true;
          data.pageIndex = idx;
        }

        FPDF_BOOL hasX, hasY, hasZoom;
        FS_FLOAT x, y, zoom;
        if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y,
                                       &zoom)) {
          if (hasX) {
            data.hasDestX = true;
            data.destX = x;
          }
          if (hasY) {
            data.hasDestY = true;
            data.destY = y;
          }
          if (hasZoom) {
            data.hasDestZoom = true;
            data.destZoom = zoom;
          }
        }
      }

      links_.push_back(std::move(data));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, links_.size());
    for (uint32_t i = 0; i < links_.size(); i++) {
      auto &d = links_[i];
      Napi::Object obj = Napi::Object::New(env);
      if (d.hasBounds)
        obj.Set("bounds",
                CreateBoundsObject(env, d.left, d.bottom, d.right, d.top));
      if (!d.url.empty())
        obj.Set("url", Napi::String::New(env, d.url));
      if (d.hasPageIndex)
        obj.Set("pageIndex", Napi::Number::New(env, d.pageIndex));
      if (!d.actionType.empty())
        obj.Set("actionType", Napi::String::New(env, d.actionType));
      if (d.hasDestX)
        obj.Set("destX", Napi::Number::New(env, d.destX));
      if (d.hasDestY)
        obj.Set("destY", Napi::Number::New(env, d.destY));
      if (d.hasDestZoom)
        obj.Set("destZoom", Napi::Number::New(env, d.destZoom));
      if (!d.filePath.empty())
        obj.Set("filePath", Napi::String::New(env, d.filePath));
      arr.Set(i, obj);
    }
    deferred_.Resolve(arr);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<LinkData> links_;
};
