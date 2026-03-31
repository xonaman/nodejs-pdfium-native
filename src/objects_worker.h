#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_edit.h"
#include "fpdf_text.h"

// ---------------------------------------------------------------------------
// GetObjectWorker — async page object extraction
// ---------------------------------------------------------------------------

struct PageObjectData {
  std::string type;
  float left = 0, bottom = 0, right = 0, top = 0;
  unsigned int fillR = 0, fillG = 0, fillB = 0, fillA = 0;
  bool hasFillColor = false;
  unsigned int strokeR = 0, strokeG = 0, strokeB = 0, strokeA = 0;
  bool hasStrokeColor = false;
  // text-specific
  std::u16string text;
  float fontSize = 0;
  std::string fontName;
  int fontWeight = -1;
  int italicAngle = 0;
  bool hasItalicAngle = false;
  std::string renderMode;
  std::string fontFamily;
  int isEmbedded = -1; // -1 = unknown, 0 = no, 1 = yes
  int fontFlags = -1;
  // image-specific
  unsigned int imageWidth = 0, imageHeight = 0;
  float horizontalDpi = 0, verticalDpi = 0;
  unsigned int bitsPerPixel = 0;
  std::string colorspace;
  std::vector<std::string> filters;
  bool hasImageMetadata = false;
};

class GetObjectWorker : public Napi::AsyncWorker {
public:
  GetObjectWorker(Napi::Env env, FPDF_PAGE page, int index,
                  std::shared_ptr<std::atomic<bool>> pageAlive,
                  std::shared_ptr<std::atomic<bool>> docAlive,
                  Napi::Object pageObj)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), index_(index), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)), pageRef_(Napi::Persistent(pageObj)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    FPDF_PAGEOBJECT obj = FPDFPage_GetObject(page_, index_);
    if (!obj) {
      SetError("Object not found");
      return;
    }

    int type = FPDFPageObj_GetType(obj);
    switch (type) {
    case FPDF_PAGEOBJ_TEXT:
      data_.type = "text";
      break;
    case FPDF_PAGEOBJ_PATH:
      data_.type = "path";
      break;
    case FPDF_PAGEOBJ_IMAGE:
      data_.type = "image";
      break;
    case FPDF_PAGEOBJ_SHADING:
      data_.type = "shading";
      break;
    case FPDF_PAGEOBJ_FORM:
      data_.type = "form";
      break;
    default:
      data_.type = "unknown";
      break;
    }

    FPDFPageObj_GetBounds(obj, &data_.left, &data_.bottom, &data_.right,
                          &data_.top);

    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a)) {
      data_.hasFillColor = true;
      data_.fillR = r;
      data_.fillG = g;
      data_.fillB = b;
      data_.fillA = a;
    }
    if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a)) {
      data_.hasStrokeColor = true;
      data_.strokeR = r;
      data_.strokeG = g;
      data_.strokeB = b;
      data_.strokeA = a;
    }

    if (type == FPDF_PAGEOBJ_TEXT) {
      FPDF_TEXTPAGE textPage = FPDFText_LoadPage(page_);
      if (textPage) {
        data_.text = ReadU16(
            [&](auto *, unsigned long) {
              return FPDFTextObj_GetText(obj, textPage, nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFTextObj_GetText(obj, textPage, buf, len);
            });
        FPDFText_ClosePage(textPage);
      }

      float fs = 0;
      if (FPDFTextObj_GetFontSize(obj, &fs))
        data_.fontSize = fs;

      FPDF_FONT font = FPDFTextObj_GetFont(obj);
      if (font) {
        size_t nameLen = FPDFFont_GetBaseFontName(font, nullptr, 0);
        if (nameLen > 1) {
          std::vector<char> nameBuf(nameLen);
          FPDFFont_GetBaseFontName(font, nameBuf.data(), nameLen);
          data_.fontName = std::string(nameBuf.data(), nameLen - 1);
        }
        data_.fontWeight = FPDFFont_GetWeight(font);
        int angle = 0;
        if (FPDFFont_GetItalicAngle(font, &angle)) {
          data_.hasItalicAngle = true;
          data_.italicAngle = angle;
        }

        // font family name
        size_t famLen = FPDFFont_GetFamilyName(font, nullptr, 0);
        if (famLen > 1) {
          std::vector<char> famBuf(famLen);
          FPDFFont_GetFamilyName(font, famBuf.data(), famLen);
          data_.fontFamily = std::string(famBuf.data(), famLen - 1);
        }

        data_.isEmbedded = FPDFFont_GetIsEmbedded(font);
        data_.fontFlags = FPDFFont_GetFlags(font);
      }

      // text render mode
      FPDF_TEXT_RENDERMODE rm = FPDFTextObj_GetTextRenderMode(obj);
      switch (rm) {
      case FPDF_TEXTRENDERMODE_FILL:
        data_.renderMode = "fill";
        break;
      case FPDF_TEXTRENDERMODE_STROKE:
        data_.renderMode = "stroke";
        break;
      case FPDF_TEXTRENDERMODE_FILL_STROKE:
        data_.renderMode = "fillStroke";
        break;
      case FPDF_TEXTRENDERMODE_INVISIBLE:
        data_.renderMode = "invisible";
        break;
      case FPDF_TEXTRENDERMODE_FILL_CLIP:
        data_.renderMode = "fillClip";
        break;
      case FPDF_TEXTRENDERMODE_STROKE_CLIP:
        data_.renderMode = "strokeClip";
        break;
      case FPDF_TEXTRENDERMODE_FILL_STROKE_CLIP:
        data_.renderMode = "fillStrokeClip";
        break;
      case FPDF_TEXTRENDERMODE_CLIP:
        data_.renderMode = "clip";
        break;
      default:
        data_.renderMode = "unknown";
        break;
      }
    }

    if (type == FPDF_PAGEOBJ_IMAGE) {
      FPDFImageObj_GetImagePixelSize(obj, &data_.imageWidth,
                                     &data_.imageHeight);

      // rich image metadata
      FPDF_IMAGEOBJ_METADATA meta;
      if (FPDFImageObj_GetImageMetadata(obj, page_, &meta)) {
        data_.hasImageMetadata = true;
        data_.horizontalDpi = meta.horizontal_dpi;
        data_.verticalDpi = meta.vertical_dpi;
        data_.bitsPerPixel = meta.bits_per_pixel;
        switch (meta.colorspace) {
        case FPDF_COLORSPACE_DEVICEGRAY:
          data_.colorspace = "deviceGray";
          break;
        case FPDF_COLORSPACE_DEVICERGB:
          data_.colorspace = "deviceRGB";
          break;
        case FPDF_COLORSPACE_DEVICECMYK:
          data_.colorspace = "deviceCMYK";
          break;
        case FPDF_COLORSPACE_CALGRAY:
          data_.colorspace = "calGray";
          break;
        case FPDF_COLORSPACE_CALRGB:
          data_.colorspace = "calRGB";
          break;
        case FPDF_COLORSPACE_LAB:
          data_.colorspace = "lab";
          break;
        case FPDF_COLORSPACE_ICCBASED:
          data_.colorspace = "iccBased";
          break;
        case FPDF_COLORSPACE_SEPARATION:
          data_.colorspace = "separation";
          break;
        case FPDF_COLORSPACE_DEVICEN:
          data_.colorspace = "deviceN";
          break;
        case FPDF_COLORSPACE_INDEXED:
          data_.colorspace = "indexed";
          break;
        case FPDF_COLORSPACE_PATTERN:
          data_.colorspace = "pattern";
          break;
        default:
          data_.colorspace = "unknown";
          break;
        }
      }

      // compression filters
      int filterCount = FPDFImageObj_GetImageFilterCount(obj);
      for (int fi = 0; fi < filterCount; fi++) {
        unsigned long fLen = FPDFImageObj_GetImageFilter(obj, fi, nullptr, 0);
        if (fLen > 1) {
          std::vector<char> fBuf(fLen);
          FPDFImageObj_GetImageFilter(obj, fi, fBuf.data(), fLen);
          data_.filters.emplace_back(fBuf.data(), fLen - 1);
        }
      }
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Object result = Napi::Object::New(env);
    result.Set("type", Napi::String::New(env, data_.type));
    result.Set("bounds", CreateBoundsObject(env, data_.left, data_.bottom,
                                            data_.right, data_.top));

    if (data_.hasFillColor) {
      result.Set("fillColor", CreateColorObject(env, data_.fillR, data_.fillG,
                                                data_.fillB, data_.fillA));
    } else {
      result.Set("fillColor", env.Null());
    }

    if (data_.hasStrokeColor) {
      result.Set("strokeColor",
                 CreateColorObject(env, data_.strokeR, data_.strokeG,
                                   data_.strokeB, data_.strokeA));
    } else {
      result.Set("strokeColor", env.Null());
    }

    if (data_.type == "text") {
      SetU16(result, "text", env, data_.text);
      result.Set("fontSize", Napi::Number::New(env, data_.fontSize));
      result.Set("fontName", Napi::String::New(env, data_.fontName));
      if (data_.fontWeight >= 0)
        result.Set("fontWeight", Napi::Number::New(env, data_.fontWeight));
      if (data_.hasItalicAngle)
        result.Set("italicAngle", Napi::Number::New(env, data_.italicAngle));
      if (!data_.renderMode.empty())
        result.Set("renderMode", Napi::String::New(env, data_.renderMode));
      if (!data_.fontFamily.empty())
        result.Set("fontFamily", Napi::String::New(env, data_.fontFamily));
      if (data_.isEmbedded >= 0)
        result.Set("isEmbedded",
                   Napi::Boolean::New(env, data_.isEmbedded != 0));
      if (data_.fontFlags >= 0)
        result.Set("fontFlags", Napi::Number::New(env, data_.fontFlags));
    }

    if (data_.type == "image") {
      result.Set("imageWidth", Napi::Number::New(env, data_.imageWidth));
      result.Set("imageHeight", Napi::Number::New(env, data_.imageHeight));
      if (data_.hasImageMetadata) {
        result.Set("horizontalDpi",
                   Napi::Number::New(env, data_.horizontalDpi));
        result.Set("verticalDpi", Napi::Number::New(env, data_.verticalDpi));
        result.Set("bitsPerPixel", Napi::Number::New(env, data_.bitsPerPixel));
        if (!data_.colorspace.empty())
          result.Set("colorspace", Napi::String::New(env, data_.colorspace));
      }
      if (!data_.filters.empty()) {
        Napi::Array fArr = Napi::Array::New(env, data_.filters.size());
        for (uint32_t fi = 0; fi < data_.filters.size(); fi++)
          fArr.Set(fi, Napi::String::New(env, data_.filters[fi]));
        result.Set("filters", fArr);
      }

      // bind render: (opts?) => page.renderImage(index, opts)
      Napi::Object pageObj = pageRef_.Value();
      Napi::Value renderImageVal = pageObj.Get("renderImage");
      if (renderImageVal.IsFunction()) {
        Napi::Function renderImage = renderImageVal.As<Napi::Function>();
        Napi::Function bind = renderImage.Get("bind").As<Napi::Function>();
        result.Set(
            "render",
            bind.Call(renderImage, {pageObj, Napi::Number::New(env, index_)}));
      }
    }

    deferred_.Resolve(result);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  int index_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  Napi::ObjectReference pageRef_;
  PageObjectData data_;
};
