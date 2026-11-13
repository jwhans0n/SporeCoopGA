"""Repeated native Creature cancellation/return regression (BUG-0641).

Requires an already shared private hidden/muted Windows pair. Inert without
--execute. Uses native controls and read-only request/observer verification.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tools/tests')]
from coop_adventure import FixtureError
from coop_adventure_editor import status
from coop_editor_party import Ownership,parse_context
from coop_editor_creations import CreationGame
from coop_editor_creation_save import asset_snapshot,history_rows,cancel_quit_modal,request_errors

CANCEL=0x90439D7C


def return_checks(states,checks,context):
    if any(s['frozen'] for s in states):raise FixtureError('Creation return entered recovery')
    if any(parse_context(s['text'])!=context for s in states):raise FixtureError('Creation return replaced the shared context')
    return all(s['ready'] for s in states) and len(set(checks))==1 and checks[0].startswith('CONVERGED ')


def run(args):
    args.output.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.candidate)
    meta=json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    pins={row['instance']:row['pid'] for row in meta['processes']}
    games=[CreationGame(i,args.output,60,time.monotonic()+300,[1024,768]) for i in args.instances]
    author=next(g for g in games if g.instance==args.author)
    result=dict(passed=False,candidate=args.candidate,author=args.author,processes=pins,returns=[])
    def sample(label):
        owner.verify_windows();path=args.output/(label+'-native.json')
        command=[sys.executable,str(ROOT/'tools/tests/coop_editor_creation_validation.py'),'--execute',
            '--metadata',str(args.metadata),'--candidate',args.candidate,'--participant',
            f'{args.author}:{pins[args.author]}:creature','--output',str(path),'--exit-confirmation']
        done=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=20)
        (args.output/(label+'-reader.txt')).write_text(done.stdout+done.stderr)
        if done.returncode:raise FixtureError('Owned native request could not be verified')
        row=json.loads(path.read_text())['rows'][0]
        if request_errors(row,'creature'):raise FixtureError('Native request is not a fresh Creature creation')
        return row
    try:
        owner.verify_windows();context=parse_context(status(games[0])['text'])
        def converged():
            states=[status(g) for g in games]
            checks=[g.call('coop.editor check').strip() for g in games]
            return checks if return_checks(states,checks,context) else None
        games[0].wait('initial actual native convergence',converged)
        inventory=asset_snapshot(games[0]);history=history_rows(games[0].call('coop.editor history'))
        result.update(context=context,before_assets=inventory,before_history=history)
        requests=set()
        for index in range(args.repeat):
            label=f'return-{index+1}'
            author.native_create('creature')
            author.wait('owned native creature entry',lambda:'phase=1' in author.call('coop.editor creations') and 'entered=1' in author.call('coop.editor creations'))
            proof=sample(label);serial=author.call('coop.editor creations')
            # Native allocators may reuse an address; the creation serial must
            # still identify a distinct participant-local session.
            match=re.search(r'\bserial=(\d+)\b',serial)
            if not match or match[1] in requests:raise FixtureError('Creation session serial was reused')
            requests.add(match[1]);author.shot(label+'-entered')
            author.click_control(CANCEL);confirmed=False
            def returned():
                nonlocal confirmed
                states=[status(g) for g in games]
                if any(s['frozen'] for s in states):raise FixtureError('Native return entered recovery')
                if 'active=0' in author.call('coop.editor creations'):return states[args.instances.index(args.author)]['ready']
                if not confirmed:
                    modal=cancel_quit_modal(author.call('coop.ui tree 7'))
                    if modal:
                        fresh=sample(label+'-confirmation')
                        if fresh['request']!=proof['request']:raise FixtureError('Cancel request identity changed')
                        if cancel_quit_modal(author.call('coop.ui tree 7'))!=modal:raise FixtureError('Cancel dialog changed')
                        author.click(*modal['point']);confirmed=True
                return False
            author.wait('native cancellation and canonical return',returned,timeout=70)
            checks=games[0].wait('actual observer return convergence',converged)
            if any(asset_snapshot(g)!=inventory or history_rows(g.call('coop.editor history'))!=history for g in games):
                raise FixtureError('Cancellation changed canonical assets or history')
            # Canonical readiness can precede the final native fade-in. Retain
            # visual evidence after rendered frames, not the transition frame.
            author.call('wait.frames 25 15000')
            author.shot(label+'-returned')
            result['returns'].append(dict(serial=int(match[1]),request=proof['request'],checks=checks,states=[status(g) for g in games]))
        for g in games:
            if 'total faults: 0' not in g.call('exc'):raise FixtureError('Native fault occurred')
        result['passed']=True
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        for g in games:g.save_log()
        owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--self-test',action='store_true');p.add_argument('--metadata',type=Path)
    p.add_argument('--candidate');p.add_argument('--instances',nargs=2,type=int,default=[47,48])
    p.add_argument('--author',type=int);p.add_argument('--repeat',type=int,default=2);p.add_argument('--output',type=Path)
    a=p.parse_args()
    if a.self_test:
        context=('a'*32,1);state=dict(frozen=False,ready=True,text='project='+context[0]+' epoch=1')
        assert return_checks([state,state],['CONVERGED a','CONVERGED a'],context)
        assert not return_checks([state,dict(state,ready=False)],['CONVERGED a']*2,context)
        assert not return_checks([state,state],['CONVERGED a','CONVERGED b'],context)
        for bad in (dict(state,frozen=True),dict(state,text='project='+'b'*32+' epoch=1')):
            try:return_checks([state,bad],['CONVERGED a']*2,context)
            except FixtureError:pass
            else:raise AssertionError('Unsafe creation return accepted')
        print('Creation return observer/context guards passed; no game connections');return
    if not a.execute:print('Prepared only; requires an owned private pair and exact candidate.');return
    if not a.metadata or not a.output or a.author not in a.instances or not 2<=a.repeat<=4 or not re.fullmatch('[0-9a-f]{64}',a.candidate or '') or not a.output.resolve().is_relative_to((ROOT/'out').resolve()):
        p.error('Exact private metadata, candidate, author, 2-4 repetitions and new OUT output required')
    run(a)


if __name__=='__main__':main()
