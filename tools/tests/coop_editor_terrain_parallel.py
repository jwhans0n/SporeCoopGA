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
import struct
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair,status
from coop_editor_creations import input_steps,validate_steps,verify_viewport
from coop_editor_crew_terrain import TerrainConsole,native_planet
from coop_editor_party import Ownership,PublicGame,parse_context,parse_history,parse_identity

EFFECT_COLUMNS=((0x02A907B5,0x20,12),(0x02A907B6,0x38,56),(0x03A23F97,0xD,4),
                (0x03DD8E59,0xA,4),(0x03DD8E5A,0xD,4),(0x7A6AA70B,0xD,4))
MODEL_COLUMNS=((0x03A90C57,0x20,12),(0x03A90C5F,0x38,56))


def recipe_records(fields,columns):
    """Exact ordered primitive records for the measured BasicTools append case."""
    decoded=[];count=None
    for ident,kind,width in columns:
        field=fields.get(ident)
        if not isinstance(field,(tuple,list)) or len(field)!=3 or field[:2] not in ((kind,0x9C),[kind,0x9C]):
            raise FixtureError('BasicTools fixture requires every verified native recipe column')
        try:raw=bytes.fromhex(field[2])
        except (ValueError,TypeError):raise FixtureError('Malformed native recipe bytes') from None
        if len(raw)<8:raise FixtureError('Truncated native recipe column')
        rows,stride=struct.unpack('>II',raw[:8])
        if rows>8192 or stride!=width or len(raw)!=8+rows*width or count is not None and rows!=count:
            raise FixtureError('Unsupported native recipe column shape')
        count=rows;decoded.append(raw[8:])
    return [b''.join(data[n*width:(n+1)*width] for data,(_,_,width) in zip(decoded,columns)) for n in range(count or 0)]


def effect_planet(baseline,records):
    """Replace only the six exact effect columns in an owned planet value map."""
    if len(records)>8192 or any(not isinstance(row,bytes) or len(row)!=84 for row in records):
        raise FixtureError('Unsupported BasicTools effect record shape')
    recipe_records(baseline,EFFECT_COLUMNS);result=dict(baseline);offset=0
    for ident,kind,width in EFFECT_COLUMNS:
        value=struct.pack('>II',len(records),width)+b''.join(row[offset:offset+width] for row in records)
        result[ident]=type(baseline[ident])((kind,0x9C,value.hex()));offset+=width
    return result


def appended_effects(before,after,count):
    """Refuse general terrain edits; this calibration proves distinct appends."""
    old=recipe_records(before,EFFECT_COLUMNS);new=recipe_records(after,EFFECT_COLUMNS)
    if count not in (1,2) or len(new)!=len(old)+count or new[:len(old)]!=old:
        raise FixtureError('BasicTools strokes must append exactly one effect each without changing prior effects')
    if recipe_records(before,MODEL_COLUMNS)!=recipe_records(after,MODEL_COLUMNS):
        raise FixtureError('BasicTools strokes changed an unrelated model recipe')
    added=new[len(old):]
    if len(set(added))!=count:raise FixtureError('BasicTools strokes did not produce distinct effect records')
    if effect_planet(before,new)!=after:raise FixtureError('BasicTools strokes changed unrelated planet fields')
    return old,added


def checked_history(text):
    header=re.match(r'^local=(\d+):(\d+) count=(\d+)\n',text);rows=parse_history(text)
    if not header or int(header[3])!=len(rows):raise FixtureError('Complete canonical Terraform history is required')
    return rows


def require_capabilities(instances,rows,pinned):
    actual={instance:parse_identity(row['text'])[1] for instance,row in zip(instances,rows)}
    if len(rows)!=len(instances) or actual!=pinned:
        raise FixtureError('A pinned Terraform participant generation changed')


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
    held={};creation_serial=None;context=None;capabilities={}
    def write():(args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def designing():
        match=re.search(r'creation_editor active=1 phase=1 serial=(\d+) stale=0 entered=1\b',host.call('coop.editor creations'))
        return int(match[1]) if match else None
    def canonical(revision,live=True):
        rows=[status(game) for game in games]
        if any(row.get('frozen') for row in rows):raise FixtureError('Terraform froze a participant')
        if context and any(parse_context(row['text'])!=context for row in rows):raise FixtureError('The Terraform context changed')
        if capabilities:require_capabilities([game.instance for game in games],rows,capabilities)
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
        result['windows']=verify_viewport(owner,games,recipe['viewport']);initial=[status(game) for game in games]
        context=parse_context(initial[0]['text']);revision=initial[0]['revision']
        capabilities={game.instance:parse_identity(row['text'])[1] for game,row in zip(games,initial)}
        if len(set(capabilities.values()))!=len(games):raise FixtureError('Terraform participants require distinct capabilities')
        result['capabilities']={str(instance):list(capability) for instance,capability in capabilities.items()}
        host.wait('ready durable Terraform baseline',lambda:canonical(revision));record('baseline')
        for row in recipe['strokes']:
            input_steps(by_id[row['instance']],recipe['setup'],'Terraform')
            if status(host)['revision']!=revision:raise FixtureError('Terraform navigation changed authored data')
        before=native_planet(observer);history=len(checked_history(host.call('coop.editor history')))
        # Reject unsupported recipes before any press, including absent/mixed
        # columns. The fixed BasicTools case requires ordinary effect appends.
        recipe_records(before['fields'],EFFECT_COLUMNS);recipe_records(before['fields'],MODEL_COLUMNS)
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
        after=native_planet(observer);old_effects,added=appended_effects(before['fields'],after['fields'],len(recipe['strokes']))
        after_history=checked_history(host.call('coop.editor history'))
        if len(after_history)!=history+len(recipe['strokes']) or any(row[-1]!='Terraform adventure' for row in after_history[history:]):raise FixtureError('Expected exactly one Terraform Undo item per author')
        expected_authors={capabilities[row['instance']] for row in recipe['strokes']}
        if {row[:2] for row in after_history[history:]}!=expected_authors or any(row[3]!=context[1] for row in after_history[history:]):
            raise FixtureError('Terrain history authors or generations differ from pressed native inputs')
        accepted={entry[:2]:payload for entry,payload in zip(after_history[history:],added)}
        result['accepted_effects']=[dict(author=list(entry[:2]),history_revision=entry[4],payload=payload.hex()) for entry,payload in zip(after_history[history:],added)]
        record('strokes-committed')
        def history_key(game,key):
            nonlocal revision
            EditorPair.hotkey(game,key);revision+=1;host.wait('author-owned Terraform history',lambda:canonical(revision,creation_serial is None))
            if len(checked_history(host.call('coop.editor history')))!=len(after_history):raise FixtureError('Undo/Redo duplicated history')
            return native_planet(observer)['fields']
        first=by_id[recipe['strokes'][0]['instance']]
        middle=history_key(first,'z')
        if args.mode=='simultaneous':
            second=by_id[recipe['strokes'][1]['instance']]
            foreign=accepted[capabilities[second.instance]]
            if middle!=effect_planet(before['fields'],old_effects+[foreign]):
                raise FixtureError('Own Undo changed the exact accepted foreign effect or unrelated planet fields')
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


def self_test():
    checks=0
    def check(value):
        nonlocal checks
        checks+=1
        if not value:raise AssertionError('Terraform driver self-test failed')
    def rejects(action):
        try:action()
        except FixtureError:check(True)
        else:check(False)
    old=bytes(range(84));first=bytes([91])*84;second=bytes([172])*84;model=bytes(range(68))
    baseline={0xFFFFFFFF:(0xA,0,'12345678')}
    for columns,records in ((EFFECT_COLUMNS,[old]),(MODEL_COLUMNS,[model])):
        offset=0
        for ident,kind,width in columns:
            raw=struct.pack('>II',len(records),width)+b''.join(row[offset:offset+width] for row in records)
            baseline[ident]=(kind,0x9C,raw.hex());offset+=width
    after=effect_planet(baseline,[old,first,second])
    check(appended_effects(baseline,after,2)==([old],[first,second]))
    middle=effect_planet(baseline,[old,second]);check(recipe_records(middle,EFFECT_COLUMNS)==[old,second])
    check(effect_planet(baseline,[old])==baseline)
    check(effect_planet(middle,[old,first,second])==after)
    rejects(lambda:appended_effects(baseline,effect_planet(baseline,[first,old,second]),2))
    rejects(lambda:appended_effects(baseline,effect_planet(baseline,[old,first,first]),2))
    rejects(lambda:appended_effects(baseline,after,1))
    changed=dict(after);changed[0xFFFFFFFF]=(0xA,0,'87654321');rejects(lambda:appended_effects(baseline,changed,2))
    changed=dict(after);key=MODEL_COLUMNS[0][0];changed[key]=(*changed[key][:2],changed[key][2][:-2]+'FF');rejects(lambda:appended_effects(baseline,changed,2))
    for mode in range(7):
        malformed=dict(after);key,kind,width=EFFECT_COLUMNS[mode%len(EFFECT_COLUMNS)]
        field=malformed[key]
        if mode==0:del malformed[key]
        elif mode==1:malformed[key]=(kind,0x80,field[2])
        elif mode==2:malformed[key]=(kind,0x9C,'not hex')
        elif mode==3:malformed[key]=(kind,0x9C,field[2][:-2])
        elif mode==4:malformed[key]=(kind,0x9C,(struct.pack('>II',3,width+1)+bytes.fromhex(field[2])[8:]).hex())
        elif mode==5:malformed[key]=(kind,0x9C,(struct.pack('>II',8193,width)+bytes.fromhex(field[2])[8:]).hex())
        elif mode==6:malformed[key]=(kind,0x9C,(struct.pack('>II',2,width)+bytes.fromhex(field[2])[8:]).hex())
        rejects(lambda:recipe_records(malformed,EFFECT_COLUMNS))
    text=lambda slot,generation:f'project={"ad"*16} capability={slot}:{generation}'
    pinned={95:(0,1),96:(1,3)};rows=[{'text':text(0,1)},{'text':text(1,3)}]
    require_capabilities([95,96],rows,pinned);check(True)
    rejects(lambda:require_capabilities([95,96],[rows[0],{'text':text(1,4)}],pinned))
    rejects(lambda:require_capabilities([95,96],[rows[0],{'text':text(2,3)}],pinned))
    rejects(lambda:require_capabilities([95,96],rows[:1],pinned))
    history='local=0:1 count=2\nauthor=0:1 sequence=1 epoch=9 revision=1 applied=1 redo=0 Terraform adventure\nauthor=1:3 sequence=1 epoch=9 revision=2 applied=1 redo=0 Terraform adventure\n'
    entries=checked_history(history);check({row[:2] for row in entries}==set(pinned.values()))
    check({row[:2] for row in entries}!={(0,1),(1,4)})
    rejects(lambda:checked_history(history.replace('count=2','count=3')))
    rejects(lambda:checked_history(history.split('author=1:3')[0]))
    print(json.dumps(dict(self_test=True,checks=checks,native_calls=False)))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--execute',action='store_true')
    parser.add_argument('--metadata',type=Path);parser.add_argument('--instances',type=int,nargs='+',default=[95,96,97,98]);parser.add_argument('--dll-sha256')
    parser.add_argument('--recipe',type=Path);parser.add_argument('--mode',choices=['simultaneous','host-away'],default='simultaneous');parser.add_argument('--output',type=Path)
    parser.add_argument('--timeout',type=int,default=120);parser.add_argument('--deadline',type=int,default=1800);parser.add_argument('--self-test',action='store_true');args=parser.parse_args()
    if args.self_test:self_test();return
    if not args.execute:print(json.dumps(dict(prepared=True,launches_games=False,measured_recipe_required=True,modes=['simultaneous','host-away'])));return
    if not args.metadata or not args.output or not args.recipe or len(args.instances) not in (2,4) or len(set(args.instances))!=len(args.instances) or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or '') or not 30<=args.timeout<=300 or not 120<=args.deadline<=3600:parser.error('Exact private metadata/hash, measured recipe and bounded2/4-player arguments required')
    run(args)


if __name__=='__main__':main()
