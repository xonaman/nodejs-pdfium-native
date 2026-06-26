/**
 * Shared download/verify helpers for the install and dependency-build scripts.
 *
 * Goals:
 *  - never trust the network blindly: verify a pinned SHA-256 before use,
 *  - survive flaky connections: timeout + exponential-backoff retry,
 *  - never leave a half-written file behind: download to `<dest>.part` and
 *    rename atomically only after the checksum passes,
 *  - extract archives without restoring archived ownership or honouring
 *    absolute/`..` paths (tar's default, kept by NOT passing -P).
 */
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import {
  createReadStream,
  createWriteStream,
  mkdirSync,
  readFileSync,
  rmSync,
  renameSync,
} from 'node:fs';
import { dirname, join } from 'node:path';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

/** Compute the SHA-256 of a file as a lowercase hex string. */
export async function sha256File(path) {
  const hash = createHash('sha256');
  await pipeline(createReadStream(path), hash);
  return hash.digest('hex');
}

/** Read the native-dependency manifest (scripts/native-deps.json). */
export function loadManifest() {
  return JSON.parse(readFileSync(join(import.meta.dirname, '..', 'native-deps.json'), 'utf8'));
}

/**
 * Download `url` to `dest` with a per-attempt timeout and exponential-backoff
 * retry. Writes to `<dest>.part` (same filesystem, so the rename is atomic) and
 * only promotes it once the optional `expectedSha256` matches. A checksum
 * mismatch or unexpected origin fails immediately (not retried).
 *
 * @param {string} url
 * @param {string} dest
 * @param {{ expectedSha256?: string, allowedOrigin?: string, timeoutMs?: number, retries?: number }} [opts]
 */
export async function downloadFile(url, dest, opts = {}) {
  const { expectedSha256, allowedOrigin, timeoutMs = 60000, retries = 4 } = opts;

  const u = new URL(url);
  if (allowedOrigin && u.origin !== allowedOrigin) {
    throw new Error(`Refusing to download from unexpected origin: ${u.origin}`);
  }

  mkdirSync(dirname(dest), { recursive: true });
  const tmp = `${dest}.part`;

  let lastErr;
  for (let attempt = 1; attempt <= retries; attempt++) {
    try {
      const res = await fetch(u, { redirect: 'follow', signal: AbortSignal.timeout(timeoutMs) });
      if (!res.ok) throw new Error(`HTTP ${res.status} ${res.statusText}`);
      if (!res.body) throw new Error('empty response body');
      await pipeline(Readable.fromWeb(res.body), createWriteStream(tmp));

      if (expectedSha256) {
        const actual = await sha256File(tmp);
        if (actual !== expectedSha256.toLowerCase()) {
          throw new Error(
            `checksum mismatch for ${u.href}\n  expected ${expectedSha256}\n  actual   ${actual}`,
          );
        }
      }

      renameSync(tmp, dest);
      return dest;
    } catch (err) {
      lastErr = err;
      rmSync(tmp, { force: true });
      const msg = String(err?.message ?? err);
      // integrity failures are not transient — surface them immediately
      if (msg.includes('checksum mismatch') || msg.includes('unexpected origin')) throw err;
      if (attempt < retries) {
        const backoff = Math.min(1000 * 2 ** (attempt - 1), 8000);
        console.warn(`  attempt ${attempt}/${retries} failed (${msg}); retrying in ${backoff}ms`);
        await sleep(backoff);
      }
    }
  }
  throw new Error(`Failed to download ${u.href} after ${retries} attempts: ${lastErr?.message}`);
}

/**
 * Extract a .tar.gz / .tar.xz into `destDir`. Compression is auto-detected.
 * Not passing `-P` keeps tar's default refusal of absolute and `..` paths;
 * `--no-same-owner` avoids restoring archived uid/gid.
 */
export function extractTarball(tarball, destDir, { strip } = {}) {
  mkdirSync(destDir, { recursive: true });
  const args = ['-xf', tarball, '-C', destDir, '--no-same-owner'];
  if (strip) args.push('--strip-components', String(strip));
  execFileSync('tar', args, { stdio: 'inherit' });
}
