#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_doc.h"

// ---------------------------------------------------------------------------
// GetBookmarksWorker — async bookmark extraction
// ---------------------------------------------------------------------------

struct BookmarkData {
  std::u16string title;
  int pageIndex = -1;
  bool hasPageIndex = false;
  bool open = false;
  std::string actionType;
  std::string url;
  float destX = 0, destY = 0, destZoom = 0;
  bool hasDestX = false, hasDestY = false, hasDestZoom = false;
  std::vector<BookmarkData> children;
};

class GetBookmarksWorker : public Napi::AsyncWorker {
public:
  GetBookmarksWorker(Napi::Env env, FPDF_DOCUMENT doc,
                     std::shared_ptr<std::atomic<bool>> docAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        doc_(doc), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    if (!docAlive_->load()) {
      SetError("Document was destroyed");
      return;
    }
    CollectBookmarks(nullptr, bookmarks_, 0);
  }

  void OnOK() override { deferred_.Resolve(BuildArray(Env(), bookmarks_)); }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<BookmarkData> bookmarks_;

  // maximum siblings per level to prevent DoS from malicious PDFs
  static constexpr int MAX_SIBLINGS_PER_LEVEL = 10000;

  void CollectBookmarks(FPDF_BOOKMARK parent, std::vector<BookmarkData> &out,
                        int depth) {
    if (depth >= MAX_BOOKMARK_DEPTH)
      return;

    int siblingCount = 0;
    FPDF_BOOKMARK child = FPDFBookmark_GetFirstChild(doc_, parent);
    while (child && siblingCount < MAX_SIBLINGS_PER_LEVEL) {
      siblingCount++;
      BookmarkData data;

      data.title = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFBookmark_GetTitle(child, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFBookmark_GetTitle(child, buf, len);
          });

      // open/closed state: positive count = open, negative = closed
      int count = FPDFBookmark_GetCount(child);
      data.open = count > 0;

      FPDF_DEST dest = FPDFBookmark_GetDest(doc_, child);
      FPDF_ACTION action = FPDFBookmark_GetAction(child);

      if (!dest && action) {
        dest = FPDFAction_GetDest(doc_, action);
      }

      if (dest) {
        data.hasPageIndex = true;
        data.pageIndex = FPDFDest_GetDestPageIndex(doc_, dest);

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

      if (action) {
        unsigned long actionType = FPDFAction_GetType(action);
        data.actionType = ActionTypeString(actionType);

        if (actionType == PDFACTION_URI) {
          unsigned long uriLen =
              FPDFAction_GetURIPath(doc_, action, nullptr, 0);
          if (uriLen > 0) {
            std::vector<char> uriBuf(uriLen);
            FPDFAction_GetURIPath(doc_, action, uriBuf.data(), uriLen);
            data.url = std::string(uriBuf.data(), uriLen - 1);
          }
        }
      }

      CollectBookmarks(child, data.children, depth + 1);
      out.push_back(std::move(data));
      child = FPDFBookmark_GetNextSibling(doc_, child);
    }
  }

  Napi::Array BuildArray(Napi::Env env,
                         const std::vector<BookmarkData> &items) {
    Napi::Array arr = Napi::Array::New(env, items.size());
    for (uint32_t i = 0; i < items.size(); i++) {
      auto &d = items[i];
      Napi::Object obj = Napi::Object::New(env);
      SetU16(obj, "title", env, d.title);
      if (d.hasPageIndex)
        obj.Set("pageIndex", Napi::Number::New(env, d.pageIndex));
      obj.Set("open", Napi::Boolean::New(env, d.open));
      if (!d.actionType.empty())
        obj.Set("actionType", Napi::String::New(env, d.actionType));
      if (!d.url.empty())
        obj.Set("url", Napi::String::New(env, d.url));
      if (d.hasDestX)
        obj.Set("destX", Napi::Number::New(env, d.destX));
      if (d.hasDestY)
        obj.Set("destY", Napi::Number::New(env, d.destY));
      if (d.hasDestZoom)
        obj.Set("destZoom", Napi::Number::New(env, d.destZoom));
      if (!d.children.empty())
        obj.Set("children", BuildArray(env, d.children));
      arr.Set(i, obj);
    }
    return arr;
  }
};
