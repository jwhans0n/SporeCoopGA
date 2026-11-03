#!/usr/bin/env python3
"""Measure actual native release/commit stalls in owned Windows editor games.

Attaches to an existing two/four-player private editor by default. --setup
bootstrap opens a private adventure through ordinary UI. LAN-only runs connect
directly through game-owned transport; WAN runs use owned impairment proxies.
Never launches games, deploys a DLL, changes
frame caps, writes memory, or accesses personal profiles. Offline contract tests
can import the parsers without opening a console connection.
"""
import argparse
from contextlib import ExitStack, contextmanager
import errno
import json
import math
import os
from pathlib import Path
import re
import time

import coop_editor_party as party_module
from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair, status, retry_grant
from coop_editor_performance import configuration, editor_uncovered, source_fence, values
from editor_network_proxy import EditorProxy, Profile
from uifind import find

ROOT = Path(__file__).resolve().parents[2]
PROFILES = {
    'lan': (Profile(rtt_ms=10), 200),
    'wan': (Profile(rtt_ms=100, loss=.01, reorder=.02, duplicate=.01), 500),
    'wan_2mbit': (Profile(rtt_ms=100, loss=.01, reorder=.02, duplicate=.01, bits_second=2000000), 750),
}
EVENT = re.compile(r'^event=(\d+) stage=([a-z_]+) qpc=(\d+) project=([0-9a-f]{32}) '
                   r'author=(\d+):(\d+) sequence=(\d+) operation_epoch=(\d+) revision=(\d+) success=([012])$')
NATIVE_SPANS = ('read', 'write', 'projection', 'resolution', 'supported')
NATIVE_COUNTS = ('parsed_image_hits', 'parsed_image_misses', 'pose_capture_hits', 'pose_capture_fallbacks')
NATIVE_OWNERS = ('parsed_image_entries', 'parsed_image_bytes', 'parsed_image_owners',
                 'parsed_image_owner_bytes', 'parsed_image_peak_owners', 'parsed_image_peak_bytes')
NATIVE_CAPTURE_PHASES = ('before', 'after_bytes', 'after_projection', 'canonical', 'opaque', 'delivery')
NATIVE_CAPTURE_FIELDS = ('calls', 'ticks', 'begin', 'end', 'frame')
NATIVE_CAPTURE_KEYS = {f'native_capture_{phase}_{field}'
                       for phase in NATIVE_CAPTURE_PHASES for field in NATIVE_CAPTURE_FIELDS}
NATIVE_WORK_KEYS = {'native_work_frequency', *NATIVE_COUNTS, *NATIVE_OWNERS}
NATIVE_WORK_KEYS.update(f'native_{name}_{field}' for name in NATIVE_SPANS for field in ('calls', 'ticks'))


def write_command_artifact(path, events, replace=os.replace, pause=time.sleep):
    """Keep the previous complete log if a diagnostic reader holds the file."""
    temporary = path.with_name(path.name + '.' + str(time.time_ns()) + '.pending')
    temporary.write_text(json.dumps(events, indent=2) + '\n', encoding='utf-8')
    for attempt in range(5):
        try:
            replace(temporary, path)
            return
        except OSError as error:
            if error.errno not in (errno.EACCES, errno.EPERM, errno.EINVAL) or attempt == 4:
                # Retain both the old complete log and new pending artifact.
                raise
            pause(.05)


@contextmanager
def hold_command_artifacts(games):
    """No fixture log serialization or file writes while native input is held."""
    games = tuple(games)
    if any(getattr(game, 'hold_command_artifact', False) for game in games):
        raise FixtureError('Nested measured command-artifact hold')
    for game in games:
        game.hold_command_artifact = True
    primary = None
    try:
        yield
    except BaseException as error:
        primary = error
        raise
    finally:
        for game in games:
            game.hold_command_artifact = False
        # measure's own finally has already released input/stopped captures.
        failures = []
        for game in games:
            try:
                game.save_log()
            except BaseException as error:
                failures.append((game, error))
        if failures:
            # Preserve an earlier Native failure and still try every observer.
            # The pending-file writer retains the complete uninstalled logs.
            raised = primary if primary is not None else failures[0][1]
            details = [dict(instance=getattr(game, 'instance', None),
                            type=type(error).__name__, error=str(error))
                       for game, error in failures]
            raised.command_artifact_errors = details
            if hasattr(raised, 'add_note'):
                for detail in details:
                    raised.add_note('Command artifact flush failed: ' + str(detail))
            if primary is None:
                raise raised


def native_work_sample(text):
    """Optional development counters, sampled outside the measured window."""
    row = values(text)
    known = NATIVE_WORK_KEYS | NATIVE_CAPTURE_KEYS
    names = re.findall(r'\b([a-z_][a-z0-9_]*)=', text)
    present = known & set(names)
    if not present:
        return None
    if any(names.count(key) != 1 for key in present) or not present <= row.keys():
        raise FixtureError('Duplicate or invalid native work counters')
    return checked_native_work({key: row[key] for key in present})


def checked_native_work(row):
    capture = NATIVE_CAPTURE_KEYS & row.keys()
    keys = NATIVE_WORK_KEYS | capture
    if (not NATIVE_WORK_KEYS <= row.keys() or (capture and capture != NATIVE_CAPTURE_KEYS) or
            any(type(row[key]) is not int or not 0 <= row[key] < 2**64 for key in keys) or
            not row['native_work_frequency']):
        raise FixtureError('Partial or invalid native work counters')
    if any(row[key] for key in NATIVE_OWNERS[:4]) or row['parsed_image_peak_owners'] > 2 or row['parsed_image_peak_bytes'] > 16 << 20:
        raise FixtureError('Native parsed owners escaped their callback or exceeded their bound')
    for phase in NATIVE_CAPTURE_PHASES if capture else ():
        calls, ticks, begin, end, frame = (row[f'native_capture_{phase}_{field}'] for field in NATIVE_CAPTURE_FIELDS)
        if ((not calls and (ticks or begin or end or frame)) or
                (calls and (not begin or end < begin or ticks < end - begin))):
            raise FixtureError('Invalid native capture phase clock or unused counter: ' + phase)
    return {key: row[key] for key in keys}


def native_work_delta(before, after):
    if before is None and after is None:
        return None
    if before is None or after is None:
        raise FixtureError('Native work counter availability or clock changed')
    before, after = checked_native_work(before), checked_native_work(after)
    if before.keys() != after.keys() or before['native_work_frequency'] != after['native_work_frequency']:
        raise FixtureError('Native work counter availability or clock changed')
    frequency = after['native_work_frequency']
    if any(after[key] < before[key] for key in before if key not in NATIVE_OWNERS):
        raise FixtureError('Native work counters reset during the gesture')
    spans = {}
    for name in NATIVE_SPANS:
        calls = after[f'native_{name}_calls'] - before[f'native_{name}_calls']
        ticks = after[f'native_{name}_ticks'] - before[f'native_{name}_ticks']
        spans[name] = dict(calls=calls, ticks=ticks, inclusive_ms=ticks * 1000 / frequency)
    staged = None
    if NATIVE_CAPTURE_KEYS <= before.keys():
        phases = {}
        for phase in NATIVE_CAPTURE_PHASES:
            prefix = f'native_capture_{phase}_'
            calls = after[prefix+'calls'] - before[prefix+'calls']
            ticks = after[prefix+'ticks'] - before[prefix+'ticks']
            begin, end, frame = (after[prefix+field] for field in ('begin', 'end', 'frame'))
            if not calls:
                if any(after[prefix+field] != before[prefix+field] for field in NATIVE_CAPTURE_FIELDS):
                    raise FixtureError('Native capture phase changed without a completed call: ' + phase)
            elif begin < before[prefix+'end'] or ticks < end - begin:
                raise FixtureError('Native capture phase cumulative clock or ordering changed: ' + phase)
            phases[phase] = dict(calls=calls, ticks=ticks, inclusive_ms=ticks * 1000 / frequency,
                                 last=dict(begin=begin, end=end, frame=frame,
                                           elapsed_ms=(end-begin) * 1000 / frequency))
        ordered = all(row['calls'] == 1 for row in phases.values())
        if ordered:
            last = [phases[phase]['last'] for phase in NATIVE_CAPTURE_PHASES]
            if any(a['end'] > b['begin'] or a['frame'] >= b['frame'] for a, b in zip(last, last[1:])):
                raise FixtureError('Native six-phase capture order or frame progression changed')
        staged = dict(clock='win32_qpc', phases=phases, single_pipeline_order_checked=ordered,
                      scope='Cumulative phase deltas and last intervals; retries or partial advances do not identify one pipeline. '
                            'Phase durations include nested native work spans; the groups must not be summed.')
    return dict(frequency=frequency, spans=spans, staged_capture=staged,
                counts={key: after[key] - before[key] for key in NATIVE_COUNTS},
                ownership={key: after[key] for key in NATIVE_OWNERS},
                scope='Release through durable settlement; nested spans overlap and must not be summed.')


def percentile(samples, fraction=.95):
    if not 0 < fraction <= 1 or not samples or any(not math.isfinite(x) or x < 0 for x in samples):
        raise FixtureError('Missing or invalid performance samples')
    return sorted(samples)[math.ceil(len(samples)*fraction)-1]


def stage_page(text):
    lines = text.splitlines()
    if not lines or not lines[0].startswith('clock=win32_qpc '):
        raise FixtureError('QPC stage instrumentation is unavailable')
    header = values(lines[0])
    required = {'pid', 'frequency', 'capture', 'started_qpc', 'recording', 'count', 'capacity',
                'overflow', 'clock_error', 'offset', 'end'}
    if not required <= header.keys() or any(type(header[k]) is not int for k in required):
        raise FixtureError('Invalid QPC stage header')
    if (not header['pid'] or not header['frequency'] or not header['capture'] or not header['started_qpc'] or
            header['recording'] or header['overflow'] or header['clock_error'] or
            not 0 <= header['offset'] <= header['end'] <= header['count'] <= header['capacity'] <= 4096 or
            header['end']-header['offset']>32):
        raise FixtureError('Incomplete, saturated or invalid QPC stage capture')
    rows = []
    for line in lines[1:]:
        match = EVENT.fullmatch(line)
        if not match:
            raise FixtureError('Truncated or invalid QPC stage row')
        index, stage, qpc, project, slot, generation, sequence, epoch, revision, success = match.groups()
        row = dict(index=int(index), stage=stage, qpc=int(qpc), project=project, author_slot=int(slot),
                   author_generation=int(generation), sequence=int(sequence), epoch=int(epoch),
                   revision=int(revision), success=int(success))
        if (row['index'] != header['offset'] + len(rows) or not any(int(c,16) for c in project) or
                not 0 <= row['author_slot'] < 4 or min(row['author_generation'], row['sequence'], row['epoch']) <= 0 or
                row['qpc'] < header['started_qpc']):
            raise FixtureError('Invalid QPC stage identity or timestamp')
        rows.append(row)
    if len(rows) != header['end'] - header['offset']:
        raise FixtureError('Missing QPC stage rows')
    return dict(header=header, events=rows)


def read_stages(game, expected_pid):
    game.call('coop.editor stages stop')
    result = stage_page(game.call('coop.editor stages read 0 32'))
    if result['header']['pid'] != expected_pid:
        raise FixtureError('Stage capture belongs to another process')
    while len(result['events']) < result['header']['count']:
        page = stage_page(game.call(f'coop.editor stages read {len(result["events"])} 32'))
        for key in ('pid', 'frequency', 'capture', 'started_qpc', 'count', 'capacity'):
            if page['header'][key] != result['header'][key]:
                raise FixtureError('Stage capture changed during pagination')
        if not page['events']:
            raise FixtureError('Stage pagination made no progress')
        result['events'].extend(page['events'])
    return result


def operation(row):
    return (row['project'], row['author_slot'], row['author_generation'], row['sequence'], row['epoch'])


def correlate(captures, author_instance, revision):
    author = captures[str(author_instance)]
    confirmations = [row for row in author['events'] if row['stage'] == 'own_confirmed' and row['revision'] == revision]
    if len(confirmations) != 1:
        raise FixtureError('Each measured release must have one exact accepted operation')
    key = operation(confirmations[0]); frequency = author['header']['frequency']
    rows_by_player = {}
    for instance, capture in captures.items():
        if capture['header']['frequency'] != frequency:
            raise FixtureError('Cross-process QPC frequencies differ')
        events = [row for row in capture['events'] if operation(row) == key]
        by_stage = {}
        for event in events:
            if event['success'] == 2:
                raise FixtureError('A measured operation contains a failed native or durable stage')
            by_stage.setdefault(event['stage'], []).append(event)
        def stamp(name):
            found = by_stage.get(name, [])
            if len(found) != 1:
                raise FixtureError(f'Participant {instance}: missing or repeated {name} stage')
            return found[0]['qpc']
        def span(first, last):
            begin, end = stamp(first), stamp(last)
            if begin > end:
                raise FixtureError(f'Participant {instance}: reordered {first}/{last} timestamps')
            return (end-begin)*1000/frequency
        row = dict(apply_ms=span('apply_work_begin','apply_work_end'),
                   persist_ms=span('persist_work_begin','persist_work_end'),
                   durable_ms=span('durable_begin','durable_end'), scene_ready_qpc=stamp('scene_ready'))
        if str(instance) == str(author_instance):
            row.update(release_to_capture_ms=span('gesture_release','capture_begin'),
                       capture_ms=span('capture_begin','capture_end'),
                       supported_ms=span('supported_begin','supported_end'),
                       inverse_ms=span('inverse_begin','inverse_end'),
                       release_to_confirmation_ms=span('gesture_release','own_confirmed'),
                       submit_to_confirmation_ms=span('local_submit','own_confirmed'))
        if key[1] != 0 and any(event['stage']=='host_request_received' for event in events):
            row['host_request_to_queue_ms'] = span('host_request_received','host_decision_queued')
        rows_by_player[instance] = row
    return dict(project=key[0], author_slot=key[1], author_generation=key[2], sequence=key[3], epoch=key[4],
                revision=revision, frequency=frequency, participants=rows_by_player)


def frame_sample(text):
    result = values(text)
    required = {'samples','invalid','recording','seconds','median_ms','p95_ms','max_ms'}
    if (not required <= result.keys() or result['invalid'] or result['recording'] or
            not 2 <= result['samples'] < 8192 or
            any(not math.isfinite(result[k]) or result[k] <= 0 for k in ('seconds','median_ms','p95_ms','max_ms')) or
            not result['median_ms'] <= result['p95_ms'] <= result['max_ms']):
        raise FixtureError('Incomplete, saturated or invalid release-window frame measurements')
    return result


def summarize(gestures, instances, samples, budget, p95_frame_ms=100, max_frame_ms=250):
    if len(gestures) != len(instances)*samples:
        raise FixtureError('Every participant must author the requested number of actual gestures')
    authors, frames = {}, {}
    seen = set()
    for gesture in gestures:
        c = gesture['correlation']; identity = tuple(c[k] for k in ('project','author_slot','author_generation','sequence','epoch'))
        if identity in seen:
            raise FixtureError('A repeated operation cannot count as another gesture')
        seen.add(identity)
    for instance in instances:
        own = [row for row in gestures if row['author_instance'] == instance]
        if len(own) != samples:
            raise FixtureError('Missing per-author samples')
        delays = [row['correlation']['participants'][str(instance)]['submit_to_confirmation_ms'] for row in own]
        releases = [row['correlation']['participants'][str(instance)]['release_to_confirmation_ms'] for row in own]
        authors[str(instance)] = dict(samples=len(own), confirmation_p95_ms=percentile(delays),
                                     release_p95_ms=percentile(releases), release_max_ms=max(releases),
                                     confirmation_budget_ms=budget, passed=percentile(delays) <= budget)
        observed = [row['frames'][str(instance)] for row in gestures]
        maxima = [row['max_ms'] for row in observed]
        worst_p95 = max(row['p95_ms'] for row in observed)
        frames[str(instance)] = dict(windows=len(observed), commit_window_max_p95_ms=percentile(maxima),
                                     worst_native_window_p95_ms=worst_p95, max_ms=max(maxima),
                                     p95_limit_ms=p95_frame_ms,max_limit_ms=max_frame_ms,
                                     passed=percentile(maxima)<=p95_frame_ms and worst_p95<=p95_frame_ms and max(maxima)<=max_frame_ms)
    return dict(authors=authors, frames=frames,
                passed=all(row['passed'] for row in authors.values()) and all(row['passed'] for row in frames.values()))


def uses_owned_proxy(args):
    return args.setup=='bootstrap' and args.profiles!=['lan']


def route_owned_join(command,instance,args,proxies):
    words=command.split()
    if words[:1]!=['coop.join']:return command
    if (args.setup!='bootstrap' or instance not in args.instances[1:] or len(words)<2 or
            words[1]!=f'127.0.0.1:{args.port}'):
        raise FixtureError('A join does not belong to this owned fixture party')
    if uses_owned_proxy(args):
        if instance not in proxies:raise FixtureError('The owned impairment proxy is unavailable')
        words[1]=f'127.0.0.1:{proxies[instance].port}'
    elif proxies:
        raise FixtureError('A direct LAN fixture must not own impairment proxies')
    return ' '.join(words)


def menu_login_button_only(game):
    # Reused offline profiles have a bottom-left Login menu button. It is not
    # the registration dialog whose close control the onboarding helper uses.
    if not game.menu() or not game.caption('Create') or game.caption("If you don't register"):
        return False
    visible=[(x,y) for x,y,shown in find(game.console,'Login') if shown]
    return bool(visible) and all(0<=x<300 and 720<=y<768 for x,y in visible)


class CommitParty(party_module.EditorParty):
    def __init__(self, args, proxies):
        self.args=args;self.output=args.output;self.output.mkdir(parents=True,exist_ok=False)
        owner=self
        self.ownership=party_module.Ownership(args.metadata,args.instances,args.expected_dll_sha256)
        self.records=self.ownership.records;self.deadline=time.monotonic()+args.seconds
        class MeasuredConsole(party_module.PublicConsole):
            def call(self,line):
                extra=(line in ('coop.frame_metrics start','coop.frame_metrics stop','coop.editor stages reset','coop.editor stages stop','bg','window','mods','log 2000') or
                       bool(re.fullmatch(r'coop.editor stages read \d{1,4} (?:[1-9]|[12][0-9]|3[0-2])',line)))
                if extra:
                    if time.monotonic()>self.deadline:raise FixtureError('The bounded commit fixture deadline expired')
                    return party_module.Console.call(self,line)
                return super().call(line)
        class MeasuredGame(party_module.PublicGame):
            def save_log(self):
                if not getattr(self, 'hold_command_artifact', False):
                    write_command_artifact(self.output / f'{self.instance}-commands.json', self.events)
            def dismiss_registration(self):
                if menu_login_button_only(self):
                    owner.result.setdefault('native_navigation_guards',[]).append(dict(
                        instance=self.instance,reason='Visible Create menu with only its bottom-left Login button'))
                    owner.write();return
                return super().dismiss_registration()
            def wait(self,description,predicate,timeout=None):
                result=super().wait(description,predicate,timeout)
                if description=='native editor shell ready' and self.instance==args.instances[0]:
                    owner.capture_original_baseline(self)
                return result
            def choose_card(self,name):
                super().choose_card(name)
                if self.instance==args.instances[0] and name==args.adventure:
                    self.adventure_selected_by_native_browser=True
            def call(self,command,required=True):
                return super().call(route_owned_join(command,self.instance,args,proxies),required)
        self.all_games=[MeasuredGame(i,self.output,args.timeout,self.deadline) for i in args.instances]
        for game in self.all_games:game.console=MeasuredConsole(game.instance,self.deadline)
        self.games=self.all_games;self.active_games=list(self.games);self.host,self.guest=self.games[:2]
        self.created={};self.completed_cycles=0;self.compact_records=True
        self.result=dict(schema=1,passed=False,complete_acceptance=False,stage='commit-performance',
                         candidate_sha256=args.expected_dll_sha256,instances=args.instances,requested_adventure=args.adventure,
                         adventure_selected_by_native_browser=False,
                         setup=args.setup,checks=[],gates={},profiles=[],
                         helper_closes_party_transport_on_exit=bool(proxies),
                         processes={str(i):self.records[i]['pid'] for i in args.instances},
                         method='Actual input release, exact operation-keyed QPC stages and primed Present frame windows. No native inspection inside measured windows.',
                         bulk_asset_contention_pass=False)
        for game in self.games:self.verify(game)

    def capture_original_baseline(self,game):
        inspection=game.call('coop.editor inspect')
        self.result['original_native_baseline']=dict(instance=game.instance,
            before_hosting=True,before_title_input=True,inspection=inspection)
        self.write()
        if not inspection.startswith('placements=') or not re.search(r'\bnative_sha256=[0-9a-f]{64}\b',inspection):
            raise FixtureError('Original native baseline failed before hosting: '+inspection)

    def record(self,label):
        self.ownership.verify_windows()
        self.result['checks'].append(dict(label=label,states=[status(g) for g in self.all_games]))
        self.write()

    def body(self,game,object_id):
        point=self.objects(game).get(object_id,{}).get('screen')
        if point is None:raise FixtureError('The selected fixture placement is not visible: '+object_id)
        point=(round(point[0]),round(point[1]-self.args.body_offset_y))
        config=values(game.call('coop.frame_metrics config'))
        if not self.args.world_left<=point[0]<config['screen_width']-80 or not 80<=point[1]<config['screen_height']-90:
            raise FixtureError('The selected native body is outside the unobstructed world viewport')
        return point


def settled_revision(party,revision):
    rows=[status(game) for game in party.all_games]
    if any(row.get('frozen') for row in rows):raise FixtureError('A commit froze an editor replica')
    if any(row.get('revision',0)>revision for row in rows):raise FixtureError('One release produced more than one transaction')
    return all(row.get('phase')==4 and row.get('ready') and not row.get('gesture') and
               row.get('revision')==revision and row.get('durable')==revision for row in rows)


def await_measurement_gate(party):
    gate=party.args.measurement_gate
    if gate is None:return
    ready=gate.with_name(gate.name+'.ready')
    ready.write_text(json.dumps(dict(candidate_sha256=party.args.expected_dll_sha256,
        instances=party.args.instances,stage='warmup-complete'),indent=2)+'\n',encoding='utf-8')
    party.result['measurement_gate']=dict(ready=str(ready),waiting=True)
    party.write()
    while time.monotonic()<party.deadline:
        if gate.is_file():
            if gate.read_text(encoding='utf-8').strip()!=party.args.expected_dll_sha256:
                raise FixtureError('Measurement gate does not identify this exact candidate')
            party.result['measurement_gate']['waiting']=False
            party.write();return
        time.sleep(.25)
    raise FixtureError('The bounded commit fixture expired before measurement was released')


def handoff_native_selection(party,author,object_id=None):
    """Release another author's visible selection before an unmeasured handoff."""
    revision=status(party.host)['revision'];deselected=[]
    for observer in party.all_games:
        if observer is author:continue
        before=status(observer)
        if before.get('frozen') or before.get('gesture'):
            raise FixtureError('An observer has an unsettled native operation at the author handoff')
        if before.get('selection') is None:continue
        observer.call('key esc');observer.call('wait.frames 8 15000')
        def released():
            current=status(observer)
            return (current.get('selection') is None and not current.get('gesture') and
                    not any(line.startswith('lease ') for line in observer.call('coop.editor leases').splitlines()))
        observer.wait('ordinary native deselection and lease release',released)
        if status(party.host)['revision']!=revision:
            raise FixtureError('Native author handoff changed the committed draft')
        deselected.append(observer.instance)
    locate=getattr(getattr(party,'args',None),'native_locate_card',None)
    if locate:
        if not object_id:raise FixtureError('Native Locate requires one exact fixture object')
        author.call(f'move {locate[0]} {locate[1]}');author.call(f'click {locate[0]} {locate[1]} right');author.call('wait.frames 120 15000')
        author.wait('exact placement selected by ordinary native Locate',lambda:status(author).get('selection')==object_id)
        if status(party.host)['revision']!=revision:raise FixtureError('Native Locate changed the committed draft')
    party.result.setdefault('native_author_handoffs',[]).append(dict(author=author.instance,deselected=deselected,revision=revision,
        **(dict(native_locate_card=locate,object=object_id) if locate else {})))
    party.write()


def begin_checked_drag(party,author,object_id,source,revision):
    author.call(f'move {source[0]} {source[1]}');author.call('wait.frames 5 15000')
    author.call(f'button {source[0]} {source[1]} down');author.call('wait.frames 8 15000');time.sleep(.12)
    observed=None
    try:
        def owned():
            nonlocal observed
            observed=status(author)
            if observed.get('frozen'):raise FixtureError('Native target acquisition froze the editor')
            if observed.get('selection') not in (None,object_id):
                raise FixtureError('Native mouse-down selected another placement: '+str(observed.get('selection')))
            return observed.get('selection')==object_id and retry_grant(author.call('coop.editor leases'),object_id)
        author.wait('exact native target and ownership before movement',owned,timeout=3)
    except Exception:
        # No placement movement was issued. Cancel the native press instead of
        # releasing a possible capture against an unintended object.
        author.call('key esc');author.call(f'button {source[0]} {source[1]} up');author.call('wait.frames 8 15000')
        proof=party.converge(revision)
        party.result.setdefault('native_target_rejections',[]).append(dict(instance=author.instance,
            expected=object_id,observed=observed,revision=revision,after_cancel_convergence=proof))
        party.write();raise


def assert_target_moved(before,after,object_id):
    if object_id not in after or before.get(object_id,{}).get('position')==after[object_id]['position']:
        raise FixtureError('The gesture did not move the actual authored placement')
    for object_key,placement in before.items():
        if object_key!=object_id and object_key in after and any(placement.get(k)!=after[object_key].get(k) for k in ('position','rotation','scale','relative_scale')):
            raise FixtureError('A native move changed another placement')


def checked_warm_drag(party,author,object_id):
    source=party.body(author,object_id);target=(source[0]+party.args.drag_pixels,source[1])
    begin_checked_drag(party,author,object_id,source,status(party.host)['revision'])
    try:
        for fraction in (.35,.7,1.):
            author.call(f'move {round(source[0]+(target[0]-source[0])*fraction)} {source[1]}')
            author.call('wait.frames 6 15000');time.sleep(.06)
    finally:author.call(f'button {target[0]} {target[1]} up')


def measure(party,author,object_id,index):
    with hold_command_artifacts(party.all_games):
        return measure_without_artifact_io(party,author,object_id,index)


def measure_without_artifact_io(party,author,object_id,index):
    before_revision=status(party.host)['revision'];before=party.objects(author)
    source=party.body(author,object_id);target=(source[0]+(party.args.drag_pixels if index%2==0 else -party.args.drag_pixels),source[1])
    held=False;recording=[]
    try:
        begin_checked_drag(party,author,object_id,source,before_revision);held=True
        for fraction in (.35,.7,1.):
            author.call(f'move {round(source[0]+(target[0]-source[0])*fraction)} {source[1]}')
            author.call('wait.frames 6 15000');time.sleep(.06)
        work_before={str(game.instance):native_work_sample(game.call('coop.editor status')) for game in party.all_games}
        source_before={str(game.instance):game.call('log 2000') for game in party.all_games}
        for game in party.all_games:
            game.call('coop.editor stages reset');game.call('coop.frame_metrics start');recording.append(game)
        # Prime at least two real Present intervals before release. The first
        # post-release Present alone cannot measure the preceding native stall.
        for game in party.all_games:game.call('wait.frames 3 15000')
        author.call(f'button {target[0]} {target[1]} up');held=False
        party.host.wait('one durably accepted native release',lambda:settled_revision(party,before_revision+1))
        frames={str(game.instance):frame_sample(game.call('coop.frame_metrics stop')) for game in party.all_games}
        recording.clear()
        work_after={str(game.instance):native_work_sample(game.call('coop.editor status')) for game in party.all_games}
        native_work={str(game.instance):native_work_delta(work_before[str(game.instance)],work_after[str(game.instance)]) for game in party.all_games}
        sources={str(game.instance):source_fence(source_before[str(game.instance)],game.call('log 2000')) for game in party.all_games}
        captures={str(game.instance):read_stages(game,party.records[game.instance]['pid']) for game in party.all_games}
        correlation=correlate(captures,author.instance,before_revision+1)
        party.converge(before_revision+1)
        after=party.objects(author)
        assert_target_moved(before,after,object_id)
        result=dict(author_instance=author.instance,object=object_id,correlation=correlation,frames=frames,captures=captures,
                    frame_source_fences=sources,native_work=native_work)
        return result
    finally:
        if held:author.call(f'button {target[0]} {target[1]} up',required=False)
        for game in recording:
            game.call('coop.frame_metrics stop',required=False);game.call('coop.editor stages stop',required=False)


def run(args):
    party=None;proxies={};completed=False
    try:
        with ExitStack() as stack:
            if uses_owned_proxy(args):
                proxies={i:stack.enter_context(EditorProxy(args.port,seed=0xBEEF1234+n*101)) for n,i in enumerate(args.instances[1:])}
            party=CommitParty(args,proxies)
            if args.setup=='bootstrap':
                if any(values(game.call('coop.frame_metrics config')).get(k)!=v for game in party.all_games for k,v in (('screen_width',1024),('screen_height',768))):
                    raise FixtureError('Ordinary bootstrap coordinates require1024x768; attach supports other configured viewports')
                native=status(party.host)
                if native.get('editing') and native.get('ready'):party.capture_original_baseline(party.host)
                if len(args.instances)==4:party.bootstrap()
                else:EditorPair.bootstrap(party)
                party.result['adventure_selected_by_native_browser']=bool(getattr(party.host,'adventure_selected_by_native_browser',False))
                party.join()
            else:party.converge()
            configs={str(g.instance):configuration(g) for g in party.all_games}
            inspected={str(g.instance):g.call('coop.editor inspect') for g in party.all_games}
            party.result['initial_native_inspection']=inspected
            if args.expected_native_sha256:
                actual=re.search(r'\bnative_sha256=([0-9a-f]{64})\b',inspected[str(party.host.instance)])
                if not actual or actual[1]!=args.expected_native_sha256:raise FixtureError('Attached native fixture differs from the requested initial snapshot')
                party.result['initial_native_sha256_verified']=True
            for game in party.all_games:
                editor_uncovered(game);game.call('coop.uistate 0')
                if values(game.call('coop.editor inspect')).get('placements',0)<args.minimum_placements and not args.diagnostic:
                    raise FixtureError('The fixture is smaller than the requested large-editor gate')
            candidates=set(party.objects(party.host))
            for game in party.all_games:candidates&={key for key,row in party.objects(game).items() if row.get('screen') is not None}
            chosen=args.object or (sorted(candidates)[0] if candidates else None)
            if not chosen or any(chosen not in party.objects(g) for g in party.all_games):raise FixtureError('Choose one stable visible placement with --object')
            party.result['native_input']=dict(body_offset_y=args.body_offset_y,drag_pixels=args.drag_pixels,native_locate_card=args.native_locate_card)
            # Warm through the existing checked native gesture path; warm-up and
            # ownership retries are excluded from the30 measured releases.
            for author in party.all_games:
                handoff_native_selection(party,author,chosen)
                before=party.objects(author)
                party.gesture(author,'warm-owned-native-move',lambda g=author:checked_warm_drag(party,g,chosen))
                assert_target_moved(before,party.objects(author),chosen)
            party.result['configuration']=configs;party.result['fixture_object']=chosen
            await_measurement_gate(party)
            for name in args.profiles:
                profile,budget=PROFILES[name]
                for proxy in proxies.values():proxy.configure(profile)
                party.converge();rows=[];before={str(i):proxy.snapshot() for i,proxy in proxies.items()}
                for author in party.all_games:
                    handoff_native_selection(party,author,chosen)
                    for index in range(args.samples):
                        row=measure(party,author,chosen,index);rows.append(row)
                        party.result['active_profile']=dict(name=name,gestures=rows);party.write()
                summary=summarize(rows,args.instances,args.samples,budget,args.p95_frame_ms,args.max_frame_ms)
                summary.update(name=name,profile=vars(profile if proxies else Profile()),impairment_applied=bool(proxies),gestures=rows,
                               traffic={str(i):EditorProxy.difference(before[str(i)],proxy.snapshot()) for i,proxy in proxies.items()},
                               asset_contention_verified=False)
                party.result['profiles'].append(summary);party.result.pop('active_profile',None);party.write()
                if not summary['passed']:raise FixtureError('Native confirmation/frame-stall budget failed: '+name)
            for game in party.all_games:
                party.verify(game);editor_uncovered(game)
                if configuration(game)!=configs[str(game.instance)] or 'total faults: 0' not in game.call('exc'):
                    raise FixtureError('Native rendering configuration changed or a fault occurred')
            party.result.update(passed=True,commit_stall_gate=not args.diagnostic,
                                large_editor_gate=not args.diagnostic and args.minimum_placements>=400,
                                all_authors_measured=True,rate_scope='Symmetric per peer/direction;2Mbit/s has no concurrent missing-asset transfer and cannot pass the bulk gate.')
            party.write()
            if proxies:
                party.host.call('coop.editor end');party.host.wait('owned proxy party ends',lambda:all(status(g).get('phase')==0 for g in party.all_games))
                for game in party.all_games:game.call('coop.leave')
            completed=True
        return 0
    except Exception as error:
        if party:
            party.result.update(passed=False,error=str(error),
                adventure_selected_by_native_browser=bool(getattr(party.host,'adventure_selected_by_native_browser',False)))
            if getattr(error, 'command_artifact_errors', None):
                party.result['command_artifact_errors'] = error.command_artifact_errors
            party.write()
        else:
            args.output.mkdir(parents=True,exist_ok=True);(args.output/'result.json').write_text(json.dumps(dict(passed=False,error=str(error)),indent=2)+'\n',encoding='utf-8')
        print(str(error));return 1
    finally:
        if party:
            if not completed:party.result['owner_must_settle_failed_native_workflow']=True;party.write()
            party.close()


def parser():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--metadata',type=Path,required=True)
    p.add_argument('--instances',nargs='+',type=int,required=True);p.add_argument('--expected-dll-sha256',required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--setup',choices=('attach','bootstrap'),default='attach',
                   help='Bootstrap opens an existing adventure after native captain onboarding; attach uses a ready shared editor')
    p.add_argument('--adventure',default='Motel of Melora (\'22 Edition)');p.add_argument('--object')
    p.add_argument('--expected-native-sha256',help='Optional exact initial native fixture hash, especially for attach')
    p.add_argument('--profiles',nargs='+',choices=tuple(PROFILES),default=['lan']);p.add_argument('--samples',type=int,default=30)
    p.add_argument('--minimum-placements',type=int,default=400);p.add_argument('--diagnostic',action='store_true')
    p.add_argument('--p95-frame-ms',type=float,default=100);p.add_argument('--max-frame-ms',type=float,default=250)
    p.add_argument('--body-offset-y',type=int,default=28);p.add_argument('--drag-pixels',type=int,default=8)
    p.add_argument('--native-locate-card',nargs=2,type=int,metavar=('X','Y'),
                   help='Optional visible cast card for normal right-click Locate before each author group')
    p.add_argument('--measurement-gate',type=Path,
                   help='After warmup write PATH.ready and wait for PATH containing the exact candidate hash')
    p.add_argument('--world-left',type=int,default=356);p.add_argument('--seconds',type=int,default=1800)
    p.add_argument('--timeout',type=int,default=120);p.add_argument('--port',type=int,default=59093)
    p.add_argument('--title',default='Private Native Commit Performance')
    return p


def main():
    p=parser();args=p.parse_args()
    if not args.execute or os.name!='nt':p.error('Requires --execute and already-owned Windows lab processes')
    if len(args.instances) not in (2,4) or len(set(args.instances))!=len(args.instances) or any(not 2<=i<=100 for i in args.instances):p.error('Two/four distinct private instances2..100 required')
    if not re.fullmatch('[0-9a-f]{64}',args.expected_dll_sha256) or not 30<=args.samples<=100 or not 60<=args.seconds<=3500 or not 1<=args.timeout<=300 or not 1<=args.port<=65535:p.error('Exact candidate hash and bounded test settings required')
    if not 1<=args.minimum_placements<=4096 or not 0<args.p95_frame_ms<=args.max_frame_ms<=1000 or not -200<=args.body_offset_y<=200 or not 1<=args.drag_pixels<=40 or not 0<=args.world_left<=2000:p.error('Invalid placement, body or frame bounds')
    if args.setup=='attach' and args.profiles!=['lan']:p.error('WAN profiles require --setup bootstrap and owned impairment proxies')
    if len(set(args.profiles))!=len(args.profiles):p.error('Each network profile may be measured only once per receipt')
    if args.expected_native_sha256 and not re.fullmatch('[0-9a-f]{64}',args.expected_native_sha256):p.error('Expected initial native hash must be exact SHA256')
    if any(not re.fullmatch(r'[^\r\n]{1,256}',value) for value in (args.adventure,args.title)):p.error('Adventure/title must be bounded one-line labels')
    if args.object and not re.fullmatch(r'[1-9][0-9]{0,19}:[1-9][0-9]{0,9}',args.object):p.error('Object must be one stable native logical ID:generation')
    if args.native_locate_card and (not args.object or not 0<=args.native_locate_card[0]<args.world_left or not 70<=args.native_locate_card[1]<1000):
        p.error('Native Locate requires --object and a bounded visible cast-card point to the left of the world viewport')
    args.output=args.output.resolve()
    if (ROOT/'out').resolve() not in args.output.parents:p.error('Output must remain under this worktree out directory')
    if args.measurement_gate is not None:
        args.measurement_gate=args.measurement_gate.resolve()
        if ((ROOT/'out').resolve() not in args.measurement_gate.parents or not args.measurement_gate.parent.is_dir() or
                args.measurement_gate.exists() or args.measurement_gate.with_name(args.measurement_gate.name+'.ready').exists()):
            p.error('Measurement gate requires a new path below an existing worktree out directory')
    args.host,args.guest=args.instances[:2];args.stage='commit-performance';args.fixture='existing';args.roundtrip_cycles=0
    args.resume_result=None;args.onboard_host=False;args.history_input='hotkeys'
    return run(args)


if __name__=='__main__':raise SystemExit(main())
