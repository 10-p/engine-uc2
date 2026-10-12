# Netplay: UC2's System Link browser on the web (the player-driven network E2E)

Branch `netplay`, worktree `<WorkUC2>/engine-uc2-netplay` (from main aa0f54e). Frontend: `<WorkUC2>/ufrontend-netplay`
branch `uc2-netplay` (its notebook `docs/migration/notes/uc2-player-net-e2e.md` holds the spec side). Logs
`<WorkUC2>/logs/netplay/`. Newest step at the bottom.

Task (owner, 2026-10-12): a network E2E driven only as a player drives it: host through "Networked Combat > Host
System Link Game", guest through "Join System Link Game", the guest finding the host on that page ("the end goal is
that we have to see the servers here in the peerjs room"), a local PeerServer as the broker.

## Step 0 (2026-10-12): why the page is empty on the web, and the plan

- UC2's "Join System Link Game" page is `UCMenu.UC2Browser`; its only source is `IpDrv.ClientBeaconReceiver`, which
  binds `BeaconPort` (9777), broadcasts `REPORT?<nonce>` to `ServerBeaconPort` (8777) every second and keeps the
  replies `<BeaconProduct> <port> ?BeaconText=<text>?Nonce=<nonce>` for `BeaconTimeout` (5 s); the server half is
  `IpDrv.UdpBeacon`, spawned by `UnrealMPGameInfo` on a listen server, replying with `Level.Game.GetBeaconText()`
  (`<Epic>/UCMenu/Classes/UC2Browser.uc:40-51, 323-345, 487-`, `<Epic>/IpDrv/Classes/ClientBeaconReceiver.uc:70,
  131, 165-`, `<Epic>/IpDrv/Classes/UdpBeacon.uc:35, 51-70`, `<Epic>/Engine/Classes/GameInfo.uc:994`).
- On the web `AUdpLink::execBindPort` fails at `socket()` (`Source/IpDrv/Src/UdpLink.cpp:46-104`; E-2 logged
  `UdpBeacon ... BindPort: socket failed` on every listen host), so neither half exists and the page never lists
  anything. E-2 left it out on purpose (`webrtc.md` "Not done": in-game discovery).
- The glue has the discovery (the broker's `/peerjs/peers`, room-scoped, plus a status ping per host answered with
  what the host's driver pushed through `webrtc_set_listen_status`), and the engine reads it with the v1 ABI's
  `webrtc_discover_servers` / `webrtc_is_discovery_done` / `webrtc_get_server_*`. UC2 never called them.

Plan (the UE1 siblings' answer to the same problem, read-only reference
`WorkUnreal/engine-ut99/Source/WebRTCDrv/WebRTCLan.cpp` and its `IpDrv/Src/UdpLink.cpp` hooks; engine-ut2004 does the
address part in `WebRTCDiscovery.cpp`):

1. A synthetic LAN in WebRTCDrv (web only), reached from `AUdpLink` through a small hook table that IpDrv owns and
   WebRTCDrv fills in (so the web `ucc-bin.js`, which links IpDrv but not WebRTCDrv, keeps Epic's socket path and
   links without it): web `BindPort` registers the link, `SendText` gives the datagram to the LAN, `Tick` delivers
   the LAN's replies through Epic's `eventReceivedText`. Script unchanged.
2. A `REPORT?<nonce>` from the receiver is answered once per server the glue discovered, from a synthetic address
   `10.199.0.N` (append-only room table), port 7777, with the host's beacon text and the receiver's nonce. Proxied
   `webrtc_*` calls only from a link's tick (never from inside script) and at most four times a second (WebRTCLan's
   two measured failure modes: a hang from a proxied call inside script, a proxy-queue overflow from per-frame calls).
3. The host's driver pushes its real beacon text in the status row (the row's game-type field; open question for the
   owner whether a field of its own, i.e. an optional ABI module, is wanted instead).
4. `UWebRTCNetDriver::InitConnect` translates a synthetic address back to the room key (the join from the page is a
   travel to `10.199.0.N:7777?...`).
5. Native: unchanged (all of it `__EMSCRIPTEN__`); proved by the native build and `tools/dev/net-test.py`.

## Step 1 (2026-10-12): the synthetic LAN (code), builds

- `Source/IpDrv/Inc/UdpLinkWeb.h` (new): `FUdpLinkWebHooks` {Bind, Unbind, Send, NextReply} and `GUdpLinkWebHooks`,
  owned by IpDrv, NULL unless WebRTCDrv installs it. A table rather than direct calls because the web `ucc-bin.js`
  links IpDrv without WebRTCDrv (Source/UCC/CMakeLists.txt): with direct calls its link would fail; with the table it
  keeps Epic's socket code.
- `Source/IpDrv/Src/UdpLink.cpp`, `__EMSCRIPTEN__` only and only when the table is set: `execBindPort` registers the
  link and returns the port asked for (or 40000+), `execSendText` hands the text to the LAN, `Tick` delivers the LAN's
  datagrams through Epic's `eventReceivedText/Line/Binary`, `PostScriptDestroyed` unbinds. Native: no change (the
  native build of this branch compiles none of it; `build-native` rc 0, 0 errors, `logs/netplay/build-native-0.log`).
- `Source/WebRTCDrv/Src/WebRTCLan.cpp` (new): the LAN. `Send` only RECORDS a `REPORT?<nonce>` (it runs inside the
  receiver's script Timer); the link's next `NextReply` (tick context) starts the glue's discovery (at most every 4 s:
  a broker request plus a WebRTC status ping per host), polls it (at most every 0.25 s) and queues one answer per
  discovered server: `<BeaconProduct> 7777 ?BeaconText=<beacon>?Nonce=<nonce>` from `10.199.0.<room index>`.
  `BeaconProduct` from the ini (`[IpDrv.UdpBeacon]`), else `Unreal(Avaris)` (UdpBeacon's default; the receiver
  compares case-insensitively). Beacon text = the row's game-type field when it holds a `|` (a UC2 host of this
  build), else built from the row's plain fields. Lines (DevNet): `[WebRTC-LAN] link bound on port N ...`,
  `[WebRTC-LAN] discovery started`, `[WebRTC-LAN] server '<name>' room=<key> map=<map> p/m <ping>ms -> 10.199.0.N
  beacon '<text>'` and `discovery done: N server(s)` (when the list changes), `[WebRTC-LAN] REPORT from the System
  Link page answered with N beacon(s): ...` (when N changes).
- `WebRTCNetDriver.cpp`: `PushListenStatus` sends `Game->eventGetBeaconText()` (once the GRI exists) as the row's
  game type and logs `[WebRTC] listen status: System Link beacon '<text>'` when it changes (so the host's log says
  when a guest's PRI arrives: the player count and names are in the text); `InitConnect` turns a `10.199.0.N` host
  back into the room key (`[WebRTC] InitConnect: System Link address 10.199.0.N -> room key '<key>'`).
- `WebRTCDrv.h`: `WebRTCLan_Install()` (called by `AUTO_INITIALIZE_REGISTRANTS_WEBRTCDRV`), `WebRTCLan_RoomForHost`;
  member `LastBeacon`. CMake: `Src/WebRTCLan.cpp`, include dir `Source/IpDrv/Inc` (header only).
- Builds: `build-native` (configure + `-j4`, `bin/capped 6G`) rc 0, 0 errors; `build-wasm-perf` (configure + `-j4`,
  `bin/capped 6G`) rc 0, 0 errors (`logs/netplay/build-wasm-perf-0.log`), SDLLaunch.wasm sha256
  30fb6d8644e0fc4ce430271510ad31bf2485c0bdf21a40be154d282cd4f23848; the module imports `webrtc_discover_servers`.

## Step 2 (2026-10-12): the browser runs (frontend spec `uc2-wasm-netplay.spec.js`)

- `host-0` (main's artifact 870c5115..., host phase only): every host step green except the beacon line (not in
  that build): Networked Combat > Host System Link Game > nag (Escape) > Enter x3 > Enter x3 > Enter (the last one
  answers "Begin Match? (A) yes") brought `CTF-Aqueduct` up (a fresh install's host page defaults to Capture the Flag);
  `[WebRTC] InitListen: room 'default' server name 'E2E Netplay Host'`, the glue registered
  `uc2-default-public-room_e2enetplayhost-srv`, the broker's `/peerjs/peers` listed exactly that id.
- `full-1` (this branch's artifact 30fb6d86...): host as above plus
  `[WebRTC] listen status: System Link beacon 'CTF|Liandri Corporation|CTF-Aqueduct|CTF_Custom|1|4|0||Anubis|0'`;
  guest: `[WebRTC-LAN] link bound on port 9777`, `discovery started`, first REPORT answered with 0, then
  `[WebRTC-LAN] server 'E2E Netplay Host' room=default-public-room_e2enetplayhost map=CTF-Aqueduct 1/4 32ms ->
  10.199.0.0 beacon '...'`, `discovery done: 1 server(s)`, `REPORT from the System Link page answered with 1
  beacon(s): '...' from 10.199.0.0:7777`. The page (screenshot `11-guest-system-link-listed.png`): JOIN SYSTEM LINK,
  one row "Liandri Corporation | pure | CTF | Aqueduct | 1 / 4", "1 MATCH FOUND", JOIN enabled. Stopped at the spec's
  own "1 Match Found" OCR check (the label is drawn dim; the row is now checked instead).
- Learned: without a profile a player is named after his CHARACTER (the beacon's names field: `Anubis`), not the
  launcher's name; the session name is the profile default `Liandri Corporation` (GRI ServerName set by
  PlayerProfile), while the launcher's server name only addresses the peer. "discovery started" is logged every 4 s
  while the page is open (noise; quietened at the next rebuild).

## Step 3 (2026-10-12): the whole player flow green; native regressions

- `discovery started` is now said once (it repeats every 4 s while the page asks; the list's changes are what is
  logged). wasm-perf rebuilt: rc 0, 0 errors (`logs/netplay/build-wasm-perf-1.log`), SDLLaunch.wasm sha256
  54ea768a9c16bc47a5499588704c507fc7fbb561603d65ab70d10feb3aaee60d.
- Browser runs (frontend `uc2-wasm-netplay.spec.js`; notebook `ufrontend-netplay/docs/migration/notes/uc2-player-net-e2e.md`):
  `full-2` (30fb6d86...) joined: guest `[WebRTC] InitConnect: System Link address 10.199.0.0 -> room key
  'default-public-room_e2enetplayhost'`, `Welcomed by server: LEVEL=CTF-Aqueduct GAME=UCGameplay.CTFGame LONE=0`; host
  `Join request: menu.unr?...?Class=UCCharacters.UCPawn_Jugg_Arclite?Melee=0?Name=NetGuest?...`, `Join succeeded:
  NetGuest`, beacon `CTF|Liandri Corporation|CTF-Aqueduct|CTF_Custom|2|4|0||Anubis;NetGuest|0` (red only on the spec's
  name expectation). `full-3` 1 passed (both scoreboards show Anubis and NetGuest; the guest's tab closed, the host
  dropped it by `Connection timed out after 60.000000 seconds`). `full-5` (54ea768a...) **1 passed (7.1 min)**: the
  same, and both players leave through the in-game menu (FORFEIT GAME): the host's beacon is back to `|1|4|...|Anubis|`
  at once, both pages back in the menus, no Critical/assertion/trap. Machine memory in use peaked at 9.6 GB (5.6 GB
  before the run); the run's cgroup ceiling 7G was not hit.
- Native: the `build-native` object of `UdpLink.cpp` (compiled after the edit) has no hook symbol; incremental native
  build rc 0. `NET_TREE=netplay-native tools/dev/net-test.sh dedicated1 listen travel`: 21 PASS, 2 FAIL
  (`listen.hit`, `listen.kill`) + leftovers PASS; main's own build (engine-uc2 aa0f54e, `NET_TREE=netplay-native-base
  ... listen`) fails the same two items the same way (`NetB health on the listen server: None`), the known flake noted
  in splitscreen.md/web-g10.md/profiles.md: not this branch. The web `ucc-bin.js` (IpDrv without WebRTCDrv) still links.
- Not done: a native proof of Epic's System Link listing (two native processes on loopback, the page listing the host
  through the real UdpBeacon). The page logs nothing about its rows (its debug lines are `DevBrian`, suppressed), so a
  native check needs the GUI driven by the virtual pad and a screenshot read per process: not cheap; Epic's native
  path is untouched (none of this compiles natively).
