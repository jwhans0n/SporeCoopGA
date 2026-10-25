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
u16  proto   protocol version (10)
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

## 4.8 Adventure stationary actors (protocol 10, preview 6)

Adventure resource bundle formats are unchanged. Protocol 10 adds stationary
actor updates and native interaction messages under `MSG_ADVENTURE`; older
previews fail version negotiation. This section specifies the stationary actor
subkind. Other adventure messages retain their own preparation, event and
movement-generation fields.

The host still samples world actors at 10 Hz. Creatures, vehicles, unknown native
classes, timed bombs, grenades, thrown objects and held items continue on the
existing unreliable `Actors=128` full-snapshot path. A verified building,
ornament or interactive ornament can use reliable `StaticActors=136`: send its
complete baseline once per peer, then send only changed gameplay fields. A
moving candidate is promoted for the rest of its act/incarnation, receives one
reliable full correction and resumes 10 Hz full snapshots. A stopped promoted
object remains dynamic so later packets can repair a lost movement update.

All fields below are little endian. `run` is encoded as low then high `u32`.

```text
u8   subkind = 136
u64  run
u32  world sequence
u8   act
u8   count (0..12)
repeat count times:
    u32 marker
    u32 incarnation
    u8  fields (0 = gameplay only, 1 = stationary baseline, 2 = promotion)
    u32 noun
    if fields != 0:
        f32 position[3]
        f32 orientation[4]
    f32 health
    f32 effective maximum health
    f32 authored/native maximum tuning
    u8  flags
    u8  party relationship (0..7, FF = unavailable)
```

The header is 15 bytes, a gameplay record 27 bytes and a full record 55 bytes.
A single gameplay update therefore uses 42 application bytes, a single baseline
70 bytes and a full 12-record packet 675 bytes. Flags retain combatant bit 0,
dead bit 1, impressed bit 2 and powerup kind in bits 4..7; bit 3 remains reserved
and is rejected. These fields do not encode a mine fuse, boost duration or
teleport outcome. Those interactions require their validated native event paths.

The sender retains the latest sampled state and the last successfully queued
state for each peer/identity. It queues at most four packets per peer per tick,
stopping when that peer already has 64 pending reliable messages. Unqueued
changes coalesce to the latest value. A per-peer rotating identity cursor gives
later baselines and changes a turn even when earlier identities change every
tick. A failed queue operation does not advance that peer's sent-state shadow.
The transport's reliable ordered delivery supplies the baseline-before-change
guarantee; there is no second application acknowledgement protocol. Peer join or
leave clears that peer's shadow and cursor, including when a player id is reused.

Receivers accept only the host and the active run. They validate a complete
packet before merging any records, reject nonfinite or invalid health/state
values, and ignore unknown gameplay-only identities. Per-actor sequence checks
merge both subkinds into one full cached state, retaining the baseline transform
when a gameplay-only record arrives. Older reliable state cannot rewind a newer
dynamic snapshot. The cached full state also reconstructs a missing native
object. Complete reliable rosters and retired-identity tombstones still govern
removal; a new incarnation gets a new baseline.

Future-act reliable baselines are staged separately, up to 2048 identities in
each of eight act slots, then merged after native `SetCurrentAct`. Past-act
packets are dropped. This assumes normal forward act progression; a diagnostic
backward act jump within the same run does not establish a fresh baseline
generation. Replay starts a new run. Run initialization happens before the first
valid current-run world packet is cached, so a baseline received immediately
after commit survives the first world tick.

`coop.adventure status` reports stationary/dynamic actor counts, bytes, full
baselines, gameplay updates, promotions and deferred sends. These byte counters
count outgoing application payloads per recipient, excluding transport headers,
UDP/IP overhead and retransmissions. Use transport byte counters to compare
actual network traffic over equal time windows.

## 4.9 Adventure explosion presentation (protocol 10, preview 6)

Reliable `MSG_ADVENTURE` subkind 146 (`Detonated`) includes the host's actual
native effect descriptor. This permits exactly-once presentation even when a
short-lived explosive dies before its guest-side actor binding exists. The
little-endian payload is:

```text
u8   subkind = 146
u64  run
u8   act
u64  event serial
u32  marker
u32  incarnation
u8   player (FF for the host detonation event)
u8   powerup kind (6, 10, 13 or 15)
f32  effect position[3]
f32  effect orientation[4]
f32  effect scale
u32  effect id
```

The common event header is 28 bytes; the captured presentation trailer adds 36
bytes, for 64 application bytes. Host native `ExplodePowerup` invokes its
verified effect helper before radius damage/destruction. The mod captures those
exact arguments and publishes them, retaining authored effect selection, pose
and scale. The event supplies no guest-authored damage, radius or fuse values.

A guest with the exact host-identity binding runs native visual cleanup with
damage paths gated and substitutes the captured presentation. A guest without
that binding invokes only the verified effect helper once and tombstones the
host identity. It does not destroy an arbitrary same-marker object, which could
be a respawner or a later incarnation. Complete reliable rosters perform normal
safe local sibling cleanup after their required host identities bind. Repeated
events cannot replay the effect or consume a later binding. Nonfinite/out-of-
bounds presentation values, stale runs and past acts are rejected.

Retirement initializes the world run before recording the exact tombstone, even
if this routed event precedes every actor packet and the first world tick. Both
full snapshot formats and future-act merges reject that identity afterwards;
newer incarnations at the same marker remain eligible.

## 4.10 Preparation messages during native cleanup

Every incoming `MSG_ADVENTURE` enters the coordinator before asset/native
dispatch. When a client receives a valid newer Prepare while native cleanup is
pending, it retains that packet and subsequent host adventure packets in order.
The next coordinator tick after cleanup initializes the new descriptor and asset
receiver before forwarding its content manifest and captain transfer messages.
This includes native subkinds 192+, which previously bypassed the coordinator.

The queue is bounded at 512 packets and 2 MiB. Overflow retains the validated first
Prepare, initializes that new run and cancels it explicitly, preserving the
party connection. Abort/reset discard pending transfers. Native cleanup remains
mandatory even if a later reset invalidates ordinary attempt callbacks. No
resource bundle or wire format changes are needed for this ordering barrier.

## 4.11 Unanimous adventure pause (protocol 12)

Local user/menu pause intent is independent of Intro/Talk confirmation. Two
reliable `MSG_ADVENTURE` subkinds carry the new policy:

```text
u8   subkind = 164       // participant -> host
u64  run
u64  local intent serial
u8   paused             // exactly 0 or 1

u8   subkind = 165       // host -> participants
u64  run
u64  snapshot revision
u8   participant count  // 1..4, unique IDs, includes host and recipient
repeat count:
    u8   player
    u64  accepted local intent serial
    u8   paused         // exactly 0 or 1; unknown serial 0 cannot be paused
```

The host accepts increasing intent serials only from a connected eligible member
of the frozen run roster. Unknown participants begin unpaused. The host coalesces
all changes into one latest snapshot per tick; there is no event backlog or
per-frame unchanged publication. A hold requires every current connected run
participant to be paused. One Resume clears it, and disconnected/recycled IDs
cannot retain pause authority.

Guests accept increasing revisions only from the host, in the current run. IDs
must belong to the frozen launch roster, but the host's complete snapshot owns
the live subset. It need not match the guest Session's locally observed subset:
the authoritative pause snapshot can arrive before its peer-leave notification.
The recipient must also see its current local intent serial acknowledged before
entering a shared hold. Local Resume ends its hold immediately, preventing an
old all-paused snapshot from re-pausing a later menu visit.

Validated current-run pause packets can initialize this small state before the
first native frame, so keepalive's earlier Running commit cannot acknowledge and
lose an initial intent. Old runs and wrong-source/kind messages cannot initiate
that state. Host aggregate lookup is cached for native creature update filters.

Native pause recalculation and puppet history resets are coalesced outside-frame
work with generation invalidation. Reset/cancellation discards pending state.
The existing PlayerState and adventure/captain resource bundle formats remain
unchanged. Pause pose admission retains health/identity fields and sender-time
ordering while rejecting spatial changes during the hold; interpolation resets
preserve the separate authoritative teleport-generation fence.
