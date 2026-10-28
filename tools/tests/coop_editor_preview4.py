#!/usr/bin/env python3
"""Focused native preview 4 regression on an already owned, hidden lab pair.

Only ordinary UI input and public diagnostics are used. No process launch,
deployment, native calls, memory writes or personal profiles are allowed.
"""
import argparse
import ctypes
import json
from pathlib import Path
import re
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from coop_editor_party import Ownership,PublicGame,parse_history,parse_party_positions,native_play
from coop_adventure_editor import status,EditorPair
from coop_editor_native_flow import settle
from windows_lab import json_write
from sporectl import quote

def modified_click(owner,game,x,y,ctrl=False,shift=False):
    """Ordinary mouse messages with modifier bits omitted by SporeKit 0.3.4."""
    owner.verify_windows()
    match=re.search(r'\bhwnd=([0-9A-Fa-f]+)',game.call('info'))
    if not match:raise RuntimeError('Owned input HWND is unavailable')
    hwnd=int(match[1],16)
    user=ctypes.WinDLL('user32',use_last_error=True)
    user.GetWindowThreadProcessId.argtypes=(ctypes.c_void_p,ctypes.POINTER(ctypes.c_ulong))
    user.PostMessageW.argtypes=(ctypes.c_void_p,ctypes.c_uint,ctypes.c_size_t,ctypes.c_ssize_t)
    def post(message,flags):
        pid=ctypes.c_ulong()
        if not user.GetWindowThreadProcessId(hwnd,ctypes.byref(pid)) or pid.value!=owner.records[game.instance]['pid']:
            raise RuntimeError('Owned input HWND changed identity')
        if not user.PostMessageW(hwnd,message,flags,(x&65535)|((y&65535)<<16)):
            raise ctypes.WinError(ctypes.get_last_error())
    game.call(f'move {x} {y}')
    try:
        if ctrl:game.call('key ctrl down')
        if shift:game.call('key shift down')
        flags=(8 if ctrl else 0)|(4 if shift else 0)
        post(0x200,flags);game.call('wait.frames 3 15000')
        post(0x201,flags|1);game.call('wait.frames 6 15000')
        post(0x202,flags);game.call('wait.frames 6 15000')
    finally:
        if ctrl:game.call('key ctrl up')
        if shift:game.call('key shift up')
        post(0x200,0)

def playtests(games,record,converged,together_only=False,leave_running=False):
    host,guest=games[:2]
    revision=status(host)['revision']
    before=[(g.call('coop.editor check').strip(),g.call('coop.editor history')) for g in games]
    for tester in ([] if together_only else games):
        for game in games:game.call('coop.uistate 0')
        tester.click_control(0x07104131)
        host.wait('solo native Play',lambda:all(status(g).get('phase')==6 for g in games) and status(tester).get('playtesting'))
        for observer in games:
            if observer is not tester and (status(observer).get('playtesting') or not status(observer).get('editing')):
                raise RuntimeError('Solo Play moved an observer out of its editor')
        if any(g.adventure()['run'] for g in games):raise RuntimeError('Solo Play enabled party gameplay')
        record('solo-play-'+str(tester.instance))
        tester.click_control(0x07104130)
        host.wait('solo native Build and restoration acknowledgements',lambda:converged(revision))
        if [(g.call('coop.editor check').strip(),g.call('coop.editor history')) for g in games]!=before:
            raise RuntimeError('Solo return changed the committed draft or history')
        record('solo-build-return-'+str(tester.instance))
    def prompt(game):
        match=re.search(r"^editor_prompt_button0 (\d+) (\d+) (\d+) (\d+) 1 ",game.call('coop.uistate'),re.M)
        if not match:return False
        r=tuple(map(int,match.groups()));game.click((r[0]+r[2])//2,(r[1]+r[3])//2);return True
    guest.call('coop.uistate 1');guest.call('wait.frames 6 15000');guest.press('Test Together')
    host.wait('guest Test Together host approval',lambda:'editor_prompt=2' in host.call('coop.uistate'))
    record('test-together-request');prompt(host)
    host.wait('explicit readiness on all participants',lambda:all('editor_prompt=3' in g.call('coop.uistate') for g in games))
    for game in games[1:]:prompt(game)
    time.sleep(.5)
    if any(status(g).get('playtesting') for g in games):raise RuntimeError('Group started before host readiness')
    prompt(host)
    host.wait('one running co-op preview',lambda:all(status(g).get('playtesting') and 'Adventure in progress' in g.adventure()['text'] for g in games))
    runs={g.adventure()['run'] for g in games}
    if len(runs)!=1 or not next(iter(runs)):raise RuntimeError('Group has different run identities')
    player_ids=[g.player_id() for g in games]
    def positions():
        rows=[parse_party_positions(g.call('coop.avatar'),g.call('coop.status'),len(games)-1) for g in games]
        if not all(rows):return False
        for observer in rows:
            for peer,remote in observer['remotes'].items():
                if peer not in player_ids:return False
                local=rows[player_ids.index(peer)]['local']
                if sum((a-b)**2 for a,b in zip(remote['puppet_position'],local))>4:return False
        return rows
    initial=host.wait('reciprocal live remote captains',positions)
    record('test-together-live-captains')
    for mover in games:
        mover.call('coop.uistate 0');mover.call('key w down')
        try:mover.call('wait.frames 30 15000')
        finally:mover.call('key w up')
    later=host.wait('reciprocal moving remote captains',positions)
    if any(sum((a-b)**2 for a,b in zip(initial[i]['local'],later[i]['local']))<.25 for i in range(len(games))):
        raise RuntimeError('A native captain did not move during co-op preview')
    for first,last in zip(initial,later):
        for peer,remote in first['remotes'].items():
            if sum((a-b)**2 for a,b in zip(remote['puppet_position'],last['remotes'][peer]['puppet_position']))<.25:
                raise RuntimeError('A remote captain did not move reciprocally')
    record('test-together-reciprocal-movement')
    if leave_running:return
    host.click_control(0x07104130);host.wait('coordinated group Build return',lambda:converged(revision))
    if [(g.call('coop.editor check').strip(),g.call('coop.editor history')) for g in games]!=before:
        raise RuntimeError('Group return changed the draft or history')
    record('test-together-build-return')

def properties(owner,games,record,converged,skip=0):
    host=games[0]
    completed=0
    def edit(game,label,action):
        nonlocal completed
        completed+=1
        if completed<=skip:return
        revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
        action();host.wait(label,lambda:converged(revision+1))
        if len(parse_history(host.call('coop.editor history')))!=len(history)+1:
            raise RuntimeError(label+' did not produce exactly one Undo item')
        record(label)
    for game in games:
        for peer in games:peer.call('coop.uistate 0')
        game.call('move 88 165');game.call('wait.frames 6 15000')
        game.click(148,126);game.call('wait.frames 10 15000');game.click(148,126)
        game.wait('native class ownership',lambda:'lease scope=Class ' in game.call('coop.editor leases'))
        role=str(game.instance)
        edit(game,'health-'+role,lambda:game.click(220 if game is games[0] else 190,310))
        edit(game,'awareness-'+role,lambda:game.click(170 if game is games[0] else 119,226))
        edit(game,'speed-'+role,lambda:game.click(175 if game is games[0] else 235,254))
        edit(game,'damage-'+role,lambda:game.click(240 if game is games[0] else 200,282))
        edit(game,'stance-'+role,lambda:game.click(240 if game is games[0] else 83,193))
        edit(game,'respawn-'+role,lambda:game.click(132,647))
        edit(game,'visibility-'+role,lambda:game.click(42,382))
        edit(game,'restore-visibility-'+role,lambda:game.click(42,382))
        def speech():
            game.click(166,351)
            x1,y1,x2,y2,_=game.control(0xCEFA1100)
            game.click((x1+x2)//2,(y1+y2)//2);game.call('key end')
            game.call('text '+quote(' Preview four participant '+role+' speech'))
            game.click(x2+66,y1-30)
        edit(game,'speech-collection-'+role,speech)
        def captions():
            game.click(166,351)
            x1,y1,x2,y2,_=game.control(0xCEFA1100)
            game.click(x1+174,y1-30)
            x1,y1,x2,y2,_=game.control(0xCEFA1100)
            game.click((x1+x2)//2,(y1+y2)//2);game.call('key end')
            game.call('text '+quote(' Preview four participant '+role+' caption'))
            game.click(x2+66,y1-30)
        edit(game,'caption-collection-'+role,captions)
        game.click(282,161);game.call('wait.frames 10 15000')

def movement(games,record,converged):
    host=games[0];pair=object.__new__(EditorPair)
    for game in games:
        for peer in games:peer.call('coop.uistate 0')
        host.wait('previous gesture releases ownership',lambda:all(not re.search(r'^lease ',p.call('coop.editor leases'),re.M) for p in games))
        objects=pair.objects(game)
        target=next((key for key,row in objects.items() if row['screen']),None)
        if target is None:raise RuntimeError('A visible native placement is required')
        source=pair.body(game,target);revision=status(host)['revision']
        history=parse_history(host.call('coop.editor history'))
        EditorPair.slow_drag(game,source,(source[0]+(-35 if game is host else 35),source[1]))
        host.wait('one direct drag by '+str(game.instance),lambda:converged(revision+1))
        after=pair.objects(host)
        if after[target]['position']==objects[target]['position']:raise RuntimeError('Direct drag did not move its placement')
        if len(parse_history(host.call('coop.editor history')))!=len(history)+1:raise RuntimeError('Direct drag produced duplicate Undo items')
        record('direct-drag-'+str(game.instance))

def locate_cast(game,tab,name):
    game.call('coop.uistate 0')
    for attempt in range(2):
        game.click(tab,42);game.call('move 88 170');game.call('wait.frames 8 15000')
        if game.caption(name):break
    else:raise RuntimeError('The expected native cast card is not visible: '+name)
    game.call('click 88 170 right');game.call('wait.frames 45 15000')
    return game.wait('located cast selection',lambda:status(game).get('selection'))

def sound(games,record,converged):
    host=games[0];pair=object.__new__(EditorPair)
    for game in games:
        target=locate_cast(game,261,'Alarm Buzz');before=pair.objects(game)
        if target not in before or not before[target]['screen'] or sum((a-b)**2 for a,b in zip(before[target]['screen'],(512,394)))>9:
            raise RuntimeError('The first audio cast entry did not locate the emitter origin')
        revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
        EditorPair.slow_drag(game,(512,394),(542 if game is host else 482,394))
        host.wait('sound movement by '+str(game.instance),lambda:converged(revision+1))
        after=pair.objects(game)
        if after[target]['position']==before[target]['position']:raise RuntimeError('The native audio origin did not move')
        if any(after[target].get(field)!=before[target].get(field) for field in ('pitch','gain')):
            raise RuntimeError('Audio movement changed authored pitch or gain')
        if len(parse_history(host.call('coop.editor history')))!=len(history)+1:raise RuntimeError('Audio movement produced duplicate Undo items')
        for key,row in before.items():
            if key!=target and any(after[key].get(field)!=row.get(field) for field in ('position','rotation','scale','destination','destination_rotation','destination_scale')):
                raise RuntimeError('Audio movement changed another placement')
        record('sound-movement-'+str(game.instance))
        EditorPair.hotkey(game,'z');host.wait('sound movement Undo',lambda:converged(revision+2))
        restored=pair.objects(game)[target]
        if any(restored.get(field)!=before[target].get(field) for field in ('position','rotation','scale','relative_scale','elevation','pitch','gain')):
            raise RuntimeError('Audio Undo did not restore its complete pose')
        record('sound-movement-undo-'+str(game.instance))

def teleporters(games,record,converged):
    host=games[0];pair=object.__new__(EditorPair)
    pose=('position','rotation','scale')
    destination=('destination','destination_rotation','destination_scale')
    for game in games:
        for endpoint,changed,preserved in (('entrance',pose,destination),('destination',destination,pose)):
            target=locate_cast(game,219,'Teleporter')
            for attempt in range(8):
                point=pair.objects(game)[target].get('destination_screen')
                if point and 365<point[0]<820 and 130<point[1]<520:break
                game.call('wheel 820 400 -1');game.call('wait.frames 30 15000')
            before=pair.objects(game);row=before[target]
            point=row['screen' if endpoint=='entrance' else 'destination_screen']
            if not point or not 345<point[0]<850 or not 100<point[1]<550:
                raise RuntimeError('Teleporter endpoint is outside the ordinary world input area')
            revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
            EditorPair.slow_drag(game,tuple(round(n) for n in point),(round(point[0]+35),round(point[1]+15)))
            host.wait(endpoint+' movement by '+str(game.instance),lambda:converged(revision+1))
            after=pair.objects(game)[target]
            if all(after.get(field)==row.get(field) for field in changed):raise RuntimeError('Teleporter endpoint did not move')
            if any(after.get(field)!=row.get(field) for field in preserved):raise RuntimeError('Moving one endpoint changed the other endpoint')
            if len(parse_history(host.call('coop.editor history')))!=len(history)+1:raise RuntimeError('Endpoint movement produced duplicate history')
            record('teleporter-'+endpoint+'-'+str(game.instance))
            EditorPair.hotkey(game,'z');host.wait('endpoint Undo',lambda:converged(revision+2))
            if any(pair.objects(game)[target].get(field)!=row.get(field) for field in pose+destination):
                raise RuntimeError('Endpoint Undo changed the other endpoint or failed to restore its transform')
            record('teleporter-'+endpoint+'-undo-'+str(game.instance))

def ball(games,record,converged):
    host=games[0];pair=object.__new__(EditorPair)
    for game in games:
        target=locate_cast(game,219,'Teleporter')
        before=pair.objects(game)[target];revision=status(host)['revision']
        # Native Locate restores this fixture's close view. The ordinary
        # white multi-axis sphere is above the entrance, outside its ring.
        screen=before['screen'];point=(round(screen[0]+31),round(screen[1]-128))
        record('ball-before-'+str(game.instance))
        EditorPair.slow_drag(game,point,(point[0]+25,point[1]+20))
        host.wait('multi-axis ball rotation',lambda:converged(revision+1))
        after=pair.objects(game)[target]
        if sum(a!=b for a,b in zip(before['rotation'],after['rotation']))<3:
            raise RuntimeError('The native multi-axis ball did not rotate across axes')
        if any(after.get(field)!=before.get(field) for field in ('position','scale','destination','destination_rotation','destination_scale')):
            raise RuntimeError('Ball rotation changed a position, scale or the other endpoint')
        record('ball-rotation-'+str(game.instance))
        EditorPair.hotkey(game,'z');host.wait('ball Undo',lambda:converged(revision+2))
        if pair.objects(game)[target]['rotation']!=before['rotation']:raise RuntimeError('Ball Undo failed')
        record('ball-undo-'+str(game.instance))

def class_removal(games,record,converged):
    host=games[0];pair=object.__new__(EditorPair)
    for game in games[:2]:
        target=locate_cast(game,261,'Alarm Buzz');before=pair.objects(game)
        revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
        game.call('move 88 170');game.call('wait.frames 8 15000');game.click(29,160)
        host.wait('native cast removal',lambda:converged(revision+1))
        if target in pair.objects(game):raise RuntimeError('Removing the cast entry retained its placement')
        if len(parse_history(host.call('coop.editor history')))!=len(history)+1:raise RuntimeError('Cast removal produced duplicate history')
        record('cast-removal-'+str(game.instance))
        for key,delta,present in (('z',2,True),('y',3,False),('z',4,True)):
            EditorPair.hotkey(game,key);host.wait('cast removal history '+key,lambda:converged(revision+delta))
            actual=pair.objects(game)
            if (target in actual)!=present:raise RuntimeError('Cast removal Undo/Redo lost its placement mapping')
            if present:
                for field in ('position','rotation','scale','pitch','gain'):
                    if actual[target].get(field)!=before[target].get(field):raise RuntimeError('Cast restoration changed authored fields')
            record('cast-removal-history-'+key+'-'+str(delta)+'-'+str(game.instance))

def disguises(games,record,converged):
    host=games[0];pair=object.__new__(EditorPair)
    for game in games[:2]:
        target=locate_cast(game,219,'Teleporter');before=pair.objects(game)
        for attempt in range(3):
            if game.caption('Speech/Captions'):break
            game.call('move 88 170');game.call('wait.frames 6 15000');game.click(148,126)
        game.wait_caption('Speech/Captions')
        game.wait('class ownership before disguise',lambda:'lease scope=Class ' in game.call('coop.editor leases'))
        revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
        game.click(160,450);game.press('Disguised')
        game.wait_caption('Choose an Object');game.choose_card('Abandonned Saucer')
        host.wait('native disguise picker commit',lambda:converged(revision+1))
        if len(parse_history(host.call('coop.editor history')))!=len(history)+1:raise RuntimeError('Disguise created duplicate history')
        if set(pair.objects(game))!=set(before):raise RuntimeError('Disguise changed stable placement identities')
        for key,row in before.items():
            if any(pair.objects(game)[key].get(field)!=row.get(field) for field in ('position','rotation','destination','destination_rotation','destination_scale')):
                raise RuntimeError('Disguise changed an authored transform')
        record('disguise-'+str(game.instance))
        for key,delta in (('z',2),('y',3),('z',4)):
            EditorPair.hotkey(game,key);host.wait('disguise history '+key,lambda:converged(revision+delta))
            if key=='z' and any(pair.objects(game)[target].get(field)!=before[target].get(field) for field in ('scale','relative_scale')):
                raise RuntimeError('Disguise Undo did not restore native scale companions')
            record('disguise-history-'+key+'-'+str(delta)+'-'+str(game.instance))
        if game.caption('Speech/Captions'):game.click(282,161)

def asset_matrix(games,record,converged):
    host=games[0]
    for game,custom in ((games[0],False),(games[1],False),(games[0],True),(games[1],True)):
        locate_cast(game,50,'Ace')
        revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
        original=game.call('coop.editor inspect');classes=int(re.search(r'\bclasses=(\d+)',original)[1])
        game.click(240,605);game.wait_caption('Choose one or more Creatures')
        name='Preview4Captain'+str(game.instance) if custom else 'Ace'
        for attempt in range(3):
            game.press('My Creations' if custom else 'Maxis');game.call('wait.frames 25 15000')
            if game.caption(name):break
        game.choose_card(name)
        host.wait('native asset import matrix',lambda:converged(revision+1))
        if int(re.search(r'\bclasses=(\d+)',game.call('coop.editor inspect'))[1])!=classes+1:
            raise RuntimeError('Native import did not create one canonical class')
        if len(parse_history(host.call('coop.editor history')))!=len(history)+1:raise RuntimeError('Import produced duplicate history')
        label=('custom' if custom else 'stock')+'-import-'+str(game.instance);record(label)
        EditorPair.hotkey(game,'z');host.wait('import Undo',lambda:converged(revision+2))
        if int(re.search(r'\bclasses=(\d+)',game.call('coop.editor inspect'))[1])!=classes:raise RuntimeError('Import Undo retained a partial cast entry')
        EditorPair.hotkey(game,'y');host.wait('import Redo',lambda:converged(revision+3));record(label+'-undo-redo')

def batch_imports(owner,games,record,converged):
    from coop_editor_save import exact_card_points
    from uifind import match_groups
    host=games[0]
    for game in games[:2]:
        locate_cast(game,50,'Ace')
        revision=status(host)['revision'];history=parse_history(host.call('coop.editor history'))
        classes=int(re.search(r'\bclasses=(\d+)',game.call('coop.editor inspect'))[1])
        game.click(240,605);game.wait_caption('Choose one or more Creatures')
        game.call('wait.frames 45 15000')
        def stock_cards():
            first=exact_card_points(match_groups(game.console,'Ace'),'Ace')
            second=exact_card_points(match_groups(game.console,'Aepox'),'Aepox')
            return (min(first),min(second)) if first and second else None
        first,second=game.wait('two actual native stock thumbnails',stock_cards)
        game.click(*first);game.call('move 995 750');game.call('wait.frames 7 15000');modified_click(owner,game,*second,ctrl=True)
        game.wait('two native selections',lambda:game.caption('Selected 2 item(s)'))
        game.record('two-stock-assets-selected');game.click_control(0x00AD0E52)
        host.wait('both selected native imports',lambda:converged(revision+2))
        if int(re.search(r'\bclasses=(\d+)',game.call('coop.editor inspect'))[1])!=classes+2:
            raise RuntimeError('Multi-selection lost a native cast entry')
        if len(parse_history(host.call('coop.editor history')))!=len(history)+2:
            raise RuntimeError('Multi-selection did not create one transaction per asset')
        record('two-stock-imports-'+str(game.instance))
        for n,key in enumerate(('z','z','y','y','z','z'),3):
            EditorPair.hotkey(game,key);host.wait('batch asset history '+key,lambda:converged(revision+n))
        if int(re.search(r'\bclasses=(\d+)',game.call('coop.editor inspect'))[1])!=classes:
            raise RuntimeError('Batch Undo retained a cast entry')
        record('two-stock-imports-undo-'+str(game.instance))

def run(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256)
    games=[PublicGame(i,out,90,time.monotonic()+1500) for i in args.instances]
    host,guest=games[:2]
    result={'passed':False,'complete_acceptance':False,'dll_sha256':args.dll_sha256,'checks':[]}
    def record(label):
        owner.verify_windows()
        rows=[]
        for game in games:
            game.record(label)
            rows.append({'instance':game.instance,'status':status(game),
                         'objects':game.call('coop.editor objects'),'history':game.call('coop.editor history'),
                         'faults':game.call('exc')})
        result['checks'].append({'label':label,'participants':rows});json_write(out/'result.json',result)
    def converged(revision=None):
        states=[status(g) for g in games]
        if any(s.get('frozen') for s in states):raise RuntimeError('Editor froze: '+repr(states))
        if not all(s.get('phase')==4 and not s.get('gesture') for s in states):return False
        if revision is not None and not all(s.get('revision')==revision for s in states):return False
        checks=[g.call('coop.editor check').strip() for g in games]
        return checks[0].startswith('CONVERGED ') and len(set(checks))==1
    try:
        for game in ([] if args.attach else games):
            game.call('coop.leave');game.call('coop.uistate 0')
            if game.menu() and game.control(0x062D88E9):game.click(860,302)
            game.dismiss_registration();settle(game)
        if args.onboard:
            for game in games:
                game.onboard('Preview4Captain'+str(game.instance));game.exit_adventure()
            record('native-first-use-complete');result['passed']=True;return
        if args.recover:
            host.call(f'coop.host {args.port} Preview4RecoveredHost')
            host.call('coop.editor recover')
            host.wait('cold checkpoint recovery',lambda:status(host).get('phase')==4)
            for game in games[1:]:game.call(f'coop.join 127.0.0.1:{args.port} Preview4RecoveredGuest{game.instance}')
            host.wait('recovered shared editor',converged)
            record('cold-checkpoint-recovery')
        if args.stage=='baseline':
            host.wait('shared native editors',converged)
            record('verified-shared-baseline');result['passed']=True;return
        if args.stage in ('playtests','group-launch'):
            host.wait('shared native editors',converged)
            playtests(games,record,converged,args.stage=='group-launch',args.stage=='group-launch');result['passed']=True;return
        if args.stage=='properties':
            host.wait('shared native editors',converged)
            properties(owner,games,record,converged,args.skip_property_checks);result['passed']=True;return
        if args.stage=='movement':
            host.wait('shared native editors',converged)
            movement(games,record,converged);result['passed']=True;return
        if args.stage=='sound':
            host.wait('shared native editors',converged)
            sound(games,record,converged);result['passed']=True;return
        if args.stage=='teleporters':
            host.wait('shared native editors',converged)
            teleporters(games,record,converged);result['passed']=True;return
        if args.stage=='ball':
            host.wait('shared native editors',converged)
            ball(games,record,converged);result['passed']=True;return
        if args.stage=='class-removal':
            host.wait('shared native editors',converged)
            class_removal(games,record,converged);result['passed']=True;return
        if args.stage=='disguises':
            host.wait('shared native editors',converged)
            disguises(games,record,converged);result['passed']=True;return
        if args.stage=='asset-matrix':
            host.wait('shared native editors',converged)
            asset_matrix(games,record,converged);result['passed']=True;return
        if args.stage=='batch-imports':
            host.wait('shared native editors',converged)
            batch_imports(owner,games,record,converged);result['passed']=True;return
        if not args.attach:
            host.wait('native Create menu',lambda:host.caption('Create'))
            host.press('Create');host.wait('creator radial',lambda:host.control(0x07A7B140));host.click(553,522)
            host.wait('adventure creator',lambda:host.control(0x07A7F798));host.click(820,350)
            host.wait_caption('Choose a Planet');host.choose_card('Alpine Valley')
            host.wait('native editor',lambda:status(host).get('ready'))
            host.call('wait.frames 40 15000');host.call('drag 967 655 700 470');host.call('wait.frames 35 15000')
            host.call(f'coop.host {args.port} Preview4Host');guest.call(f'coop.join 127.0.0.1:{args.port} Preview4Guest')
        host.wait('empty shared editor',lambda:converged(0));record('empty-shared-editor')
        for game in games:game.call('wait.frames 60 15000')
        # Guest imports a stock creature before any placement exists. This
        # catches host-only allocation and empty-class-roster assumptions.
        if not guest.caption('Choose one or more Creatures'):
            guest.click(88,165);guest.call('wait.frames 30 15000')
            if not guest.caption('Choose one or more Creatures'):guest.click(88,165)
        guest.wait_caption('Choose one or more Creatures')
        guest.press('Maxis');guest.call('wait.frames 30 15000');guest.choose_card('Ace')
        host.wait('guest native stock cast import',lambda:converged(1));record('guest-stock-import')
        history=parse_history(host.call('coop.editor history'))
        if len(history)!=1:raise RuntimeError('One import did not produce exactly one synchronized Undo item')
        EditorPair.hotkey(guest,'z');host.wait('guest import Undo',lambda:converged(2));record('guest-import-undo')
        EditorPair.hotkey(guest,'y');host.wait('guest import Redo',lambda:converged(3));record('guest-import-redo')
        EditorPair.slow_drag(guest,(88,165),(601,470));host.wait('guest places imported asset',lambda:converged(4));record('guest-placement')
        result['passed']=True
    except Exception as error:
        result['error']=repr(error)
        try:record('failure')
        except Exception as secondary:result['record_error']=repr(secondary)
        raise
    finally:
        json_write(out/'result.json',result)
        for game in games:game.save_log()
        owner.close()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true');parser.add_argument('--onboard',action='store_true')
    parser.add_argument('--stage',choices=('baseline','imports','playtests','group-launch','properties','movement','sound','teleporters','ball','class-removal','disguises','asset-matrix','batch-imports'),default='imports')
    parser.add_argument('--attach',action='store_true',help='Continue an owned, empty shared native editor pair')
    parser.add_argument('--recover',action='store_true',help='Recover the private host checkpoint and join its guest')
    parser.add_argument('--metadata',type=Path);parser.add_argument('--dll-sha256')
    parser.add_argument('--output',type=Path);parser.add_argument('--instances',type=int,nargs='+',default=[91,92])
    parser.add_argument('--port',type=int,default=47916)
    parser.add_argument('--skip-property-checks',type=int,default=0,help='Resume after this many recorded property checks')
    args=parser.parse_args()
    if not args.execute:print(json.dumps({'prepared':True,'launches_games':False}));return
    if not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or ''):
        parser.error('Execution requires exact private metadata, output and frozen DLL SHA-256')
    if not 2<=len(args.instances)<=4 or len(set(args.instances))!=len(args.instances) or not 1024<=args.port<=65535:
        parser.error('Invalid private party or port')
    run(args)

if __name__=='__main__':main()
