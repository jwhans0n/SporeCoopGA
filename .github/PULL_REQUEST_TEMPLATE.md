## What and why

## Player report / issue
<!-- link, or "none" -->

## Root cause
<!-- what was observed (logs, runtime) and what is inferred -->

## Evidence
- New test: `tools/tests/...` (suite `...`)
  - FAILS without the fix: `out/lab/<run>/` (main @ <sha>, or `coop.fix <name> off`)
  - PASSES with it: `out/lab/<run>/`
- `tools/lab.py run smoke`: `out/lab/<run>/summary.md`
- `make test`: ALL OK

## Knowledge recorded
- [ ] `.sym` entries (GOG 2024, confidence word, how checked)
- [ ] `docs/re/<area>.md` section (unnumbered, anchor)
- [ ] `docs/06-testing.md` (test facts, pitfalls)
- [ ] `docs/known-issues.md` (removed, narrowed or added)
- [ ] `CHANGELOG.md` [Unreleased] (player-visible changes only)

## Checklist
- [ ] No raw game addresses in code (symbols by name)
- [ ] English, no em dash
- [ ] No player data (names, paths) from reports
- [ ] If an agent wrote this: the owner reviewed it before it is published
