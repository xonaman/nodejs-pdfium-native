/**
 * Verify that the SHA-256 hashes pinned in native-deps.json still match what
 * the upstream URLs serve. Use after a version bump, or in CI as a supply-chain
 * tripwire. By default only this platform's artifacts are checked; pass --all
 * to verify every pinned target (large download for prebuilt-binary deps).
 */
import { rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { downloadFile, loadManifest } from './lib/integrity.mjs';

const all = process.argv.includes('--all');
const manifest = loadManifest();
const platform = process.platform;
const arch = process.arch;

const checks = [];
for (const [name, dep] of Object.entries(manifest)) {
  if (dep.targets) {
    for (const [key, t] of Object.entries(dep.targets)) {
      if (!all && key !== `${platform}-${arch}` && key !== `${platform}-musl-${arch}`) continue;
      checks.push({
        label: `${name} ${key}`,
        url: `${dep.baseUrl}/${encodeURIComponent(dep.version)}/${t.asset}`,
        sha256: t.sha256,
        origin: dep.origin,
      });
    }
  } else {
    checks.push({
      label: `${name} ${dep.version}`,
      url: dep.url,
      sha256: dep.sha256,
      origin: dep.origin,
    });
  }
}

let failed = 0;
for (const c of checks) {
  const tmp = join(tmpdir(), `verify-${c.label.replace(/\W+/g, '-')}`);
  try {
    process.stdout.write(`Verifying ${c.label} ... `);
    await downloadFile(c.url, tmp, { expectedSha256: c.sha256, allowedOrigin: c.origin });
    console.log('OK');
  } catch (err) {
    console.log(`FAIL\n  ${err.message}`);
    failed++;
  } finally {
    rmSync(tmp, { force: true });
  }
}

if (failed) {
  console.error(`\n${failed} checksum(s) did not match.`);
  process.exit(1);
}
console.log(`\nAll ${checks.length} checksum(s) verified.`);
