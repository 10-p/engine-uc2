// Console and AutoFOV, browser check. It lives in
// engine-uc2 and runs with the frontend's harness through the config beside it, so nothing in the frontend
// repository changes. From <repo>/../unreal-frontend-arch-private:
//   yarn build --target uc2 --engine-artifact <engine-uc2>/build-wasm-perf/Source/SDLLaunch
//   node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=<N> -- --target uc2 \
//     --config <engine-uc2>/tools/dev/d78/web/playwright.d78.config.js
//
// At a 1920x1080 canvas: the web ini's console key is the tilde key (KeyboardEvent.code "Backquote", SDL scancode
// GRAVE) and AutoFOV is on; the menu level (3D) is drawn with the Hor+ FOV of 16:9; the key left of 1 opens the
// console over the main menu, typed commands run and their replies reach the log.

const fs = require('fs');
const path = require('path');
const e2e = path.join(process.env.UFRONTEND || path.join(__dirname, '..', '..', '..', '..', '..', 'unreal-frontend-arch-private'), 'packages', 'e2e');
const { test, expect } = require(path.join(e2e, 'lib', 'fixtures'));
const { assertHardwareRenderer } = require(path.join(e2e, 'lib', 'gpu'));
const { setupTracking } = require(path.join(e2e, 'lib', 'tracking'));
const profile = require(path.join(e2e, 'lib', 'engines', 'uc2.profile'));

const BASE_URL = 'http://localhost:8080';

test.use({ viewport: { width: 1920, height: 1080 } });

test('uc2 D-7/D-8: the tilde key opens the console, commands run; AutoFOV widens the 16:9 view', async ({ page }) => {
    test.setTimeout(600 * 1000);
    const t = setupTracking(page, {
        milestones: profile.bootMilestones.concat(profile.renderMilestones),
        surface: profile.bootSurface,
        ignorePageErrors: [/pointer lock/],
    });
    const dir = process.env.UC2_E2E_SHOT_DIR || test.info().outputDir;
    fs.mkdirSync(dir, { recursive: true });
    await page.goto(`${BASE_URL}/`, { waitUntil: 'domcontentloaded', timeout: 60000 });
    expect(await page.evaluate(() => self.crossOriginIsolated === true), 'cross-origin isolated').toBe(true);
    const renderer = await assertHardwareRenderer(page);
    console.log(`  [D78] WebGL renderer: ${renderer}`);
    await page.locator('#start-btn').waitFor({ state: 'visible', timeout: 120000 });
    await page
        .locator('.p-fieldset-legend')
        .filter({ hasText: 'Advanced' })
        .first()
        .click()
        .catch(() => {});
    await page.fill('#engine-options', '-log -ini:System:[UC2GLDrv.UC2GLRenderDevice]:CheckGLErrors=True -ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:CheckGLErrors=True -ini:System:[UC2Movie]:StartupMovies=False', { timeout: 30000 });
    await page.locator('#start-btn').click();

    const count = (re) => t.findLogs(re).length;
    const waitMore = async (re, before, ms) => {
        const end = Date.now() + ms;
        while (Date.now() < end) {
            if (count(re) > before) return true;
            if (t.errors.some((e) => /Aborted|RuntimeError|unreachable|Assertion/.test(e))) return false;
            await page.waitForTimeout(200);
        }
        return false;
    };
    expect(await waitMore(/Bringing Level Menu\.MyLevel up for play/, 0, 300000), 'the menu level is up').toBe(true);
    expect(await waitMore(/UC2GLDrv: frame 600, \d+ GL errors so far/, 0, 240000), 'frame 600').toBe(true);
    console.log(`  [D78] ${t.findLogs(/UC2GLDrv: frame 600, \d+ GL errors so far/).pop()}`);

    const steps = [];
    const step = (name, ok, detail) => {
        steps.push({ name, ok });
        console.log(`  [D78] ${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ': ' + detail : ''}`);
    };
    const key = async (k, holdMs = 150) => {
        await page.keyboard.down(k);
        await page.waitForTimeout(holdMs);
        await page.keyboard.up(k);
    };

    // The console key the engine uses (the web ini's ConsoleKey=192)
    step('console key is the tilde key', count(/Console: Engine\.Console, ConsoleKey 192/) > 0,
        t.findLogs(/Console: Engine\.Console, ConsoleKey/).pop());
    // AutoFOV's mode and the menu level's view at 16:9
    step('AutoFOV on (Adaptive)', count(/\[AutoFOV\] mode=2 referenceAspect=1\.333333/) > 0,
        t.findLogs(/\[AutoFOV\] mode=/).pop());
    const fovLine = t.findLogs(/\[AutoFOV\] viewport=1920x1080 aspect=[\d.]+ base=[\d.]+ effective=[\d.]+/).pop();
    let fovOk = false;
    if (fovLine) {
        const m = fovLine.match(/base=([\d.]+) effective=([\d.]+)/);
        const base = Number(m[1]), eff = Number(m[2]);
        const want = (2 * Math.atan(Math.tan((base * Math.PI) / 360) * (16 / 9) / (4 / 3)) * 180) / Math.PI;
        fovOk = Math.abs(eff - want) < 0.01;
    }
    step('the 1920x1080 view is Hor+ (effective = 2 atan(tan(base/2) * (16/9)/(4/3)))', fovOk, fovLine);

    // the GUI's own log lines (OpenMenu/CloseMenu), suppressed by the shipped ini, tell whether a page changed
    const exec = (c) => page.evaluate((cmd) => window.ueExec && window.ueExec(cmd), c);
    for (const ev of ['DevGUI', 'DevUI']) {
        const b = count(new RegExp(`Unsuppressed event ${ev}`));
        await exec(`UNSUPPRESS EVENT=${ev}`);
        step(`UNSUPPRESS ${ev}`, await waitMore(new RegExp(`Unsuppressed event ${ev}`), b, 10000));
    }
    await page.evaluate(() => document.activeElement && document.activeElement.blur && document.activeElement.blur());
    await page.waitForTimeout(1000);

    // the console over the main menu: open, type, Enter
    await key('Backquote');
    await page.waitForTimeout(800);
    await page.keyboard.type('AUTOFOV', { delay: 60 });
    await page.waitForTimeout(500);
    const shot = await page.locator('#canvas').screenshot({ timeout: 30000 });
    fs.writeFileSync(path.join(dir, 'uc2-d78-console-1920x1080.png'), shot);
    let before = count(/\[AutoFOV\] mode=2 \(Adaptive\)/);
    await key('Enter');
    step('the tilde key opens the console; AUTOFOV typed and run', await waitMore(/\[AutoFOV\] mode=2 \(Adaptive\)/, before, 10000),
        t.findLogs(/\[AutoFOV\] mode=2 \(Adaptive\)/).pop());
    await page.waitForTimeout(800);
    await key('Backquote');
    await page.waitForTimeout(800);
    await page.keyboard.type('get Engine.GameEngine CacheSizeMegs', { delay: 60 });
    await page.waitForTimeout(500);
    before = count(/^\s*32\.0+\s*$/);
    await key('Enter');
    step('get Engine.GameEngine CacheSizeMegs answers 32 in the log', await waitMore(/^\s*32\.0+\s*$/, before, 10000));
    // Escape closes an open console line without taking the main menu anywhere
    await page.waitForTimeout(800);
    before = count(/OpenMenu|CloseMenu/);
    await key('Backquote');
    await page.waitForTimeout(800);
    await key('Escape');
    await page.waitForTimeout(1500);
    step('Escape closes the console, the menu stays', count(/OpenMenu|CloseMenu/) === before);
    const shot2 = await page.locator('#canvas').screenshot({ timeout: 30000 });
    fs.writeFileSync(path.join(dir, 'uc2-d78-menu-1920x1080.png'), shot2);
    console.log(`  [D78] pictures in ${dir}`);

    const traps = t.errors.filter((e) => /Aborted|RuntimeError|unreachable|Assertion/.test(e));
    expect(traps, 'no wasm trap').toEqual([]);
    expect(steps.filter((x) => !x.ok).map((x) => x.name), 'every step').toEqual([]);
});
