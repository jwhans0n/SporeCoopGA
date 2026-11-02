# Exact Terraform proof across Save, export and recovery

This note describes the candidate native adapter, not completed native acceptance.
The installed C15 run still rejected the first real BasicTools append with an
ambiguous record identity. That separate copy/materialization-boundary failure
remains under investigation; these transition receipts do not weaken its checks.

## Why an explicit receipt is needed

Terraform proxy materialization has already been verified at `F35E10`: effect
helper `F325B0` and the model loop write derived position, OR Transform flag 4,
and increment the bookkeeping WORD at Transform+2. Exact owned input/output
maps preserve the authored bytes without a numeric tolerance. Ordinary read
proofs retain those maps only under the current logical and native owner.

Native Save `F44E80` rotates the working resource's external key at
`F44EEF..F44F19` before the successful callback. Save/export/recovery also hold
authoring while the ordinary mutation scope is unavailable. Neither source
rotation nor a new controller epoch may silently authorize an old map. Runtime
therefore supplies a completed operation or controller/barrier receipt; the
native adapter verifies fresh owners around each synchronous read or rebuild.

The resource key at ScenarioResource+8 is external envelope provenance. The
verified reflection descriptor `15AF500` omits +8/+C/+10; resource constructor
`F2E670` initializes those words to zero. Serialized recipe equality does not
prove source identity. The receipt checks source independently.

## Owned proof and allowed transitions

`TerrainTransitionTicket` owns its entry snapshot, complete rendered recipe,
exact materialization maps and identity values. Combined charged owned values
are bounded to 64 MiB. It never owns native objects. Retained address identities
are compared only against freshly resolved mode/data/resource/tool owners.
The synchronous caller callback is never retained or used as mutation authority.

Every receipt names its exact before ticket, operation serial, logical project,
epoch, participant generation, native generation and actual scene source.

* Save requires the same owner, scope and native generation, plus the completed
  native Save module generation/serial and exact source result. Both the current
  raw recipe and persisted saved recipe must equal the retained rendered recipe
  in full before owned saved bytes are normalized and strictly validated.
* Export preserves scope, owner and actual source. An explicit Close rebaseline
  may declare its verified new native generation; the runtime callback pins that
  exact result. Ordinary EndExport does not adopt an unexpected generation.
* Recovery may transfer a retained value proof to an explicitly accepted new
  controller epoch/participant generation. Actual native owners and the complete
  raw recipe remain checked before publication.
* `ExportRebuild` is a distinct non-null-ticket path, preserving the exact owner,
  source, scope, serial and native generation. Its accepted final barrier binds
  a verified latest canonical snapshot and dependencies. Unknown newer raw
  values are replaced through canonical Load, never normalized by inference.
* `RecoveryBootstrap` is a distinct null-ticket path available only to canonical
  recovery restore. It requires a newly pinned recovery/controller-start receipt
  and verified owned canonical bytes/dependencies. Null Save/export/ordinary
  resume is rejected.

Canonical rebuild brackets the existing verified native restore, Terraform Load
and `F35E10` under a stack-scoped read identity. It establishes a fresh exact map,
retires native allocations before its final owner fence, and returns a private
prepared ticket. Actual native readiness, strict canonical readback and accepted
controller attachment precede publication. The previous checkpoint remains the
recovery source if any stage fails. Mutation gates and leases are unchanged.

Resume attempts retire their ticket once. Runtime records successful resume
before camera/readiness retries. A successful resume can return its already
checked snapshot; no closing-scope recapture is needed. Failed output operations
preserve caller outputs. A failed canonical rebuild may leave the native scene
requiring the existing recovery safeguards and cannot publish a partial proof.

## Evidence and limits

The production-adapter i686 fixture currently passes 14,451 focused terrain
checks, including exact Save source rotation, saved/live raw mismatch, malformed
receipts, stale owners, changed project/epoch/participant/native generation,
callback denial/exception, callback-side owner replacement, one-use retirement,
allocation failure, the combined memory bound, explicit Export rebuild and null
Recovery bootstrap. It also changes native generation during the final native
Release and asserts that no prepared ticket or borrowed read identity survives.

This focused result does not establish running-game Save/reopen, export, cold
recovery or full-tab acceptance. Exact raw equality is deliberately conservative:
if native Save or asynchronous rebuilding changes a retained row, ordinary
transfer rejects. A fresh explicitly fenced canonical rebuild is required.

The first actual C15 append failure is retained in
`out/preview9-c15-terrain-comparison.json`. Its baseline has no duplicate record
identity groups. Ten old rows contain verified derived XYZ differences and
bookkeeping counters; the active raw vectors contain one new effect per author.
The frozen public inspect serializes the committed working resource, whereas
`capture_draft` exports the active Terraform property list into an owned parsed
copy. Those are different read boundaries. Transform assignment `537E10`
increments a destination counter rather than copying the source WORD, but the
actual property-export call chain still requires confirmation before any
copy-boundary normalization is added. No XYZ tolerance or guessed identity
matching is authorized by that observation.
