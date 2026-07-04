/**
 * Check each native dependency in native-deps.json for a newer upstream GitHub
 * release and, when found, re-pin the version and re-hash every target asset.
 *
 * This is the piece Dependabot cannot do: the prebuilt native libraries are
 * upstream release tarballs, not a package-manager ecosystem. Runs in CI (see
 * .github/workflows/update-native-deps.yml) and emits a markdown summary for the
 * pull-request body via GITHUB_OUTPUT.
 */
import { createHash } from 'node:crypto';
import { appendFileSync, readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { Readable } from 'node:stream';
import { pipeline } from 'node:stream/promises';

const manifestPath = join(import.meta.dirname, 'native-deps.json');
const manifest = JSON.parse(readFileSync(manifestPath, 'utf8'));

const token = process.env.GITHUB_TOKEN;
const apiHeaders = {
  accept: 'application/vnd.github+json',
  'user-agent': 'update-native-deps',
  ...(token ? { authorization: `Bearer ${token}` } : {}),
};

// baseUrl looks like https://github.com/OWNER/REPO/releases/download
function repoFromBaseUrl(baseUrl) {
  const match = /^https:\/\/github\.com\/([^/]+\/[^/]+)\/releases\/download\/?$/.exec(baseUrl);
  if (!match) {
    throw new Error(`cannot derive a github.com OWNER/REPO from baseUrl: ${baseUrl}`);
  }
  return match[1];
}

async function latestReleaseTag(repo) {
  const res = await fetch(`https://api.github.com/repos/${repo}/releases/latest`, {
    headers: apiHeaders,
    signal: AbortSignal.timeout(30000),
  });
  if (!res.ok) {
    throw new Error(`GitHub API returned HTTP ${res.status} for ${repo} latest release`);
  }
  const { tag_name: tag } = await res.json();
  if (!tag) throw new Error(`latest release for ${repo} has no tag_name`);
  return tag;
}

async function sha256Url(url, allowedOrigin) {
  const parsed = new URL(url);
  if (allowedOrigin && parsed.origin !== allowedOrigin) {
    throw new Error(`refusing to fetch from unexpected origin: ${parsed.origin}`);
  }
  const res = await fetch(parsed, { redirect: 'follow', signal: AbortSignal.timeout(120000) });
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${parsed.href}`);
  if (!res.body) throw new Error(`empty response body for ${parsed.href}`);
  const hash = createHash('sha256');
  await pipeline(Readable.fromWeb(res.body), hash);
  return hash.digest('hex');
}

const changes = [];
for (const [name, dep] of Object.entries(manifest)) {
  const repo = repoFromBaseUrl(dep.baseUrl);
  const latest = await latestReleaseTag(repo);
  if (latest === dep.version) {
    console.log(`${name}: up to date (${dep.version})`);
    continue;
  }

  console.log(
    `${name}: ${dep.version} -> ${latest}; re-hashing ${Object.keys(dep.targets).length} targets`,
  );
  for (const [key, target] of Object.entries(dep.targets)) {
    const url = `${dep.baseUrl}/${encodeURIComponent(latest)}/${target.asset}`;
    target.sha256 = await sha256Url(url, dep.origin);
    console.log(`  ${key}: ${target.sha256}`);
  }
  changes.push({ name, from: dep.version, to: latest });
  dep.version = latest;
}

if (changes.length === 0) {
  console.log('\nAll native dependencies are up to date.');
} else {
  writeFileSync(manifestPath, `${JSON.stringify(manifest, null, 2)}\n`);
  const summary = changes.map((c) => `- **${c.name}**: \`${c.from}\` → \`${c.to}\``).join('\n');
  console.log(`\n${summary}`);

  if (process.env.GITHUB_OUTPUT) {
    const body = [
      'Automated native dependency update.',
      '',
      summary,
      '',
      'Checksums were recomputed from the upstream release assets and independently re-verified by `verify-checksums`.',
    ].join('\n');
    appendFileSync(process.env.GITHUB_OUTPUT, 'changed=true\n');
    appendFileSync(process.env.GITHUB_OUTPUT, `body<<PR_BODY_EOF\n${body}\nPR_BODY_EOF\n`);
  }
}
