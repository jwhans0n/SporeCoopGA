#!/usr/bin/env python3
"""Native act metadata, goals and captain regression on an owned private party.

Attach to two to four hidden lab editors with an authored captain and a goal.
Drive normal UI, then Undo every test edit. Never
launch games, deploy binaries or change profile databases.
"""
import argparse
import re
import time
from pathlib import Path

from coop_adventure import quote
from coop_adventure_editor import EditorPair, status
from coop_editor_party import Ownership, PublicGame, parse_history
from windows_lab import json_write


def content(game):
    lines=game.call('coop.editor acts').splitlines()
    captain=lines[0].split(' captain=',1)[1]
    return captain,tuple(line for line in lines if line.startswith(('act=',' goal=')))


def run(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256)
    games=[PublicGame(i,out,80,time.monotonic()+900)for i in args.instances]
    host,guest=games[:2];result={'passed':False,'dll_sha256':args.dll_sha256,'checks':[]}
    def settled(revision):
        rows=[status(g)for g in games]
        if any(r.get('frozen')for r in rows):raise RuntimeError('An authoring operation froze an editor')
        if any(r.get('revision',0)>revision for r in rows):raise RuntimeError('An input created duplicate transactions')
        if not all(r.get('phase')==4 and r.get('revision')==revision and not r.get('gesture')for r in rows):return False
        hashes=[g.call('coop.editor check').strip()for g in games]
        return hashes[0].startswith('CONVERGED ')and len(set(hashes))==1
    def record(label):
        owner.verify_windows();rows=[]
        for g in games:
            faults=g.call('exc')
            if 'total faults: 0'not in faults:raise RuntimeError('Native fault during '+label)
            rows.append({'instance':g.instance,'status':status(g),'acts':g.call('coop.editor acts'),'faults':faults})
        result['checks'].append({'label':label,'participants':rows});json_write(out/'result.json',result)
    def change(label,action):
        revision=status(host)['revision'];history=len(parse_history(host.call('coop.editor history')))
        action();host.wait(label,lambda:settled(revision+1))
        if len(parse_history(host.call('coop.editor history')))!=history+1:raise RuntimeError(label+' did not create one Undo item')
        record(label)
    def undo(game,expected,label,redo=False):
        revision=status(host)['revision'];EditorPair.hotkey(game,'y'if redo else'z')
        host.wait(label,lambda:settled(revision+1))
        if any(content(g)!=expected for g in games):raise RuntimeError(label+' lost unrelated authored values')
        record(label)
    def select(game,index):
        if not re.search(r'^editor_act='+str(index)+r'\b',game.call('coop.editor acts')):
            game.click_control(0x0743B8E1+index)
        game.call('wait.frames 10 15000')
    def append(game,control,text):
        game.click_control(control);game.call('key end');game.call('text '+quote(text))
        game.call('wait.frames 8 15000');game.call('key enter')
        game.click(800,450)
    try:
        for g in games:g.call('coop.uistate 0')
        baseline=content(host);revision=status(host)['revision'];host.wait('initial committed editors',lambda:settled(revision))
        count=len(re.findall(r'^act=', '\n'.join(baseline[1]),re.M))
        if count<2:raise RuntimeError('Independent act authoring needs at least two acts')
        select(guest,0);select(host,count-1)
        change('guest-act-name',lambda:append(guest,0x0743B978,' [co-op name]'))
        named=content(host)
        if named==baseline:raise RuntimeError('The native name edit did not change its act')
        def description():
            host.click_control(0x0743B978)
            append(host,0x0710A140,' [co-op description]')
        change('host-other-act-description',description);both=content(host)
        undo(guest,(baseline[0],tuple(baseline[1][:2])+tuple(both[1][2:])),'guest-name-undo-preserves-host-description')
        undo(host,baseline,'host-description-undo')
        # Focusing the native name editor reserves the act while its related
        # controls are used. The timer itself retains its ordinary native UI.
        select(guest,0);guest.click_control(0x0743B978);guest.click(800,450)
        change('guest-act-timer',lambda:guest.click_control(0x07462210))
        if content(host)==baseline:raise RuntimeError('The native timer did not change')
        undo(guest,baseline,'guest-timer-undo')
        goal=next((re.match(r' goal=(\d+):0 ',line)for line in baseline[1]if re.match(r' goal=\d+:0 ',line)),None)
        if not goal:raise RuntimeError('This fixture needs an existing first goal')
        select(guest,int(goal[1]));guest.click_control(0x0743B978);guest.click(800,450)
        change('guest-goal-all-setting',lambda:guest.click_control(0x07E1F2F8))
        undo(guest,baseline,'guest-goal-all-setting-undo')
        guest.click_control(0x0743B978);guest.click(800,450)
        change('guest-goal-deletion',lambda:guest.click_control(0x073BBC38))
        undo(guest,baseline,'guest-goal-deletion-undo')
        for game,label in ((host,'host'),(guest,'guest')):
            host.wait('previous captain ownership expires',lambda:not re.search(r'^lease ',(guest if game is host else host).call('coop.editor leases'),re.M))
            game.call('click 965 660 right');game.call('wait.frames 90 15000')
            change(label+'-captain-lock',lambda:game.click_control(0x0770B540))
            undo(game,baseline,label+'-captain-lock-undo')
        text=guest.call('coop.editor acts');screen=re.search(r'captain_screen=([-\d.e]+),([-\d.e]+)',text)
        if not screen:raise RuntimeError('The selected captain is not visible')
        x,y=round(float(screen[1])),round(float(screen[2])-28)
        change('guest-captain-movement',lambda:EditorPair.slow_drag(guest,(x,y),(x+30,y)))
        moved=content(host)
        if moved[0]==baseline[0]:raise RuntimeError('The native captain did not move')
        undo(guest,baseline,'guest-captain-movement-undo')
        undo(guest,moved,'guest-captain-movement-redo',True)
        undo(guest,baseline,'guest-captain-restored')
        for g in games:g.shot('authoring-restored')
        result['passed']=True
    finally:
        json_write(out/'result.json',result)
        for g in games:g.save_log()
        owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--execute',action='store_true')
    p.add_argument('--metadata',type=Path);p.add_argument('--output',type=Path);p.add_argument('--dll-sha256')
    p.add_argument('--instances',type=int,nargs='+',default=[91,92]);args=p.parse_args()
    if not args.execute:print('Prepared native authoring regression; no game changes performed.');return
    if not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or '')or not 2<=len(set(args.instances))==len(args.instances)<=4:
        p.error('Execution needs private ownership metadata, output, frozen DLL hash and two to four unique instances')
    run(args)


if __name__=='__main__':main()
