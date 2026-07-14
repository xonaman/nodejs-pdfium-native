/**
 * Generate `scripts/prebuilds.json`: a map of "<platform>-<arch>" -> SHA-256 of
 * each prebuilt release tarball.
 *
 * Run in the release workflow AFTER the prebuild artifacts are downloaded and
 * BEFORE `npm publish`, so the pins ship *inside* the published (SLSA-attested)
 * npm package. `install.mjs` then verifies a downloaded prebuilt against the
 * pin baked into the package, closing the prebuilt-binary trust gap: a swapped
 * GitHub release asset will not match the pin and falls back to the
 * checksum-pinned source build.
 *
 * Usage: node scripts/gen-prebuild-manifest.mjs <artifacts-dir>
 */
import { createHash } from 'node:crypto';
import { createReadStream, readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pipeline } from 'node:stream/promises';

const artifactsDir = process.argv[2];
if (!artifactsDir) {
  console.error('usage: node scripts/gen-prebuild-manifest.mjs <artifacts-dir>');
  process.exit(1);
}

const root = join(import.meta.dirname, '..');
const { version } = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));

// Tarballs are named "<prefix>-v<version>-<platform>-<arch>.tar.gz"; strip the
// "<prefix>-v<version>-" head and ".tar.gz" tail to recover the platform key.
const headRe = new RegExp('^.*-v' + version.replace(/[.]/g, '\\.') + '-');

async function sha256File(path) {
  const hash = createHash('sha256');
  await pipeline(createReadStream(path), hash);
  return hash.digest('hex');
}

const files = readdirSync(artifactsDir)
  .filter((f) => f.endsWith('.tar.gz'))
  .sort();
if (files.length === 0) {
  console.error(`No .tar.gz artifacts found in ${artifactsDir}`);
  process.exit(1);
}

const pins = {};
for (const file of files) {
  const key = file.replace(/\.tar\.gz$/, '').replace(headRe, '');
  pins[key] = await sha256File(join(artifactsDir, file));
  console.log(`  ${key}  ${pins[key]}`);
}

const out = join(root, 'scripts', 'prebuilds.json');
writeFileSync(out, JSON.stringify(pins, null, 2) + '\n');
console.log(`Wrote ${Object.keys(pins).length} prebuild pin(s) to ${out}`);
