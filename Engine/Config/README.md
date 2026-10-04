# Configuration

`Default.ini`, `DefUser.ini`, `DefaultXbox.ini`, `DefUserXbox.ini` and `KeyBinds.ini` started as byte-for-byte
copies of the Epic tree's `System/` files (first commit). `tools/dev/mkrun-uc2.sh` copies them over the asset
tree's when it assembles a run tree, so this folder is where our defaults live. Every change from Epic's
file is listed here with its reason; `git log -p Engine/Config` has the exact diff.

| File | Setting | Epic | Ours | Why |
|---|---|---|---|---|
| `Default.ini` | `[URL] Port` | 1000 | 7777 | 1000 is the Xbox system-link port and is privileged (below 1024) on Linux, so a server could not listen. 7777 is the Unreal Engine 2 default. |
| `Default.ini` | `[Engine.Engine] ViewportManager` | `WinDrv.WindowsClient` | `SDL2Drv.SDL2Client` | WinDrv is Win32-only and not built; SDL2Drv is the portable client (M4). Epic's value is kept as a comment line. |
| `Default.ini` | `[Engine.Engine] RenderDevice` | `D3DDrv.D3DRenderDevice` | `UC2GLDrv.UC2GLRenderDevice` | D3DDrv is Direct3D 8 and not built. UC2GLDrv is our OpenGL device (M5); `Engine.NullRenderDevice` stays available as a commented line and through `-nullrenderer`. |
| `Default.ini` | new section `[UC2GLDrv.UC2GLRenderDevice]` | (none) | the device's settings, with the values Epic's `[D3DDrv.D3DRenderDevice]` section has where the setting exists there, plus `MaxTextureUnits`, mip biases and `CheckGLErrors=False` | The device reads its settings from its own class section. |
| `Default.ini` | `[UC2GLDrv.UC2GLRenderDevice] UsePixelShaders`, `PrecompileVertexShaders` | (none) | `True`, `True` | M6. `UsePixelShaders` draws pixel-shader materials with GLSL programs translated from Epic's Xbox shaders; `False` restores the M5 behaviour (every such material drawn with its first texture stage) for A/B comparison. Also `-nopixelshaders` / `-pixelshaders` on the command line and the console command `PIXELSHADERS [0\|1]`. `PrecompileVertexShaders` compiles the vertex shader of every combination in `VertexShaderConfigs.ini` at start-up (0.2 s under software GL). Both are also the compiled defaults. |
| `Default.ini` | new section `[SDL2Drv.SDL2Client]` | (none) | copy of `[WinDrv.WindowsClient]` | The client reads its settings from its own class section; same values as the PC client's (640x480 windowed, gamma, detail). `MenuViewportX/Y` and `ScaleHUDX` came along with the copy; UC2's `UClient` does not read them. |

`uc2.bin` also replaces, for the current run and in the derived `Avaris.ini`, a driver the ini names that this
build does not contain (`WinDrv.WindowsClient`, `D3DDrv.D3DRenderDevice`, `XboxDrv.XboxClient`...) with
`SDL2Drv.SDL2Client` / `Engine.NullRenderDevice` / `Engine.NullAudioDevice`, and logs it, so an ini from the
PC or Xbox release still boots. `-nullrenderer` selects `Engine.NullRenderDevice` (also written to
`Avaris.ini`, like any setting changed at run time).

## `web/` - the browser build's boot inis (M4w)

`web/Default.ini` and `web/DefUser.ini` are preloaded into the web build as `/System/Default.ini` and
`/System/DefUser.ini` (`Source/SDLLaunch/cmake/Web.cmake`); `appInit` derives `Avaris.ini` and `User.ini`
from them in the browser. A host must therefore not serve the asset tree's copies of these four names.
They are byte copies of the two files above: the null-device web smoke needs no change from them. Every
web-only change (GL device, WebRTC net driver, M11) goes here as a row:

| File | Setting | `Engine/Config` value | Web value | Why |
|---|---|---|---|---|
| (none yet) | | | | |
