#pragma once

#include "attachments_worker.h" // shared FPDF_ATTACHMENT read helpers
#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_annot.h"

// ---------------------------------------------------------------------------
// GetAnnotationsWorker — async annotation extraction
// ---------------------------------------------------------------------------

struct AnnotationData {
  int index = 0;
  std::string type;
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  std::u16string fileName; // set only for 'fileattachment' annotations
  std::u16string contents;
  unsigned int r = 0, g = 0, b = 0, a = 0;
  bool hasColor = false;
  unsigned int ir = 0, ig = 0, ib = 0, ia = 0;
  bool hasInteriorColor = false;
  float borderHRadius = 0, borderVRadius = 0, borderWidth = 0;
  bool hasBorder = false;
  std::u16string author;
  std::u16string subject;
  std::u16string creationDate;
  std::u16string modDate;
  int flags = 0;
  struct QuadPoints {
    float x1, y1, x2, y2, x3, y3, x4, y4;
  };
  std::vector<QuadPoints> quadPoints;
};

class GetAnnotationsWorker : public SafeAsyncWorker {
public:
  GetAnnotationsWorker(Napi::Env env, FPDF_PAGE page,
                       std::shared_ptr<std::atomic<bool>> pageAlive,
                       std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    int count = FPDFPage_GetAnnotCount(page_);
    for (int i = 0; i < count; i++) {
      FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page_, i);
      if (!annot)
        continue;

      AnnotationData data;
      data.index = i;

      FPDF_ANNOTATION_SUBTYPE subtype = FPDFAnnot_GetSubtype(annot);
      switch (subtype) {
      case FPDF_ANNOT_TEXT:
        data.type = "text";
        break;
      case FPDF_ANNOT_LINK:
        data.type = "link";
        break;
      case FPDF_ANNOT_FREETEXT:
        data.type = "freetext";
        break;
      case FPDF_ANNOT_LINE:
        data.type = "line";
        break;
      case FPDF_ANNOT_SQUARE:
        data.type = "square";
        break;
      case FPDF_ANNOT_CIRCLE:
        data.type = "circle";
        break;
      case FPDF_ANNOT_POLYGON:
        data.type = "polygon";
        break;
      case FPDF_ANNOT_POLYLINE:
        data.type = "polyline";
        break;
      case FPDF_ANNOT_HIGHLIGHT:
        data.type = "highlight";
        break;
      case FPDF_ANNOT_UNDERLINE:
        data.type = "underline";
        break;
      case FPDF_ANNOT_SQUIGGLY:
        data.type = "squiggly";
        break;
      case FPDF_ANNOT_STRIKEOUT:
        data.type = "strikeout";
        break;
      case FPDF_ANNOT_STAMP:
        data.type = "stamp";
        break;
      case FPDF_ANNOT_CARET:
        data.type = "caret";
        break;
      case FPDF_ANNOT_INK:
        data.type = "ink";
        break;
      case FPDF_ANNOT_POPUP:
        data.type = "popup";
        break;
      case FPDF_ANNOT_FILEATTACHMENT:
        data.type = "fileattachment";
        break;
      case FPDF_ANNOT_SOUND:
        data.type = "sound";
        break;
      case FPDF_ANNOT_WIDGET:
        data.type = "widget";
        break;
      case FPDF_ANNOT_REDACT:
        data.type = "redact";
        break;
      case FPDF_ANNOT_WATERMARK:
        data.type = "watermark";
        break;
      default:
        data.type = "unknown";
        break;
      }

      // for file-attachment annotations, surface the embedded file name so
      // callers can decide which annotation to read via getAnnotationAttachment
      if (subtype == FPDF_ANNOT_FILEATTACHMENT) {
        FPDF_ATTACHMENT att = FPDFAnnot_GetFileAttachment(annot);
        if (att)
          data.fileName = ReadAttachmentName(att);
      }

      FS_RECTF rect;
      if (FPDFAnnot_GetRect(annot, &rect)) {
        data.hasBounds = true;
        data.left = rect.left;
        data.bottom = rect.bottom;
        data.right = rect.right;
        data.top = rect.top;
      }

      // helper to read annotation string values via ReadU16
      auto readAnnotStr = [&](const char *key) {
        return ReadU16(
            [&](auto *, unsigned long) {
              return FPDFAnnot_GetStringValue(annot, key, nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFAnnot_GetStringValue(annot, key, buf, len);
            });
      };

      data.contents = readAnnotStr("Contents");

      unsigned int cr, cg, cb, ca;
      if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_Color, &cr, &cg, &cb,
                             &ca)) {
        data.hasColor = true;
        data.r = cr;
        data.g = cg;
        data.b = cb;
        data.a = ca;
      }

      // interior color (fill color for markup annotations)
      unsigned int icr, icg, icb, ica;
      if (FPDFAnnot_GetColor(annot, FPDFANNOT_COLORTYPE_InteriorColor, &icr,
                             &icg, &icb, &ica)) {
        data.hasInteriorColor = true;
        data.ir = icr;
        data.ig = icg;
        data.ib = icb;
        data.ia = ica;
      }

      // border style
      float hRadius, vRadius, bWidth;
      if (FPDFAnnot_GetBorder(annot, &hRadius, &vRadius, &bWidth)) {
        data.hasBorder = true;
        data.borderHRadius = hRadius;
        data.borderVRadius = vRadius;
        data.borderWidth = bWidth;
      }

      // quad points (for highlight, underline, squiggly, strikeout, link,
      // redact)
      if (FPDFAnnot_HasAttachmentPoints(annot)) {
        size_t qpCount = FPDFAnnot_CountAttachmentPoints(annot);
        for (size_t qi = 0; qi < qpCount; qi++) {
          FS_QUADPOINTSF qp;
          if (FPDFAnnot_GetAttachmentPoints(annot, qi, &qp)) {
            data.quadPoints.push_back(
                {qp.x1, qp.y1, qp.x2, qp.y2, qp.x3, qp.y3, qp.x4, qp.y4});
          }
        }
      }

      data.author = readAnnotStr("T");
      data.subject = readAnnotStr("Subj");
      data.creationDate = readAnnotStr("CreationDate");
      data.modDate = readAnnotStr("M");

      // annotation flags
      data.flags = FPDFAnnot_GetFlags(annot);

      FPDFPage_CloseAnnot(annot);
      annotations_.push_back(std::move(data));
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, annotations_.size());
    for (uint32_t i = 0; i < annotations_.size(); i++) {
      auto &d = annotations_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("index", Napi::Number::New(env, d.index));
      obj.Set("type", Napi::String::New(env, d.type));
      SetU16IfPresent(obj, "fileName", env, d.fileName);
      if (d.hasBounds)
        obj.Set("bounds",
                CreateBoundsObject(env, d.left, d.bottom, d.right, d.top));
      SetU16(obj, "contents", env, d.contents);
      if (d.hasColor) {
        obj.Set("color", CreateColorObject(env, d.r, d.g, d.b, d.a));
      } else {
        obj.Set("color", env.Null());
      }
      if (d.hasInteriorColor) {
        obj.Set("interiorColor",
                CreateColorObject(env, d.ir, d.ig, d.ib, d.ia));
      }
      SetU16(obj, "author", env, d.author);
      SetU16(obj, "subject", env, d.subject);
      SetU16(obj, "creationDate", env, d.creationDate);
      SetU16(obj, "modDate", env, d.modDate);
      obj.Set("flags", Napi::Number::New(env, d.flags));
      if (d.hasBorder) {
        Napi::Object border = Napi::Object::New(env);
        border.Set("horizontalRadius", Napi::Number::New(env, d.borderHRadius));
        border.Set("verticalRadius", Napi::Number::New(env, d.borderVRadius));
        border.Set("width", Napi::Number::New(env, d.borderWidth));
        obj.Set("border", border);
      }
      if (!d.quadPoints.empty()) {
        Napi::Array qpArr = Napi::Array::New(env, d.quadPoints.size());
        for (uint32_t qi = 0; qi < d.quadPoints.size(); qi++) {
          auto &qp = d.quadPoints[qi];
          Napi::Object qpObj = Napi::Object::New(env);
          qpObj.Set("x1", Napi::Number::New(env, qp.x1));
          qpObj.Set("y1", Napi::Number::New(env, qp.y1));
          qpObj.Set("x2", Napi::Number::New(env, qp.x2));
          qpObj.Set("y2", Napi::Number::New(env, qp.y2));
          qpObj.Set("x3", Napi::Number::New(env, qp.x3));
          qpObj.Set("y3", Napi::Number::New(env, qp.y3));
          qpObj.Set("x4", Napi::Number::New(env, qp.x4));
          qpObj.Set("y4", Napi::Number::New(env, qp.y4));
          qpArr.Set(qi, qpObj);
        }
        obj.Set("quadPoints", qpArr);
      }
      arr.Set(i, obj);
    }
    deferred_.Resolve(arr);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<AnnotationData> annotations_;
};

// ---------------------------------------------------------------------------
// GetAnnotationAttachmentWorker — read a FileAttachment annotation's bytes
// ---------------------------------------------------------------------------
// Complements the document-level embedded-files API (GetAttachmentDataWorker).
// PDFium keeps "paperclip" file attachments as page-level /FileAttachment
// annotations whose file lives on the annotation, not in the /EmbeddedFiles
// name tree. `index` is the annotation index (matching Annotation.index).

class GetAnnotationAttachmentWorker : public SafeAsyncWorker {
public:
  GetAnnotationAttachmentWorker(Napi::Env env, FPDF_PAGE page, int index,
                                std::string outputPath,
                                std::shared_ptr<std::atomic<bool>> pageAlive,
                                std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), index_(index), outputPath_(std::move(outputPath)),
        pageAlive_(std::move(pageAlive)), docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    int count = FPDFPage_GetAnnotCount(page_);
    if (index_ < 0 || index_ >= count) {
      SetError("Annotation index out of range");
      return;
    }

    FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page_, index_);
    if (!annot) {
      SetError("Failed to get annotation");
      return;
    }
    // close the annotation on every exit path (the attachment handle is only
    // read while the annotation is open, before this guard runs)
    struct AnnotGuard {
      FPDF_ANNOTATION a;
      ~AnnotGuard() { FPDFPage_CloseAnnot(a); }
    } guard{annot};

    if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_FILEATTACHMENT) {
      SetError("Annotation at index " + std::to_string(index_) +
               " is not a file attachment");
      return;
    }

    FPDF_ATTACHMENT attachment = FPDFAnnot_GetFileAttachment(annot);
    if (!attachment) {
      SetError("File-attachment annotation has no embedded file");
      return;
    }

    std::string err;
    if (!ReadAttachmentFileBytes(attachment, data_, err)) {
      SetError(err);
      return;
    }

    if (!outputPath_.empty()) {
      if (!WriteBytesToFile(outputPath_, data_, err)) {
        SetError(err);
        return;
      }
      data_.clear();
    }
  }

  void OnOK() override {
    Napi::Env env = Env();
    if (!outputPath_.empty()) {
      deferred_.Resolve(env.Undefined());
      return;
    }
    auto *vec = new std::vector<uint8_t>(std::move(data_));
    auto buffer = Napi::Buffer<uint8_t>::New(
        env, vec->data(), vec->size(),
        [](Napi::Env, uint8_t *, std::vector<uint8_t> *v) { delete v; }, vec);
    deferred_.Resolve(buffer);
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  int index_;
  std::string outputPath_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<uint8_t> data_;
};
