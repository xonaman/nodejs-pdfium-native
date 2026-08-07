# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

## [Unreleased]

## [0.9.1] - 2026-08-07

Fixes for defects found by an adversarial audit of the 0.9.0 release. Every item
below was reproduced against the shipped build before being fixed, and each is
now pinned by a regression test.

### Fixed

- `page.getCharacters()` corrupted every non-BMP character. `FPDFText_GetUnicode` returns one UTF-16 _code unit_ per index — on POSIX as well as Windows — so an emoji arrives as two consecutive lone surrogates, and converting each separately made V8 substitute `U+FFFD`. `chars.map((c) => c.char).join('') === getText()`, an invariant the docs promised, was false for any astral character. The pair is now joined onto the first entry with the second yielding an empty continuation, which keeps both that invariant and the index alignment with `getText()` / `search()`. A new `astral-text.pdf` fixture covers it; the previous tests were ASCII-only.
- `page.getStructTree()` could be made to allocate unboundedly by a ~1.2 KB file. PDFium's structure tree is a DAG and `CPDF_StructElement::UpdateKidIfElement` resolves every kid slot naming a matching dict, so a `/K` array repeating the same child gives that element two live children pointing at one node. Flattening it into a tree doubled the node count per level (a 12-link chain measured 4095 elements, 17 links would be 262,143), all while holding the global PDFium mutex — stalling every other PDFium call in the process, not just the one page. The walk is now bounded by `MAX_STRUCT_NODES` (100,000) in addition to the existing depth cap.
- `flattenDocument(pdf, { pages: [] })` flattened **every** page instead of none. The native worker could not distinguish an explicitly empty selection from an absent one, so a caller computing `pages` from a filter that matched nothing silently stripped every annotation and form widget in the document. An empty array is now a no-op that returns an unchanged copy; omitting the option still means every page.
- `nUpPages()` accepted a `columns`/`rows` value outside the signed 32-bit range and let the native `ToInt32` coercion wrap it, so `columns: 2**32 + 2` silently produced a 2-up layout instead of an error. It now uses the same `isNativeIndex` guard as every other index in the library.
- `TextCharacter.fontWeight` was documented "Absent if unavailable" but was present as a meaningless `0` for every character in a non-embedded font — PDFium returns 0 when the font declares no weight and reserves -1 for errors. The key is now omitted unless the weight is positive.
- `addAttachments()` truncated an attachment of 4 GiB or more on Windows, where `unsigned long` is 32-bit, writing a short file and reporting success. Payloads above `INT_MAX` are now rejected with an explicit message.

## [0.9.0] - 2026-08-07

### Added

- Digital signature inspection on `PDFiumDocument`: `getSignatures()` lists every signature with its encoding (`/SubFilter`), reason, signing time, DocMDP certification level and the `/ByteRange` its digest covers, and `getSignatureContents(index, options?)` returns the raw PKCS#1 / PKCS#7 blob as a `Buffer` (or writes it to `options.output`). Wraps PDFium's `fpdf_signature.h`. Nothing is cryptographically verified — PDFium performs no verification, so these report what the signature dictionary declares; pass the blob and `byteRange` to a crypto library to actually validate. This closes the gap where `metadata.signatureCount` could detect signatures but nothing could inspect them.
- Positioned text extraction on `PDFiumPage`: `getCharacters(options?)` returns every character with its tight bounding box, baseline origin, font name/size/weight/flags and rotation. Character indices line up with `getText()` and `search()` results, so a match maps straight back to page coordinates. `options.start` / `options.count` page through dense pages instead of materialising tens of thousands of objects. Two PDFium behaviours are surfaced rather than hidden: `isGenerated` marks the line breaks and spaces PDFium synthesizes between text runs (they carry no real font), and `angle` runs clockwise — text rotated 45° counterclockwise reports `2π − π/4`, not `π/4`.
- `assemblePages(input, pages, options?)` builds a new PDF from selected source pages, in the order given. Unlike `splitDocument`, which only cuts a document into consecutive runs, the index list is taken literally: pages may be reordered, omitted, or repeated.
- `nUpPages(input, options)` imposes `columns × rows` source pages onto each output sheet. The sheet defaults to the size of the first source page, so a 2×2 n-up of A4 lands on A4.
- `getJavaScriptActions()` on `PDFiumDocument` lists the document-level scripts a viewer runs on open, for inspection and triage of untrusted files. Nothing is executed — the bundled PDFium is built with V8 disabled, so scripts are returned as inert text.
- `getNamedDestinations()` on `PDFiumDocument` lists the anchors that GoTo actions and external links target by name, resolving each to a page index, fit type and view parameters. Reads both the modern `/Names /Dests` name tree and the legacy `/Dests` catalog dictionary.
- `addAttachments(input, attachments, options?)` embeds files into a copy of a document — the library's first write path. **It cannot produce a conformant PDF/A-3 e-invoice:** PDFium's write API reaches only the file name, the bytes and the `/Params` dates, not `/Subtype` (MIME type), `/Desc`, `/AFRelationship`, the catalog `/AF` array, an OutputIntent or XMP. `AttachmentInput` therefore has no `mimeType` or `description` field rather than accepting those values and dropping them silently. Reading e-invoices back remains fully supported.
- `getStructTree()` on `PDFiumPage` returns the tagged-PDF structure tree — the logical outline (headings, paragraphs, tables, figures) that screen readers follow — with alternate text, actual text, per-element language and marked content IDs. `metadata.isTagged` already reported whether a document had one; now it can be read. Recursion is capped like the bookmark walker so a cyclic document cannot overflow the stack.
- `flattenDocument(input, options?)` merges annotations and form widgets into the page content and removes them, with per-page selection and the display/print appearance distinction. It bakes in the appearance streams a document already carries rather than regenerating them, which is correct for any PDF whose widgets have valid `/AP` entries; a form flagged `/NeedAppearances` is the uncovered case.
- New exported types: `Signature`, `GetSignatureContentsOptions`, `TextCharacter`, `GetCharactersOptions`, `AssemblePagesOptions`, `NUpPagesOptions`, `JavaScriptAction`, `NamedDestination`, `DestinationView`, `AttachmentInput`, `AddAttachmentsOptions`, `StructElement`, `FlattenDocumentOptions`.

### Documented

- A README "Not supported" section now states the deliberate gaps up front: interactive form filling (PDFium's `FPDF_FORMFILLINFO` is a UI event-loop API that does not map onto a stateless promise library, and writing `/V` directly yields PDFs whose appearance contradicts their value), producing PDF/A-3 e-invoices, signature verification, XMP metadata and JavaScript execution.
- `getAttachment()` notes a PDFium reading quirk: an embedded file that decodes to _zero_ bytes comes back as its raw compressed stream, because PDFium treats "decoded to empty" as a decode failure and falls back to the undecoded bytes.

### Fixed

- Native error messages containing a colon are no longer truncated. `parseNativeError` treated everything before the first colon as an error code, so `splitDocument`'s `"Split index out of range: 5"` reached the caller as code `"Split index out of range"` with message `" 5"` — the useful half discarded. Only the fixed set of PDFium error codes (`FILE`, `FORMAT`, `PASSWORD`, `SECURITY`, `PAGE`, `UNKNOWN`) is stripped now; every other message is passed through whole. This affected `splitDocument`, `mergeDocuments` and any file-output path since 0.5.0.

### Changed

- Internal: `VectorFileWrite`, `SaveDocument` and `LoadDoc` moved from `split_merge_worker.h` into a shared `document_io.h`, and the byte-writing helper moved from `attachments_worker.h` into `napi_helpers.h` as `WriteBytesToFile`. No public API change.

## [0.8.0] - 2026-08-06

### Added

- File-attachment annotation extraction on `PDFiumPage`: `getAnnotationAttachment(index, options?)` reads the embedded file of a page-level `/FileAttachment` (paperclip) annotation as a `Buffer` (or writes it to `options.output`), wrapping PDFium's `FPDFAnnot_GetFileAttachment` / `FPDFAttachment_GetFile`. This complements the document-level `getAttachment()`: file-attachment annotations live on a page, not in the `/EmbeddedFiles` name tree, so the two cover the two distinct ways PDFium exposes attached files.
- `Annotation` objects now include `index` (the 0-based page annotation index, used to address `getAnnotationAttachment`) and, for `'fileattachment'` annotations, `fileName` (the embedded file's name).

### Fixed

- `document.getAttachment(index)` (added in 0.7.0) and `page.getAnnotationAttachment(index)` now reject indices outside the signed 32-bit range instead of letting them through. The previous `Number.isInteger` guard missed large integers (e.g. `2**32 + 1`), which the native `ToInt32` coercion would silently wrap onto a valid — but different — entry, resolving with the wrong file instead of erroring. Both indices are now validated with a shared 32-bit-aware check.

## [0.7.0] - 2026-08-05

### Added

- Embedded-file (attachment) API on `PDFiumDocument`: `getAttachments()` lists every embedded file in the `/EmbeddedFiles` name tree (name, MIME type, dates) without decoding streams, and `getAttachment(index, options?)` reads the raw bytes as a `Buffer` (or writes them to `options.output`). Wraps PDFium's `FPDFDoc_GetAttachment` / `FPDFAttachment_GetFile`. This closes the gap where `metadata.attachmentCount` could detect attachments but nothing could read them — enabling extraction of the embedded `factur-x.xml` / `zugferd-invoice.xml` / `xrechnung.xml` from ZUGFeRD / Factur-X / XRechnung PDF/A-3 e-invoices.
- `Attachment` and `GetAttachmentOptions` types are exported from the package root.

## [0.6.1] - 2026-07-15

### Security

- Verify the downloaded prebuilt binary against a per-platform SHA-256 pin shipped in the package before use, falling back to the checksum-pinned source build on mismatch or a missing pin. Closes the prebuilt-binary trust gap where a swapped GitHub release asset could be executed unverified.

### Changed

- The `update-native-deps` PR body now describes `verify-checksums` as a consistency check against the same source rather than "independent" verification.

### Added

- Hermetic test coverage for prebuilt verification and manifest generation (`test/prebuild.test.ts`).

## [0.6.0] - 2026-06-27

### Added

- `.nvmrc` and a `packageManager` field to pin the development toolchain
- `.editorconfig` for consistent cross-editor defaults
- Vitest v8 coverage reporting (`npm run test:coverage`)
- `typecheck` step wired into CI (`npm run typecheck`) for type-level regression coverage
- Weekly `windows-latest` canary workflow to detect drift from the pinned `windows-2022` runner
- `"./package.json"` to the package `exports` map

### Changed

- Raised the minimum supported Node.js to `>=22.0.0` (dropped EOL Node 20)
- TypeScript config: `nodenext` module resolution, `es2023` target, and stricter flags (`verbatimModuleSyntax`, `exactOptionalPropertyTypes`, `noUnusedLocals`, `noUnusedParameters`, `noImplicitOverride`, `noFallthroughCasesInSwitch`)
- ESLint: type-aware linting for `lib/`, adopted the `globals` package, and dropped `eslint-plugin-prettier`
- Native build: bumped the C++ standard to C++20 and Node-API to 9 (C++ standard now defined once via a gyp variable)
- CI/CD hardening: pinned all GitHub Actions to commit SHAs, added npm and native-dependency caching, a concurrency group, job timeouts, and top-level least-privilege permissions

### Security

- Added OpenSSF Scorecard analysis, Dependabot, and a security policy (`SECURITY.md`); CodeQL code scanning runs via GitHub's default setup
- Supply chain: pin and verify the SHA-256 of every downloaded native dependency (`scripts/native-deps.json`) through a hardened downloader (timeout, retry, atomic writes, origin pinning); added an `npm run verify:checksums` tripwire

### Fixed

- Windows build: upgraded `node-gyp` to 13 and dropped Node 20 from the CI matrix
- Pinned Windows CI to `windows-2022` to avoid a VS 2026 (MSVC C1001) internal compiler error

## [0.5.6] - 2026-06-26

### Fixed

- Fix double free of addon instance data on environment teardown
- `npm audit` fix for transitive dev dependencies

## [0.5.5] - 2026-04-26

### Changed

- Build TypeScript on `npm install` via `prepare` script (`husky && tsc`) so `dist/` is always rebuilt from `lib/` after install and on `npm pack`/publish, preventing stale `dist/` drift

## [0.5.4] - 2026-04-11

### Fixed

- Remove `%PDF-` header validation — PDFium already returns `FORMAT` error for invalid input; the pre-check was blocking PDF repair for files with damaged or missing headers
- Ref-counted PDFium initialization (`atomic<int>` refcount) for multi-worker-thread safety — init on first env, destroy on last
- Zero-copy `Buffer::New` with heap-allocated vector and release callback for render, image extraction, split, and merge operations (replaces `Buffer::Copy`)
- Fix `ReadU16` minimum length check from 4 to 2 bytes
- Fix `AddonData` memory leak in cleanup hook

### Changed

- Extract shared BGRA→RGB/RGBA pixel conversion into `pixel_convert.h` inline helper
- Consolidate `GetPdfiumErrorMessage()` into `napi_helpers.h`, removing duplicates
- Switch compiler optimization from `-Os` to `-O2`; replace `<filesystem>` with POSIX `access()`
- Compact `pageAliveFlags` and optimize pixel conversion loops

## [0.5.3] - 2026-04-08

### Fixed

- Store document and page constructor references per-env instead of as static globals — fixes cross-isolate V8 crash when multiple worker threads load the addon concurrently
- Add `SafeAsyncWorker` base class with `OnWorkComplete` guard (env-alive check, `napi_open_handle_scope` probe, try-catch) — all 13 async worker classes now inherit from it

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

[Unreleased]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.9.1...HEAD
[0.9.1]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.9.0...v0.9.1
[0.9.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.8.0...v0.9.0
[0.8.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.7.0...v0.8.0
[0.7.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.6.1...v0.7.0
[0.6.1]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.6.0...v0.6.1
[0.6.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.6...v0.6.0
[0.5.6]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.5...v0.5.6
[0.5.5]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.4...v0.5.5
[0.5.4]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.3...v0.5.4
[0.5.3]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.2...v0.5.3
[0.5.2]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.1...v0.5.2
[0.5.1]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.5.0...v0.5.1
[0.5.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.4.0...v0.5.0
[0.4.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.3.0...v0.4.0
[0.3.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.2.4...v0.3.0
[0.2.4]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.2.3...v0.2.4
[0.2.3]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.2.2...v0.2.3
[0.2.2]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.2.0...v0.2.2
[0.2.0]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.1.7...v0.2.0
[0.1.7]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.1.6...v0.1.7
[0.1.6]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.1.5...v0.1.6
[0.1.5]: https://github.com/xonaman/nodejs-pdfium-native/compare/v0.1.4...v0.1.5
[0.1.4]: https://github.com/xonaman/nodejs-pdfium-native/compare/0.1.3...v0.1.4
[0.1.3]: https://github.com/xonaman/nodejs-pdfium-native/compare/0.1.2...0.1.3
[0.1.2]: https://github.com/xonaman/nodejs-pdfium-native/releases/tag/0.1.2
