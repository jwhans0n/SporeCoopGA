#!/usr/bin/env python3
"""Measured ordinary Terraform strokes, simultaneously or while the host designs.

Never launches/deploys games or writes profiles. No connection opens without
--execute, exact private ownership and a measured1024x768 recipe. Only normal
mouse/key input and public diagnostics are used. An unmeasured recipe rejects.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import re
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair,status
from coop_editor_creations import input_steps,validate_steps,verify_viewport
from coop_editor_crew_terrain import TerrainConsole,native_planet,RECIPE_FIELDS
from coop_editor_party import Ownership,PublicGame,parse_context,parse_history


class TerrainPartyConsole(TerrainConsole):
    def call(self,line):
        if line in ('coop.editor assets','coop.editor creations'):
            if time.monotonic()>self.deadline:raise FixtureError('The bounded Terraform fixture expired')
            from sporectl import Console
            return Console.call(self,line)
        return super().call(line)


def canonical_header(text):
    # Town has more cast rows than a bounded public console response. Terrain
    # convergence needs the canonical whole-document digest, not every row.
    # A truncated row list must not invalidate its complete, checked header.
    match=re.match(r'^revision=(\d+) durable=(\d+) canonicaldocumenthash=([0-9a-f]{64})\n',text)
    if not match:raise FixtureError('Missing canonical terrain document header')
    return dict(revision=int(match[1]),durable=int(match[2]),hash=match[3])


def load_recipe(path,instances,mode):
    data=json.loads(path.read_text(encoding='utf-8-sig'))
    if data.get('schema')!=1 or data.get('viewport')!=[1024,768] or data.get('measured') is not True:
        raise ValueError('A measured schema1 recipe at1024x768 is required')
    validate_steps(data.get('setup'))
    for step in data['setup']:
        action,value=next(iter(step.items()))
        if action in ('click','drag') and any(coordinate>=[1024,768][i%2] for i,coordinate in enumerate(value)):
            raise ValueError('Terraform setup input leaves its measured viewport')
    strokes=data.get('strokes')
    if not isinstance(strokes,list) or len(strokes)!=(2 if mode=='simultaneous' else 1):raise ValueError('Expected two simultaneous authors or one host-away guest')
    authors=set()
    for row in strokes:
        if row.get('instance') not in instances or row['instance'] in authors or mode=='host-away' and row['instance']==instances[0]:raise ValueError('Invalid/distinct Terraform authors')
        authors.add(row['instance'])
        for name in ('source','target'):
            point=row.get(name)
            if not isinstance(point,list) or len(point)!=2 or any(type(v) is not int or not 0<=v<limit for v,limit in zip(point,[1024,768])):raise ValueError('Measured viewport stroke coordinates required')
        if row['source']==row['target']:raise ValueError('A Terraform stroke must move')
    return data


def run(args):
    recipe=load_recipe(args.recipe,args.instances,args.mode);args.output.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256);deadline=time.monotonic()+args.deadline
    games=[PublicGame(i,args.output,args.timeout,deadline) for i in args.instances]
    for game in games:game.console=TerrainPartyConsole(game.instance,deadline)
    by_id={game.instance:game for game in games};host=games[0];observer=next(g for g in games if g!=host)
    result=dict(passed=False,complete_acceptance=False,mode=args.mode,recipe=recipe,checks=[])
    held={};creation_serial=None;context=None
    def write():(args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def designing():
        match=re.search(r'creation_editor active=1 phase=1 serial=(\d+) stale=0 entered=1\b',host.call('coop.editor creations'))
        return int(match[1]) if match else None
    def canonical(revision,live=True):
        rows=[status(game) for game in games]
        if any(row.get('frozen') for row in rows):raise FixtureError('Terraform froze a participant')
        if context and any(parse_context(row['text'])!=context for row in rows):raise FixtureError('The Terraform context changed')
        if any(row.get('revision',0)>revision for row in rows):raise FixtureError('Unexpected duplicate Terraform transaction')
        if not all(row.get('phase')==4 for row in rows):return False
        states=[canonical_header(game.call('coop.editor assets')) for game in games]
        if not all(state['revision']==state['durable']==revision for state in states) or len({state['hash'] for state in states})!=1:return False
        if live and not all(row.get('ready') and not row.get('gesture') for row in rows):return False
        if not live and (designing()!=creation_serial or not all(row.get('ready') and not row.get('gesture') for game,row in zip(games,rows) if game!=host)):return False
        # Public native check captures the active Adventure scene and is
        # intentionally unavailable for the host inside a creation editor.
        # Its canonical state is still checked above through all-peer assets.
        checks=[game.call('coop.editor check').strip() for game in games if live or game!=host]
        return checks[0].startswith('CONVERGED ') and len(set(checks))==1
    def record(label):
        owner.verify_windows();rows=[]
        for game in games:
            faults=game.call('exc')
            if 'total faults: 0' not in faults:raise FixtureError('Native fault: '+faults)
            rows.append(dict(instance=game.instance,native_check_applicable=not creation_serial or game!=host,status=status(game),assets=game.call('coop.editor assets'),
                             creations=game.call('coop.editor creations'),check=game.call('coop.editor check'),history=game.call('coop.editor history'),faults=faults))
        result['checks'].append(dict(label=label,participants=rows));write()
    def move(row,steps=6):
        game=by_id[row['instance']];source=row['source'];target=row['target']
        for index in range(1,steps+1):
            x=round(source[0]+(target[0]-source[0])*index/steps);y=round(source[1]+(target[1]-source[1])*index/steps)
            game.call(f'move {x} {y}');game.call('wait.frames 2 15000')
    def start(row):
        game=by_id[row['instance']];x,y=row['source'];game.call(f'move {x} {y}');game.call('wait.frames 5 15000');game.call(f'button {x} {y} down');held[game.instance]=row['target']
        game.call('wait.frames 3 15000')
    def release(row):
        game=by_id[row['instance']];x,y=row['target'];game.call(f'button {x} {y} up');held.pop(game.instance,None);game.call('wait.frames 7 15000')
    try:
        result['windows']=verify_viewport(owner,games,recipe['viewport']);context=parse_context(status(host)['text']);revision=status(host)['revision']
        host.wait('ready durable Terraform baseline',lambda:canonical(revision));record('baseline')
        for row in recipe['strokes']:
            input_steps(by_id[row['instance']],recipe['setup'],'Terraform')
            if status(host)['revision']!=revision:raise FixtureError('Terraform navigation changed authored data')
        before=native_planet(observer);history=len(parse_history(host.call('coop.editor history')))
        if args.mode=='host-away':
            host.call('coop.uistate 0');host.click_control(0x07104130);host.click_control(0xC00F0301);host.click_control(0xC00F0310)
            creation_serial=host.wait('host local Creature Editor',designing)
            if status(host).get('editing'):raise FixtureError('The host did not leave its native Adventure scene')
            record('host-away-before-stroke')
        owner.verify_windows()
        # All presses and native histories start before either release. This is
        # stronger than merely sending two drag commands at nearby wall times.
        for row in recipe['strokes']:start(row)
        with ThreadPoolExecutor(max_workers=len(recipe['strokes'])) as pool:list(pool.map(move,recipe['strokes']))
        states=[status(by_id[row['instance']]) for row in recipe['strokes']]
        if not all(state.get('gesture') for state in states):raise FixtureError('Each native Terraform history must be open before release')
        if status(host)['revision']!=revision:raise FixtureError('A held stroke committed before release')
        result['held_histories']=states;write()
        with ThreadPoolExecutor(max_workers=len(recipe['strokes'])) as pool:list(pool.map(release,recipe['strokes']))
        revision+=len(recipe['strokes']);host.wait('ordered durable Terraform strokes',lambda:canonical(revision,creation_serial is None))
        after=native_planet(observer);changed={key for key in before['fields'].keys()|after['fields'].keys() if before['fields'].get(key)!=after['fields'].get(key)}
        if not changed&RECIPE_FIELDS:raise FixtureError('The native strokes did not change a terrain recipe')
        after_history=parse_history(host.call('coop.editor history'))
        if len(after_history)!=history+len(recipe['strokes']) or any(row[-1]!='Terraform adventure' for row in after_history[history:]):raise FixtureError('Expected exactly one Terraform Undo item per author')
        expected_authors={by_id[row['instance']].player_id() for row in recipe['strokes']}
        if {row[0] for row in after_history[history:]}!=expected_authors:raise FixtureError('Terrain history authors differ from pressed native inputs')
        record('strokes-committed')
        def history_key(game,key):
            nonlocal revision
            EditorPair.hotkey(game,key);revision+=1;host.wait('author-owned Terraform history',lambda:canonical(revision,creation_serial is None))
            if len(parse_history(host.call('coop.editor history')))!=len(after_history):raise FixtureError('Undo/Redo duplicated history')
            return native_planet(observer)['fields']
        first=by_id[recipe['strokes'][0]['instance']]
        middle=history_key(first,'z')
        if args.mode=='simultaneous':
            second=by_id[recipe['strokes'][1]['instance']]
            if middle==before['fields'] or middle==after['fields']:raise FixtureError('Own Undo failed to retain a distinct foreign stroke')
            if history_key(second,'z')!=before['fields']:raise FixtureError('Both author Undos did not restore the baseline')
            if history_key(second,'y')!=middle:raise FixtureError('Foreign Redo changed the retained recipe')
            if history_key(first,'y')!=after['fields']:raise FixtureError('Both Redos did not restore exact native recipe order')
            history_key(first,'z')
            if history_key(second,'z')!=before['fields']:raise FixtureError('Terraform cleanup did not restore the exact baseline')
        else:
            if middle!=before['fields'] or history_key(first,'y')!=after['fields'] or history_key(first,'z')!=before['fields']:raise FixtureError('Host-away terrain history changed unrelated planet values')
            if designing()!=creation_serial:raise FixtureError('Guest Terraform returned the host from its creation editor')
            record('host-away-history-complete');host.click_control(0x90439D7C)
            host.wait('host ordinary cancellation returns',lambda:'creation_editor active=0' in host.call('coop.editor creations') and status(host).get('ready'))
            creation_serial=None;host.wait('all returned native scenes',lambda:canonical(revision))
        record('restored');result['passed']=True;result['final_revision']=revision;print(json.dumps(dict(passed=True,mode=args.mode,revision=revision)))
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        # Release already-held native input and our own unsaved host editor
        # even when the test deadline itself expired. These bounded cleanup
        # actions still require the original live private process ownership.
        for game in games:game.console.deadline=max(game.console.deadline,time.monotonic()+30)
        for instance,point in list(held.items()):
            try:owner.verify_windows();by_id[instance].call(f'button {point[0]} {point[1]} up')
            except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        if creation_serial:
            try:
                owner.verify_windows()
                if designing()==creation_serial:
                    host.click_control(0x90439D7C);host.wait('cancel held host Creature Editor',lambda:'creation_editor active=0' in host.call('coop.editor creations') and status(host).get('ready'))
            except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        write()
        for game in games:game.save_log()
        owner.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path);parser.add_argument('--instances',type=int,nargs='+',default=[95,96,97,98]);parser.add_argument('--dll-sha256')
    parser.add_argument('--recipe',type=Path);parser.add_argument('--mode',choices=['simultaneous','host-away'],default='simultaneous');parser.add_argument('--output',type=Path)
    parser.add_argument('--timeout',type=int,default=120);parser.add_argument('--deadline',type=int,default=1800);args=parser.parse_args()
    if not args.execute:print(json.dumps(dict(prepared=True,launches_games=False,measured_recipe_required=True,modes=['simultaneous','host-away'])));return
    if not args.metadata or not args.output or not args.recipe or len(args.instances) not in (2,4) or len(set(args.instances))!=len(args.instances) or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or '') or not 30<=args.timeout<=300 or not 120<=args.deadline<=3600:parser.error('Exact private metadata/hash, measured recipe and bounded2/4-player arguments required')
    run(args)


if __name__=='__main__':main()
