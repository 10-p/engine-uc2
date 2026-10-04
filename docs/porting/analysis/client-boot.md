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
