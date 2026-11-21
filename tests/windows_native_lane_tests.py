#!/usr/bin/env python3
"""Portable contracts for the Windows backend; native smoke is a separate gate."""
import io
import hashlib
import json
import os
import subprocess
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from windows_native_lane import (FixtureStore, LaneConfig, WindowsLane, digest, required_fixtures,
                                 real_unit_command)  # noqa: E402
from tests_viewport import viewport_of, canvas_of  # noqa: E402
from windows_native_suite import original_command, verdict, OwnedJob, lab_record  # noqa: E402


class LaneTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.env = patch.dict(os.environ, APPDATA=str(self.root / 'appdata'))
        self.env.start()
        self.addCleanup(self.env.stop)
        self.addCleanup(self.tmp.cleanup)
        self.raw = dict(schema=1, id='regression', source_commit='a' * 40,
                        installation=str(self.root / 'out/windows-lab/install'),
                        output=str(self.root / 'out/windows-lab/run'), fixtures=str(self.root / 'fixtures'),
                        build=str(self.root / 'build'), symbols=str(self.root / 'symbols'),
                        payload_hashes={n: 'a' * 64 for n in (
                            'steam_api.dll', 'sporekit.dll', 'coop.dll', 'SporeCoop.package')},
                        symbol_hashes={'gog2024.sym': 'b' * 64}, fixture_hashes={})
        self.path = self.root / 'lane.json'

    def config(self):
        self.path.write_text(json.dumps(self.raw), encoding='utf-8')
        return LaneConfig.load(self.path, self.root)

    def test_hashes_support_python_without_file_digest(self):
        path = self.root / 'payload.bin'
        payload = bytes(range(256)) * 8193
        path.write_bytes(payload)
        for originally_present in (True, False):
            with self.subTest(originally_present=originally_present), patch.dict(hashlib.__dict__):
                if originally_present:
                    hashlib.__dict__['file_digest'] = object()
                else:
                    hashlib.__dict__.pop('file_digest', None)
                with patch.dict(hashlib.__dict__):
                    hashlib.__dict__.pop('file_digest', None)
                    self.assertFalse(hasattr(hashlib, 'file_digest'))
                    path.write_bytes(payload)
                    self.assertEqual(digest(path), hashlib.sha256(payload).hexdigest())
                    path.write_bytes(b'')
                    self.assertEqual(digest(path), hashlib.sha256(b'').hexdigest())
                self.assertEqual(hasattr(hashlib, 'file_digest'), originally_present)

    def archive(self, entries, name='creature.tgz'):
        root = Path(self.raw['fixtures'])
        root.mkdir(exist_ok=True)
        path = root / name
        with tarfile.open(path, 'w:gz') as archive:
            for entry in entries:
                info = tarfile.TarInfo(entry[0])
                if len(entry) > 2:
                    info.type, info.linkname = entry[2], entry[1]
                    archive.addfile(info)
                else:
                    data = entry[1].encode()
                    info.size = len(data)
                    archive.addfile(info, io.BytesIO(data))
        self.raw['fixture_hashes'][name] = digest(path)
        return path

    def test_explicit_independent_udp_and_console_identity(self):
        cfg = self.config()
        self.assertEqual((cfg.host, cfg.guest, cfg.port), (49, 50, 47810))
        self.assertEqual(cfg.profile('guest'), 'winlab_regression_guest_50')
        self.assertEqual(cfg.game_log('host').name, 'sporekit.49.log')
        self.assertNotIn('Games', str(cfg.profile_dir('host')))

    def test_rejects_installation_escape_and_unbound_payload(self):
        for key, value in [('installation', str(self.root / 'player')),
                           ('output', str(self.root / 'out/windows-lab')),
                           ('host', 50), ('port', 47858), ('source_commit', 'HEAD'),
                           ('payload_hashes', {}), ('viewport', [1920, 1080])]:
            previous = self.raw.get(key)
            self.raw[key] = value
            with self.subTest(key=key), self.assertRaises((ValueError, TypeError)):
                self.config()
            if previous is None:
                del self.raw[key]
            else:
                self.raw[key] = previous

    def test_unmarked_or_different_profile_is_never_adopted(self):
        cfg = self.config()
        root = cfg.profile_dir('host')
        root.mkdir(parents=True)
        (root / 'personal.dat').write_text('preserve')
        with self.assertRaisesRegex(RuntimeError, 'unmarked'):
            WindowsLane(cfg).profile('host')
        self.assertEqual((root / 'personal.dat').read_text(), 'preserve')

    def test_native_loader_discovers_only_one_planet_in_its_owned_profile(self):
        cfg = self.config()
        lane = WindowsLane(cfg)
        self.addCleanup(lane.close)
        root = lane.profile('host')
        slot = root / 'Games/Game0'
        slot.mkdir(parents=True)
        planet = slot / 'Private Pony.spo'
        planet.write_bytes(b'portable ownership contract, not a native fixture')
        with patch.object(lane, 'owner', side_effect=AssertionError('read-only discovery acquired ownership')):
            self.assertEqual(lane.owned_planet(cfg.host), ('Game0', 'Private Pony'))
            with self.assertRaises(ValueError):
                lane.owned_planet(99)
            extra = slot / 'Another.spo'
            extra.write_bytes(b'ambiguous')
            with self.assertRaises(RuntimeError):
                lane.owned_planet(cfg.host)
            extra.unlink()
            planet.unlink()
            with self.assertRaises(RuntimeError):
                lane.owned_planet(cfg.host)
            planet.write_bytes(b'contract')
            (root / '.sporecoop-windows-lane.json').write_text('{}')
            with self.assertRaises(RuntimeError):
                lane.owned_planet(cfg.host)

    def test_windows_native_loader_checks_actual_mode_and_named_avatar(self):
        import load_game
        class Native:
            def __init__(self, mode='0x01654C02 Creature', planet='Private Pony', accept=True,
                         paused=False, replace_on_settle=False):
                self.mode, self.planet, self.accept = mode, planet, accept
                self.paused, self.replace_on_settle = paused, replace_on_settle
                self.loaded, self.calls = False, []
            def call(self, command):
                self.calls.append(command)
                if command.startswith('coop.load '):
                    self.loaded = True
                    return dict(ok=self.accept, out='loading Game0/Private Pony')
                if command == 'game.mode':
                    return dict(ok=True, out=self.mode if self.loaded else '0x02CCD1D2 GalaxyGameEntry')
                if command == 'coop.avatar':
                    return dict(ok=True, out=f"Creature: local=1234\ngame_name '{self.planet}.spo'")
                if command == 'coop.pauses':
                    return dict(ok=True, out=f'paused={int(self.paused)}')
                if command == 'wait.frames 300 60000' and self.replace_on_settle:
                    self.loaded = False
                return dict(ok=True, out='')
        class Owned:
            def owned_planet(self, instance):
                if instance != 49:
                    raise ValueError('unowned instance')
                return 'Game0', 'Private Pony'
        with patch('windows_native_lane.backend', return_value=Owned()):
            native = Native()
            self.assertIn('Private Pony.spo', load_game.load_windows(native, 49, timeout=0))
            self.assertIn('coop.load Game0 "Private Pony"', native.calls)
            self.assertFalse(any(command.startswith('click ') for command in native.calls))
            for native in (Native(planet='Wrong'), Native(mode='0x01 CreatureEditor'), Native(accept=False),
                           Native(paused=True), Native(replace_on_settle=True)):
                with self.subTest(calls=native.calls), self.assertRaises(RuntimeError):
                    load_game.load_windows(native, 49, timeout=0)
            native = Native()
            with self.assertRaises(ValueError):
                load_game.load_windows(native, 99, timeout=0)
            self.assertEqual(native.calls, [])

    def test_profile_reset_refuses_escape_unknown_and_loaded_save(self):
        cfg = self.config()
        lane = WindowsLane(cfg)
        lane.profile('host')
        with self.assertRaises(ValueError):
            lane.remove(cfg.profile('host'), '../outside')
        with self.assertRaises(ValueError):
            lane.remove('test', 'Games')
        lane.labs['host'] = object()
        with self.assertRaisesRegex(RuntimeError, 'loaded'):
            lane.remove(cfg.profile('host'), 'Games')
        with self.assertRaisesRegex(RuntimeError, 'loaded'):
            lane.remove(cfg.profile('host'), 'EditorSaves.package')

    def test_child_process_cannot_mutate_owner_lane(self):
        cfg = self.config()
        with patch.dict(os.environ, SPORE_WINDOWS_LANE_OWNER=str(os.getpid() + 1)):
            with self.assertRaisesRegex(RuntimeError, 'owning'):
                WindowsLane(cfg).profile('guest')

    def test_entire_installation_is_owned_before_profile_mutation(self):
        cfg = self.config()
        first, second = WindowsLane(cfg), WindowsLane(cfg)
        try:
            first.profile('host')
            with self.assertRaisesRegex(RuntimeError, 'another test run'):
                second.profile('guest')
            self.assertFalse(cfg.profile_dir('guest').exists())
            first.close()
            second.profile('guest')
        finally:
            first.close()
            second.close()

    def test_failed_game_cleanup_retains_ownership_until_restoration(self):
        class Lab:
            failed = True
            def stop(self):
                if self.failed:
                    raise OSError('owned process did not retire')
        cfg = self.config()
        first, second = WindowsLane(cfg), WindowsLane(cfg)
        lab = Lab()
        first.profile('host')
        first.labs['host'] = lab
        try:
            with self.assertRaisesRegex(RuntimeError, 'cleanup failed'):
                first.close()
            self.assertIn('host', first.labs)
            self.assertIsNotNone(first.lease)
            with self.assertRaisesRegex(RuntimeError, 'another test run'):
                second.profile('guest')
            lab.failed = False
            first.close()
            second.profile('guest')
        finally:
            lab.failed = False
            first.close()
            second.close()

    def test_archive_safety_before_existing_games_change(self):
        for bad in [('Games/../escape', 'bad'), ('Games/link', '/outside', tarfile.SYMTYPE),
                    ('Games/A', 'one'), ('Games\\bad', 'bad'), ('Games/Game0/C:bad', 'bad')]:
            self.archive([('Games/Game0/Scalinus.spo', 'native'), bad])
            cfg = self.config()
            lane = WindowsLane(cfg)
            games = lane.profile('host') / 'Games'
            games.mkdir(exist_ok=True)
            previous = games / 'keep'
            previous.write_text('committed')
            # A normal extra path is allowed; a case-fold duplicate is forbidden.
            if bad[0] == 'Games/A':
                self.archive([('Games/Game0/Scalinus.spo', 'native'), bad, ('Games/a', 'two')])
                lane.store = FixtureStore(cfg.fixtures, self.raw['fixture_hashes'])
            with self.subTest(member=bad[0]), self.assertRaises(ValueError):
                lane.restore('creature', cfg.profile('host'))
            self.assertEqual(previous.read_text(), 'committed')

    def test_failed_hash_preserves_previous_save(self):
        path = self.archive([('Games/Game0/Scalinus.spo', 'native')])
        cfg = self.config()
        path.write_bytes(b'changed after manifest')
        lane = WindowsLane(cfg)
        games = lane.profile('host') / 'Games'
        games.mkdir()
        (games / 'keep').write_text('committed')
        with self.assertRaisesRegex(ValueError, 'hash'):
            lane.restore('creature', cfg.profile('host'))
        self.assertEqual((games / 'keep').read_text(), 'committed')

    def test_private_deploy_retires_only_obsolete_symbol_maps(self):
        for name in self.raw['payload_hashes']:
            path = Path(self.raw['build']) / name
            path.parent.mkdir(exist_ok=True)
            path.write_bytes(name.encode())
            self.raw['payload_hashes'][name] = digest(path)
        symbol = Path(self.raw['symbols']) / 'gog2024.sym'
        symbol.parent.mkdir()
        symbol.write_text('verified source maps')
        self.raw['symbol_hashes']['gog2024.sym'] = digest(symbol)
        cfg = self.config()
        cfg.installation.mkdir(parents=True)
        (cfg.installation / 'sporecoop-test-copy.json').write_text('{}')
        for target in cfg.payload_targets().values():
            target.parent.mkdir(parents=True, exist_ok=True)
        symbols = cfg.installation / 'SporebinEP1/mods/sporekit/symbols'
        symbols.mkdir(parents=True)
        (symbols / 'old.sym').write_text('stale override')
        (symbols / 'keep.txt').write_text('unrelated diagnostic')
        lane = WindowsLane(cfg)
        try:
            with patch('windows_lab.inside_lab', side_effect=lambda path: path):
                lane.deploy()
            cfg.verify_installation()
            self.assertEqual({p.name for p in symbols.iterdir()}, {'gog2024.sym', 'keep.txt'})
        finally:
            lane.close()

    def test_restore_and_fresh_warm_slot_keep_loaded_tree(self):
        self.archive([('Games/Game0/Scalinus.spo', 'native')])
        cfg = self.config()
        lane = WindowsLane(cfg)
        lane.restore('creature', cfg.profile('host'))
        lane.labs['host'] = object()
        lane.restore('creature', cfg.profile('host'), '10')
        games = cfg.profile_dir('host') / 'Games'
        self.assertEqual((games / 'Game0/Scalinus.spo').read_text(), 'native')
        self.assertEqual((games / 'Game10/Scalinus.spo').read_text(), 'native')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            lane.restore('creature', cfg.profile('host'), '10')
        with self.assertRaisesRegex(RuntimeError, 'loaded'):
            lane.restore('creature', cfg.profile('host'))

    def test_real_packages_are_case_dependencies_not_universal(self):
        self.assertEqual(required_fixtures([
            {'name': n} for n in ('unit', 'creature', 'ui', 'team', 'space_play', 'space_page', 'chaos_tribe')
        ]), ['creature.tgz', 'space.tgz', 'tribe.tgz'])
        self.assertEqual(required_fixtures([dict(name='asset', fixture='creature',
                                              creation_packages=['creations-guest.package'])]),
                         ['creations-guest.package', 'creature.tgz'])

    def test_missing_package_starts_clean_without_fabricating_a_package(self):
        cfg = self.config()
        lane = WindowsLane(cfg)
        lane.restore_creations()
        self.assertFalse((cfg.profile_dir('guest') / 'EditorSaves.package').exists())
        self.assertFalse((cfg.profile_dir('host') / 'EditorSaves.package').exists())

    def test_viewport_is_native_observation(self):
        class Console:
            def call(self, _):
                return {'out': 'screen 1440 1080\nother status'}
        self.assertEqual(viewport_of(Console()), (1440, 1080))
        class Canvas:
            def call(self, _):
                return {'out': 'FFFFFFF0 0 0 1024 768 enabled=1'}
        self.assertEqual(canvas_of(Canvas()), (0, 0, 1024, 768))

    def test_original_commands_are_preserved_and_never_shell_executed(self):
        path, args, environment = original_command(dict(cmd='SPORE_FIX_OFF=foo python3 tools/tests/coop_team.py --x'))
        self.assertEqual(path.name, 'coop_team.py')
        self.assertEqual(args, ['--x'])
        self.assertEqual(environment, {'SPORE_FIX_OFF': 'foo'})
        with self.assertRaises(ValueError):
            original_command(dict(cmd='echo RESULT: OK'))
        with self.assertRaises(ValueError):
            original_command(dict(cmd='python3 ../outside.py'))

    def test_unit_adapter_runs_only_the_real_repository_target(self):
        command = ('wsl', '-e', 'bash', '-lc',
                   'PATH=/native/git-fast:$PATH make -j6 B=out/build0715 HOSTCXX=/usr/bin/clang++ '
                   'LLVM_MINGW=/native/toolchain TEST_JOBS=4 test')
        self.assertTrue(real_unit_command(command))
        self.assertTrue(real_unit_command(('wsl', '-e', 'make', 'test')))
        for bad in (('py', '-c', 'print("RESULT: OK")'), command[:4] + (command[4] + '; echo pass',),
                    command[:4] + ('make --eval=test: test',), command[:4] + ('echo RESULT:OK',)):
            self.assertFalse(real_unit_command(bad))

    def test_skip_missing_result_crash_and_gate_failure_never_pass(self):
        test = dict(name='creature')
        self.assertTrue(verdict(test, 0, 'RESULT: OK', []))
        for code, text, failures in [(0, 'RESULT: SKIP', []), (0, 'entered preview', []),
                                     (0, 'RESULT: INCONCLUSIVE no fixture precondition', []),
                                     (1, 'RESULT: OK', []), (0, 'RESULT: OK', ['new crash']),
                                     (0, 'RESULT: OK\nTraceback', [])]:
            with self.subTest(code=code, text=text, failures=failures):
                self.assertFalse(verdict(test, code, text, failures))

    def test_real_unit_boundary_ignores_selftests_planted_verdict_strings(self):
        from windows_native_suite import skipped_case
        test = dict(name='unit')
        text = ('ok selftest: RESULT: FAIL\n'
                'ok selftest: RESULT: SKIP\n'
                'ok selftest: RESULT: INCONCLUSIVE\n'
                'ok selftest: Traceback\n')
        self.assertTrue(verdict(test, 0, text, []))
        self.assertFalse(skipped_case(test, text))
        self.assertFalse(verdict(test, 2, text, []))
        self.assertFalse(verdict(test, 0, text, ['supervisor failed']))
        self.assertTrue(skipped_case(dict(name='creature'), 'RESULT: INCONCLUSIVE'))
        self.assertFalse(verdict(dict(name='creature'), 0, text, []))

    def test_native_worker_logs_unicode_even_with_legacy_ambient_encoding(self):
        from windows_native_suite import case_environment
        # Actual native lobby colour swatch from the Windows UI scenario.
        row = 'Alice (host, you) \u25a0\u25a0\u25a0'
        with patch.dict(os.environ, PYTHONUTF8='0', PYTHONIOENCODING='cp1252'):
            env = case_environment(self.root, dict(monitor='off'), 'ui')
            self.assertEqual(os.environ['PYTHONIOENCODING'], 'cp1252')
            self.assertEqual(env['SPORE_LAB_RUN'], str(self.root))
            self.assertEqual(env['SPORE_LAB_TEST'], 'ui')
            self.assertEqual(env['SPORE_MONITOR'], 'off')
            result = subprocess.run([sys.executable, '-c', 'print(' + ascii(row) + ')'],
                                    capture_output=True, env=env, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr.decode('utf-8', 'replace'))
        self.assertEqual(result.stdout.decode('utf-8').splitlines(), [row])

    @staticmethod
    def readiness_fixture(pauses, replacement=None, raw_snapshot=False):
        class Clock:
            now = 0
            def clock(self):
                return self.now
            def sleep(self, value):
                self.now += value
        class Native:
            poll = 0
            def __init__(self):
                self.calls = []
            def call(self, command):
                self.calls.append(command)
                if command == 'game.mode':
                    self.poll += 1
                    return dict(ok=True, out='0x01654C04 Tribe')
                if command == 'coop.avatar':
                    name = 'Other.spo' if replacement and self.poll >= replacement else 'Zorispe.spo'
                    raw = 'snapshot.spo' if raw_snapshot else name
                    return dict(ok=True, out=f"Tribe: local=1234 place=0xA3930F03 world '{name}'\ngame_name '{raw}'")
                if command == 'coop.pauses':
                    paused = pauses[min(self.poll - 1, len(pauses) - 1)]
                    return dict(ok=True, out='pauses: flags=3 paused=1 reasons: 04BF38A7=1' if paused
                                else 'pauses: flags=0 paused=0 reasons: none')
                if command == 'coop.uistate':
                    return dict(ok=True, out='open=0 input_lock=0')
                if command == 'frame':
                    return dict(ok=True, out=f'frame={self.poll}')
                raise AssertionError('readiness attempted an unapproved command: ' + command)
        return Native(), Clock()

    def test_native_scene_waits_for_natural_pause_clear_and_stable_frames(self):
        from windows_native_lane import wait_scene_ready
        native, clock = self.readiness_fixture([True, True, False])
        receipt = self.root / 'readiness.json'
        result = wait_scene_ready(dict(host=native), 'Tribe', timeout=2, receipt=receipt,
                                  clock=clock.clock, sleep=clock.sleep)
        self.assertEqual(result['phase'], 'ready')
        self.assertGreaterEqual(result['elapsed'], .75)
        self.assertEqual(json.loads(receipt.read_text())['contexts']['host'][2], 'Zorispe.spo')

    def test_native_scene_persistent_pause_fails_with_native_reason(self):
        from windows_native_lane import wait_scene_ready
        native, clock = self.readiness_fixture([True])
        receipt = self.root / 'readiness.json'
        with self.assertRaisesRegex(RuntimeError, '04BF38A7'):
            wait_scene_ready(dict(host=native), 'Tribe', timeout=.5, receipt=receipt,
                             clock=clock.clock, sleep=clock.sleep)
        result = json.loads(receipt.read_text())
        self.assertEqual(result['phase'], 'failed')
        self.assertIn('paused=1', result['last']['host']['pauses'])

    def test_native_scene_replacement_rejects_the_original_context(self):
        from windows_native_lane import wait_scene_ready
        native, clock = self.readiness_fixture([True, False], replacement=2)
        receipt = self.root / 'readiness.json'
        with self.assertRaisesRegex(RuntimeError, 'world context changed'):
            wait_scene_ready(dict(host=native), 'Tribe', timeout=2, receipt=receipt,
                             clock=clock.clock, sleep=clock.sleep)
        result = json.loads(receipt.read_text())
        self.assertEqual(result['phase'], 'failed')
        self.assertEqual(result['contexts']['host'][2], 'Zorispe.spo')

    def test_native_scene_uses_logical_world_during_a_native_snapshot(self):
        from windows_native_lane import wait_scene_ready
        native, clock = self.readiness_fixture([False], raw_snapshot=True)
        result = wait_scene_ready(dict(host=native), 'Tribe', timeout=2,
                                  clock=clock.clock, sleep=clock.sleep)
        self.assertEqual(result['contexts']['host'][2], 'Zorispe.spo')
        self.assertIn("game_name 'snapshot.spo'", result['last']['host']['avatar'])

    def test_receipt_uses_existing_lab_coverage_and_log_gate_schema(self):
        import time
        import loggates
        import labcov
        cfg = self.config()
        run = self.root / 'lab/20261120-120000-windows-test'
        run.mkdir(parents=True)
        rows = [dict(name='creature', ok=True, verdict='PASS', log=str(run / 'creature.log')),
                dict(name='ui', ok=False, verdict='INCONCLUSIVE', skipped=True, log=str(run / 'ui.log'))]
        data = lab_record(cfg, rows, run.name, 'fix/windows-native', time.time())
        (run / 'results.json').write_text(json.dumps(data), encoding='utf-8')
        (run / 'creature.host.game.log').write_text('', encoding='utf-8')
        recorded = list(loggates.recorded([str(run.parent)]))
        self.assertEqual(len(recorded), 2)
        self.assertEqual(recorded[0][1]['head'], cfg.source_commit)
        self.assertFalse(data['ok'], 'an inconclusive case cannot pass the native gate')
        logs, _ = loggates.result_logs(str(run), rows[0])
        self.assertIn('host', logs)
        self.assertIn('creature', labcov.runs_by_test([str(run.parent)]))

    @unittest.skipUnless(os.name == 'nt', 'Windows Job kernel test runs natively on Windows')
    def test_job_cleanup_stops_only_owned_case_process(self):
        import subprocess
        job = OwnedJob()
        owned = subprocess.Popen([sys.executable, '-c', 'import time;time.sleep(60)'])
        unrelated = subprocess.Popen([sys.executable, '-c', 'import time;time.sleep(60)'])
        try:
            job.assign(owned)
            job.close()
            self.assertIsNotNone(owned.wait(timeout=10))
            self.assertIsNone(unrelated.poll(), 'an independent process must remain untouched')
        finally:
            job.close()
            for process in (owned, unrelated):
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)

    @unittest.skipUnless(os.name == 'nt', 'native WinError contract')
    def test_termination_race_requires_this_owned_handle_to_have_exited(self):
        from types import SimpleNamespace
        from unittest.mock import Mock
        from windows_lab import DesktopProcess
        for wait_result, exited in ((0, True), (258, False)):
            process = DesktopProcess.__new__(DesktopProcess)
            process.handle = 123
            process.kernel = SimpleNamespace(TerminateProcess=Mock(return_value=False),
                                             WaitForSingleObject=Mock(return_value=wait_result))
            process.poll = Mock(side_effect=[None, 42] if exited else [None])
            with patch('windows_lab.C.get_last_error', return_value=5):
                if exited:
                    process.terminate()
                    process.kernel.WaitForSingleObject.assert_called_once_with(123, 1000)
                    self.assertEqual(process.poll.call_count, 2)
                else:
                    with self.assertRaises(OSError):
                        process.terminate()


if __name__ == '__main__':
    unittest.main()
