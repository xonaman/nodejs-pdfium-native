#include "document.h"

#include <climits>
#include <cstring>

#include "fpdf_attachment.h"
#include "fpdf_catalog.h"
#include "fpdf_signature.h"
#include "split_merge_worker.h"

// ---------------------------------------------------------------------------
// Module-level: loadDocument (async)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// LoadDocumentWorker — async document loading
// ---------------------------------------------------------------------------

class LoadDocumentWorker : public SafeAsyncWorker {
public:
  // buffer variant — copies data since buffer may be GC'd
  LoadDocumentWorker(Napi::Env env, std::vector<uint8_t> data,
                     std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  LoadDocumentWorker(Napi::Env env, std::string path, std::string password)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        filePath_(std::move(path)), password_(std::move(password)),
        useFile_(true) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    const char *pw = password_.empty() ? nullptr : password_.c_str();

    if (useFile_) {
      doc_ = FPDF_LoadDocument(filePath_.c_str(), pw);
    } else {
      if (bufferData_.size() > static_cast<size_t>(INT_MAX)) {
        SetError("FORMAT:Buffer too large");
        return;
      }
      doc_ = FPDF_LoadMemDocument(bufferData_.data(),
                                  static_cast<int>(bufferData_.size()), pw);
    }

    if (!doc_) {
      SetError(GetPdfiumErrorMessage());
      return;
    }

    pageCount_ = FPDF_GetPageCount(doc_);

    // read metadata under the mutex
    const char *metaTags[] = {"Title",   "Author",   "Subject",      "Keywords",
                              "Creator", "Producer", "CreationDate", "ModDate"};
    for (int i = 0; i < 8; i++) {
      meta_[i] = ReadU16(
          [&](auto *, unsigned long) {
            return FPDF_GetMetaText(doc_, metaTags[i], nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDF_GetMetaText(doc_, metaTags[i], buf, len);
          });
    }
    FPDF_GetFileVersion(doc_, &pdfVersion_);
    permissions_ = FPDF_GetDocPermissions(doc_);

    // tagged PDF flag
    isTagged_ = FPDFCatalog_IsTagged(doc_) != 0;

    // document language (UTF-16LE encoded)
    language_ = ReadU16(
        [&](auto *, unsigned long) {
          return FPDFCatalog_GetLanguage(doc_, nullptr, 0);
        },
        [&](FPDF_WCHAR *buf, unsigned long len) {
          return FPDFCatalog_GetLanguage(doc_, buf, len);
        });

    // signature and attachment counts
    signatureCount_ = FPDF_GetSignatureCount(doc_);
    attachmentCount_ = FPDFDoc_GetAttachmentCount(doc_);

    // file identifiers (raw byte strings → hex)
    auto readFileId = [&](FPDF_FILEIDTYPE idType) -> std::string {
      unsigned long len = FPDF_GetFileIdentifier(doc_, idType, nullptr, 0);
      if (len <= 1)
        return "";
      std::vector<char> buf(len);
      FPDF_GetFileIdentifier(doc_, idType, buf.data(), len);
      // convert raw bytes to hex string
      static const char hex[] = "0123456789abcdef";
      std::string result;
      result.reserve((len - 1) * 2);
      for (unsigned long j = 0; j < len - 1; j++) {
        unsigned char c = static_cast<unsigned char>(buf[j]);
        result.push_back(hex[c >> 4]);
        result.push_back(hex[c & 0x0F]);
      }
      return result;
    };
    permanentId_ = readFileId(FILEIDTYPE_PERMANENT);
    changingId_ = readFileId(FILEIDTYPE_CHANGING);
  }

  void OnOK() override {
    Napi::Env env = Env();
    auto *addon = Env().GetInstanceData<AddonData>();
    Napi::Object docObj = addon->docConstructor.New({});
    PDFiumDocument *docWrapper = PDFiumDocument::Unwrap(docObj);
    docWrapper->SetDocument(doc_);

    docObj.Set("pageCount", Napi::Number::New(env, pageCount_));

    // set metadata as a plain JS property
    const char *metaKeys[] = {"title",   "author",   "subject",      "keywords",
                              "creator", "producer", "creationDate", "modDate"};
    Napi::Object metaObj = Napi::Object::New(env);
    for (int i = 0; i < 8; i++) {
      SetU16(metaObj, metaKeys[i], env, meta_[i]);
    }
    metaObj.Set("pdfVersion", Napi::Number::New(env, pdfVersion_));

    // permission flags (ISO 32000-1:2008, Table 22)
    Napi::Object permObj = Napi::Object::New(env);
    permObj.Set("print",
                Napi::Boolean::New(env, (permissions_ & (1 << 2)) != 0));
    permObj.Set("modify",
                Napi::Boolean::New(env, (permissions_ & (1 << 3)) != 0));
    permObj.Set("copy",
                Napi::Boolean::New(env, (permissions_ & (1 << 4)) != 0));
    permObj.Set("annotate",
                Napi::Boolean::New(env, (permissions_ & (1 << 5)) != 0));
    permObj.Set("fillForms",
                Napi::Boolean::New(env, (permissions_ & (1 << 8)) != 0));
    permObj.Set("extractForAccessibility",
                Napi::Boolean::New(env, (permissions_ & (1 << 9)) != 0));
    permObj.Set("assemble",
                Napi::Boolean::New(env, (permissions_ & (1 << 10)) != 0));
    permObj.Set("printHighQuality",
                Napi::Boolean::New(env, (permissions_ & (1 << 11)) != 0));
    metaObj.Set("permissions", permObj);

    metaObj.Set("isTagged", Napi::Boolean::New(env, isTagged_));
    SetU16(metaObj, "language", env, language_);
    metaObj.Set("signatureCount", Napi::Number::New(env, signatureCount_));
    metaObj.Set("attachmentCount", Napi::Number::New(env, attachmentCount_));
    if (!permanentId_.empty())
      metaObj.Set("permanentId", Napi::String::New(env, permanentId_));
    if (!changingId_.empty())
      metaObj.Set("changingId", Napi::String::New(env, changingId_));

    docObj.Set("metadata", metaObj);

    // for buffer variant, transfer ownership of the copied data
    // the data must stay alive as long as the document is open
    if (!useFile_ && !bufferData_.empty()) {
      docWrapper->SetOwnedBuffer(std::move(bufferData_));
    }

    deferred_.Resolve(docObj);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  std::vector<uint8_t> bufferData_;
  std::string filePath_;
  std::string password_;
  bool useFile_;
  FPDF_DOCUMENT doc_ = nullptr;
  int pageCount_ = 0;
  std::u16string meta_[8];
  int pdfVersion_ = 0;
  unsigned long permissions_ = 0xFFFFFFFF;
  bool isTagged_ = false;
  std::u16string language_;
  int signatureCount_ = 0;
  int attachmentCount_ = 0;
  std::string permanentId_;
  std::string changingId_;
};

/**
 * Opens a PDF document from a Buffer or file path. Returns a
 * Promise<PDFiumDocument>.
 */
Napi::Value LoadDocument(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected a Buffer or string path argument")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Value arg = info[0];
  std::string password;
  if (info.Length() > 1 && info[1].IsString()) {
    password = info[1].As<Napi::String>().Utf8Value();
  }

  LoadDocumentWorker *worker = nullptr;

  if (arg.IsBuffer()) {
    auto buffer = arg.As<Napi::Buffer<uint8_t>>();
    // copy buffer data — original JS buffer may be GC'd before worker runs
    std::vector<uint8_t> data(buffer.Data(), buffer.Data() + buffer.Length());
    worker = new LoadDocumentWorker(env, std::move(data), std::move(password));
  } else if (arg.IsString()) {
    std::string path = arg.As<Napi::String>().Utf8Value();
    worker = new LoadDocumentWorker(env, std::move(path), std::move(password));
  } else {
    Napi::TypeError::New(env, "Expected a Buffer or string path argument")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}

// ---------------------------------------------------------------------------
// splitDocument — split a PDF into multiple documents (async)
// ---------------------------------------------------------------------------

Napi::Value SplitDocument(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (input, splitAt, options?) arguments")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Value arg = info[0];
  Napi::Array jsSplitAt = info[1].As<Napi::Array>();

  std::vector<int> splitAt;
  splitAt.reserve(jsSplitAt.Length());
  for (uint32_t i = 0; i < jsSplitAt.Length(); i++) {
    splitAt.push_back(jsSplitAt.Get(i).As<Napi::Number>().Int32Value());
  }

  std::vector<std::string> outputPaths;
  std::string password;
  if (info.Length() > 2 && info[2].IsObject()) {
    Napi::Object opts = info[2].As<Napi::Object>();
    if (opts.Has("outputs") && opts.Get("outputs").IsArray()) {
      Napi::Array jsOutputs = opts.Get("outputs").As<Napi::Array>();
      outputPaths.reserve(jsOutputs.Length());
      for (uint32_t i = 0; i < jsOutputs.Length(); i++) {
        outputPaths.push_back(jsOutputs.Get(i).As<Napi::String>().Utf8Value());
      }
    }
    if (opts.Has("password") && opts.Get("password").IsString()) {
      password = opts.Get("password").As<Napi::String>().Utf8Value();
    }
  }

  SplitDocumentWorker *worker = nullptr;

  if (arg.IsBuffer()) {
    auto buffer = arg.As<Napi::Buffer<uint8_t>>();
    std::vector<uint8_t> data(buffer.Data(), buffer.Data() + buffer.Length());
    worker =
        new SplitDocumentWorker(env, std::move(data), std::move(splitAt),
                                std::move(outputPaths), std::move(password));
  } else if (arg.IsString()) {
    std::string path = arg.As<Napi::String>().Utf8Value();
    worker =
        new SplitDocumentWorker(env, std::move(path), std::move(splitAt),
                                std::move(outputPaths), std::move(password));
  } else {
    Napi::TypeError::New(env, "Expected a Buffer or string path argument")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}

// ---------------------------------------------------------------------------
// mergeDocuments — combine multiple PDFs (async)
// ---------------------------------------------------------------------------

Napi::Value MergeDocuments(const Napi::CallbackInfo &info) {
  Napi::Env env = info.Env();

  if (info.Length() < 1 || !info[0].IsArray()) {
    Napi::TypeError::New(env, "Expected (inputs[], options?) arguments")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  Napi::Array jsInputs = info[0].As<Napi::Array>();
  if (jsInputs.Length() == 0) {
    Napi::TypeError::New(env, "At least one input document is required")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string outputPath;
  if (info.Length() > 1 && info[1].IsObject()) {
    Napi::Object opts = info[1].As<Napi::Object>();
    if (opts.Has("output") && opts.Get("output").IsString()) {
      outputPath = opts.Get("output").As<Napi::String>().Utf8Value();
    }
  }

  std::vector<DocInput> inputs;
  inputs.reserve(jsInputs.Length());

  for (uint32_t i = 0; i < jsInputs.Length(); i++) {
    Napi::Value item = jsInputs.Get(i);
    DocInput di;

    // each item is either a Buffer/string, or { input, password }
    if (item.IsBuffer()) {
      auto buffer = item.As<Napi::Buffer<uint8_t>>();
      di.bufferData.assign(buffer.Data(), buffer.Data() + buffer.Length());
      di.useFile = false;
    } else if (item.IsString()) {
      di.filePath = item.As<Napi::String>().Utf8Value();
      di.useFile = true;
    } else if (item.IsObject()) {
      Napi::Object obj = item.As<Napi::Object>();
      if (!obj.Has("input")) {
        Napi::TypeError::New(env, "Each input must have an 'input' property")
            .ThrowAsJavaScriptException();
        return env.Null();
      }
      Napi::Value inp = obj.Get("input");
      if (inp.IsBuffer()) {
        auto buffer = inp.As<Napi::Buffer<uint8_t>>();
        di.bufferData.assign(buffer.Data(), buffer.Data() + buffer.Length());
        di.useFile = false;
      } else if (inp.IsString()) {
        di.filePath = inp.As<Napi::String>().Utf8Value();
        di.useFile = true;
      } else {
        Napi::TypeError::New(env, "input must be a Buffer or string path")
            .ThrowAsJavaScriptException();
        return env.Null();
      }
      if (obj.Has("password") && obj.Get("password").IsString()) {
        di.password = obj.Get("password").As<Napi::String>().Utf8Value();
      }
    } else {
      Napi::TypeError::New(env,
                           "Each input must be a Buffer, string, or object")
          .ThrowAsJavaScriptException();
      return env.Null();
    }

    inputs.push_back(std::move(di));
  }

  auto *worker =
      new MergeDocumentsWorker(env, std::move(inputs), std::move(outputPath));
  auto promise = worker->Promise();
  worker->Queue();
  return promise;
}

// ---------------------------------------------------------------------------
// Module init
// ---------------------------------------------------------------------------

/**
 * Marks the environment as shutting down so in-flight async workers bail
 * out of OnWorkComplete instead of creating a HandleScope on a dead isolate.
 * Called from worker thread JS before signaling readiness for termination.
 */
Napi::Value PrepareShutdown(const Napi::CallbackInfo &info) {
  auto envAlive = GetEnvAlive(info.Env());
  if (envAlive)
    envAlive->store(false);
  return info.Env().Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  // per-environment alive flag for async worker safety (Layer 2+3)
  auto *addonData = new AddonData();
  env.SetInstanceData(addonData);
  auto envAlive = addonData->envAlive;

  // Layer 2: cleanup hook — fires during RunCleanup() after uv drain
  env.AddCleanupHook([addonData]() {
    addonData->envAlive->store(false);
    delete addonData;
  });

  if (g_pdfium_refcount.fetch_add(1) == 0) {
    FPDF_InitLibrary();
  }

  // decrement ref count on environment teardown; destroy when last env exits
  env.AddCleanupHook([]() {
    if (g_pdfium_refcount.fetch_sub(1) == 1) {
      FPDF_DestroyLibrary();
    }
  });

  // register classes — constructor refs are per-env (not static globals)
  // because each worker thread has its own V8 isolate
  Napi::Function pageCtor = PDFiumPage::Init(env);
  addonData->pageConstructor = Napi::Persistent(pageCtor);
  addonData->pageConstructor.SuppressDestruct();

  Napi::Function docCtor = PDFiumDocument::Init(env);
  addonData->docConstructor = Napi::Persistent(docCtor);
  addonData->docConstructor.SuppressDestruct();

  exports.Set("PDFiumDocument", docCtor);
  exports.Set("PDFiumPage", pageCtor);
  exports.Set("loadDocument", Napi::Function::New(env, LoadDocument));
  exports.Set("splitDocument", Napi::Function::New(env, SplitDocument));
  exports.Set("mergeDocuments", Napi::Function::New(env, MergeDocuments));
  exports.Set("prepareShutdown", Napi::Function::New(env, PrepareShutdown));

  return exports;
}

NODE_API_MODULE(pdfium, Init)
