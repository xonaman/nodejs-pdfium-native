# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

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
