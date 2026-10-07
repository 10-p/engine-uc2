// engine-uc2 D-2 (UI scaling; docs/porting/analysis/ui-scale.md), browser check. It lives in engine-uc2 and runs
// with the frontend's harness (its fixtures, its served build, its memory guard) through the config beside it,
// so nothing in the frontend repository is changed. From <WorkUC2>/ufrontend:
//   yarn build --target uc2 --engine-artifact <engine-uc2>/build-wasm-perf/Source/SDLLaunch
//   node packages/e2e/tools/e2e-mem-guard.mjs --max-gb=<N> -- --target uc2 \
//     --config <engine-uc2>/tools/dev/d2/web/playwright.d2.config.js
// (the frontend sits beside engine-uc2: <WorkUC2>/ufrontend; UFRONTEND=<dir> says otherwise).
//
// With a 1920x1080 canvas the engine draws the menus (laid out on a 640x480 grid) in the centred 4:3 box
// x 240..1680, scaled 2.25 ([SDL2Drv.SDL2Client] UIScaleMode=Fit43, the web ini's default). The test maps the
// native mouse scenario's 640x480 positions into that box (as tools/dev/m10-inject.py does with M10_UIMAP) and
// checks that hovering and clicking hit the drawn items; it saves the canvas before and after the click.
// NOTE for uc2-wasm-boot.spec.js: its play test clicks at raw 640x480 canvas pixels (`at = (x, y) => [box.x + x,
// box.y + y]`), which matched the old top-left drawing; with D-2 in the web build it needs the mapping below
// (at 1024x768: scale 1.6, origin 0,0).

const fs = require('fs');
const path = require('path');
const e2e = path.join(process.env.UFRONTEND || path.join(__dirname, '..', '..', '..', '..', '..', 'ufrontend'), 'packages', 'e2e');
const { test, expect } = require(path.join(e2e, 'lib', 'fixtures'));
const { assertHardwareRenderer } = require(path.join(e2e, 'lib', 'gpu'));
const { setupTracking } = require(path.join(e2e, 'lib', 'tracking'));
const profile = require(path.join(e2e, 'lib', 'engines', 'uc2.profile'));

const BASE_URL = 'http://localhost:8080';

test.use({ viewport: { width: 1920, height: 1080 } });

test('uc2 D-2: the menus in a centred 4:3 box at 1920x1080, hover and click hit the drawn items', async ({ page }) => {
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
    console.log(`  [D2] WebGL renderer: ${renderer}`);
    await page.locator('#start-btn').waitFor({ state: 'visible', timeout: 120000 });
    await page
        .locator('.p-fieldset-legend')
        .filter({ hasText: 'Advanced' })
        .first()
        .click()
        .catch(() => {});
    await page.fill('#engine-options', '-log -glerrors -nostartupmovies', { timeout: 30000 });
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
    const glLine = t.findLogs(/UC2GLDrv: frame 600, \d+ GL errors so far/).pop();
    console.log(`  [D2] ${glLine}`);

    const steps = [];
    const step = (name, ok, detail) => {
        steps.push({ name, ok });
        console.log(`  [D2] ${ok ? 'ok  ' : 'FAIL'} ${name}${detail ? ': ' + detail : ''}`);
    };
    const exec = (c) => page.evaluate((cmd) => window.ueExec && window.ueExec(cmd), c);
    for (const ev of ['DevGUI', 'DevUI']) {
        const before = count(new RegExp(`Unsuppressed event ${ev}`));
        await exec(`UNSUPPRESS EVENT=${ev}`);
        step(`UNSUPPRESS ${ev}`, await waitMore(new RegExp(`Unsuppressed event ${ev}`), before, 10000));
    }
    await page.evaluate(() => document.activeElement && document.activeElement.blur && document.activeElement.blur());
    await page.waitForTimeout(1000);

    const box = await page.locator('#canvas').boundingBox();
    // UCanvas::ApplyUIBox: s = min(W/640, H/480), origin floor((W-640s)/2), floor((H-480s)/2).
    const s = Math.min(box.width / 640, box.height / 480);
    const bx = Math.floor((box.width - 640 * s) / 2);
    const by = Math.floor((box.height - 480 * s) / 2);
    console.log(`  [D2] canvas ${box.width}x${box.height}: UI box ${bx},${by} ${640 * s}x${480 * s} (scale ${s})`);
    step('the canvas is 1920x1080', box.width === 1920 && box.height === 1080, `${box.width}x${box.height}`);
    const at = (x, y) => [box.x + bx + x * s, box.y + by + y * s];

    const shot1 = await page.locator('#canvas').screenshot({ timeout: 30000 });
    fs.writeFileSync(path.join(dir, 'uc2-d2-menu-1920x1080.png'), shot1);

    // The native mouse scenario's positions (tools/dev/m10-controls.sh, scen_mouse): Story y 146, Options y 299,
    // Instant Action y 184, at x 140.
    await page.mouse.move(...at(140, 146));
    await page.waitForTimeout(500);
    let before = count(/Mouse hover selects UC2MainMenu/);
    await page.mouse.move(...at(140, 299), { steps: 5 });
    step('hover over OPTIONS selects it', await waitMore(/Mouse hover selects UC2MainMenu/, before, 5000),
        t.findLogs(/Mouse hover selects UC2MainMenu/).pop());
    await page.waitForTimeout(500);
    before = count(/OpenMenu \[UCMenu\.UC2Arena_Main\]/);
    await page.mouse.move(...at(140, 184));
    await page.mouse.down();
    await page.waitForTimeout(250);
    await page.mouse.up();
    step('a click on INSTANT ACTION opens the arena page', await waitMore(/OpenMenu \[UCMenu\.UC2Arena_Main\]/, before, 15000));
    step('not the hovered OPTIONS', count(/OpenMenu \[UCMenu\.UC2Settings_Main\]/) === 0);
    await waitMore(/OpenMenu \[GUI\.GUIMessageBox\]/, 0, 20000);
    await page.waitForTimeout(3000);
    const shot2 = await page.locator('#canvas').screenshot({ timeout: 30000 });
    fs.writeFileSync(path.join(dir, 'uc2-d2-arena-1920x1080.png'), shot2);
    console.log(`  [D2] pictures in ${dir}`);

    const traps = t.errors.filter((e) => /Aborted|RuntimeError|unreachable|Assertion/.test(e));
    expect(traps, 'no wasm trap').toEqual([]);
    expect(steps.filter((x) => !x.ok).map((x) => x.name), 'every step').toEqual([]);
});
