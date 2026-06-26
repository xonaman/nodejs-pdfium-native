# Security Policy

## Supported Versions

Only the latest released version of `pdfium-native` receives security updates.

| Version | Supported |
| ------- | --------- |
| latest  | ✅        |
| older   | ❌        |

## Reporting a Vulnerability

Please report security vulnerabilities **privately** through GitHub's
[private vulnerability reporting](https://github.com/xonaman/nodejs-pdfium-native/security/advisories/new)
rather than opening a public issue.

`pdfium-native` parses untrusted PDF input in native C/C++ code via PDFium, so
memory-safety issues are treated as high priority. We aim to acknowledge reports
within 7 days.

Please include:

- a description of the vulnerability and its impact,
- a minimal PDF or code sample that reproduces it,
- the affected package version and platform.
