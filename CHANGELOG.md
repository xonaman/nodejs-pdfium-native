# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [0.5.2] - 2026-04-08

### Added

- Validate `%PDF-` magic header on buffer input before passing to PDFium, returning a `FORMAT` error instead of risking an internal abort on garbage input
- Guard all async worker `OnOK`/`OnError` callbacks against V8 environment teardown — prevents fatal `HandleScope` crash when a Node.js worker thread is terminated mid-operation

## [0.5.1] - 2026-04-04

### Added

- Function overloads for `splitDocument()` and `mergeDocuments()` — return type now narrows to `Promise<void>` when file output paths are provided, or `Promise<Buffer[]>` / `Promise<Buffer>` otherwise

## [0.5.0] - 2026-04-04

### Added

- `splitDocument()` — split a PDF into multiple documents at given page indices, with buffer or file output
- `mergeDocuments()` — combine multiple PDFs into one, supporting buffers, file paths, and per-document passwords
- Comparison table in README now includes Split / Merge row
- Table of contents in README

## [0.4.0] - 2026-04-04

### Added

- `concurrency()` — get/set max concurrent native operations dispatched to the libuv thread pool (default: CPU cores, powered by p-limit)
- Husky + lint-staged pre-commit hook (Prettier + ESLint on staged files)

## [0.3.0] - 2026-03-31

### Added

- `ImagePageObject.render()` — extract and render embedded PDF images as PNG, JPEG, or raw stream bytes
- Three render modes: intrinsic bitmap, rendered bitmap (with mask/matrix), and raw encoded stream
- File output support for image rendering via `output` option
- `objects()` async generator on `PDFiumPage` for iterating all page objects
- Image dimension cap (`MAX_IMAGE_PIXELS = 256 MP`) to prevent allocation failures on huge embedded images
- Negative object index guard in `RenderImageWorker`

### Changed

- **Breaking:** Default render format changed from JPEG to PNG for both `page.render()` and `image.render()` — use `{ format: 'jpeg' }` to restore previous behavior
- Image `render()` binding moved from TypeScript layer to native C++ (`GetObjectWorker::OnOK`) for cleaner API
- `PDFiumPage.getObject()` is now a direct passthrough to the native addon

## [0.3.0] - 2026-03-31

### Added

- `ImagePageObject.render()` — extract and render embedded PDF images as PNG, JPEG, or raw stream bytes
- Three render modes: intrinsic bitmap, rendered bitmap (with mask/matrix), and raw encoded stream
- File output support for image rendering via `output` option
- `objects()` async generator on `PDFiumPage` for iterating all page objects
- Image dimension cap (`MAX_IMAGE_PIXELS = 256 MP`) to prevent allocation failures on huge embedded images
- Negative object index guard in `RenderImageWorker`

### Changed

- **Breaking:** Default render format changed from JPEG to PNG for both `page.render()` and `image.render()` — use `{ format: 'jpeg' }` to restore previous behavior
- Image `render()` binding moved from TypeScript layer to native C++ (`GetObjectWorker::OnOK`) for cleaner API
- `PDFiumPage.getObject()` is now a direct passthrough to the native addon

## [0.2.4] - 2026-03-30

### Fixed

- Bumped minimum Node.js version to 20.11.0 (`import.meta.dirname` requirement)
- Wrapped web `ReadableStream` with `Readable.fromWeb()` in install script
- Updated README Node version range from 18–24 to 20–24
- Used static import for `Readable` instead of dynamic `import()`

### Added

- npm publish provenance (`--provenance` flag)
- Build provenance attestation for prebuilt binaries via `actions/attest-build-provenance`

## [0.2.3] - 2026-03-28

### Added

- `getFormFields()` for reading PDF form fields (text fields, checkboxes, radio buttons, combo boxes, list boxes, signatures)
- Bookmark sibling count limit (`MAX_SIBLINGS_PER_LEVEL = 10000`) to prevent DoS from malicious PDFs

### Changed

- Extracted shared UTF-16 read/write helpers (`ReadU16`, `SetU16`, `SetU16IfPresent`, `ToNapiString`, `ActionTypeString`) to `napi_helpers.h`, reducing boilerplate across all workers
- Split monolithic `page_workers.h` (1500 lines) into 7 individual worker files
- Moved `CHECK_ALIVE` macro to `napi_helpers.h`

### Fixed

- `ReadU16` now rejects odd byte lengths to prevent buffer overflow on malformed PDFs
- `SearchWorker` and `GetFormFieldsWorker` now properly reject (instead of silently resolving empty) when PDFium fails to load text page or initialize form environment
- Form fill environment cleanup is now guarded against null handle
- Render worker validates bitmap buffer and stride before pixel conversion
- Render worker pixel buffer allocation casts both dimensions to `size_t` to prevent integer overflow
- `stb_write_callback` guards against negative size parameter
- `LoadDocumentWorker` rejects buffers larger than `INT_MAX`
- Annotation handles are now closed before `push_back` to prevent resource leaks on allocation failure
- Text and search workers guard against `INT_MAX` overflow in buffer sizing
- Removed unused `#include <algorithm>` from `page.h`

## [0.2.2] - 2026-03-28

### Changed

- README overhaul: benefit-driven headline, Quick Start moved above features, grouped "Why" section (Performance / Developer experience / Reliability), use cases section, competitor comparison table with actual packages (@hyzyla/pdfium, pdfjs-dist, pdf-parse, pdf2json)
- Added npm badges (version, node, license, platform)
- Expanded npm keywords for discoverability
- Added homepage and bugs URLs to package.json

## [0.2.1] - 2026-03-28

### Changed

- Cross-compile `darwin-x64` on `macos-latest` (arm64) and `win32-arm64` on `windows-latest` (x64), eliminating dedicated runners
- Moved `linux-arm64` prebuild from native `ubuntu-24.04-arm` runner to QEMU Docker cross-compile
- Added `linux-musl-x64` and `linux-musl-arm64` prebuilds via Alpine Docker

### Fixed

- `install.mjs` now detects musl libc and downloads the correct prebuilt tarball on Alpine/musl systems
- Cache page dimensions in C++ to avoid reading them without mutex in `Render()`
- `g_initialized` is now `std::atomic<bool>` for thread safety
- Consistent alive flag ordering in `PDFiumPage::CleanUp()` (set under mutex, matching `PDFiumDocument`)

### Performance

- Binary size reduced ~37% via LTO, `-Os`, `-fvisibility=hidden`, dead code stripping, and link-time symbol stripping

## [0.2.0] - 2026-03-28

### Changed

- **BREAKING**: `getText()`, `getObject()`, `getLinks()`, `search()`, `getAnnotations()`, and `getBookmarks()` now return Promises instead of synchronous values
- `objects()` generator is now an async generator (`AsyncGenerator<PageObject>`)
- GetPage bounds check moved off the main thread into the async worker
- Alive flags in `destroy()`/`close()` are now set under the global mutex

### Added

- GC Finalize callbacks on `PDFiumPage` and `PDFiumDocument` as safety net for unreleased resources
- `FPDF_DestroyLibrary` cleanup hook on environment teardown
- `page_workers.h` with dedicated AsyncWorker classes for all page/document operations

### Fixed

- SSRF vulnerability in install script (CodeQL critical)
- Missing `permissions: contents: read` in CI/release workflow jobs (CodeQL medium)

### Removed

- `ia32` platform support (macOS, Linux musl, Windows)

## [0.1.7] - 2026-03-28

### Fixed

- MSVC ARM64 build: compile `stb_image_write` as C++ to fix const initializer error

## [0.1.6] - 2026-03-28

### Fixed

- Windows prebuild: use bash shell for tarball creation

## [0.1.5] - 2026-03-28

### Changed

- Merged publish and prebuild into single `release.yml` workflow

## [0.1.4] - 2026-03-28

### Changed

- Release triggered by tag push (`v*`) instead of GitHub release event
- Auto-create GitHub release with prebuilt tarballs

## [0.1.3] - 2026-03-28

### Fixed

- npm publish authentication with OIDC trusted publishing (Node 24 / npm 11.5.1+)

## [0.1.2] - 2026-03-28

### Fixed

- Prebuild tarball naming and `contents: write` permission for release uploads

## [0.1.1] - 2026-03-28

### Fixed

- Prebuild workflow failures
- Added `@types/node` dev dependency

## [0.1.0] - 2026-03-27

### Added

- Async document loading from `Buffer` or file path (with optional password)
- Page rendering to JPEG/PNG (`Buffer` or file output) with scale/quality options
- Text extraction via `getText()`
- Text search with bounding rectangles via `search()`
- Page object inspection with discriminated union types (text/image/path/etc.)
- Document metadata as a cached readonly property
- Bookmark tree traversal via `getBookmarks()`
- Link enumeration via `getLinks()`
- Annotation reading via `getAnnotations()`
- Support for macOS (arm64, x64), Linux glibc (x64, arm64, arm, ia32, ppc64), Linux musl (x64, arm64, ia32), and Windows (x64, arm64, ia32)
- ESLint + Prettier configuration
- GitHub Actions publish workflow with test gate
- TypeScript type declarations for JS consumers
