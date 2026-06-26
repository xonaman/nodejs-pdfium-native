import { execSync } from 'node:child_process';
import { existsSync, mkdirSync, rmSync } from 'node:fs';
import { join } from 'node:path';
import { downloadFile, extractTarball, loadManifest } from './lib/integrity.mjs';

const { pdfium } = loadManifest();

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

const platform = process.platform;
// support cross-compilation via npm_config_arch (e.g. win32 x64 → arm64)
const arch = process.env.npm_config_arch || process.arch;
const musl = isMusl();
const key = musl ? `${platform}-musl-${arch}` : `${platform}-${arch}`;
const entry = pdfium.targets[key];

if (!entry) {
  console.error(`Unsupported platform: ${key}`);
  console.error(`Supported: ${Object.keys(pdfium.targets).join(', ')}`);
  process.exit(1);
}

const depsDir = join(import.meta.dirname, '..', 'deps', 'pdfium');

if (existsSync(join(depsDir, 'include', 'fpdfview.h'))) {
  console.log('PDFium already downloaded, skipping.');
  process.exit(0);
}

// version is repo-controlled (native-deps.json); encode it so it can't escape
// the release path, and downloadFile pins both the origin and the SHA-256.
const url = `${pdfium.baseUrl}/${encodeURIComponent(pdfium.version)}/${entry.asset}`;
const tarball = join(import.meta.dirname, '..', entry.asset);

console.log(`Downloading PDFium ${pdfium.version} for ${key}...`);
console.log(`URL: ${url}`);

await downloadFile(url, tarball, {
  expectedSha256: entry.sha256,
  allowedOrigin: pdfium.origin,
});

console.log('Checksum verified. Extracting...');
mkdirSync(depsDir, { recursive: true });
extractTarball(tarball, depsDir);
rmSync(tarball, { force: true });

console.log(`PDFium installed to ${depsDir}`);
