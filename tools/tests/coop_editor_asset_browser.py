#!/usr/bin/env python3
# suite: none (opt-in native Windows fixture requiring an already-owned hidden lab and explicit private inputs; automatic launcher qualification is tracked by BUG-1014)
"""Checked native card drop on an explicitly owned, already running private pair.

Uses ordinary input only. A failed invocation never retries a dispatched drop.
Creation entry/save uses coop_editor_creation_save.py and its native picker route.
"""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import FixtureError
from coop_adventure_editor import status, EditorPair
from coop_editor_creations import CreationGame, assets, verify_viewport
from coop_editor_party import Ownership, parse_context, parse_history

ROOT=Path(__file__).resolve().parents[2]
CARD=re.compile(r'^card class=(\d+:\d+) key=\S+ revision=\d+ rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)',re.M)


def semantic_rows(snapshot):
    # A checked Undo/Redo stamps new field revisions on restored values.
    return {key:{name:value for name,value in row.items()if name!='content_revision'}for key,row in snapshot['rows'].items()}


def run(args):
    args.output.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.candidate)
    games=[CreationGame(i,args.output,45,time.monotonic()+240,[1024,768])for i in args.instances]
    host=games[0];author=next(g for g in games if g.instance==args.author)
    result=dict(passed=False,candidate=args.candidate,source=args.source,target=args.target,dispatched=False)
    def write():
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def snapshot():
        samples=[assets(g.asset_text())for g in games]
        if samples[0]!=samples[1]or samples[0]['revision']!=samples[0]['durable']:return None
        return samples[0]
    def converged():
        checks=[g.call('coop.editor check').strip()for g in games]
        if not checks[0].startswith('CONVERGED ')or len(set(checks))!=1:raise FixtureError('Actual observer native state differs')
        return checks
    def settled(revision):
        states=[status(g)for g in games]
        if any(s['frozen']for s in states):raise FixtureError('Native editor entered recovery')
        if any(s['revision']>revision for s in states):raise FixtureError('Unexpected extra transaction')
        return all(s['revision']==s['durable']==revision and s.get('ready')for s in states)
    try:
        result['windows']=verify_viewport(owner,games,[1024,768])
        before=host.wait('settled canonical inventory',snapshot);result['before']=before
        context=parse_context(status(host)['text']);result['context']=list(context)
        if args.source not in before['rows']or args.target and args.target not in before['rows']:raise FixtureError('Exact source/target class is absent')
        result['native_before']=[g.call('coop.editor inspect')for g in games]
        history=parse_history(host.call('coop.editor history'));result['history_before']=history
        result['native_checks_before']=converged()
        kind=before['rows'][args.source]['key'][2]
        tab={0x2B978C46:50,0x2399BE55:136,0x24682294:178,0x476A98C7:178}.get(kind)
        if tab is None:raise FixtureError('Select an already displayed supported native palette')
        # Native tab clicks also collapse the current palette. Reopen it before
        # resolving the exact card; never drag against guessed hidden contents.
        author.click(tab,40)
        author.call('wait.frames 3 15000')
        if not author.control(0x52DF67AF):
            author.click(tab,40);author.call('wait.frames 3 15000')
        def cards():
            rows={m[1]:tuple(map(int,m.groups()[1:]))for m in CARD.finditer(author.call('coop.editor browser'))}
            return rows if args.source in rows and (not args.target or args.target in rows)else None
        visible=author.wait('exact current native cast cards',cards)
        box=visible[args.source];start=((box[0]+box[2])//2,(box[1]+box[3])//2)
        if args.target:
            box=visible[args.target];end=((box[0]+box[2])//2,(box[1]+box[3])//2)
        else:
            slots=re.findall(r'^empty rect=(-?\d+),(-?\d+),(-?\d+),(-?\d+)$',author.call('coop.editor browser'),re.M)
            if not slots:raise FixtureError('Fixture requires a verified current empty card')
            x1,y1,x2,y2=map(int,slots[0]);end=((x1+x2)//2,(y1+y2)//2)
        owner.verify_windows()
        if snapshot()!=before or parse_context(status(author)['text'])!=context:raise FixtureError('Drop context changed before input')
        result.update(start=start,end=end,dispatched=True,input='held'if args.held_drag else'scheduled');write()
        if args.held_drag:
            # A single normal press held through explicit frame fences keeps
            # the private virtual cursor alive during background frame gaps.
            EditorPair.slow_drag(author,start,end)
        else:author.call(f'drag {start[0]} {start[1]} {end[0]} {end[1]}')
        # Poll actual state instead of long frame fences in background-limited games.
        host.wait('exactly one durable card transaction',lambda:settled(before['revision']+1))
        after=host.wait('matching canonical inventory',snapshot);result['after']=after
        result['native_checks_after']=converged()
        changed=parse_history(host.call('coop.editor history'));result['history_after']=changed
        if len(changed)!=len(history)+1 or changed[:-1]!=history:raise FixtureError('Drop did not create one Undo item')
        if not args.target:
            added=set(after['rows'])-set(before['rows'])
            if len(added)!=1 or any(after['rows'][key]!=row for key,row in before['rows'].items()):raise FixtureError('Copy changed unrelated cast entries')
            if after['rows'][next(iter(added))]['key']!=before['rows'][args.source]['key']:raise FixtureError('Copied content differs')
        author.call('coop.editor undo');host.wait('card Undo',lambda:settled(before['revision']+2))
        result['undo']=host.wait('Undo inventory',snapshot);result['native_checks_undo']=converged()
        if semantic_rows(result['undo'])!=semantic_rows(before):raise FixtureError('Undo changed unrelated asset data')
        author.call('coop.editor redo');host.wait('card Redo',lambda:settled(before['revision']+3))
        result['redo']=host.wait('Redo inventory',snapshot);result['native_checks_redo']=converged()
        if semantic_rows(result['redo'])!=semantic_rows(after):raise FixtureError('Redo differs from accepted native result')
        result['native_after']=[g.call('coop.editor inspect')for g in games]
        for g in games:
            if 'total faults: 0'not in g.call('exc'):raise FixtureError('Native fault occurred')
            g.record('browser-drop-accepted')
        result['passed']=True
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        write()
        for g in games:g.save_log()
        owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--self-test',action='store_true');p.add_argument('--metadata',type=Path)
    p.add_argument('--candidate');p.add_argument('--instances',nargs=2,type=int,default=[45,46])
    p.add_argument('--author',type=int);p.add_argument('--source');p.add_argument('--target');p.add_argument('--output',type=Path)
    p.add_argument('--held-drag',action='store_true',help='One normal held press with native frame fences for slow background games')
    args=p.parse_args()
    if args.self_test:
        a={'rows':{'7:1':dict(content_revision=1,key=[1,2,3],behavior='a')}}
        b={'rows':{'7:1':dict(content_revision=9,key=[1,2,3],behavior='a')}}
        assert semantic_rows(a)==semantic_rows(b)
        b['rows']['7:1']['behavior']='b';assert semantic_rows(a)!=semantic_rows(b)
        assert CARD.search('card class=7:1 key=1!2.3 revision=2 rect=17,115,158,216 edit=1 owners=0')
        print('Native browser receipt/parser checks passed; no game connections');return
    if not args.execute:
        print('Prepared only. Requires exact private process metadata, candidate hash and source class.');return
    if not args.metadata or not args.output or args.author not in args.instances or not re.fullmatch('[0-9a-f]{64}',args.candidate or''):
        p.error('Explicit owned pair, candidate, author and new OUT output are required')
    if not re.fullmatch(r'[1-9]\d{0,19}:[1-9]\d{0,9}',args.source or'')or args.target and not re.fullmatch(r'[1-9]\d{0,19}:[1-9]\d{0,9}',args.target):
        p.error('Exact full source/target class identity required')
    if not args.output.resolve().is_relative_to((ROOT/'out').resolve()):p.error('New OUT directory required')
    run(args)


if __name__=='__main__':main()
