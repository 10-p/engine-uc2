// UI scaling browser check: the frontend's Playwright config (browser flags, served build, GPU recipe) with this folder's
// spec only. Used as --config by the command in uc2-d2-uiscale.spec.js.
const path = require('path');
const e2e = path.join(process.env.UFRONTEND || path.join(__dirname, '..', '..', '..', '..', '..', 'unreal-frontend-arch-private'), 'packages', 'e2e');
const base = require(path.join(e2e, 'playwright.config.js'));
// Test output (and the per-test browser profiles under it) goes to D2_E2E_OUT, default <repo>/../logs/d2/web.
const out = process.env.D2_E2E_OUT || path.join(__dirname, '..', '..', '..', '..', '..', 'logs', 'd2', 'web');
module.exports = { ...base, testDir: __dirname, testMatch: ['uc2-d2-uiscale.spec.js'], outputDir: path.join(out, 'test-results') };
