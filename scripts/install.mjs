/**
 * Try to download a prebuilt binary from GitHub releases.
 * Falls back to compiling from source if no prebuilt is available.
 *
 * Prebuilt tarball naming: pdfium-v{version}-{platform}-{arch}.tar.gz
 * Contents: build/Release/pdfium.node + build/Release/libpdfium.{dylib,so,dll}
 */
import { existsSync, mkdirSync, createWriteStream, readFileSync, unlinkSync } from 'node:fs';
import { join } from 'node:path';
import { execSync, execFileSync } from 'node:child_process';
import { pipeline } from 'node:stream/promises';

const root = join(import.meta.dirname, '..');
const pkg = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
const version = pkg.version;
const repo = 'xonaman/nodejs-pdfium-native';

const platform = process.platform;
const arch = process.arch;
const tarName = `pdfium-v${version}-${platform}-${arch}.tar.gz`;
const releaseUrl = `https://github.com/${repo}/releases/download/v${version}/${tarName}`;

const outDir = join(root, 'build', 'Release');

async function tryDownload() {
  console.log(`Checking for prebuilt binary: ${tarName}`);

  try {
    const res = await fetch(releaseUrl, { redirect: 'follow' });
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

    await pipeline(body, fileStream);

    // extract tar.gz into project root
    execFileSync('tar', ['xzf', tmpTar, '-C', root], { stdio: 'inherit' });
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
