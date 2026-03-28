#pragma once

#include <atomic>
#include <mutex>
#include <napi.h>
#include <string>
#include <vector>

#include "fpdf_doc.h"
#include "fpdfview.h"

inline std::atomic<bool> g_initialized{false};

// PDFium is not thread-safe — serialize all calls through a global mutex.
// This unblocks the Node.js event loop while waiting for the lock.
inline std::mutex g_pdfium_mutex;

// image format constants
enum ImageFormat { IMAGE_FORMAT_JPEG = 0, IMAGE_FORMAT_PNG = 1 };

// maximum render dimension to prevent OOM (16384 × 16384 × 4 ≈ 1 GB)
constexpr int MAX_RENDER_DIMENSION = 16384;

// maximum bookmark tree recursion depth
constexpr int MAX_BOOKMARK_DEPTH = 64;

// common alive-check macro for all page workers
#define CHECK_ALIVE()                                                          \
  if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {              \
    SetError("Page or document was closed");                                   \
    return;                                                                    \
  }

// ---------------------------------------------------------------------------
// PDFium UTF-16 string helpers
// ---------------------------------------------------------------------------

// read a PDFium UTF-16LE buffer into a std::u16string.
// `getLen` returns the byte length (including null terminator).
// `getData` writes into the provided buffer and returns the byte length.
template <typename GetLen, typename GetData>
inline std::u16string ReadU16(GetLen getLen, GetData getData) {
  unsigned long len = getLen(static_cast<FPDF_WCHAR *>(nullptr), 0);
  if (len <= 2)
    return {};
  std::vector<unsigned short> buf(len / sizeof(unsigned short));
  getData(reinterpret_cast<FPDF_WCHAR *>(buf.data()), len);
  return std::u16string(reinterpret_cast<const char16_t *>(buf.data()),
                        len / sizeof(unsigned short) - 1);
}

// convert a std::u16string to a Napi::String (empty string if input is empty)
inline Napi::String ToNapiString(Napi::Env env, const std::u16string &s) {
  if (s.empty())
    return Napi::String::New(env, "");
  return Napi::String::New(env, reinterpret_cast<const char16_t *>(s.data()),
                           s.size());
}

// set a Napi object property from a u16string (always sets, uses "" if empty)
inline void SetU16(Napi::Object &obj, const char *key, Napi::Env env,
                   const std::u16string &s) {
  obj.Set(key, ToNapiString(env, s));
}

// set a Napi object property from a u16string only if non-empty
inline void SetU16IfPresent(Napi::Object &obj, const char *key, Napi::Env env,
                            const std::u16string &s) {
  if (!s.empty())
    obj.Set(key, ToNapiString(env, s));
}

// ---------------------------------------------------------------------------
// action type string from FPDF action type constant
// ---------------------------------------------------------------------------

inline const char *ActionTypeString(unsigned long actionType) {
  switch (actionType) {
  case PDFACTION_GOTO:
    return "goto";
  case PDFACTION_REMOTEGOTO:
    return "remoteGoto";
  case PDFACTION_URI:
    return "uri";
  case PDFACTION_LAUNCH:
    return "launch";
  case PDFACTION_EMBEDDEDGOTO:
    return "embeddedGoto";
  default:
    return "unknown";
  }
}

// ---------------------------------------------------------------------------
// Napi object helpers
// ---------------------------------------------------------------------------

// helper: create a { left, bottom, right, top } bounds object
inline Napi::Object CreateBoundsObject(Napi::Env env, double left,
                                       double bottom, double right,
                                       double top) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set("left", Napi::Number::New(env, left));
  obj.Set("bottom", Napi::Number::New(env, bottom));
  obj.Set("right", Napi::Number::New(env, right));
  obj.Set("top", Napi::Number::New(env, top));
  return obj;
}

// helper: create bounds from an FS_RECTF struct
inline Napi::Object CreateBoundsFromRect(Napi::Env env, const FS_RECTF &rect) {
  return CreateBoundsObject(env, rect.left, rect.bottom, rect.right, rect.top);
}

// helper: create an { r, g, b, a } color object
inline Napi::Object CreateColorObject(Napi::Env env, unsigned int r,
                                      unsigned int g, unsigned int b,
                                      unsigned int a) {
  Napi::Object obj = Napi::Object::New(env);
  obj.Set("r", Napi::Number::New(env, r));
  obj.Set("g", Napi::Number::New(env, g));
  obj.Set("b", Napi::Number::New(env, b));
  obj.Set("a", Napi::Number::New(env, a));
  return obj;
}
