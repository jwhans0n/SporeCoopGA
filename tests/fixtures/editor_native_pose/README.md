# Genuine Native pose oracle

These are exact owned snapshots from the closed, private C33 two-player test
project `b0dc93c2d39651170b01a584e26119e4`. They were reconstructed offline from
the retained checkpoint/journal with validated project, epoch, revision and
prior-digest chains. Root verified the six retained Store files twice before
retirement and again after both processes closed; every byte was unchanged.
No personal profile or live save was edited to create these fixtures.

Both roles' R48..62 Native snapshots are byte-identical. The 15 deduplicated
images retain all 28 consecutive author/observer cases. `manifest.json` records
every source image size/SHA and each original role/revision pair. The test
compares whole output vectors to the actual next image, tests the complete
inverse, and demonstrates that Position-only differs from every actual AFTER.
Four transitions change compressed length. Slot 7 is logical object
4294967303, generation 1; every other decoded byte remains unchanged.

`roster.bin` contains the full logical ID/generation, dense serialized slot,
native class index and all ten fixed native pose/companion field payloads.
Full IDs and advertised fields came from an independent core State decoder;
unadvertised destination defaults are copied exactly from genuine BASE native
fields, without assigning semantic presence. `edits.bin` contains the 14
targets from the independently decoded typed AFTER documents. These values
are fixtures, never authority for a production Native caller.

`refpack_golden.h` retains 85 scalar compressed-output hashes for deterministic
size/pattern and command/distance boundaries, including 16,000,000-byte inputs.
The same generated inputs and frozen scalar implementation were independently
compared byte-for-byte to the original supported encoder in the guarded OUT
checkpoint. Repository tests use no extracted executable code. Three additional
cases compare the genuine R48 compressed bytes directly; one tests repeatable
alternate-window output. All 89 inputs have read-only, trailing guard pages.

Original receipts: `out/preview9-c34-pose-format/receipt.json`,
`container-receipt.json`, `typed-manifest-receipt.json`,
`scalar-guarded-receipt.json`; independent review:
`out/preview9-c34-pose-format-review/receipt.json`. OUT paths are retained lab
evidence, not build dependencies. The fixtures total under 1 MiB.

This corpus proves format reproduction for the recorded inputs. It does not
mint NativeRead/Write fixed-point provenance, authorize induction, qualify a
Native integration or establish a performance improvement.
