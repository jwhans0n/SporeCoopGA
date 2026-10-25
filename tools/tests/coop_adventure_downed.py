#!/usr/bin/env python3
"""Check real downed movement and camera input in an existing hidden lab pair.

Start a manual-revive adventure and separate captains by at least 4 metres first.
Raw native positions are read independently of normalized player publications.
Screenshots are retained for visual flicker and revive HUD review.
"""
import argparse
import json
from pathlib import Path
import re
import struct
import time

from adventure_memory import AdventureMemory
from coop_adventure import Game, FixtureError, verify_lab
from coop_adventure_combat import check, expect_health, hit, hp
from coop_adventure_lobby import TOWN, lobby, click_named, playing
from coop_adventure_preparation import menu
from adventure_control_fixture import teleport,marker_teleport


def avatar(memory):
    text = memory.game.call('coop.avatar')
    match = re.search(r'local=(?:0x)?([0-9a-fA-F]+)', text)
    if not match:
        raise FixtureError('native captain pointer unavailable')
    address = int(match.group(1), 16)
    return address, memory.pointer(address + 0x24)


def pose(memory, identity):
    address, incarnation = identity
    check(memory.pointer(address + 0x24) == incarnation, 'captain object changed during downed control test')
    return dict(position=struct.unpack('<fff', memory.read(address + 0xC4, 12)),
                velocity=struct.unpack('<fff', memory.read(address + 0x288, 12)))


def distance(a, b):
    return sum((x - y) ** 2 for x, y in zip(a, b)) ** .5


def counters(game):
    text = game.call('coop.adventure status')
    match = re.search(r'downed controls: entries=(\d+) exits=(\d+) brain=(\d+) wasd=(\d+) '
                      r'cursor=(\d+) orders=(\d+) corrections=(\d+)', text)
    check(match is not None, 'installed test build lacks native downed control diagnostics')
    return dict(zip(('entries', 'exits', 'brain', 'wasd', 'cursor', 'orders', 'corrections'), map(int, match.groups())))


def camera(game):
    text = game.call('coop.cam')
    rows = re.findall(r'^  m\[\d\] =\s+(.+)$', text, re.M)
    check(len(rows) == 4, 'native camera diagnostic unavailable')
    return tuple(float(value) for row in rows for value in row.split())


def frame_position(game):
    # coop.cam reads game::position on the game thread, between frames. It does
    # not normalize a PlayerState, unlike coop.avatar. An asynchronous process
    # read can legitimately observe physics integration before its Update fence.
    text=game.call('coop.cam')
    match=re.search(r'avatar=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',text)
    check(match is not None,'raw frame-boundary native captain position unavailable')
    return tuple(map(float,match.groups()))


def ring(game, player):
    output=game.call('coop.adventure.hud')
    match=re.search(r"^revive_ring \d+ progress=([\d.]+) text='P"+str(player+1)+r" (.*?)'",output,re.M)
    return dict(progress=float(match[1]),text=match[2],native_hud=output) if match else None


def revive(games,readers,victim_index,records):
    victim,rescuer=games[victim_index],games[1-victim_index]
    memory=readers[victim_index]
    player=victim.player_id()
    baseline=victim.wait('native two metre revive ring',lambda:ring(victim,player),timeout=5)
    check(baseline['progress']==0,'revival progressed with no captain nearby')
    def beside():
        position=list(pose(memory,avatar(memory))['position'])
        position[0]+=-1 if player==0 else 1
        teleport(rescuer,records[rescuer.instance],position)
    beside()
    rescuer.wait('same downed ring on nearby rescuer',lambda:ring(rescuer,player),timeout=5)
    progressing=victim.wait('native revive progress and countdown',lambda:
        (state if (state:=ring(victim,player)) and state['progress']>=.15 else None),timeout=3)
    victim.record('revive-progress')
    # The native host pause shares the same simulation-time pause predicate as
    # scene holds. Observe the unpaused guest HUD while the authority is paused.
    host,guest=games
    host.call('key esc')
    host.wait('host native pause menu',lambda:host.caption('Resume Game'),timeout=5)
    try:
        time.sleep(.15)
        frozen_before=ring(guest,player)
        clock_before=readers[0].goals()['elapsed']
        check(frozen_before is not None,'guest revive ring disappeared during host pause')
        time.sleep(1.1)
        frozen_after=ring(guest,player)
        clock_after=readers[0].goals()['elapsed']
        check(frozen_after is not None and abs(frozen_after['progress']-frozen_before['progress'])<.02,
              'revive progress advanced during native shared simulation pause')
        check(clock_after==clock_before,'native host clock advanced in its pause menu')
        guest.record('revive-host-pause')
    finally:
        host.press('Resume Game')
    marker_teleport(rescuer,records[rescuer.instance],169,10 if rescuer.player_id()==0 else -10)
    moved_out=victim.wait('leaving revive radius resets progress',lambda:
        (state if (state:=ring(victim,player)) and state['progress']==0 else None),timeout=3)
    victim.record('revive-moved-out')
    beside()
    victim.wait('second revive hold starts',lambda:
        (state if (state:=ring(victim,player)) and state['progress']>=.15 else None),timeout=3)
    rescuer_health=hp(rescuer)[0]
    hit(rescuer,rescuer.player_id(),'env',5)
    expect_health(rescuer,rescuer_health-5)
    damaged=victim.wait('damage to rescuer resets revive progress',lambda:
        (state if (state:=ring(victim,player)) and state['progress']<=.1 else None),timeout=2)
    maximum=hp(victim)[1]
    expect_health(victim,maximum*.5,timeout=5)
    victim.wait('revive ring removed after completion',lambda:ring(victim,player) is None,timeout=3)
    identity=avatar(memory)
    before=pose(memory,identity)
    victim.call('key W down')
    try:
        time.sleep(.65)
    finally:
        victim.call('key W up',required=False)
    after=pose(memory,identity)
    check(distance(before['position'],after['position'])>.1,'revived native captain cannot move')
    camera_before=camera(victim)
    victim.call('drag 640 330 740 350 right')
    victim.call('wait.frames 12 15000')
    camera_after=camera(victim)
    check(camera_before!=camera_after,'revived camera did not remain usable')
    for game in games:
        check('total faults: 0' in game.call('exc'),'native revive test logged an exception')
        game.record('revived')
    return dict(baseline=baseline,progressing=progressing,moved_out=moved_out,damage_reset=damaged,
                revived_health=hp(victim),movement_before=before,movement_after=after,
                camera_before=camera_before,camera_after=camera_after,
                native_pause=dict(before=frozen_before,after=frozen_after,clock_before=clock_before,clock_after=clock_after),
                group_hold='Native host pause tested with the same shared clock pause predicate; not combined with a conversation barrier.')


def run(games, readers, victim_index):
    victim, rescuer = games[victim_index], games[1 - victim_index]
    memory, other = readers[victim_index], readers[1 - victim_index]
    identity, other_identity = avatar(memory), avatar(other)
    check(all('Adventure in progress' in game.adventure()['text'] and 'death=0' in game.adventure()['text'] for game in games),
          'a committed manual-revive attempt is required')
    initial = pose(memory, identity)
    check(distance(initial['position'], pose(other, other_identity)['position']) >= 4,
          'separate captains by at least 4 metres before the test to prevent proximity revival')
    victim.record('alive-before-downed')
    victim.call('key W down')
    try:
        time.sleep(.5)
    finally:
        victim.call('key W up',required=False)
    live_input=pose(memory,identity)
    check(distance(initial['position'],live_input['position'])>.1,
          'ordinary native movement did not exercise input before the knockout')
    maximum = hp(victim)[1]
    hit(victim, victim.player_id(), 'env', 100000, 10)
    expect_health(victim, maximum)
    before = counters(victim)
    hit(victim, victim.player_id(), 'env', 100000)
    expect_health(victim, 1)
    victim.wait('native downed control entry', lambda: counters(victim)['entries'] > before['entries'], timeout=8)
    anchor = frame_position(victim)
    victim.record('downed-start')
    samples = []
    victim.call('key W down')
    victim.call('key space down')
    try:
        started = time.monotonic()
        while time.monotonic() - started < 3:
            value = pose(memory, identity)
            frame=frame_position(victim)
            check(distance(frame, anchor) <= .1, 'raw native downed captain moved under held input between frames')
            samples.append(dict(wall=time.monotonic() - started,frame_position=frame,**value))
            time.sleep(.12)
    finally:
        victim.call('key W up', required=False)
        victim.call('key space up', required=False)
    victim.click(760, 400)
    victim.call('key 1')
    victim.call('wait.frames 15 15000')
    check(distance(frame_position(victim), anchor) <= .1,
          'mouse or ability input moved a downed captain')
    camera_before = camera(victim)
    victim.call('drag 640 330 740 350 right')
    victim.call('wait.frames 18 15000')
    camera_after = camera(victim)
    check(camera_before != camera_after, 'downed control fence also blocked the local camera')
    check(distance(frame_position(victim), anchor) <= .1, 'camera manipulation displaced the downed captain')
    after = counters(victim)
    check(after['entries'] == before['entries'] + 1, 'one knockout repeated native transition teardown')
    check(after['brain'] > before['brain'] and after['wasd'] > before['wasd'],
          'held input did not exercise the real native brain/movement fences')
    for game in games:
        check('total faults: 0' in game.call('exc'), 'native control test logged an exception')
        game.record('downed-final')
    for index in range(4):
        victim.shot('downed-render-'+str(index))
    return dict(victim=victim.instance, run=victim.adventure()['run'], anchor=anchor, samples=samples,
                alive_input_before=initial,alive_input_after=live_input,
                counters_before=before, counters_after=after, camera_before=camera_before, camera_after=camera_after,
                checks=['raw main-thread native anchor under held movement/jump; asynchronous physics samples retained', 'mouse/ability movement blocked',
                        'camera remains usable', 'one downed transition', 'no native faults'],
                screenshots_require_visual_review=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', type=Path, required=True)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--victim', choices=('host', 'guest'), default='guest')
    parser.add_argument('--both',action='store_true',help='test both origins, including native revival between knockouts')
    parser.add_argument('--launch-town',action='store_true',help='launch Town using existing saved captains first')
    parser.add_argument('--captains',default='PreparationNew41,PreparationNew42')
    parser.add_argument('--port',type=int,default=47870)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    records = verify_lab(args.metadata, (args.host, args.guest))
    games = [Game(instance, args.out,timeout=180) for instance in (args.host, args.guest)]
    readers = []
    try:
        if args.launch_town:
            host,guest=games
            host.call('coop.adventure cancel',required=False)
            for game in games:
                menu(game,records[game.instance]);game.call('coop.leave')
            host.call(f'coop.host {args.port} DownedHost')
            guest.call(f'coop.join 127.0.0.1:{args.port} DownedGuest')
            host.wait('two downed-test participants',lambda:host.adventure()['players']==2)
            host.call('coop.adventure start '+TOWN)
            names=args.captains.split(',')
            check(len(names)==2,'two existing captain names are required')
            for game,name in zip(games,names):game.prepare_captain(name,create=False)
            for game in games:lobby(game,2)
            for game in (guest,host):click_named(game,'prep_ready')
            playing(games,records)
        readers = [AdventureMemory(game, records[game.instance]) for game in games]
        if args.both:
            if readers[0].goals()['act']==0:
                for game in games:game.call(f'coop.adventure.world near 169 {game.player_id()} 0 0')
                games[0].wait('real Town MoveTo populates act1',lambda:all(m.goals()['act']==1 for m in readers))
            check(all(m.goals()['act']==1 for m in readers),'both-origin regression requires populated Town act1')
            cases=[]
            for victim_index in (1,0):
                case_out=args.out/('guest-origin' if victim_index else 'host-origin')
                case_out.mkdir(parents=True,exist_ok=True)
                for game in games:game.output=case_out
                # A positive X offset at the gate is inside its tower. Stage
                # the tested captain in the open street and the rescuer away
                # from its collision body until proximity revival is requested.
                victim,rescuer=games[victim_index],games[1-victim_index]
                marker_teleport(victim,records[victim.instance],169,-5)
                rescuer.call(f'coop.adventure.world near 98 {rescuer.player_id()} -10 0')
                time.sleep(2.5)
                case=run(games,readers,victim_index)
                case['revive']=revive(games,readers,victim_index,records)
                (case_out/'case.json').write_text(json.dumps(case,indent=2)+'\n',encoding='utf-8')
                cases.append(case)
            result=dict(cases=cases,goals=[memory.goals() for memory in readers])
        else:
            result = run(games, readers, args.victim == 'guest')
        result.update(result='PASS', profiles=records)
        (args.out / 'result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print('PASS: native downed controls and live camera')
        return 0
    except (FixtureError, OSError, ValueError) as error:
        print('FAIL:', error)
        return 1
    finally:
        for reader in readers:
            reader.close()
        for game in games:
            game.save_log()


if __name__ == '__main__':
    raise SystemExit(main())
