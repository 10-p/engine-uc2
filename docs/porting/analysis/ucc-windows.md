# S2 notebook: the script compiler on Windows (`ucc.exe make`, MinGW i686), proved under wine

Spike S2 (2026-10-10). Question from the owner: "is it that bad to build ucc in Windows? We may expect to build
this in Windows at some point". Goal: the Editor package (the UnrealScript compiler, M14,
`analysis/scripts.md`) built into the MinGW `ucc.exe` of M12 (`analysis/windows.md`), and the M14 proof
(17 packages from `Source/<Pkg>/Classes`, compared with the shipped ones, problems 0) repeated with `ucc.exe`
under wine.

Worktree `engine-uc2-s2-uccwin`, branch `s2-uccwin`. Logs `<WorkUC2>/logs/s2/`, run trees `<WorkUC2>/run/s2*`,
wine prefix `<WorkUC2>/.cache/wine-s2` (its own, so `wineserver -k` never touches another agent's run). Every run
through `<WorkUC2>/bin/capped` (builds 6G at `-j2`: a verification run shares the machine; wine 3G).

## Step 1: Editor in the MinGW build

Baseline first, before any change: `bin/capped 4G 900 -- cmake --preset windows` (rc 0),
`bin/capped 6G 5400 -- cmake --build build-windows -j2` (`logs/s2/build-base.log`), to record uc2.exe and ucc.exe
as they are on main.
Baseline (main fa0796a, before any change; `logs/s2/base/exes.txt`, a copy of uc2.exe in `logs/s2/base/`):
- `uc2.exe` 87,194,629 bytes, sha256 `b98a5b0cf74297e5e4cd79a040d0add90bbf99416dc1c52505bd2d6a38b33a95`
- `ucc.exe` 70,877,876 bytes, sha256 `04b4f79e...e73789ff` (7,970,318 bytes stripped)

The change: one line. Root `CMakeLists.txt:429-434` (the M14 block): `if(UC2_PTR_BYTES EQUAL 8 OR MINGW)` becomes
`if(UC2_PTR_BYTES EQUAL 8)` and the comment says why. The 64-bit reason stands (UEditorEngine's four pointers
over script's `int Pad[8]`); MinGW i686 has 4-byte pointers, so it does not apply there.

`bin/capped 4G 900 -- cmake --preset windows` rc 0, `bin/capped 6G 5400 -- cmake --build build-windows -j2 -- -k`
rc 0 (`logs/s2/build-1.log`): `Linking CXX static library libEditor.a`, `Linking CXX executable ucc.exe`, nothing
else rebuilt. **No source change was needed.** What the Linux Editor build needed (scripts.md step 1:
`GCC_PACK(4)` on `AutosaveTimeMinutes`, `Src/UC2EditorRegistrants.cpp`, the static class accessors,
`UC2_TTF_GDI`, the `UnParams.cpp` duplicates under `__STATIC_LINK`, `struct FObjectExport;`, `register INT`,
`&FEditorVisibility::AddPortal`, `#undef MAXFLOAT`) is compiler-level, not Linux-level, and MinGW is GCC: it
already covered MinGW. The Editor sources have no Win32 include outside `TTFontImport.cpp`, whose GDI path is
`_MSC_VER` only (`UC2_TTF_GDI`); MinGW takes the same no-GDI path as Linux (no `.uc` imports a TrueType font:
`grep -a -rli TrueTypeFontFactory Source/*/Classes` finds nothing), so ucc.exe still imports only ADVAPI32,
KERNEL32, msvcrt, WS2_32 (`objdump -p`). Warnings: the Editor TUs show the same header warnings every MinGW TU
of the engine shows in the baseline log (`"__cdecl" redefined` from UnGnuG.h:194, `-Wwrite-strings`,
`AUTOGENERATE_FUNCTION ... initialized and declared 'extern'` from UnScript.h:33, ...); none Editor-specific.

After: `ucc.exe` 91,247,661 bytes (+20.4 MB, RelWithDebInfo), sha `7c7537bb...7da9537`; **`uc2.exe` not relinked:
87,194,629 bytes, sha256 `b98a5b0c...38b33a95`, identical to the baseline** (`logs/s2/exes-after.txt`).

## Step 2: `ucc.exe verifyclasses` under wine

```
tools/dev/mkrun-uc2.sh build-windows s2-win
UC2_WINEPREFIX=<WorkUC2>/.cache/wine-s2 bin/capped 3G 600 -- tools/dev/m12-wine.sh <WorkUC2>/run/s2-win/System ucc.exe verifyclasses
```

Exit 0; `logs/s2/verify.log`: `VERIFY: loading package Editor`, **`VERIFY: Editor class layouts match`**,
**`VERIFY: all native class layouts match`**; 0 problem/MISMATCH lines, 0 Critical. So MinGW's MS-ABI layout
(`-mms-bitfields`, 8-byte `double` alignment: windows.md decisions 2 and 4) gives UEditorEngine the script's
offsets too: the `GCC_PACK(4)` on `AutosaveTimeMinutes` fences the bitfield run under both bitfield rules, and
UEditorEngine has no 8-byte member.

## Step 3: the M14 proof with `ucc.exe` under wine, and the two differences from the Linux compiler

### Driving ucc.exe from `script-build.sh`

`tools/dev/script-build.sh --build build-windows ...`: when the build directory has `Source/UCC/ucc.exe` and no
`ucc-bin`, the script copies ucc.exe into the tree and runs `make` and `editor.stripsource` through
`tools/dev/m12-wine.sh <tree>/System ucc.exe ...` (inside `bin/capped`, same `--mem`), in the prefix
`$UC2_WINEPREFIX` (default `<WorkUC2>/.cache/wine-scripts`; these runs used `.cache/wine-s2`). assemble, the inis
and compare are unchanged. Also: tree names `s<N>...` (logs default `logs/s<N>`), and the make/strip lines now say
which compiler ran and how long it took. Nothing else was needed for wine: the tree's symlinks
(`<Pkg>/Classes -> Source/<Pkg>/Classes`, resource folders -> epic-sources) and hard links are followed by wine's
Z: mapping, `..\<Pkg>\Classes\*.uc`, `#exec ... FILE=..\Textures\...` and `` `include `` paths resolve, and
`-log=ucc.log` lands in the tree.

### First run: the same summary, but not the same bytes

`UC2_WINEPREFIX=<WorkUC2>/.cache/wine-s2 tools/dev/script-build.sh --build build-windows --tree s2-make --logs
<WorkUC2>/logs/s2` (all four stages): `ucc.exe make`: `Success - 0 error(s), 85 warning(s)`, all 17 packages; the
compare summary was line for line the one of main's Linux build (UCGameplay problems 20 and UCMenu problems 5 are
the post-M14 script changes D-6/D-9/G-8, recorded in vanilla.md step 2 and rebind.md: main's baseline). But the
packages compared directly with a Linux build of the same sources (`build-native` of this worktree, tree
`s2-linux`) were not all byte-identical: Editor, Engine, GamePlay (expected: `InternalTime`, below), and
**UCGameplay (3383 bytes), UCMenu (60712), UCMutators (129)**. A second Linux run (`s2-linux2`) differed from the
first only in Editor/Engine/GamePlay: the three were a Windows-vs-Linux difference, deterministic on each side.

### Cause 1: `appQsort` is the C library's `qsort`

`UObject::SavePackage` (Core/Src/UnObj.cpp) sorts the names, imports and exports that a conformed save appends
after the previous generation's, by usage count (`LinkerNameSort`/`LinkerImportSort`/`LinkerExportSort`), with
`appQsort` = `qsort` (UnAnsi.cpp:580). Equal counts are frequent (most new locals are used once), and qsort leaves
their order to the library: glibc 2.43's qsort is a merge sort (stable: the table order stays), msvcrt's is not.
Hence the appended exports of UCMutators (the patch-era `Reset`/`ScoreKill` locals), the appended
`IpDrv`/`TcpNetDriver` names and imports of Engine, and the appended exports of UCGameplay/UCMenu came out in other
orders. Fix: `UC2StableSort` (UnObj.cpp, beside the comparators), a bottom-up merge sort, for those three sorts
only. Proof: rebuilt both compilers, `s2-linux3`/`s2-make2`: Linux output unchanged (byte-identical to `s2-linux`,
so glibc's order is exactly the stable order), UCMutators and Engine's tables now the same under wine; UCGameplay
and UCMenu exports and imports the same, only their appended **names** still in different orders.

### Cause 2: the localization files are read in directory order

A temporary log of the name map before the conform step (FName index + name, not committed;
`logs/s2/diag/pre-*.txt`) showed the same names with different FName indices: e.g. `DeathString` 1744 on Linux,
2195 under wine; `Start` 3190 / 1745 (83 of UCGameplay's names). `UObject::CacheDrivers` (UnObj.cpp) reads every
`Localization/int/*.int` in `FindFiles` order to register their `[Public]` sections, and reading a file creates
the FNames of all its section and key names, so the file order fixes those names' indices. `FindFiles` is
`readdir()` order on both builds (FFileManagerUnix.h `SearchDirectory`; MinGW's readdir sits on FindFirstFile):
ext4 hash order on Linux (it would be another order on another file system or machine), sorted under wine (as
NTFS lists them). Then the name map is built in FName index order and the conform swaps follow it, so the
appended names with equal usage counts end up in a file-system-dependent order. Fix: `CacheDrivers` sorts the
`.int` list (and the language directory list) with `UC2SortLikeNTFS`, the NTFS order M14 already used for the
`.uc` files, moved from Editor's UMakeCommandlet.cpp into Core (`Core/Inc/UnFile.h` declaration, definition in
UnObj.cpp; UMakeCommandlet.cpp and UStripSourceCommandlet.cpp call it as before). Proof: the pre-conform name maps
of all 17 packages (38,087 lines) identical on both; the wine side did not move (its order was already NTFS's);
the Linux side moved to it.

### Changes, with file:line and why

| File | Change | Why |
|---|---|---|
| `CMakeLists.txt:429-435` | M14 block: `if(UC2_PTR_BYTES EQUAL 8 OR MINGW)` -> `if(UC2_PTR_BYTES EQUAL 8)` | Editor in the MinGW i686 build (step 1) |
| `Source/Core/Src/UnObj.cpp:5704-5736` (before `CacheDrivers`) | `UC2CompareNTFS` + `UC2SortLikeNTFS` (moved from Editor) | Core needs it for CacheDrivers |
| `Source/Core/Src/UnObj.cpp:5783, 5861` (`CacheDrivers`) | `UC2SortLikeNTFS( Files )`, `UC2SortLikeNTFS( CoreLangs )` after the two `FindFiles` | cause 2: file-system-independent FName indices |
| `Source/Core/Src/UnObj.cpp:4273-4308` | `UC2StableSort` | cause 1 |
| `Source/Core/Src/UnObj.cpp:4537, 4616, 4686` (SavePackage) | the three `appQsort` calls -> `UC2StableSort` | cause 1 |
| `Source/Core/Inc/UnFile.h:497-499` | `CORE_API void UC2SortLikeNTFS( TArray<FString>& )` | declaration for Core and Editor |
| `Source/Editor/Src/UMakeCommandlet.cpp:11-14` | its copy of the sort removed, comment points to Core | moved |
| `Source/Editor/Src/UStripSourceCommandlet.cpp:39-40` | the local `extern` declaration removed (the header has it) | moved |
| `tools/dev/script-build.sh` | ucc.exe-under-wine mode, `s<N>` trees, timings in the make/strip lines | step 3 |

Effect on what runs: the game never saves a package (rebind.md), so the sort change only touches `ucc make`,
`stripsource` and editor commandlets. The file order changes which index some FNames get at start-up in the game
too (on Linux; on Windows it was already this order); nothing depends on FName indices across runs. Linux script
output: UCGameplay.u and UCMenu.u change (other 15 byte-identical): the same name multiset, the same imports,
the same export table, only the order of the appended names (UCGameplay from index 8030 of 8075, UCMenu from 4940
of 5101) and the exports that refer to them by index (10 and 38 exports). So the asset tree's rebuilt
UCGameplay.u/UCMenu.u (built before this) and ones rebuilt after it differ in bytes with the same content.

### The proof, after the two fixes (final binaries of this branch)

```
UC2_WINEPREFIX=<WorkUC2>/.cache/wine-s2 tools/dev/script-build.sh --build build-windows --tree s2-make --logs <WorkUC2>/logs/s2
tools/dev/script-build.sh --build build-native --tree s2-linux --logs <WorkUC2>/logs/s2      # the Linux reference, same sources
```

`ucc.exe make -nobind -CONFORMNOCONFIG -noh : exit 1 in 26 s, Success - 0 error(s), 85 warning(s)` (exit 1 = Epic's
"there were warnings"; the same 85 as main's d12/g8 builds), `ucc.exe editor.stripsource -CONFORM: exit 0`;
compare against the shipped packages (`logs/s2/proof-windows.txt`):

```
Core             identical           exports  1011, byte-identical  1011, neutral    0, findings  0, problems  0
Engine           different-explained exports 12640, byte-identical 12125, neutral  933, findings  1, problems  0
UCAnnouncements  identical           exports   215, byte-identical   215, neutral    0, findings  0, problems  0
Editor           different-explained exports   446, byte-identical   438, neutral   15, findings  2, problems  0
UnrealEd         identical           exports   127, byte-identical   127, neutral    0, findings  0, problems  0
IpDrv            equivalent          exports   526, byte-identical   525, neutral    1, findings  0, problems  0
UWeb             identical           exports   153, byte-identical   153, neutral    0, findings  0, problems  0
GamePlay         equivalent          exports   974, byte-identical   962, neutral   16, findings  0, problems  0
UnrealGame       equivalent          exports   882, byte-identical   841, neutral   73, findings  0, problems  0
UCElements       equivalent          exports  2956, byte-identical  2955, neutral    1, findings  0, problems  0
UCGameplay       different           exports 10842, byte-identical 10217, neutral 1130, findings  6, problems 20
UCEffects        equivalent          exports  3535, byte-identical  3498, neutral   37, findings  0, problems  0
UCCharacters     equivalent          exports  2704, byte-identical  2646, neutral   58, findings  0, problems  0
UCInventory      equivalent          exports  1924, byte-identical  1863, neutral   88, findings  0, problems  0
UCMutators       equivalent          exports   579, byte-identical   557, neutral   29, findings  0, problems  0
GUI              different-explained exports  3526, byte-identical  3363, neutral  234, findings  1, problems  0
UCMenu           different           exports 10100, byte-identical  9932, neutral  223, findings  2, problems 5
```

This is main's Linux baseline line for line: the M14 proof had problems 0 everywhere; since then the D-6/G-8
mouse/keyboard changes (UCPlayerController, UCPlayerInput, UCTrainingController: UCGameplay problems 20, vanilla.md
step 2) and the D-9 keys page (UC2Settings_Main, UC2Settings_Keys, KeyBindingPanel: UCMenu problems 5, rebind.md)
changed the scripts on purpose, and the compare against the shipped files reports exactly those. The Linux
reference (`logs/s2/proof-linux.txt`) prints the same 17 lines, and the detail, per-function and patch-table files
of the two compares are identical (`diff -q` on `s2-{linux,make}-{compare,functions,patches}.txt`).

**Windows-built against Linux-built, directly** (`logs/s2/linux-vs-win-cmp.txt`, `cmp` of both `System/` (stripped)
and `Unstripped/`): **14 of 17 packages byte-identical** (28 of 34 files). Editor, Engine, GamePlay differ in 48,
534, 31 bytes; `script-diff.py <s2-linux>/System <s2-make>/System --packages Engine,Editor,GamePlay`
(`logs/s2/linux-vs-win-summary.txt`):

```
Engine           equivalent          exports 12641, byte-identical 12573, neutral   68, findings  0, problems  0
Editor           equivalent          exports   446, byte-identical   440, neutral    6, findings  0, problems  0
GamePlay         equivalent          exports   974, byte-identical   970, neutral    4, findings  0, problems  0
```

with `neutral {'import-time': 68}`, `{6}`, `{4}`: the `InternalTime` of the 78 `#exec`-imported textures
(`appSeconds()` at import), which differs between two Linux runs as well (scripts.md, "Repeatability"; measured
again here: `s2-linux2` vs `s2-linux`, and `s2-diagl` vs `s2-linux`, the same three packages only). Everything
else, including the float-heavy `#exec NEW StaticMesh` imports of Editor (M14's finding 7 against the shipped
file) and every float constant and default, is bit-identical between MinGW/msvcrt-under-wine and glibc.
(`script-diff.py` with a rebuilt package as the baseline crashes on UCMenu, a function that exists only on one
side, as g8 noted; with the packages byte-identical it was not needed.)

`ucc-bin verifyclasses` on Linux with the changed Core: exit 0, `VERIFY: Editor class layouts match`, `VERIFY: all
native class layouts match` (`logs/s2/verify-linux.out`).

## Step 4: measurements

| What | Result | Label |
|---|---|---|
| `ucc.exe make` under wine (17 packages) | 21-26 s wall over 6 runs (22 s typical; 26 s with load 12) | measured (`script-build.sh` line) |
| `ucc-bin make` natively, same tree, same machine | 7-9 s over 6 runs | measured |
| `stripsource`: ucc.exe / ucc-bin | 5-9 s / 5-8 s | measured |
| wine start-up + exit (`ucc.exe` banner only, warm prefix) | 1.3 s | measured |
| `ucc.exe` size | 70,877,876 -> 91,251,902 bytes (RelWithDebInfo); stripped 7,970,318 -> 9,539,086 (+1.57 MB: Editor) | measured |
| ucc.exe imports | ADVAPI32, KERNEL32, msvcrt, WS2_32 (unchanged) | measured (`objdump -p`) |
| `uc2.exe` after enabling Editor only | 87,194,629 bytes, sha256 b98a5b0c...38b33a95 = baseline (not relinked) | measured |
| `uc2.exe` after the Core fixes | 87,207,747 bytes (stripped 10,596,366 -> 10,599,438: +3 KB = `UC2StableSort` and `UC2SortLikeNTFS` now in Core), sha256 4d00c76d...268b173f; relinked because Core changed | measured |
| game boots under wine | `M12_TREE=s2-win M12_OUT=<WorkUC2>/logs/s2/smoke UC2_WINEPREFIX=<WorkUC2>/.cache/wine-s2 tools/dev/m12-wine-smoke.sh verify null gl leftovers`: 4 PASS | measured |

Smoke lines (final binaries, `logs/s2/smoke/smoke-summary.txt`):
```
PASS verify: exit 0, 'VERIFY: all native class layouts match', Critical 0
PASS null: menu level up with Engine.NullRenderDevice, SDL2 video driver: windows, Ctrl+C at 45 s -> exit 0, Critical 0
PASS gl: GL_RENDERER : llvmpipe (LLVM 21.1.8, 256 bits); GL_VERSION : 4.3 (Compatibility Profile) Mesa 26.0.8-1ubuntu0.3; 0 GL errors in 825 frames; menu screenshot luminance stddev 29.7, 71.3% of pixels lit (smoke-gl-menu.png); exit 0, Critical 0
PASS leftovers: no wineserver of /home/ion/WorkUC2/.cache/wine-s2, no uc2.exe/ucc.exe of run/s2-win running
```

The wine factor (about 2.5-3x) is not the compiler: the same code natively is 8 s, so the time is wine's
(presumably file I/O through wineserver: `make` reads 2,218 `.uc` files plus the `#exec` textures and every package; not profiled). A real Windows
host should be close to the native number; not measured (no Windows machine here).

Memory: builds under `capped 6G` (-j2), make/strip under 3G (wine and native), verifyclasses 3G (wine) / 2G
(native), smoke 3G per run. No ceiling hit (no exit 137); every non-zero exit is ucc's own "warnings" 1.

## Step 5: what a real Windows host (no wine) would still need

Not tried (no Windows machine); assumptions, each from reading the script and the build:

1. **Toolchain**: MinGW-w64 i686 with the posix thread model (MSYS2 `mingw32` environment:
   `mingw-w64-i686-toolchain`, `mingw-w64-i686-cmake`, git, python3). `cmake --preset windows` points at
   `cmake/toolchain-mingw-w64.cmake`, written for a cross build; whether MSYS2's triplet-prefixed compilers satisfy
   it unchanged is not verified (BUILD.md says a native MSYS2 build "lands on the same code paths"; not run).
   FetchContent clones SDL2 and openal-soft from GitHub at configure time (network, git).
2. **Inputs of the script build** (the script resolves them relative to the repo's parent, `<WorkUC2>`):
   `assets/uc2/` (content folders + `System/` with Epic's inis, `Localization/`), `epic-sources/System/*.u` (the
   shipped packages: conform baseline and compare baseline) and `epic-sources/<Pkg>/<resource dirs>` (textures and
   models the `#exec` lines import), `reference/iso-extract/System/{Characters,Gametypes,Maplists}.ini` (retail
   inis for the strip), the repo's `Source/<Pkg>/Classes`. `UC2_ASSETS_DIR`, `UC2_EPIC_SOURCES`,
   `UC2_SHIPPED_SYSTEM` override the first three.
3. **Git checkout**: `.gitattributes` has `* -text`, so git for Windows keeps the LF line endings and the Latin-1
   bytes of the `.uc` files; a CRLF conversion would change every class's script text (CRCs, positions) and the
   compare would no longer be against what Epic compiled.
4. **What `script-build.sh` assumes that is bash/Linux-only** (for the CMake rules that replace it):
   - `bin/capped` (cgroups through systemd: Linux only) around every ucc run; `tools/dev/m12-wine.sh` for ucc.exe.
   - the tree: `cp -al` (hard links of the content folders; NTFS has hard links, but they need the same volume),
     `ln -s` for `<Pkg>/Classes` and the resource folders (on Windows symlinks need Developer Mode or admin;
     MSYS2's `ln -s` copies unless `MSYS=winsymlinks:nativestrict`; a copy works for a build), `cp -a`, `chmod`.
   - bash arrays, `$SECONDS`, `set -euo pipefail`, `stat -c %s`, `grep -a -o`, `tr -d '\r'`, `diff`, process
     substitution `<(...)` (header diff only).
   - paths with `/`; ucc itself is run from `<tree>/System` and reads `..\<Pkg>\Classes`, `..\GUIRes`, `Avaris.ini`,
     `User.ini` relative to it (true on any OS).
   - `python3` for `script-diff.py`/`upkg151.py` (portable Python; reads bytes, no OS calls beyond files).
   The compile itself (`ucc make -nobind -CONFORMNOCONFIG -noh`, `ucc editor.stripsource -CONFORM`, Epic's inis copied
   as `Avaris.ini`/`User.ini`, the retail inis for the strip, UCMenu.u kept unstripped) is OS-independent.
5. What only a real Windows run can confirm: real NTFS directory order (assumed = `UC2SortLikeNTFS` order; under
   wine the listing already came sorted that way, and the code no longer depends on it for the localization files
   and the `.uc`/`.u` lists), Microsoft's msvcrt `qsort` (no longer used by the package save), and the wall time.

## Open questions (for the coordinator / owner)

1. The Linux script output changes (UCGameplay.u, UCMenu.u: order of the appended names only, step 3). The asset
   tree's rebuilt packages were made before; whether to rebuild them (bytes change, content not) is the
   coordinator's call. Network: a conformed rebuild keeps the shipped GUID, so two peers with differently built
   UCMenu.u of the same source would have had the same GUID and different name tables before this fix; after
   it, Linux and Windows builds agree.
2. `appQsort` stays the C library's qsort everywhere else (UT2004 and the siblings do the same). Ties sorted by it
   at run time (e.g. render sorts) can still differ between glibc, msvcrt and Emscripten's musl; nothing seen
   depends on it (M12's pinned frame is 100 % equal in the static part), so it was left alone. Making
   `appQsort` itself stable would be a one-function change with an engine-wide reach.
3. `FindFiles` itself still returns readdir() order; only the callers that the script build depends on sort
   (M14: `.uc`, `.u`; S2: localization files and directories). Sorting inside `FFileManagerUnix::SearchDirectory`
   (Windows' NTFS behaviour for every caller) is the general alternative; it would also change run-time listings
   (profiles, content manager), so not done here.
