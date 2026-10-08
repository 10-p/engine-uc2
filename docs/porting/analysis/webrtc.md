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
