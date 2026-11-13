"""Measure sustained native browser FPS on an already-owned private pair.

No launches, deployment, profile edits or document mutation. --fix-state off is
an explicit negative control, restored to on in finally. Below-threshold FPS,
lost readiness, changed draft/history or observer mismatch is a failure.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/tests'),str(ROOT/'tools')]
from sporectl import Console
from coop_adventure import FixtureError
from coop_adventure_editor import status
from coop_editor_party import Ownership,parse_context
from coop_editor_creations import CreationGame
from coop_editor_creation_save import canonical_row,history_rows


def frame(text):
    match=re.search(r'^frame=(\d+) fps=([0-9.]+)\b',text,re.M)
    if not match:raise FixtureError('Missing native Present frame counter')
    return int(match[1])


def run(args):
    args.output.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.candidate)
    games=[CreationGame(i,args.output,60,time.monotonic()+180,[1024,768])for i in args.instances]
    result=dict(passed=False,candidate=args.candidate,fix_state=args.fix_state,samples=[])
    def stable():
        states=[status(g)for g in games]
        if any(not s['ready']or s['frozen']or s['phase']!=4 or s['gesture']for s in states):raise FixtureError('Native scene is not idle and ready')
        if any(parse_context(s['text'])!=context for s in states):raise FixtureError('Project/epoch changed')
        if any(canonical_row(g.asset_text())!=canonical for g in games)or history_rows(games[0].call('coop.editor history'))!=history:raise FixtureError('Browser changed canonical draft/history')
        checks=[g.call('coop.editor check').strip()for g in games]
        if not checks[0].startswith('CONVERGED ')or len(set(checks))!=1:raise FixtureError('Native observer scenes differ')
        return checks
    def fix(state):
        for g in games:
            command='coop.fix editor.browser_idle '+state
            reply=Console.call(g.console,command)
            if not reply['ok']or reply['rc']:raise FixtureError('Browser fix switch rejected')
    try:
        owner.verify_windows();context=parse_context(status(games[0])['text'])
        canonical=canonical_row(games[0].asset_text());history=history_rows(games[0].call('coop.editor history'))
        result.update(context=context,canonical=canonical,history=history)
        stable();fix(args.fix_state)
        for tab,x in [('creatures',50),('buildings',136),('creatures-return',50)]:
            for g in games:g.click(x,40);g.call('wait.frames 3 15000')
            time.sleep(3)
            before=stable();started=time.monotonic();initial=[frame(g.call('info'))for g in games]
            time.sleep(args.seconds)
            final=[frame(g.call('info'))for g in games];elapsed=time.monotonic()-started
            fps=[(end-start)/elapsed for start,end in zip(initial,final)]
            row=dict(tab=tab,seconds=elapsed,frames_before=initial,frames_after=final,fps=fps,native_before=before,native_after=stable())
            result['samples'].append(row)
            (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
            if min(fps)<args.minimum_fps:raise FixtureError(f'{tab}: sustained native FPS {fps} below {args.minimum_fps}')
        for g in games:
            if 'total faults: 0'not in g.call('exc'):raise FixtureError('Native fault occurred')
        result['passed']=True
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        try:fix('on')
        finally:
            (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
            for g in games:g.save_log()
            owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--self-test',action='store_true')
    p.add_argument('--metadata',type=Path);p.add_argument('--candidate');p.add_argument('--instances',nargs=2,type=int,default=[45,46])
    p.add_argument('--output',type=Path);p.add_argument('--fix-state',choices=('on','off'),default='on');p.add_argument('--seconds',type=float,default=5);p.add_argument('--minimum-fps',type=float,default=8)
    a=p.parse_args()
    if a.self_test:
        assert frame('frame=17 fps=2.0 device=1\n')==17
        try:frame('not ready')
        except FixtureError:pass
        else:raise AssertionError('Missing frame counter accepted')
        print('Native FPS receipt guards passed; no game connections');return
    if not a.execute:print('Prepared only; exact private pair and candidate required.');return
    if not a.metadata or not a.output or not re.fullmatch('[0-9a-f]{64}',a.candidate or'')or not 3<=a.seconds<=15 or not 1<=a.minimum_fps<=60 or not a.output.resolve().is_relative_to((ROOT/'out').resolve()):p.error('Explicit owned pair, candidate, bounded FPS window and new OUT output required')
    run(a)


if __name__=='__main__':main()
