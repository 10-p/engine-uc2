// engine-uc2 D-7/D-8: the frontend's Playwright config (browser flags, served build, GPU recipe) with this folder's
// spec only. Used as --config by the command in uc2-d78.spec.js (the same arrangement as tools/dev/d2/web).
const path = require('path');
const e2e = path.join(process.env.UFRONTEND || path.join(__dirname, '..', '..', '..', '..', '..', 'ufrontend'), 'packages', 'e2e');
const base = require(path.join(e2e, 'playwright.config.js'));
// Test output (and the per-test browser profiles under it) goes to D78_E2E_OUT, default <WorkUC2>/logs/d78/web.
const out = process.env.D78_E2E_OUT || path.join(__dirname, '..', '..', '..', '..', '..', 'logs', 'd78', 'web');
module.exports = { ...base, testDir: __dirname, testMatch: ['uc2-d78.spec.js'], outputDir: path.join(out, 'test-results') };
