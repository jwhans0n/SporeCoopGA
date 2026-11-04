# 4. Netcode

## Fork and upstream protocol scope
<a id="netcode-protocol-scope"></a>

The installed C49 shared-editor preview 9 build uses protocol **23**. The source
being integrated with upstream 0.7.3.6 is a separate, **uninstalled protocol 25
candidate**; it rejects protocol 23 and 24 peers before editor messages. Upstream owns top-level
`MSG_NEST=52` and `MSG_PARTS=53`, so the candidate moves `MSG_ADVENTURE` to 61 and
`MSG_EDITOR` to 62, retaining `MSG_ACTIVITY=54` and upstream `MSG_DIAG=60`.
All participants need the same candidate before testing it together.

Protocol 25 requires Extended Complexity policy 2, whose Native adapter keeps
marker storage stable before binding actors. Known policy-1 capability values
remain decodable by updated clients, allowing NativeDefault edits with retired
or mismatched capability values; they never establish an Extended cohort, even
when every participant advertises policy 1. This is not executable compatibility
with protocol-24 clients. The authored Saved, checkpoint and journal formats are
unchanged; marker capacity is transient Native storage.

The published GA gameplay release 0.7.4.0 uses protocol 15. Sections marked
protocol 8, 10 or 12 below retain the historical preview contract and evidence.
The upstream transport and capability notes retain their protocol 7 context;
their older-peer compatibility claims do not permit a protocol 7, 15 or 23
client to join the integration candidate. Merging source does not update an
installed game or establish new native/platform acceptance.

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
u16  proto   protocol version (25; uninstalled upstream-integration candidate)
u32  conn    connection id: the guest picks a new random one for every join (protocol 6)
u16  seq     packet number (for ack)
u16  ack     last received seq
u128 ackbits bitmask of the previous 128 (2 x u64, protocol 5)
u8   count   number of messages
[messages: u8 type, u8 channel, u16 len, u16 rel_id (reliable only), payload]
```

Protocol 7 added the Space extensions. A packet with another header protocol is dropped before
the handshake; `REJ_VERSION` applies to a mismatched protocol inside an otherwise valid `HELLO`.

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
| `HELLO` | R | `u16` protocol version, `u32` exe build-id, `str8` nickname (no mod version: peers of the same protocol are not told apart) |
| `WELCOME` | R | `u8` your player id, `u32` world id (save hash), `u8` count, then `{u8 id, str8 name}` per player (no host tick) |
| `REJECT` | R | `u8` reason (1 version, 2 no free slots, 3 different exe), `str8` text |
| `PLAYER_JOIN/LEAVE` | R | JOIN `u8 id, str8 name`; LEAVE `u8 id, u8 reason` (no appearance: the look comes from `CREATION` and the species in the state) |
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
| `PING/PONG` (8, 9) | U | unused: nothing sends `PING` (the host still answers one with a `PONG`); the round trip in `coop status` and the lobby is `rtt_ms`, averaged from the packet acks |
| `ABILITY` (11) | - | reserved, never sent or handled |
| `BYE` | U | reason; sent in three packets on leave (protocol 6: a reliable one waited behind any lost earlier message) |

CORRECTION (2026-10-28, fix/w3-net, BUG-0144): the rows `HELLO`, `WELCOME`, `PLAYER_JOIN/LEAVE`
and `PING/PONG` described fields and messages the protocol never had (a mod version, a host tick,
an appearance, a PING sender); they now list what the `write()` methods in `net/protocol.h` send.

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
* ~ superseded (2026-10-28): No packets for 5 s → "connection unstable", 15 s → disconnect.
* ~ superseded (2026-10-28): `coop status` shows RTT, packet loss, and each player's state.
* CORRECTION (2026-10-28, fix/w3-net, BUG-0069): no packets for 30 s (`Session::timeout_ms`,
  session.h: the game can block that long while saving or loading) → disconnect ("connection to
  host lost (timeout)" on a guest, the peer dropped on the host). A guest without `WELCOME` after
  10 s gives up ("no answer from host (timeout)"); the host drops a peer it has not accepted after
  10 s. There is no "unstable" state. `coop status` prints per peer `rtt`, `last` (ms since the
  last packet), `sent`, `recv`, `acked`, `resend` and `pending` (reliable messages not acked
  yet), not a loss figure.
* The first packet of a connection has seq 1 (BUG-0030): a peer that has received nothing writes
  ack 0 with no ack bits, which acknowledges nothing (with a first packet of seq 0 it acknowledged
  the host's lost `WELCOME`, and the join timed out).

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

## Messages from other players (2026-10-27, fix/w1-net)
<a id="netcode-untrusted"></a>

Every handler treats a message from another player as untrusted: it reads the whole message and
checks it (`r.ok()` after the last field, counts and indices against what this machine has, sizes
against the buffer they describe, numbers finite and in range, player ids against the session)
before it changes anything, and a snapshot chunk that does not parse does not count for its cycle.
A message is used and passed on only when it fits the player it came from. Shared checks: `mods/coop/net/validate.h` (unit tests
`tests/validate_tests.cpp`); in game: `coop_net_robust.py`
([06-testing](06-testing.md#testing-net-robust)).

## Big messages, late states, clocks and ids (2026-10-28, fix/w2-net)
<a id="netcode-w2"></a>

* **Reliable messages above one packet are split** (BUG-0034). A reliable payload fits a packet up
  to 1163 bytes (1200 minus the 31-byte header and 6 bytes of message header). A bigger one is cut
  into pieces of that size by `Connection::send_reliable`, each its own reliable message with flag
  bit 2 ("more pieces follow") on all but the last; the receiver joins them in order (reliable
  delivery is ordered) and delivers one message. Limit `kMaxReliable` = 1 MB per message (a sender
  refuses more, a receiver drops what goes past it). Before, such a message was never sent, blocked
  every reliable message after it and made every flush send a full burst of empty packets (5000
  packets/s on a LAN). Old peers (protocol 7, no change of `kProtoVersion`) deliver the pieces as
  separate messages, which their parsers reject; they stalled on such a message anyway.
* **A guest ignores the state of a player not in its roster** (BUG-0109). The host relays a guest's
  state unreliably behind the frame's snapshot chunks, so the last state of a leaving guest could
  arrive after the reliable `PLAYER_LEAVE` and made a ghost partner (in Space a "partner" that
  disabled instead of killed and repaired the ship). `player_join` also drops an old entry of the
  id (its puppet, its snapshot buffer).
* **Clock offset over a window** (BUG-0133, `net::ClockOffset` in interp.h): local time minus sender
  time of the fastest snapshots, the minimum of 1 s buckets over the last 3 s. A faster snapshot is
  taken at once; a higher minimum (the latency rose) is followed at 0.25 s per second, so the render
  clock slows down and never jumps back. After +300 ms of latency puppets and NPC proxies
  interpolate again after ~3.4 s (before: 50 s of 20 Hz steps).
* **Ship and vehicle snapshots carry the host's time** (BUG-0122): a u32 `now_ms()` after the last
  entry of each `MSG_UFO_STATE` / `MSG_CIV_STATE` chunk (`net::snapshot_time` finds it by the entry
  size; older guests ignore it, older hosts do not send it and the guest falls back to the arrival
  time). `VelocityEstimate::update(p, now, sender_s)` divides by the host's interval and ages the
  snapshot in host time mapped by a `ClockOffset`. One packet 75 ms late: 0.1 units of error at
  100 u/s (before 22).
* **Net ids of host objects** (BUG-0154): `net::alloc_id` (net/ids.h) skips ids in use and 0, for
  ships (squadron.cpp), civ vehicles (civ.cpp, both places) and tribe members (tribe.cpp).
* **A player's creations leave with the player** (BUG-0076): `creations::on_player_leave` forgets the
  owner's bundles (host store, received pieces, applied keys), so the next player of the id is shown
  with its own creature or the world's species, and later joiners do not get the old one.
* Fix switches (tests): `coop.corefix big_reliable|state_roster|offset_window|sender_time|id_reuse|creations_leave 0|1`;
  unit tests: `net_tests --nofix` runs the wave 2 cases on the code before the fixes (they fail).

## Decoder fuzzing and the transport seam (2026-10-28, fix/w4-census)
<a id="netcode-fuzzing"></a>

`tests/fuzz_decoders.cpp` runs in `make test` (AddressSanitizer + UBSan, fixed seeds, about 3 s;
compiling it takes longer than running it). It feeds everything portable that parses bytes from
another player or from the internet:

* every `protocol.h` struct and the MSG_NPC_STATE batch: write/read round trip, every strict prefix
  rejected, 6000 mutations each (bit flips, interesting bytes, truncation, insertion, deletion,
  0xFFFF lengths); an accepted mutation must encode and decode to itself;
* `read_lobby` (both `fix` modes), `chunk_add` (MSG_CREATION pieces: shuffled, repeated, forged
  offsets and totals), `snapshot_time`, the UTF-8 helpers and `str8`/`str16`;
* RefPack, DBPF (`Package`, `index_info`/`parse_index`, `find_keys`, `metadata_names`, header
  fields aimed at), the update pack and the release JSON / SHA256SUMS parsers;
* random and mutated datagrams into `Connection::on_packet` and into a host and a guest `Session`.

The Session runs over `Session::set_transport()` (`net/transport.h`: the UDP socket by default, a
queue in the test) with `Session::clock` (nullptr: `now_ms()`), so no socket and no real time is
involved. `fuzz_decoders --seed S --iters N` runs longer; built with `-DFUZZ_LIBFUZZER
-fsanitize=fuzzer,address` (clang) the same file is a libFuzzer target (first byte picks the decoder).
Checked that it catches real mistakes: removing the `o + 3 > n` check of the RefPack 3-byte code or
the `r.left() < len` check of `on_packet` makes it fail with a heap-buffer-overflow.

Not covered: the `on_message` handlers of the game modules (tribe, civ, cell, space ...) read their
payloads next to game calls and do not build without the game; their common checks live in
`validate.h` (tests/validate_tests.cpp).

Found on the way: `Reader::raw(nullptr, 0)` (an empty payload read into an empty vector's `data()`)
passed nullptr to memcpy (undefined behaviour, harmless on the compilers we use): it skips the copy
now. A RefPack stream that lacks only its final bare stop code (0xFC) is accepted, because the
decoder stops at the declared size; that is intended (the data is complete).

## HELLO / WELCOME caps and the rejoin token (2026-10-28, fix/w5-net)
<a id="netcode-hello-caps"></a>

Protocol 7 stays. HELLO (`proto u16, build u32, name str8`) and WELCOME (`your_id u8, world u32,
players`) get an extension appended after their 0.7.2 fields, which every older reader ignores
(their `read()` never checked for trailing bytes): `u32 caps`, then the payload of each set bit in
ascending bit order (BUG-0219).

| bit | name | payload | meaning |
| --- | --- | --- | --- |
| 0 | `CAP_SPLIT` | none | joins split reliable messages (the BUG-0034 receiver) |
| 1 | `CAP_TOKEN` | `u64` | HELLO only: the rejoin token (BUG-0235); never in WELCOME or anything relayed |
| 2 | `CAP_VERSION` | HELLO: `str8`; WELCOME: `str8` + `u8 n` + n x (`u8 id`, `str8`) | the versions (`coop X, SporeKit Y, <game build> <exe hash 8>`) of the sender and, in WELCOME, of the players the host knows (0.7.4, feat/w7-trace; see #netcode-versions-masks) |

* A message without the extension reads as caps 0: 0.7.2.x, 0.7.3.0 and fakepeer. A short payload
  means "absent", not a failed read. Unknown bits and trailing bytes are ignored. A new field takes
  the next bit and appends its payload after those of the lower bits; never insert a field before
  the extension. `tests/fuzz_decoders.cpp` checks both shapes (`hello_welcome_ext`, and a prefix
  that still holds the base fields must be read).
* `Connection::peer_split` (false until the handshake) comes from the guest's HELLO on the host and
  from WELCOME on the guest. A reliable message above one packet (`Connection::kMaxSingle` = 1163
  bytes of payload) goes out in pieces only to such a peer; to an older one it is not sent and
  counted (`too_big_old`, a line `older peer N: too_big_old=.. frag_lost=..` after the session
  block of `coop.status`); pieces from a peer that never announced splitting are dropped
  (`frag_lost`). Chat lines to an older peer are cut between characters to `kMaxSingle - 3` bytes,
  per peer (the host's relay too). One notice names an older player at each join.
* What a mixed session loses (0.7.2.x / 0.7.3.0 with a newer version): chat lines above about 1160
  bytes arrive cut, a Creature species table of 42 or more records does not reach the older player.
  0.7.3.0 is "older" here too (it has no caps), although it could join pieces.
* Rejoin token: `mods/coop/rejoin.cpp`, `<AppData>\SporeCoop\rejoin-<instance>-<slot>.txt` in the
  profile's AppData (lanes: in the lane's bottle), the first slot the game can open exclusively, kept
  open for the process. A join sends the token in the file (a crash left it) or writes a fresh one;
  a clean leave, the session end and the unload empty it. The host's HELLO handler drops an accepted
  entry with the same token that has been silent for 2 s before the full check and before WELCOME
  (PLAYER_LEAVE(old) goes out before PLAYER_JOIN(new)). Without a token nobody is replaced.
  Switches: `coop.fix net.peer_caps`, `net.rejoin_token` (off = the 0.7.3.0 name rule of BUG-0209),
  `core.rejoin_name` (off = never replace), `pool.meals_trim`. `net_tests --off <peer_caps|rejoin_token|addr_label|update_paths>`
  turns one switch off.

## Peer versions and masked addresses in logs (2026-10-29, feat/w7-trace)
<a id="netcode-versions-masks"></a>

* `CAP_VERSION` (bit 2 of the HELLO / WELCOME caps): `Session::version` goes out in HELLO; the host
  keeps it per peer (`Peer::version`, `PlayerInfo::version`), sends its own and the known players'
  in WELCOME and appends the newcomer's to PLAYER_JOIN (`str8` after the 0.7.2 fields: older
  readers ignore it; an empty or missing one is "unknown (pre-0.7.4)"). A payload that does not
  read leaves the message readable without versions. Both sides log every partner's versions on a
  join (`net: player N 'X' joined from ...: coop ...`, `session: ...`), `coop.status` lists them
  (`version N 'X': ...`), the trace repeats them once a minute; a different SporeCoop or SporeKit
  version is a warning line, a different SporeCoop version also a notice in the chat. The exe build
  itself must match already (REJ_BUILD).
* Addresses: `Endpoint::masked()` / `net::mask_ip` (net/ipmask.h) for every log line, `coop.status`
  and the chat log that goes into problem reports: loopback as is, LAN / VPN ranges by class
  (`192.168.x.x:47810`, `10.x.x.x`, `172.N.x.x`, `100.64.x.x (VPN/CGNAT)`), a public address as
  `public #ABCD` (16 bits of FNV-1a: tells players apart, not where they are). A host name looked
  up for a join is logged by its length only. The full address stays in the session (and in the
  co-op window, which the player sees). Tests: `net_tests` `test_peer_versions`, `test_ip_mask`;
  `fuzz_decoders` PLAYER_JOIN with and without the version.

## Support diagnostics: MSG_DIAG (2026-10-29, feat/w7-diag)
<a id="netcode-diag"></a>

* `MSG_DIAG = 60`, `u8 kind` + payload (`net/diagmsg.h`): clock request / answer (guest <-> host,
  unreliable; the guest keeps the answer with the shortest round trip of the last 12), bug mark
  (any -> host -> all, reliable, `u8 origin, f64 session time, str8 name, str8 text`; the host sets
  origin and name), digest request / digest / result of the sync self-check (reliable; a Cell
  digest is ~6 KB: split for peers with `CAP_SPLIT`). An older SporeCoop drops the unknown type
  (host: `cb.custom` falls through; guest: `world::on_message` ignores it): no protocol bump.
* `ConnStats` gained `bytes_sent`, `bytes_recv`, `lost` (a sent packet that an ack 32 to 128
  packets later does not have; further back the ack bits say nothing, so a bulk transfer that
  outruns them counts nothing) and the round-trip samples of a window (`rtt_min/max/sum/n`,
  `take_window()`). `Session::conn_to(id)` gives the connection for the statistics.

## Local addresses and Wine's nsi (2026-10-29, fix/w8-adapters)
<a id="netcode-wine-nsi"></a>

What the co-op window's "Friends connect to" line and the LAN scan's subnet broadcast targets need
is the PC's IPv4 addresses with their prefix lengths: `GetAdaptersAddresses(AF_INET)`
(`mods/coop/adapters.cpp`, BUG-0286).

* ✔ Wine 10.0 (Bottles, lab lanes): two games of **one Wine prefix** that call
  `GetAdaptersAddresses` at the same moment crash. `coop.adapters stress 300` on both games of a
  lane at once (old synchronous path) crashed one within seconds (lab run 20261029-193811: heap
  fault in ntdll on the main thread); a worker lookup 4 ms after start-up, when host and guest
  started together, faulted in ucrtbase memcpy in 3 of 3 cold starts of the creature fixture.
* ✔ Mechanism (Wine 10.0 source, `dlls/nsi/nsi.c` `NsiEnumerateObjectsAllParametersEx`): after
  the `IOCTL_NSIPROXY_WINE_ENUMERATE_ALL` call it takes `params->count = *(DWORD *)out` and
  copies `count * size` bytes for each table without checking it against the count it asked for.
  When the reply's count is garbage (observed: 0xC8F00180 bytes for one table), memcpy runs off the
  malloc'd `out` buffer: a fault on the first read when ucrt copies backwards (destination above
  the source, the player crash: nothing written), or a heap overwrite first when it copies forwards
  (the later heap faults). Why the reply is garbage when two processes ask together is not known
  (? the shared nsiproxy.sys in the prefix's winedevice.exe).
* ✔ Negative: two threads of **one** game looking up together (GetAdaptersAddresses AF_INET and
  AF_UNSPEC, GetAdaptersInfo, gethostbyname of the own host name), 1500 times: no fault. 1000
  serial lookups in one game while sessions open and close: no fault.
* ✔ A per-prefix named mutex (`Local\SporeCoop.nsi`) around each lookup was **not enough**: with
  the two games taking turns, `coop_adapters.py` still lost a game twice in a row (lab runs
  20261029-194630: a fault in d3d9.dll on a render thread, no report; 20261029-195441: a silent
  exit), so the garbage reply is not only about two requests at the same instant. Not analysed
  further.
* The mod's rule (`mods/coop/adapters.cpp`): never on the main thread, never at start, at most
  every 5 min when the window or the LAN scan asks. **Under Wine** (ntdll exports
  `wine_get_version`) it never calls iphlpapi: the address a UDP socket connected to 8.8.8.8 /
  192.168.255.254 / 10.255.255.254 would send from (`connect` on UDP sends nothing) plus
  `getaddrinfo(gethostname())` (Wine 10.0 resolves that on the Unix side; only `gethostbyname` of
  the own name goes through `GetAdaptersInfo`). No prefix length there: a private address gets a
  /24 subnet broadcast. On Windows `GetAdaptersAddresses` with the `ERROR_BUFFER_OVERFLOW` retry,
  under the mutex. Both run under a fault guard (a vectored handler sends the faulting worker to
  a landing function that releases the mutex and ends the thread; the list keeps its last value
  and no lookup runs again in that game). Other nsi users in the game are not covered: Wine's
  own ws2_32 (`gethostbyname` of the own host name, `SIO_ROUTING_INTERFACE_QUERY`), winhttp's
  proxy detection.
