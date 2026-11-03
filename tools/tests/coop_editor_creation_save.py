"""Bounded, resumable single-creation Save probe. Inert without --execute.

Use only after an explicit all-participant UI handoff. Each invocation performs
one bounded stage and preserves the scene on failure. Construction steps must
be calibrated normal native input, never guessed as a successful part placement.
No launches, deployment, profile writes, process writes or native function calls.
The native validation subprocess uses bounded ReadProcessMemory only.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/tests'),str(ROOT/'tools')]
from coop_adventure import FixtureError
from coop_adventure_editor import status
from coop_editor_party import Ownership,parse_context,parse_history
from coop_editor_creations import CreationGame,NEW,EDIT,CHOICE,FAMILIES,TYPES,assets,input_steps,validate_steps,verify_viewport
import hashlib

PROFILES={
    'creature':dict(family='creature',editor_id=0x465C50BA,model_type=0x9EA3031A,model_name='AnimalCreature',choice=None),
    'building-house':dict(family='building',editor_id=0x4E3F7777,model_type=0x4E3F7777,model_name='BuildingHouse',choice=0xC00F0341),
    'vehicle-military-land':dict(family='vehicle',editor_id=0x7D433FAD,model_type=0x7D433FAD,model_name='VehicleMilitaryLand',choice=0xC00F0340),
    'spaceship':dict(family='spaceship',editor_id=0x96B24187,model_type=0x98E03C0D,model_name='UFO',choice=None),
}
DONE,CANCEL=0x058A3350,0x90439D7C

def creation_serial(text):
    match=re.search(r'creation_editor active=1 phase=1 serial=(\d+) stale=0 entered=1\b',text)
    if not match or not 0<int(match[1])<1<<64:raise FixtureError('Original native creation session is not designing')
    return int(match[1])

def steps_file(path,profile):
    if path.stat().st_size>32768:raise FixtureError('Input recipe exceeds32KiB')
    raw=path.read_bytes();value=json.loads(raw.decode('utf-8-sig'))
    if isinstance(value,dict):
        if value.get('schema')!=1 or value.get('profile')!=profile or value.get('viewport')!=[1024,768] or not value.get('evidence'):
            raise FixtureError('Measured recipe belongs to a different native profile/viewport')
        value=value.get('steps')
    validate_steps(value)
    for step in value:
        action,item=next(iter(step.items()))
        if action in ('key','press','choose_card'):raise FixtureError('Only construction/naming input is allowed in steps')
        if action=='control' and int(item,16)in(DONE,CANCEL):raise FixtureError('Use the explicit finish/cancel stage')
        if action in ('click','drag','slow_drag') and any(v>=[1024,768][i%2]for i,v in enumerate(item)):
            raise FixtureError('Recipe leaves the measured viewport')
    return value,hashlib.sha256(raw).hexdigest()

def asset_snapshot(game):
    # A checkpoint roundtrip must preserve ResourceKey sequences identically.
    return json.loads(json.dumps(assets(game.asset_text())))

def participant_peers(games):
    result=[]
    for game in games:
        found=re.search(r'\bcapability=(\d+):(\d+)\b',status(game)['text'])
        if not found or not 0<=int(found[1])<4 or not 0<int(found[2])<=0xFFFFFFFF:raise FixtureError('Participant generation missing')
        result.append(dict(instance=game.instance,peer=list(map(int,found.groups()))))
    return result

def checkpoint_head(previous,state):
    path=Path(previous.get('workflow_head','')).resolve()
    if not path.is_relative_to((ROOT/'out').resolve()) or path.name!='checkpoint-head.json':raise FixtureError('Invalid OUT workflow head')
    head=json.loads(path.read_text())
    if head.get('result')!=str(state.resolve()) or head.get('sha256')!=hashlib.sha256(state.read_bytes()).hexdigest():
        raise FixtureError('Stale/modified checkpoint; use the last recorded result')
    return path

def other_editor_profiles(args):
    result=[];seen=set()
    for value in getattr(args,'other_editor',[]) or []:
        match=re.fullmatch(r'([0-9]{1,5}):([a-z-]+)',value)
        if not match or int(match[1])not in args.instances or int(match[1])==args.author or int(match[1])in seen or match[2]not in PROFILES:
            raise FixtureError('Other editor needs a distinct owned INSTANCE:PROFILE')
        seen.add(int(match[1]));result.append(dict(instance=int(match[1]),profile=match[2]))
    if result and not getattr(args,'allow_other_editors',False):raise FixtureError('Other editors require --allow-other-editors')
    return sorted(result,key=lambda row:row['instance'])

def other_request_errors(row,profile):
    expected=PROFILES[profile];errors=[]
    if row.get('model_type')!=expected['model_type']or row.get('model_type_name')!=expected['model_name']:errors.append('other editor family/model changed')
    if row.get('new_asset')is True:
        if row.get('request_editor_id')!=expected['editor_id']:errors.append('other New editor profile changed')
    elif row.get('new_asset')is False:
        source=row.get('source_key',[])
        if row.get('request_editor_id')!=0xFFFFFFFF or len(source)!=3 or not source[0]or source[1]!=TYPES[FAMILIES.index(expected['family'])]:errors.append('other Edit source/family changed')
    else:errors.append('other request mode is missing')
    if row.get('save_phase_38C')!=0 or row.get('ui_pending_command_cc')!=0:errors.append('other editor Save/transition is pending')
    return errors

def resume_errors(previous,current):
    errors=[]
    for key in ('candidate','metadata','instances','process_pins','peer_pins','author','profile','target_class'):
        if previous.get(key)!=current.get(key):errors.append('changed '+key)
    if bool(previous.get('allow_other_editors',False))!=bool(current.get('allow_other_editors',False)) or previous.get('other_editor_profiles',[])!=current.get('other_editor_profiles',[]):errors.append('changed other editor declaration')
    if previous.get('other_editor_conflict'):errors.append('previous other-editor scope expired')
    if previous.get('save_dispatched') or previous.get('passed')and previous.get('stage')in('finish','cancel'):
        errors.append('workflow is terminal or Save was already dispatched')
    if not previous.get('context') or not previous.get('serial') or not previous.get('native_request'):
        errors.append('previous stage has no complete creation identity')
    return errors

HEADER=re.compile(r'^revision=(\d+) durable=(\d+) canonicaldocumenthash=([0-9a-f]{64})$',re.M)
INSPECT=re.compile(r'^placements=(\d+) classes=(\d+) acts=(\d+) act=(\d+) bytes=(\d+) source=([0-9A-F]{8}![0-9A-F]{8}\.[0-9A-F]{8})$',re.M)

def canonical_row(text):
    m=HEADER.search(text)
    if not m:raise FixtureError('Missing complete canonical hash header')
    return dict(revision=int(m[1]),durable=int(m[2]),hash=m[3])

def inspect_row(text):
    m=INSPECT.search(text)
    if not m:raise FixtureError('Missing native scene roster witness')
    return dict(placements=int(m[1]),classes=int(m[2]),acts=int(m[3]),act=int(m[4]),bytes=int(m[5]),source=m[6])

def history_rows(text):return [list(row)for row in parse_history(text)]

def request_errors(row,profile,source=None):
    expected=PROFILES[profile];errors=[]
    if row.get('request_editor_id')!=(0xFFFFFFFF if source else expected['editor_id']) or row.get('model_type')!=expected['model_type'] or row.get('model_type_name')!=expected['model_name']:
        errors.append('wrong exact native request/model subtype')
    if source:
        if row.get('new_asset')is not False or row.get('source_key')!=[source[1],source[2],source[0]]:errors.append('Edit request/source changed')
    elif not row.get('new_asset'):errors.append('request is not the original New operation')
    return errors

def saved_asset_result(before,current,profile,target=None):
    old=before['rows'];new=current['rows'];family=TYPES[FAMILIES.index(PROFILES[profile]['family'])]
    if target:
        if set(old)!=set(new) or target not in old or any(new[key]!=row for key,row in old.items()if key!=target):raise FixtureError('Edit changed unrelated classes or class roster')
        prior,row=old[target],new[target]
        if any(row[key]!=prior[key]for key in ('settings','behavior')):raise FixtureError('Edit changed target behavior/settings')
        if row['key']==prior['key'] or row['key'][2]!=family or row['key'][1]>>24!=0x7D or row['content_revision']!=current['revision'] or row['content_revision']<=prior['content_revision']:
            raise FixtureError('Edit did not publish a new immutable content version/revision')
        return dict(updated_class=target,updated_asset=row,prior_asset=prior)
    added=set(new)-set(old)
    if len(added)!=1 or any(new.get(key)!=value for key,value in old.items()):raise FixtureError('Save changed old cast content or did not add exactly one class')
    target=next(iter(added));row=new[target]
    if row['key'][2]!=family or row['key'][1]>>24!=0x7D:raise FixtureError('New class has the wrong family or immutable alias')
    return dict(added_class=target,added_asset=row)

def finish_errors(row,profile,source=None):
    errors=request_errors(row,profile,source)
    allowed={r['type']for r in row.get('allowed_types',[])}
    if not allowed or row.get('model_type')not in allowed:errors.append('subtype is outside native allowed list')
    if not any(row.get('changed_4B3_4B4',[])):errors.append('native editor has no changed model/metadata')
    if row.get('finish_state_if_debug_disabled')!=0:errors.append('native finish masks still block or require confirmation')
    if row.get('save_phase_38C')!=0 or row.get('ui_pending_command_cc')!=0:errors.append('native save/exit is already pending')
    visible=lambda name:any(w.get('visible')for w in row.get('widgets',{}).get(name,[]))
    if not visible('058A3350'):errors.append('Done is not visible in the verified editor container')
    if row.get('early_done_owner_flag44') and visible('03F67620'):errors.append('native early-Done gate is active')
    return errors

def run(args):
    if not args.output.resolve().is_relative_to((ROOT/'out').resolve()):raise FixtureError('Output must be a new OUT directory')
    if args.state and args.state.stat().st_size>1<<20:raise FixtureError('Checkpoint exceeds1MiB')
    args.output.mkdir(parents=True,exist_ok=False)
    if args.state and not args.state.resolve().is_relative_to((ROOT/'out').resolve()):raise FixtureError('Checkpoint must be a retained OUT result')
    previous=json.loads(args.state.read_text())if args.state else None
    head=checkpoint_head(previous,args.state)if previous else args.output.resolve()/'checkpoint-head.json'
    metadata=json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    processes={r['instance']:r for r in metadata['processes']}
    declarations=other_editor_profiles(args)
    owner=Ownership(args.metadata,args.instances,args.candidate)
    deadline=time.monotonic()+240
    games=[CreationGame(i,args.output,60,deadline,[1024,768])for i in args.instances]
    host=games[0];author=next(g for g in games if g.instance==args.author)
    result=dict(passed=False,complete_acceptance=False,stage=args.stage,candidate=args.candidate,
                metadata=str(args.metadata.resolve()),instances=args.instances,author=args.author,profile=args.profile,target_class=getattr(args,'target_class',None),family=PROFILES[args.profile]['family'],
                process_pins=[dict(instance=i,pid=processes[i]['pid'],profile=processes[i]['profile'])for i in args.instances],
                applied_recipes=list(previous.get('applied_recipes',[]))if previous else [],
                allow_other_editors=bool(getattr(args,'allow_other_editors',False)),other_editor_profiles=declarations,
                other_editors=list(previous.get('other_editors',[]))if previous else [])
    context=None
    def write():
        result['workflow_head']=str(head)
        path=args.output.resolve()/'result.json';raw=(json.dumps(result,indent=2)+'\n').encode()
        temporary=path.with_suffix('.tmp');temporary.write_bytes(raw);temporary.replace(path)
        next_head=head.with_suffix('.tmp');next_head.write_text(json.dumps(dict(result=str(path),sha256=hashlib.sha256(raw).hexdigest()))+'\n');next_head.replace(head)
        for g in games:g.save_log()
    def other_conflict(message):
        result.setdefault('other_editor_conflict',message);raise FixtureError(message)
    def other_public_current():
        if not result['allow_other_editors']:return
        for pin in result['other_editors']:
            game=next(g for g in games if g.instance==pin['instance'])
            try:serial=creation_serial(game.call('coop.editor creations'))
            except FixtureError:other_conflict('Other participant is no longer designing')
            if serial!=pin['serial']:other_conflict('Other creation session/serial changed')
    def sample_others(label):
        if not result['other_editors']:return
        other_public_current();path=args.output/(label+'-native.json')
        command=[sys.executable,str(ROOT/'tools/tests/coop_editor_creation_validation.py'),'--execute','--metadata',str(args.metadata),'--candidate',args.candidate,'--output',str(path),'--observe-other-editors']
        for pin in result['other_editors']:command.extend(['--participant',f'{pin["instance"]}:{processes[pin["instance"]]["pid"]}:{pin["profile"]}'])
        done=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=20)
        (args.output/(label+'-sample.txt')).write_text(done.stdout+done.stderr)
        if done.returncode:other_conflict('Other editor read-only validation failed: '+done.stderr)
        receipt=json.loads(path.read_text());rows=receipt['rows']
        if receipt.get('observer_only')is not True:other_conflict('Other-editor receipt has no observer boundary')
        if len(rows)!=len(result['other_editors'])or len({row['instance']for row in rows})!=len(rows):other_conflict('Incomplete other editor receipt')
        for pin in result['other_editors']:
            row=next((row for row in rows if row['instance']==pin['instance']),None)
            if row is None or other_request_errors(row,pin['profile']):other_conflict('Other editor family/request/save state changed')
            values={key:row.get(key)for key in ('request','request_editor_id','model_type','new_asset','source_key')}
            if any(key in pin and pin[key]!=value for key,value in values.items()):other_conflict('Other Native creation request/source changed')
            pin.update(values)
        other_public_current();result[label]=dict(instances=[row['instance']for row in rows],idle=True);write()
    def all_assets(revision):
        snapshots=[asset_snapshot(g)for g in games]
        if any(row!=snapshots[0]or row['revision']!=revision or row['durable']!=revision for row in snapshots):raise FixtureError('Canonical asset rows differ across participants')
        result['canonical_asset_peers']=[dict(instance=g.instance,revision=row['revision'],durable=row['durable'],hash=row['hash'],rows=len(row['rows']))for g,row in zip(games,snapshots)]
        return snapshots[0]
    def live_adventure():
        away={pin['instance']for pin in result['other_editors']}
        live=[g for g in games if g.instance not in away]
        for game in live:
            if not status(game).get('ready'):return False
            if re.search(r'creation_editor active=1\b',game.call('coop.editor creations')):raise FixtureError('Unpinned participant entered another creation editor')
        return live
    def canonical(revision=None):
        other_public_current()
        states=[status(g)for g in games]
        if any(s.get('frozen')for s in states):raise FixtureError('Participant frozen; preserve scene')
        if any(s.get('phase')!=4 for s in states):return False
        if context is not None and any(list(parse_context(s['text']))!=context for s in states):raise FixtureError('Project/epoch changed')
        rows=[canonical_row(g.call('coop.editor assets'))for g in games]
        if any(r['revision']!=r['durable']or revision is not None and r['revision']!=revision for r in rows):return False
        return rows[0]if all(r==rows[0]for r in rows)else False
    def designing():return bool(re.search(r'creation_editor active=1 phase=1\b.*\bentered=1\b',author.call('coop.editor creations')))
    def guard():
        owner.verify_windows()
        if participant_peers(games)!=result['peer_pins']:raise FixtureError('Participant session generation changed')
        serial=creation_serial(author.call('coop.editor creations'))
        if result.get('serial',serial)!=serial:raise FixtureError('Native creation serial changed')
        if canonical()!=result['baseline']['canonical']:raise FixtureError('Canonical baseline changed; preserve current design')
        result['serial']=serial
    def returned():
        s=status(author)
        return 'creation_editor active=0'in author.call('coop.editor creations')and s.get('ready')and all(
            re.search(r'\b'+k+'='+str(v)+r'\b',s['text'])for k,v in(
                ('resume_initialization',4),('resume_enter_pending',0),('resume_transitioning',0),('world_visible',1)))
    def sample(label):
        path=args.output/(label+'-native.json')
        guard()
        command=[sys.executable,str(ROOT/'tools/tests/coop_editor_creation_validation.py'),'--execute','--metadata',str(args.metadata),
            '--candidate',args.candidate,'--participant',f'{args.author}:{processes[args.author]["pid"]}:{args.profile}','--output',str(path)]
        if result.get('target_source'):
            group,instance,kind=result['target_source'];command.extend(['--edit-source',f'{group:08X}!{instance:08X}.{kind:08X}'])
        done=subprocess.run(command,cwd=ROOT,capture_output=True,text=True,timeout=20)
        (args.output/(label+'-sample.txt')).write_text(done.stdout+done.stderr)
        if done.returncode:raise FixtureError('Native read-only validation failed: '+done.stderr)
        receipt=json.loads(path.read_text());rows=receipt['rows']
        if receipt.get('observer_only')or len(rows)!=1 or rows[0].get('instance')!=args.author:raise FixtureError('Author receipt belongs to a different observer/participant')
        row=rows[0]
        if previous and previous.get('native_request')!=row['request']:raise FixtureError('Native creation request changed; do not replay input')
        errors=request_errors(row,args.profile,result.get('target_source'))
        if errors:raise FixtureError(', '.join(errors))
        guard();result['native_request']=row['request'];result[label]=row;write();return row
    def record(label):
        owner.verify_windows()
        result[label]=dict(states=[status(g)for g in games],canonical=canonical(),
            creations=[g.call('coop.editor creations')for g in games],history=history_rows(host.call('coop.editor history')))
        for g in games:
            if 'total faults: 0'not in g.call('exc'):raise FixtureError('Native fault; preserve scene')
            if getattr(args,'screenshots',False):g.record(label)
        write()
    try:
        owner.verify_windows();result['windows']=verify_viewport(owner,games,[1024,768]);result['peer_pins']=participant_peers(games)
        if previous:
            errors=resume_errors(previous,result)
            if errors:raise FixtureError('Previous stage rejected: '+', '.join(errors))
            result['serial']=previous['serial'];result['native_request']=previous['native_request']
            context=previous['context'];result['context']=context;result['baseline']=previous['baseline']
            if not designing():raise FixtureError('Original local native editor is no longer designing')
        else:
            if args.stage!='enter':raise FixtureError('Begin with the explicit enter stage')
            baseline=host.wait('settled canonical baseline',canonical)
            if result['allow_other_editors']:
                if not status(author).get('ready'):raise FixtureError('Author must initially own a ready Adventure Editor')
                away={g.instance for g in games if not status(g).get('ready')}
                if away!={row['instance']for row in declarations}:raise FixtureError('Every non-ready other editor needs an explicit profile')
                result['other_editors']=[dict(row,family=PROFILES[row['profile']]['family'],serial=creation_serial(next(g for g in games if g.instance==row['instance']).call('coop.editor creations')),presence='designing')for row in declarations]
                reference=author;sample_others('other-editors-initial')
                initial=live_adventure()
                if not initial:raise FixtureError('An unpinned Adventure participant is not ready')
            else:
                if not all(status(g).get('ready')for g in games):raise FixtureError('All participants must initially be in their native Adventure Editor')
                reference=host;initial=games
            checks=[g.call('coop.editor check').strip()for g in initial]
            if not checks[0].startswith('CONVERGED ')or len(set(checks))!=1:raise FixtureError('Initial native scenes differ')
            context=list(parse_context(status(host)['text']));result['context']=context
            result['baseline']=dict(canonical=baseline,assets=all_assets(baseline['revision'])if result['allow_other_editors']else asset_snapshot(host),history=history_rows(host.call('coop.editor history')),
                native=inspect_row(reference.call('coop.editor inspect')),native_reference=reference.instance)
        baseline=result['baseline']
        if result['target_class']:
            target=baseline['assets']['rows'].get(result['target_class'])
            if not target or not target['key'][1] or not target['key'][2] or target['key'][1]>>24!=0x7D or target['key'][2]!=TYPES[FAMILIES.index(PROFILES[args.profile]['family'])]:raise FixtureError('Target immutable class/family is missing from baseline')
            result['target_source']=target['key']
        if canonical()!=baseline['canonical']:raise FixtureError('Unexpected intervening canonical edit; preserve and review current state')
        if previous:sample_others('other-editors-start')
        record('before')
        if args.stage=='enter':
            author.call('coop.uistate 0')
            if result['target_class']:author.click_control(EDIT)
            else:
                author.click_control(NEW);author.click_control(CHOICE+FAMILIES.index(PROFILES[args.profile]['family']))
                choice=PROFILES[args.profile]['choice']
                if choice is not None:author.click_control(choice)
            author.wait('fully entered native creation',designing);sample('entered')
        else:
            sample('before-action')
            if args.stage=='steps':
                steps,digest=steps_file(args.steps,args.profile)
                if digest in result['applied_recipes']:raise FixtureError('This exact recipe was already attempted; do not replay input')
                result['applied_recipes'].append(digest);result['steps_sha256']=digest;result['steps']=steps;result['completed_steps']=0;write()
                for index,step in enumerate(steps):
                    guard();action,value=next(iter(step.items()))
                    if action in ('click','drag','slow_drag'):
                        points=[value[i:i+2]for i in range(0,len(value),2)]
                        for ident in (DONE,CANCEL):
                            rect=author.control(ident)
                            if rect and any(rect[0]<=x<=rect[2] and rect[1]<=y<=rect[3]for x,y in points):
                                raise FixtureError('Construction step targets Done/Cancel; use its explicit stage')
                    result['input_in_flight']=index;write();input_steps(author,[step],args.name)
                    result['completed_steps']=index+1;result.pop('input_in_flight',None);write()
                author.call('wait.frames 30 15000');sample('after-action')
            elif args.stage=='inspect':
                pass
            elif args.stage=='finish':
                row=result['before-action'];errors=finish_errors(row,args.profile,result.get('target_source'));result['finish_preflight_errors']=errors;write()
                if errors:raise FixtureError('Done preflight rejected without clicking: '+', '.join(errors))
                sample_others('other-editors-before-save');guard();result['save_dispatched']=True;write();author.click_control(DONE)
                try:author.wait('ordinary Save returns independently',returned,timeout=60)
                except Exception:
                    if designing():sample('failed-done')
                    raise
                host.wait('exactly one durable creation admission',lambda:canonical(baseline['canonical']['revision']+1))
                history=history_rows(host.call('coop.editor history'))
                summary='Replace shared asset content' if result['target_class'] else 'Create adventure asset'
                if len(history)!=len(baseline['history'])+1 or history[:-1]!=baseline['history'] or history[-1][-1]!=summary or history[-1][0:2]!=next(row['peer']for row in result['peer_pins']if row['instance']==args.author) or history[-1][3:7]!=[context[1],baseline['canonical']['revision']+1,1,0]:raise FixtureError('Save did not add one creation Undo item')
                live=author.wait('live Adventure participants settle',live_adventure)if result['allow_other_editors']else games
                inspected=[inspect_row(g.call('coop.editor inspect'))for g in live];old=baseline['native']
                if any(r['classes']!=old['classes']+(0 if result['target_class']else 1) or r['placements']!=old['placements']or r['acts']!=old['acts']for r in inspected):raise FixtureError('Save changed an unexpected native roster')
                result['returned_inspections']=inspected
                result.update(saved_asset_result(baseline['assets'],all_assets(baseline['canonical']['revision']+1)if result['allow_other_editors']else asset_snapshot(host),args.profile,result['target_class']))
                checks=[g.call('coop.editor check').strip()for g in live]
                if not checks[0].startswith('CONVERGED ')or len(set(checks))!=1:raise FixtureError('Returned native scenes differ')
                result['strict_native_checks']=checks
                result['scene_proofs']=[dict(instance=g.instance,native='not_applicable_other_creation_editor'if g.instance not in {row.instance for row in live}else 'converged')for g in games]
            elif args.stage=='cancel':
                author.click_control(0x90439D7C);author.wait('ordinary Cancel returns',returned)
                if canonical()!=baseline['canonical']or history_rows(host.call('coop.editor history'))!=baseline['history']:raise FixtureError('Cancel changed authored state/history')
        if args.stage not in ('finish',)and canonical()!=baseline['canonical']:raise FixtureError('Unsaved local stage changed canonical state')
        sample_others('other-editors-end');record('after');result['passed']=True;write()
    except Exception as error:
        result['error']=repr(error)
        try:record('failure-preserved')
        except Exception as evidence_error:result['evidence_error']=repr(evidence_error)
        write();raise
    finally:write();owner.close()

def self_test():
    from tempfile import TemporaryDirectory
    checks=0
    for profile,config in PROFILES.items():
        row=dict(request_editor_id=config['editor_id'],model_type=config['model_type'],model_type_name=config['model_name'],allowed_types=[dict(type=config['model_type'])],new_asset=True,changed_4B3_4B4=[1,0],finish_state_if_debug_disabled=0,save_phase_38C=0,ui_pending_command_cc=0,widgets={'058A3350':[dict(visible=True)]})
        assert not finish_errors(row,profile);checks+=1
        for key,value in [('model_type',0),('request_editor_id',0),('new_asset',False),('changed_4B3_4B4',[0,0]),('finish_state_if_debug_disabled',2),('save_phase_38C',1),('ui_pending_command_cc',1),('allowed_types',[]),('widgets',{})]:
            bad=dict(row);bad[key]=value;assert finish_errors(bad,profile);checks+=1
    assert creation_serial('creation_editor active=1 phase=1 serial=7 stale=0 entered=1')==7;checks+=1
    for text in ('creation_editor active=0','creation_editor active=1 phase=1 serial=0 stale=0 entered=1','creation_editor active=1 phase=1 serial=7 stale=1 entered=1'):
        try:creation_serial(text)
        except FixtureError:checks+=1
        else:raise AssertionError('Expired creation accepted')
    current=dict(candidate='a'*64,metadata='owned.json',instances=[95,96],process_pins=[dict(instance=95,pid=123,profile='winlab_owned')],peer_pins=[dict(instance=95,peer=[0,1])],author=95,profile='creature')
    previous=dict(current,context=['b'*32,9],serial=7,native_request=123,stage='steps',passed=True)
    assert not resume_errors(previous,current);checks+=1
    for key in current:
        bad=dict(current);bad[key]=None;assert resume_errors(previous,bad);checks+=1
    for changes in (dict(save_dispatched=True),dict(stage='finish'),dict(serial=0),dict(native_request=0)):
        bad=dict(previous,**changes);assert resume_errors(bad,current);checks+=1
    steps,digest=steps_file(ROOT/'tools/tests/fixtures/editor_creation/creature-mouth-c7.json','creature');assert len(steps)==4 and len(digest)==64;checks+=1
    with TemporaryDirectory()as directory:
        path=Path(directory)/'steps.json'
        for steps in ([{'control':'058A3350'}],[{'key':'enter'}],[{'click':[1024,0]}],[{'call':'Editor::Save'}]):
            path.write_text(json.dumps(steps))
            try:steps_file(path,'creature')
            except (FixtureError,ValueError):checks+=1
            else:raise AssertionError('Unsafe construction recipe accepted')
    assert resume_errors(dict(previous,target_class='1:1'),dict(current,target_class='2:1'));checks+=1
    source=[0x40626200,0x7D000001,TYPES[0]];config=PROFILES['creature']
    edit=dict(request_editor_id=0xFFFFFFFF,model_type=config['model_type'],model_type_name=config['model_name'],allowed_types=[dict(type=config['model_type'])],new_asset=False,source_key=[source[1],source[2],source[0]],changed_4B3_4B4=[1,0],finish_state_if_debug_disabled=0,save_phase_38C=0,ui_pending_command_cc=0,widgets={'058A3350':[dict(visible=True)]})
    assert not finish_errors(edit,'creature',source);checks+=1
    for key,value in [('request_editor_id',config['editor_id']),('new_asset',True),('source_key',[source[0],source[1],source[2]])]:
        bad=dict(edit);bad[key]=value;assert finish_errors(bad,'creature',source);checks+=1
    old=dict(revision=22,rows={'1:1':dict(key=source,settings='10:1',behavior='a'*64,content_revision=20),'2:1':dict(key=source,settings='11:1',behavior='b'*64,content_revision=18)})
    after=json.loads(json.dumps(old));after['revision']=23;after['rows']['1:1']['key'][1]+=1;after['rows']['1:1']['content_revision']=23
    assert saved_asset_result(old,after,'creature','1:1')['updated_class']=='1:1';checks+=1
    for key,value in [('behavior','c'*64),('settings','99:1'),('key',source),('content_revision',22)]:
        bad=json.loads(json.dumps(after));bad['rows']['1:1'][key]=value
        try:saved_asset_result(old,bad,'creature','1:1')
        except FixtureError:checks+=1
        else:raise AssertionError('Invalid Edit result accepted')
    for kind in ('unrelated','added','removed'):
        bad=json.loads(json.dumps(after))
        if kind=='unrelated':bad['rows']['2:1']['content_revision']=23
        if kind=='added':bad['rows']['3:1']=bad['rows']['2:1']
        if kind=='removed':bad['rows'].pop('2:1')
        try:saved_asset_result(old,bad,'creature','1:1')
        except FixtureError:checks+=1
        else:raise AssertionError('Edit damaged unrelated class state')
    old_zero=json.loads(json.dumps(old));after_zero=json.loads(json.dumps(after))
    old_zero['rows']['1:1']['key'][0]=0;after_zero['rows']['1:1']['key'][0]=0
    assert saved_asset_result(old_zero,after_zero,'creature','1:1')['updated_asset']['key'][0]==0;checks+=1
    from coop_editor_creation_validation import edit_source_key
    assert edit_source_key('00000000!7D000001.2B978C46')==[0x7D000001,TYPES[0],0];checks+=1
    held=dict(edit,new_asset=True,request_editor_id=config['editor_id'],changed_4B3_4B4=[0,0],finish_state_if_debug_disabled=4)
    assert not other_request_errors(held,'creature');checks+=1
    for key,value in [('model_type',0),('request_editor_id',0),('save_phase_38C',1),('ui_pending_command_cc',1)]:
        bad=dict(held);bad[key]=value;assert other_request_errors(bad,'creature');checks+=1
    held=dict(edit,source_key=[source[1],source[2],0],changed_4B3_4B4=[0,0],finish_state_if_debug_disabled=4)
    assert not other_request_errors(held,'creature');checks+=1
    held['source_key']=[0,source[2],0];assert other_request_errors(held,'creature');checks+=1
    from types import SimpleNamespace
    declared=SimpleNamespace(instances=[95,96,97,98],author=96,allow_other_editors=True,other_editor=['98:spaceship','95:creature'])
    assert [row['instance']for row in other_editor_profiles(declared)]==[95,98];checks+=1
    for values in (['96:creature'],['95:creature','95:spaceship'],['99:creature'],['95:unknown']):
        bad=SimpleNamespace(**dict(vars(declared),other_editor=values))
        try:other_editor_profiles(bad)
        except FixtureError:checks+=1
        else:raise AssertionError('Invalid other editor declaration accepted')
    from coop_editor_creation_save_contracts import workflow_contracts
    checks+=workflow_contracts(sys.modules[__name__])
    print(f'staged creation Save: {checks} offline checks passed')

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--self-test',action='store_true');p.add_argument('--screenshots',action='store_true')
    p.add_argument('--metadata',type=Path);p.add_argument('--candidate');p.add_argument('--instances',nargs='+',type=int,default=[95,96])
    p.add_argument('--author',type=int);p.add_argument('--profile',choices=PROFILES);p.add_argument('--stage',choices=('enter','steps','inspect','finish','cancel'))
    p.add_argument('--allow-other-editors',action='store_true',help='Explicitly pin other already-designing sessions; author must start ready')
    p.add_argument('--other-editor',action='append',default=[],help='Owned INSTANCE:PROFILE for every other non-ready participant')
    p.add_argument('--target-class',help='Existing full class-id:generation; UI owner selects its native cast card before enter')
    p.add_argument('--state',type=Path);p.add_argument('--steps',type=Path);p.add_argument('--name',default='Preview9NativeSave');p.add_argument('--output',type=Path)
    args=p.parse_args()
    if args.self_test:self_test();return
    if not args.execute:
        print(json.dumps(dict(prepared=True,profiles=PROFILES,stages=['enter','steps','inspect','finish','cancel'],normal_input_only=True,requires_explicit_ui_handoff=True,creature_recipe='tools/tests/fixtures/editor_creation/creature-held-c39.json',historical_creature_recipe='tools/tests/fixtures/editor_creation/creature-mouth-c7.json',building_house_recipe='Accepted C11 part drag commands were not retained: supply an explicit measured steps file',spaceship_recipe='No valid measured Save design retained'),indent=2));return
    if (not args.metadata or not args.output or len(args.instances)not in(2,4)or len(set(args.instances))!=len(args.instances)or args.author not in args.instances or
        not re.fullmatch('[0-9a-f]{64}',args.candidate or'')or not args.profile or not args.stage or args.stage=='steps'and not args.steps or
        not re.fullmatch('[A-Za-z0-9 _-]{1,64}',args.name)):
        p.error('Exact ownership/candidate, profile, stage, OUT output and bounded name/steps required')
    if args.target_class:
        target=re.fullmatch(r'([0-9]{1,20}):([0-9]{1,10})',args.target_class)
        if not target or not 0<int(target[1])<1<<64 or not 0<int(target[2])<1<<32:p.error('Exact nonzero target full class ID required')
        args.target_class=f'{int(target[1])}:{int(target[2])}'
    if (args.stage=='enter')==bool(args.state):p.error('Only enter starts without an existing checkpoint')
    run(args)

if __name__=='__main__':main()
