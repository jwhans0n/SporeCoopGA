#!/usr/bin/env python3
"""BUG-1275: ticket history verdicts use bounded real-Git batches, not a process per ticket.

--tool selects an old tickets.py for the failing performance twin. No network or project refs
are changed: the history, annotated tags and ambiguous commit objects live in a temporary repo.
"""
import argparse
import hashlib
import importlib.util
from pathlib import Path
import re
import subprocess
import tempfile
import time
from types import SimpleNamespace


def load(path):
    spec = importlib.util.spec_from_file_location('ticket_history_under_test', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class LegacyGit:
    """The pre-batching Git queries, retained as a real Git oracle for unchanged verdicts."""

    def __init__(self, root):
        self.root = root
        self._resolve, self._anc, self._tag = {}, {}, {}
        self.calls = 0

    def _run(self, *args):
        self.calls += 1
        return subprocess.run(['git', '-C', str(self.root), *args], capture_output=True, text=True)

    def resolve(self, h):
        if h not in self._resolve:
            r = self._run('rev-parse', '--verify', '--quiet', h + '^{commit}')
            self._resolve[h] = r.stdout.strip() if r.returncode == 0 else None
        return self._resolve[h]

    def is_ancestor(self, h):
        if h not in self._anc:
            self._anc[h] = self._run('merge-base', '--is-ancestor', h, 'HEAD').returncode == 0
        return self._anc[h]

    def first_tag(self, h):
        if h not in self._tag:
            tags = [tuple(int(x) for x in t[1:].split('.'))
                    for t in self._run('tag', '--contains', h).stdout.split()
                    if re.fullmatch(r'v\d+(?:\.\d+)+', t)]
            self._tag[h] = min(tags) if tags else None
        return self._tag[h]

    def grep(self, tid):
        return self._run('log', '--format=%h', f'--grep={tid}\\b', '-E', 'HEAD').stdout.split()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, default=Path(__file__).resolve().parents[1] / 'tickets.py')
    args = parser.parse_args()
    mod = load(args.tool)

    class CountedGit(mod.Git):
        def __init__(self, root):
            self.calls = 0
            super().__init__(str(root))

        def _run(self, *a, **kw):
            self.calls += 1
            return super()._run(*a, **kw)

    with tempfile.TemporaryDirectory(prefix='tickets-git-') as tmp:
        root = Path(tmp)

        def git(*a, data=None):
            return subprocess.run(['git', '-C', str(root), *a], input=None if data is None else data.encode(),
                                  capture_output=True, check=True).stdout.decode().strip()

        git('init', '-q', '--object-format=sha1')
        git('config', 'user.name', 'Ticket tests')
        git('config', 'user.email', 'ticket-tests@example.invalid')
        git('config', 'tag.gpgSign', 'false')
        git('config', 'core.abbrev', '9')
        tree = git('mktree', data='')

        def commit(msg, parent=None):
            return git('commit-tree', tree, *(['-p', parent] if parent else []), data=msg + '\n')

        base = commit('base BUG-0001')
        one = commit('first BUG-0002\n\nbody BUG-0003; BUG-0004x is not a match', base)
        two = commit('second BUG-0002\n\nxBUG-0005, BUG-0006_ and BUG-0007- punctuation\n'
                     'Locale-sensitive suffixes BUG-0014\u00e9 and BUG-0015\U0001f601', one)
        side = commit('unmerged BUG-0008', base)
        arm = commit('merged branch BUG-0016', base)
        merged = git('commit-tree', tree, '-p', two, '-p', arm, data='merge BUG-0017\n')
        # Numeric release order differs from lexical order and creation order. An off-branch
        # release contains base but not the later main commits. Both tag kinds must work.
        git('update-ref', 'refs/heads/main', merged)
        git('update-ref', 'refs/heads/merge-arm', arm)
        git('symbolic-ref', 'HEAD', 'refs/heads/main')
        git('tag', 'v0.7.10', two)
        git('tag', '-a', 'v0.7.9', one, '-m', 'annotated release')
        git('tag', '-a', 'v0.7.8', side, '-m', 'off-branch release')
        git('tag', '-a', 'v0.7.12', 'v0.7.9', '-m', 'nested annotated release')
        git('tag', 'v0.7.2-preview', two)
        git('tag', 'nonrelease', base)
        blob = git('hash-object', '-w', '--stdin', data='not a commit\n')
        git('tag', 'v0.1.0', blob)  # release-looking tags of noncommits do not contain commits
        tag = git('rev-parse', 'refs/tags/v0.7.9')
        unreachable = commit('unreachable BUG-0009')
        # One fast-import creates distinct fix commits for the scale fixture. This proves
        # object-name validation is batched as well as the per-ticket grep queries.
        stream = []
        for n in range(100, 200):
            message = f'fix BUG-{n:04}\n'
            stream.append(f'commit refs/heads/main\nmark :{n}\n'
                          f'committer Ticket <ticket@example.invalid> {n} +0000\n'
                          f'data {len(message.encode())}\n{message}'
                          f'from {merged if n == 100 else ":" + str(n - 1)}\ndeleteall\n\n')
        git('fast-import', '--quiet', data=''.join(stream))
        imported = git('log', '--format=%H', '-100', 'HEAD').splitlines()[::-1]

        # A genuine ambiguous seven-digit abbreviation, not a fake return value or missing
        # object: birthday-search commit bytes in memory, then ask Git to write both objects.
        seen = {}
        collision = None
        for i in range(300000):
            raw = (f'tree {tree}\nauthor Ticket <ticket@example.invalid> 1 +0000\n'
                   f'committer Ticket <ticket@example.invalid> 1 +0000\n\ncollision {i}\n')
            data = raw.encode()
            oid = hashlib.sha1(b'commit ' + str(len(data)).encode() + b'\0' + data).hexdigest()
            short = oid[:7]
            if short in seen:
                collision = short, seen[short], raw
                break
            seen[short] = raw
        assert collision, 'bounded birthday search must find a real seven-digit commit collision'
        for raw in collision[1:]:
            git('hash-object', '-t', 'commit', '-w', '--stdin', data=raw)
        ambiguous = collision[0]
        del seen

        cases = [
            ('BUG-0001', base[:7], '0.7.8'),
            ('BUG-0002', one[:10], '0.7.10'),
            ('BUG-0002', two, '0.7.10'),
            ('BUG-0003', one, 'unreleased'),
            ('BUG-0004', two, '0.7.10'),
            ('BUG-0005', two, '0.7.10'),
            ('BUG-0006', two, '0.7.10'),
            ('BUG-0007', two, '0.7.10'),
            ('BUG-0008', side, '0.7.10'),
            ('BUG-0009', unreachable, 'unreleased'),
            ('BUG-0010', 'f' * 40, 'unreleased'),
            ('BUG-0011', blob, 'unreleased'),
            ('BUG-0012', ambiguous, 'unreleased'),
            ('BUG-0013', tag, '0.7.9'),
            ('BUG-0014', two, '0.7.10'),
            ('BUG-0015', two, '0.7.10'),
            ('BUG-0016', arm, 'unreleased'),
            ('BUG-0017', merged, 'unreleased'),
        ]
        # The ticket count must not create one grep process per ticket, even when every ID
        # differs. Repeated and abbreviated hashes exercise batch deduplication as in the repo.
        cases += [(f'BUG-{n:04}', imported[n - 100] if n % 2 else imported[n - 100][:7],
                   'unreleased') for n in range(100, 200)]
        tickets = [dict(id=tid, status='fixed', fixed_in=version, commits=cs,
                        _path=f'{tid}.md', _private=False) for tid, cs, version in cases]

        def verdicts(cls):
            start = time.perf_counter()
            backend = cls(root)
            if hasattr(backend, 'prepare'):
                backend.prepare(c for t in tickets for c in mod.commits_of(t))
            ctx = SimpleNamespace(git=backend, notes=[], scope='new')
            result = [mod.rule_commits(t, ctx) for t in tickets]
            # grep order and contents, not only the rule's unordered extra-commit notes.
            greps = [backend.grep(f'BUG-{i:04}') for i in range(1, 10)]
            return backend, result, ctx.notes, greps, time.perf_counter() - start

        old, old_result, old_notes, old_greps, old_time = verdicts(LegacyGit)
        new, new_result, new_notes, new_greps, new_time = verdicts(CountedGit)
        assert new_result == old_result, (new_result, old_result)
        assert new_notes == old_notes, (new_notes, old_notes)
        assert new_greps == old_greps, (new_greps, old_greps)
        assert new.resolve(ambiguous) is None and new.resolve(unreachable) == unreachable
        assert not new.is_ancestor(unreachable) and not new.is_ancestor(side)
        assert new.first_tag(base) == (0, 7, 8) and new.first_tag(one) == (0, 7, 9)
        assert new.first_tag(two) == (0, 7, 10)
        print(f'same verdicts: {len(tickets)} tickets, ambiguous/unreachable/annotated/off-branch tags PASS')
        print(f'legacy: {old.calls} Git calls, {old_time:.3f}s; candidate: {new.calls} Git calls, {new_time:.3f}s')
        assert new.calls <= 7, f'history work must be batched, not scale with tickets ({new.calls} Git calls)'

        # A context represents one history snapshot. Retargeting HEAD and a release tag must
        # affect a new context, never mix old HEAD ancestry with new release membership.
        unprepared = CountedGit(root)
        later = commit('later BUG-0200', imported[-1])
        git('update-ref', 'refs/heads/main', later)
        git('tag', '-f', 'v0.7.10', side)
        git('tag', '-f', 'v0.7.1', later)
        assert not new.is_ancestor(later) and not new.grep('BUG-0200')
        assert new.first_tag(two) == (0, 7, 10)
        assert not unprepared.is_ancestor(later) and unprepared.first_tag(two) == (0, 7, 10)
        fresh = CountedGit(root)
        if hasattr(fresh, 'prepare'):
            fresh.prepare([later])
        assert fresh.is_ancestor(later) and fresh.first_tag(two) == (0, 7, 1)
        assert fresh.grep('BUG-0200')
        print('HEAD/tag snapshot binding and fresh-context refresh PASS')

        # Git permits raw commit objects containing NUL even though normal commit commands
        # refuse such messages. Compare the unusual-message fallback against Git itself.
        raw = (f'tree {tree}\nparent {later}\nauthor Ticket <ticket@example.invalid> 1 +0000\n'
               f'committer Ticket <ticket@example.invalid> 1 +0000\n\nBUG-0300\0BUG-0301\n')
        nul = git('hash-object', '-t', 'commit', '--literally', '-w', '--stdin', data=raw)
        git('update-ref', 'refs/heads/main', nul)
        raw_new, raw_old = CountedGit(root), LegacyGit(root)
        for tid in ('BUG-0300', 'BUG-0301', 'BUG-0200', 'BUG-0014', 'BUG-0015', 'BUG-0002.*'):
            assert raw_new.grep(tid) == raw_old.grep(tid), tid
        print('Unicode word-boundary and unusual raw-message/regex fallbacks PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
