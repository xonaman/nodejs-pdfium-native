#pragma once

#include <atomic>
#include <mutex>
#include <napi.h>

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
