"""Passive native-editor retention samples, never a memory acceptance decision.

All game operations are injected read-only diagnostics. This module cannot launch,
stop, join, leave, input, restore or save a game. Logical retained capacities are
kept separate from operating-system process counters and native history counts.
"""
import re
import time


class AccountingError(ValueError):
    pass


class NotSettled(AccountingError):
    """The owner may retry this read-only sample within its existing deadline."""


LIMITS = {
    'history_limit': 128,
    'decision_limit': 1024,
    'hash_limit': 272,
    'lease_reply_limit_per_channel': 64,
    'transport_packet_history_limit_per_connection': 1024,
}
# Periodic reliable presence is expected even while the authored draft is idle.
# Its acknowledgement queue is measured, not confused with pending authoring.
QUIESCENT = {
    'input_count', 'local_count', 'queued_packets', 'queued_transfers',
    'pending_leases', 'release_fences', 'lease_retirements', 'deferred_count',
    'held_count', 'staged_size', 'receiving', 'requested_scopes', 'actions',
    'guarded_placements', 'pending_native_capacity', 'adventure_queue_count',
}
REQUIRED = set(LIMITS) | {
    'history', 'decisions', 'coordinator_history', 'coordinator_decisions',
    'replica_history', 'replica_decisions', 'hashes', 'lease_decisions',
    'resource_size', 'resource_capacity', 'native_current_size', 'native_current_capacity',
    'store_document_size', 'store_document_capacity', 'transport_connections',
    'transport_sent_packets', 'transport_payload_capacity', 'receiver_data_capacity',
    'state_estimate', 'coordinator_estimate', 'replica_estimate',
    'store_native_size', 'store_native_capacity', 'record_data_size', 'record_data_capacity',
    'immutable_base_capacity', 'native_before_capacity', 'native_reopen_capacity',
    'requested_scopes',
} | QUIESCENT


def parse_accounting(text):
    if not isinstance(text, str) or len(text.encode('utf-8')) > 8192:
        raise AccountingError('Passive accounting response exceeds its bounded diagnostic size')
    lines = text.splitlines()
    if not lines or not lines[0].startswith('Passive editor container counts/capacities;'):
        raise AccountingError('The candidate lacks the passive accounting diagnostic')
    result = {}
    for line in lines[1:]:
        found = re.fullmatch(r'([a-z][a-z0-9_]{0,79})=([0-9]{1,20})', line)
        if not found or found[1] in result or int(found[2]) > 2**64 - 1:
            raise AccountingError('Malformed, duplicate or overflowing passive accounting field')
        result[found[1]] = int(found[2])
    if not REQUIRED.issubset(result):
        raise AccountingError('Incomplete passive accounting categories: ' + repr(sorted(REQUIRED - result.keys())))
    for field, expected in LIMITS.items():
        if result[field] != expected:
            raise AccountingError('The memory candidate changed the recorded container limit: ' + field)
    for prefix in ('', 'coordinator_', 'replica_'):
        if result[prefix + 'history'] > 128 or result[prefix + 'decisions'] > 1024:
            raise AccountingError('The candidate exceeded its retained Core history/decision count bound')
    if result['hashes'] > result['hash_limit']:
        raise AccountingError('The candidate exceeded its retained revision-hash count bound')
    if result['transport_sent_packets'] > result['transport_connections'] * 1024:
        raise AccountingError('The candidate exceeded its per-connection sent-packet history bound')
    return result


def _number(text, field):
    found = re.search(r'\b' + field + r'=([0-9]+)\b', text)
    if not found:
        raise AccountingError('Native retention status lacks ' + field)
    return int(found[1])


def _admitted(states, mode):
    if mode not in ('editing', 'detached', 'galaxy'):
        raise AccountingError('Unsupported retention sample lifecycle mode')
    phase = 4 if mode == 'editing' else 0
    for state in states:
        if state.get('frozen'):
            raise AccountingError('A participant froze; no settled retention sample is valid')
        if state.get('phase') != phase or state.get('gesture') or state.get('playtesting'):
            raise NotSettled('The native editor lifecycle has not settled')
        _number(state['text'], 'settled')  # Preserve the stronger transport-idle diagnostic below.
        if mode == 'galaxy':
            if state.get('editing'):
                raise NotSettled('The native editor has not returned to Galaxy')
        elif not state.get('ready') or not state.get('editing'):
            raise NotSettled('The native authoring resource is not ready')
    if mode == 'editing':
        revisions = {state.get('revision') for state in states}
        if len(revisions) != 1 or any(state.get('durable') != state.get('revision') for state in states):
            raise NotSettled('The current accepted draft is not durably present on all participants')
        projects = [re.search(r'\bproject=([0-9a-f]{32})\b', state['text']) for state in states]
        if any(not project or project[1] == '0' * 32 for project in projects) or len({project[1] for project in projects}) != 1:
            raise AccountingError('The settled participants do not share one nonempty editor project')


def _context(state):
    text = state['text']
    result = []
    for pattern in (
        r'\bproject=([0-9a-f]{32})\b', r'\bcapability=([0-9]+:[0-9]+)\b',
        r'\beditor_native[^\n]*\bgeneration=([0-9]+)\b'):
        found = re.search(pattern, text)
        if not found:
            raise AccountingError('The retention diagnostic lacks its exact logical/native context')
        result.append(found[1])
    return tuple(result)


def _quiet(accounting):
    pending = sorted(field for field in QUIESCENT if accounting[field])
    if pending:
        raise NotSettled('The editor still has authoring/transfer work outstanding: ' + repr(pending))


def _checks(games, mode):
    if mode != 'editing':
        return None
    checks = [game.call('coop.editor check').strip() for game in games]
    if any(not re.fullmatch(r'CONVERGED [0-9a-f]{64}', value) for value in checks) or len(set(checks)) != 1:
        raise NotSettled('Native canonical hashes have not converged on all participants')
    return checks


def collect_sample(games, records, *, mode, strict, status, memory, machine,
                   process_roster, verify_windows, baseline_environment=None):
    """Collect one settled read-only sample; never classifies allocator leaks.

    A diagnostic prewarm may record an incomplete roster. Every admitted strict
    baseline/cycle/release sample rechecks all four actual owned process IDs.
    """
    if len(games) != 4 or len({game.instance for game in games}) != 4:
        raise AccountingError('Retention collection requires the exact owned four-editor roster')
    measured = [records[game.instance]['pid'] for game in games]
    if len(set(measured)) != 4:
        raise AccountingError('Retention process identities are not distinct')
    begun = time.time()
    safety = verify_windows()
    states = [status(game) for game in games]
    _admitted(states, mode)
    contexts = [_context(state) for state in states]
    checks = _checks(games, mode)
    environment = machine()
    complete = process_roster(environment, measured, diagnostic=not strict)
    if strict and not complete:
        raise AccountingError('Strict retention samples require every live Spore process')
    if baseline_environment is not None and any(environment.get(key) != baseline_environment.get(key) for key in ('cpu', 'gpu')):
        raise AccountingError('CPU/GPU/driver identity changed during the retained-memory window')
    if mode == 'galaxy' and any(not game.menu() for game in games):
        raise NotSettled('Every native game must finish its normal Galaxy exit')
    row = {'time': begun, 'mode': mode, 'strict_roster': bool(strict),
        'roster_complete': complete, 'environment': environment, 'safety': safety,
        'measurement_scope': 'No timed FPS sample. OS counters and logical retained containers are separate.',
        'processes': {}}
    for game, state in zip(games, states):
        history_match = re.search(r'\bhistory=([0-9]+)/([0-9]+)\b', state['text'])
        registered = re.search(r'\badventure content:[^\n]*\bregistered=([0-9]+)\b', state['text'])
        if not history_match or not registered:
            raise AccountingError('Native history or registered-package count is unavailable')
        if not 0 <= int(history_match[1]) <= int(history_match[2]) <= 25:
            raise AccountingError('Native history exceeded the verified 25-entry bound')
        inspect = game.call('coop.editor inspect') if mode != 'galaxy' else None
        history = game.call('coop.editor history')
        core_count = re.search(r'\bcount=([0-9]+)\b', history)
        native_size = re.search(r'\bbytes=([0-9]+)\b', inspect or '')
        if not core_count or (mode != 'galaxy' and not native_size):
            raise AccountingError('Core history or live native serialized-byte count is unavailable')
        before_accounting = memory(records[game.instance]['pid'])
        clock = time.monotonic()
        raw = game.call('coop.editor accounting')
        accounting = parse_accounting(raw)
        _quiet(accounting)
        collection_seconds = time.monotonic() - clock
        after_accounting = memory(records[game.instance]['pid'])
        if accounting['history'] != int(core_count[1]):
            raise AccountingError('Passive and ordinary Core history counters disagree')
        row['processes'][str(game.instance)] = dict(
            pid=records[game.instance]['pid'], **after_accounting,
            os_before_accounting=before_accounting, os_after_accounting=after_accounting,
            accounting=accounting, accounting_raw=raw,
            accounting_collection_seconds=collection_seconds,
            revision=state.get('revision'), durable=state.get('durable'),
            history_cursor=int(history_match[1]), history_size=int(history_match[2]),
            registered=int(registered[1]), core_history_count=int(core_count[1]),
            native_serialized_bytes=int(native_size[1]) if native_size else None,
            native_inspect_available=mode != 'galaxy',
            selection=state.get('selection'), native_status=state['text'],
            transport_ack_idle=bool(_number(state['text'], 'settled')),
            inspect=inspect, core_history=history,
            accounting_interpretation='Shared references may alias. Capacity units are field-specific; estimates are not allocator/process bytes.')
    after = [status(game) for game in games]
    _admitted(after, mode)
    accounting_after = [parse_accounting(game.call('coop.editor accounting')) for game in games]
    for value in accounting_after:
        _quiet(value)
    if _checks(games, mode) != checks:
        raise NotSettled('The native canonical draft changed while passive counters were collected')
    for before, current, context in zip(states, after, contexts):
        if (any(before.get(key) != current.get(key) for key in
                ('revision', 'durable', 'phase', 'frozen', 'editing', 'ready', 'gesture', 'playtesting', 'selection')) or
                context != _context(current)):
            raise NotSettled('The editor changed while passive counters were being collected')
    environment_after = machine()
    complete_after = process_roster(environment_after, measured, diagnostic=not strict)
    if any(environment_after.get(key) != environment.get(key) for key in ('cpu', 'gpu')):
        raise AccountingError('CPU/GPU/driver identity changed while collecting passive counters')
    safety_after = verify_windows()
    if mode == 'galaxy' and any(not game.menu() for game in games):
        raise NotSettled('A game left Galaxy while passive counters were being collected')
    for game, state, counts in zip(games, after, accounting_after):
        row['processes'][str(game.instance)]['native_status_after'] = state['text']
        row['processes'][str(game.instance)]['selection_after'] = state.get('selection')
        row['processes'][str(game.instance)]['transport_ack_idle_after'] = bool(_number(state['text'], 'settled'))
        row['processes'][str(game.instance)]['accounting_after'] = counts
    row['canonical_checks'] = checks
    row['admission'] = 'Same native canonical/durable draft with empty authoring and transfer queues before/after. Periodic presence acknowledgements remain measured.'
    row['environment_after'] = environment_after
    row['safety_after'] = safety_after
    row['roster_complete'] = complete and complete_after
    row['finished_time'] = time.time()
    row['collection_seconds'] = row['finished_time'] - begun
    return row
