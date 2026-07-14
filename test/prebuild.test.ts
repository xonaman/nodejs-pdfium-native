import { createHash } from 'node:crypto';
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterAll, beforeAll, describe, expect, it } from 'vitest';
// The install/release scripts are plain .mjs (they run without a build step);
// import them directly to exercise the real prebuilt-verification logic.
import { sha256File, verifyPrebuild } from '../scripts/lib/integrity.mjs';
import { computePins } from '../scripts/gen-prebuild-manifest.mjs';

const sha256 = (buf: string) => createHash('sha256').update(buf).digest('hex');

let dir: string;
let tar: string;
let hash: string;

beforeAll(async () => {
  // verifyPrebuild only hashes the file, so arbitrary bytes stand in for a tarball
  dir = mkdtempSync(join(tmpdir(), 'prebuild-test-'));
  tar = join(dir, 'pdfium-v9.9.9-darwin-arm64.tar.gz');
  writeFileSync(tar, 'fake-prebuilt-bytes');
  hash = await sha256File(tar);
});

afterAll(() => rmSync(dir, { recursive: true, force: true }));

describe('verifyPrebuild', () => {
  it('accepts a tarball whose hash matches the pin', async () => {
    const r = await verifyPrebuild(tar, 'darwin-arm64', { 'darwin-arm64': hash });
    expect(r.ok).toBe(true);
    expect((r as { sha256: string }).sha256).toBe(hash);
  });

  it('accepts a pin recorded in uppercase (case-insensitive compare)', async () => {
    const r = await verifyPrebuild(tar, 'darwin-arm64', { 'darwin-arm64': hash.toUpperCase() });
    expect(r.ok).toBe(true);
  });

  it('rejects a tampered/mismatched tarball', async () => {
    const r = await verifyPrebuild(tar, 'darwin-arm64', { 'darwin-arm64': 'dead' + hash.slice(4) });
    expect(r.ok).toBe(false);
    expect((r as { reason: string }).reason).toBe('mismatch');
  });

  it('refuses when no pin exists for the platform', async () => {
    const r = await verifyPrebuild(tar, 'win32-arm64', { 'darwin-arm64': hash });
    expect(r.ok).toBe(false);
    expect((r as { reason: string }).reason).toBe('no-pin');
  });
});

describe('computePins', () => {
  it('maps each tarball to its sha256, keyed by <platform>-<arch>', async () => {
    const muslTar = join(dir, 'pdfium-v9.9.9-linux-musl-x64.tar.gz');
    writeFileSync(muslTar, 'other-bytes');
    const pins = await computePins(dir, '9.9.9');
    expect(pins['darwin-arm64']).toBe(hash);
    expect(pins['linux-musl-x64']).toBe(sha256('other-bytes'));
    expect(Object.keys(pins).sort()).toEqual(['darwin-arm64', 'linux-musl-x64']);
  });

  it('ignores non-tarball files', async () => {
    writeFileSync(join(dir, 'pdfium-v9.9.9-notes.txt'), 'ignore me');
    const pins = await computePins(dir, '9.9.9');
    expect(Object.keys(pins)).not.toContain('notes');
  });
});
