# AI Coding Agent Guide

## Repository Overview

**Type**: Native C++ Node.js addon providing PDFium bindings  
**Languages**: C++ (N-API addon), TypeScript (ESM wrapper + types)  
**PDFium**: bblanchon/pdfium-binaries (chromium/7749, prebuilt shared libraries)  
**Package**: `@xonaman/pdfium-native`, scoped to GitHub Packages  
**Size**: Single C++ source (~1100 lines), single TS wrapper (~300 lines), 38 tests

Trust these instructions. Only perform additional exploration if information is incomplete.

---

## CRITICAL: Build Commands

### Prerequisites

The native addon requires PDFium shared libraries downloaded before compilation.

```bash
# 1. Install dependencies
npm ci

# 2. Download PDFium (fetches prebuilt shared lib for current platform)
node scripts/download-pdfium.mjs

# 3. Build native addon + bundle shared lib
npm run build

# 4. Build TypeScript → dist/
npm run build:ts
```

### Validation Commands

```bash
# Format check (Prettier)
npm run format:check

# Lint check (ESLint with TypeScript)
npm run lint:check

# Tests (Vitest, 38 tests)
npm test
```

### Auto-fix

```bash
npm run format      # Auto-fix formatting
npm run lint        # Auto-fix lint issues
```

---

## Project Structure

| Path | Purpose |
|------|---------|
| `src/pdfium_addon.cc` | C++ native addon (N-API). All PDF operations. |
| `src/stb_image_write.h` | Single-header C lib for JPEG/PNG encoding |
| `lib/index.ts` | TypeScript ESM wrapper, types, error classes |
| `lib/index.test.ts` | Vitest tests (38 tests) |
| `dist/` | Compiled JS + declarations (generated, gitignored) |
| `build/Release/` | Compiled .node + shared lib (generated, gitignored) |
| `deps/pdfium/` | Downloaded PDFium headers + lib (gitignored) |
| `scripts/` | Build/install/download scripts (ESM .mjs) |
| `examples/` | Standalone usage examples |
| `test/fixtures/` | PDF fixtures (metadata, bookmarks, links, annotations) |
| `binding.gyp` | node-gyp build config (macOS/Linux/Windows) |

### Generated Files (DO NOT EDIT)

- `dist/` — TypeScript compiler output
- `build/` — node-gyp compiler output
- `deps/` — Downloaded PDFium binaries

---

## Architecture

### C++ Layer (`src/pdfium_addon.cc`)

Two main classes exposed to JS via N-API:

- **PDFiumDocument**: Wraps `FPDF_DOCUMENT`. Properties: `pageCount`, `metadata`. Methods: `getPage()`, `getBookmarks()`, `destroy()`.
- **PDFiumPage**: Wraps `FPDF_PAGE`. Properties: `number`, `width`, `height`, `size`, `objectCount`. Methods: `getText()`, `render()`, `getObject()`, `getLinks()`, `search()`, `getAnnotations()`, `close()`.

Key patterns:
- **Global mutex**: `std::mutex g_pdfium_mutex` — PDFium is NOT thread-safe. All operations acquire this lock.
- **AsyncWorkers**: `LoadDocumentWorker`, `GetPageWorker`, `RenderWorker` — expensive ops run off the main thread via `Napi::AsyncWorker`.
- **Properties in OnOK()**: Cached properties (width, height, metadata, pageCount) are set as plain JS properties in `AsyncWorker::OnOK()`, not as native getters.
- **Error format**: `GetPdfiumErrorMessage()` returns `"CODE:message"` (e.g., `"PASSWORD:Password required or incorrect"`). The TS layer parses this into typed error classes.

### TypeScript Layer (`lib/index.ts`)

- Loads native addon via `createRequire(import.meta.url)('../build/Release/pdfium.node')`
- Exports typed error classes: `PDFiumError` (base), `PDFiumFileError`, `PDFiumFormatError`, `PDFiumPasswordError`, `PDFiumSecurityError`
- `parseNativeError()` maps C++ error strings to typed errors
- All interfaces exported: `PageSize`, `PageRenderOptions`, `SearchMatch`, `Bookmark`, `Link`, `Annotation`, `DocumentMetadata`, etc.

### Install Flow (`scripts/install.mjs`)

1. Try downloading prebuilt binary from GitHub releases
2. If not available, fall back to: download PDFium → node-gyp rebuild → bundle shared lib

---

## Code Patterns

### C++ Error Handling

```cpp
// in AsyncWorker::Execute()
SetError("CODE:Human-readable message");

// in sync methods
Napi::Error::New(env, "Page is closed").ThrowAsJavaScriptException();
```

Error codes: `FILE`, `FORMAT`, `PASSWORD`, `SECURITY`, `PAGE`, `UNKNOWN`

### C++ Property Pattern (set in OnOK, not native getters)

```cpp
void OnOK() override {
  auto obj = docConstructor.New({});
  obj.Set("pageCount", Napi::Number::New(Env(), pageCount_));
  // ...
  deferred_.Resolve(obj);
}
```

### TypeScript Error Handling

```typescript
try {
  return new PDFiumDocument(await addon.loadDocument(input, password));
} catch (err) {
  throw parseNativeError(err);
}
```

### Test Pattern

```typescript
it('rejects with PDFiumFormatError for invalid data', async () => {
  await expect(loadDocument(Buffer.from('not a pdf'))).rejects.toThrow(PDFiumFormatError);
});
```

Tests use inline PDF strings for simple cases and `test/fixtures/*.pdf` for rich features (bookmarks, links, annotations).

---

## TypeScript Requirements

- **Strict mode**: `strict: true`, `noUncheckedIndexedAccess: true`
- **ESM only**: `"type": "module"` in package.json
- **Target**: ESNext with bundler module resolution
- **Single source file**: `lib/index.ts` — all types, classes, and exports in one file

---

## C++ Requirements

- **C++17**: Required by node-addon-api v8
- **N-API / node-addon-api**: ABI-stable addon API
- **No raw pointers in JS**: All PDFium handles wrapped in classes with proper cleanup
- **Thread safety**: Always acquire `g_pdfium_mutex` before any PDFium API call
- **RAII**: Document and page handles closed in destructors / explicit close methods

---

## CI/CD Pipeline (GitHub Actions)

**CI** (`ci.yml`): Push/PR to `main` → test matrix (ubuntu/macOS/Windows × Node 20/22/24)  
**Publish** (`publish.yml`): Release published → same test matrix → npm publish to GitHub Packages  
**Prebuild** (`prebuild.yml`): Release published → build per-platform tarballs → upload to GitHub release

---

## Platform Support

| OS | Architectures |
|----|---------------|
| macOS | arm64, x64 |
| Linux (glibc) | x64, arm64, arm, ia32, ppc64 |
| Linux (musl) | x64, arm64, ia32 |
| Windows | x64, arm64, ia32 |

Shared library linking:
- **macOS**: `@loader_path` rpath, `install_name_tool` fix in `bundle-lib.mjs`
- **Linux**: `$ORIGIN` rpath
- **Windows**: DLL copied via `binding.gyp` copies directive

---

## Quick Reference

**Add a new PDF feature**: Implement in `src/pdfium_addon.cc` (C++ method on PDFiumPage or PDFiumDocument) → expose via N-API → add TS types and wrapper in `lib/index.ts` → add tests in `lib/index.test.ts`

**Add a test fixture**: Use `scripts/generate-fixtures.mjs` (requires `pdf-lib`, install with `npm install --no-save pdf-lib`)

**Rebuild after C++ changes**: `npm run build`

**Rebuild after TS changes**: `npm run build:ts`

**Run single test**: `npx vitest run -t "test name"`
