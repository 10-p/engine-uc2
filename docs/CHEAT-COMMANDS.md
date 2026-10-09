# Position cheat commands: `WHEREAMI` and `TELEPORT`

Two console commands that tell where the player is and put the player somewhere else. They are useful to every
engine of the frontend (reproducing a bug at an exact spot, benchmark views, test scenarios, screenshots for
comparisons), so every engine should offer them with **these names and this format**. They are **cheats**: an
engine offers them only where the game itself allows cheats, refuses them elsewhere, and logs every use.

First implementation: engine-uc2 (Unreal Championship 2), `Source/Engine/Src/UC2Cheats.cpp`.

## Commands

| Command | What it does |
|---|---|
| `WHEREAMI` | Prints the local player's position and view as `<x> <y> <z> <yaw> <pitch>` on the console and on the screen (the game's own message line), and logs it. |
| `TELEPORT <x> <y> <z> [<yaw> [<pitch>]]` | Moves the local player there. With `<yaw>` (and `<pitch>`) the view turns too; without them the view is kept. |

The output of `WHEREAMI` is valid input for `TELEPORT`: pasting it back gives the same place and the same view.

### Format

- `<x> <y> <z>`: world coordinates in the engine's own units (Unreal units for the Unreal engines), printed with
  one decimal (`1977.8 583.7 -34.1`). The point is the player's pawn location (the centre of its collision
  cylinder), not the eye or the camera.
- `<yaw>`: the view's yaw in the engine's rotation units, printed as an integer in `0..65535` for the Unreal
  engines (65536 = a full turn); `TELEPORT` accepts any integer and wraps it.
- `<pitch>`: the view's pitch, the same units, printed signed in `-32768..32767` (negative = looking down);
  `TELEPORT` accepts any integer.
- Fields are separated by one space; nothing else is on the line. Example: `1977.8 583.7 -34.1 33000 -1400`.

### Log lines

Every use writes one line beginning with `Cheat: ` to the engine's log, so a log always shows that a cheat was
used:

```
Cheat: WHEREAMI 1977.8 583.7 -34.1 33000 -1400
Cheat: TELEPORT 1977.8 583.7 -34.1 33000 -1400
Cheat: TELEPORT ok: 1977.8 583.7 -34.1 33000 -1400
Cheat: TELEPORT failed (no room there): ...
Cheat: TELEPORT refused: cheats are only for a standalone game (...)
```

`TELEPORT ok:` / `failed` report where the player actually is afterwards, in `WHEREAMI`'s format.

## Where they work (the guard)

- **A single-player (standalone) game: yes.** This is the rule each game already has for its cheats; the engine
  uses the game's own rule rather than inventing one. In the Unreal engines: the local `PlayerController` has a
  `CheatManager`, which `PlayerController.AddCheats` creates only when `Level.NetMode == NM_Standalone`.
- **A network game (listen server, client, dedicated server): no.** The command is refused with a `Cheat: ...
  refused` log line and a console message. A client could not move its pawn anyway (the server owns it).
- **Test harnesses** that need positions in network games set a test-only ini key in that process
  (engine-uc2: `[Engine.GameEngine] AllowNetCheats=True`, given on the command line as
  `-ini:System:[Engine.GameEngine]:AllowNetCheats=True`). It is not in any shipped ini. With it, the log line
  says so: `Cheat: TELEPORT ... (AllowNetCheats: a test setting)`.

What "protected" means here: the commands do nothing in normal network play, and every use leaves a `Cheat:`
line. It is not anti-cheat: anyone who edits their own ini or binary can do anything on their own machine; a
server never accepts a teleport from a client.

## Engine notes

- **Name clash in Unreal Engine 2:** Epic's `CheatManager` already has `exec function Teleport()` (no arguments:
  to the surface the player looks at). The console runs script `exec` functions before the engine's own
  commands, so engine-uc2 checks `TELEPORT` **with arguments** first (in `UPlayer::Exec`); `TELEPORT` alone is
  still Epic's. UT2004 has the same `CheatManager.Teleport` and needs the same order.
- The engine moves the pawn with `FarMoveActor` (no sweep) and clears its velocity; the server's movement
  correction is told the new spot (UC2 returns a pawn to its last `ServerMove` position otherwise).
- engine-uc2's older test commands `NETPROBE`, `NETPLACE`, `NETPLACENEAR` and `NETAIM` (`UC2NetTest.cpp`: every
  player's position, move a named player, aim at a player) are cheats too: the same guard, the same `Cheat:` lines.
- Keys can be bound to them like any console command, e.g. `F11=WHEREAMI`.
- The web frontend can send them through its console bridge (`ue_exec_command` in engine-uc2).
