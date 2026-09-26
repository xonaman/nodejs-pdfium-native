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

// read page index, fit type and view parameters out of a resolved FPDF_DEST
inline void ReadDestination(FPDF_DOCUMENT doc, FPDF_DEST dest,
                            NamedDestinationInfo &info) {
  info.pageIndex = FPDFDest_GetDestPageIndex(doc, dest);

  unsigned long numParams = 0;
  FS_FLOAT params[4] = {0, 0, 0, 0};
  unsigned long view = FPDFDest_GetView(dest, &numParams, params);
  info.view = DestinationViewString(view);
  if (numParams > 4)
    numParams = 4;
  info.viewParams.assign(params, params + numParams);

  FPDF_BOOL hasX = 0, hasY = 0, hasZoom = 0;
  FS_FLOAT x = 0, y = 0, zoom = 0;
  if (FPDFDest_GetLocationInPage(dest, &hasX, &hasY, &hasZoom, &x, &y, &zoom)) {
    info.hasX = hasX != 0;
    info.hasY = hasY != 0;
    info.hasZoom = hasZoom != 0;
    info.x = x;
    info.y = y;
    info.zoom = zoom;
  }
}

inline Napi::Object DestinationToObject(Napi::Env env,
                                        const NamedDestinationInfo &info) {
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
  return obj;
}

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
      // A null here is ambiguous and must NOT be reported as a failed entry
      // the way the other counted listings do. FPDF_GetNamedDest has two
      // lookup paths: indices below the /Names /Dests name-tree count go
      // through the tree, which dereferences indirect objects, while indices
      // at or above it fall through to the legacy catalog /Dests dictionary,
      // which does not. FPDF_CountNamedDests counts every legacy key all the
      // same, so a perfectly legal entry whose value is an indirect reference
      // to a destination array is counted and then returns null -- PDF
      // 32000-1 allows an indirect reference anywhere a direct object is, and
      // PDFium resolves those same destinations correctly through its own
      // name lookup. Surfacing them as nulls would flag healthy documents as
      // damaged. The legacy boundary is the total count minus the name-tree
      // count, and PDFium exposes no way to ask for the latter, so the two
      // cases cannot be told apart here.
      //
      // Nor can such an entry be reported under its name: when the lookup
      // fails, nameLen is left at 0 and nothing is written to the buffer, so
      // the index API yields neither a destination nor a name. That is why
      // this listing is the one place in the library that can come back
      // shorter than its count. metadata.namedDestinationCount exposes the
      // count so the gap is visible, and getNamedDestination(name) reaches
      // these destinations through FPDF_GetNamedDestByName, which resolves
      // them -- so a caller holding a name from an incoming link is not stuck
      // with what this loop could not enumerate.
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

      ReadDestination(doc_, dest, info);
      destinations_.push_back(std::move(info));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, destinations_.size());
    for (uint32_t i = 0; i < destinations_.size(); i++) {
      arr.Set(i, DestinationToObject(env, destinations_[i]));
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

// ---------------------------------------------------------------------------
// GetNamedDestinationWorker — async lookup of a single destination by name
// ---------------------------------------------------------------------------
// FPDF_GetNamedDestByName searches the /Names /Dests name tree and the legacy
// catalog /Dests dictionary alike, and dereferences an indirect value in
// either. It therefore resolves destinations the index-based listing above
// cannot reach, which is the whole reason this entry point exists: resolving
// the "#Chapter2" of an incoming link is the listing's stated use, and for a
// document whose legacy /Dests values are indirect the listing yields nothing
// to match against.
//
// The name is passed through as UTF-8. PDF name-tree keys are byte strings and
// PDFium compares them as such, so an ASCII name -- what producers emit in
// practice -- matches exactly, while a key stored in UTF-16BE or PDFDocEncoding
// will not be found by its decoded form. Resolves to null when no destination
// carries the name, which is an answer rather than an error.

class GetNamedDestinationWorker : public SafeAsyncWorker {
public:
  GetNamedDestinationWorker(Napi::Env env, FPDF_DOCUMENT doc, std::string name,
                            std::u16string nameU16,
                            std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        doc_(doc), name_(std::move(name)), nameU16_(std::move(nameU16)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    if (!docAlive_ || !docAlive_->load()) {
      SetError("Document was destroyed");
      return;
    }

    FPDF_DEST dest = FPDF_GetNamedDestByName(doc_, name_.c_str());
    if (!dest)
      return;

    // echo the name back: the by-name path returns no name of its own, and an
    // entry that matched carries exactly the one that was asked for. The
    // caller's own UTF-16 string is used rather than widening the UTF-8 bytes,
    // which would mangle anything outside ASCII.
    info_.name = nameU16_;
    ReadDestination(doc_, dest, info_);
    found_ = true;
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (!found_) {
      deferred_.Resolve(env.Null());
      return;
    }
    deferred_.Resolve(DestinationToObject(env, info_));
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_DOCUMENT doc_;
  std::string name_;
  std::u16string nameU16_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  NamedDestinationInfo info_;
  bool found_ = false;
};
