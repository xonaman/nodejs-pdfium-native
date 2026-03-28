# AI Coding Agent Guide

## Repository Overview

**Type**: Native C++ Node.js addon providing PDFium bindings  
**Languages**: C++ (N-API addon), TypeScript (ESM wrapper + types)  
**PDFium**: bblanchon/pdfium-binaries (chromium/7749, prebuilt shared libraries)  
**Package**: `pdfium-native`, published to npmjs  
**Size**: ~8 C++ headers/source files, ~5 TS files, 38 tests across 6 test files

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

| Path                    | Purpose                                                                          |
| ----------------------- | -------------------------------------------------------------------------------- |
| `src/pdfium_addon.cc`   | C++ addon entry: module init, `LoadDocumentWorker`                               |
| `src/document.h`        | `PDFiumDocument` class, `GetPageWorker`                                          |
| `src/page.h`            | `PDFiumPage` class (all page methods dispatch workers)                           |
| `src/page_workers.h`    | AsyncWorkers: GetText, Search, GetLinks, GetAnnotations, GetObject, GetBookmarks |
| `src/render_worker.h`   | `RenderWorker` for page rendering                                                |
| `src/napi_helpers.h`    | Shared mutex, constants, helper functions                                        |
| `src/stb_image_write.h` | Single-header C lib for JPEG/PNG encoding                                        |
| `lib/index.ts`          | Main export, `loadDocument()`, error parsing                                     |
| `lib/document.ts`       | `PDFiumDocument` TS wrapper                                                      |
| `lib/page.ts`           | `PDFiumPage` TS wrapper                                                          |
| `lib/types.ts`          | All TypeScript interfaces and types                                              |
| `lib/errors.ts`         | Typed error classes                                                              |
| `dist/`                 | Compiled JS + declarations (generated, gitignored)                               |
| `build/Release/`        | Compiled .node + shared lib (generated, gitignored)                              |
| `deps/pdfium/`          | Downloaded PDFium headers + lib (gitignored)                                     |
| `scripts/`              | Build/install/download scripts (ESM .mjs)                                        |
| `test/*.test.ts`        | Vitest tests (6 files, 38 tests)                                                 |
| `test/fixtures/`        | PDF fixtures (generated via pdf-lib)                                             |
| `binding.gyp`           | node-gyp build config (macOS/Linux/Windows)                                      |

### Generated Files (DO NOT EDIT)

- `dist/` — TypeScript compiler output
- `build/` — node-gyp compiler output
- `deps/` — Downloaded PDFium binaries

---

## Architecture

### C++ Layer

Two main classes exposed to JS via N-API:

- **PDFiumDocument** (`src/document.h`): Wraps `FPDF_DOCUMENT`. Properties: `pageCount`, `metadata`. Methods: `getPage()` → Promise, `getBookmarks()` → Promise, `destroy()`. Has `Finalize()` GC destructor as safety net.
- **PDFiumPage** (`src/page.h`): Wraps `FPDF_PAGE`. Properties: `number`, `width`, `height`, `size`, `objectCount`. Methods: `getText()` → Promise, `render()` → Promise, `getObject()` → Promise, `getLinks()` → Promise, `search()` → Promise, `getAnnotations()` → Promise, `close()`. Has `Finalize()` GC destructor as safety net.

Key patterns:

- **All methods are async**: Every expensive operation runs off the main thread via `Napi::AsyncWorker`. Workers extract raw data into C++ structs in `Execute()` (under mutex, on worker thread), then convert to N-API objects in `OnOK()` (on main thread).
- **Global mutex**: `std::mutex g_pdfium_mutex` — PDFium is NOT thread-safe. All PDFium API calls acquire this lock.
- **AsyncWorkers**: `LoadDocumentWorker`, `GetPageWorker`, `RenderWorker` (in respective files), plus `GetTextWorker`, `SearchWorker`, `GetLinksWorker`, `GetAnnotationsWorker`, `GetObjectWorker`, `GetBookmarksWorker` (all in `page_workers.h`).
- **Lifecycle management**: `shared_ptr<atomic<bool>>` alive flags shared between documents/pages/workers. Flags are set under mutex in `CleanUp()`. Workers check alive flags before proceeding.
- **Properties in OnOK()**: Cached properties (width, height, metadata, pageCount) are set as plain JS properties in `AsyncWorker::OnOK()`, not as native getters.
- **Error format**: `GetPdfiumErrorMessage()` returns `"CODE:message"` (e.g., `"PASSWORD:Password required or incorrect"`). The TS layer parses this into typed error classes.

### TypeScript Layer (`lib/`)

- `index.ts`: Loads native addon, exports `loadDocument()`, re-exports all types
- `document.ts`: `PDFiumDocument` class wrapping native document
- `page.ts`: `PDFiumPage` class wrapping native page
- `types.ts`: All interfaces (`NativePage`, `NativeDocument`, `PageObject`, `SearchMatch`, `Bookmark`, `Link`, `Annotation`, etc.)
- `errors.ts`: Typed error classes: `PDFiumError` (base), `PDFiumFileError`, `PDFiumFormatError`, `PDFiumPasswordError`, `PDFiumSecurityError`

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

Tests use `test/fixtures/*.pdf` (generated via `scripts/generate-fixtures.mjs` using pdf-lib).

---

## TypeScript Requirements

- **Strict mode**: `strict: true`, `noUncheckedIndexedAccess: true`
- **ESM only**: `"type": "module"` in package.json
- **Target**: ESNext with bundler module resolution
- **Multi-file**: Types in `lib/types.ts`, classes in separate files, barrel export from `lib/index.ts`

---

## C++ Requirements

- **C++17**: Required by node-addon-api v8
- **N-API / node-addon-api**: ABI-stable addon API
- **No raw pointers in JS**: All PDFium handles wrapped in classes with proper cleanup
- **Thread safety**: Always acquire `g_pdfium_mutex` before any PDFium API call
- **Finalize callbacks**: Both Document and Page have `Finalize()` destructors for GC cleanup
- **RAII**: Document and page handles closed in destructors / explicit close methods

---

## CI/CD Pipeline (GitHub Actions)

**CI** (`ci.yml`): Push/PR to `main` → test matrix (ubuntu/macOS/Windows × Node 20/22/24)  
**Release** (`release.yml`): Tag push `v*` → test → prebuild (8 platform tarballs) → npm publish + GitHub release

---

## Platform Support

| OS            | Architectures          |
| ------------- | ---------------------- |
| macOS         | arm64, x64             |
| Linux (glibc) | x64, arm64, arm, ppc64 |
| Linux (musl)  | x64, arm64             |
| Windows       | x64, arm64             |

Shared library linking:

- **macOS**: `@loader_path` rpath, `install_name_tool` fix in `bundle-lib.mjs`
- **Linux**: `$ORIGIN` rpath
- **Windows**: DLL copied via `binding.gyp` copies directive

---

## Quick Reference

**Add a new PDF feature**: Implement C++ worker in `src/page_workers.h` → add method to `src/page.h` or `src/document.h` → update TS types in `lib/types.ts` → update wrapper in `lib/page.ts` or `lib/document.ts` → add tests in `test/`

**Add a test fixture**: Use `scripts/generate-fixtures.mjs` (requires `pdf-lib`, install with `npm install --no-save pdf-lib`)

**Rebuild after C++ changes**: `npm run build`

**Rebuild after TS changes**: `npm run build:ts`

**Run single test**: `npx vitest run -t "test name"`

---

## Changelog Maintenance

For medium to major changes, update `CHANGELOG.md` before committing. The changelog follows [Keep a Changelog](https://keepachangelog.com/) format with sections: Added, Changed, Fixed, Removed, Deprecated, Security.

```markdown
## [X.Y.Z] - YYYY-MM-DD

### Changed

- Description of breaking or notable change

### Added

- Description of new feature
```

Guidelines:

- **Always update** for: new features, breaking changes, bug fixes, removed functionality, security fixes
- **Skip** for: CI-only tweaks, formatting, internal refactors with no API change, dependency bumps
- Use imperative mood ("Add", "Fix", "Remove", not "Added", "Fixes", "Removed") within entries — but use past tense for section headers per Keep a Changelog convention
- Mark breaking changes prominently (e.g., prefix with **BREAKING**)
- Reference the method/class affected so users can grep for impact
