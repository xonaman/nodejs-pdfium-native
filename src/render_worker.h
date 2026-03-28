#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <memory>

#include "stb_image_write.h"

// stb write callback — appends bytes to a std::vector
inline void stb_write_callback(void *context, void *data, int size) {
  auto *out = static_cast<std::vector<uint8_t> *>(context);
  auto *bytes = static_cast<uint8_t *>(data);
  out->insert(out->end(), bytes, bytes + size);
}

// ---------------------------------------------------------------------------
// RenderWorker — async page rendering
// ---------------------------------------------------------------------------

class RenderWorker : public Napi::AsyncWorker {
public:
  RenderWorker(Napi::Env env, FPDF_PAGE page, int renderWidth, int renderHeight,
               int format, int quality, std::string outputPath, int rotation,
               bool transparent, int renderFlags,
               std::shared_ptr<std::atomic<bool>> pageAlive)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), renderWidth_(renderWidth), renderHeight_(renderHeight),
        format_(format), quality_(quality), outputPath_(std::move(outputPath)),
        rotation_(rotation), transparent_(transparent),
        renderFlags_(renderFlags), pageAlive_(std::move(pageAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);

    // check that the page hasn't been closed while we were queued
    if (!pageAlive_ || !pageAlive_->load()) {
      SetError("Page was closed before render completed");
      return;
    }

    FPDF_BITMAP bitmap =
        FPDFBitmap_Create(renderWidth_, renderHeight_, transparent_ ? 1 : 0);
    if (!bitmap) {
      SetError("Failed to create bitmap");
      return;
    }

    if (!transparent_) {
      FPDFBitmap_FillRect(bitmap, 0, 0, renderWidth_, renderHeight_,
                          0xFFFFFFFF);
    }
    FPDF_RenderPageBitmap(bitmap, page_, 0, 0, renderWidth_, renderHeight_,
                          rotation_, renderFlags_);

    int channels = transparent_ ? 4 : 3;

    // convert BGRA → RGB or BGRA → RGBA for stb
    void *bufferData = FPDFBitmap_GetBuffer(bitmap);
    int stride = FPDFBitmap_GetStride(bitmap);
    std::vector<uint8_t> pixels(static_cast<size_t>(renderWidth_) *
                                renderHeight_ * channels);

    for (int y = 0; y < renderHeight_; y++) {
      auto *row = static_cast<uint8_t *>(bufferData) + y * stride;
      for (int x = 0; x < renderWidth_; x++) {
        int srcIdx = x * 4;
        int dstIdx = (y * renderWidth_ + x) * channels;
        pixels[dstIdx + 0] = row[srcIdx + 2]; // R ← BGRA[2]
        pixels[dstIdx + 1] = row[srcIdx + 1]; // G ← BGRA[1]
        pixels[dstIdx + 2] = row[srcIdx + 0]; // B ← BGRA[0]
        if (transparent_)
          pixels[dstIdx + 3] = row[srcIdx + 3]; // A ← BGRA[3]
      }
    }

    FPDFBitmap_Destroy(bitmap);

    // encode to JPEG or PNG
    int ok;
    if (format_ == IMAGE_FORMAT_PNG) {
      ok = stbi_write_png_to_func(stb_write_callback, &encodedData_,
                                  renderWidth_, renderHeight_, channels,
                                  pixels.data(), renderWidth_ * channels);
    } else {
      ok = stbi_write_jpg_to_func(stb_write_callback, &encodedData_,
                                  renderWidth_, renderHeight_, channels,
                                  pixels.data(), quality_);
    }

    if (!ok) {
      SetError("Failed to encode image");
      return;
    }

    // write to file if output path was specified
    if (!outputPath_.empty()) {
      // verify parent directory exists
      std::filesystem::path outPath(outputPath_);
      auto parentDir = outPath.parent_path();
      if (!parentDir.empty() && !std::filesystem::is_directory(parentDir)) {
        SetError("Parent directory does not exist: " + parentDir.string());
        return;
      }

      FILE *f = fopen(outputPath_.c_str(), "wb");
      if (!f) {
        SetError("Failed to open output file: " + outputPath_);
        return;
      }
      size_t written = fwrite(encodedData_.data(), 1, encodedData_.size(), f);
      fclose(f);
      if (written != encodedData_.size()) {
        SetError("Failed to write output file: " + outputPath_);
        return;
      }
      encodedData_.clear();
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (outputPath_.empty()) {
      auto data = Napi::Buffer<uint8_t>::Copy(env, encodedData_.data(),
                                              encodedData_.size());
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
  int renderWidth_;
  int renderHeight_;
  int format_;
  int quality_;
  std::string outputPath_;
  int rotation_;
  bool transparent_;
  int renderFlags_;
  std::vector<uint8_t> encodedData_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
};
