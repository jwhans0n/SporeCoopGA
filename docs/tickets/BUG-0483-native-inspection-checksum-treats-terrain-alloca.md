---
id: BUG-0483
title: Native inspection checksum treats terrain allocation state as authored Undo data
status: open
kind: tooling
stage: GA native fixtures
reported: 2026-11-04
source: C67 native Model Clone Undo checksum
role: observer
versions: private preview9 C67
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature:
docs:
related: BUG-0479
---

## Report
C67 Model Clone161 and Undo162 restore both complete native byte images to d751b52b3f8305f175042f883f61883490d85deda53d198f637502cdf0e05015, with strict canonical convergence and zero faults. The observer's document_semantic_sha256 nevertheless changes from be6d525f8e21a93bbd8c1cb66193335a520d48b610339fb41a553968f8aa951d to 660c93bca791b9ab2fc6b18a92aa9a5991212dee3e16672199dc498ab397e9d0. Evidence: out/preview9-c67-host-model-clone/result.json. The old fixture treats this as authored restoration failure.

## Root cause
Observed source: the inspection hash serializes next_id and next_terrain_order. Core intentionally retains those high-water marks through structural Undo to prevent identity reuse. Native projection caches can retain an older otherwise-identical projection; its terrain identity match does not compare allocator marks. Actual C68 Undo174 confirms host allocators55834575134/order286 versus observer55834575135/order287, while authored checksums are identical. The full checksum differs solely because of those two fields.

## Fix
Retain the original full projection checksum and print its two allocation counters. Add an authored projection checksum that fixes only these two counters to UINT64_MAX-1 in a private copy. All actual object identities, orders, values, references and native bytes remain covered and unchanged. Structural fixtures must still assert complete native SHA restoration, exact added/removed columns, canonical convergence and one history entry. Actual C68 host and guest Model/Effect clone histories pass.

## Verification
FAIL: actual C67 artifact above. PASS: out/preview9-c68-host-model-clone/result.json and out/preview9-c68-guest-model-clone/result.json, with complete native/authored semantic restoration through Undo/Redo/Undo. Before/after authored hashes differ on the genuine append and return exactly on Undo. Both allocator values are retained for each capture. This changes diagnostics and their interpretation, not Undo, allocation or validation policy.

## History
- 2026-11-04: opened.
