# Contributing to SporeCoop

Thanks for your interest! Bug reports: see [docs/REPORTING.md](docs/REPORTING.md). Talk to us on
the [SporeCoop Discord](https://discord.gg/7Yre7u4b8x).

For code and research:

1. Open an issue first for anything bigger than a small fix, so we can agree on the approach.
2. Read [CLAUDE.md](CLAUDE.md): it holds the project's rules for everybody (people and AI agents):
   knowledge first, checking game code before calling it, tests, git.
3. Every feature and every bug fix needs a test on two game instances
   ([docs/06-testing.md](docs/06-testing.md), [docs/10-dev-pipeline.md](docs/10-dev-pipeline.md)); a bug
   fix's test must fail without the fix.
4. Game functions are called by name, never by a raw address: add them to
   `data/symbols/overrides_dc04aee5.sym` (the GOG 2024 build) with a confidence word.
5. Write down what you learn about the game in `docs/re/<area>.md`.

Please don't:

* publish decompiled game code (our notes and our own code only);
* add support for unofficial or modified game builds;
* include other players' names or files from their reports in commits.

Everything public (code comments, docs, commits) is in English.
