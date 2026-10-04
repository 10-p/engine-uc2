# Configuration

`Default.ini`, `DefUser.ini`, `DefaultXbox.ini`, `DefUserXbox.ini` and `KeyBinds.ini` started as byte-for-byte
copies of the Epic tree's `System/` files (first commit). `tools/dev/mkrun-uc2.sh` copies them over the asset
tree's when it assembles a run tree, so this folder is where our defaults live. Every change from Epic's
file is listed here with its reason; `git log -p Engine/Config` has the exact diff.

| File | Setting | Epic | Ours | Why |
|---|---|---|---|---|
| `Default.ini` | `[URL] Port` | 1000 | 7777 | 1000 is the Xbox system-link port and is privileged (below 1024) on Linux, so a server could not listen. 7777 is the Unreal Engine 2 default. |
| `Default.ini` | `[Engine.Engine] ViewportManager` | `WinDrv.WindowsClient` | `SDL2Drv.SDL2Client` | WinDrv is Win32-only and not built; SDL2Drv is the portable client (M4). Epic's value is kept as a comment line. |
| `Default.ini` | `[Engine.Engine] RenderDevice` | `D3DDrv.D3DRenderDevice` | `Engine.NullRenderDevice` | D3DDrv is not built; the null device is the only one until the GL driver (M5). The two lines swapped places: Epic's file already had the null device as a comment. |
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
