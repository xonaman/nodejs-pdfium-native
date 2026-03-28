# pdfium-native

[![npm version](https://img.shields.io/npm/v/pdfium-native)](https://www.npmjs.com/package/pdfium-native)
[![Node.js](https://img.shields.io/node/v/pdfium-native)](https://nodejs.org)
[![License](https://img.shields.io/npm/l/pdfium-native)](https://github.com/xonaman/nodejs-pdfium-native/blob/main/LICENSE)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux%20%7C%20Windows-blue)]()

Fast, native PDF rendering and text extraction for Node.js — powered by [PDFium](https://pdfium.googlesource.com/pdfium/), the same engine used in Chromium. Built as a C++ addon with N-API for ABI stability across Node.js versions.

> Designed for server-side workloads. Non-blocking, fast, and production-ready.

## 🚀 Quick Start

```typescript
import { loadDocument } from 'pdfium-native';

const doc = await loadDocument('invoice.pdf');
const page = await doc.getPage(0);

const text = await page.getText();
const image = await page.render({ scale: 3, format: 'png' }); // high-resolution render

page.close();
doc.destroy();
```

## 💡 Why pdfium-native?

**⚡ Performance**

- Native C++ — no WASM overhead, no JS parsing
- Non-blocking — all operations run off the main thread via libuv workers

**🛠️ Developer experience**

- Built-in JPEG/PNG rendering — no extra dependencies like sharp
- Prebuilt binaries for 10 platform/arch combinations — no compile step
- Full TypeScript support with types included

**🔒 Reliability**

- Built on PDFium — the PDF engine used in Chromium
- ABI-stable via N-API — works across Node.js 18–24 without recompilation
- Password-protected PDFs supported out of the box

### 🎯 Use cases

- 🖼️ Generate thumbnails and previews for uploaded PDFs
- 📄 Extract searchable text from documents at scale
- ⚙️ Build server-side PDF processing pipelines
- 🔗 Read annotations, bookmarks, and links from existing PDFs

### 📊 How it compares

|                     | **pdfium-native**       | @hyzyla/pdfium            | pdfjs-dist        | pdf-parse      | pdf2json            |
| ------------------- | ----------------------- | ------------------------- | ----------------- | -------------- | ------------------- |
| Engine              | PDFium (C++ addon)      | PDFium (WASM)             | pdf.js (JS)       | pdf.js (JS)    | pdf.js fork (JS)    |
| Rendering           | ✅ JPEG/PNG built-in    | ⚠️ Raw bitmap (BYO sharp) | ⚠️ Canvas/browser | ❌             | ❌                  |
| Text extraction     | ✅                      | ❌                        | ✅                | ✅             | ✅ (structured)     |
| Search (with rects) | ✅                      | ❌                        | ⚠️ Manual         | ❌             | ❌                  |
| Annotations         | ✅                      | ❌                        | ⚠️ Partial        | ❌             | ❌                  |
| Bookmarks           | ✅                      | ❌                        | ✅                | ❌             | ❌                  |
| Links               | ✅                      | ❌                        | ✅                | ❌             | ❌                  |
| Form fields         | ❌                      | ❌                        | ✅                | ❌             | ✅                  |
| Async I/O           | ✅ libuv workers        | ❌ Sync (WASM)            | ❌ Main thread    | ❌ Main thread | ⚠️ Events / streams |
| Environment         | Node.js                 | Node.js + browser         | Node.js + browser | Node.js        | Node.js             |
| Dependencies        | None¹                   | None (WASM bundled)       | None              | pdf.js         | None (since v3)     |
| Platforms           | macOS / Linux / Windows | Any (WASM)                | Any               | Any            | Any                 |

¹ Prebuilt binaries downloaded at install — no runtime dependencies. Falls back to source compilation if unavailable.

## 📦 Install

```bash
npm install pdfium-native
```

Prebuilt binaries are available for all [supported platforms](#supported-platforms) — most installs require no compiler. If no prebuilt is available, the package falls back to compiling from source (requires a C++ toolchain: Xcode CLI tools on macOS, `build-essential` on Linux, Visual Studio on Windows).

## 🌍 Supported Platforms

| OS                    | Architectures          |
| --------------------- | ---------------------- |
| macOS                 | arm64, x64             |
| Linux (glibc)         | x64, arm64, arm, ppc64 |
| Linux (musl / Alpine) | x64, arm64             |
| Windows               | x64, arm64             |

## 📚 API

### `loadDocument(input, password?)`

Opens a PDF from a `Buffer` or file path string. Returns `Promise<PDFiumDocument>`.

```typescript
const doc = await loadDocument(buffer);
const doc = await loadDocument('/path/to/file.pdf');
const doc = await loadDocument(buffer, 'secret');
```

---

### PDFiumDocument

| Property    | Type               | Description                             |
| ----------- | ------------------ | --------------------------------------- |
| `pageCount` | `number`           | Total number of pages.                  |
| `metadata`  | `DocumentMetadata` | Title, author, dates, PDF version, etc. |

#### `getPage(index)`

Loads a page by 0-based index. Returns `Promise<PDFiumPage>`.

#### `pages()`

Async generator that yields every page. Caller must close each page.

```typescript
for await (const page of doc.pages()) {
  console.log(await page.getText());
  page.close();
}
```

#### `getBookmarks()`

Returns the bookmark/outline tree. Returns `Promise<Bookmark[]>`.

```typescript
interface Bookmark {
  title: string;
  pageIndex?: number;
  children?: Bookmark[];
}
```

#### `destroy()`

Closes the document and frees all native resources. Must be called when done.

---

### PDFiumPage

| Property      | Type                | Description                                         |
| ------------- | ------------------- | --------------------------------------------------- |
| `width`       | `number`            | Page width in points (1 pt = 1/72 inch).            |
| `height`      | `number`            | Page height in points.                              |
| `size`        | `{ width, height }` | Page dimensions.                                    |
| `number`      | `number`            | 0-based page index.                                 |
| `objectCount` | `number`            | Number of page objects (text, images, paths, etc.). |

#### `getText()`

Extracts all text from the page. Returns `Promise<string>`.

#### `render(options?)`

Renders the page to an encoded image. Returns `Promise<Buffer>`, or `Promise<void>` when `output` is specified.

```typescript
interface PageRenderOptions {
  scale?: number; // default: 1 (72 DPI). Use 3–4 for print quality.
  width?: number; // override render width in pixels
  height?: number; // override render height in pixels
  format?: 'jpeg' | 'png'; // default: 'jpeg'
  quality?: number; // JPEG quality 1–100 (default: 100)
  output?: string; // write to file instead of returning a Buffer
}
```

#### `getObject(index)`

Returns the page object at the given index. Returns `Promise<PageObject>`. Objects are discriminated by `type`:

```typescript
type PageObject = TextPageObject | ImagePageObject | OtherPageObject;

// all objects have:
//   bounds: { left, bottom, right, top }
//   fillColor: { r, g, b, a } | null
//   strokeColor: { r, g, b, a } | null

// type: 'text' adds: text, fontSize, fontName
// type: 'image' adds: imageWidth, imageHeight
// type: 'path' | 'shading' | 'form' | 'unknown'
```

#### `getLinks()`

Returns all links on the page. Returns `Promise<Link[]>`.

```typescript
interface Link {
  bounds?: { left; bottom; right; top };
  url?: string; // external URL
  pageIndex?: number; // internal link target
}
```

#### `search(text, options?)`

Searches for text on the page. Returns `Promise<SearchMatch[]>` with character positions and bounding rectangles.

```typescript
const matches = await page.search('invoice', { caseSensitive: true, wholeWord: false });
// [{ charIndex: 42, length: 7, rects: [{ left, top, right, bottom }] }]
```

#### `getAnnotations()`

Returns all annotations on the page. Returns `Promise<Annotation[]>`.

```typescript
interface Annotation {
  type: 'text' | 'link' | 'highlight' | 'underline' | 'strikeout' | /* ... */ 'unknown';
  bounds?: { left; bottom; right; top };
  contents: string;
  color: { r; g; b; a } | null;
}
```

#### `close()`

Closes the page and frees resources. Must be called when done with the page.

---

### DocumentMetadata

```typescript
interface DocumentMetadata {
  title: string;
  author: string;
  subject: string;
  keywords: string;
  creator: string;
  producer: string;
  creationDate: string;
  modDate: string;
  pdfVersion: number; // e.g. 17 for PDF 1.7
}
```

---

## 🙏 Acknowledgements

This project uses prebuilt PDFium binaries from [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries), which provides automated builds of the PDFium library for multiple platforms. Thanks to [@bblanchon](https://github.com/bblanchon) for maintaining this invaluable resource.

## 🧹 Memory Management

Always call `page.close()` and `doc.destroy()` when done. While GC-triggered destructor hooks exist as a safety net, they should not be relied on — explicit cleanup ensures resources are freed promptly.

```typescript
const doc = await loadDocument('file.pdf');
try {
  const page = await doc.getPage(0);
  try {
    // use page
  } finally {
    page.close();
  }
} finally {
  doc.destroy();
}
```

## 📄 License

MIT
