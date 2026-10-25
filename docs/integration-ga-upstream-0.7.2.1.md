# Galactic Adventures fork integration

Fork identity: `0.7.3.0-ga-preview.7`, protocol 10.

## Source ancestry

| Input | Commit | Scope |
|---|---|---|
| Upstream SporeCoop 0.7.2.1 | `eb63c11133b9d2474cfc942ac18566edbaf7c4f1` | Latest fork base: Space features, GOG/Steam recognition, frame-loop support and SporeKit 0.3.4 diagnostics |
| Galactic Adventures preview 6 | `b69164aad62cb678500555dd33a8d8c67ea4199f` | Complete linear preview 1-6 history, including native Windows support, performance fixes and regression tools |
| Common base | `7765a74` | Upstream SporeCoop 0.6.0 |

The integration preserves both histories. Fork downloads, update checks and new
problem reports use [jwhans0n/SporeCoopGA](https://github.com/jwhans0n/SporeCoopGA).
Historical upstream reports and attribution retain their original links.

## Compatibility boundaries

Base-stage game recognition retains upstream GOG 2016, GOG 2024, Steam DRM and
verified same-code handling. The native GA adapter additionally requires the
actual exact-file GOG SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The GA notes call this March 2017 after the ModAPI address map; upstream calls
this same executable GOG 2024. Steam recognition does not enable the GA adapter.
No Steam adventure validation is claimed.

Protocol 10 retains the GA wire and resource-bundle formats while carrying the
upstream base-stage features. Every player should use this exact fork build;
upstream protocol 7 clients cannot join. Historical preview 6 tests remain
historical evidence for that build, not automatic integration acceptance.

## Build and release integration

The upstream Space map used `M_PI`, which is not supplied by the selected
LLVM-MinGW 20261020 headers. The integration uses C++20 `std::numbers::pi` in
that expression, without changing its geometry. The full build must pass after
this portability correction and the fork diagnostic-link changes.

The GitHub Actions build retains upstream Ubuntu 22.04 and its pinned LLVM-MinGW
20261006 toolchain. It builds the full project, runs the portable tests and
creates local release artifacts. The Win32 ABI executables require a Windows
runner or local execution; Linux portable tests do not validate the native
`SkHost` layout or game hooks. The merged SDK/core retain SporeKit 0.3.4.

For a future hyphenated preview tag, the release workflow passes `--prerelease`
to GitHub. A regular numeric tag retains the regular release behavior. This
integration creates no tag and publishes no release. Diagnostic report text and
its archive README direct new reports to the fork's issue tracker.

The package script copies build binaries into its stage before stripping the
copies. Packaging must preserve hashes of the native test binaries and record
separate packaged hashes; a stripped file has a different whole-file hash.
The package audit compares PE code sections and every update-pack payload with
the staged zip, including version, symbol maps and settings-preservation flags.

## Validation record

Record integration results with exact commands, source/binary hashes, artifacts
and failures. A completed build, unit test or static native ABI check does not
establish a complete adventure playthrough.

| Gate | Integration result |
|---|---|
| Full loader/SporeKit/coop/resource/setup build | Pending |
| Portable network, adventure, asset, runtime, flow, UI and knowledge tests | Pending |
| Thirteen Win32 native-adapter ABI fixtures | Pending |
| Installed dc04 symbol verification | Pending |
| Fork package contents, version and release/update destinations | Pending |
| Hidden, muted two-player native preparation, cancellation and Replay | Pending |
| Representative native interaction and static/FPS regressions | Pending |

Native checks use isolated lab profiles and a private installation. They must not
change the player's running save or restart personal sessions. Preserve the
upstream lab GPU-load limits and the Windows lab isolation rules in
[testing](06-testing.md).

No tag or published release is part of this integration. GA previews should be
installed from their matching zip/local pack; automatic suffix ordering and
GitHub latest-release discovery do not cover prereleases.
