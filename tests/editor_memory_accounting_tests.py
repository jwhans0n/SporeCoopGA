"""Pure fake-reader tests: no game connection, input, process or profile access."""
from pathlib import Path
import copy
import sys
import unittest

HERE = Path(__file__).resolve().parents[1]
REPO = next(parent for parent in Path(__file__).resolve().parents
            if (parent / 'Makefile').is_file() and (parent / 'tools/tests').is_dir())
sys.path.insert(0, str(REPO / 'tools/tests'))
sys.path.insert(0, str(HERE / 'tools/tests'))
from editor_memory_accounting import parse_accounting, collect_sample, AccountingError, NotSettled
from coop_editor_performance import process_roster, FixtureError


def counters(**changed):
    result = dict(history=128, decisions=192, coordinator_history=128,
        coordinator_decisions=192, replica_history=0, replica_decisions=0,
        hashes=193, lease_decisions=64, resource_size=52729, resource_capacity=65536,
        native_current_size=15676, native_current_capacity=16000,
        store_document_size=116163, store_document_capacity=120000,
        transport_connections=3, transport_sent_packets=3072,
        transport_payload_capacity=2048, receiver_data_capacity=1048576,
        history_limit=128, decision_limit=1024, hash_limit=272,
        lease_reply_limit_per_channel=64, transport_packet_history_limit_per_connection=1024,
        state_estimate=245808, coordinator_estimate=245808, replica_estimate=0,
        store_native_size=15676, store_native_capacity=16000,
        record_data_size=15676, record_data_capacity=16000,
        immutable_base_capacity=15310, native_before_capacity=15676,
        native_reopen_capacity=0, requested_scopes=0,
        future_counter=77)
    result.update(changed)
    return result


def response(values=None):
    values = counters() if values is None else values
    return ('Passive editor container counts/capacities; shared references can alias; '
        'estimates are not allocator or process bytes.\n' +
        ''.join(key + '=' + str(value) + '\n' for key, value in values.items()))


class FakeGame:
    def __init__(self, instance):
        self.instance = instance
        self.commands = []
        self.in_menu = False
        self.on_accounting = None
        self.accounting = response()
        self.core_count = 128
        self.state = dict(revision=192, durable=192, phase=4, frozen=0,
            ready=1, editing=1, gesture=0, playtesting=0, selection='1:1',
            text='revision=192 durable=192 frozen=0 settled=1 phase=4 '
                 'project=123456789abcdef0123456789abcdef0 capability=0:1\n'
                 'editor_native generation=7 history=25/25\nadventure content: registered=1\n')

    def call(self, command):
        self.commands.append(command)
        if command == 'coop.editor inspect':
            if self.in_menu:
                raise AssertionError('The collector tried to capture a native resource at Galaxy')
            return 'placements=1 bytes=15676 source=408A0000!12345678.366A930D'
        if command == 'coop.editor history':
            return 'local=0:1 count=' + str(self.core_count)
        if command == 'coop.editor accounting':
            if self.on_accounting:
                self.on_accounting()
            return self.accounting
        raise AssertionError('Non-diagnostic game operation: ' + command)

    def menu(self):
        return self.in_menu


class CollectorTests(unittest.TestCase):
    def setUp(self):
        self.games = [FakeGame(instance) for instance in range(89, 93)]
        self.records = {game.instance: {'pid': 101 + index} for index, game in enumerate(self.games)}
        self.environment = {'cpu': [{'Name': 'fake CPU'}], 'gpu': [{'DriverVersion': 'fake driver'}],
            'spore_pids': [101, 102, 103, 104]}
        self.memory_calls = []
        self.window_checks = 0

    def sample(self, **changed):
        def memory(pid):
            self.memory_calls.append(pid)
            return {'private_bytes': 100000 + len(self.memory_calls), 'working_set': 200000}
        def windows():
            self.window_checks += 1
            return {'all_owned_hidden_muted': True}
        arguments = dict(mode='editing', strict=True,
            status=lambda game: copy.deepcopy(game.state), memory=memory,
            machine=lambda: copy.deepcopy(self.environment), process_roster=process_roster,
            verify_windows=windows, baseline_environment=copy.deepcopy(self.environment))
        arguments.update(changed)
        return collect_sample(self.games, self.records, **arguments)

    def test_all_accounting_fields_and_raw_evidence_preserved(self):
        self.assertEqual(parse_accounting(response()), counters())
        sample = self.sample()
        self.assertTrue(sample['roster_complete'])
        self.assertTrue(sample['strict_roster'])
        self.assertEqual(len(sample['processes']), 4)
        self.assertEqual(len(self.memory_calls), 8)
        self.assertEqual(self.window_checks, 2)
        for game in self.games:
            row = sample['processes'][str(game.instance)]
            self.assertEqual(row['accounting'], counters())
            self.assertEqual(row['accounting_raw'], response())
            self.assertEqual(row['accounting']['decisions'], 192)
            self.assertLess(row['accounting']['decisions'], row['accounting']['decision_limit'])
            self.assertEqual(row['history_size'], 25)
            self.assertEqual(row['registered'], 1)
            self.assertEqual(row['native_serialized_bytes'], 15676)
            self.assertEqual(row['native_status'], row['native_status_after'])
            self.assertEqual(row['private_bytes'], row['os_after_accounting']['private_bytes'])
            self.assertGreater(row['private_bytes'], row['os_before_accounting']['private_bytes'])
            self.assertEqual(game.commands, ['coop.editor inspect', 'coop.editor history', 'coop.editor accounting'])
        self.assertNotIn('memory_gate_passed', sample)

    def test_each_strict_sample_rejects_an_unmeasured_or_missing_game(self):
        self.environment['spore_pids'].append(999)
        with self.assertRaises(FixtureError):
            self.sample(baseline_environment=None)
        self.assertFalse(any(game.commands for game in self.games))
        self.environment['spore_pids'] = [101, 102, 103]
        with self.assertRaises(FixtureError):
            self.sample(baseline_environment=None)

    def test_explicit_diagnostic_records_extra_process_but_cannot_accept(self):
        self.environment['spore_pids'].append(999)
        sample = self.sample(strict=False, baseline_environment=None)
        self.assertFalse(sample['roster_complete'])
        self.assertFalse(sample['strict_roster'])
        self.assertNotIn('memory_gate_passed', sample)

    def test_replaced_hardware_or_driver_rejects_sample(self):
        baseline = copy.deepcopy(self.environment)
        self.environment['gpu'][0]['DriverVersion'] = 'changed'
        with self.assertRaises(AccountingError):
            self.sample(baseline_environment=baseline)
        self.assertFalse(any(game.commands for game in self.games))

    def test_unsettled_or_nondurable_state_is_retryable_without_capture(self):
        self.games[1].state['text'] = self.games[1].state['text'].replace('settled=1', 'settled=0')
        with self.assertRaises(NotSettled):
            self.sample()
        self.assertFalse(any(game.commands for game in self.games))
        self.games[1].state['text'] = self.games[1].state['text'].replace('settled=0', 'settled=1')
        self.games[1].state['durable'] = 191
        with self.assertRaises(NotSettled):
            self.sample()

    def test_wrong_project_or_frozen_replica_fails(self):
        self.games[1].state['text'] = self.games[1].state['text'].replace('project=1234', 'project=5678')
        with self.assertRaises(AccountingError):
            self.sample()
        self.games[1].state['frozen'] = 1
        with self.assertRaises(AccountingError):
            self.sample()

    def test_changed_state_during_collection_is_not_an_accepted_sample(self):
        self.games[0].on_accounting = lambda: self.games[0].state.update(revision=193, durable=193)
        with self.assertRaises(NotSettled):
            self.sample()

    def test_post_collection_admission_is_rechecked(self):
        for field, value in [('ready', 0), ('selection', '2:1')]:
            with self.subTest(field=field):
                self.setUp()
                self.games[0].on_accounting = lambda f=field, v=value: self.games[0].state.update({f: v})
                with self.assertRaises(NotSettled):
                    self.sample()
        self.setUp()
        self.games[0].on_accounting = lambda: self.games[0].state.update(
            text=self.games[0].state['text'].replace('settled=1', 'settled=0'))
        with self.assertRaises(NotSettled):
            self.sample()

    def test_same_revision_project_or_native_owner_change_is_rejected(self):
        for original, changed in [('project=1234', 'project=5678'),
                                  ('capability=0:1', 'capability=0:2'),
                                  ('generation=7', 'generation=8')]:
            with self.subTest(original=original):
                self.setUp()
                def replace(o=original, c=changed):
                    for game in self.games:
                        game.state['text'] = game.state['text'].replace(o, c)
                self.games[0].on_accounting = replace
                with self.assertRaises(NotSettled):
                    self.sample()

    def test_process_started_during_collection_is_rejected(self):
        self.games[0].on_accounting = lambda: self.environment['spore_pids'].append(999)
        with self.assertRaises(FixtureError):
            self.sample()

    def test_dormant_history_is_separate_after_end(self):
        for game in self.games:
            game.state.update(phase=0, revision=0, durable=0)
            game.state['text'] = game.state['text'].replace('history=25/25', 'history=0/0')
            game.core_count = 0
            game.accounting = response(counters(history=0, decisions=0))
        sample = self.sample(mode='detached')
        for row in sample['processes'].values():
            self.assertEqual(row['core_history_count'], 0)
            self.assertEqual(row['accounting']['coordinator_history'], 128)
            self.assertEqual(row['history_size'], 0)

    def test_galaxy_records_no_native_snapshot_without_claiming_zero_bytes(self):
        for game in self.games:
            game.in_menu = True
            game.state.update(phase=0, editing=0, ready=0)
            game.state['text'] = game.state['text'].replace('history=25/25', 'history=0/0')
        sample = self.sample(mode='galaxy')
        for game in self.games:
            row = sample['processes'][str(game.instance)]
            self.assertIsNone(row['native_serialized_bytes'])
            self.assertIsNone(row['inspect'])
            self.assertFalse(row['native_inspect_available'])
            self.assertNotIn('coop.editor inspect', game.commands)
            self.assertEqual(row['accounting']['native_current_capacity'], 16000)

    def test_bad_actual_history_or_missing_accounting_fails_closed(self):
        self.games[0].state['text'] = self.games[0].state['text'].replace('history=25/25', 'history=26/26')
        with self.assertRaises(AccountingError):
            self.sample()
        self.games[0].state['text'] = self.games[0].state['text'].replace('history=26/26', 'history=25/25')
        self.games[0].accounting = 'unavailable'
        with self.assertRaises(AccountingError):
            self.sample()


class ParserTests(unittest.TestCase):
    def test_rejects_malformed_duplicate_and_overflow_fields(self):
        for invalid in [response() + 'history=1\n', response() + 'bad=-1\n',
                        response() + 'bad=1.5\n', response() + 'bad=18446744073709551616\n',
                        response() + 'x=' + '1' * 21 + '\n', response() + 'x' * 8192]:
            with self.subTest(invalid=invalid[-80:]), self.assertRaises(AccountingError):
                parse_accounting(invalid)

    def test_rejects_missing_categories_or_changed_bounds(self):
        values = counters()
        del values['transport_connections']
        for invalid in [response(values), response(counters(history=129)),
                        response(counters(coordinator_decisions=1025)),
                        response(counters(hashes=273)), response(counters(history_limit=256)),
                        response(counters(transport_sent_packets=3073))]:
            with self.subTest(invalid=invalid[-80:]), self.assertRaises(AccountingError):
                parse_accounting(invalid)

    def test_shared_alias_capacities_are_not_summed_or_reported_as_heap(self):
        values = counters(resource_capacity=100000000, native_current_capacity=100000000,
            store_document_capacity=100000000)
        self.assertEqual(parse_accounting(response(values)), values)


if __name__ == '__main__':
    unittest.main(verbosity=2)
