#!/usr/bin/env python3
"""Native collaboration regression on an already owned hidden lab party.

Normal UI input and read-only diagnostics only. The runner owns deployment,
processes and isolated profiles. This fixture never writes native/save memory.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from coop_editor_party import Ownership,PublicGame,parse_history
from coop_adventure_editor import EditorPair,status
from coop_editor_preview4 import locate_cast
from uifind import match_groups,LINE
from windows_lab import json_write


def cast_cards(game,name):
    cards=set()
    for group in match_groups(game.console,name):
        for index,line in enumerate(group):
            if 'id=0x04A1CB70' not in line:continue
            nodes=[LINE.match(value)for value in group[index:]]
            if not nodes or not all(nodes):continue
            values=[tuple(int(node.group(i),16 if i==1 else 10)for i in range(1,6))for node in nodes]
            if not all(v[0]&1 for v in values):continue
            _,x1,y1,x2,y2=values[0]
            x=sum(v[1]for v in values[1:])+x1;y=sum(v[2]for v in values[1:])+y1
            if 0<=x<310 and 100<=y<570:cards.add((x,y,x+x2-x1,y+y2-y1))
    return sorted(cards,key=lambda p:(p[1],p[0]))


def open_properties(game,name,tab=50):
    if inspector_open(game):game.click_control(0x0595E0A8)
    if not cast_cards(game,name):game.click(tab,42)
    rect=game.wait('visible cast '+name,lambda:cast_cards(game,name))[0]
    x1,y1,x2,y2=rect
    game.call(f'move {(x1+x2)//2} {(y1+y2)//2}');game.call('wait.frames 8 15000')
    game.click(x2-10,y1+18);game.call('wait.frames 10 15000')
    game.wait('class ownership for '+name,lambda:'lease scope=Class ' in game.call('coop.editor leases'))
    return rect


def render_positions(game):
    result={}
    text=game.call('coop.editor visuals')
    for line in text.splitlines():
        match=re.search(r'placement=(\d+:\d+) render=([^ ]+)(?: exit=([^ ]+))?',line)
        if match:
            result[match[1]]={'position':tuple(map(float,match[2].split(',')))}
            if match[3]:result[match[1]]['destination']=tuple(map(float,match[3].split(',')))
    return result,text


def close(a,b):return sum((x-y)**2 for x,y in zip(a,b))<.0001
def inspector_open(game):
    control=game.control(0x0595E0A8)
    return bool(control and control[4])


def run(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256)
    games=[PublicGame(i,out,70,time.monotonic()+1200)for i in args.instances]
    host=games[0];pair=object.__new__(EditorPair)
    result={'passed':False,'dll_sha256':args.dll_sha256,'checks':[]}
    def converged(revision=None):
        states=[status(g)for g in games]
        if any(s.get('frozen')for s in states):raise RuntimeError('Frozen editor: '+repr(states))
        if any(s.get('phase')!=4 or s.get('gesture')for s in states):return False
        if revision is not None and any(s.get('revision')!=revision for s in states):return False
        hashes=[g.call('coop.editor check').strip()for g in games]
        return hashes[0].startswith('CONVERGED ')and len(set(hashes))==1
    def record(label):
        owner.verify_windows();rows=[]
        for g in games:
            g.record(label)
            rows.append(dict(instance=g.instance,state=status(g),visuals=g.call('coop.editor visuals'),leases=g.call('coop.editor leases'),history=g.call('coop.editor history'),faults=g.call('exc')))
        result['checks'].append(dict(label=label,participants=rows));json_write(out/'result.json',result)
        print(label,flush=True)
    try:
        for g in games:
            g.call('coop.uistate 0')
            if inspector_open(g):g.click_control(0x0595E0A8)
        host.wait('shared baseline',converged)
        if args.stage in ('properties','all'):
            for keeper,editor in ((games[0],games[1]),(games[1],games[0])):
                open_properties(keeper,'Ace')
                revision=status(host)['revision']
                open_properties(editor,'Hivehat')
                record('independent-property-viewers-'+str(editor.instance))
                editor.click(260,310)
                host.wait('unrelated cast property edit',lambda:converged(revision+1))
                if not inspector_open(keeper):raise RuntimeError('Remote cast edit closed the independent inspector')
                record('unrelated-property-edit-'+str(editor.instance))
                EditorPair.hotkey(editor,'z');host.wait('unrelated property Undo',lambda:converged(revision+2))
                if inspector_open(editor):editor.click_control(0x0595E0A8)
                if not inspector_open(keeper):open_properties(keeper,'Ace')
                cards=cast_cards(editor,'Hivehat');x1,y1,x2,y2=cards[0]
                editor.call(f'move {(x1+x2)//2} {(y1+y2)//2}');editor.call('wait.frames 10 15000');editor.click_control(0x07B6B2CD)
                host.wait('unrelated cast removal',lambda:converged(revision+3))
                record('unrelated-cast-removal-'+str(editor.instance))
                EditorPair.hotkey(editor,'z');host.wait('unrelated cast removal Undo',lambda:converged(revision+4))
                # This private roster has its empty Add card at (240,605).
                # 04A1CB6D on a hovered populated card opens Replace instead.
                editor.click(240,605);editor.wait_caption('Choose one or more Creatures')
                editor.press('Maxis');editor.call('wait.frames 25 15000');editor.choose_card('Ace')
                host.wait('unrelated native import',lambda:converged(revision+5))
                record('unrelated-import-'+str(editor.instance))
                EditorPair.hotkey(editor,'z');host.wait('unrelated import Undo',lambda:converged(revision+6))
                if inspector_open(keeper):keeper.click_control(0x0595E0A8)
        if args.stage in ('teleporters','live','all'):
            for mover in games[:2]:
                for endpoint in ('position','destination'):
                    target=locate_cast(mover,219,'Teleporter')
                    for _ in range(8):
                        row=pair.objects(mover)[target];point=row.get('screen'if endpoint=='position'else'destination_screen')
                        if point and 365<point[0]<820 and 140<point[1]<520:break
                        mover.call('wheel 820 400 -1');mover.call('wait.frames 25 15000')
                    if not point or not 345<point[0]<850 or not 100<point[1]<550:raise RuntimeError('Endpoint outside input area')
                    revision=status(host)['revision'];history=host.call('coop.editor history');canonical=row[endpoint]
                    x,y=map(round,point);tx,ty=x+35,y+15
                    try:
                        mover.call(f'move {x} {y}');mover.call('wait.frames 5 15000')
                        mover.call(f'button {x} {y} down');mover.call('wait.frames 10 15000');time.sleep(.2)
                        for fraction in (.35,.7,1):
                            mover.call(f'move {round(x+(tx-x)*fraction)} {round(y+(ty-y)*fraction)}');mover.call('wait.frames 8 15000');time.sleep(.1)
                        native=render_positions(mover)[0][target][endpoint]
                        if close(native,canonical):raise RuntimeError('Native drag did not move the selected endpoint')
                        def live():
                            for g in games:
                                if g is mover:continue
                                rows,_=render_positions(g)
                                if target not in rows or endpoint not in rows[target]or not close(rows[target][endpoint],native):return False
                            return True
                        host.wait('reciprocal native render poses before release',live,timeout=10)
                        if status(host)['revision']!=revision or host.call('coop.editor history')!=history:raise RuntimeError('Live preview created a premature transaction')
                        record('live-'+endpoint+'-'+str(mover.instance))
                    finally:mover.call(f'button {tx} {ty} up')
                    host.wait('one completed native drag',lambda:converged(revision+1))
                    if len(parse_history(host.call('coop.editor history')))!=len(parse_history(history))+1:raise RuntimeError('Drag created duplicate Undo entries')
                    expected=pair.objects(mover)[target][endpoint]
                    for g in games:
                        if not close(render_positions(g)[0][target][endpoint],expected):raise RuntimeError('Committed endpoint display requires a local click')
                    record('committed-'+endpoint+'-'+str(mover.instance))
                    EditorPair.hotkey(mover,'z');host.wait('native drag Undo',lambda:converged(revision+2))
                    for g in games:
                        if not close(render_positions(g)[0][target][endpoint],canonical):raise RuntimeError('Undo left a stale native visual')
        if args.stage in ('placement','live','all'):
            for mover in games[:2]:
                locate_cast(mover,50,'Ace')
                revision=status(host)['revision'];history=host.call('coop.editor history');before=pair.objects(mover)
                def ghosts():
                    values=[]
                    for g in games:
                        if g is mover:continue
                        text=render_positions(g)[1]
                        match=re.search(r'ghost=\d+:\d+ class=\d+ render=([^ ]+)',text)
                        if not match:return False
                        values.append(tuple(map(float,match[1].strip().split(','))))
                    return values if all(close(values[0],v)for v in values)else False
                try:
                    mover.call('move 88 165');mover.call('button 88 165 down');mover.call('wait.frames 10 15000');time.sleep(.2)
                    mover.call('move 601 470');mover.call('wait.frames 15 15000');time.sleep(.3)
                    first=host.wait('native placement ghosts before commit',ghosts,timeout=12)
                    if status(host)['revision']!=revision or host.call('coop.editor history')!=history:raise RuntimeError('Placement preview authored a transaction')
                    mover.call('move 630 470');mover.call('wait.frames 15 15000')
                    host.wait('native placement ghosts follow the cursor',lambda:(v:=ghosts())and not close(v[0],first[0]),timeout=12)
                    record('live-placement-'+str(mover.instance))
                finally:mover.call('button 630 470 up')
                host.wait('placement completion',lambda:converged(revision+1))
                host.wait('temporary ghost cleanup',lambda:all('ghost='not in render_positions(g)[1]for g in games))
                if len(pair.objects(mover))!=len(before)+1:raise RuntimeError('Native placement was not canonically allocated')
                if len(parse_history(host.call('coop.editor history')))!=len(parse_history(history))+1:raise RuntimeError('Placement created duplicate history')
                for key,delta in (('z',2),('y',3),('z',4)):
                    EditorPair.hotkey(mover,key);host.wait('placement history '+key,lambda:converged(revision+delta))
                    if any('ghost='in render_positions(g)[1]for g in games):raise RuntimeError('History resurrected a temporary preview actor')
                record('placement-undo-redo-'+str(mover.instance))
        result['passed']=True;json_write(out/'result.json',result)
    except Exception as exc:
        result['error']=repr(exc)
        try:record('failure')
        except Exception as secondary:result['record_error']=repr(secondary)
        json_write(out/'result.json',result);raise
    finally:
        for g in games:g.save_log()
        owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--metadata',type=Path);p.add_argument('--dll-sha256')
    p.add_argument('--instances',type=int,nargs='+',default=[91,92]);p.add_argument('--output',type=Path)
    p.add_argument('--stage',choices=('properties','teleporters','placement','live','all'),default='all')
    a=p.parse_args()
    if not a.execute:print(json.dumps({'prepared':True,'launches_games':False}));return
    if not a.metadata or not a.output or not re.fullmatch('[0-9a-f]{64}',a.dll_sha256 or '')or not 2<=len(a.instances)<=4:p.error('Exact owned metadata, DLL hash, output and 2-4 instances required')
    run(a)

if __name__=='__main__':main()
