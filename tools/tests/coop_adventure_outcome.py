#!/usr/bin/env python3
"""Drive a real defended-generator failure through native result controls.

Only a recorded, hidden and muted Windows lab pair is accepted. The fixture
starts stock Defend the Crystal Mine with its authored captain and ordinary
Ready/Continue controls. Native actor damage destroys the authored generator;
it never calls the controlled ``finish`` command or writes a play state.
"""
import argparse
import json
from pathlib import Path
import struct
import time

from adventure_control_fixture import local_avatar, marker_teleport
from adventure_interaction_fixture import _verify_game
from adventure_memory import AdventureMemory
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_actor_motion import start_mine
from coop_adventure_combat_movement import actors
from coop_adventure_enemy_targeting import loaded_identity
from coop_adventure_pause import open_menu
from coop_adventure_preparation import menu


def check(value, message):
    if not value:
        raise FixtureError(message)


def analyze(samples, failure_state=6):
    """A terminal state is monotonic through the rest of this native run."""
    seen = [False, False]
    regressions = []
    acts = [set(), set()]
    for sample in samples:
        for index, peer in enumerate(sample['peers']):
            state, act = peer['goals']['state'], peer['goals']['act']
            if seen[index] and state != failure_state:
                regressions.append(dict(wall=sample['wall'], peer=index, state=state, act=act))
            if state == failure_state:
                seen[index] = True
            if seen[index]:
                acts[index].add(act)
    return dict(native_terminal_seen=seen, terminal_regressions=regressions,
                terminal_acts=[sorted(values) for values in acts],
                native_terminal_monotonic=all(seen) and not regressions and
                all(len(values) == 1 for values in acts))


def read_peer(game, memory):
    avatar = local_avatar(game)
    mode = memory.pointer(memory.singleton)
    play = memory.pointer(mode+0x78)
    return dict(instance=game.instance, goals=memory.goals(),
                coordinator=game.adventure(), avatar=avatar, play=play,
                position=list(struct.unpack('<3f', memory.read(avatar+0xC4, 12))),
                health=struct.unpack('<f', memory.read(avatar+0x5E0, 4))[0],
                dead=bool(memory.read(avatar+0xB5E, 1)[0]))


def lifecycle_peer(game, memory):
    """Do not dereference a retired scenario after a native Done transition."""
    mode = game.call('game.mode')
    response = game.call('call App::IStateManager::Get cdecl')
    manager = int(response.split('eax=0x', 1)[1].split()[0], 16)
    result = dict(instance=game.instance, game_mode=mode,
                  application_state=memory.pointer(manager+0x88),
                  coordinator=game.adventure())
    if 'Scenario' in mode:
        try:
            result['native'] = read_peer(game, memory)
        except (FixtureError, OSError, ValueError) as error:
            # A coordinated Done can retire Scenario between these independent
            # read-only calls. Never retain or dereference its old play pointer.
            result['native_retired_during_sample'] = str(error)
    return result


def result_controls(games, memories, result, timeout=30, host_only=False):
    """Use the real failure cinematic checkmark, then require native results."""
    start = time.monotonic()
    next_click = [0., 0.]
    deadline = start+timeout
    while time.monotonic() < deadline:
        row = dict(wall=time.monotonic()-result['_sample_start'],
                   phase='failure-cinematic', peers=[])
        all_panels = True
        for index, (game, memory) in enumerate(zip(games, memories)):
            row['peers'].append(read_peer(game, memory))
            if index and host_only:
                continue
            if game.caption('Replay') and game.caption('Done'):
                continue
            all_panels = False
            button = game.control(0x0445C99D)
            if button and button[4] and time.monotonic() >= next_click[index]:
                x1, y1, x2, y2, _ = button
                game.click((x1+x2)//2, (y1+y2)//2)
                next_click[index] = time.monotonic()+1
                result['controls'].append(dict(instance=game.instance,
                    wall=time.monotonic()-result['_sample_start'], action='native-result-checkmark'))
        result['samples'].append(row)
        if all_panels:
            return
        time.sleep(.1)
    raise FixtureError('native failure cinematic did not reach both result panels')


def run(args, games, records):
    result = dict(result='INCONCLUSIVE', scenario='Defend the Crystal Mine',
                  failure_act=args.act, down_guest=args.down_guest, guest_paused=args.guest_paused,
                  profiles=records, controls=[], samples=[],
                  limits=['Generator damage uses the existing reviewed native actor-hit fixture.',
                          'No play-state writes, controlled finish, NPC orders or objective writes.',
                          'An explicit host act change stages the selected authored chapter.'])
    memories = []
    try:
        for game in games:
            _verify_game(game, records[game.instance])
        result['loaded_shadows'] = loaded_identity(games, args.metadata, args.expected_sha256)
        if not args.resume:
            start_mine(games, records, args.port)
        memories = [AdventureMemory(game, records[game.instance]) for game in games]
        if args.act:
            games[0].call(f'coop.adventure.world act {args.act}')
            for game, memory in zip(games, memories):
                game.wait('requested authored Mine chapter', lambda: memory.goals()['act'] == args.act)
        for memory in memories:
            check(memory.goals()['state'] == 3, 'fresh native Playing is required')
        goal = next((goal for goal in memories[0].goals()['goals']
                     if goal['type'] == 11 and goal['primary'] == 1069), None)
        check(goal and len(goal['targets']) == 1, 'the authored generator defend goal is unavailable')
        marker = goal['targets'][0]
        rows = actors(games[0].call(f'coop.adventure.world actorsat {marker} 48'))
        generator = [actor for actor in rows if actor['cls'] == 1069]
        check(len(generator) == 1, 'expected one native generator incarnation')
        result['generator'] = generator[0]
        result['before'] = [read_peer(game, memory) for game, memory in zip(games, memories)]
        if args.down_guest:
            # Owner-local native placement keeps the host outside the revive
            # radius. It does not modify either captain's HP or lifecycle.
            marker_teleport(games[1], records[games[1].instance], marker)
            # Environmental hits originate on the victim's owner. A host hit
            # against a guest puppet is deliberately rejected by native policy.
            games[1].call('coop.adventure.world hit 1 env 100000 2')
            games[1].wait('authoritative guest Downed', lambda:
                'DOWNED' in games[1].call('coop.uistate') or
                struct.unpack('<f', memories[1].read(local_avatar(games[1])+0x5E0, 4))[0] <= 1,
                timeout=5)
            result['downed'] = [read_peer(game, memory) for game, memory in zip(games, memories)]
        if args.guest_paused:
            open_menu(games[1])
            result['paused'] = [read_peer(game, memory) for game, memory in zip(games, memories)]
        result['_sample_start'] = time.monotonic()
        result['trigger'] = games[0].call(f'coop.adventure.world actorhit {marker} env 100000 2')
        start = time.monotonic()
        while time.monotonic()-start < args.seconds:
            result['samples'].append(dict(wall=time.monotonic()-result['_sample_start'],
                phase='post-generator-failure',
                peers=[read_peer(game, memory) for game, memory in zip(games, memories)]))
            time.sleep(.1)
        if args.guest_paused and games[1].caption('Resume Game'):
            games[1].call('key esc')
            result['controls'].append(dict(instance=games[1].instance,
                wall=time.monotonic()-result['_sample_start'], action='native-pause-resume-after-failure'))
        result_controls(games, memories, result, host_only=args.host_done_during_guest_cinematic)
        if args.host_done_during_guest_cinematic:
            button = games[1].control(0x0445C99D)
            check(button and button[4] and not games[1].caption('Replay'),
                  'guest must still be waiting at its native failure cinematic checkmark')
            result['guest_cinematic_before_host_done'] = dict(control=button,
                native=read_peer(games[1], memories[1]))
        # Ordinary input must not put a terminal captain back into the world.
        if not args.host_done_during_guest_cinematic:
            games[1].call('key W down')
            try:
                games[1].call('key 1')
                games[1].call('key space')
                start = time.monotonic()
                while time.monotonic()-start < args.post_seconds:
                    result['samples'].append(dict(wall=time.monotonic()-result['_sample_start'],
                        phase='guest-W-at-results',
                        peers=[read_peer(game, memory) for game, memory in zip(games, memories)]))
                    time.sleep(.1)
            finally:
                games[1].call('key W up', required=False)
        result['summary'] = analyze(result['samples'])
        for game in games:
            game.record('native-failure-results')
            check('total faults: 0' in game.call('exc'), 'native fault during generator failure')
        if args.expect == 'shared':
            check(result['summary']['native_terminal_monotonic'],
                  'a participant continued playing after the authoritative native failure')
        else:
            check(not result['summary']['native_terminal_monotonic'],
                  'the requested baseline escape did not reproduce')
        result['result'] = 'PASS'
        if args.done:
            if args.guest_replay_before_host_done:
                games[1].press('Replay')
                result['controls'].append(dict(instance=games[1].instance,
                    wall=time.monotonic()-result['_sample_start'], action='guest-Replay-before-host-Done'))
            # Capture the host leaving first while the guest has not accepted
            # its results. This previously left only the guest in Scenario.
            games[0].press('Done')
            time.sleep(.5)
            if args.require_party_return:
                games[0].wait('host Done returns the whole party to galaxy',
                    lambda: all(game.menu() for game in games), timeout=8)
            result['after_host_done'] = [lifecycle_peer(game, memory)
                                        for game, memory in zip(games, memories)]
            if games[1].caption('Done'):
                games[1].press('Done')
            for game in games:
                menu(game, records[game.instance])
            result['menus_recovered'] = True
    except (FixtureError, OSError, ValueError) as error:
        result['result'], result['error'] = 'FAIL', str(error)
        if result['samples']:
            result['summary'] = analyze(result['samples'])
        for game in games:
            try:
                game.record('outcome-failure')
            except (FixtureError, OSError, ValueError):
                pass
    finally:
        result.pop('_sample_start', None)
        for memory in memories:
            memory.close()
        for game in games:
            game.save_log()
        (args.out/'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--host', type=int, default=83)
    parser.add_argument('--guest', type=int, default=84)
    parser.add_argument('--port', type=int, default=47853)
    parser.add_argument('--act', type=int, choices=range(5), default=0)
    parser.add_argument('--seconds', type=float, default=3)
    parser.add_argument('--post-seconds', type=float, default=8)
    parser.add_argument('--down-guest', action='store_true')
    parser.add_argument('--guest-paused', action='store_true')
    parser.add_argument('--resume', action='store_true')
    parser.add_argument('--done', action='store_true')
    parser.add_argument('--require-party-return', action='store_true')
    parser.add_argument('--guest-replay-before-host-done', action='store_true')
    parser.add_argument('--host-done-during-guest-cinematic', action='store_true')
    parser.add_argument('--expected-sha256')
    parser.add_argument('--expect', choices=('shared', 'escaped'), default='shared')
    args = parser.parse_args()
    if (args.require_party_return or args.guest_replay_before_host_done or
            args.host_done_during_guest_cinematic) and not args.done:
        parser.error('party return options require --done')
    if args.guest_replay_before_host_done and args.host_done_during_guest_cinematic:
        parser.error('a guest still in its cinematic cannot have already chosen Replay')
    check(args.host != args.guest, 'distinct owned lab instances are required')
    records = verify_lab(args.metadata, (args.host, args.guest))
    args.out.mkdir(parents=True, exist_ok=True)
    games = [Game(instance, args.out, timeout=180) for instance in (args.host, args.guest)]
    result = run(args, games, records)
    print(json.dumps(dict(result=result['result'], summary=result.get('summary'), error=result.get('error'))))
    return result['result'] != 'PASS'


if __name__ == '__main__':
    raise SystemExit(main())
