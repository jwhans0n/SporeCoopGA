"""Controlled stage/resume tests. Every game, OS owner and sampler is fake."""
import json
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace
from unittest.mock import patch


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
            state['actions'].append(command)
            if state.get('expire_other_on_click')and command.startswith(('drag ','move ')):
                state['others'][95]['serial']+=1;state['expire_other_on_click']=False
            return 'ok'
        def wait(self, _, predicate, **__):
            result = predicate()
            if not result: raise module.FixtureError('Controlled wait did not settle')
            return result
        def click_control(self, ident):
            state['actions'].append(ident)
            if ident==module.EDIT or module.CHOICE <= ident < module.CHOICE+4: state['designing'] = True
            if ident == module.DONE: state['designing'] = False; state['revision'] = 1
            if ident == module.CANCEL: state['designing'] = False
        def click(self, x, y):
            assert self.instance==state.get('author',95)
            state['actions'].append(('click', x, y))
            if state.get('expire_other_on_click'):state['others'][95]['serial']+=1
        def control(self, ident): return (980, 730, 1023, 767, 1) if ident == module.DONE else (940, 730, 979, 767, 1)
        def save_log(self): pass
    def status(game):
        revision = state['revision']
        peer = game.instance-95
        return dict(phase=4, frozen=False, ready=game.instance not in state.get('others',{})and not state['designing'], revision=revision, durable=revision,
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
            rows.append(row)
        Path(command[command.index('--output')+1]).write_text(json.dumps(dict(observer_only='--observe-other-editors'in command,rows=rows)))
        return SimpleNamespace(returncode=0,stdout='controlled read-only sampler',stderr='')
    with TemporaryDirectory(dir=module.ROOT/'out') as directory:
        for profile,editing,away in [('creature',False,''),('building-house',False,''),('creature',True,''),('building-house',False,'save'),('building-house',False,'save-other-edit'),('building-house',False,'default-denied'),('building-house',False,'pending'),('building-house',False,'serial-expired')]:
            root = Path(directory)/(profile+('-edit'if editing else '')+('-'+away if away else ''));root.mkdir()
            state.update(revision=0,designing=False,serial=7,blocker=0,actions=[],peers=1,profile=profile,editing=editing,author=96 if away else 95,other_pending=False,expire_other_on_click=False)
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
                finish = args('finish', report(blocked), 'finish-valid'); module.run(finish)
                saved = json.loads(report(finish).read_text())
                assert saved['passed'] and saved['save_dispatched'] and saved['updated_asset'if editing else 'added_asset']['key'][2] == module.TYPES[module.FAMILIES.index(module.PROFILES[profile]['family'])]; checks += 1
                assert state['actions'].count(module.DONE) == 1 and state['revision'] == 1; checks += 1
                if profile=='building-house':assert 0xC00F0341 in state['actions'];checks += 1
                if editing:assert module.EDIT in state['actions']and module.NEW not in state['actions'];checks += 1
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
