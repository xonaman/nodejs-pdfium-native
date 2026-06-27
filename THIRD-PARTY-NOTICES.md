# Third-Party Notices

`pdfium-native` is distributed under the [MIT License](./LICENSE). It bundles
and/or links against the third-party native components listed below. Each
component remains under its own license; the relevant license texts are shipped
inside the prebuilt PDFium distribution under `deps/pdfium/licenses/`.

## Bundled native libraries

| Component                                          | Version         | License                                 |
| -------------------------------------------------- | --------------- | --------------------------------------- |
| [PDFium](https://pdfium.googlesource.com/pdfium/)  | `chromium/7749` | BSD-3-Clause                            |
| [stb_image_write](https://github.com/nothings/stb) | `v1.16`         | Public Domain (Unlicense) or MIT (dual) |

- **PDFium** — the PDF rendering and parsing engine, consumed as a prebuilt
  shared library from
  [bblanchon/pdfium-binaries](https://github.com/bblanchon/pdfium-binaries).
  Copyright The PDFium Authors. Licensed under BSD-3-Clause.
- **stb_image_write** — single-header PNG/JPEG encoder vendored in
  `src/stb_image_write.{h,cc}`, used to encode rendered bitmaps. By Sean Barrett
  (and based on Jon Olick's public-domain JPEG writer). Dual-licensed: you may
  use it under the MIT License or as public domain (Unlicense).

## Components statically linked into the prebuilt PDFium library

The prebuilt PDFium shared library statically incorporates the third-party
components below. Their versions track the upstream PDFium `chromium/7749`
build; full license texts are provided in `deps/pdfium/licenses/`.

| Component                 | License                                   |
| ------------------------- | ----------------------------------------- |
| Abseil                    | Apache-2.0                                |
| Anti-Grain Geometry (2.3) | Anti-Grain Geometry License (permissive)  |
| fast_float                | MIT (also Apache-2.0 / BSL-1.0)           |
| FreeType                  | FreeType License (FTL) or GPLv2           |
| ICU                       | Unicode License v3                        |
| Little CMS (lcms2)        | MIT                                       |
| libjpeg-turbo             | BSD-3-Clause and IJG License              |
| OpenJPEG                  | BSD-2-Clause                              |
| libpng                    | libpng License (PNG Reference Library v2) |
| libtiff                   | libtiff License (MIT/BSD-style)           |
| LLVM libc                 | Apache-2.0 with LLVM Exceptions           |
| simdutf                   | MIT (also Apache-2.0)                     |
| zlib (1.3.1)              | zlib License                              |

If you redistribute `pdfium-native` (or its prebuilt binaries), you must retain
the above notices and the corresponding license texts.
