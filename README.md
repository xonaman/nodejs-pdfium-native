# pdfium-native

Native Node.js bindings for [PDFium](https://pdfium.googlesource.com/pdfium/) — Google's open-source PDF rendering engine. Built as a C++ addon using N-API for full ABI stability across Node.js versions.

All expensive operations are async (runs on the libuv thread pool), so the main thread is never blocked.

## Install

```bash
npm install pdfium-native
```

Prebuilt PDFium binaries are downloaded automatically during install. A C++ compiler is required (Xcode CLI tools on macOS, `build-essential` on Linux, Visual Studio on Windows).

## Supported Platforms

| OS                    | Architectures                |
| --------------------- | ---------------------------- |
| macOS                 | arm64, x64                   |
| Linux (glibc)         | x64, arm64, arm, ia32, ppc64 |
| Linux (musl / Alpine) | x64, arm64, ia32             |
| Windows               | x64, arm64, ia32             |

## Quick Start

```typescript
import { loadDocument } from 'pdfium-native';

const doc = await loadDocument('invoice.pdf');
const page = await doc.getPage(0);

// extract text
const text = page.getText();

// render to buffer
const jpeg = await page.render({ scale: 3, format: 'jpeg', quality: 90 });

// render to file
await page.render({ scale: 4, format: 'png', output: 'page-0.png' });

page.close();
doc.destroy();
```

## API

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
  console.log(page.getText());
  page.close();
}
```

#### `getBookmarks()`

Returns the bookmark/outline tree as `Bookmark[]`.

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

Extracts all text from the page as a string.

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

Returns the page object at the given index. Objects are discriminated by `type`:

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

Returns all links on the page as `Link[]`.

```typescript
interface Link {
  bounds?: { left; bottom; right; top };
  url?: string; // external URL
  pageIndex?: number; // internal link target
}
```

#### `search(text, options?)`

Searches for text on the page. Returns matches with character positions and bounding rectangles.

```typescript
const matches = page.search('invoice', { caseSensitive: true, wholeWord: false });
// [{ charIndex: 42, length: 7, rects: [{ left, top, right, bottom }] }]
```

#### `getAnnotations()`

Returns all annotations on the page as `Annotation[]`.

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

## Memory Management

Always call `page.close()` and `doc.destroy()` when done. PDFium allocates native memory that is not tracked by the garbage collector.

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

## License

MIT
