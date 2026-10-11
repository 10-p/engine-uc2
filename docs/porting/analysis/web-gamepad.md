# Game pads in the browser (G-5)

Running notebook of the engine side of task G-5 (PLAN.md "Owner review of 2026-10-10"). Worktree
`/home/ion/WorkUC2/engine-uc2-g5-gamepad`, branch `g5-gamepad`. The frontend side and the full step-0 reading (the
SDL2 port, Emscripten's proxying, the browser's rules, the sibling engines) are in the frontend worktree's notebook
`/home/ion/WorkUC2/ufrontend-gamepad/docs/migration/notes/gamepad.md` (branch `uc2-gamepad`). Logs:
`/home/ion/WorkUC2/logs/g5/`. Owner decision during the task: UC2 only for now; the frontend side generic.

## Step 0 - what the engine needs on the web (2026-10-10, reading only)

- Nothing in code. `Source/SDL2Drv/Src/SDL2Pads.cpp` uses SDL's GameController API, which Emscripten's SDL2 port
  (2.32.10) serves from the browser's Gamepad API: every joystick is a game controller with the W3C "standard"
  mapping (`SDL_gamecontrollerdb.h:988-990`), hot-plug arrives as `SDL_CONTROLLERDEVICEADDED/REMOVED` from the
  browser's `gamepadconnected/disconnected` (registered by SDL on the page, posted back to the engine's pthread by
  Emscripten's html5 library, `libhtml5.js:221-233, 1935-1980`), the state is sampled per `SDL_PumpEvents` through
  two synchronous proxied calls per open pad.
- Only `[SDL2Drv.SDL2Client] UseJoystick=False` in `Engine/Config/web/Default.ini` (DELTAS of
  `tools/dev/sync-web-ini.py`, "untested") keeps pads off on the web.
- What the web cannot give through SDL (Emscripten's `SDL_sysjoystick.c`): rumble (`EMSCRIPTEN_JoystickRumble`
  returns `SDL_Unsupported()`, so `SetPadRumble`'s `SDL_GameControllerRumble` returns -1; the rest of the rumble code
  runs and costs nothing), analog triggers (b6/b7 are buttons in the standard mapping: 0 or 32767, i.e. 0 or 255 for
  `PadButtonThreshold` and `GetAnalogButtonAmount`; the browser's own `pressed` threshold applies instead of the
  Xbox's 30/255), the input test channel's virtual pad (`SDL_JOYSTICK_VIRTUAL` is not compiled in the port).
- Rumble through the browser would need engine code of our own (the Gamepad API's `vibrationActuator.playEffect(
  "dual-rumble", ...)` called on the page, e.g. a `MAIN_THREAD_EM_ASM` from `SetPadRumble` under `__EMSCRIPTEN__`):
  not done, an owner question (Chrome and Edge have it, Firefox and Safari did not at the time of writing).
  G-10 (owner, 2026-10-10: "Web rumble now") did it: `SDL2Pads.cpp` under `__EMSCRIPTEN__`, `web-g10.md` step 3.

## Plan

1. Prove with the frontend's harness (`uc2-wasm-gamepad.spec.js`, a scripted fake pad) on the main artifact, pads
   enabled by the launcher's first-boot seed of `UseJoystick=True`.
2. If clean: drop the `UseJoystick` delta from `sync-web-ini.py` (the web file then has the native `True`),
   `Engine/Config/README.md`, rebuild `wasm-perf` here (only the preloaded ini changes), re-run the spec on it.

## G-11 (2026-10-10): the pads' player slots, the same on the web

The owner's final rule (splitscreen.md G-11): `[SDL2Drv.SDL2Client] PadsStartAtPlayerTwo` (G-9's `KeyboardIsPlayer`
renamed) is False natively and on the web; `tools/dev/sync-web-ini.py` has no delta for it any more, and the launcher's
Advanced "Force gamepads to start with second player" (frontend target data `gamepadPorts`) sets it per player under
THE RULE. Player one's pad pulled out opens no "controller removed" page on the web either (the same `PadRemoved`). The
launcher's "press any button" hint and its connect/disconnect notices were removed (frontend notebook, G-11): SDL's
Emscripten driver gets `gamepadconnected` on the engine thread by itself. Chrome lists a pad only after a button press
on it; UC2's own "press Start" for player two makes that happen naturally.

## H-3 (2026-10-11): the first press of a pad

The press that makes the browser list a pad is the pad's starting state for SDL's Emscripten driver (never an event):
`SDL2Pads.cpp` `PadAdded` reads the browser's pressed buttons once and the first `ReadPad` takes them as a press. Without
it the owner's Start on the character page was lost. `h3-pads.md`.
