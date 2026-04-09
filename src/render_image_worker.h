#pragma once

#include "napi_helpers.h"
#include "render_worker.h" // stb_write_callback

#include <atomic>
#include <cstdio>
#include <memory>
#include <vector>

#ifdef _WIN32
#include <io.h>
#define F_OK 0
#define access _access
#else
#include <unistd.h>
#endif

#include "fpdf_edit.h"

// max total pixels for image bitmap allocation (~256 MP, ~1 GB at 4 channels)
constexpr size_t MAX_IMAGE_PIXELS = 256 * 1024 * 1024;

// image render mode
enum RenderImageMode {
  RENDER_IMAGE_BITMAP = 0, // FPDFImageObj_GetBitmap (intrinsic pixels)
  RENDER_IMAGE_RENDERED =
      1,                // FPDFImageObj_GetRenderedBitmap (with mask/matrix)
  RENDER_IMAGE_RAW = 2, // FPDFImageObj_GetImageDataRaw (original stream)
};

// ---------------------------------------------------------------------------
// RenderImageWorker — async image rendering from a page object
// ---------------------------------------------------------------------------

class RenderImageWorker : public SafeAsyncWorker {
public:
  RenderImageWorker(Napi::Env env, FPDF_PAGE page, FPDF_DOCUMENT doc,
                    int objectIndex, int format, int quality,
                    std::string outputPath, int mode,
                    std::shared_ptr<std::atomic<bool>> pageAlive,
                    std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), doc_(doc), objectIndex_(objectIndex), format_(format),
        quality_(quality), outputPath_(std::move(outputPath)), mode_(mode),
        pageAlive_(std::move(pageAlive)), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    if (!pageAlive_->load() || (docAlive_ && !docAlive_->load())) {
      SetError("Page or document was closed");
      return;
    }

    if (objectIndex_ < 0) {
      SetError("Invalid object index");
      return;
    }

    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page_, objectIndex_);
    if (!obj) {
      SetError("Invalid object index");
      return;
    }
    if (FPDFPageObj_GetType(obj) != FPDF_PAGEOBJ_IMAGE) {
      SetError("Object at index " + std::to_string(objectIndex_) +
               " is not an image");
      return;
    }

    bool ok;
    if (mode_ == RENDER_IMAGE_RAW) {
      ok = GetRawData(obj);
    } else {
      ok = RenderBitmap(obj);
    }
    if (!ok)
      return;

    // write to file if output path was specified
    if (!outputPath_.empty()) {
      if (!WriteToFile())
        return;
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (outputPath_.empty()) {
      auto *vec = new std::vector<uint8_t>(std::move(encodedData_));
      auto data = Napi::Buffer<uint8_t>::New(
          env, vec->data(), vec->size(),
          [](Napi::Env, uint8_t *, std::vector<uint8_t> *v) { delete v; }, vec);
      deferred_.Resolve(data);
    } else {
      deferred_.Resolve(env.Undefined());
    }
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  FPDF_DOCUMENT doc_;
  int objectIndex_;
  int format_;
  int quality_;
  std::string outputPath_;
  int mode_;
  std::vector<uint8_t> encodedData_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;

  // get the raw encoded stream bytes (original JPEG, Flate, etc.)
  bool GetRawData(FPDF_PAGEOBJECT obj) {
    unsigned long len = FPDFImageObj_GetImageDataRaw(obj, nullptr, 0);
    if (len == 0) {
      SetError("Failed to get raw image data length");
      return false;
    }
    encodedData_.resize(len);
    unsigned long written =
        FPDFImageObj_GetImageDataRaw(obj, encodedData_.data(), len);
    if (written != len) {
      SetError("Failed to read raw image data");
      return false;
    }
    return true;
  }

  // render via bitmap (either intrinsic or rendered) and encode
  bool RenderBitmap(FPDF_PAGEOBJECT obj) {
    FPDF_BITMAP bitmap = nullptr;

    if (mode_ == RENDER_IMAGE_RENDERED) {
      bitmap = FPDFImageObj_GetRenderedBitmap(doc_, page_, obj);
    } else {
      bitmap = FPDFImageObj_GetBitmap(obj);
    }

    if (!bitmap) {
      SetError("Failed to get image bitmap");
      return false;
    }

    int width = FPDFBitmap_GetWidth(bitmap);
    int height = FPDFBitmap_GetHeight(bitmap);
    int stride = FPDFBitmap_GetStride(bitmap);
    void *bufferData = FPDFBitmap_GetBuffer(bitmap);
    int bitmapFormat = FPDFBitmap_GetFormat(bitmap);

    if (!bufferData || width <= 0 || height <= 0) {
      FPDFBitmap_Destroy(bitmap);
      SetError("Failed to get bitmap buffer");
      return false;
    }

    if (static_cast<size_t>(width) * static_cast<size_t>(height) >
        MAX_IMAGE_PIXELS) {
      FPDFBitmap_Destroy(bitmap);
      SetError("Image dimensions too large (" + std::to_string(width) + "x" +
               std::to_string(height) + ")");
      return false;
    }

    // determine source layout
    int srcBytesPerPixel;
    bool srcHasAlpha;
    switch (bitmapFormat) {
    case FPDFBitmap_Gray:
      srcBytesPerPixel = 1;
      srcHasAlpha = false;
      break;
    case FPDFBitmap_BGR:
      srcBytesPerPixel = 3;
      srcHasAlpha = false;
      break;
    case FPDFBitmap_BGRx:
      srcBytesPerPixel = 4;
      srcHasAlpha = false;
      break;
    case FPDFBitmap_BGRA:
      srcBytesPerPixel = 4;
      srcHasAlpha = true;
      break;
    default:
      srcBytesPerPixel = 4;
      srcHasAlpha = true;
      break;
    }

    // output channels: PNG can carry alpha, JPEG cannot
    bool keepAlpha = srcHasAlpha && format_ == IMAGE_FORMAT_PNG;
    int outChannels =
        (bitmapFormat == FPDFBitmap_Gray) ? 1 : (keepAlpha ? 4 : 3);

    std::vector<uint8_t> pixels(static_cast<size_t>(width) *
                                static_cast<size_t>(height) * outChannels);

    // hoisted conditionals for auto-vectorization
    if (bitmapFormat == FPDFBitmap_Gray) {
      for (int y = 0; y < height; y++) {
        auto *src = static_cast<uint8_t *>(bufferData) + y * stride;
        auto *dst = pixels.data() + y * width;
        for (int x = 0; x < width; x++)
          dst[x] = src[x];
      }
    } else if (keepAlpha) {
      for (int y = 0; y < height; y++) {
        auto *src = static_cast<uint8_t *>(bufferData) + y * stride;
        auto *dst = pixels.data() + y * width * 4;
        for (int x = 0; x < width; x++) {
          dst[0] = src[2]; // R ← B
          dst[1] = src[1]; // G
          dst[2] = src[0]; // B ← R
          dst[3] = src[3]; // A
          src += srcBytesPerPixel;
          dst += 4;
        }
      }
    } else {
      for (int y = 0; y < height; y++) {
        auto *src = static_cast<uint8_t *>(bufferData) + y * stride;
        auto *dst = pixels.data() + y * width * 3;
        for (int x = 0; x < width; x++) {
          dst[0] = src[2]; // R ← B
          dst[1] = src[1]; // G
          dst[2] = src[0]; // B ← R
          src += srcBytesPerPixel;
          dst += 3;
        }
      }
    }

    FPDFBitmap_Destroy(bitmap);

    // encode
    int ok;
    if (format_ == IMAGE_FORMAT_PNG) {
      ok = stbi_write_png_to_func(stb_write_callback, &encodedData_, width,
                                  height, outChannels, pixels.data(),
                                  width * outChannels);
    } else {
      ok = stbi_write_jpg_to_func(stb_write_callback, &encodedData_, width,
                                  height, outChannels, pixels.data(), quality_);
    }

    if (!ok) {
      SetError("Failed to encode image");
      return false;
    }
    return true;
  }

  bool WriteToFile() {
    // check parent directory exists
    auto slash = outputPath_.rfind('/');
    if (slash != std::string::npos && slash > 0) {
      std::string parentDir = outputPath_.substr(0, slash);
      if (access(parentDir.c_str(), F_OK) != 0) {
        SetError("Parent directory does not exist: " + parentDir);
        return false;
      }
    }

    FILE *f = fopen(outputPath_.c_str(), "wb");
    if (!f) {
      SetError("Failed to open output file: " + outputPath_);
      return false;
    }
    size_t written = fwrite(encodedData_.data(), 1, encodedData_.size(), f);
    fclose(f);
    if (written != encodedData_.size()) {
      SetError("Failed to write output file: " + outputPath_);
      return false;
    }
    encodedData_.clear();
    return true;
  }
};
