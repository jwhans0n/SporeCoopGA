# SporeCoopGA Documentation

SporeCoopGA focuses on playing Galactic Adventures together. Shared Adventure
Editor placement support is under validation. Broader adventure semantics and
other Spore editors follow in the [roadmap](../README.md#roadmap).

## Galactic Adventures

- [Installation](../README.md#installation): get the matching fork build for every player.
- [Galactic Adventures co-op guide](GALACTIC-ADVENTURES.md): start from Sporepedia, select saved captains, ready up and play together.
- [0.7.4.0 release report](validation-ga-0.7.4.0.md): first published GA release, version ordering, packaging and validation limits.
- [Preview 12 validation report](validation-ga-preview.12.md): shared result authority, party menu return, Replay and current acceptance limits.
- [Preview 11 validation report](validation-ga-preview.11.md): authored player-team enemy targeting for guests, native evidence and current limits.
- [Preview 10 validation report](validation-ga-preview.10.md): native creature/vehicle followers, motion traces and current acceptance limits.
- [Preview 9 validation report](validation-ga-preview.9.md): NPC presentation, unanimous pause and six-metre revival checks.
- [Preview 8 validation report](validation-ga-preview.8.md): recorded music, item, confirmation, lighting and downed checks.
- [Preview 7 integration report](integration-ga-upstream-0.7.2.1.md): completed historical upstream merge validation.
- [Known issues](known-issues.md): recorded limitations and open problems.
- [Shared Adventure Editor development guide](MULTIPLAYER-ADVENTURE-EDITOR.md): placement collaboration under validation, native controls, saving and recovery. Separate from the published gameplay release.
- [Shared editor validation report](reports/EDITOR-IMPLEMENTATION-VALIDATION.md): automated evidence, isolated native runs and outstanding acceptance gates.
- [Shared editor testing playbook](EDITOR-TESTING-PLAYBOOK.md): focused development checks, integration checkpoints and opt-in overnight release acceptance.
- [Multiplayer Adventure Editor implementation plan](MULTIPLAYER-ADVENTURE-EDITOR-PLAN.md): acceptance stages and Undo rules; later shared history, independent acts and playtesting while editing.

The first published GA release **[`0.7.4.0`](https://github.com/jwhans0n/SporeCoopGA/releases/tag/v0.7.4.0)**
uses protocol 15 and remains experimental. It retains preview 12 gameplay,
including shared wins and losses, host Done for the party, and applied and accepted
results before Replay opens a shared lobby with fresh readiness.
See the release report for release checks and the preview 12 report for its native
acceptance evidence. Historical preview 11 two-player Mine
checks pass for guest/host targeting, natural damage and switching; broader
adventure compatibility remains unproven. Preview 10's locomotion results
retain material residual tracking lag; earlier results retain their version
labels. Ordinary guest combat input remains unverified.
Native GA support requires the exact GOG executable with SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Every participant needs Galactic Adventures and the same fork build. Steam and
Linux GA compatibility is not established by this evidence. See the guide and
validation report above before treating inherited platform support as GA support.

## Foundation documentation

These documents describe the inherited SporeCoop architecture, base-stage work
and development tools. Some build targets, platform results and gameplay notes
are historical; they do not expand the GA compatibility stated above.

1. [Architecture and loading into the game](01-architecture.md): process loading, lifecycle and symbol database.
2. [SporeKit and the mod toolkit](02-modkit.md): mod API, console, `sporectl`, hot reload, screenshots and input.
3. [Co-op gameplay](03-gameplay.md): shared-world rules and inherited stage behavior.
4. [Netcode](04-netcode.md): synchronization, interpolation and host authority.
5. [Reverse engineering](05-re-notes.md): methodology, native functions and structures.
6. [Testing](06-testing.md): isolated game instances, fake peers, Bottles and automation.
7. [Co-op in all modes](07-modes.md): mode adapters, shared resources and phase transitions.
8. [Interface](08-ui.md): native menus, connection window, lobby and LAN discovery.
9. [Playing together in all phases](09-coop-design.md): upstream design and engine findings in [re/](re/).
10. [Development pipeline](10-dev-pipeline.md): test lanes, fixtures, network testing and knowledge rules.

The [inherited installation guide](INSTALL.md) records base-stage setup and
historical GOG 2016/GOG 2024 platform testing. Use the fork installation entry
above for current GA download guidance.
