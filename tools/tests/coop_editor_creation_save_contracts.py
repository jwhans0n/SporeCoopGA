"""Controlled stage/resume tests. Every game, OS owner and sampler is fake."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch

EDIT_MODAL = """Window 100 id=0xFFFFFFF0 cmd=0x00000000 fl=0x3 [0 0 1024 768] drw=00000000 vt=00000000 font=00000001 ''
  Window 101 id=0x05B598F7 cmd=0x00000000 fl=0x13 [0 0 1024 768] drw=00000000 vt=00000000 font=00000001 ''
    Window 102 id=0x06318E78 cmd=0x00000000 fl=0x43 [322 368 701 400] drw=00000000 vt=00000000 font=00AEBB69 ''
      - 103 id=0x01510D08 cmd=0x00000000 fl=0x3 [0 0 379 32] drw=00000000 vt=00000000 font=00AEBB69 'Save "ControlledCreation" as a new creation or replace "PriorCreature" before exiting?'
      Window 104 id=0x05822D40 cmd=0x00000000 fl=0x3 [-88 -112 472 118] drw=00000000 vt=00000000 font=00AEBB69 ''
        - 105 id=0x05107B17 cmd=0x00000000 fl=0x3 [21 176 130 213] drw=00000000 vt=00000000 font=AF14B67E 'Don't Save'
        - 106 id=0x05107B18 cmd=0x00000000 fl=0x3 [130 176 217 213] drw=00000000 vt=00000000 font=AF14B67E 'Cancel'
        - 107 id=0x05107B19 cmd=0x00000000 fl=0x3 [341 176 435 213] drw=00000000 vt=00000000 font=AF14B67E 'Replace'
        - 108 id=0x05107B1A cmd=0x00000000 fl=0x3 [435 176 539 213] drw=00000000 vt=00000000 font=AF14B67E 'Save New'
  Window 109 id=0x062EFC8C cmd=0x00000000 fl=0x13 [0 0 1024 768] drw=00000000 vt=00000000 font=00000001 ''
later unrelated UI may be truncated here"""


def confirmation_contracts(module):
    checks=0
    modal=module.edit_save_new_modal(EDIT_MODAL)
    assert modal['point']==(721,450)and modal['new_name']=='ControlledCreation'and modal['old_name']=='PriorCreature';checks+=1
    for old,new in [('0x06318E78','0x06318E79'),('before exiting?','before playing?'),("'Save New'","'Save'"),
                    ('0x05107B19','0x05107B1A'),('fl=0x43','fl=0x3'),('[-88 -112 472 118]','[-88 -112 40 118]'),
                    ('[0 0 1024 768]','[0 0 800 600]'),('fl=0x13 [0 0 1024 768]','fl=0x12 [0 0 1024 768]'),
                    ('0x05107B1A cmd=0x00000000','0x05107B1A cmd=0x00000001'),("'Replace'","'Unexpected action'"),
                    ('0x06318E78 cmd=0x00000000','0x06318E78 cmd=0x00000001'),('fl=0x43','fl=0x41'),
                    ('0x05B598F7 cmd=0x00000000 fl=0x13','0x05B598F7 cmd=0x00000000 fl=0x11'),
                    ('0xFFFFFFF0 cmd=0x00000000','0xFFFFFFF0 cmd=0x00000001'),
                    ('0x05107B1A cmd=0x00000000 fl=0x3','0x05107B1A cmd=0x00000000 fl=0x1')]:
        try:module.edit_save_new_modal(EDIT_MODAL.replace(old,new))
        except module.FixtureError:checks+=1
        else:raise AssertionError('Unrecognized Edit confirmation accepted')
    for tree in ('',EDIT_MODAL[:EDIT_MODAL.index('  Window 109')],EDIT_MODAL+'x'*8192):
        try:module.edit_save_new_modal(tree)
        except module.FixtureError:checks+=1
        else:raise AssertionError('Incomplete Edit overlay accepted')
    source=[0x40626200,0x7D000001,module.TYPES[0]];config=module.PROFILES['creature']
    row=dict(request=123,request_editor_id=config['editor_id'],model_type=config['model_type'],model_type_name=config['model_name'],new_asset=False,
             source_key=[source[1],source[2],source[0]],allowed_types=[dict(type=config['model_type'])],changed_4B3_4B4=[0,1],
             finish_state_if_debug_disabled=0,save_phase_38C=0,ui_pending_command_cc=258,ui_confirmation_flag_ec=0,widgets={'058A3350':[dict(visible=True)]})
    assert not module.edit_confirmation_errors(row,'creature',source,123);checks+=1
    assert not module.edit_confirmation_errors(dict(row,save_phase_38C=6),'creature',source,123);checks+=1
    assert module.edit_confirmation_errors(row,'creature',None,123);checks+=1
    for key,value in [('request',124),('source_key',[0,source[2],source[0]]),('new_asset',True),('ui_pending_command_cc',0),('ui_pending_command_cc',257),('ui_confirmation_flag_ec',1),('save_phase_38C',1),('finish_state_if_debug_disabled',2)]:
        assert module.edit_confirmation_errors(dict(row,**{key:value}),'creature',source,123);checks+=1
    return checks


def workflow_contracts(module):
    checks = 0
    state = dict(revision=0, designing=False, serial=7, blocker=0, actions=[], peers=1, profile='creature')
    def asset_text():
        revision = state['revision']
        text = f'revision={revision} durable={revision} canonicaldocumenthash='+('a' if not revision else 'd')*64+'\n'
        for index in range(2 if state.get('editing')else 1+revision):
            asset_type = module.TYPES[module.FAMILIES.index(module.PROFILES[state['profile']]['family'])] if index else module.TYPES[0]
            instance=0x7d000001+index+(16 if state.get('editing')and revision and index==0 else 0)
            content_revision=revision if state.get('editing')and index==0 else 0
            text += (f'asset class={8589934593+index}:1 settings={21474836481+index}:1 key=40626200!{instance:08X}.{asset_type:08X} '
                     f'content_revision={content_revision} behavior_sha256='+('b'*64)+'\n')
        return text
    class Owner:
        def __init__(self, *_): pass
        def verify_windows(self): return []
        def close(self): pass
    class Game:
        def __init__(self, instance, *_): self.instance = instance
        def asset_text(self): return asset_text()
        def call(self, command):
            if command == 'coop.editor assets': return asset_text()
            if command == 'coop.editor creations':
                if self.instance in state.get('others',{}):return f'creation_editor active=1 phase=1 serial={state["others"][self.instance]["serial"]} stale=0 entered=1'
                return f'creation_editor active=1 phase=1 serial={state["serial"]} stale=0 entered=1' if state['designing'] else 'creation_editor active=0'
            if command == 'coop.editor inspect':
                assert self.instance not in state.get('others',{}),'Native Adventure inspect while other editor owns scene'
                return f'placements=419 classes={2 if state.get("editing")else 1+state["revision"]} acts=5 act=0 bytes=60654 source=408A0000!BF0CE0D0.366A930D'
            if command == 'coop.editor history':
                return f'author={state.get("author",95)-95}:1 sequence=1 epoch=99 revision=1 applied=1 redo=0 '+('Replace shared asset content' if state.get('editing')else 'Create adventure asset') if state['revision'] else ''
            if command == 'coop.editor check':
                assert self.instance not in state.get('others',{}),'Native strict check while other editor owns scene'
                return 'CONVERGED '+('a' if not state['revision'] else 'd')*64
            if command == 'exc': return 'total faults: 0'
            if command == 'coop.ui tree 7':
                assert state.get('editing')and state.get('modal'),'New path unexpectedly probes Edit confirmation'
                case=state.get('confirmation_case')
                state['tree_calls']+=1
                if case=='stale':state['serial']+=1
                if case=='late-stale'and state['tree_calls']==2:state['serial']+=1
                if case=='other':state['others'][95]['serial']+=1
                tree=EDIT_MODAL.replace('before exiting?','before playing?')if case=='modal'else EDIT_MODAL
                if case=='changed'and state.get('tree_seen'):tree=tree.replace('PriorCreature','DifferentCreature')
                state['tree_seen']=True
                return tree
            state['actions'].append(command)
            if state.get('expire_other_on_click')and command.startswith(('drag ','move ')):
                state['others'][95]['serial']+=1;state['expire_other_on_click']=False
            return 'ok'
        def wait(self, _, predicate, **__):
            result = predicate()
            if not result and _=='ordinary Save returns independently':result=predicate()
            if not result: raise module.FixtureError('Controlled wait did not settle')
            return result
        def click_control(self, ident):
            state['actions'].append(ident)
            if ident==module.EDIT or module.CHOICE <= ident < module.CHOICE+4: state['designing'] = True
            if ident == module.DONE:
                if state.get('confirmation_case')not in('','direct-wait'):state['modal']=True
                else:
                    state['designing'] = False; state['revision'] = 1
                    if state.get('confirmation_case')=='direct-wait':state['ready_delay']=1
            if ident == module.CANCEL: state['designing'] = False
        def click(self, x, y):
            assert self.instance==state.get('author',95)
            if state.get('modal'):
                assert(x,y)==(721,450)
                dispatched=json.loads(state['finish_report'].read_text())
                assert dispatched['confirmation_dispatched']and dispatched['save_dispatched'],'Confirmation must be recorded before input'
                state['actions'].append(('confirm',x,y))
                if state.get('confirmation_case')=='click-failure':raise module.FixtureError('Controlled post-dispatch input failure')
                state.update(designing=False,revision=1,modal=False)
                if state.get('confirmation_case')=='confirm-wait':state['ready_delay']=1
                return
            state['actions'].append(('click', x, y))
            if state.get('expire_other_on_click'):state['others'][95]['serial']+=1
        def control(self, ident): return (980, 730, 1023, 767, 1) if ident == module.DONE else (940, 730, 979, 767, 1)
        def save_log(self): pass
    def status(game):
        revision = state['revision']
        peer = game.instance-95
        delayed=game.instance==state.get('author',95)and state.get('ready_delay',0)>0
        if delayed:state['ready_delay']-=1
        return dict(phase=4, frozen=False, ready=not delayed and game.instance not in state.get('others',{})and not state['designing'], revision=revision, durable=revision,
                    text=f'project={"c"*32} epoch=99 capability={peer}:{state["peers"]} resume_initialization=4 resume_enter_pending=0 resume_transitioning=0 world_visible=1')
    def sample(command, **_):
        rows=[]
        for at,value in enumerate(command):
            if value!='--participant':continue
            instance,_,profile=command[at+1].split(':');instance=int(instance);config=module.PROFILES[profile]
            row=dict(instance=instance,request=123 if instance==state.get('author',95)else 1000+instance,
                request_editor_id=config['editor_id'],model_type=config['model_type'],model_type_name=config['model_name'],
                allowed_types=[dict(type=config['model_type'])],new_asset=True,changed_4B3_4B4=[1,1],
                finish_state_if_debug_disabled=state['blocker'],save_phase_38C=0,ui_pending_command_cc=0,
                widgets={'058A3350':[dict(visible=True)]})
            if instance in state.get('others',{}):
                row.update(changed_4B3_4B4=[0,0],finish_state_if_debug_disabled=4,
                    save_phase_38C=1 if state.get('other_pending')else 0)
                if state['others'][instance].get('editing'):
                    kind=module.TYPES[module.FAMILIES.index(config['family'])]
                    row.update(new_asset=False,request_editor_id=0xFFFFFFFF,source_key=[0x7D000000+instance,kind,0])
            if '--edit-source'in command:
                key=command[command.index('--edit-source')+1];group,tail=key.split('!');resource,kind=tail.split('.')
                row.update(request_editor_id=0xFFFFFFFF,new_asset=False,source_key=[int(resource,16),int(kind,16),int(group,16)])
                if state.get('wrong_source'):row['source_key'][0]^=1
                if state.get('modal'):
                    row.update(ui_pending_command_cc=258,ui_confirmation_flag_ec=0)
                    case=state.get('confirmation_case')
                    if case=='source':row['source_key'][0]^=1
                    if case=='late-source'and state['tree_calls']>=2:row['source_key'][0]^=1
                    if case=='request':row['request']+=1
                    if case=='pending':row['ui_pending_command_cc']=0
                    if case=='phase':row['save_phase_38C']=3
            rows.append(row)
        Path(command[command.index('--output')+1]).write_text(json.dumps(dict(observer_only='--observe-other-editors'in command,rows=rows)))
        return SimpleNamespace(returncode=0,stdout='controlled read-only sampler',stderr='')
    with TemporaryDirectory(dir=module.ROOT/'out') as directory:
        cases=[(profile,editing,away,'')for profile,editing,away in [('creature',False,''),('building-house',False,''),('creature',True,''),('building-house',False,'save'),('building-house',False,'save-other-edit'),('building-house',False,'default-denied'),('building-house',False,'pending'),('building-house',False,'serial-expired')]]
        cases += [('creature',True,'',case)for case in ('confirm','confirm-wait','direct-wait','source','late-source','request','pending','phase','modal','changed','stale','late-stale','click-failure')]
        cases += [('creature',True,'confirm-away','confirm')]
        cases += [('creature',True,'confirm-other','other')]
        for profile,editing,away,confirmation_case in cases:
            root = Path(directory)/(profile+('-edit'if editing else '')+('-'+away if away else '')+('-'+confirmation_case if confirmation_case else ''));root.mkdir()
            state.update(revision=0,designing=False,serial=7,blocker=0,actions=[],peers=1,profile=profile,editing=editing,author=96 if away else 95,other_pending=False,expire_other_on_click=False,confirmation_case=confirmation_case,modal=False,tree_seen=False,tree_calls=0,ready_delay=0)
            state['others']={95:dict(profile='creature',serial=11),97:dict(profile='vehicle-military-land',serial=12),98:dict(profile='spaceship',serial=13)}if away else {}
            instances=[95,96,97,98]if away else [95,96]
            if away=='save-other-edit':state['others'][95]['editing']=True
            metadata = root/'metadata.json'
            metadata.write_text(json.dumps(dict(processes=[dict(instance=i, pid=i+100, profile=f'winlab_fake{i}') for i in instances])))
            house_steps=root/'house-controlled.json';house_steps.write_text(json.dumps([{'drag':[60,114,600,400]},{'text':'ControlledHouse'}]))
            def args(stage, parent=None, tag=None):
                return SimpleNamespace(metadata=metadata, output=root/(tag or stage), state=parent, candidate='a'*64,
                    instances=instances, author=state['author'], profile=profile,
                    allow_other_editors=bool(away)and away!='default-denied',other_editor=[f'{i}:{pin["profile"]}'for i,pin in state['others'].items()]if away and away!='default-denied'else [], target_class='8589934593:1'if editing else None, stage=stage, screenshots=False,
                    steps=module.ROOT/'tools/tests/fixtures/editor_creation/creature-held-c39.json' if profile=='creature' else house_steps, name='ControlledCreation')
            def report(arguments): return arguments.output/'result.json'
            with patch.object(module, 'Ownership', Owner), patch.object(module, 'CreationGame', Game), patch.object(module, 'status', status), \
                 patch.object(module, 'verify_viewport', lambda *_: []), patch.object(module.subprocess, 'run', sample):
                enter = args('enter')
                if away=='default-denied':
                    try:module.run(enter)
                    except module.FixtureError:checks+=1
                    else:raise AssertionError('Default silently allowed other editors')
                    assert not state['actions'];checks+=1;continue
                module.run(enter)
                first = json.loads(report(enter).read_text())
                assert first['passed'] and first['serial'] == 7 and first['native_request'] == 123; checks += 1
                parent=report(enter)
                if away=='pending':
                    state['other_pending']=True;pending=args('inspect',parent,'other-native-save-pending');actions=list(state['actions'])
                    try:module.run(pending)
                    except module.FixtureError:checks+=1
                    else:raise AssertionError('Other Native Save pending was ignored')
                    bad=json.loads(report(pending).read_text());assert bad['other_editor_conflict']and state['actions']==actions;checks+=1;continue
                if away=='serial-expired':
                    state['expire_other_on_click']=True;expired=args('steps',parent,'other-serial-expired')
                    try:module.run(expired)
                    except module.FixtureError:checks+=1
                    else:raise AssertionError('Replaced other session was adopted')
                    bad=json.loads(report(expired).read_text());assert bad['other_editor_conflict']and module.DONE not in state['actions'];checks+=1;continue
                if editing:
                    state['wrong_source']=True;wrong=args('inspect',parent,'wrong-native-source');actions=list(state['actions'])
                    try:module.run(wrong)
                    except module.FixtureError:checks+=1
                    else:raise AssertionError('Wrong Native source accepted')
                    assert state['actions']==actions;checks+=1
                    state['wrong_source']=False;parent=report(wrong)
                steps = args('steps', parent); module.run(steps)
                built = json.loads(report(steps).read_text())
                assert built['passed'] and built['completed_steps'] == (9 if profile=='creature' else 2) and len(built['applied_recipes']) == 1; checks += 1
                stale = args('inspect', report(enter), 'stale-parent'); actions = list(state['actions'])
                try: module.run(stale)
                except module.FixtureError: checks += 1
                else: raise AssertionError('Stale parent checkpoint replayed')
                assert state['actions'] == actions; checks += 1
                state['blocker'] = 2
                blocked = args('finish', report(steps), 'finish-blocked')
                try: module.run(blocked)
                except module.FixtureError: checks += 1
                else: raise AssertionError('Blocked native Save dispatched')
                rejected = json.loads(report(blocked).read_text())
                assert rejected['finish_preflight_errors'] and not rejected.get('save_dispatched') and module.DONE not in state['actions']; checks += 1
                state['blocker'] = 0
                finish = args('finish', report(blocked), 'finish-valid');state['finish_report']=report(finish)
                if confirmation_case and confirmation_case not in('confirm','confirm-wait','direct-wait'):
                    try:module.run(finish)
                    except module.FixtureError:checks+=1
                    else:raise AssertionError('Expired/unknown confirmation accepted')
                    failed=json.loads(report(finish).read_text())
                    assert not failed['passed']and failed['save_dispatched']and state['actions'].count(module.DONE)==1 and state['revision']==0;checks+=1
                    confirmations=[action for action in state['actions']if isinstance(action,tuple)and action[0]=='confirm']
                    assert len(confirmations)==int(confirmation_case=='click-failure');checks+=1
                    terminal=args('finish',report(finish),'confirmation-replayed');actions=list(state['actions'])
                    try:module.run(terminal)
                    except module.FixtureError:checks+=1
                    else:raise AssertionError('Confirmation/Done replayed after failed dispatch')
                    assert state['actions']==actions;checks+=1;continue
                module.run(finish)
                saved = json.loads(report(finish).read_text())
                assert saved['passed'] and saved['save_dispatched'] and saved['updated_asset'if editing else 'added_asset']['key'][2] == module.TYPES[module.FAMILIES.index(module.PROFILES[profile]['family'])]; checks += 1
                assert state['actions'].count(module.DONE) == 1 and state['revision'] == 1; checks += 1
                if profile=='building-house':assert 0xC00F0341 in state['actions'];checks += 1
                if editing:assert module.EDIT in state['actions']and module.NEW not in state['actions'];checks += 1
                if confirmation_case in('confirm','confirm-wait'):
                    assert saved['confirmation_dispatched']and sum(isinstance(action,tuple)and action[0]=='confirm'for action in state['actions'])==1;checks+=1
                if away:
                    assert saved['baseline']['native_reference']==96 and len(saved['other_editors'])==3;checks+=1
                    assert len(saved['returned_inspections'])==1 and len(saved['strict_native_checks'])==1;checks+=1
                    assert len(saved['canonical_asset_peers'])==4 and all(row['revision']==row['durable']==1 for row in saved['canonical_asset_peers']);checks+=1
                    assert sum(row['native']=='not_applicable_other_creation_editor'for row in saved['scene_proofs'])==3;checks+=1
                    assert all(row['serial']==state['others'][row['instance']]['serial']for row in saved['other_editors']);checks+=1
                terminal = args('finish', report(finish), 'finish-replayed'); actions = list(state['actions'])
                try: module.run(terminal)
                except module.FixtureError: checks += 1
                else: raise AssertionError('Terminal Save replayed')
                assert state['actions'] == actions; checks += 1
    return checks
