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
  // form-specific: the objects the form draws, bounds in page coordinates
  std::vector<PageObjectData> objects;
  bool objectsTruncated = false;
};

// a form XObject can nest forms and hold any number of objects, so the walk is
// bounded like getStructTree(): a crafted file must not allocate without limit
// while it holds the global PDFium mutex
constexpr int MAX_FORM_DEPTH = 32;
constexpr int MAX_FORM_OBJECTS = 20000;

// the matrix that applies `inner`, then `outer` (PDF row-vector convention)
inline FS_MATRIX ConcatMatrix(const FS_MATRIX &inner, const FS_MATRIX &outer) {
  return {inner.a * outer.a + inner.b * outer.c,
          inner.a * outer.b + inner.b * outer.d,
          inner.c * outer.a + inner.d * outer.c,
          inner.c * outer.b + inner.d * outer.d,
          inner.e * outer.a + inner.f * outer.c + outer.e,
          inner.e * outer.b + inner.f * outer.d + outer.f};
}

class GetObjectWorker : public SafeAsyncWorker {
public:
  GetObjectWorker(Napi::Env env, FPDF_PAGE page, int index,
                  std::shared_ptr<std::atomic<bool>> pageAlive,
                  std::shared_ptr<std::atomic<bool>> docAlive,
                  Napi::Object pageObj)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), index_(index), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)),
        pageRef_(Napi::Persistent(pageObj)) {}

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

    budget_ = MAX_FORM_OBJECTS;
    ReadObject(obj, nullptr, 0, data_);
    if (textPage_)
      FPDFText_ClosePage(textPage_);
  }

  // reads one object; `toPage` maps the coordinate space of the form holding
  // it to page space, and is null for an object placed on the page itself
  void ReadObject(FPDF_PAGEOBJECT obj, const FS_MATRIX *toPage, int depth,
                  PageObjectData &data) {
    int type = FPDFPageObj_GetType(obj);
    switch (type) {
    case FPDF_PAGEOBJ_TEXT:
      data.type = "text";
      break;
    case FPDF_PAGEOBJ_PATH:
      data.type = "path";
      break;
    case FPDF_PAGEOBJ_IMAGE:
      data.type = "image";
      break;
    case FPDF_PAGEOBJ_SHADING:
      data.type = "shading";
      break;
    case FPDF_PAGEOBJ_FORM:
      data.type = "form";
      break;
    default:
      data.type = "unknown";
      break;
    }

    FPDFPageObj_GetBounds(obj, &data.left, &data.bottom, &data.right,
                          &data.top);
    if (toPage) {
      const float xs[2] = {data.left, data.right};
      const float ys[2] = {data.bottom, data.top};
      bool first = true;
      for (float x : xs) {
        for (float y : ys) {
          float px = toPage->a * x + toPage->c * y + toPage->e;
          float py = toPage->b * x + toPage->d * y + toPage->f;
          if (first || px < data.left)
            data.left = px;
          if (first || px > data.right)
            data.right = px;
          if (first || py < data.bottom)
            data.bottom = py;
          if (first || py > data.top)
            data.top = py;
          first = false;
        }
      }
    }

    unsigned int r, g, b, a;
    if (FPDFPageObj_GetFillColor(obj, &r, &g, &b, &a)) {
      data.hasFillColor = true;
      data.fillR = r;
      data.fillG = g;
      data.fillB = b;
      data.fillA = a;
    }
    if (FPDFPageObj_GetStrokeColor(obj, &r, &g, &b, &a)) {
      data.hasStrokeColor = true;
      data.strokeR = r;
      data.strokeG = g;
      data.strokeB = b;
      data.strokeA = a;
    }

    if (type == FPDF_PAGEOBJ_TEXT) {
      if (!textPage_)
        textPage_ = FPDFText_LoadPage(page_);
      FPDF_TEXTPAGE textPage = textPage_;
      if (textPage) {
        data.text = ReadU16(
            [&](auto *, unsigned long) {
              return FPDFTextObj_GetText(obj, textPage, nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFTextObj_GetText(obj, textPage, buf, len);
            });
      }

      float fs = 0;
      if (FPDFTextObj_GetFontSize(obj, &fs))
        data.fontSize = fs;

      FPDF_FONT font = FPDFTextObj_GetFont(obj);
      if (font) {
        size_t nameLen = FPDFFont_GetBaseFontName(font, nullptr, 0);
        if (nameLen > 1) {
          std::vector<char> nameBuf(nameLen);
          FPDFFont_GetBaseFontName(font, nameBuf.data(), nameLen);
          data.fontName = std::string(nameBuf.data(), nameLen - 1);
        }
        data.fontWeight = FPDFFont_GetWeight(font);
        int angle = 0;
        if (FPDFFont_GetItalicAngle(font, &angle)) {
          data.hasItalicAngle = true;
          data.italicAngle = angle;
        }

        // font family name
        size_t famLen = FPDFFont_GetFamilyName(font, nullptr, 0);
        if (famLen > 1) {
          std::vector<char> famBuf(famLen);
          FPDFFont_GetFamilyName(font, famBuf.data(), famLen);
          data.fontFamily = std::string(famBuf.data(), famLen - 1);
        }

        data.isEmbedded = FPDFFont_GetIsEmbedded(font);
        data.fontFlags = FPDFFont_GetFlags(font);
      }

      // text render mode
      FPDF_TEXT_RENDERMODE rm = FPDFTextObj_GetTextRenderMode(obj);
      switch (rm) {
      case FPDF_TEXTRENDERMODE_FILL:
        data.renderMode = "fill";
        break;
      case FPDF_TEXTRENDERMODE_STROKE:
        data.renderMode = "stroke";
        break;
      case FPDF_TEXTRENDERMODE_FILL_STROKE:
        data.renderMode = "fillStroke";
        break;
      case FPDF_TEXTRENDERMODE_INVISIBLE:
        data.renderMode = "invisible";
        break;
      case FPDF_TEXTRENDERMODE_FILL_CLIP:
        data.renderMode = "fillClip";
        break;
      case FPDF_TEXTRENDERMODE_STROKE_CLIP:
        data.renderMode = "strokeClip";
        break;
      case FPDF_TEXTRENDERMODE_FILL_STROKE_CLIP:
        data.renderMode = "fillStrokeClip";
        break;
      case FPDF_TEXTRENDERMODE_CLIP:
        data.renderMode = "clip";
        break;
      default:
        data.renderMode = "unknown";
        break;
      }
    }

    if (type == FPDF_PAGEOBJ_IMAGE) {
      FPDFImageObj_GetImagePixelSize(obj, &data.imageWidth, &data.imageHeight);

      // rich image metadata
      FPDF_IMAGEOBJ_METADATA meta;
      if (FPDFImageObj_GetImageMetadata(obj, page_, &meta)) {
        data.hasImageMetadata = true;
        data.horizontalDpi = meta.horizontal_dpi;
        data.verticalDpi = meta.vertical_dpi;
        data.bitsPerPixel = meta.bits_per_pixel;
        switch (meta.colorspace) {
        case FPDF_COLORSPACE_DEVICEGRAY:
          data.colorspace = "deviceGray";
          break;
        case FPDF_COLORSPACE_DEVICERGB:
          data.colorspace = "deviceRGB";
          break;
        case FPDF_COLORSPACE_DEVICECMYK:
          data.colorspace = "deviceCMYK";
          break;
        case FPDF_COLORSPACE_CALGRAY:
          data.colorspace = "calGray";
          break;
        case FPDF_COLORSPACE_CALRGB:
          data.colorspace = "calRGB";
          break;
        case FPDF_COLORSPACE_LAB:
          data.colorspace = "lab";
          break;
        case FPDF_COLORSPACE_ICCBASED:
          data.colorspace = "iccBased";
          break;
        case FPDF_COLORSPACE_SEPARATION:
          data.colorspace = "separation";
          break;
        case FPDF_COLORSPACE_DEVICEN:
          data.colorspace = "deviceN";
          break;
        case FPDF_COLORSPACE_INDEXED:
          data.colorspace = "indexed";
          break;
        case FPDF_COLORSPACE_PATTERN:
          data.colorspace = "pattern";
          break;
        default:
          data.colorspace = "unknown";
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
          data.filters.emplace_back(fBuf.data(), fLen - 1);
        }
      }
    }

    if (type == FPDF_PAGEOBJ_FORM) {
      FS_MATRIX formMatrix;
      if (!FPDFPageObj_GetMatrix(obj, &formMatrix))
        return;
      FS_MATRIX childToPage =
          toPage ? ConcatMatrix(formMatrix, *toPage) : formMatrix;
      int count = FPDFFormObj_CountObjects(obj);
      for (int i = 0; i < count; i++) {
        if (depth >= MAX_FORM_DEPTH || budget_ <= 0) {
          data.objectsTruncated = true;
          break;
        }
        FPDF_PAGEOBJECT child = FPDFFormObj_GetObject(obj, i);
        if (!child)
          continue;
        budget_--;
        data.objects.emplace_back();
        ReadObject(child, &childToPage, depth + 1, data.objects.back());
      }
    }
  }

  void OnOK() override { deferred_.Resolve(ToJs(Env(), data_, true)); }

  // `topLevel`: an image placed on the page itself, which page.renderImage()
  // can address by its index; an image inside a form has no such index
  Napi::Object ToJs(Napi::Env env, const PageObjectData &data, bool topLevel) {
    Napi::Object result = Napi::Object::New(env);
    result.Set("type", Napi::String::New(env, data.type));
    result.Set("bounds", CreateBoundsObject(env, data.left, data.bottom,
                                            data.right, data.top));

    if (data.hasFillColor) {
      result.Set("fillColor", CreateColorObject(env, data.fillR, data.fillG,
                                                data.fillB, data.fillA));
    } else {
      result.Set("fillColor", env.Null());
    }

    if (data.hasStrokeColor) {
      result.Set("strokeColor",
                 CreateColorObject(env, data.strokeR, data.strokeG,
                                   data.strokeB, data.strokeA));
    } else {
      result.Set("strokeColor", env.Null());
    }

    if (data.type == "text") {
      SetU16(result, "text", env, data.text);
      result.Set("fontSize", Napi::Number::New(env, data.fontSize));
      result.Set("fontName", Napi::String::New(env, data.fontName));
      if (data.fontWeight >= 0)
        result.Set("fontWeight", Napi::Number::New(env, data.fontWeight));
      if (data.hasItalicAngle)
        result.Set("italicAngle", Napi::Number::New(env, data.italicAngle));
      if (!data.renderMode.empty())
        result.Set("renderMode", Napi::String::New(env, data.renderMode));
      if (!data.fontFamily.empty())
        result.Set("fontFamily", Napi::String::New(env, data.fontFamily));
      if (data.isEmbedded >= 0)
        result.Set("isEmbedded", Napi::Boolean::New(env, data.isEmbedded != 0));
      if (data.fontFlags >= 0)
        result.Set("fontFlags", Napi::Number::New(env, data.fontFlags));
    }

    if (data.type == "image") {
      result.Set("imageWidth", Napi::Number::New(env, data.imageWidth));
      result.Set("imageHeight", Napi::Number::New(env, data.imageHeight));
      if (data.hasImageMetadata) {
        result.Set("horizontalDpi", Napi::Number::New(env, data.horizontalDpi));
        result.Set("verticalDpi", Napi::Number::New(env, data.verticalDpi));
        result.Set("bitsPerPixel", Napi::Number::New(env, data.bitsPerPixel));
        if (!data.colorspace.empty())
          result.Set("colorspace", Napi::String::New(env, data.colorspace));
      }
      if (!data.filters.empty()) {
        Napi::Array fArr = Napi::Array::New(env, data.filters.size());
        for (uint32_t fi = 0; fi < data.filters.size(); fi++)
          fArr.Set(fi, Napi::String::New(env, data.filters[fi]));
        result.Set("filters", fArr);
      }

      // bind render: (opts?) => page.renderImage(index, opts)
      if (topLevel) {
        Napi::Object pageObj = pageRef_.Value();
        Napi::Value renderImageVal = pageObj.Get("renderImage");
        if (renderImageVal.IsFunction()) {
          Napi::Function renderImage = renderImageVal.As<Napi::Function>();
          Napi::Function bind = renderImage.Get("bind").As<Napi::Function>();
          result.Set("render",
                     bind.Call(renderImage,
                               {pageObj, Napi::Number::New(env, index_)}));
        }
      }
    }

    if (data.type == "form") {
      Napi::Array objects = Napi::Array::New(env, data.objects.size());
      for (uint32_t i = 0; i < data.objects.size(); i++)
        objects.Set(i, ToJs(env, data.objects[i], false));
      result.Set("objects", objects);
      if (data.objectsTruncated)
        result.Set("objectsTruncated", Napi::Boolean::New(env, true));
    }

    return result;
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
  FPDF_TEXTPAGE textPage_ = nullptr;
  int budget_ = 0;
};
