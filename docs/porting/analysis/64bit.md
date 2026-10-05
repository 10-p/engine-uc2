# M13 — 64-bit (LP64 x86_64; LLP64 windows64; arm64 build-only) notebook

Branch `m13-64bit`, worktree `/home/ion/WorkUC2/engine-uc2-m13`. Appended after every step so a successor can
continue (CLAUDE.md). Commands run through `<WorkUC2>/bin/capped`. Logs: `<WorkUC2>/logs/m13/`.

## What the templates did (read 2026-10-05)

- **engine-ut2004** (`/home/ion/WorkUnreal/engine-ut2004`, read-only): Ryan Gordon's amd64 port of UE2 is in
  Epic's UT2004 code. `SERIAL_POINTER_INDEX` (pointer slots in bytecode hold a 4-byte index into
  `GSerializedPointers[]`, filled by a *linear search* `SerialPointerIndex`), `PTRINT`, `UPointerProperty`
  (script type `pointer`, ElementSize `sizeof(PTRINT)`), object properties aligned to 4 in `Link`, Object.uc's
  `ObjectInternal[7]` declared as `pointer` and recompiled into the shipped `.u`. One hand fix at link time:
  `USound.Data` (`byte[44]` placeholder) widened to 48 on LP64. `FMallocAnsi` pads every allocation by 32 bytes
  on 64-bit ("some uscript classes have four bytes extra at the end"). `UObject::Index`/`_LinkerIndex` are
  `PTRINT` (they double as registration stashes). `BITFIELD` stays `unsigned int` (4 bytes), never `long`.
  CMake: `UT_ALLOW_64BIT`, `UT_PTR_BYTES` gate every `-m32`.
- **UC2's Epic tree already has `SERIAL_POINTER_INDEX`** (UnObjBas.h, UnScript.h `ReadObject`, UnCorSc.cpp
  `execBoolVariable`, UnClass.cpp `XFERPTR`, UnObj.cpp registration stash for `Name`), keyed on `__x86_64__`
  only, with the same linear search and a 64K table. UC2's shipped `.u` files are 32-bit: every native pointer
  slot in script is an `int` (no `pointer` type), so UT2004's "recompile Object.uc" is not open to us before M14.
- **ufrontend 2.83/2.84** (UT99, Unreal 1 at LP64; `ufrontend/docs/migration/steps/2.83-64bit-ue1.md`,
  `2.84-64bit-unreal1.md`): the playbook. Bytecode keeps 4-byte slots holding an object index; mirrors are
  pack(4) and object properties align to 4; **a placeholder table in `UStruct::Link` gives each named native
  stand-in (script `int` hiding a C++ pointer/vtable) its C++ size** — same as before at 32-bit; the
  null-context zero-fill (`execContext` zeroes the compiler's 4 bytes of an 8-byte pointer) fixed in `execLet`;
  registration stashes moved to pointer-sized fields; hand-described reflection (`UInput` alias struct by
  arithmetic) crashed the client; `long` as 32 bits (`appGetLocalIP`); a literal `+ 4` next to a pointer in a
  renderer; a layout guard (fatal) + struct pass. Found by running: P1 scratch buffers read at pointer width
  after a 4-byte fill, P3 None-context struct zeroed to its 32-bit size, ASan bugs (not width).

## Plan

1. CMake: `native64` must really produce x86_64 everywhere (check `file`), cast diagnostics visible.
2. Compile census → every pointer↔int site; fix at the declaration.
3. Bytecode: `SERIAL_POINTER_INDEX` on every 64-bit target (not just `__x86_64__`), O(1) lookup.
4. Layout: placeholder table at link time for script `int`s that are C++ pointers/vtables (hand-written
   mirrors), pack(4) mirrors, object properties aligned 4; `verifyclasses` 64-bit mode.
5. VM hazards from 2.83 (null-context fill, scratch buffers).
6. Run: verify, census, client, GL, audio, movies, controls; cross-ABI saved data; windows64; arm64 preset.

## Log

### Step 1 — baseline: `native64` already configures and links (2026-10-05)

```
bin/capped 4G 900 -- cmake --preset native64          # "Target arch: x86_64; pointer width: 8 bytes"
bin/capped 6G 3600 -- cmake --build build-native64 -j6 -- -k   # 6m10s, 0 errors
file build-native64/Source/UCC/ucc-bin                 # ELF 64-bit LSB pie executable, x86-64
```
Log: `logs/m13/build64-first.log`. Everything links (uc2.bin 66 MB, ucc-bin 54 MB) because `-fpermissive`
turns every narrowing pointer cast into a warning. Nothing has run yet.

**Cast census** (`tools/dev/m13-cast-census.sh <log>`, new): distinct source lines with "cast from <ptr> to
<int> loses precision" (C++ makes it an error; `-fpermissive` a warning; GCC has no switch to make only
that one an error again) or `-Wint-to-pointer-cast`, fetched libraries skipped. Before any fix:
**29 ptr-to-int + 90 int-to-ptr = 119 lines** (Engine 81, IpDrv 21, Core 10, GUI 7);
`logs/m13/cast-census-before.txt`. Not seen by the compiler (needs reading/running): type puns like
`*(FResolveInfo**)&PrivateResolveInfo`, literal `4`s, `sizeof(INT)` used for a pointer, layouts.
