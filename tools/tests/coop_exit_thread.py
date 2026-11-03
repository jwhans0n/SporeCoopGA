#!/usr/bin/env python3
"""The game ends with its own exit code when it closes through ExitProcess without
cSporeApp::Shutdown (the game's fault handler does that), BUG-0107: coop.dll's keepalive thread
was a joinable global std::thread, whose destructor at DLL detach called std::terminate -> abort.

One game of the lane: `coop.exittest` calls ExitProcess(0) on the main thread; an abort on the way
out writes "abort" into mods/logs/coop_exittest.txt (its SIGABRT handler).

  SPORE_LANE=2 python3 tools/tests/coop_exit_thread.py [--without-fix]
  (--without-fix: the old global std::thread, the test must FAIL)"""
import os
import sys
import time

import lane
from diag_check import LOGDIR, start_host
from twoinst import Checks, alive


def main():
    without = '--without-fix' in sys.argv
    ck = Checks()
    marker = os.path.join(LOGDIR, 'coop_exittest.txt')
    if os.path.exists(marker):
        os.remove(marker)
    c = start_host(ck)
    time.sleep(3)
    try:
        c.call('coop.exittest old' if without else 'coop.exittest')
    except Exception as e:  # noqa: BLE001  the game is gone before it answers
        print('  (no answer: the game exited)', type(e).__name__)
    gone = False
    for _ in range(30):
        if not alive(lane.HOST):
            gone = True
            break
        time.sleep(1)
    ck(gone, 'the game ended')
    text = open(marker).read() if os.path.exists(marker) else ''
    print('  marker:', text.strip().replace('\n', ' | ') or '(none)')
    ck(text.startswith('exit 0'), 'coop.exittest ran')
    ck('abort' not in text, 'no abort on the way out (std::terminate of a joinable std::thread)')
    lane.sh('kill', check=False)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
