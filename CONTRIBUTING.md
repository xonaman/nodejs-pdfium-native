# Contributing to pdfium-native

Thanks for your interest in improving `pdfium-native`! This package is a native
Node.js addon that wraps [PDFium](https://pdfium.googlesource.com/pdfium/) for
PDF rendering and text extraction. The notes below cover the native development
loop so you can build and test changes locally.

## Prerequisites

- Node.js `>=22.0.0` (see `.nvmrc` — `nvm use` will pick the right version)
- npm `11.x` (pinned via the `packageManager` field)
- A C++ toolchain capable of C++20:
  - **macOS** — Xcode Command Line Tools (`xcode-select --install`)
  - **Linux** — `build-essential` (and `python3` for node-gyp)
  - **Windows** — Visual Studio 2022 with the "Desktop development with C++" workload

## Native development loop

The addon links against a prebuilt PDFium shared library that is downloaded
(and checksum-verified) into `deps/pdfium/`. A full from-source loop looks like:

```bash
# 1. Install JS dependencies without running the postinstall native build
npm ci --ignore-scripts

# 2. Download + verify the prebuilt PDFium native dependency
npm run download            # fetches the libpdfium for your platform/arch

# 3. Compile the C++ addon (src/ -> build/Release/pdfium.node)
npx node-gyp rebuild

# 4. Bundle the shared library next to the compiled addon
node scripts/bundle-lib.mjs

# 5. Compile TypeScript (lib/ -> dist/) and run the test suite
npm run build:ts
npm test
```

Steps 3 and 4 are also wrapped together as `npm run build`
(`node-gyp rebuild && node scripts/bundle-lib.mjs`).

> **Install fallback:** on a normal `npm install`, `scripts/install.mjs` first
> tries to download a prebuilt binary for your platform/arch. If none is
> available, it transparently falls back to compiling the addon from source,
> which is why the C++ toolchain above is required for unsupported targets.

## Quality checks

Run these before opening a pull request — CI runs the same checks:

```bash
npm run format:check   # Prettier
npm run lint:check     # ESLint (type-aware for lib/)
npm run typecheck      # tsc --noEmit
npm test               # vitest run
npm run verify:checksums  # native-dependency integrity tripwire
```

`npm run format` and `npm run lint` apply autofixes. A Husky pre-commit hook runs
Prettier and ESLint on staged files.

## Pull requests

- Branch off `main` and keep changes focused.
- Add or update tests for behavior changes.
- Update `README.md` and add a `CHANGELOG.md` entry under `## [Unreleased]`
  (Keep a Changelog format) when your change is user-visible.
- Make sure all quality checks above pass locally.

## Reporting security issues

Please do **not** open public issues for security vulnerabilities. Follow the
process in [`SECURITY.md`](./SECURITY.md) instead.
