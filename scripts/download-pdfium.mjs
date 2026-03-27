import { execSync, execFileSync } from 'node:child_process';
import { createWriteStream, mkdirSync, existsSync, rmSync } from 'node:fs';
import { pipeline } from 'node:stream/promises';
import { Readable } from 'node:stream';
import { join } from 'node:path';

const PDFIUM_VERSION = 'chromium/7749';
const BASE_URL = 'https://github.com/bblanchon/pdfium-binaries/releases/download';

// detect musl libc (Alpine, Void, etc.)
function isMusl() {
  if (process.platform !== 'linux') return false;
  try {
    const ldd = execSync('ldd --version 2>&1 || true', { encoding: 'utf8' });
    return ldd.toLowerCase().includes('musl');
  } catch {
    return (
      existsSync('/lib/ld-musl-x86_64.so.1') ||
      existsSync('/lib/ld-musl-aarch64.so.1') ||
      existsSync('/lib/ld-musl-armhf.so.1')
    );
  }
}

const TARGETS = {
  // macOS
  'darwin-arm64': 'pdfium-mac-arm64',
  'darwin-x64': 'pdfium-mac-x64',
  // Linux (glibc)
  'linux-x64': 'pdfium-linux-x64',
  'linux-arm64': 'pdfium-linux-arm64',
  'linux-arm': 'pdfium-linux-arm',
  'linux-ia32': 'pdfium-linux-x86',
  'linux-ppc64': 'pdfium-linux-ppc64',
  // Linux (musl / Alpine)
  'linux-musl-x64': 'pdfium-linux-musl-x64',
  'linux-musl-arm64': 'pdfium-linux-musl-arm64',
  'linux-musl-ia32': 'pdfium-linux-musl-x86',
  // Windows
  'win32-x64': 'pdfium-win-x64',
  'win32-arm64': 'pdfium-win-arm64',
  'win32-ia32': 'pdfium-win-x86',
};

const platform = process.platform;
const arch = process.arch;
const musl = isMusl();
const key = musl ? `${platform}-musl-${arch}` : `${platform}-${arch}`;
const target = TARGETS[key];

if (!target) {
  console.error(`Unsupported platform: ${key}`);
  console.error(`Supported: ${Object.keys(TARGETS).join(', ')}`);
  process.exit(1);
}

const depsDir = join(import.meta.dirname, '..', 'deps', 'pdfium');

if (existsSync(join(depsDir, 'include', 'fpdfview.h'))) {
  console.log('PDFium already downloaded, skipping.');
  process.exit(0);
}

const encodedVersion = encodeURIComponent(PDFIUM_VERSION);
const url = `${BASE_URL}/${encodedVersion}/${target}.tgz`;
const tarball = join(import.meta.dirname, '..', `${target}.tgz`);

console.log(`Downloading PDFium ${PDFIUM_VERSION} for ${key}...`);
console.log(`URL: ${url}`);

const response = await fetch(url, { redirect: 'follow' });
if (!response.ok) {
  console.error(`Download failed: ${response.status} ${response.statusText}`);
  process.exit(1);
}

mkdirSync(join(import.meta.dirname, '..', 'deps'), { recursive: true });
await pipeline(Readable.fromWeb(response.body), createWriteStream(tarball));

console.log('Extracting...');
mkdirSync(depsDir, { recursive: true });
execFileSync('tar', ['-xzf', tarball, '-C', depsDir], { stdio: 'inherit' });

// clean up tarball
rmSync(tarball);

console.log(`PDFium installed to ${depsDir}`);
