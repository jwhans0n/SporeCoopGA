"""Offline contract tests: no Windows process, console connection or native input."""
import copy
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
import coop_editor_performance as perf


def report(kind='independent'):
    rows = []
    for instance in (2, 3):
        rows.append(dict(instance=instance, pid=1000 + instance, inspect='placements=400 native_sha256=opaque',
            counts={'placements': 400}, projection='three checked landmarks',
            configuration=dict(frame='frame_limit_ms=0 screen_width=1024 screen_height=768',
                background='background mode on', window='window 0 0 1040 807', mods='coop gen=1'),
            party='offline\n' if kind == 'independent' else 'host port=47812 players=2/4\n' if instance == 2 else 'client id=1 connected\n',
            sample_start=100., sample_stop=120.,
            source_fence={'verified': True},
            metrics=dict(samples=2000, invalid=0, recording=0, seconds=20., mean_fps=100.,
                median_fps=100., median_ms=10., p95_ms=20.)))
    return dict(schema=2, kind=kind, fixture='four-hundred placements', hardware='recorded hardware',
        expected_dll_sha256='1' * 64, machine=dict(cpu=['CPU'], gpu=['GPU driver'], spore_pids=[1002, 1003]),
        sporekit_ini_sha256='3' * 64,
        minimum_placements=400, seconds_requested=20, sessions=rows, **{'pass': True})


class ComparisonTests(unittest.TestCase):
    def run_compare(self, baseline, shared, diagnostic=False):
        with tempfile.TemporaryDirectory(prefix='editor-perf-offline-') as temp:
            directory = Path(temp)
            for name, value in (('baseline', baseline), ('shared', shared)):
                (directory / (name + '.json')).write_text(json.dumps(value), encoding='utf-8')
            return perf.compare(SimpleNamespace(baseline=directory / 'baseline.json', shared=directory / 'shared.json', diagnostic=diagnostic))

    def test_matched_uncapped_native_reports(self):
        result = self.run_compare(report(), report('shared'))
        self.assertTrue(result['pass'])
        self.assertTrue(result['large_editor_gate'])
        self.assertFalse(result['memory_turnover_verified'])

    def test_budget_boundaries_and_regressions(self):
        for fps, p95, expected in ((90., 23., True), (89.9, 23., False), (100., 23.01, False)):
            current = report('shared')
            for row in current['sessions']:
                row['metrics'].update(median_fps=fps, median_ms=1000 / fps, p95_ms=p95)
            self.assertEqual(self.run_compare(report(), current)['pass'], expected)

    def test_configuration_camera_and_hardware_mismatch_rejected(self):
        edits = (
            lambda r: r['sessions'][0].update(pid=77),
            lambda r: r['sessions'][0].update(inspect='different authored bytes'),
            lambda r: r['sessions'][0].update(projection='moved camera'),
            lambda r: r['sessions'][0]['configuration'].update(window='window 1 0 1040 807'),
            lambda r: r['machine'].update(gpu=['different driver']),
            lambda r: r['machine'].update(spore_pids=[1002, 1003, 2000]),
            lambda r: r.update(expected_dll_sha256='2' * 64),
        )
        for edit in edits:
            current = report('shared'); edit(current)
            with self.assertRaises(perf.FixtureError): self.run_compare(report(), current)

    def test_invalid_metrics_cannot_create_a_pass(self):
        for name, value in (('median_fps', 0), ('median_fps', float('nan')), ('p95_ms', float('inf')),
                            ('p95_ms', -1), ('samples', 99), ('samples', 8193), ('invalid', 1), ('recording', 1),
                            ('p95_ms', 9), ('median_ms', 20)):
            current = report('shared'); current['sessions'][0]['metrics'][name] = value
            with self.assertRaises(perf.FixtureError): self.run_compare(report(), current)

    def test_duplicate_and_nonoverlapping_process_samples_rejected(self):
        for edit in (lambda r: r['sessions'].append(copy.deepcopy(r['sessions'][0])),
                     lambda r: r['sessions'][1].update(pid=r['sessions'][0]['pid']),
                     lambda r: r['sessions'][1].update(sample_start=119, sample_stop=139)):
            current = report('shared'); edit(current)
            with self.assertRaises(perf.FixtureError): self.run_compare(report(), current)
    def test_partial_or_mixed_party_roster_rejected(self):
        for edit in (lambda r: r['sessions'][0].update(party='host port=47812 players=4/4\n'),
                     lambda r: r['sessions'][1].update(party='client id=3 connected\n'),
                     lambda r: r['sessions'][1].update(party='host port=47812 players=2/4\n')):
            current = report('shared'); edit(current)
            with self.assertRaises(perf.FixtureError): self.run_compare(report(), current)

    def test_connected_party_is_not_an_independent_native_baseline(self):
        baseline = report(); baseline['sessions'][0]['party'] = 'host port=47812 players=2/4\n'
        with self.assertRaises(perf.FixtureError): self.run_compare(baseline, report('shared'))

    def test_small_or_capped_reports_need_explicit_diagnostic(self):
        for mode in ('small', 'single', 'capped'):
            baseline, current = report(), report('shared')
            for value in (baseline, current):
                if mode == 'small':
                    value['minimum_placements'] = 1
                    for row in value['sessions']: row['counts']['placements'] = 3
                elif mode == 'single':
                    value['sessions'] = value['sessions'][:1]
                    if value['kind'] == 'shared': value['sessions'][0]['party'] = 'host port=47812 players=1/4\n'
                else:
                    for row in value['sessions']: row['configuration']['frame'] = 'frame_limit_ms=33 screen_width=1024 screen_height=768'
            with self.assertRaises(perf.FixtureError): self.run_compare(baseline, current)
            result = self.run_compare(baseline, current, True)
            self.assertTrue(result['pass']); self.assertFalse(result['large_editor_gate'])
            self.assertEqual(result['kind'], 'diagnostic-comparison')


class FakeGame:
    last = None
    def __init__(self, instance, output):
        self.instance, self.output, self.commands = instance, output, []
        self.console = SimpleNamespace(sock=None); self.saved = False; FakeGame.last = self
    def call(self, command, required=True):
        self.commands.append((command, required))
        responses = {
            'info': 'pid=1002\nframes from: Present\nmode=0x1 (Scenario)\n',
            'coop.editor inspect': 'placements=400 classes=1 acts=1 act=0 bytes=100 native_sha256=opaque',
            'coop.editor status': 'phase=4 revision=3 durable=3 frozen=0',
            'coop.status': 'host port=47812 players=2/4\n',
            'coop.frame_metrics config': 'frame_limit_ms=0 screen_width=1024 screen_height=768',
            'coop.editor objects': 'screen=100,100\nscreen=200,100\nscreen=100,200\n',
            'bg': 'background mode on', 'window': 'window 0 0 1040 807', 'mods': 'coop gen=1',
            'coop.frame_metrics start': 'recording=1',
            'coop.frame_metrics stop': 'samples=2000 invalid=0 recording=0 seconds=20.000000 mean_fps=100.000000 median_fps=100.000000 median_ms=10.000000 p95_ms=20.000000',
            'exc': 'total faults: 0',
            'log 2000': '12:00:00.000 [INF] ?: ready\n',
        }
        if command not in responses: raise AssertionError('Unexpected command: ' + command)
        return responses[command]
    def save_log(self): self.saved = True


class SamplingTests(unittest.TestCase):
    def args(self):
        return SimpleNamespace(output=Path('unused/report.json'), minimum_placements=400, kind='shared', seconds=20)
    def test_integer_identity_and_signed_configuration_are_exact(self):
        self.assertEqual(perf.values('revision=18446744073709551615 cap=-1')['revision'], 18446744073709551615)
        self.assertEqual(perf.values('cap=-1')['cap'], -1)
        self.assertEqual(perf.values('p95_ms=12.5')['p95_ms'], 12.5)
    def test_readonly_configuration_and_sampling_contract(self):
        with patch.object(perf, 'Game', FakeGame), patch.object(perf, '_verify_game'), patch.object(perf, 'memory', return_value={}), \
             patch.object(perf.time, 'sleep'), patch.object(perf.time, 'monotonic', side_effect=(100., 120.)):
            result = perf.sample(2, {'pid': 1002}, self.args())
        commands = [command for command, _ in FakeGame.last.commands]
        self.assertNotIn('frame.limit', commands)
        self.assertEqual(commands.count('coop.frame_metrics start'), 1)
        self.assertEqual(commands.count('coop.frame_metrics stop'), 1)
        self.assertEqual(result['sample_stop'] - result['sample_start'], 20)
        self.assertTrue(FakeGame.last.saved)
    def test_interrupted_recording_is_stopped_in_finally(self):
        with patch.object(perf, 'Game', FakeGame), patch.object(perf, '_verify_game'), patch.object(perf, 'memory', return_value={}), \
             patch.object(perf.time, 'sleep', side_effect=(None, perf.FixtureError('interrupted'))), patch.object(perf.time, 'monotonic', return_value=100.):
            with self.assertRaises(perf.FixtureError): perf.sample(2, {'pid': 1002}, self.args())
        self.assertIn(('coop.frame_metrics stop', False), FakeGame.last.commands)
    def test_lost_start_response_still_stops_possibly_started_recording(self):
        class LostStart(FakeGame):
            def call(self, command, required=True):
                result = super().call(command, required)
                if command == 'coop.frame_metrics start': raise perf.FixtureError('lost response')
                return result
        with patch.object(perf, 'Game', LostStart), patch.object(perf, '_verify_game'), patch.object(perf, 'memory', return_value={}), \
             patch.object(perf.time, 'sleep'), patch.object(perf.time, 'monotonic', return_value=100.):
            with self.assertRaises(perf.FixtureError): perf.sample(2, {'pid': 1002}, self.args())
        self.assertIn(('coop.frame_metrics stop', False), FakeGame.last.commands)
    def test_camera_landmarks_must_be_visible_and_noncollinear(self):
        game = FakeGame(2, Path('.')); config = perf.configuration(game)
        for points in ('screen=hidden', 'screen=100,100\nscreen=100,100\nscreen=100,100',
                       'screen=100,100\nscreen=200,200\nscreen=300,300',
                       'screen=-10,100\nscreen=2000,200\nscreen=300,300'):
            game.call = lambda *args, value=points, **kwargs: value
            with self.assertRaises(perf.FixtureError): perf.projection(game, config)
    def test_temporary_frame_source_switch_or_log_wrap_rejected(self):
        before = 'entry one\nentry two\nentry three\n'
        self.assertTrue(perf.source_fence(before, before + 'ordinary entry\n')['verified'])
        for after in (before + 'frames: Present has not reached SporeKit\nframes: Present reaches SporeKit again\n',
                      before + 'frames: [sporekit] frames=loop\n', 'wrapped log\n', ''):
            with self.assertRaises(perf.FixtureError): perf.source_fence(before, after)


if __name__ == '__main__': unittest.main()
