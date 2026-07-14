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
import { readdirSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { pathToFileURL } from 'node:url';
import { sha256File } from './lib/integrity.mjs';

/**
 * Map every "<prefix>-v<version>-<platform>-<arch>.tar.gz" in `artifactsDir` to
 * its SHA-256, keyed by "<platform>-<arch>" (matching install.mjs's lookup).
 *
 * @param {string} artifactsDir
 * @param {string} version
 * @returns {Promise<Record<string, string>>}
 */
export async function computePins(artifactsDir, version) {
  // strip the "<prefix>-v<version>-" head and ".tar.gz" tail to recover the key
  const headRe = new RegExp('^.*-v' + version.replace(/[.]/g, '\\.') + '-');
  const files = readdirSync(artifactsDir)
    .filter((f) => f.endsWith('.tar.gz'))
    .sort();
  const pins = {};
  for (const file of files) {
    const key = file.replace(/\.tar\.gz$/, '').replace(headRe, '');
    pins[key] = await sha256File(join(artifactsDir, file));
  }
  return pins;
}

// CLI entrypoint (skipped when this module is imported, e.g. by tests)
if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const artifactsDir = process.argv[2];
  if (!artifactsDir) {
    console.error('usage: node scripts/gen-prebuild-manifest.mjs <artifacts-dir>');
    process.exit(1);
  }
  const root = join(import.meta.dirname, '..');
  const { version } = JSON.parse(readFileSync(join(root, 'package.json'), 'utf8'));
  const pins = await computePins(artifactsDir, version);
  if (Object.keys(pins).length === 0) {
    console.error(`No .tar.gz artifacts found in ${artifactsDir}`);
    process.exit(1);
  }
  for (const [key, sha] of Object.entries(pins)) console.log(`  ${key}  ${sha}`);
  const out = join(root, 'scripts', 'prebuilds.json');
  writeFileSync(out, JSON.stringify(pins, null, 2) + '\n');
  console.log(`Wrote ${Object.keys(pins).length} prebuild pin(s) to ${out}`);
}
