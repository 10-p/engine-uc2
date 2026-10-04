# Configuration

`Default.ini`, `DefUser.ini`, `DefaultXbox.ini`, `DefUserXbox.ini` and `KeyBinds.ini` started as byte-for-byte
copies of the Epic tree's `System/` files (first commit). `tools/dev/mkrun-uc2.sh` copies them over the asset
tree's when it assembles a run tree, so this folder is where our defaults live. Every change from Epic's
file is listed here with its reason; `git log -p Engine/Config` has the exact diff.

| File | Setting | Epic | Ours | Why |
|---|---|---|---|---|
| `Default.ini` | `[URL] Port` | 1000 | 7777 | 1000 is the Xbox system-link port and is privileged (below 1024) on Linux, so a server could not listen. 7777 is the Unreal Engine 2 default. |

`web/` will hold the web build's boot inis (M4w).
