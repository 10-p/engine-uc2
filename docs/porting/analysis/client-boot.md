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
