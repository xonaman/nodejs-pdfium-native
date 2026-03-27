# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/), and this project adheres to [Semantic Versioning](https://semver.org/).

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
