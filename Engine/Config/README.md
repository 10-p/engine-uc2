# Configuration

`Default.ini`, `DefUser.ini`, `DefaultXbox.ini`, `DefUserXbox.ini` and `KeyBinds.ini` are Epic's `System/` files of the
PC tree; `Default.ini` and `DefUser.ini` carry our defaults, the other three stay Epic's bytes (`DefaultXbox.ini` and
`DefUserXbox.ini` are reference copies nothing reads; `KeyBinds.ini` holds the pad layouts `UC2Settings_Controller`
offers). `DefUnrealEd.ini` is the editor's (below). `tools/dev/mkrun-uc2.sh` copies them over the game folder's when it
assembles a run tree; the web and Android
builds carry generated variants of `Default.ini` and `DefUser.ini` (`web/`, `android/`, below). `Default.ini`,
`DefUser.ini` and `web/*.ini` are LF; the three Epic copies keep Epic's CRLF (`tools/dev/sync-web-ini.py --check`
compares them with the Epic drop). The engine reads both endings. Against Epic's files:
`git diff --ignore-cr-at-eol 9fc7bcf -- Engine/Config`.

Every value below is also the compiled default unless the row says otherwise, so an ini without the key behaves the
same.

## `Default.ini`

| Setting | Epic | Ours | Why |
|---|---|---|---|
| `[URL] Port` | `1000` | `7777` | 1000 is the Xbox system-link port and privileged on Linux; 7777 is Unreal Engine 2's default. |
| `[URL] EXEName` | `avaris.exe` | `uc2.exe` | The game program the editor's Play Level starts (only the editor reads it); this build's Windows game is `uc2.exe`. |
| `[Engine.Engine] ViewportManager` | `WinDrv.WindowsClient` | `SDL2Drv.SDL2Client` | The portable client; WinDrv is Win32-only and not built (Epic's line stays as a comment). |
| `[Engine.Engine] RenderDevice` | `D3DDrv.D3DRenderDevice` | `UC2GLDrv.UC2GLRenderDevice` | The OpenGL device; D3DDrv is not built. `Engine.NullRenderDevice` stays selectable (commented line, `-nullrenderer`). |
| `[Engine.Engine] AudioDevice` | `Engine.NullAudioDevice` | `UC2ALAudio.UC2ALAudioSubsystem` | The OpenAL Soft device on the cooked sound data. Without libopenal, an output device or the data the game runs silent and says so once; `-nosound` selects the null device for one run. |
| `[Engine.Engine] FrameRateCap` | (none) | `0` (uncapped) | A frame-rate cap for a game without a tick rate of its own (standalone, listen server); a network client keeps the engine's cap. The frontend's launcher writes it with `-ini:`. |
| `[Engine.Engine] ScriptSaveConfig` | (none: `SaveConfig()` / `StaticSaveConfig()` compiled to no-ops on PC and Xbox) | `True` | Script's `SaveConfig()` writes the object's config properties to its ini (a globalconfig property another class sets is left out and logged); `False` is Epic's no-op. |
| `[Engine.Engine] OverlayCameraClass` | (none; `DefaultXbox.ini` has it) | `UCGameplay.UCOverlayCamera` | The Xbox's value: the overlay camera of the split-screen views. |
| `[Core.System] Suppress` | Epic's list | `+KeyGlyphs` | The key glyphs' log lines stay out of the log unless asked for. |
| `[Engine.GameEngine] FpsLogSeconds` | (none) | `0` (off) | `>0` logs one `FPS:` line every that many seconds (frames, fps, frame time avg/min/max/p99, 1% low); console `FPSLOG <s>`. |
| `[Engine.GameReplicationInfo] ServerName` | `Another Unreal Server` | `Liandri Corporation` | The Xbox's session name (the profile default), also for a host without a profile. |
| new section `[UC2ALAudio.UC2ALAudioSubsystem]` | (none) | `OutputDevice=`, `MaxVoices=64`, `ZoneReverb=True`, `UseEFX=True`, `MusicFades=True`, `CueLog=True`, `UseHRTF=False` | The audio device's settings. `Backends` (not written) is OpenAL Soft's backend order, compiled `pipewire,pulse,alsa,oss`, passed as `ALSOFT_DRIVERS` unless the environment sets it; `WaveFile=<file>` renders to a WAV through OpenAL Soft's wave writer (tests). `ZoneReverb` and `MusicFades` differ from the Xbox code (zone reverb was compiled out, fades ignored): `False` gives its behaviour. `CueLog` writes a `DevSound` line per cue (DevSound stays suppressed). `UseHRTF` asks OpenAL Soft for headphone virtualisation (its embedded data set), the key and default engine-ut2004 uses. |
| new section `[SDL2Drv.SDL2Client]` | (none) | a copy of `[WinDrv.WindowsClient]`, plus the keys below | The client reads its own class section: 640x480 windowed, gamma, detail. |
| `[SDL2Drv.SDL2Client] BorderlessFullscreen` | (none: WinDrv's exclusive fullscreen) | `True` | Native desktop: fullscreen (Alt+Enter, `TOGGLEFULLSCREEN`, `StartupFullscreen`) is SDL's desktop fullscreen, the window over its display at the display's own resolution and the game at that size (`SDL2Viewport: borderless fullscreen at <w>x<h>`); `FullscreenViewportX/Y` are left as they are, a fullscreen `SETRES` is not applied (`SETRES <w>x<h> not applied: borderless fullscreen stays at <w>x<h>`) and a windowed one at the borderless size leaves fullscreen as Alt+Enter does. `False`: exclusive fullscreen, the display changing to the mode nearest `FullscreenViewportX/Y`. The web and Android do not read it. |
| `[SDL2Drv.SDL2Client] UseJoystick` | (WinDrv: `False`) | `True` | Game pads through SDL's GameController API; read every tick, so `set ini:Engine.Engine.ViewportManager UseJoystick False` turns them off at once. |
| `[SDL2Drv.SDL2Client] PadButtonThreshold`, `PadStickDeadZone`, `PadRepeatDelay`, `PadRepeatRate`, `PadRumble`, `PadRumbleScale`, `JoyDeadZone`, `LogInput` | (none) | `30`, `0`, `0.2`, `0.1`, `True`, `1`, `0.35`, `False` | What the Xbox driver hard-codes, with the Xbox's values (`XINPUT_GAMEPAD_MAX_CROSSTALK` 30/255; the bindings carry the dead zone; key repeat 0.2/0.1 s). `LogInput` writes the `SDL2Input` lines. |
| `[SDL2Drv.SDL2Client] MaxDistortion` | (none; `DefaultXbox.ini` has `35.0`) | `35.0` | The largest screen-space displacement of a distortion material, in pixels of a 640x480 frame; without it distortion displaces nothing. |
| `[SDL2Drv.SDL2Client] UIScaleMode`, `UIMenuBackground`, `UISharpText` | (none: the UI drawn 1:1 from the top-left) | `Fit43`, `Fill`, `True` | `Fit43` draws the 640x480 menus and HUD in the largest centred 4:3 box of the window, the 3D world and the console's typing line on the whole window (with the script set wide-hud the HUD's left and right groups at the window's sides); `Native640` draws them 1:1 from the top-left. `UIMenuBackground=Fill` fills the bars beside the box with the menu background; `UISharpText` keeps text sharp when the box is scaled. |
| `[SDL2Drv.SDL2Client] UseMouse`, `TouchPad`, `RenderScale` | (none) | `True`, `True`, `1.0` | `UseMouse=False` leaves the mouse out: no events, no capture, no cursor (compiled default `False` on Android). `TouchPad` (Android): the on-screen pad, a virtual SDL controller fed by the app's overlay. `RenderScale` (Android, 0.1 to 4): the frame's size against the screen's, scaled to the whole screen by the present pass (above 1: supersampling; each side at most 8192). The launcher passes its own value with `-ini:`. |
| `[SDL2Drv.SDL2Client] SplitScreenJoinInMatch`, `SplitScreenFullDetail`, `PadsStartAtPlayerTwo` | (none) | `True`, `False`, `False` | Start on an unbound pad joins a match in progress (standalone or listen); `SplitScreenFullDetail=False` keeps the Xbox's detail reductions per split-screen player (`SplitScreenMgr.uc` `SSDetailLevels`), `True` gives every player the single player's detail; `PadsStartAtPlayerTwo=True` leaves player one to the keyboard and mouse and puts the first pad on player two. |
| new section `[SDL2Drv.PadMap]` | (none: hard-coded in the Xbox driver) | SDL control = `Joy1`..`Joy16` / `JoyX` `JoyY` `JoyU` `JoyV` [`Invert`] | The SDL controller to Xbox control table (Black = right shoulder, White = left shoulder); the same table is compiled in for controls the section does not name. |
| new section `[UC2GLDrv.UC2GLRenderDevice]` | (none) | the device's settings; where Epic's `[D3DDrv.D3DRenderDevice]` has the setting, its value, plus the keys below | The device reads its own class section. |
| `LevelOfAnisotropy`, `NumAASamples` (both device sections) | `LevelOfAnisotropy=1` (D3DDrv), no anti-aliasing | `16`, `4` | Anisotropy on every stage's minifying filter, clamped to the hardware's maximum. `NumAASamples` is the sample count of the multisampled main framebuffer (0 = off; desktop GL and OpenGL ES 3 / WebGL 2; none on ES 2 / WebGL 1). An ini with the older `AntiAliasing` key and no `NumAASamples` has it read once. |
| `MaterialStages`, `FixedFunctionPrograms` (both) | (none: Epic's D3DDrv clamps the stages to 4 and refuses cards with fewer) | `4`, `True` | The stage count the material compiler splits passes for, as Epic's compiler always ran (a pass is drawn with every stage it has, up to `MaxTextureUnits`: Epic's two-layer terrain pass has five); the fixed-function stages drawn by generated GLSL programs with D3D8 semantics (`False`: the GL texture environment, with combine3/combine4). |
| `UsePixelShaders`, `PrecompileVertexShaders` (both) | (none) | `True`, `True` | Pixel-shader materials drawn with GLSL translated from Epic's Xbox shaders (`False`: their first texture stage); the vertex shaders of `VertexShaderConfigs.ini` compiled at start. Console `PIXELSHADERS [0\|1]`. |
| `UsePostProcess`, `DOFShippedBlurAlpha` (both) | (none) | `True`, `True` | What the Xbox does with render targets and Epic's PC code left out: distortion, emissive glow and bloom, depth of field, the blur and colour-remap camera effects, dynamic gamma (needs `UsePixelShaders`). The device then reports `XboxPostProcess`. Console `POSTPROCESS [0\|1]`. `DOFShippedBlurAlpha`: the always-on depth of field uses the blur alpha the shipped binary reads (65 in every camera mode) instead of script's (100 in third person); console `DOFSHIPPEDALPHA [0\|1]`. |
| `DistortionCullDistance` (both) | (none: a constant 5000 in `D3DMaterialState.cpp`) | `15000` | The distance over which screen-space distortion fades out (`1 - saturate(z/D)^2`); `5000` is the Xbox's. |
| `UseXboxShadows`, `UseXboxProjectors`, `UseTerrain3Layer` (both) | (none) | `True`, `True`, `True` | The Xbox's projected shadow textures (one `PS_ShadowBlur` tile; needs post-processing), its batching of level-static projectors, and its three-layer terrain pass; `False` gives Epic's PC path. Consoles `XBOXSHADOWS`, `XBOXPROJECTORS`, `TERRAIN3LAYER`. |
| `PostProcessReferenceHeight`, `PostProcessTargetScale` (both) | (none: the blurs sized for the Xbox's 640x480) | `480`, `2` | The bloom, glow and depth of field work on a picture of this many lines, so they cover the same share of the picture at every resolution (`0`: back-buffer pixels); their targets have twice the Xbox's texel count per axis (`1` = the Xbox's, `4` accepted). Console `PPREFERENCE`. |
| new section `[UC2GL4ESDrv.UC2GL4ESRenderDevice]` | (none) | the values of `[UC2GLDrv.UC2GLRenderDevice]`, plus `GLESVersion=3` | The same driver behind GL4ES reads its own section: the web renderer, and on Linux the `-gl4es` device. `GLESVersion=3` asks for OpenGL ES 3.0 / WebGL 2 and falls back to `2` (ES 2 / WebGL 1) where that context cannot be had. |
| `[Engine.Console] ConsoleKey` | `9` (Tab) | `192` (the key left of 1, whatever the layout) | On Tab the console took the scoreboard's key in a match and Start's in the menus. A derived `Avaris.ini` with `ConsoleKey=9` is moved to 192 for the run. |
| new section `[ufront.AutoFOV]` | (none: the horizontal FOV fixed, the vertical one shrinking on wide windows) | `AutoFOV=2`, `ReferenceAspect=1.333333`, `WeaponScale=1.0`, `SplitClampToFullScreen=True` | Hor+ on windows wider than 4:3: the vertical FOV stays what UC2 shows at 4:3 (90 becomes 106.26 at 16:9). The section and keys the sibling engines share, so the frontend's launcher sets them alike. `SplitClampToFullScreen`: a split-screen view wider than the window gets the full window's FOV, not a wider one. Console `AUTOFOV`. |
| `[GUI.GUIController] bHideMouseCursor` | `True` | `False` | The GUI draws its own cursor in fullscreen only when this is false. |
| `[GUI.GUIController] bUnlockAll` | (none) | `False` | `True` (the launcher's "Unlock all content") sets Epic's menu flags in memory after the menus start: every character, bot, skin, mutator, ladder and cinematic. Never saved to the profile. |
| new section `[GUI.EmulatedJoypad]` | (none: letters hard-wired in `UnGUICore.cpp`) | engine key = pad code [`/` pad code on a page with a text field] | The menus' keyboard emulation of the pad as data: arrows/WASD = D-pad, Enter/Space = A, Escape/Backspace/right mouse = B, Tab = Start, F1 = Back and the rest of the table. Without the section Epic's table applies. |
| new section `[GUI.KeyGlyphs] ShowKeys` | (none) | `True` | While player one uses the keyboard or the mouse, the pad's glyphs show the keys that do the same (needs the key-glyphs script set). |

`uc2.bin` replaces, for the run and in the derived `Avaris.ini`, a driver the ini names that this build lacks
(`WinDrv.WindowsClient`, `D3DDrv.D3DRenderDevice`, `XboxDrv.XboxClient`, ...) with `SDL2Drv.SDL2Client`,
`Engine.NullRenderDevice` or `Engine.NullAudioDevice`, and logs it, so an ini of the PC or Xbox release still boots.

## `DefUser.ini`

| Setting | Epic | Ours | Why |
|---|---|---|---|
| `[Engine.Input] RightMouse` | `AltFire` | `AltFire \| Shield 1 \| onrelease Shield 0` | The pad's left-trigger command: block and reflect. |
| `[Engine.Input] MiddleMouse` | `Zoom` | `Lock 1 \| onrelease Lock 0` | Lock-on (the pad's right-stick click); `Zoom` does nothing in UC2. |
| `[Engine.Input] E`, `Q`, `C`, `V`, `Ctrl`, `Tab` | empty, `AllAmmo`, empty, `CallForHelp`, `Jump`, `Type` | melee (pad B), back to ranged (pad Y), view 1P/3P (pad Black), taunt/voice (pad White), `SmartJump 1`, `ShowScores` | The pad's actions on keys around WASD; `Q=AllAmmo` was a cheat beside the movement keys. |
| `[Engine.Input] PageUp`, `PageDown` | empty, `LookDown` | `Axis aMouseY Speed=+4.0` / `-4.0` | Look up and down through the look stick's variables, with the pad's curve, sensitivity and invert. |
| `[Engine.Input] Home`, `End`, `P`, `O`, `U`, `I`, `M`, `NumPad0`..`NumPad6` | camera and editor debug commands | `CenterView`; empty | Development bindings a player could hit by accident (P froze the world). |
| `[Engine.Input] MouseWheelUp`, `MouseWheelDown` | `NextSkill`, `PrevSkill` | `NextWeapon`, `NextWeapon` | No script has `NextSkill`/`PrevSkill`; UC2 has `NextWeapon` only, so both directions cycle the ranged weapons, like the pad's Y. |
| `[Engine.Input] Joy1`..`Joy16`, `JoyU`, `JoyV`, `JoySlider1`, `JoySlider2` | (none on PC) | `KeyBinds.ini` Layout_0, D-pad in the Xbox's order (Joy11 left, Joy12 right) | A pad works before a profile applies its layout. |
| `[Engine.Input] JoyX`, `JoyY`, `JoyU`, `JoyV` | (none on PC) | the Xbox's: `Axis aStrafe/aForward/aMouseX/aMouseY SpeedBase=100.0 DeadZone=0.3` (U/V `\| Count bXAxis/bYAxis`) | The engine scales the stick's walk on PC as the Xbox's PlayerMove does, so the Xbox binding works, also after a profile's swap-sticks rewrite. |
| `[UCGameplay.UCPlayerInput] aTurn_FrictionRange`, `aLookUp_FrictionRange` | `(Min=1.0,Max=0.75)`, `(Min=1.0,Max=0.65)` | `(Min=1.0,Max=0.95)`, `(Min=1.0,Max=0.9)` | The retail disc's (Xbox) values: the section is the pad's, the mouse has its own channel. |
| `[UCGameplay.UCPlayerController] LockSearchTimer` | `0.1` | `0.3` | The retail disc's pause between lock-target switches. |
| new keys `[UCGameplay.UCPlayerInput] MouseLookSensitivity`, `bInvertMouseLook`, `bLazyMouseCamera`, `bDoubleTapDodge` | (none) | `5`, `False`, `False`, `False` | The mouse-look patch set's settings (the shipped package ignores them): linear mouse turn (0.022 degrees per count x sensitivity at FOV 90), its own invert, Epic's lazy third-person camera under the mouse when `True`, UT2004-style double-tap dodge. |

## The editor's inis (`UnrealEd.exe`, BUILD.md "The editor")

| File or setting | Epic | Ours | Why |
|---|---|---|---|
| `DefUnrealEd.ini` (the viewport layout and the browsers; `UnrealEd.ini` is made from it on the first start) | the game folder's, every `Device=D3DDrv.D3DRenderDevice` | the same file (LF) with `Device=UC2WGLDrv.UC2WGLRenderDevice` | The editor's device; D3DDrv is not built. |
| the editor's system ini | `Avaris.ini`, the game's | `UC2Editor.ini`, made from `Default.ini` on the first start | `Default.ini` names the game's SDL client and device. The editor sets `[Engine.Engine] ViewportManager=WinDrv.WindowsClient` and `RenderDevice=UC2WGLDrv.UC2WGLRenderDevice` (an `Engine.NullRenderDevice` stays) in its own file, so the game's ini is untouched. `INI=<file>` on the command line names another, which then gets these two drivers. |
| `[UC2WGLDrv.UC2WGLRenderDevice]` (read from no shipped ini) | (none) | the keys of `[UC2GLDrv.UC2GLRenderDevice]`, with the compiled defaults `UsePostProcess=False`, `NumAASamples=0`, plus `GDIPresent` | The editor's viewports show the level as PC's D3DDrv drew it, without the Xbox post-processing, and one context draws every viewport window. `GDIPresent=True` draws every viewport in framebuffer objects and copies each finished frame into its window with GDI instead of `SwapBuffers` (the present pass's gamma left out, as D3DDrv's gamma ramp is in a window); its compiled default is `True` under wine, where `SwapBuffers` on the viewport windows leaves parts of them unpainted, and `False` on Windows. |

## Read from no shipped ini

| Setting | Default | What |
|---|---|---|
| `[UC2Movie] StartupMovies`, `Cinematics`, `LoadingMovie` | `True` | Switches for the start-up logos, the cinematics and the loading movie; set with `-ini:System:[UC2Movie]:StartupMovies=False`. The logos never play when the command line joins a server. |
| `[Engine.GameEngine] AllowNetCheats` | `False` | Lets a network client and a dedicated server run `WHEREAMI`, `TELEPORT`, `NETPROBE`, `NETPLACE` (a standalone game and a listen host always may). For tests only: never in a shipped ini. Every use logs a `Cheat:` line ([`docs/CHEAT-COMMANDS.md`](../../docs/CHEAT-COMMANDS.md)). |
| test keys: `[UC2GLDrv.UC2GLRenderDevice]` `ShotTimes`, `ShotRamp`, `ShotPresented`, `Exec`, `PixelShaderToggleTimes`, `PostProcessToggleTimes`, `SelfTest`, `CombinerTest`, `MatCensus`, `Stats`, `NoLODBias`, `MaskCaps`, `DualDiffuse`, `MaterialTime`; `[SDL2Drv.SDL2Client] InputTestFile`; `[UC2Movie] ShotTimes`; `[UC2NetTest] CommandFile`, `Drop`, `PacketIdStart`, `OldAcks`; `[SDLLaunch] FramePattern` | (none) | Set by the test tools with `-ini:`, read once and never written back (`appReadTestKey`). The GL4ES device reads its own section first, then the desktop device's. |

## `web/`: the browser build's boot inis

`web/Default.ini` and `web/DefUser.ini` are preloaded into the web build as `/System/Default.ini` and
`/System/DefUser.ini` (`Source/SDLLaunch/cmake/Web.cmake`); `appInit` derives `Avaris.ini` and `User.ini` from them, so a
host must not serve the game folder's copies of these four names. They are generated: `tools/dev/sync-web-ini.py`
writes them as the native files with the differences below and nothing else (`--check` fails when they are not that).
A new web-only difference is an entry in the script's `DELTAS` (a fixed value) or `WEB_KEYS` / `WEB_SECTIONS` (kept as
the web file has it) and a row here.

| File | Setting | Native | Web | Why |
|---|---|---|---|---|
| `Default.ini` | `[Engine.Engine] RenderDevice` | `UC2GLDrv.UC2GLRenderDevice` | `UC2GL4ESDrv.UC2GL4ESRenderDevice` | The same driver behind GL4ES on WebGL 2 (WebGL 1 as the fallback). |
| `Default.ini` | `[Engine.Engine] NetworkDevice` | `IpDrv.TcpNetDriver` | `WebRTCDrv.WebRTCNetDriver` | A browser has no sockets: the WebRTC driver sends the engine's packets over the frontend's data channels. `uc2.bin` on the web also switches a saved `Avaris.ini` that names the TCP driver. |
| `Default.ini` | `NumAASamples` (both device sections) | `4` | `0` | Anti-aliasing off by default in the browser (heavy on a laptop); a player sets `NumAASamples=4` in the GL4ES device's section of the browser's `Avaris.ini`, or `-ini:System:[UC2GL4ESDrv.UC2GL4ESRenderDevice]:NumAASamples=4`. |
| `Default.ini` | `[Engine.Engine] AudioDevice` | `UC2ALAudio.UC2ALAudioSubsystem` | kept as the web file has it (`WEB_KEYS`) | The same device, on openal-soft's Web Audio backend (the context starts on the page's first click or key; without Web Audio, AudioWorklet or cross-origin isolation the game runs silent and says why). |
| `Default.ini` | sections `[UC2ALAudio.UC2ALAudioSubsystem]`, `[UC2Movie]` | the native section, or none | kept as the web file has them (`WEB_SECTIONS`) | The web audio and movie settings, written out (`Backends=` empty: the web build has one backend; movies on). |
| `Default.ini` | new section `[WebRTCDrv.WebRTCNetDriver]` | none | the `[IpDrv.TcpNetDriver]` values except `ConnectionTimeout=60.0`, `InitialConnectTimeout=300.0` and `DownloadManagers=Engine.ChannelDownload` only (`WEB_SECTIONS`) | The driver reads its own class section; the timeouts are longer because a browser streams a map's files on demand and a guest sends nothing while it loads. |

## `android/`: the Android app's inis

`android/Default.ini` and `android/DefUser.ini` go into the APK (`Android/app/build.gradle`), and the app installs
them into the game folder's `System/` before every start, each only where it differs, so the `Avaris.ini` and
`User.ini` the engine derives on a phone come from them. They are generated: `tools/dev/sync-android-ini.py` writes
them as the native files with the differences below and nothing else (`--check` fails when they are not that). A new
Android difference is an entry in the script's `DELTAS` and a row here.

| File | Setting | Native | Android | Why |
|---|---|---|---|---|
| `Default.ini` | `[Engine.Engine] RenderDevice` | `UC2GLDrv.UC2GLRenderDevice` | `UC2GL4ESDrv.UC2GL4ESRenderDevice` | OpenGL ES only: the same driver behind GL4ES. `uc2.bin` on Android also switches an `Avaris.ini` from a desktop build that names the desktop device. |
| `Default.ini` | `[SDL2Drv.SDL2Client] StartupFullscreen` | `False` | `True` | The window is the whole screen anyway (the client makes it so on Android). |
| `Default.ini` | `[SDL2Drv.SDL2Client] UseMouse` | `True` | `False` | A phone is played with pads and the on-screen pad: the mouse is left out (also the compiled default on Android). |
| `Default.ini` | `[GUI.GUIController] bHideMouseCursor` | `False` | `True` | No mouse, so the menus draw no cursor. |
| `Default.ini` | `[UC2GL4ESDrv.UC2GL4ESRenderDevice] NumAASamples` | `4` | `0` | No multisampled main framebuffer by default on a phone's GPU. |
| `Default.ini` | `[UC2GL4ESDrv.UC2GL4ESRenderDevice] UseVSync` | `False` | `True` | The frame paced by the display: no tearing, and no frames the screen never shows (heat, battery). |
