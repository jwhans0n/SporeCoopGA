"""Offline contracts for release-stall acceptance; no processes or consoles."""
import copy
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/tests'))
import coop_editor_commit_performance as perf


PROJECT='01'+'00'*15


def capture(author=0):
    events=[]
    def add(stage,qpc,success=0):
        events.append(dict(index=len(events),stage=stage,qpc=qpc,project=PROJECT,author_slot=author,
                           author_generation=1,sequence=1,epoch=9,revision=5,success=success))
    for stage,qpc in [('gesture_release',20),('capture_begin',22),('capture_end',30),('supported_begin',31),
                      ('supported_end',35),('inverse_begin',36),('inverse_end',40),('local_submit',45),
                      ('apply_work_begin',50),('apply_work_end',60),('scene_ready',60),('persist_work_begin',61),
                      ('durable_begin',70),('durable_end',80),('persist_work_end',90),('own_confirmed',100)]:
        add(stage,qpc,1 if stage.endswith('_end') or stage=='scene_ready' else 0)
    header=dict(pid=1002,frequency=1000,capture=1,started_qpc=10,recording=0,count=len(events),capacity=4096,
                overflow=0,clock_error=0,offset=0,end=len(events))
    return dict(header=header,events=events)


def page(value):
    h=value['header'];text='clock=win32_qpc '+' '.join(f'{key}={item}' for key,item in h.items())+'\n'
    for row in value['events']:
        text+=(f'event={row["index"]} stage={row["stage"]} qpc={row["qpc"]} project={row["project"]} '
               f'author={row["author_slot"]}:{row["author_generation"]} sequence={row["sequence"]} '
               f'operation_epoch={row["epoch"]} revision={row["revision"]} success={row["success"]}\n')
    return text


class StageContracts(unittest.TestCase):
    def test_native_work_reports_inclusive_counter_deltas_without_guessing_costs(self):
        before={'native_work_frequency':1000,**{key:2 for key in perf.NATIVE_COUNTS}}
        for name in perf.NATIVE_SPANS:
            before[f'native_{name}_calls']=4;before[f'native_{name}_ticks']=50
        after={key:value+3 if key!='native_work_frequency' else value for key,value in before.items()}
        after['native_projection_ticks']=90
        encode=lambda row:'editor_native '+ ' '.join(f'{key}={value}' for key,value in row.items())
        result=perf.native_work_delta(perf.native_work_sample(encode(before)),perf.native_work_sample(encode(after)))
        self.assertEqual(result['spans']['projection'],dict(calls=3,ticks=40,inclusive_ms=40))
        self.assertEqual(result['spans']['read']['inclusive_ms'],3)
        self.assertEqual(result['counts']['pose_capture_hits'],3)
        self.assertIn('must not be summed',result['scope'])

    def test_native_work_old_payloads_are_explicitly_unavailable(self):
        self.assertIsNone(perf.native_work_sample('editor_native available=1 captures=5'))
        self.assertIsNone(perf.native_work_delta(None,None))
        with self.assertRaises(perf.FixtureError):perf.native_work_sample('native_work_frequency=1000')
        with self.assertRaises(perf.FixtureError):perf.native_work_delta(None,{'native_work_frequency':1000})

    def test_native_work_rejects_partial_reset_invalid_and_changed_clocks(self):
        sample={'native_work_frequency':1000,**{key:2 for key in perf.NATIVE_COUNTS}}
        for name in perf.NATIVE_SPANS:
            sample[f'native_{name}_calls']=4;sample[f'native_{name}_ticks']=50
        for key,value in [('native_work_frequency',0),('native_read_calls',-1),('native_read_ticks',2**64)]:
            bad={**sample,key:value}
            with self.assertRaises(perf.FixtureError):perf.native_work_sample(' '.join(f'{k}={v}' for k,v in bad.items()))
        for key,value in [('native_work_frequency',2000),('native_read_calls',3),('native_read_ticks',49)]:
            with self.assertRaises(perf.FixtureError):perf.native_work_delta(sample,{**sample,key:value})

    def test_drag_requires_exact_native_selection_and_live_lease_before_movement(self):
        calls=[]
        lease='local=0:1 phase=4 revision=7\nmember peer=0:1 phase=4 pending=0\nlease scope=Placement object=4:1 token=11 remaining_ms=9000\n'
        author=SimpleNamespace(instance=95,state=dict(selection='4:1',frozen=0),
            call=lambda command:(calls.append(command),lease if command=='coop.editor leases' else '')[1],
            wait=lambda label,predicate,timeout:self.assertTrue(predicate()))
        owner=SimpleNamespace(result={},converge=mock.Mock(),write=mock.Mock())
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state),mock.patch.object(perf.time,'sleep'):
            perf.begin_checked_drag(owner,author,'4:1',(500,365),7)
        self.assertEqual(calls,['move 500 365','wait.frames 5 15000','button 500 365 down','wait.frames 8 15000','coop.editor leases'])
        owner.converge.assert_not_called()

    def test_wrong_mouse_down_target_is_cancelled_without_any_placement_movement(self):
        calls=[];proofs=[];writes=[]
        author=SimpleNamespace(instance=95,state=dict(selection='5:1',frozen=0))
        author.call=lambda command:calls.append(command)
        author.wait=lambda label,predicate,timeout:predicate()
        owner=SimpleNamespace(result={},converge=lambda revision:(proofs.append(revision),'CONVERGED unchanged')[1],write=lambda:writes.append(True))
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state),mock.patch.object(perf.time,'sleep'),self.assertRaisesRegex(perf.FixtureError,'another placement'):
            perf.begin_checked_drag(owner,author,'4:1',(500,365),7)
        self.assertEqual(calls,['move 500 365','wait.frames 5 15000','button 500 365 down','wait.frames 8 15000','key esc','button 500 365 up','wait.frames 8 15000'])
        self.assertEqual(proofs,[7]);self.assertEqual(writes,[True])
        self.assertEqual(owner.result['native_target_rejections'][0]['observed']['selection'],'5:1')

    def test_warmup_rejects_wrong_or_additional_changed_placements(self):
        before={'4:1':dict(position=(1,2,3),rotation=(0,0,0,1),scale=1),
            '5:1':dict(position=(4,5,6),rotation=(0,0,0,1),scale=1)}
        with self.assertRaisesRegex(perf.FixtureError,'did not move'):
            perf.assert_target_moved(before,copy.deepcopy(before),'4:1')
        after=copy.deepcopy(before);after['4:1']['position']=(2,2,3)
        perf.assert_target_moved(before,after,'4:1')
        after['5:1']['scale']=2
        with self.assertRaisesRegex(perf.FixtureError,'another placement'):
            perf.assert_target_moved(before,after,'4:1')

    def test_author_handoff_uses_normal_deselection_without_a_transaction(self):
        calls=[];writes=[]
        author=SimpleNamespace(instance=95,state=dict(revision=7,selection='4:1'))
        observer=SimpleNamespace(instance=96,state=dict(revision=7,selection='4:1',gesture=0,frozen=0))
        def call(command):
            calls.append(command)
            if command=='key esc':observer.state['selection']=None
            return 'local=1:1 phase=4 revision=7\n'
        observer.call=call;observer.wait=lambda label,predicate:self.assertTrue(predicate())
        owner=SimpleNamespace(host=author,all_games=[author,observer],result={},write=lambda:writes.append(True))
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state):
            perf.handoff_native_selection(owner,author)
        self.assertEqual(calls,['key esc','wait.frames 8 15000','coop.editor leases'])
        self.assertEqual(owner.result['native_author_handoffs'],[dict(author=95,deselected=[96],revision=7)])
        self.assertEqual(writes,[True]);self.assertEqual(author.state['selection'],'4:1')

    def test_native_locate_is_explicit_unmeasured_input_and_checks_selected_object(self):
        calls=[]
        author=SimpleNamespace(instance=95,state=dict(revision=7,selection=None))
        def call(command):
            calls.append(command)
            if command=='click 88 170 right':author.state['selection']='4:1'
        author.call=call;author.wait=lambda label,predicate:self.assertTrue(predicate())
        owner=SimpleNamespace(host=author,all_games=[author],result={},args=SimpleNamespace(native_locate_card=[88,170]),write=lambda:None)
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state):
            perf.handoff_native_selection(owner,author,'4:1')
        self.assertEqual(calls,['move 88 170','click 88 170 right','wait.frames 120 15000'])
        self.assertEqual(owner.result['native_author_handoffs'][0]['native_locate_card'],[88,170])
        self.assertEqual(author.state['revision'],7)

    def test_author_handoff_does_not_open_menu_for_an_unselected_observer(self):
        author=SimpleNamespace(instance=95,state=dict(revision=7))
        observer=SimpleNamespace(instance=96,state=dict(revision=7,selection=None),call=mock.Mock())
        owner=SimpleNamespace(host=author,all_games=[author,observer],result={},write=lambda:None)
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state):
            perf.handoff_native_selection(owner,author)
        observer.call.assert_not_called()
        observer.state['gesture']=1
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state),self.assertRaisesRegex(perf.FixtureError,'unsettled'):
            perf.handoff_native_selection(owner,author)
        observer.call.assert_not_called()

    def test_author_handoff_cannot_hide_a_revision_change(self):
        author=SimpleNamespace(instance=95,state=dict(revision=7))
        observer=SimpleNamespace(instance=96,state=dict(selection='4:1'))
        def call(command):
            if command=='key esc':observer.state['selection']=None;author.state['revision']=8
            return ''
        observer.call=call;observer.wait=lambda label,predicate:self.assertTrue(predicate())
        owner=SimpleNamespace(host=author,all_games=[author,observer],result={},write=lambda:None)
        with mock.patch.object(perf,'status',side_effect=lambda game:game.state),self.assertRaisesRegex(perf.FixtureError,'changed the committed'):
            perf.handoff_native_selection(owner,author)

    def test_exact_qpc_stage_identity(self):
        value=capture();self.assertEqual(perf.stage_page(page(value)),value)
        captures={'2':value,'3':copy.deepcopy(value)};captures['3']['header']['pid']=1003
        result=perf.correlate(captures,2,5)
        self.assertEqual(result['participants']['2']['release_to_confirmation_ms'],80)
        self.assertEqual(result['participants']['2']['submit_to_confirmation_ms'],55)
        self.assertEqual(result['participants']['3']['durable_ms'],10)
        self.assertEqual(result['participants']['2']['capture_ms'],8)

    def test_incomplete_overflow_and_corrupt_pages_rejected(self):
        for name,value in [('recording',1),('overflow',1),('clock_error',1),('frequency',0),('pid',0),('end',99)]:
            bad=capture();bad['header'][name]=value
            with self.subTest(name=name),self.assertRaises(perf.FixtureError):perf.stage_page(page(bad))
        value=capture();text=page(value)
        for bad in (text.rstrip()[:-5],text.replace('author=0:1','author=0:0'),text.replace('event=1 ','event=7 ')):
            with self.assertRaises(perf.FixtureError):perf.stage_page(bad)

    def test_missing_failed_duplicate_and_wrong_operation_stages(self):
        def check(change):
            values={'2':capture(),'3':capture()};change(values)
            with self.assertRaises(perf.FixtureError):perf.correlate(values,2,5)
        check(lambda v:v['2']['events'].pop())
        check(lambda v:v['2']['events'].append(copy.deepcopy(v['2']['events'][-1])))
        check(lambda v:v['3']['events'][8].update(success=2))
        check(lambda v:v['3']['events'][8].update(epoch=10))
        check(lambda v:v['3']['events'][8].update(author_generation=2))
        check(lambda v:v['3']['header'].update(frequency=2000))
        check(lambda v:v['2']['events'][0].update(qpc=101))

    def test_missing_early_native_instrumentation_cannot_pass(self):
        values={'2':capture(),'3':capture()}
        values['2']['events']=[row for row in values['2']['events'] if row['stage']!='capture_begin']
        with self.assertRaises(perf.FixtureError):perf.correlate(values,2,5)


def gestures(samples=30,instances=(2,3)):
    rows=[]
    for slot,instance in enumerate(instances):
        for index in range(samples):
            correlation=perf.correlate({str(i):capture(slot) for i in instances},instance,5)
            correlation['sequence']=index+1
            frames={str(i):dict(p95_ms=20,max_ms=30) for i in instances}
            rows.append(dict(author_instance=instance,correlation=correlation,frames=frames))
    return rows


class AcceptanceContracts(unittest.TestCase):
    def test_bottom_menu_login_is_not_a_registration_dialog(self):
        game=SimpleNamespace(menu=lambda:True,caption=lambda text:text=='Create',console=object())
        with mock.patch.object(perf,'find',return_value=[(114,752,True),(600,180,False)]):
            self.assertTrue(perf.menu_login_button_only(game))
        for points in ([],[(114,752,False)],[(114,752,True),(600,180,True)],[(114,180,True)]):
            with self.subTest(points=points),mock.patch.object(perf,'find',return_value=points):
                self.assertFalse(perf.menu_login_button_only(game))
        for captions in (set(),{'Create',"If you don't register"}):
            game.caption=lambda text:text in captions
            with mock.patch.object(perf,'find',return_value=[(114,752,True)]):
                self.assertFalse(perf.menu_login_button_only(game))
        game.menu=lambda:False
        game.caption=lambda text:text=='Create'
        with mock.patch.object(perf,'find',return_value=[(114,752,True)]):
            self.assertFalse(perf.menu_login_button_only(game))

    def test_four_authors_require120_unique_operations_and_full_frame_windows(self):
        instances=(95,96,97,98);rows=gestures(instances=instances)
        summary=perf.summarize(rows,instances,30,200)
        self.assertTrue(summary['passed'])
        self.assertEqual(len(rows),120)
        self.assertEqual({i:v['samples'] for i,v in summary['authors'].items()},{str(i):30 for i in instances})
        self.assertEqual({i:v['windows'] for i,v in summary['frames'].items()},{str(i):120 for i in instances})
        for invalid in (rows[:-1],rows[:-1]+[rows[0]]):
            with self.assertRaises(perf.FixtureError):perf.summarize(invalid,instances,30,200)

    def test_ready_native_divergence_stops_without_repolling(self):
        calls=[];games=[]
        for instance,check in enumerate(('CONVERGED fixture','DIVERGED: quaternion differs')):
            game=SimpleNamespace(instance=instance,state=dict(phase=4,ready=1,gesture=0,revision=0,durable=0,
                text='project='+PROJECT+' epoch=17 capability='+str(instance)+':1'))
            game.call=lambda line,instance=instance,check=check:(calls.append((instance,line)),check)[1]
            games.append(game)
        host=SimpleNamespace(wait=lambda label,predicate:predicate())
        owner=SimpleNamespace(active_games=games,host=host)
        with mock.patch.object(perf.party_module,'status',side_effect=lambda g:g.state),self.assertRaisesRegex(perf.FixtureError,'quaternion differs'):
            perf.party_module.EditorParty.converge(owner)
        self.assertEqual(calls,[(0,'coop.editor check'),(1,'coop.editor check')])

    def test_direct_lan_transport_survives_helper_cleanup(self):
        args=SimpleNamespace(setup='bootstrap',profiles=['lan'],instances=[95,96],port=59093)
        self.assertFalse(perf.uses_owned_proxy(args))
        self.assertEqual(perf.route_owned_join('coop.join 127.0.0.1:59093 Guest',96,args,{}),
                         'coop.join 127.0.0.1:59093 Guest')
        self.assertEqual(perf.route_owned_join('coop.editor check',95,args,{}),'coop.editor check')
        for instance,line in ((95,'coop.join 127.0.0.1:59093 Guest'),
                              (97,'coop.join 127.0.0.1:59093 Guest'),
                              (96,'coop.join 127.0.0.1:59094 Guest')):
            with self.assertRaises(perf.FixtureError):perf.route_owned_join(line,instance,args,{})
        with self.assertRaises(perf.FixtureError):
            perf.route_owned_join('coop.join 127.0.0.1:59093 Guest',96,args,{96:SimpleNamespace(port=60000)})
        args.setup='attach'
        with self.assertRaises(perf.FixtureError):perf.route_owned_join('coop.join 127.0.0.1:59093 Guest',96,args,{})

    def test_wan_bootstrap_requires_its_owned_proxy(self):
        args=SimpleNamespace(setup='bootstrap',profiles=['lan','wan'],instances=[95,96],port=59093)
        self.assertTrue(perf.uses_owned_proxy(args))
        with self.assertRaises(perf.FixtureError):perf.route_owned_join('coop.join 127.0.0.1:59093 Guest',96,args,{})
        self.assertEqual(perf.route_owned_join('coop.join 127.0.0.1:59093 Guest',96,args,{96:SimpleNamespace(port=60000)}),
                         'coop.join 127.0.0.1:60000 Guest')

    def test_baseline_join_accepts_automatic_progress_and_keeps_offer_action(self):
        context='project='+PROJECT+' epoch=17 capability=0:1'
        for phases in ((4,4),(4,2,1,3)):
            calls=[];recorded=[]
            games=[]
            for instance,phase in enumerate(phases):
                game=SimpleNamespace(instance=instance,state=dict(phase=phase,text=context))
                game.wait=lambda label,predicate:predicate()
                game.call=lambda line,i=instance:calls.append((i,line))
                games.append(game)
            owner=SimpleNamespace(host=games[0],all_games=games,result={'gates':{}},
                converge=lambda:'CONVERGED fixture',record=recorded.append)
            with mock.patch.object(perf.party_module,'status',side_effect=lambda g:g.state):
                perf.party_module.EditorParty.join(owner)
            self.assertEqual(calls,[(i,'coop.editor join') for i,phase in enumerate(phases) if phase==1])
            self.assertTrue(owner.result['gates'][('two' if len(phases)==2 else 'four')+'_native_baselines'])
            self.assertEqual(len(recorded),1)

    def test_baseline_join_rejects_stale_project_epoch_and_replacement(self):
        context='project='+PROJECT+' epoch=17 capability=0:1'
        for stale in ('project='+'02'+'00'*15+' epoch=17','project='+PROJECT+' epoch=18',
                      'project='+PROJECT,'project='+PROJECT+' epoch=0'):
            calls=[]
            host=SimpleNamespace(state=dict(phase=4,text=context))
            guest=SimpleNamespace(state=dict(phase=4,text=stale),
                wait=lambda label,predicate:predicate(),call=calls.append)
            owner=SimpleNamespace(host=host,all_games=[host,guest],result={'gates':{}},
                converge=lambda:None,record=lambda _:None)
            with mock.patch.object(perf.party_module,'status',side_effect=lambda g:g.state),self.assertRaises(perf.FixtureError):
                perf.party_module.EditorParty.join(owner)
            self.assertEqual(calls,[])
        host=SimpleNamespace(state=dict(phase=4,text=context))
        guest=SimpleNamespace(state=dict(phase=2,text=context),wait=lambda label,predicate:predicate(),call=lambda _:None)
        def replace_context():guest.state['text']=context.replace('epoch=17','epoch=19')
        owner=SimpleNamespace(host=host,all_games=[host,guest],result={'gates':{}},
            converge=replace_context,record=lambda _:None)
        with mock.patch.object(perf.party_module,'status',side_effect=lambda g:g.state),self.assertRaisesRegex(perf.FixtureError,'context changed'):
            perf.party_module.EditorParty.join(owner)

    def test_measurement_gate_fences_exact_candidate_before_samples(self):
        with tempfile.TemporaryDirectory() as directory:
            gate=Path(directory)/'measure-go';hash_value='a'*64
            owner=SimpleNamespace(args=SimpleNamespace(measurement_gate=gate,
                expected_dll_sha256=hash_value,instances=[95,96]),result={},deadline=100,write=lambda:None)
            def release(unused):gate.write_text(hash_value+'\n',encoding='utf-8')
            with mock.patch.object(perf.time,'monotonic',return_value=1),mock.patch.object(perf.time,'sleep',side_effect=release):
                perf.await_measurement_gate(owner)
            self.assertFalse(owner.result['measurement_gate']['waiting'])
            self.assertTrue(gate.with_name(gate.name+'.ready').is_file())
            gate.write_text('b'*64,encoding='utf-8')
            with mock.patch.object(perf.time,'monotonic',return_value=1),self.assertRaisesRegex(perf.FixtureError,'exact candidate'):
                perf.await_measurement_gate(owner)

    def test_measurement_gate_cannot_outlive_runner_deadline(self):
        with tempfile.TemporaryDirectory() as directory:
            owner=SimpleNamespace(args=SimpleNamespace(measurement_gate=Path(directory)/'measure-go',
                expected_dll_sha256='a'*64,instances=[95,96]),result={},deadline=100,write=lambda:None)
            with mock.patch.object(perf.time,'monotonic',return_value=101),self.assertRaisesRegex(perf.FixtureError,'expired'):
                perf.await_measurement_gate(owner)

    def test_original_native_baseline_is_recorded_before_hosting(self):
        calls=[];writes=[]
        inspection='placements=1 classes=1 acts=1\nnative_sha256='+'a'*64
        owner=SimpleNamespace(result={},write=lambda:writes.append(True))
        game=SimpleNamespace(instance=95,call=lambda command:(calls.append(command),inspection)[1])
        perf.CommitParty.capture_original_baseline(owner,game)
        self.assertEqual(calls,['coop.editor inspect'])
        self.assertEqual(writes,[True])
        self.assertEqual(owner.result['original_native_baseline'],dict(instance=95,
            before_hosting=True,before_title_input=True,inspection=inspection))

    def test_failed_original_projection_is_retained_without_authoring_input(self):
        calls=[];writes=[]
        error='Native projection inspection failed: owner=21474836494:1 reference=8589935701:1'
        owner=SimpleNamespace(result={},write=lambda:writes.append(True))
        game=SimpleNamespace(instance=95,call=lambda command:(calls.append(command),error)[1])
        with self.assertRaisesRegex(perf.FixtureError,'21474836494:1'):
            perf.CommitParty.capture_original_baseline(owner,game)
        self.assertEqual(calls,['coop.editor inspect'])
        self.assertEqual(writes,[True])
        self.assertEqual(owner.result['original_native_baseline']['inspection'],error)

    def test_all_authors_and_observers_contribute(self):
        result=perf.summarize(gestures(),[2,3],30,200)
        self.assertTrue(result['passed']);self.assertEqual(result['frames']['2']['windows'],60)

    def test_single_rare_freeze_is_never_hidden_by_p95(self):
        rows=gestures();rows[-1]['frames']['2']['max_ms']=800
        result=perf.summarize(rows,[2,3],30,200)
        self.assertFalse(result['passed']);self.assertEqual(result['frames']['2']['max_ms'],800)
        self.assertEqual(result['frames']['2']['commit_window_max_p95_ms'],30)

    def test_frequent_release_spikes_fail_even_when_overall_native_p95_is_low(self):
        rows=gestures()
        for row in rows:row['frames']['3']['max_ms']=150
        self.assertFalse(perf.summarize(rows,[2,3],30,200)['passed'])

    def test_confirmation_thresholds_preserved(self):
        rows=gestures()
        for row in rows:row['correlation']['participants'][str(row['author_instance'])]['submit_to_confirmation_ms']=200.01
        self.assertFalse(perf.summarize(rows,[2,3],30,200)['passed'])
        self.assertTrue(perf.summarize(rows,[2,3],30,500)['passed'])

    def test_missing_or_duplicate_own_operations_rejected(self):
        rows=gestures()
        with self.assertRaises(perf.FixtureError):perf.summarize(rows[:-1],[2,3],30,200)
        rows[-1]=copy.deepcopy(rows[0])
        with self.assertRaises(perf.FixtureError):perf.summarize(rows,[2,3],30,200)

    def test_frame_buffer_saturation_and_missing_maximum_rejected(self):
        base='samples=30 invalid=0 recording=0 seconds=0.5 median_ms=16 p95_ms=20 max_ms=80'
        self.assertEqual(perf.frame_sample(base)['max_ms'],80)
        for bad in (base.replace('samples=30','samples=8192'),base.replace('max_ms=80',''),
                    base.replace('invalid=0','invalid=1'),base.replace('max_ms=80','max_ms=19')):
            with self.assertRaises(perf.FixtureError):perf.frame_sample(bad)


if __name__=='__main__':unittest.main()
