# E-2: web multiplayer (WebRTCDrv over the frontend's WebRTC data channels)

Branch `e2-webrtc`, worktree `<WorkUC2>/engine-uc2-e2` (from main 1519163). Frontend: `<WorkUC2>/ufrontend`
branch `uc2` (local commits). Logs `<WorkUC2>/logs/e2/`, run trees `<WorkUC2>/run/e2*`. Notebook appended after
every step (newest at the bottom of "Steps").

## Starting point (2026-10-08)

- Natively network play works over `IpDrv.TcpNetDriver` (`analysis/network.md`, `tools/dev/net-test.sh`). In the
  browser: `Engine/Config/web/Default.ini` names `NetworkDevice=IpDrv.TcpNetDriver`, no `[WebRTCDrv.*]` section,
  `Source/WebRTCDrv` does not exist (the root CMakeLists already lists the package name), the web link has no
  `webrtc-shim.js` (the vendored copy in `vendor/glue/` is byte-identical to engine-ut2004's), and
  `targets/uc2/target.json` declares no `network.modes` / `serverName` and the uc2 e2e profile has empty net tables.
- Template (read-only): `/home/ion/WorkUnreal/engine-ut2004/Source/WebRTCDrv` (driver 603 lines, connection 113,
  discovery 183), its SDLLaunch hooks (auto-switch TcpNetDriver -> WebRTCNetDriver under `__EMSCRIPTEN__`,
  `AUTO_INITIALIZE_REGISTRANTS_WEBRTCDRV`, `GWebRTCEngine`), its UnGame.cpp `Browse` redirect (`<room>?connect`
  -> network URL), `[WebRTCDrv.WebRTCNetDriver]` in its web Default.ini.
- `UnNetDrv.h` UC2 vs UT2004: `Time` is FLOAT (UT2004 DOUBLE), no voice/redirect members, UC2 adds the "standby
  cheat" check (`LastReceiveTime`, `UpdateLastReceiveTime()` must be called by the driver on every received
  packet, as TcpNetDriver does, or `NotifyStandbyHack` fires after 1 s server / 3 s client),
  `PreServerTravel`, `CreateChild` (split-screen child connections), `ShouldIncludeInStandbyCheck`.
  `UnConn.h`: `MaxPacket` is `_WORD`, `PacketOverhead` BYTE, packet ids `_WORD` (wrap fix in network.md L2).
- Frontend contract (glue `packages/glue/src/webrtc/index.js`, `join-key.js`): the host's driver calls
  `webrtc_init_listen("<room>/<server name>")`; the glue registers `<peerPrefix>-<room>_<serverSlug>-srv`. A guest
  launch's first engine argument is `<room>_<serverSlug>?connect[?travel options]` (frontend `parseUrlParams` +
  `buildCmdLine`); the engine must browse it as a network URL and call `webrtc_connect("<peerPrefix>-<key>-srv")`.
  `peerPrefix` for uc2 = `uc2` (target data). The P2P mode is what the ENGINE establishes by itself
  (frontend netModeSetting.ts: "a FIRST boot seeds nothing for P2P").
- ServerName (D-3 finding, `saveconfig.md`): `PlayerProfile.SetServerName` (Engine/Classes/PlayerProfile.uc:764)
  writes the profile's server name ("Liandri Corporation" by default) over the CLASS DEFAULT of
  `GameReplicationInfo.ServerName` at start-up; it never writes the ini. The template's InitListen reads the
  CONFIG (`GConfig [Engine.GameReplicationInfo] ServerName`), which the profile code does not touch.

## Plan

1. Port WebRTCDrv (web build only), adapted to UC2: standby-check receive time, FLOAT time, no template-only
   ufront log hooks in Engine that UC2 lacks; status push through `Notify->NotifyGetLevel()`; no in-game server
   browser hook (UC2 has no UT2004 MasterServerClient browser; discovery = the launcher's room + invite link).
2. Web inis/launcher: `[Engine.Engine] NetworkDevice=WebRTCDrv.WebRTCNetDriver` in the web Default.ini, a full
   `[WebRTCDrv.WebRTCNetDriver]` section, the launcher's auto-switch for inis persisted by older builds.
3. `Browse`: `<key>?connect` -> network URL (web only).
4. Frontend target uc2: `network.modes` (p2p only), `serverName`, notices, capabilities `webrtc`; e2e profile
   milestones; a net spec (host listen + client invite join; then two clients).
5. Native regressions (net-test.sh), browser runs with memory peaks, NETDROP/PktLoss over WebRTC.

## Steps

### Step 1 (2026-10-08): the driver, the web link, the web inis, the join-key browse

Engine changes (all web-only; the native build compiles nothing new):
- `Source/WebRTCDrv/` (new, `if(NOT EMSCRIPTEN) return()`, as the template's root CMakeLists adds it only inside
  `if(EMSCRIPTEN)`: evidence `engine-ut2004/CMakeLists.txt:1283-1287`): `Inc/WebRTCDrv.h`, `Src/WebRTCDrv.cpp`,
  `Src/WebRTCNetDriver.cpp`, `Src/WebRTCConnection.cpp`. Differences from the template, each for a UC2 reason:
  - `UpdateLastReceiveTime()` on every received packet (UC2's standby-cheat clock, `UnNetDrv.cpp:250-297`;
    TcpNetDriver.cpp:299 does it);
  - no `emscripten_sleep(100)` per tick while waiting: in the template a listen server alone would run at
    10 frames a second; UC2's engine is on a worker (M4w) and yields once per frame already;
  - the listen status (name/map/players for the glue's status replies) through `Notify->NotifyGetLevel()`;
  - the peer id prefix `uc2` (target data `network.peerPrefix`), `ServerName` from `?ServerName=` else the
    CONFIG `[Engine.GameReplicationInfo] ServerName` (not the class default, which the profile overwrites);
  - `PreServerTravel() {}` as UTcpNetDriver (natively verified travel path: loading clients are not
    force-failed);
  - no discovery/server-browser file (UC2 has no MasterServerClient browser; its System Link browser is
    UdpBeacon-based, see step 2 notes), no `webrtc_deferred_open` (the frontend no longer uses it; joins come
    as the startup URL);
  - config normaliser kept (an Avaris.ini from an older web build has no `[WebRTCDrv.WebRTCNetDriver]`):
    UNetDriver::StaticConstructor defaults MaxClientRate/MaxInternetClientRate only; the rest would be 0.
- `Source/SDLLaunch/CMakeLists.txt` links WebRTCDrv when the target exists (`UC2_WITH_WEBRTCDRV`);
  `cmake/Web.cmake` adds `--js-library vendor/glue/webrtc-shim.js` (+ LINK_DEPENDS); `SDLLaunch.cpp` registers
  the classes and, in `ReplaceMissingDrivers`, switches `NetworkDevice=IpDrv.TcpNetDriver` to the WebRTC driver
  (an Avaris.ini persisted by an older web build), as the template's launcher does.
- `Engine/Config/web/Default.ini` (generated by `tools/dev/sync-web-ini.py`: new DELTA for NetworkDevice, new
  WEB_SECTIONS entry): `NetworkDevice=WebRTCDrv.WebRTCNetDriver` and a `[WebRTCDrv.WebRTCNetDriver]` section
  (native TcpNetDriver values; ConnectionTimeout 60, InitialConnectTimeout 300 because a guest streaming
  DM-Deadbolt's files the first time sends nothing while it loads, and until it is in the level the host applies
  InitialConnectTimeout, UnConn.cpp:1032). README rows in `Engine/Config/README.md`.
- `Engine/Src/UnGame.cpp` `UGameEngine::Browse` (web only): a LOCAL URL with `?connect` and a map is the
  frontend's join key: Host = key, Map = FURL::DefaultMap, and the `connect` option is dropped (a partial travel
  copies every option, so leaving it would turn a later local map into a host).

Build: wasm-perf from scratch, `bin/capped 6G 5400 -- logs/e2/build-wasm-perf.sh 0` (-j4).

Build result: `build-wasm-perf` rc 0, 0 `error:` (`logs/e2/build-wasm-perf-0.log`, ~6 min); the module imports the 9
webrtc_* calls the driver makes; SDLLaunch.wasm sha256 0426c53b0ef05b93...

### Step 2 (2026-10-08): frontend data + spec; first browser run: the JOIN works over WebRTC

Frontend (ufrontend, branch uc2, uncommitted at this point): `targets/uc2/target.json` network block
(`serverName` = Avaris.ini [Engine.GameReplicationInfo] ServerName, stock `Another Unreal Server`; `modes` p2p only +
`networkDevice`), notices (join-failed / join-refused / version-mismatch / host-failed with UC2's own wording),
capability `webrtc` recommended; `packages/e2e/lib/engines/uc2.profile.js` host/client milestone tables and net
surface; new `packages/e2e/uc2-wasm-net.spec.js` (shared lib/net-harness.js; NETPROBE through the shell's ueExec;
memory sampled from Node). New `tools/dev/e2-web-net.sh` (this repo) runs it under the rules.

Run 1 (`logs/e2/e2e-net-1.txt`, serve-web proven = this build by sha256 before/after, null sink by index, removed):
host listens (`[WebRTC] InitListen: room 'uc2net-muze3adc' server name 'E2E Host Server'`, the launcher's seed
read from the CONFIG), the glue registers `uc2-uc2net-muze3adc_e2ehostserver-srv`; guest launched from the invite
URL: `Browse: '...' with ?connect is a WebRTC join key`, `[WebRTC] InitConnect: room key ...`, pipe FULLY OPEN on
both sides, host `ACCEPTED connection 1`, guest `Welcomed by server: LEVEL=DM-Deadbolt GAME=UCGameplay.UCDeathmatch`,
guest `Bringing Level DM-Deadbolt.MyLevel up for play`, host `Join request: ...?Name=JoinPlayer...`, `Join
succeeded: JoinPlayer`; NETPROBE on both pages agrees (2 PRIs, conns=1, net=ListenServer / Client).
FAILED at the spawn: `ueExec('Fire')` every ~4 s for 120 s spawned nobody (both PRIs stayed spectators, pawns=0).
The uc2 boot spec spawns with a REAL left click held 1 s (its comment: a second click for the pointer lock); the
console route of `Fire` is not what it uses. Next: real clicks, plus `ueExecResult('Fire')` once to see the
engine's answer, and the full console of each page written to disk.
Observations: the glue's game data channel is `ordered=true/maxRetx=null` (reliable, ordered SCTP), not the
unordered/unreliable channel network.md assumed: the engine's own acks/resends run on top of a channel that never
loses a datagram while it is open (loss only when a pipe dies and the session layer reconnects). The startup logos
play before the main loop starts (the host's listen peer registers only after them: PEER_OPEN came ~30 s after
InitListen). Memory (2 s samples from Node): renderer PSS host 1,429 MiB, guest 1,328 MiB; all Chromium processes
together 3,066 MiB; cgroup memory.peak 5,691 MiB (ceiling 8G); machine in use 13.3 GB peak (9.7 GB before).

### Step 3 (2026-10-08): the spec passes: join, spawn, movement, score, leave over WebRTC

`ueExecResult('Fire')` on the guest: `{"output":"","ok":true,"cmd":"Fire","delivered":4}` (accepted, nobody
spawned); the spec now spawns with a real left click held 1 s on each canvas (as uc2-wasm-boot.spec.js does).
Run 2 (`logs/e2/e2e-net-2.txt`, pages' consoles `logs/e2/e2e-net-2-pages/`, serve-web = this build by sha256
before and after, null sink by index, removed): **1 passed (1.7 min)**:
- host `[WebRTC] InitListen: room 'uc2net-muze905d' server name 'E2E Host Server'`, peer
  `uc2-uc2net-muze905d_e2ehostserver-srv` registered; guest join key browsed, pipe open, `Welcomed by server:
  LEVEL=DM-Deadbolt GAME=UCGameplay.UCDeathmatch LONE=0`, guest level up, host `Join succeeded: JoinPlayer`;
- both clicked: host `START MATCH`; NETPROBE: host `localpawn=1 pawns=2 conns=1`, guest `net=Client localpawn=1 pawns=2`;
- W held 1.5 s on the guest page: the host saw the guest pawn go (1531.9,3111.5,-34.1) -> (927.9,3680.0,-386.1),
  901 units (it walked off a ledge); at rest guest and host agree to 0.0 units;
- guest `Suicide`: host PRI `score=-1 deaths=1`, the guest's own view the same;
- guest `disconnect`: host `conns=0 pawns=1 pris=1` (the guest's PRI gone) within one probe; the guest is back on
  its own (`net=Standalone level=Menu`). The drop came from the engine's own channel-0 close, before the glue's
  pipe-close path (`PEER_DROPPED` not seen: the glue reaps a closed host pipe only after its reconnect grace).
- Engine lines worth knowing: guest `Received invalid actor class` + `channel 51: got None` once (the run-time
  DefaultPhysicsVolume, Epic's, as natively, network.md step 3); host `UdpBeacon ... BindPort: socket failed` /
  `UdpBeacon failed to bind a port.` (the game spawns its system-link beacon on a listen server; a browser has no
  UDP sockets; harmless); `Failed to assign pawn class to new player!` in the menu after leaving (also natively).
- Memory (Node, 2 s): PSS host browser 1,654 MiB, guest browser 1,550 MiB, all Chromium 3,098 MiB; cgroup
  memory.peak 5,676 MiB (ceiling 8G, not hit); machine in use peak 13.4 GB (10.0 GB before the run).
Frontend committed (ufrontend 6f2a302 target data + README, 079462e spec + profile + e2e README); `yarn test` 28/28,
`yarn gates` "all CI-tier gates green" (`logs/e2/fe-test-1.txt`, `fe-gates-1.txt`).
