/**
 * Try to download a prebuilt binary from GitHub releases.
 * Falls back to compiling from source if no prebuilt is available.
 *
 * Prebuilt tarball naming: pdfium-v{version}-{platform}-{arch}.tar.gz
 * Musl (Alpine) naming:    pdfium-v{version}-linux-musl-{arch}.tar.gz
 * Contents: build/Release/pdfium.node + build/Release/libpdfium.{dylib,so,dll}
 */
import { execSync } from 'node:child_process';
import { createWriteStream, existsSync, mkdirSync, readFileSync, unlinkSync } from 'node:fs';
import { join } from 'node:path';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';
import { extractTarball, verifyPrebuild } from './lib/integrity.mjs';

const root = join(import.meta.dirname, '..');
const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const version = pkg.version;

// Per-platform SHA-256 pins for the prebuilt tarballs, generated at release
// time (scripts/gen-prebuild-manifest.mjs) and shipped inside this package.
// The prebuilt binary is the code that actually runs, so it is verified against
// this pin before use; a swapped release asset fails the check and we fall back
// to the checksum-pinned source build.
function loadPrebuildPins() {
  try {
    return JSON.parse(readFileSync(join(import.meta.dirname, 'prebuilds.json'), 'utf8'));
  } catch {
    return {};
  }
}

// validate version to prevent SSRF via malicious package.json
if (!/^\d+\.\d+\.\d+(-[a-zA-Z0-9.]+)?$/.test(version)) {
  console.error(`Invalid version format: ${version}`);
  process.exit(1);
}

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
const arch = process.arch;
const musl = isMusl();
const platformKey = musl ? `${platform}-musl` : platform;
const tarName = `pdfium-v${version}-${platformKey}-${arch}.tar.gz`;

// hardcoded origin ensures the URL can never point to an attacker-controlled host
const releaseOrigin = 'https://github.com';
const releasePath = `/xonaman/nodejs-pdfium-native/releases/download/v${version}/${tarName}`;
const releaseUrl = new URL(releasePath, releaseOrigin);

const outDir = join(root, 'build', 'Release');

async function tryDownload() {
  console.log(`Checking for prebuilt binary: ${tarName}`);

  try {
    if (releaseUrl.origin !== releaseOrigin) {
      throw new Error(`Unexpected URL origin: ${releaseUrl.origin}`);
    }
    const res = await fetch(releaseUrl, { redirect: 'follow', signal: AbortSignal.timeout(60000) });
    if (!res.ok) {
      console.log(`No prebuilt binary found (HTTP ${res.status}), will compile from source.`);
      return false;
    }

    mkdirSync(outDir, { recursive: true });
    const tmpTar = join(root, tarName);

    // download to temp file
    const fileStream = createWriteStream(tmpTar);
    const body = res.body;
    if (!body) return false;

    await pipeline(Readable.fromWeb(body), fileStream);

    // verify the prebuilt against the pinned checksum before trusting it
    const pinKey = `${platformKey}-${arch}`;
    const check = await verifyPrebuild(tmpTar, pinKey, loadPrebuildPins());
    if (!check.ok) {
      if (check.reason === 'no-pin') {
        console.log(
          `No pinned checksum for ${pinKey}; refusing unverified prebuilt, will compile from source.`,
        );
      } else {
        console.log(
          `Prebuilt checksum mismatch for ${pinKey} (expected ${check.expected}, got ${check.actual}); will compile from source.`,
        );
      }
      unlinkSync(tmpTar);
      return false;
    }

    // extract tar.gz into project root (hardened: no -P, no archived ownership)
    extractTarball(tmpTar, root);
    unlinkSync(tmpTar);

    // verify the .node file exists
    const nodeFile = join(outDir, 'pdfium.node');
    if (existsSync(nodeFile)) {
      console.log('Prebuilt binary installed successfully.');
      return true;
    }

    console.log('Prebuilt archive extracted but pdfium.node not found, will compile from source.');
    return false;
  } catch (err) {
    console.log(`Failed to download prebuilt binary: ${err.message}`);
    return false;
  }
}

async function buildFromSource() {
  console.log('Building from source...');
  execSync('node scripts/download-pdfium.mjs', { stdio: 'inherit', cwd: root });
  execSync('npx node-gyp rebuild', { stdio: 'inherit', cwd: root });
  execSync('node scripts/bundle-lib.mjs', { stdio: 'inherit', cwd: root });
}

const nodeFile = join(outDir, 'pdfium.node');
if (existsSync(nodeFile)) {
  console.log('pdfium.node already exists, skipping install.');
  process.exit(0);
}

const downloaded = await tryDownload();
if (!downloaded) {
  await buildFromSource();
}
