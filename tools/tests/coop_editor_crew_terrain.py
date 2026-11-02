#!/usr/bin/env python3
"""Crew/Terraform UI checks on an already-owned hidden Windows editor party.

Preparation opens no connections. Execution needs exact lab ownership, candidate
hash and a measured UI recipe. Probe mode records public diagnostics only. The
fixture never launches/deploys games, calls native functions, writes memory, or
changes profile databases. Every accepted edit must produce one transaction and
Undo item, converge natively, and survive Undo/Redo through normal shortcuts.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path
import re
import struct
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair, status
from coop_editor_creations import input_steps, validate_steps, verify_viewport
from coop_editor_party import Ownership, PublicConsole, PublicGame, parse_context, parse_history, parse_identity
from sporectl import Console

FAMILIES = (
    'crew_capacity', 'crew_add', 'crew_replace', 'crew_remove', 'crew_lock',
    'sculpt', 'paint', 'smooth', 'erase', 'stamp_move', 'stamp_scale', 'stamp_rotate',
    'stamp_delete', 'water', 'atmosphere', 'ground', 'hazards', 'colors', 'gradient',
    'flora_add', 'flora_remove', 'flora_density', 'ground_effect', 'time',
    'day_night', 'visual_style', 'planet_preset',
)
RECIPE_FIELDS = {0x02A907B5, 0x02A907B6, 0x03A23F97, 0x03DD8E59, 0x03DD8E5A,
                 0x7A6AA70B, 0x03A90C57, 0x03A90C5F}
PLANET_FIELDS = {0x00B2CCCB, 0x02A907B7, 0x03A23F98, 0x03A23F99, 0x03ABB0AF,
                 0x04E13CB7, 0x0536250C, 0x0536250D, 0x072BEC91, 0x073137BB,
                 0x073154E0, 0x07B18058, 0x0B208B41, 0x48C74C5C, 0x56B14F05,
                 0x8E390196, 0x968C496A, 0x968C496B, 0xB6929C92, 0xB6929C93,
                 0xB6929C94, 0xB6929C95, 0xD2F669EC}


class NeedMore(Exception):
    pass


def planet_prefix(data):
    """Decode verified prefix semantics; native_planet retains its raw-byte SHA."""
    def take(at, count):
        if at+count > len(data):
            raise NeedMore()
        return data[at:at+count]
    if struct.unpack('<II', take(0, 8)) != (0xB523D4F9, 17):
        raise FixtureError('The native scenario header is not the verified version17')
    count, = struct.unpack('>I', take(8, 4))
    if count > 4096:
        raise FixtureError('Native terrain property count exceeds its verified bound')
    at, result = 12, {}
    sizes = {0:16, 0x20:16, 0x31:16, 0x32:16, 0x33:16, 0x34:16,
             1:1, 2:1, 5:1, 6:1, 3:2, 7:2, 8:2, 4:4, 9:4, 0xA:4,
             0xD:4, 0xB:8, 0xC:8, 0xE:8, 0x30:8, 0x38:56}
    for _ in range(count):
        ident, kind, flags = struct.unpack('>IHH', take(at, 8));at += 8
        begin = at
        if ident in result:
            raise FixtureError('Duplicate native planet property')
        if flags & 0x80:
            items, width = struct.unpack('>II', take(at, 8));at += 8
            if items > 8192 or width > 32 << 20:
                raise FixtureError('Native planet array exceeds its verified bound')
            if kind in (0x12, 0x13):
                for _ in range(items):
                    chars, = struct.unpack('>I', take(at, 4));at += 4
                    length = chars*(2 if kind == 0x13 else 1)
                    take(at, length);at += length
            else:
                take(at, items*width);at += items*width
        elif kind in (0x12, 0x13):
            chars, = struct.unpack('>I', take(at, 4));at += 4
            length = chars*(2 if kind == 0x13 else 1)
            take(at, length);at += length
        else:
            if kind not in sizes:
                raise FixtureError('Unsupported native planet scalar shape')
            take(at, sizes[kind]);at += sizes[kind]
        value = bytearray(data[begin:at])
        if not flags & 0x80 and kind in (0x20, 0x31, 0x32) and len(value) == 16:
            value[12:16] = bytes(4)
        # BuildProperties inserts this cleared None placeholder with an unused
        # inline union. Only its 16 payload bytes are nonsemantic; presence,
        # type and flags still participate in every planet comparison.
        if ident == 0x040539D4 and kind == 0 and flags == 0 and len(value) == 16:
            value[:] = bytes(16)
        result[ident] = (kind, flags, value.hex())
    return result, at


class TerrainConsole(PublicConsole):
    def call(self, line):
        if re.fullmatch(r'coop.editor inspect bytes [0-9]{1,6} 2048', line):
            if time.monotonic() > self.deadline:
                raise FixtureError('The bounded Crew/Terraform fixture expired')
            return Console.call(self, line)
        return super().call(line)


def native_planet(game):
    data, identity = bytearray(), None
    # Console replies are bounded to8192 characters. A4096-byte hex page plus
    # its header is truncated, so use2048-byte pages with exact count checks.
    for offset in range(0, 262144, 2048):
        page = game.call(f'coop.editor inspect bytes {offset} 2048')
        found = re.fullmatch(r'NATIVE_BYTES size=(\d+) offset=(\d+) count=(\d+) sha256=([0-9a-f]{64})\nhex=([0-9a-f]*)\s*', page)
        if not found:
            raise FixtureError('Native planet inspection is unavailable: '+page)
        size, actual, count = map(int, found.groups()[:3])
        if actual != offset or count != len(found[5])//2 or count > 2048:
            raise FixtureError('Malformed native byte page')
        stamp = (size, found[4])
        if identity is not None and stamp != identity:
            raise FixtureError('Native snapshot changed during property pagination')
        identity = stamp;data.extend(bytes.fromhex(found[5]))
        try:
            fields, end = planet_prefix(data)
            return dict(fields=fields, prefix_bytes=end,
                        # Hash the untouched native prefix, including unused
                        # placeholder bytes, rather than the semantic fields.
                        prefix_sha256=hashlib.sha256(data[:end]).hexdigest())
        except NeedMore:
            if not count or len(data) == size:
                raise FixtureError('The native planet property stream is truncated')
    raise FixtureError('This focused fixture needs a planet prefix under256KiB')


def semantic(game):
    text = game.call('coop.editor inspect')
    found = re.search(r'\bdocument_semantic_sha256=([0-9a-f]{64})\b', text)
    if not found:
        raise FixtureError('Native semantic projection failed: '+text)
    return found[1]


def checked_history(text):
    header=re.match(r'^local=(\d+):(\d+) count=(\d+)\n',text);rows=parse_history(text)
    if not header or int(header[3])!=len(rows):raise FixtureError('Complete canonical Crew/Terraform history is required')
    return rows


def require_party(instances,rows,context,capabilities):
    if len(rows)!=len(instances) or len(set(instances))!=len(instances):raise FixtureError('Incomplete native editor party')
    actual={instance:parse_identity(row['text'])[1] for instance,row in zip(instances,rows)}
    if any(parse_context(row['text'])!=context for row in rows) or actual!=capabilities:
        raise FixtureError('The pinned project, epoch or participant generation changed')
    if len(set(actual.values()))!=len(instances) or len({peer[0] for peer in actual.values()})!=len(instances) or any(not 0<=peer[0]<4 or not peer[1] for peer in actual.values()):
        raise FixtureError('The pinned native editor capabilities are invalid or duplicated')


def require_new_history(before,after,author,context,revision):
    if (len(after)!=len(before)+1 or after[-1][:2]!=author or not after[-1][2] or after[-1][3]!=context[1] or
            after[-1][4]!=revision or after[-1][5:7]!=(1,0)):
        raise FixtureError('The native action did not create exactly one item for its pinned author generation and revision')


def require_source_controls(hits,controls):
    actual={value.upper() for value in re.findall(r'^([0-9A-Fa-f]{8}) -?\d+ -?\d+ -?\d+ -?\d+ enabled=1\b',hits,re.M)}
    if not set(controls)<=actual:
        raise FixtureError('The measured native drag source or its owning control is absent')


def guarded_input_steps(game,case,phase,label,record_guard):
    guards={row['step']:row for row in case.get('source_guards',[]) if row['phase']==phase}
    for index,step in enumerate(case.get(phase,[])):
        action,value=next(iter(step.items()))
        if action in ('drag','slow_drag'):
            guard=guards[index];hits=game.call(f'coop.ui at {value[0]} {value[1]}')
            record_guard(phase,index,value[:2],guard['controls'],hits)
            require_source_controls(hits,guard['controls'])
        input_steps(game,[step],label)


def validate_recipe(data):
    if not isinstance(data,dict):raise ValueError('A measured native recipe object is required')
    viewport = data.get('viewport')
    if type(data.get('schema')) is not int or data['schema']!=1 or viewport != [1024, 768] or any(type(n) is not int for n in viewport) or data.get('measured') is not True:
        raise ValueError('An explicitly measured schema1 recipe at1024x768 is required')
    cases = data.get('cases')
    if not isinstance(cases, list) or not 1 <= len(cases) <= 80:
        raise ValueError('A focused recipe needs1..80 cases')
    labels = set()
    for case in cases:
        if not isinstance(case, dict) or not isinstance(case.get('label'),str) or not re.fullmatch('[a-z0-9_-]{1,64}', case['label']):
            raise ValueError('Every case needs a bounded stable label')
        if case['label'] in labels or case.get('family') not in FAMILIES:
            raise ValueError('Duplicate case label or unknown control family')
        labels.add(case['label'])
        validate_steps(case.get('action'))
        if 'setup' in case:
            validate_steps(case['setup'])
        for steps in (case.get('setup', []), case['action']):
            for step in steps:
                action, value = next(iter(step.items()))
                if action in ('click', 'drag','slow_drag') and any(value[i] >= viewport[i%2] for i in range(len(value))):
                    raise ValueError('A measured input leaves its owned viewport')
        expected={(phase,index) for phase in ('setup','action') for index,step in enumerate(case.get(phase,[]))
                  if next(iter(step)) in ('drag','slow_drag')}
        guards=case.get('source_guards',[])
        if not isinstance(guards,list) or len(guards)!=len(expected):raise ValueError('Each native drag requires one measured source-control guard')
        seen=set()
        for guard in guards:
            if not isinstance(guard,dict) or set(guard)!={'phase','step','controls'}:raise ValueError('Invalid measured source guard')
            phase,step,controls=guard['phase'],guard['step'],guard['controls']
            if phase not in ('setup','action') or type(step) is not int or (phase,step) not in expected or (phase,step) in seen:
                raise ValueError('Source guard must identify one exact drag step')
            if (not isinstance(controls,list) or not 1<=len(controls)<=4 or any(not isinstance(value,str) or not re.fullmatch('[0-9A-Fa-f]{8}',value) or
                    value.upper() in {'00000000','FFFFFFF0','FFFFFFFF'} for value in controls) or len({value.upper() for value in controls})!=len(controls)):
                raise ValueError('Distinct measured native source/owner control IDs are required')
            guard['controls']=[value.upper() for value in controls];seen.add((phase,step))
        fields = case.get('planet_fields', [])
        if not isinstance(fields, list) or any(not isinstance(v, str) or not re.fullmatch('[0-9a-fA-F]{8}', v) for v in fields):
            raise ValueError('Expected planet fields must be eight-digit hexadecimal IDs')
        if type(case.get('recipe_change', False)) is not bool:
            raise ValueError('recipe_change must be Boolean')
        if case['family'].startswith('crew_') and (fields or case.get('recipe_change')):
            raise ValueError('Crew cases cannot claim terrain mutations')
        if not case['family'].startswith('crew_') and not fields and not case.get('recipe_change'):
            raise ValueError('Terraform cases must name their observed native property or recipe change')
    return data


def recipes(path):
    return validate_recipe(json.loads(path.read_text(encoding='utf-8-sig')))


def run(args):
    recipe = recipes(args.recipes) if args.recipes else dict(viewport=[1024, 768], cases=[])
    args.output.mkdir(parents=True, exist_ok=False)
    owner = Ownership(args.metadata, args.instances, args.dll_sha256)
    deadline = time.monotonic()+args.deadline
    games = [PublicGame(i, args.output, args.timeout, deadline) for i in args.instances]
    for game in games:
        game.console = TerrainConsole(game.instance, deadline)
    host = games[0]
    result = dict(passed=False, complete_acceptance=False, candidate_sha256=args.dll_sha256,
                  probe=args.probe, checks=[], coverage=[], source_checks=[],recipe=recipe)
    context=None;capabilities={}

    def write():
        (args.output/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')

    def settled(revision=None):
        rows = [status(game) for game in games]
        if any(row.get('frozen') for row in rows):
            raise FixtureError('A Crew/Terraform edit froze a participant')
        if context is not None:require_party(args.instances,rows,context,capabilities)
        if revision is not None and any(row.get('revision', 0) > revision for row in rows):
            raise FixtureError('One UI action produced duplicate synchronized transactions')
        if not all(row.get('phase') == 4 and row.get('ready') and not row.get('gesture') and
                   row.get('revision') == row.get('durable') for row in rows):
            return False
        if len({row['revision'] for row in rows}) != 1 or revision is not None and rows[0]['revision'] != revision:
            return False
        checks = [game.call('coop.editor check').strip() for game in games]
        return checks[0].startswith('CONVERGED ') and len(set(checks)) == 1

    def record(label):
        owner.verify_windows();participants = []
        for game in games:
            faults = game.call('exc')
            if 'total faults: 0' not in faults:
                raise FixtureError('Native fault during '+label+': '+faults)
            game.record(label)
            participants.append(dict(instance=game.instance, status=status(game),
                                     inspect=game.call('coop.editor inspect'),
                                     history=game.call('coop.editor history'),
                                     leases=game.call('coop.editor leases'), faults=faults))
        result['checks'].append(dict(label=label, participants=participants));write()

    def inputs(author,case,phase,label):
        def source_guard(phase,index,point,controls,hits):
            owner.verify_windows();require_party(args.instances,[status(game) for game in games],context,capabilities)
            result['source_checks'].append(dict(label=label,phase=phase,step=index,point=point,controls=controls,hits=hits));write()
        guarded_input_steps(author,case,phase,label,source_guard)

    try:
        result['owned_windows'] = verify_viewport(owner, games, recipe['viewport'])
        host.wait('initial durable editor party',settled)
        rows=[status(game) for game in games];context=parse_context(rows[0]['text'])
        capabilities={game.instance:parse_identity(row['text'])[1] for game,row in zip(games,rows)}
        require_party(args.instances,rows,context,capabilities)
        result['context']=context;result['capabilities']={str(instance):list(peer) for instance,peer in capabilities.items()}
        record('baseline')
        if args.probe:
            result['passed'] = True;return
        for case in recipe['cases']:
            if args.only and case['label'] not in args.only:
                continue
            for author in games[:2]:
                label = f'{case["label"]}-{author.instance}'
                revision = status(host)['revision']
                require_party(args.instances,[status(game) for game in games],context,capabilities)
                if case.get('setup'):
                    inputs(author,case,'setup',label)
                    if status(host)['revision'] != revision:
                        raise FixtureError('Navigation setup changed the shared draft: '+label)
                before_semantic = semantic(host);before = native_planet(host)
                history = checked_history(host.call('coop.editor history'))
                owner.verify_windows();require_party(args.instances,[status(game) for game in games],context,capabilities)
                inputs(author,case,'action',label)
                host.wait(label, lambda: settled(revision+1))
                after_history = checked_history(host.call('coop.editor history'))
                require_new_history(history,after_history,capabilities[author.instance],context,revision+1)
                after_semantic = semantic(host);after = native_planet(host)
                if after_semantic == before_semantic:
                    raise FixtureError('The expected native edit had no authored semantic effect')
                changed = {key for key in set(before['fields'])|set(after['fields'])
                           if before['fields'].get(key) != after['fields'].get(key)}
                crew = case['family'].startswith('crew_')
                expected = {int(value, 16) for value in case.get('planet_fields', [])}
                if crew and (changed or after_history[-1][-1] != 'Edit adventure crew'):
                    raise FixtureError('The Crew UI case changed another authoring family')
                if not crew:
                    if changed-(PLANET_FIELDS|RECIPE_FIELDS) or expected and not expected <= changed:
                        raise FixtureError('The native control did not change its expected planet fields')
                    if bool(changed&RECIPE_FIELDS) != bool(case.get('recipe_change', False)):
                        raise FixtureError('The native recipe change did not match its measured control family')
                    if after_history[-1][-1] not in ('Edit planet settings', 'Terraform adventure'):
                        raise FixtureError('The Terraform input committed an unrelated authoring family')
                record(label+'-committed')
                for operation, expected_fields, expected_semantic in (
                        ('z', before['fields'], before_semantic), ('y', after['fields'], after_semantic),
                        ('z', before['fields'], before_semantic)):
                    revision += 1;EditorPair.hotkey(author, operation)
                    host.wait(label+' native history', lambda r=revision+1: settled(r))
                    if len(checked_history(host.call('coop.editor history')))!=len(after_history):
                        raise FixtureError('Native Crew/Terraform Undo/Redo created another history entry')
                    if native_planet(host)['fields'] != expected_fields:
                        raise FixtureError('Native terrain Undo/Redo changed unrelated authored values')
                    if crew and semantic(host) != expected_semantic:
                        raise FixtureError('Crew Undo/Redo changed unrelated authored values')
                result['coverage'].append(dict(label=label, family=case['family'], author=author.instance,
                                                changed_planet_fields=[f'{key:08X}' for key in sorted(changed)]))
                record(label+'-restored')
        covered = {(row['family'], row['author']) for row in result['coverage']}
        result['missing_family_authors'] = [dict(family=family, author=game.instance)
            for family in FAMILIES for game in games[:2] if (family, game.instance) not in covered]
        result['passed'] = bool(result['coverage'])
        result['full_control_family_pass'] = result['passed'] and not result['missing_family_authors']
    except Exception as error:
        result['error'] = repr(error);raise
    finally:
        write()
        for game in games:
            game.save_log()
        owner.close()


def self_test(root):
    checks=0
    def check(value):
        nonlocal checks
        checks+=1
        if not value:raise AssertionError('Crew/Terraform contract self-test failed')
    def rejects(callback,kind=FixtureError):
        try:callback()
        except kind:check(True)
        else:check(False)
    sample=dict(schema=1,measured=True,viewport=[1024,768],cases=[dict(label='round',family='sculpt',recipe_change=True,
        action=[dict(drag=[762,164,350,600])],source_guards=[dict(phase='action',step=0,controls=['073933b8','72DF4CEE'])])])
    parsed=validate_recipe(copy.deepcopy(sample));check(parsed['cases'][0]['source_guards'][0]['controls']==['073933B8','72DF4CEE'])
    for reason in range(16):
        invalid=copy.deepcopy(sample);case=invalid['cases'][0]
        if reason==0:invalid.pop('measured')
        if reason==1:invalid['measured']=False
        if reason==2:invalid['measured']=1
        if reason==3:case.pop('source_guards')
        if reason==4:case['source_guards']*=2
        if reason==5:case['source_guards'][0]['step']=1
        if reason==6:case['source_guards'][0]['phase']='setup'
        if reason==7:case['source_guards'][0]['controls']=['FFFFFFFF']
        if reason==8:case['source_guards'][0]['controls']=['073933B8','073933b8']
        if reason==9:case['source_guards'][0]['controls']=['not-a-control']
        if reason==10:case['action']=[dict(slow_drag=[762,164,1024,600])]
        if reason==11:case['setup']=[dict(drag=[762,164,350,600])]
        if reason==12:case['source_guards'][0]['phase']=[]
        if reason==13:case['source_guards'][0]['step']=True
        if reason==14:invalid['schema']=True
        if reason==15:case['label']=None
        rejects(lambda:validate_recipe(invalid),ValueError)
    setup=copy.deepcopy(sample);setup['cases'][0]['setup']=[dict(slow_drag=[762,164,350,600])]
    setup['cases'][0]['source_guards'].append(dict(phase='setup',step=0,controls=['073933B8']))
    check(validate_recipe(setup)['measured'])
    hits='073933B8 727 128 798 197 enabled=1\n72DF4CEE 688 65 1024 731 enabled=1\n'
    require_source_controls(hits,['073933B8','72DF4CEE']);check(True)
    rejects(lambda:require_source_controls('08D0F4FB 0 0 1024 768 enabled=1\n',['073933B8']))
    rejects(lambda:require_source_controls(hits.replace('073933B8','1073933B8'),['073933B8']))
    rejects(lambda:require_source_controls(hits.replace('enabled=1','enabled=0'),['073933B8']))
    class FakeGame:
        def __init__(self,response):self.response=response;self.calls=[]
        def call(self,command):self.calls.append(command);return self.response
    refused=FakeGame('08D0F4FB 0 0 1024 768 enabled=1\n')
    rejects(lambda:guarded_input_steps(refused,parsed['cases'][0],'action','guarded',lambda *_:None))
    check(refused.calls==['coop.ui at 762 164']) # No button, drag or ground input after a missing palette.
    accepted=FakeGame(hits);recorded=[]
    guarded_input_steps(accepted,parsed['cases'][0],'action','guarded',lambda *values:recorded.append(values))
    check(accepted.calls==['coop.ui at 762 164','drag 762 164 350 600'] and len(recorded)==1)
    context=('ad'*16,9);caps={95:(0,1),96:(1,7)}
    def row(project,epoch,slot,generation):return dict(text=f'project={project} epoch={epoch} capability={slot}:{generation}')
    rows=[row(context[0],9,0,1),row(context[0],9,1,7)]
    require_party([95,96],rows,context,caps);check(True)
    for changed in [row('ae'*16,9,1,7),row(context[0],10,1,7),row(context[0],9,1,8),row(context[0],9,2,7),row(context[0],9,0,1)]:
        rejects(lambda r=changed:require_party([95,96],[rows[0],r],context,caps))
    rejects(lambda:require_party([95,96],rows[:1],context,caps))
    history='local=0:1 count=1\nauthor=1:7 sequence=3 epoch=9 revision=10 applied=1 redo=0 Terraform adventure\n'
    entries=checked_history(history);require_new_history([],entries,(1,7),context,10);check(True)
    rejects(lambda:checked_history(history.replace('count=1','count=2')))
    for author,epoch,revision,applied,redo,sequence in [(1,9,10,1,0,3),(7,8,10,1,0,3),(7,9,11,1,0,3),(7,9,10,0,0,3),(7,9,10,1,1,3),(7,9,10,1,0,0)]:
        changed=[(1,author,sequence,epoch,revision,applied,redo,'Terraform adventure')]
        rejects(lambda rows=changed:require_new_history([],rows,(1,7),context,10))
    rejects(lambda:require_new_history(entries,entries,(1,7),context,10))
    def prefix(rows):
        return (struct.pack('<II',0xB523D4F9,17)+struct.pack('>I',len(rows))+
                b''.join(struct.pack('>IHH',ident,kind,flags)+value for ident,kind,flags,value in rows))
    placeholder=0x040539D4;payload=bytes(range(16));changed=bytes(reversed(payload))
    original=prefix([(placeholder,0,0,payload)]);fresh=prefix([(placeholder,0,0,changed)])
    first,first_end=planet_prefix(original);second,second_end=planet_prefix(fresh)
    check(first==second=={placeholder:(0,0,bytes(16).hex())})
    check(first_end==second_end==36 and original[20:]==payload and fresh[20:]==changed)
    check(planet_prefix(prefix([]))[0]!=first) # Presence remains semantic.
    for ident,kind,flags,value,next_value in [
            (placeholder,0,1,payload,changed), # A different None flag is strict.
            (placeholder+1,0,0,payload,changed), # No other None field is masked.
            (placeholder,0x20,0,payload,changed), # A typed scalar key is strict.
            (placeholder,0x20,0x80,struct.pack('>II',1,12)+payload[:12],struct.pack('>II',1,12)+changed[:12])]:
        left=planet_prefix(prefix([(ident,kind,flags,value)]))[0]
        right=planet_prefix(prefix([(ident,kind,flags,next_value)]))[0]
        check(left!=right)
    check(planet_prefix(prefix([(placeholder,0,1,payload)]))[0]!=first)
    check(planet_prefix(prefix([(placeholder,0x20,0,payload)]))[0]!=first)
    rejects(lambda:planet_prefix(original[:-1]),NeedMore)
    class PrefixGame:
        def __init__(self,native):self.native=native
        def call(self,command):
            check(command=='coop.editor inspect bytes 0 2048')
            return (f'NATIVE_BYTES size={len(self.native)} offset=0 count={len(self.native)} '
                    f'sha256={hashlib.sha256(self.native).hexdigest()}\nhex={self.native.hex()}\n')
    left=native_planet(PrefixGame(original));right=native_planet(PrefixGame(fresh))
    check(left['fields']==right['fields'] and left['prefix_sha256']!=right['prefix_sha256'])
    check(left['prefix_sha256']==hashlib.sha256(original).hexdigest() and
          right['prefix_sha256']==hashlib.sha256(fresh).hexdigest())
    data = (root/'out/editor-terrain-root.bin').read_bytes()
    fields, end = planet_prefix(data)
    assert len(fields) == 30 and end == 14262
    assert struct.unpack('>I', bytes.fromhex(fields[0x02A907B5][2])[:4])[0] == 162
    try:
        planet_prefix(data[:20])
    except NeedMore:
        pass
    else:
        raise AssertionError('Truncated planet property prefix accepted')
    print(f'Crew/Terraform fixture: {checks} contract checks, genuine native prefix30 properties,162 records,14262 bytes; no game connections')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute', action='store_true');parser.add_argument('--probe', action='store_true')
    parser.add_argument('--self-test', action='store_true');parser.add_argument('--metadata', type=Path)
    parser.add_argument('--instances', type=int, nargs='+', default=[95, 96])
    parser.add_argument('--dll-sha256');parser.add_argument('--recipes', type=Path)
    parser.add_argument('--output', type=Path);parser.add_argument('--only', nargs='+')
    parser.add_argument('--timeout', type=int, default=120);parser.add_argument('--deadline', type=int, default=3600)
    args = parser.parse_args()
    if args.self_test:
        self_test(Path(__file__).resolve().parents[2]);return
    if not args.execute:
        print(json.dumps(dict(prepared=True, launches_games=False, required_families=FAMILIES,
                              recipe_schema=1, measured_recipe_required=True,drag_source_guards_required=True,
                              viewport=[1024, 768], normal_input_only=True)));return
    if (not args.metadata or not args.output or not args.probe and not args.recipes or
            len(args.instances) not in (2, 4) or len(set(args.instances)) != len(args.instances) or
            not re.fullmatch('[0-9a-f]{64}', args.dll_sha256 or '') or
            not 30 <= args.timeout <= 300 or not 60 <= args.deadline <= 7200):
        parser.error('Exact private metadata, frozen DLL hash, measured recipe and bounded2/4-player arguments required')
    run(args)


if __name__ == '__main__':
    main()
