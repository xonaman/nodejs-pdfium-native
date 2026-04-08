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
// Per-environment alive flag for worker thread safety
// ---------------------------------------------------------------------------
// When a Node.js worker thread is terminated (worker.terminate()), it calls
// TerminateExecution() + set_can_call_into_js(false) and then drains the libuv
// event loop. Async work completions fire during this drain. The base
// AsyncWorker::OnWorkComplete creates a HandleScope which crashes if V8 is
// shutting down ("Cannot create a handle without a HandleScope" or "Entering
// the V8 API without proper locking").
//
// We use three layers of defense:
//
// Layer 1 — C-level env probe: napi_throw(env, nullptr) uses NAPI_PREAMBLE
//   which checks can_call_into_js(). worker.terminate() sets this to false
//   BEFORE draining the loop, so completions see it immediately. Returns
//   napi_invalid_arg when alive (null value rejected), napi_cannot_run_js
//   when shutting down. No V8 calls in either path.
//
// Layer 2 — envAlive_ atomic + cleanup hook: the cleanup hook fires during
//   RunCleanup() after the uv drain, catching any completions that arrive
//   during or after the cleanup phase.
//
// Layer 3 — prepareShutdown() JS export: called from the worker thread JS
//   code before it signals readiness for termination. Sets envAlive_ to
//   false while V8 is fully alive (belt-and-suspenders).

struct AddonData {
  std::shared_ptr<std::atomic<bool>> envAlive =
      std::make_shared<std::atomic<bool>>(true);

  // per-env constructor references — must NOT be static globals because each
  // worker thread has its own V8 isolate and env. A static global would be
  // overwritten by the last thread to call Init(), causing cross-isolate use.
  Napi::FunctionReference docConstructor;
  Napi::FunctionReference pageConstructor;
};

inline std::shared_ptr<std::atomic<bool>> GetEnvAlive(Napi::Env env) {
  auto *data = env.GetInstanceData<AddonData>();
  return data ? data->envAlive : nullptr;
}

// ---------------------------------------------------------------------------
// SafeAsyncWorker — guards OnWorkComplete against env teardown
// ---------------------------------------------------------------------------
// All async workers MUST inherit from this class instead of Napi::AsyncWorker
// to be safe for use in worker threads that may be terminated at any time.
//
// OnWorkComplete is overridden to skip ALL V8 API calls when the environment
// is no longer alive. The JS promise will never settle, but the worker thread
// is being destroyed anyway — there is no listener for the result.

class SafeAsyncWorker : public Napi::AsyncWorker {
public:
  void OnWorkComplete(Napi::Env env, napi_status status) override {
    // Layer 2: cleanup hook already fired — env is being torn down
    if (envAlive_ && !envAlive_->load())
      return;

    // Layer 1: probe whether V8 is still accessible by attempting the exact
    // operation that crashes in the base class: opening a handle scope.
    // napi_open_handle_scope is a raw C call — no C++ exception on failure.
    // worker.terminate() races with this: the parent sets can_call_into_js
    // to false and calls TerminateExecution, but with relaxed atomics the
    // worker thread may not see the flag yet. If the scope open fails here,
    // we know the env is dead and bail out immediately.
    napi_handle_scope scope = nullptr;
    if (napi_open_handle_scope(env, &scope) != napi_ok)
      return;
    napi_close_handle_scope(env, scope);

    // safety net: catch Napi::Error in case the env becomes unusable between
    // our probe and the base class's HandleScope / OnOK / OnError calls.
    // The base OnWorkComplete creates a HandleScope OUTSIDE WrapCallback's
    // try-catch, so a failure there throws an uncaught Napi::Error.
    try {
      Napi::AsyncWorker::OnWorkComplete(env, status);
    } catch (const Napi::Error &) {
      // env tore down between our probe and the base class call — swallow
    }
  }

protected:
  std::shared_ptr<std::atomic<bool>> envAlive_;

  explicit SafeAsyncWorker(Napi::Env env)
      : Napi::AsyncWorker(env), envAlive_(GetEnvAlive(env)) {}
};

// ---------------------------------------------------------------------------
// PDFium UTF-16 string helpers
// ---------------------------------------------------------------------------

// read a PDFium UTF-16LE buffer into a std::u16string.
// `getLen` returns the byte length (including null terminator).
// `getData` writes into the provided buffer and returns the byte length.
template <typename GetLen, typename GetData>
inline std::u16string ReadU16(GetLen getLen, GetData getData) {
  unsigned long len = getLen(static_cast<FPDF_WCHAR *>(nullptr), 0);
  if (len < 4 || len % 2 != 0)
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
