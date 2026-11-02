"""Offline contracts for release-stall acceptance; no processes or consoles."""
import copy
from pathlib import Path
import sys
import unittest

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


def gestures(samples=30):
    rows=[]
    for instance in (2,3):
        for index in range(samples):
            correlation=perf.correlate({'2':capture(instance-2),'3':capture(instance-2)},instance,5)
            correlation['sequence']=index+1
            frames={str(i):dict(p95_ms=20,max_ms=30) for i in (2,3)}
            rows.append(dict(author_instance=instance,correlation=correlation,frames=frames))
    return rows


class AcceptanceContracts(unittest.TestCase):
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
