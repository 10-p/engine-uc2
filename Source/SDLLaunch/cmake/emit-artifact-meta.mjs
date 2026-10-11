#!/usr/bin/env node
// emit-artifact-meta.mjs — write <out>/meta.json for the just-linked WASM artifact (docs/05 §4).
// engine-uc2 (M4w): copied from engine-ut2004 and adapted: targetId uc2, repo engine-uc2, buildFlags as in
// Source/SDLLaunch/cmake/Web.cmake. Since F-4d the fetched libraries (gl4es, openal-soft) arrive as --dep-*
// from Web.cmake (cmake/ArtifactDeps.cmake), exactly as in engine-ut2004.
// Run by SDLLaunch's POST_BUILD, AFTER the .js is finalized (the MAX_TEMP_BUFFER sed), so the js
// checksum is of the served file. Self-contained (node stdlib only — no monorepo dep; the artifact
// + its descriptor travel together across the public/private seam). The monorepo's packages/build
// VALIDATES this against the glue ABI_VERSION + the host loader contract before assembling serve-web.
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync, readdirSync, statSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const args = Object.fromEntries(
  process.argv.slice(2).map((a) => { const i = a.indexOf('='); return [a.slice(2, i), a.slice(i + 1)]; })
);
const dir = args.out;

// ufront 2.19: resolve provenance HERE, at BUILD time, rather than trusting the values baked into the
// POST_BUILD command line. CMakeLists computes them with execute_process()/string(TIMESTAMP), which run
// once per CONFIGURE — so a plain `cmake --build` re-stamped the SHA the build tree was CONFIGURED at,
// indefinitely. An artifact rebuilt from new commits kept advertising an old one, which is precisely the
// check the monorepo uses to catch a stale artifact (and which has already cost this project days). It
// fails in BOTH directions: a mismatch is not proof the binary is stale, and a match is not proof it is
// fresh. The passed values remain the fallback for a build from an exported tree with no git.
const _here = dirname(fileURLToPath(import.meta.url));
const gitSha = (() => {
  try {
    return execFileSync('git', ['-C', _here, 'rev-parse', '--short', 'HEAD'], {
      encoding: 'utf8',
      stdio: ['ignore', 'pipe', 'ignore'],
    }).trim();
  } catch {
    return args.gitSha || 'unknown';
  }
})();
const builtAt = new Date().toISOString().replace(/\.\d+Z$/, 'Z');

const sha = (f) => existsSync(join(dir, f))
  ? 'sha256-' + createHash('sha256').update(readFileSync(join(dir, f))).digest('hex')
  : null;

const checksums = { wasm: sha('SDLLaunch.wasm'), js: sha('SDLLaunch.js') };
const dataSha = sha('SDLLaunch.data');           // optional (only if --preload-file produced it)
if (dataSha) checksums.data = dataSha;

// ufront 2.70: every fetched library as `<repo>@<branch|tag>` — never a commit, because the branch tip is
// what a rebuild takes — handed over as --dep-<label>=… from the same cache variables FetchContent read
// (cmake/ArtifactDeps.cmake). A GitHub URL is shortened to owner/name; a local checkout override arrives as
// `local:<path>` and is kept verbatim, so such an artifact never claims a provenance it does not have.
const depSpec = (v) => v.replace(/^https:\/\/github\.com\/([^/]+)\/([^/@]+?)(?:\.git)?(@.*)$/, '$1/$2$3');
const deps = Object.fromEntries(
  Object.entries(args).filter(([k]) => k.startsWith('dep-')).map(([k, v]) => [k.slice(4), depSpec(v)])
);

// T3: the script packages the build compiled from the patch sets (Web.cmake mirrors the build's Scripts/System into
// <out>/<--scripts>/). Each entry REPLACES the asset tree's file at `assetPath` (<--scripts-under>/<name>): the
// frontend overlays them on the manifest, the precache and the published tree. An empty list (UC2_SCRIPT_PATCHES OFF,
// or no --scripts) means the shipped packages. `sha256` is in the `checksums` form.
const scripts = [];
if (args.scripts && existsSync(join(dir, args.scripts))) {
  for (const name of readdirSync(join(dir, args.scripts)).sort()) {
    const rel = `${args.scripts}/${name}`;
    if (!statSync(join(dir, rel)).isFile()) continue;
    scripts.push({
      path: rel,
      assetPath: args['scripts-under'] ? `${args['scripts-under']}/${name}` : name,
      size: statSync(join(dir, rel)).size,
      sha256: sha(rel),
    });
  }
}

const meta = {
  targetId: args.target || 'uc2',
  engineFamily: 'UE2',
  buildConfig: 'wasm-integrated',
  abiVersion: Number(args.abi),                  // from the vendored frozen ue_abi.h UE_ABI_VERSION
  loaderStyle: 'global-module',                  // today's shell loads a classic <script> + bare globals
  provenance: { repo: 'engine-uc2', gitSha, builtAt },
  toolchain: { emscripten: args.emcc || 'unknown', ...deps },
  buildFlags: [
    // Descriptive, hand-maintained: keep in step with Source/SDLLaunch/cmake/Web.cmake (engine-ut2004's list,
    // plus what this build links that it does not: the WebGL 1 limits, -sOFFSCREEN_FRAMEBUFFER, the pool size,
    // catching at the link for openal-soft). Threaded-only artifact: the host must provide SharedArrayBuffer
    // (cross-origin isolation) and OffscreenCanvas.
    '-sASYNCIFY', '-sFULL_ES2', '-sALLOW_MEMORY_GROWTH', '-sFORCE_FILESYSTEM', '-sWASMFS',
    '-pthread', '-sPROXY_TO_PTHREAD', '-sOFFSCREENCANVAS_SUPPORT', '-sOFFSCREENCANVASES_TO_PTHREAD=#canvas',
    '-sPTHREAD_POOL_SIZE=4', '-sMAXIMUM_MEMORY=2147483648', '-sMIN_WEBGL_VERSION=1', '-sMAX_WEBGL_VERSION=1',
    '-sOFFSCREEN_FRAMEBUFFER', '-sNO_DISABLE_EXCEPTION_CATCHING',
  ],
  exports: { symbolContract: 1 },
  checksums,
  scripts,
};

writeFileSync(join(dir, 'meta.json'), JSON.stringify(meta, null, 2) + '\n');
console.log(`[meta] wrote ${join(dir, 'meta.json')} (abi ${meta.abiVersion}, ${meta.buildConfig}, ${meta.loaderStyle}; ` +
  `${scripts.length} script packages${scripts.length ? ': ' + scripts.map((s) => s.assetPath).join(' ') : ''})`);
