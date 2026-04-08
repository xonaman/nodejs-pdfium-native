#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_annot.h"
#include "fpdf_formfill.h"

// ---------------------------------------------------------------------------
// GetFormFieldsWorker — async form field extraction
// ---------------------------------------------------------------------------

struct FormFieldOptionData {
  std::u16string label;
  bool isSelected = false;
};

struct FormFieldData {
  std::string type;
  std::u16string name;
  std::u16string value;
  std::u16string alternateName;
  std::u16string exportValue;
  int flags = 0;
  double left = 0, bottom = 0, right = 0, top = 0;
  bool hasBounds = false;
  bool isChecked = false;
  std::vector<FormFieldOptionData> options;
};

class GetFormFieldsWorker : public SafeAsyncWorker {
public:
  GetFormFieldsWorker(Napi::Env env, FPDF_PAGE page, FPDF_DOCUMENT doc,
                      std::shared_ptr<std::atomic<bool>> pageAlive,
                      std::shared_ptr<std::atomic<bool>> docAlive)
      : SafeAsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)),
        page_(page), doc_(doc), pageAlive_(std::move(pageAlive)),
        docAlive_(std::move(docAlive)) {}

  Napi::Promise Promise() { return deferred_.Promise(); }

protected:
  void Execute() override {
    std::lock_guard<std::mutex> lock(g_pdfium_mutex);
    CHECK_ALIVE();

    // create a temporary form fill environment for read-only field access
    FPDF_FORMFILLINFO formFillInfo{};
    formFillInfo.version = 1;
    FPDF_FORMHANDLE formHandle =
        FPDFDOC_InitFormFillEnvironment(doc_, &formFillInfo);
    if (!formHandle) {
      SetError("Failed to initialize form fill environment");
      return;
    }

    int count = FPDFPage_GetAnnotCount(page_);
    for (int i = 0; i < count; i++) {
      FPDF_ANNOTATION annot = FPDFPage_GetAnnot(page_, i);
      if (!annot)
        continue;

      // only process widget annotations (form fields)
      if (FPDFAnnot_GetSubtype(annot) != FPDF_ANNOT_WIDGET) {
        FPDFPage_CloseAnnot(annot);
        continue;
      }

      FormFieldData data;

      // field type
      int fieldType = FPDFAnnot_GetFormFieldType(formHandle, annot);
      switch (fieldType) {
      case FPDF_FORMFIELD_PUSHBUTTON:
        data.type = "pushButton";
        break;
      case FPDF_FORMFIELD_CHECKBOX:
        data.type = "checkbox";
        break;
      case FPDF_FORMFIELD_RADIOBUTTON:
        data.type = "radioButton";
        break;
      case FPDF_FORMFIELD_COMBOBOX:
        data.type = "comboBox";
        break;
      case FPDF_FORMFIELD_LISTBOX:
        data.type = "listBox";
        break;
      case FPDF_FORMFIELD_TEXTFIELD:
        data.type = "textField";
        break;
      case FPDF_FORMFIELD_SIGNATURE:
        data.type = "signature";
        break;
      default:
        data.type = "unknown";
        break;
      }

      // field name (UTF-16LE)
      data.name = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldName(formHandle, annot, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldName(formHandle, annot, buf, len);
          });

      // field value (UTF-16LE)
      data.value = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldValue(formHandle, annot, nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldValue(formHandle, annot, buf, len);
          });

      // alternate name (tooltip)
      data.alternateName = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldAlternateName(formHandle, annot,
                                                       nullptr, 0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldAlternateName(formHandle, annot, buf,
                                                       len);
          });

      // export value (for checkboxes and radio buttons)
      data.exportValue = ReadU16(
          [&](auto *, unsigned long) {
            return FPDFAnnot_GetFormFieldExportValue(formHandle, annot, nullptr,
                                                     0);
          },
          [&](FPDF_WCHAR *buf, unsigned long len) {
            return FPDFAnnot_GetFormFieldExportValue(formHandle, annot, buf,
                                                     len);
          });

      // field flags
      data.flags = FPDFAnnot_GetFormFieldFlags(formHandle, annot);

      // bounds
      FS_RECTF rect;
      if (FPDFAnnot_GetRect(annot, &rect)) {
        data.hasBounds = true;
        data.left = rect.left;
        data.bottom = rect.bottom;
        data.right = rect.right;
        data.top = rect.top;
      }

      // checked state — derive from value/exportValue to avoid needing
      // FORM_OnAfterLoadPage (which conflicts with page lifecycle)
      if (fieldType == FPDF_FORMFIELD_CHECKBOX) {
        // checkbox is checked when its value is not "Off"
        static const std::u16string off = u"Off";
        data.isChecked = !data.value.empty() && data.value != off;
      } else if (fieldType == FPDF_FORMFIELD_RADIOBUTTON) {
        // radio is checked when its appearance state (/AS) is not "Off"
        static const std::u16string off = u"Off";
        std::u16string as = ReadU16(
            [&](auto *, unsigned long) {
              return FPDFAnnot_GetStringValue(annot, "AS", nullptr, 0);
            },
            [&](FPDF_WCHAR *buf, unsigned long len) {
              return FPDFAnnot_GetStringValue(annot, "AS", buf, len);
            });
        data.isChecked = !as.empty() && as != off;
      }

      // options (combo box / list box)
      int optCount = FPDFAnnot_GetOptionCount(formHandle, annot);
      if (optCount > 0) {
        for (int j = 0; j < optCount; j++) {
          FormFieldOptionData opt;
          opt.label = ReadU16(
              [&](auto *, unsigned long) {
                return FPDFAnnot_GetOptionLabel(formHandle, annot, j, nullptr,
                                                0);
              },
              [&](FPDF_WCHAR *buf, unsigned long len) {
                return FPDFAnnot_GetOptionLabel(formHandle, annot, j, buf, len);
              });
          opt.isSelected =
              FPDFAnnot_IsOptionSelected(formHandle, annot, j) != 0;
          data.options.push_back(std::move(opt));
        }
      }

      FPDFPage_CloseAnnot(annot);
      fields_.push_back(std::move(data));
    }

    if (formHandle)
      FPDFDOC_ExitFormFillEnvironment(formHandle);
  }

  void OnOK() override {
    Napi::Env env = Env();
    Napi::Array arr = Napi::Array::New(env, fields_.size());
    for (uint32_t i = 0; i < fields_.size(); i++) {
      auto &d = fields_[i];
      Napi::Object obj = Napi::Object::New(env);
      obj.Set("type", Napi::String::New(env, d.type));
      SetU16(obj, "name", env, d.name);
      SetU16(obj, "value", env, d.value);
      SetU16IfPresent(obj, "alternateName", env, d.alternateName);
      SetU16IfPresent(obj, "exportValue", env, d.exportValue);
      obj.Set("flags", Napi::Number::New(env, d.flags));

      if (d.hasBounds)
        obj.Set("bounds",
                CreateBoundsObject(env, d.left, d.bottom, d.right, d.top));

      obj.Set("isChecked", Napi::Boolean::New(env, d.isChecked));

      if (!d.options.empty()) {
        Napi::Array optArr = Napi::Array::New(env, d.options.size());
        for (uint32_t j = 0; j < d.options.size(); j++) {
          auto &opt = d.options[j];
          Napi::Object optObj = Napi::Object::New(env);
          SetU16(optObj, "label", env, opt.label);
          optObj.Set("isSelected", Napi::Boolean::New(env, opt.isSelected));
          optArr.Set(j, optObj);
        }
        obj.Set("options", optArr);
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
  FPDF_DOCUMENT doc_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<FormFieldData> fields_;
};
