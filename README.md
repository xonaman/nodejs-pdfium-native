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
- ABI-stable via N-API — works across Node.js 20–24 without recompilation
- Password-protected PDFs supported out of the box

### 🎯 Use cases

- 🖼️ Generate thumbnails and previews for uploaded PDFs
- 📄 Extract searchable text from documents at scale
- ⚙️ Build server-side PDF processing pipelines
- ✂️ Split PDFs by page range or merge multiple PDFs into one
- 🔗 Read annotations, bookmarks, links, and form fields from existing PDFs

### 📊 How it compares

|                 | **pdfium-native**    | @hyzyla/pdfium      | pdfjs-dist        |
| --------------- | -------------------- | ------------------- | ----------------- |
| Engine          | PDFium (C++ addon)   | PDFium (WASM)       | pdf.js (JS)       |
| Rendering       | ✅ JPEG/PNG built-in | ⚠️ Raw bitmap (BYO) | ⚠️ Canvas/browser |
| Text extraction | ✅                   | ❌                  | ✅                |
| Search          | ✅ With rects        | ❌                  | ⚠️ Manual         |
| Split / Merge   | ✅                   | ❌                  | ❌                |
| Annotations     | ✅                   | ❌                  | ⚠️ Partial        |
| Bookmarks       | ✅                   | ❌                  | ✅                |
| Links           | ✅                   | ❌                  | ✅                |
| Form fields     | ✅                   | ❌                  | ✅                |
| Async I/O       | ✅ libuv workers     | ❌ Sync             | ❌ Main thread    |
| Platforms       | macOS/Linux/Windows  | Any (WASM)          | Any               |

¹ Prebuilt binaries downloaded at install — no runtime dependencies. Falls back to source compilation if unavailable.

## Table of Contents

- [Install](#-install)
- [Supported Platforms](#-supported-platforms)
- [API](#-api)
  - [loadDocument](#loaddocumentinput-password)
  - [splitDocument](#splitdocumentinput-splitat-options)
  - [mergeDocuments](#mergedocumentsinputs-options)
  - [PDFiumDocument](#pdfiumdocument)
  - [PDFiumPage](#pdfiumpage)
  - [DocumentMetadata](#documentmetadata)
- [Concurrency](#️-concurrency)
- [Memory Management](#-memory-management)
- [License](#-license)

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

### `splitDocument(input, splitAt, options?)`

Splits a PDF into multiple documents at the given page indices. Each index in `splitAt` marks the first page of a new chunk. Returns `Promise<Buffer[]>`, or `Promise<void>` if `outputs` is set.

A 10-page PDF with `splitAt: [3, 7]` produces three documents: pages 0–2, 3–6, and 7–9.

```typescript
import { splitDocument } from 'pdfium-native';

// split a two-page PDF into two single-page documents
const [part1, part2] = await splitDocument('report.pdf', [1]);

// split into three parts (no split points = single document containing all pages)
const [a, b, c] = await splitDocument(buffer, [3, 7]);

// write parts to files
await splitDocument('report.pdf', [5], {
  outputs: ['first-half.pdf', 'second-half.pdf'],
});

// password-protected source
const parts = await splitDocument('encrypted.pdf', [3], { password: 'secret' });
```

| Option     | Type       | Default | Description                                                                                               |
| ---------- | ---------- | ------- | --------------------------------------------------------------------------------------------------------- |
| `outputs`  | `string[]` | —       | Write each part to these file paths instead of returning Buffers. Must have `splitAt.length + 1` entries. |
| `password` | `string`   | —       | Password for the source PDF.                                                                              |

---

### `mergeDocuments(inputs, options?)`

Combines multiple PDFs into a single document. Returns `Promise<Buffer>`, or `Promise<void>` if `output` is set.

Each element of `inputs` can be a `Buffer`, a file path string, or an object `{ input, password? }` for password-protected PDFs.

```typescript
import { mergeDocuments } from 'pdfium-native';

// merge two files
const buf = await mergeDocuments(['part1.pdf', 'part2.pdf']);

// mix buffers, paths, and password-protected PDFs
const buf = await mergeDocuments([
  buffer1,
  'part2.pdf',
  { input: 'encrypted.pdf', password: 'secret' },
]);

// write directly to a file
await mergeDocuments(['a.pdf', 'b.pdf'], { output: 'merged.pdf' });
```

| Option   | Type     | Default | Description                                            |
| -------- | -------- | ------- | ------------------------------------------------------ |
| `output` | `string` | —       | Write to this file path instead of returning a Buffer. |

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
  open: boolean; // whether the node is initially expanded
  actionType?: 'goto' | 'remoteGoto' | 'uri' | 'launch' | 'embeddedGoto';
  url?: string; // external URL for URI bookmarks
  destX?: number; // destination X coordinate
  destY?: number; // destination Y coordinate
  destZoom?: number; // destination zoom level
  children?: Bookmark[];
}
```

#### `destroy()`

Closes the document and frees all native resources. Must be called when done.

---

### PDFiumPage

| Property          | Type                | Description                                         |
| ----------------- | ------------------- | --------------------------------------------------- |
| `width`           | `number`            | Page width in points (1 pt = 1/72 inch).            |
| `height`          | `number`            | Page height in points.                              |
| `number`          | `number`            | 0-based page index.                                 |
| `objectCount`     | `number`            | Number of page objects (text, images, paths, etc.). |
| `rotation`        | `number`            | Page rotation: 0, 1 (90° CW), 2 (180°), 3 (270°).   |
| `hasTransparency` | `boolean`           | Whether the page has transparency.                  |
| `label`           | `string?`           | Page label (e.g. 'i', 'ii', '1').                   |
| `cropBox`         | `PageObjectBounds?` | Crop box (visible region), if set.                  |
| `trimBox`         | `PageObjectBounds?` | Trim box (intended finished size), if set.          |

#### `getText()`

Extracts all text from the page. Returns `Promise<string>`.

#### `render(options?)`

Renders the page to an encoded image. Returns `Promise<Buffer>`, or `Promise<void>` when `output` is specified.

```typescript
interface PageRenderOptions {
  scale?: number; // default: 1 (72 DPI). Use 3–4 for print quality.
  width?: number; // override render width in pixels
  height?: number; // override render height in pixels
  format?: 'jpeg' | 'png'; // default: 'png'
  quality?: number; // JPEG quality 1–100 (default: 100)
  output?: string; // write to file instead of returning a Buffer
  rotation?: 0 | 1 | 2 | 3; // 0=none, 1=90° CW, 2=180°, 3=270° CW
  transparent?: boolean; // transparent background (PNG only, default: false)
  renderAnnotations?: boolean; // render annotations (default: true)
  grayscale?: boolean; // render in grayscale
  lcdText?: boolean; // LCD-optimized sub-pixel text rendering
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

// type: 'text' adds: text, fontSize, fontName, fontWeight?, italicAngle?,
//   renderMode?, fontFamily?, isEmbedded?, fontFlags?
// type: 'image' adds: imageWidth, imageHeight, horizontalDpi?, verticalDpi?,
//   bitsPerPixel?, colorspace?, filters?, render()
// type: 'path' | 'shading' | 'form' | 'unknown'
```

**Image objects** have a `render()` method for extracting the embedded image:

```typescript
const obj = await page.getObject(0);
if (obj.type === 'image') {
  const png = await obj.render(); // PNG buffer (default)
  const jpeg = await obj.render({ format: 'jpeg', quality: 80 });
  const raw = await obj.render({ format: 'raw' }); // original encoded stream
  await obj.render({ output: '/tmp/image.png' }); // write to file
  await obj.render({ rendered: true }); // apply image mask and transformation matrix
}
```

```typescript
interface ImageRenderOptions {
  format?: 'jpeg' | 'png' | 'raw'; // default: 'png'. 'raw' returns original stream bytes
  quality?: number; // JPEG quality 1–100 (default: 100)
  output?: string; // write to file instead of returning a Buffer
  rendered?: boolean; // apply image mask and transformation matrix (default: false)
}
```

#### `objects()`

Async generator that yields every page object. Convenience wrapper around `getObject()`.

```typescript
for await (const obj of page.objects()) {
  if (obj.type === 'image') {
    await obj.render({ output: `image-${obj.imageWidth}x${obj.imageHeight}.png` });
  }
}
```

#### `getLinks()`

Returns all links on the page. Returns `Promise<Link[]>`.

```typescript
interface Link {
  bounds?: { left; bottom; right; top };
  url?: string; // external URL
  pageIndex?: number; // internal link target
  actionType?: 'goto' | 'remoteGoto' | 'uri' | 'launch' | 'embeddedGoto' | 'unknown';
  destX?: number; // destination X coordinate
  destY?: number; // destination Y coordinate
  destZoom?: number; // destination zoom level
  filePath?: string; // file path for remote goto / launch actions
}
```

#### `search(text, options?)`

Searches for text on the page. Returns `Promise<SearchMatch[]>` with character positions and bounding rectangles.

```typescript
const matches = await page.search('invoice', {
  caseSensitive: true,
  wholeWord: false,
  consecutive: false,
});
// [{ charIndex: 42, length: 7, matchedText: 'invoice', rects: [{ left, top, right, bottom }] }]
```

#### `getAnnotations()`

Returns all annotations on the page. Returns `Promise<Annotation[]>`.

```typescript
interface Annotation {
  type: 'text' | 'link' | 'highlight' | 'underline' | 'strikeout' | /* ... */ 'unknown';
  bounds?: { left; bottom; right; top };
  contents: string;
  color: { r; g; b; a } | null;
  interiorColor?: { r; g; b; a }; // fill color for markup annotations
  author: string; // annotation author
  subject: string; // annotation subject
  creationDate: string; // PDF date string (e.g. "D:20250101120000Z")
  modDate: string; // modification date
  flags: number; // annotation flags bitmask (PDF spec Table 165)
  border?: { horizontalRadius; verticalRadius; width };
  quadPoints?: Array<{ x1; y1; x2; y2; x3; y3; x4; y4 }>;
}
```

#### `getFormFields()`

Returns all form fields on the page. Returns `Promise<FormField[]>`.

```typescript
interface FormField {
  type:
    | 'unknown'
    | 'pushButton'
    | 'checkbox'
    | 'radioButton'
    | 'comboBox'
    | 'listBox'
    | 'textField'
    | 'signature';
  name: string; // field name
  value: string; // current value
  alternateName?: string; // tooltip / alternate field name
  exportValue?: string; // export value (checkboxes / radio buttons)
  flags: number; // field flags bitmask
  bounds?: { left; bottom; right; top };
  isChecked: boolean; // whether checkbox / radio is checked
  options?: FormFieldOption[]; // options for combo box / list box
}

interface FormFieldOption {
  label: string;
  isSelected: boolean;
}
```

```typescript
const fields = await page.getFormFields();
const textFields = fields.filter((f) => f.type === 'textField');
const checked = fields.filter((f) => f.isChecked);
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
  permissions: {
    print: boolean;
    modify: boolean;
    copy: boolean;
    annotate: boolean;
    fillForms: boolean;
    extractForAccessibility: boolean;
    assemble: boolean;
    printHighQuality: boolean;
  };
  isTagged: boolean; // whether the PDF is a tagged PDF
  language: string; // document language (e.g. 'en-US')
  signatureCount: number; // number of digital signatures
  attachmentCount: number; // number of file attachments
  permanentId?: string; // permanent file identifier (hex)
  changingId?: string; // changing file identifier (hex)
}
```

---

## ⚙️ Concurrency

### `concurrency(value?): number`

Gets or sets the maximum number of concurrent native operations dispatched to the thread pool.

The default is the number of CPU cores (`os.availableParallelism()`). A value of `0` resets to the default.

PDFium is single-threaded internally — all operations are serialized through a global mutex. The concurrency limiter prevents excess libuv worker threads from being blocked waiting on that mutex.

```typescript
import { loadDocument, concurrency } from 'pdfium-native';

concurrency(); // 8 (CPU cores)
concurrency(2); // limit to 2 concurrent operations
concurrency(0); // reset to default
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
