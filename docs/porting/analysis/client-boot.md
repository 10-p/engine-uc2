# Client boot: SDL2 client, launcher, menu with null render and null audio (M4)

Running notebook, appended after every step so a successor can continue. Goal (PLAN.md row M4): `uc2.bin`
boots as a client on Linux under Xvfb with `Engine.NullRenderDevice` and `Engine.NullAudioDevice`, brings
the menu level up and the menu can be driven with the keyboard.

## Commands

```sh
cd /home/ion/WorkUC2/engine-uc2
cmake --preset native && cmake --build build-native -j12      # also fetches SDL2 into build-native/_deps
tools/dev/mkrun-uc2.sh build-native                            # run tree with ucc-bin and uc2.bin
cd /home/ion/WorkUC2/run/native/System
SDL_AUDIODRIVER=dummy timeout 40 xvfb-run -a -s "-screen 0 1280x720x24" ./uc2.bin -log   # log: System/Avaris.log
```

`-log` echoes the log to the terminal (the PC build opened its log window). The log file is
`System/Avaris.log`: the launcher sets the module name to `Avaris` (as `Avaris.exe` on Windows), which also
names the ini (`Avaris.ini`, derived from `Default.ini` on first run) and the product strings
(`Localization/int/avaris.int`).

## Step 1 - the registry (commandlet short names, game-type lists): Core

Symptom (BLOCKERS.md): `UObject::GetRegistryObjects` found nothing, so `ucc server` did not resolve and
`ucc help` listed no commandlets.

Cause, found with gdb in `UObject::CacheDrivers` (`Core/Src/UnObj.cpp`): it lists the registry files with
`GFileManager->FindFiles( "Localization\\int\\*.INT" )`. The path is fine (`appUnixPath` turns `\` into `/`
and resolves case per component), but the extension comes from `GLanguageExt[LC_INT]`, which UC2 spells in
upper case (`"INT"`, `"DEU"`, ...: Xbox language codes). Windows and the Xbox match wildcards without regard
to case; `FFileManagerUnix::SearchDirectory` (taken from engine-ut2004) compared with `strstr`, so
`*.INT` matched none of the `*.int` files. `Localize()` itself was never affected: it opens files by full
name, and the M3 census logs had no "No localization" line.

Fix (`Core/Inc/FFileManagerUnix.h`): `SearchDirectory` now has `FindFirstFile` semantics: a real `*`/`?`
wildcard matcher, case-insensitive under `__FILE_NOCASE` (always set by our CMake, as on the platforms the
code was written for), `*.*` matches every name, `.` and `..` are never returned, and the `Files` /
`Directories` arguments of `FindFiles` are honoured (they were ignored; `CacheDrivers` asks for directories
only under `Localization\*` to register the languages and used to get `.`, `..` and files as "languages").
The old matcher also treated `*.ext` as "contains .ext" and `*.*` as "contains a dot"; both now follow
Windows. `FindFiles` callers in Core/Engine/IpDrv/GUI were checked: all pass a `<dir>\<wildcard>` pattern.

Result: `./ucc-bin server DM-Deadbolt` resolves the short name (`Executing Class Engine.ServerCommandlet`,
level up for play). `./ucc-bin help` lists the commandlets whose classes load (see "ucc help" below).

## Step 2 - `appItoa` / `appI64toa` off MSVC: Core

First client run stopped with `appError called: Not implemented`. `Core/Src/UnMisc.cpp` implements both with
`_itoa`/`_i64toa` under `_MSC_VER` and `appErrorf("Not implemented")` otherwise. The client reaches them in
`ULevel::SpawnPlayActor` (`URL.GetOption( "GAMEPADID=", appItoa(Actor->GamepadID) )`, `UnLevAct.cpp`); the
dedicated server never spawns a play actor, which is why M3 did not see it. Non-MSVC branch now formats with
`snprintf` into a static buffer (TCHAR is ANSI), as engine-ut2004's `UnAnsi.cpp` does on Unix.

## Step 3 - SDL2, fetched: build

Root `CMakeLists.txt`, two additions only:

- An SDL2 `FetchContent` block (native builds only), copied from engine-ut2004's: `release-2.32.x`, shallow,
  static library, no SDL tests, no SDL2main except on MinGW (it is only a `WinMain` shim), X11 on and
  Wayland off, X11 linked rather than `dlopen`ed (`SDL_X11_SHARED OFF`). For the 32-bit build on an x86_64
  host, `CMAKE_LIBRARY_PATH`/`CMAKE_PREFIX_PATH` get `/usr/lib/i386-linux-gnu`: otherwise SDL's X11 probes
  link 32-bit objects against the 64-bit libraries, every check fails silently, and `SDL_x11xinput2.h`
  then declares a fallback `XGenericEventCookie` that collides with Xlib's (engine-ut2004's finding on this
  same machine). Configure output on this machine: X11, Xcursor, XDBE, XFixes, XInput, XRandR,
  XScrnSaver, XShape all found, `/usr/lib/i386-linux-gnu/libX11.so` etc. in `EXTRA_LIBS`.
- `SDL2Drv` and `SDLLaunch` added to `UC2_NATIVE_ONLY_PACKAGES`: the web build keeps building the eight
  libraries it built at M3 and nothing else. The web client (Emscripten's `-sUSE_SDL=2` port, main loop
  yield, preload) is M4w; until then neither target exists under Emscripten.

The root `-fsigned-char -fno-strict-aliasing` and the engine `-D` definitions also reach SDL's sources (as
in engine-ut2004); SDL builds cleanly with them.

## Step 4 - `Source/SDL2Drv`: the SDL2 client and viewport

New package, about 1,200 lines: `Inc/SDL2Drv.h`, `Src/SDL2Drv.cpp` (package), `Src/SDL2Client.cpp`,
`Src/SDL2Viewport.cpp`, `CMakeLists.txt`. Classes `SDL2Drv.SDL2Client` (`USDL2Client : UClient`) and
`SDL2Drv.SDL2Viewport` (`USDL2Viewport : UViewport`); C++ only, no `.u` (like WinDrv on PC).

Sources used, and how:
- **Interface**: UC2's `Engine/Inc/UnCamera.h`. It differs from UT2004's where it matters here:
  `UClient::Tick(FLOAT)`, `GetInitScreenSize`, `UViewport::Repaint(UBOOL, FLOAT)`, `OpenWindow(DWORD...)`,
  `TryRenderDevice` returns `void`, `UpdateMousePosition` is a viewport virtual, `LastCurrent` lives in
  `UClient`, and `UClient::Init` creates the `SplitScreenMgr`, `ViewportMgr` and `LoadSaveManager`.
  engine-ut2004's driver could therefore not be copied: its signatures are UT2004's.
- **Behaviour**: Epic's `WinDrv` (`WinClient.cpp`, `WinViewport.cpp`), PC paths only (the editor branches,
  IME, debugger, DirectInput joystick are left out). What maps to what:

  | PC (WinDrv) | SDL2Drv |
  |---|---|
  | `WM_KEYDOWN`/`WM_SYSKEYDOWN` (incl. auto-repeat) -> `InputEvent(VK, IST_Press)` | `SDL_KEYDOWN` -> scancode table -> `IST_Press` (repeats too) |
  | `WM_KEYUP` -> `IST_Release`, then re-capture if the menu closed | `SDL_KEYUP`, same |
  | `WM_CHAR` -> `Engine->Key(Key, Char)`, never Enter | `SDL_TEXTINPUT` (UTF-8 -> Latin-1, code points < 256), plus Backspace/Tab/Escape on key-down, the control characters `WM_CHAR` delivers for them |
  | Alt+Enter -> `ToggleFullscreen` | same |
  | DirectInput mouse: `IK_MouseX` `+dx*MouseXMultiplier`, `IK_MouseY` `-dy*MouseYMultiplier`, wheel `IK_MouseW` (120/notch) + `IK_MouseWheelUp/Down` press+release, buttons 0..7 | SDL motion `xrel/yrel`, wheel, buttons 1..5 -> the same calls |
  | `UpdateMousePosition` (`GetCursorPos`) -> `WindowsMouseX/Y` (the GUI reads this in a window) | `SDL_GetMouseState` |
  | `SetMouseCapture`: `SetCapture`+hidden cursor+`ClipCursor` | `SDL_SetRelativeMouseMode` + `SDL_SetWindowGrab` |
  | `WM_SETFOCUS`/`WM_KILLFOCUS`: capture, `ResetInput` of GUI and `UInput`, gamma | `SDL_WINDOWEVENT_FOCUS_GAINED/LOST`, same |
  | `WM_SIZE` -> `RenDev->SetRes` / `ResizeViewport(NoWindowChange)` | `SDL_WINDOWEVENT_SIZE_CHANGED`, same |
  | `UpdateInput`: `GetKeyState` resync (release keys that are up; on Reset press keys that are down) | `SDL_GetKeyboardState` resync, same rule |
  | Windows message pump in `CMainLoop` | `USDL2Client::PumpEvents`, called from `UpdateInput` (the launcher's loop and `UViewport::ReadInput` both reach it); each event goes to the viewport whose SDL window it names |

  Shift/Ctrl/Alt map to `IK_Shift`/`IK_Ctrl`/`IK_Alt` (what `WM_KEYDOWN` reports as `VK_SHIFT`...), not to
  the left/right codes engine-ut2004 uses: UC2's bindings and `GUIController` test `IK_Shift`.
- **SDL calls and the scancode table**: engine-ut2004's `SDL2Viewport.cpp`; its Android/web/GL parts are not
  taken.
- **No rendering context.** `OpenWindow` creates a plain SDL window (no `SDL_WINDOW_OPENGL`), then
  `TryRenderDevice("ini:Engine.Engine.RenderDevice")` as `UWindowsViewport::OpenWindow` does. The null device
  (`UnNullRenderDevice.h`) needs nothing from the window: its `SetRes` only calls
  `Viewport->ResizeViewport(BLIT_Direct3D, X, Y)`. A GL device (M5) will need `SDL_WINDOW_OPENGL` at window
  creation (or the window re-created when such a device is selected); `GetWindow()` returns the
  `SDL_Window*`.
- **Gamepads**: not implemented (M10). `USDL2Client::PollGamepads` is the marked hook, called from `Tick`;
  its comment lists what the Xbox driver does (pad slots and hot-plug as `IK_JoySlider1/2`, bound pads read
  per viewport with `Engine->ActiveControllerId`, unbound pads to `InteractionMaster` only, Start on an
  unbound pad joining split screen through `UClient::BeginSplitScreen`, rumble). `SDL_INIT_GAMECONTROLLER`
  is not initialised.
- **Keyboard routing in UC2's GUI** (why keys reach the menu without any pad logic): `UGUIController::
  NativeKeyEvent` accepts input only for `Engine->ActiveControllerId` (0) and the viewport whose
  `GamepadID` equals it. `ULevel::SpawnPlayActor` sets the first viewport's `GamepadID` to 0 on every
  non-Xbox build ("For development on the PC, fake the GamepadID", `UnLevAct.cpp`), so keyboard and mouse
  are pad 0. With `[GUI.GUIController] bEmulatedJoypad=True` (Default.ini) the GUI turns arrows into the
  pad's D-pad codes, Enter into Start, Escape into Back (`UGUIController::GetXCode`).
- **Compiled defaults**: `USDL2Client::StaticConstructor` gives the inherited `UClient` settings the values
  of Default.ini's `[SDL2Drv.SDL2Client]`. Found by running with Epic's original `Default.ini` (no section
  for this client): the window opened but the render device got 0x0, so nothing would draw. Same trap
  engine-ut2004 documented.
- **Config keys** of the client: those of `[WinDrv.WindowsClient]` (`UseJoystick`, `StartupFullscreen`,
  `MouseXMultiplier`, `MouseYMultiplier`) plus `UClient`'s.

Exec commands kept from WinDrv: `GETSYSTEMLANGUAGE` (from `$LANG`), `ENDFULLSCREEN`, `TOGGLEFULLSCREEN`,
`GETCURRENTRES`, `GETCURRENTCOLORDEPTH`, `GETCOLORDEPTHS`, `GETCURRENTRENDERDEVICE`, `SETRES`, `TEMPSETRES`,
`SETMOUSE`, `LOADWINDOWPOSITION` (window position saved under `[WindowPositions]` on close, as on PC).
Not ported: `TOGGLEIME`, `SHOWDEBUGGER`/`HIDEDEBUGGER`/`BREAK` (Windows-only UnrealScript debugger).

Not done / known differences:
- Ctrl+letter does not produce the control characters `WM_CHAR` gives on Windows (Ctrl+C = 3...).
- Fullscreen uses `SDL_WINDOW_FULLSCREEN` with the configured size; untested beyond Xvfb (no display mode
  change happens under the null device anyway).
- One SDL window per viewport, as WinDrv creates one Windows window per viewport. Split screen on PC is an
  owner question (BLOCKERS.md); nothing was done for it.

## Step 5 - `Source/SDLLaunch`: `uc2.bin`

Epic's SDL 1.2 launcher (`Src/SDLLaunch.cpp`, `SDLLaunchPrivate.h`) is rewritten; the original stays in git
history (import commit 7633696) and in `epic-sources`. `Src/makefile` is left as it was (reference).
- **Structure** from engine-ut2004's launcher: globals, `CMainLoop`, `InitEngine`, `-*RENDERER` selection,
  driver normalisation, `main`. Its Emscripten, WebRTC, Android, Pixomatic and GL parts are not here yet;
  the places they go are the same functions.
- **What UC2 does at startup** from the PC `WinMain` (`Launch/Src/Launch.cpp`) and `UnEngineWin.h`
  (`InitEngine`, `CMainLoop`): `GPackage = appPackage()`, `GIsClient/GIsServer/GIsScriptable/GLazyLoad`,
  `-BENCHMARK`, the `-640x480`-style switches (written to the active client's section instead of
  `[WinDrv.WindowsClient]`), `Running.ini`, `FirstRun`, `UEngine::GEngine = Engine` before `Engine->Init()`
  (UC2-specific, as the PC and Xbox launchers do), `EXEC=`, `LOADWINDOWPOSITION`, `GRenDev->Flush` before
  exit. Left out: config wizard, log window, installer mutex, the D3D hardware probe of
  `SetInitialConfiguration`, the exec hook with property windows; `consolecommand=` logs that it is not
  available. Benchmark CSV output is reduced to one average line.
- **Static registration**: exactly `UCC.cpp`'s (the XboxLaunch order) plus `AUTO_INITIALIZE_REGISTRANTS_SDL2DRV`,
  including UWeb's two native lookups, and the POSIX synchronisation/thread factories set before `appInit`.
- **Module name** `Avaris` (`GModule`), so the ini, the log and the product strings are the PC game's
  (`Avaris.ini`, `Avaris.log`, `avaris.int`).
- **Message pump**: `Engine->Client->Viewports(0)->UpdateInput(0,0)` once per frame (engine-ut2004's
  arrangement), which pumps SDL events through the client.
- **Driver choice**: `-NULLRENDERER` sets `[Engine.Engine] RenderDevice=Engine.NullRenderDevice`
  (engine-ut2004's `SelectRenderDevice`, table cut to the one device we have). `ReplaceMissingDrivers`
  replaces a `ViewportManager`, `RenderDevice` or `AudioDevice` that the ini names but this binary does not
  contain, and logs it; tested with Epic's original `Default.ini` as `Avaris.ini`:
  `ViewportManager=WinDrv.WindowsClient is not in this build: using SDL2Drv.SDL2Client`,
  `RenderDevice=D3DDrv.D3DRenderDevice is not in this build: using Engine.NullRenderDevice`, menu up.
- **Link**: `CMakeLists.txt` as for `ucc-bin`: whole-archive of every package (SDL2Drv first), SDL2 static,
  `-Wl,-export-dynamic`. Output `build-native/Source/SDLLaunch/uc2.bin`, which `mkrun-uc2.sh` already copies.

Configuration (`Engine/Config/Default.ini`, rows in `Engine/Config/README.md`): `ViewportManager` ->
`SDL2Drv.SDL2Client`, `RenderDevice` -> `Engine.NullRenderDevice` (Epic's lines kept as comments), new
section `[SDL2Drv.SDL2Client]` = copy of `[WinDrv.WindowsClient]`. `AudioDevice` was already
`Engine.NullAudioDevice`. The file keeps its CRLF line endings (it is a byte copy of Epic's).

## Step 6 - first boots, and what running found

Run as in "Commands". Defects found by running, in order (each also described in its step above):

1. `appError: Not implemented` right after `Input system initialized for SDL2Viewport`: `appItoa` off MSVC
   (step 2).
2. With Epic's own `Default.ini` as `Avaris.ini` (no `[SDL2Drv.SDL2Client]` section) the viewport got a
   0x0 render size: `UClient`'s config properties have no compiled values (step 4, compiled defaults).
3. Not a defect, but it hides the evidence: `Default.ini` suppresses the `DevUI` and `DevGUI` log names
   (`[Core.System] Suppress=DevUI`), so the GUI's own lines (`OpenMenu [...]`, `... handled XC_PadDown for
   id 0`) are not in the log by default. For the gate runs below a test-only `Avaris.ini` without those two
   lines was written into the run tree; nothing committed changes. At run time `UNSUPPRESS EVENT=DevUI`
   (console) does the same.

Everything else ran first time: the menu level loads, `UCMenuGame.PostLogin` opens the main menu, the main
loop ticks (gdb at 15 s: `CMainLoop::RunLoop -> UGameEngine::Tick -> USDL2Client::Tick ->
USDL2Viewport::Repaint -> UGameEngine::Draw -> FPlayerSceneNode::Render`, i.e. the null device is being
drawn to every frame), and SIGTERM gives a clean shutdown (`appRequestExit(0)`, `Closed viewport`,
`SDL2 client shut down`, `Object subsystem successfully closed`).

Which menu: `GUI.GUIController OpenMenu [UCMenu.UC2MainMenu]`. `UCMenuGame.PostLogin` opens
`class'GameEngine'.default.MainMenuClass`, which is `UCMenu.UC2MainMenu` in `GameEngine.uc`'s defaults; no ini
overrides it (`[Engine.GameEngine]` has no `MainMenuClass`). `UCMenu.StartMenu` (the Xbox "press Start"
title) is not opened on this path; `GUIController` only refers to it in `ShowingTeaser`-related code.
Nothing was changed there.

### Input injection

`xdotool` and `python3-xlib` are not installed. Keys and clicks were injected through the X server's XTEST
extension from Python `ctypes` (`libXtst.so.6`, `libX11.so.6`, both present), in a test-only script in the
agent's scratch directory (not committed): find the window titled `Avaris`, `XSetInputFocus` on it (what a
window manager would do; Xvfb has none), then `XTestFakeKeyEvent` press/release pairs 1.65 s apart, or
`XTestFakeMotionEvent` + `XTestFakeButtonEvent` for clicks. This drives the real path: X server -> SDL's X11
backend -> `SDL_KEYDOWN` -> `USDL2Client::PumpEvents` -> `USDL2Viewport::HandleEvent` ->
`UEngine::InputEvent` -> `UInteractionMaster::MasterProcessKeyEvent` -> `UGUIController::NativeKeyEvent`.

Mouse, same way: clicking at screen (480,300) on the main menu (window at (320,120), 640x480) logged
`GUI.GUIController OpenMenu [UCMenu.UC2Arena_Main]`, the same page Enter on the first item opens.

### `ucc help` (open, cosmetic)

`./ucc-bin help` now prints one line per registered commandlet whose class loads: four lines with an empty
name and description, `ucc help <command>` and `ucc playerskill  Calculate player skill.` (the Editor-package
commandlets with natives do not load: Editor is not built). The empty lines are native commandlet classes
(`Engine.ServerCommandlet` is one, checked in gdb); their `HelpCmd`/`HelpOneLiner` are localized properties
and are empty on the class default object that `UCC.cpp` reads. gdb showed `LoadLocalizedStruct(...,
"ServerCommandlet")` reached only once, at static class registration (before `Core.u` provides the
`UCommandlet` properties), and not again when `Engine.u` is loaded. Not followed further: it is Epic's UCC
flow (packages are loaded under `GIsEditor=1`, and `UObject::LoadLocalized` returns at once under
`GIsEditor`), not a platform issue, and the commandlets themselves run (`ucc server`). Whether the same
holds for script reading `class'X'.default.<localized>` on a native class in the game is worth checking
when menus are visible (M5).

## Gate evidence (2026-10-04)

a. `cmake --build build-native -j12` -> `[100%] Linking CXX executable uc2.bin`, `[100%] Built target
   SDLLaunch`, no undefined reference, no multiple definition. Also from an empty build directory
   (`cmake --preset native -B <scratch>/bn && cmake --build <scratch>/bn -j12`, SDL2 fetched afresh).
b. `cd /home/ion/WorkUC2/run/native/System && SDL_AUDIODRIVER=dummy timeout 45 xvfb-run -a -s "-screen 0
   1280x720x24" ./uc2.bin -log` with the committed configuration -> exit 124 (still running when the 45 s
   timeout stopped it), `System/Avaris.log`:
   `Init: SDL2 video driver: x11`, `Init: Client initialized`,
   `Log: Bringing Level Entry.MyLevel up for play (0)...`, `Log: Game class is 'UCMenuGame'`,
   `Log: Bringing Level Menu.MyLevel up for play (0)...`, `Log: Opened viewport (SDL window 1, 640x480)`,
   `Init: Render device: Engine.NullRenderDevice (640x480 windowed)`, `Log: Startup time: 0.641201 seconds`,
   then `Log: appRequestExit(0)` at the signal; 0 `Critical:` lines. Menu class (DevUI visible, see step 6):
   `DevUI: GUI.GUIController OpenMenu [UCMenu.UC2MainMenu] () ()`.
c. Same command line, keys injected 30 s after the window appeared (Down, Down, Up, Return, Escape at
   32.5/34.2/35.8/37.5/39.1 s):
   ```
   DevUI: UC2MainMenu->ItemSelectionPanel handled XC_PadDown for id 0
   DevUI: UC2MainMenu->ItemSelectionPanel handled XC_PadDown for id 0
   DevUI: UC2MainMenu->ItemSelectionPanel handled XC_PadUp for id 0
   DevUI: UC2MainMenu->ItemSelectionPanel->GUILabelButton handled XC_Start for id 0
   DevUI: GUI.GUIController OpenMenu [UCMenu.UC2Arena_Main] () ()
   DevUI: GUIController::BringMenuToTop NewTopMenu:'Package.UC2Arena_Main' bResult:'True'
   DevUI: GUI.GUIController OpenMenu [GUI.GUIMessageBox] () ()
   DevUI: GUI.GUIController CloseMenu [GUIMessageBox]
   DevUI: GUIMessageBox handled XC_Back for id 0
   ```
   (the `handled ...` lines are `SHOWCODEHANDLER`, `GUI/Src/GUIPrivate.h`, logged as `DevUI`). This also
   shows the process alive and ticking at 30-40 s.
d. `./ucc-bin server DM-Deadbolt -log` -> `Executing Class Engine.ServerCommandlet`, `Game class is
   'UCDeathmatch'`, `Bringing Level DM-Deadbolt.MyLevel up for play (0)...`, running at the 30 s timeout.
   `./ucc-bin help` -> see above. `./ucc-bin verifyclasses` -> `VERIFY: all native class layouts match`.
   `tools/dev/map-census.sh build-native 15 4` -> `maps: 58  PASS: 58  FAIL: 0`.
e. `source /home/ion/WorkUC2/env.sh && cmake --preset wasm && cmake --build build-wasm -j6` -> the eight
   libraries (`Built target` Core, Engine, UnrealGame, UCElements, UWeb, IpDrv, UCGameplay, GUI), exit 0.
   SDL2Drv and SDLLaunch are excluded under Emscripten (native-only list) until M4w.

## Deferred, and why

- Pads: M10 (hook `USDL2Client::PollGamepads`). GL render devices and `SDL_WINDOW_OPENGL`: M5. OpenAL: M8.
  Movies and the startup logos: M9. Web client: M4w.
- Split screen on PC (one SDL window per viewport, as WinDrv): owner question, unchanged.
- Ctrl+letter control characters, IME, the UnrealScript debugger, `consolecommand=` (needs an exec hook).
- `ucc help` empty descriptions (above).
