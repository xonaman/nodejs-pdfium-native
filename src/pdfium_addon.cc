#include "document.h"

Napi::FunctionReference PDFiumDocument::pageConstructor;

// ---------------------------------------------------------------------------
// Module-level: loadDocument (async)
// ---------------------------------------------------------------------------

static Napi::FunctionReference g_docConstructor;

static std::string GetPdfiumErrorMessage() {
  unsigned long err = FPDF_GetLastError();
  const char *code;
  const char *msg;
  switch (err) {
  case FPDF_ERR_FILE:
    code = "FILE";
    msg = "File not found or could not be opened";
    break;
  case FPDF_ERR_FORMAT:
    code = "FORMAT";
    msg = "Not a valid PDF or corrupted";
    break;
  case FPDF_ERR_PASSWORD:
    code = "PASSWORD";
    msg = "Password required or incorrect";
    break;
  case FPDF_ERR_SECURITY:
    code = "SECURITY";
    msg = "Unsupported security scheme";
    break;
  case FPDF_ERR_PAGE:
    code = "PAGE";
    msg = "Page error";
    break;
  default:
    code = "UNKNOWN";
    msg = "Unknown error";
    break;
  }
  return std::string(code) + ":" + msg;
}

// ---------------------------------------------------------------------------
// LoadDocumentWorker — async document loading
// ---------------------------------------------------------------------------

class LoadDocumentWorker : public Napi::AsyncWorker {
public:
  // buffer variant — copies data since buffer may be GC'd
  LoadDocumentWorker(Napi::Env env, std::vector<uint8_t> data,
                     std::string password)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        bufferData_(std::move(data)), password_(std::move(password)),
        useFile_(false) {}

  // file path variant
  LoadDocumentWorker(Napi::Env env, std::string path, std::string password)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
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
      unsigned long len = FPDF_GetMetaText(doc_, metaTags[i], nullptr, 0);
      if (len > 2) {
        std::vector<unsigned short> buf(len / sizeof(unsigned short));
        FPDF_GetMetaText(doc_, metaTags[i], buf.data(), len);
        size_t charCount = len / sizeof(unsigned short) - 1;
        meta_[i] = std::u16string(
            reinterpret_cast<const char16_t *>(buf.data()), charCount);
      }
    }
    FPDF_GetFileVersion(doc_, &pdfVersion_);
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object docObj = g_docConstructor.New({});
    PDFiumDocument *docWrapper = PDFiumDocument::Unwrap(docObj);
    docWrapper->SetDocument(doc_);

    docObj.Set("pageCount", Napi::Number::New(env, pageCount_));

    // set metadata as a plain JS property
    const char *metaKeys[] = {"title",   "author",   "subject",      "keywords",
                              "creator", "producer", "creationDate", "modDate"};
    Napi::Object metaObj = Napi::Object::New(env);
    for (int i = 0; i < 8; i++) {
      if (meta_[i].empty()) {
        metaObj.Set(metaKeys[i], Napi::String::New(env, ""));
      } else {
        metaObj.Set(metaKeys[i],
                    Napi::String::New(
                        env,
                        reinterpret_cast<const char16_t *>(meta_[i].data()),
                        meta_[i].size()));
      }
    }
    metaObj.Set("pdfVersion", Napi::Number::New(env, pdfVersion_));
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
// Module init
// ---------------------------------------------------------------------------

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  if (!g_initialized) {
    FPDF_InitLibrary();
    g_initialized = true;

    // clean up PDFium on environment teardown
    env.AddCleanupHook([]() {
      FPDF_DestroyLibrary();
      g_initialized = false;
    });
  }

  // register classes
  Napi::Function pageCtor = PDFiumPage::Init(env);
  PDFiumDocument::pageConstructor = Napi::Persistent(pageCtor);
  PDFiumDocument::pageConstructor.SuppressDestruct();

  Napi::Function docCtor = PDFiumDocument::Init(env);
  g_docConstructor = Napi::Persistent(docCtor);
  g_docConstructor.SuppressDestruct();

  exports.Set("PDFiumDocument", docCtor);
  exports.Set("PDFiumPage", pageCtor);
  exports.Set("loadDocument", Napi::Function::New(env, LoadDocument));

  return exports;
}

NODE_API_MODULE(pdfium, Init)
