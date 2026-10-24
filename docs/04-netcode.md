# 4. Netcode

## Adventure preparation (protocol 8)

Reliable MSG_ADVENTURE subkind14 carries the host's preparation view: run,
revision, readiness epoch, adventure/locked captain keys, loading flag and up to
four members. Each member includes selection serial, cosmetic stage/progress,
original captain key, validated content/captain flags and effective readiness.
Subkind15 requests local captain replacement;16 reports cosmetic preparation
progress. Existing resource bundle formats are unchanged.

Ready messages include the readiness epoch and selection serial. Captain
acknowledgements include the serial and must match the host's validated transfer.
Replacement increments the serial/epoch, invalidates the captain barrier and clears
party readiness before opening the picker. Neither stale acknowledgements nor
cosmetic progress can release the barrier. Snapshots are sent on changes at most
10Hz; clients accept only the host, current run/roster and increasing revision.
All participants explicitly confirm Ready. Older previews fail protocol negotiation.

## 4.1 Topology

Star: the host is the server, guests connect to it over UDP (port 47810).
Guests don't talk to each other; the host relays player states.

The netcode is portable C++ (`mods/coop/net/`) with no game dependencies:
it builds both as a Windows DLL and **natively on Linux** (unit tests, `fakepeer`).

## 4.2 Packet

```
u32  magic   'SPCO'
u16  proto   protocol version (6)
u32  conn    connection id: the guest picks a new random one for every join (protocol 6)
u16  seq     packet number (for ack)
u16  ack     last received seq
u128 ackbits bitmask of the previous 128 (2 x u64, protocol 5)
u8   count   number of messages
[messages: u8 type, u8 channel, u16 len, payload]
```

Reliability uses a "seq/ack/ackbits" scheme: each reliable message remembers which
packets it was sent in, and is resent until a packet is acknowledged (RTO ≈ 1.5·RTT, min. 100 ms).
The receiver buffers reliable messages that arrive ahead of a missing one; the sender never sends a
message more than `kRelWindow` (2048) past its oldest unacknowledged one, so that buffer cannot
overflow. (Before 2026-10-23 the buffer held 1024 and silently dropped further messages while still
acknowledging their packets: after one message was lost a few times in a row on a bad link, the
dropped ones were never resent and the world transfer stalled for good.)

A packet whose `conn` differs from the connection's own is ignored. The host takes the id of the
first packet from a new address; a packet from a known address with a new id is a new connection
(the guest left and joined again through a NAT or proxy that kept the mapping) and replaces the old
peer, and ids of dropped peers are remembered, so late or duplicated packets of an old connection
cannot recreate it or mix into the new one.

Channels:
* `UNRELIABLE`: states (snapshots), where the latest value matters more than delivery;
* `RELIABLE_ORDERED`: session events, abilities, damage, chat, file chunks.

Each frame a peer gets one flush: unreliable messages first (the own `PLAYER_STATE` at the front),
then due reliable ones. The ack field covers 128 packets (protocol 5), so a flush sends up to 128
packets on a short round trip (LAN, < 40 ms) and 42 on the internet - that paces the reliable bulk
(a 3.8 MB world: ~3 MB/s); while unreliable messages remain it goes on for up to 64 more packets,
then the rest of the unreliable queue is dropped (stale by the next frame). Big snapshots (cell pool: one ~900-byte chunk per 14 cells) must stay below that.

## 4.3 Messages

| Type | Channel | Contents |
|---|---|---|
| `HELLO` | R | mod version, exe build-id, nickname |
| `WELCOME` | R | player id, world id (save hash), host tick, player list |
| `REJECT` | R | reason (version, no free slots, different exe) |
| `PLAYER_JOIN/LEAVE` | R | id, nickname, appearance |
| `PLAYER_STATE` | U | id, tick, position (3×f32), orientation, velocity, movement flags, health, species, **mode** and **place** (protocol v2: a puppet is visible only in the same mode and place) |
| `ANIM` | R | animation played by the player's creature (jump, attack, social); the host relays it |
| `NPC_SPAWN` | R | net-id, species key (ResourceKey), scale, flags (dead / "family" / impressed), position, HP |
| `NPC_STATE` | U | 10 Hz, batches of ~25: net-id, position, orientation, velocity, HP, flags |
| `NPC_DESPAWN` | R | net-id: left the 110 m radius (+20 m hysteresis) or disappeared |
| `DAMAGE_REQ` | R | guest → host: my hit on an NPC (net-id, damage, type) |
| `DAMAGE` | R | host → guest: an NPC hit you (damage, type, attacker net-id) |
| `DNA` / `DNA_ADD` | R | shared DNA total from the host / gain earned by a guest (also the DNA a guest earned alone while the host was in the main menu) |
| `SPECIES` | R | host → guests: the creature stage's species relationship table (per species: relation, impressed, killed), on change and every 10 s; the guest writes it into its own and refreshes its goal cards |
| `SOCIAL` | R | guest → host: a social ability on a mirrored NPC (net-id, ability type); host → guest: rounds done / needed; host → all: the result (impressed or refused, reaction animation, DNA); any → host: a goal card's ally / extinction reward (species, outcome, DNA), paid once |
| `RES` / `RES_ADD` | R | shared resource (u8 id: 1 tribe food, 2 civilization money, 3 empire money; f64): total from the host / change on a guest |
| `PING_MARK` | R | "look here" marker: author id, point, mode; the host forwards it to the others |
| `WORLD_TIME` | U | once per second: quaternion of the sky object (time of day) |
| `WORLD_OFFER/FILE/CHUNK/DONE` | R | transfer of the host's save in 1 KB chunks, at most 160 unacknowledged messages (600 when the round trip is under 40 ms) |
| `CHAT` | R | id, UTF-8 text |
| `PING/PONG` | U | timestamp → RTT in the status |
| `BYE` | U | reason; sent in three packets on leave (protocol 6: a reliable one waited behind any lost earlier message) |

## 4.4 Rates and smoothing

* State sending: 20 Hz (every 50 ms), no more than once per frame.
* The receiver keeps a snapshot buffer and renders the puppet with a **100 ms** delay
  (linear position interpolation, orientation slerp); when data runs short it
  extrapolates by velocity for up to 250 ms, then "freezes".
* Divergence > 8 m → the puppet teleports, otherwise it smoothly catches up.
* In game, the puppet moves through the locomotion system (a "walk to point" command
  targeting the interpolated position), so the game plays the step animations itself;
  orientation/position are corrected directly at low speed.

## 4.5 Entity identifiers

* Players: `player_id` 0 = host, 1..3 = guests.
* NPCs (M2): the host assigns a `net_id` when an object first enters any guest's
  interest radius, and keeps an `object ↔ net_id` map. On the guest, its own NPCs are disabled
  (AI doesn't tick, objects are hidden), and puppets are created from `NPC_SPAWN` instead.

## 4.6 Session and timeouts

* Handshake: `HELLO` → `WELCOME`/`REJECT`; `HELLO` is resent like any reliable message for up to 10 s.
* Leave: `BYE`; the host drops the peer at once. A lost `BYE` costs nothing on a rejoin (the new
  connection id replaces the peer), otherwise the peer times out.
* No packets for 5 s → "connection unstable", 15 s → disconnect.
* `coop status` shows RTT, packet loss, and each player's state.

### Measured over netem (2026-10-23, tribe save of 3.8 MB, `coop_rejoin_net.py`, lane 2)

| Link | handshake (join to `together`) | world transfer (host "sharing" to guest "received") | rejoin until the resent world is loaded |
|---|---|---|---|
| plain (localhost) | 0.8 s | 1.0 s | 9.5 s |
| `mobile` (120 ± 60 ms, 3 % loss) | 0.8 s | 8 s | 17 s |
| `awful` (250 ± 120 ms, 10 % loss) | 1-2 s | 16 s | 25 s |

* The host saves for ~2.6 s before sending, the guest loads for ~4 s after receiving.
* Over a long round trip the transfer is window bound: 160 messages in flight (world.cpp), host
  rtt ~450 ms during the transfer on `awful`, ~330 chunks/s. A bigger window was tried in a
  simulation of `awful` (unit test `test_bulk_bad_link` without the hole): 240-600 messages cut the
  time to 7-10 s but 2-4x the resends were spurious (jitter of ± 120 ms reorders more packets than
  the 128-packet ack window covers, so their acks never come), and a cumulative "next reliable id"
  ack did not reduce them (most spurious resends are buffered behind a hole). Left at 160.

## 4.7 NAT

M1–M3: direct IP (UDP port forwarding on the host or a VPN network). Later: a rendezvous server
for UDP hole punching (a small service you can host yourself).
