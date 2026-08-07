#pragma once

#include "napi_helpers.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "fpdf_structtree.h"

// ---------------------------------------------------------------------------
// GetStructTreeWorker — async tagged-PDF structure tree extraction
// ---------------------------------------------------------------------------
// The structure tree is what makes a PDF "tagged": the logical document
// outline (headings, paragraphs, tables, figures) sitting behind the visual
// layout. It is what screen readers follow, and it carries the alternate text
// that accessibility audits check for. `metadata.isTagged` already reports
// whether a document has one; this reads it.
//
// The tree is per-page: PDFium hands back the elements whose content lives on
// the requested page, not the whole document's tree.

// maximum structure tree recursion depth, mirroring MAX_BOOKMARK_DEPTH
constexpr int MAX_STRUCT_DEPTH = 64;

// Maximum number of elements returned for one page.
//
// A depth cap alone does not bound the work: PDFium's structure tree is a DAG,
// and CPDF_StructElement::UpdateKidIfElement resolves EVERY kid slot whose dict
// matches, so a /K array naming the same child twice gives that element two
// live children pointing at one node. Flattening that into a tree doubles the
// node count per level — a 1.8 KB file with a 12-link chain expands to 4095
// elements, and each extra link doubles it again. Since the whole traversal
// runs holding g_pdfium_mutex, an unbounded walk would stall every other
// PDFium call in the process, not just this one.
//
// The value is deliberately low enough that reaching it stays cheap: a single
// page's structure tree covers only that page's own content, so a real document
// runs to hundreds of elements, not thousands. 20,000 leaves ample headroom
// while keeping the worst case fast even on an emulated runner.
constexpr int MAX_STRUCT_NODES = 20000;

// read one of the struct element's UTF-16LE string properties
template <typename Fn> inline std::u16string ReadStructString(Fn fn) {
  return ReadU16([&](auto *, unsigned long) { return fn(nullptr, 0); },
                 [&](FPDF_WCHAR *buf, unsigned long len) {
                   return fn(buf, len);
                 });
}

struct StructElementInfo {
  std::u16string type;
  std::u16string objType;
  std::u16string title;
  std::u16string altText;
  std::u16string actualText;
  std::u16string id;
  std::u16string lang;
  int markedContentId = -1;
  std::vector<StructElementInfo> children;
};

class GetStructTreeWorker : public SafeAsyncWorker {
public:
  GetStructTreeWorker(Napi::Env env, FPDF_PAGE page,
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

    FPDF_STRUCTTREE tree = FPDF_StructTree_GetForPage(page_);
    if (!tree)
      return; // untagged page — an empty tree, not an error

    int count = FPDF_StructTree_CountChildren(tree);
    for (int i = 0; i < count; i++) {
      if (nodeCount_ >= MAX_STRUCT_NODES)
        break;
      FPDF_STRUCTELEMENT element = FPDF_StructTree_GetChildAtIndex(tree, i);
      if (!element)
        continue;
      roots_.push_back(ReadElement(element, 0));
    }

    FPDF_StructTree_Close(tree);
  }

  void OnOK() override {
    Napi::Env env = Env();
    deferred_.Resolve(BuildArray(env, roots_));
  }

  void OnError(const Napi::Error &err) override {
    deferred_.Reject(err.Value());
  }

private:
  StructElementInfo ReadElement(FPDF_STRUCTELEMENT element, int depth) {
    StructElementInfo info;
    nodeCount_++;

    info.type = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetType(element, buf, len);
    });
    info.objType = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetObjType(element, buf, len);
    });
    info.title = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetTitle(element, buf, len);
    });
    info.altText = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetAltText(element, buf, len);
    });
    info.actualText = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetActualText(element, buf, len);
    });
    info.id = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetID(element, buf, len);
    });
    info.lang = ReadStructString([&](FPDF_WCHAR *buf, unsigned long len) {
      return FPDF_StructElement_GetLang(element, buf, len);
    });

    info.markedContentId = FPDF_StructElement_GetMarkedContentID(element);

    // A malformed or hostile PDF can nest structure elements arbitrarily deep,
    // or cycle; stop descending rather than overflow the stack. The node budget
    // additionally bounds *breadth*, which the depth cap does not — see
    // MAX_STRUCT_NODES.
    if (depth >= MAX_STRUCT_DEPTH || nodeCount_ >= MAX_STRUCT_NODES)
      return info;

    int childCount = FPDF_StructElement_CountChildren(element);
    for (int i = 0; i < childCount; i++) {
      if (nodeCount_ >= MAX_STRUCT_NODES)
        break;
      // returns null for children that are content references rather than
      // elements, which is expected and simply skipped
      FPDF_STRUCTELEMENT child =
          FPDF_StructElement_GetChildAtIndex(element, i);
      if (!child)
        continue;
      info.children.push_back(ReadElement(child, depth + 1));
    }

    return info;
  }

  Napi::Array BuildArray(Napi::Env env,
                         const std::vector<StructElementInfo> &items) {
    Napi::Array arr = Napi::Array::New(env, items.size());
    for (uint32_t i = 0; i < items.size(); i++) {
      const auto &info = items[i];
      Napi::Object obj = Napi::Object::New(env);
      SetU16(obj, "type", env, info.type);
      SetU16IfPresent(obj, "objType", env, info.objType);
      SetU16IfPresent(obj, "title", env, info.title);
      SetU16IfPresent(obj, "altText", env, info.altText);
      SetU16IfPresent(obj, "actualText", env, info.actualText);
      SetU16IfPresent(obj, "id", env, info.id);
      SetU16IfPresent(obj, "lang", env, info.lang);
      if (info.markedContentId >= 0) {
        obj.Set("markedContentId",
                Napi::Number::New(env, info.markedContentId));
      }
      if (!info.children.empty()) {
        obj.Set("children", BuildArray(env, info.children));
      }
      arr.Set(i, obj);
    }
    return arr;
  }

  Napi::Promise::Deferred deferred_;
  FPDF_PAGE page_;
  std::shared_ptr<std::atomic<bool>> pageAlive_;
  std::shared_ptr<std::atomic<bool>> docAlive_;
  std::vector<StructElementInfo> roots_;
  int nodeCount_ = 0;
};
