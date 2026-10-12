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
