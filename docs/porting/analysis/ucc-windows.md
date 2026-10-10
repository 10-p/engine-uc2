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
