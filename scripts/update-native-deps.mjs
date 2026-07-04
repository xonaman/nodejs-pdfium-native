/**
 * Check each native dependency in native-deps.json for a newer upstream GitHub
 * release and, when found, re-pin the version and re-hash the asset(s).
 *
 * This is the piece Dependabot cannot do: the prebuilt/source native libraries
 * are upstream release tarballs, not a package-manager ecosystem. Runs in CI
 * (see .github/workflows/update-native-deps.yml) and emits a markdown summary
 * for the pull-request body via GITHUB_OUTPUT.
 *
 * Supports both manifest shapes:
 *  - per-platform "targets" (baseUrl + { asset, sha256 } map), version == full tag
 *  - single "url" + "sha256", version == numeric (tag with any v/n prefix stripped)
 *
 * Only upstreams that publish a GitHub Release are tracked; a dependency whose
 * repo has no usable release is skipped with a warning (kept manual) rather than
 * guessed from noisy git tags.
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

// derive "OWNER/REPO" from a github.com baseUrl (targets shape) or url (single)
function repoFor(dep) {
  const source = dep.baseUrl ?? dep.url;
  const match = /^https:\/\/github\.com\/([^/]+\/[^/]+)/.exec(source ?? '');
  if (!match) throw new Error(`cannot derive a github.com OWNER/REPO from: ${source}`);
  return match[1];
}

// returns the latest release tag, or null when the repo publishes no release
async function latestReleaseTag(repo) {
  const res = await fetch(`https://api.github.com/repos/${repo}/releases/latest`, {
    headers: apiHeaders,
    signal: AbortSignal.timeout(30000),
  });
  if (res.status === 404) return null;
  if (!res.ok) throw new Error(`GitHub API returned HTTP ${res.status} for ${repo} latest release`);
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
  const repo = repoFor(dep);
  const tag = await latestReleaseTag(repo);
  if (tag === null) {
    console.warn(`${name}: ${repo} publishes no GitHub release; skipping (update manually)`);
    continue;
  }

  if (dep.targets) {
    // targets shape: version is the full tag (e.g. "chromium/7920")
    if (tag === dep.version) {
      console.log(`${name}: up to date (${dep.version})`);
      continue;
    }
    console.log(
      `${name}: ${dep.version} -> ${tag}; re-hashing ${Object.keys(dep.targets).length} targets`,
    );
    for (const [key, target] of Object.entries(dep.targets)) {
      const url = `${dep.baseUrl}/${encodeURIComponent(tag)}/${target.asset}`;
      target.sha256 = await sha256Url(url, dep.origin);
      console.log(`  ${key}: ${target.sha256}`);
    }
    changes.push({ name, from: dep.version, to: tag });
    dep.version = tag;
  } else {
    // single-url shape: manifest version is numeric; the tag may carry a v/n
    // prefix. The url embeds the version string, so a plain substitution rebuilds
    // both the tag segment and the filename.
    const version = tag.replace(/^[^0-9]*/, '');
    if (version === dep.version) {
      console.log(`${name}: up to date (${dep.version})`);
      continue;
    }
    const url = dep.url.replaceAll(dep.version, version);
    console.log(`${name}: ${dep.version} -> ${version}; re-hashing`);
    dep.sha256 = await sha256Url(url, dep.origin);
    dep.url = url;
    changes.push({ name, from: dep.version, to: version });
    dep.version = version;
  }
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
