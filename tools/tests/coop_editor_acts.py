#!/usr/bin/env python3
"""Native act roster regression on an owned, hidden two to four player editor.

Attaches to an existing private shared editor. Uses normal native controls and
public diagnostics only; never launches, deploys or changes profile databases.
"""
import argparse
import re
import time
from pathlib import Path

from coop_editor_party import Ownership, PublicGame, parse_history
from coop_adventure_editor import EditorPair, status
from windows_lab import json_write


def acts(game):
    text=game.call('coop.editor acts')
    current=re.search(r'^editor_act=(\d+)',text)
    names=re.findall(r'^act=\d+ name="([^"]*)"',text,re.M)
    if not current or not names:raise RuntimeError('Native act diagnostics are unavailable')
    return int(current[1]),names,text


def run(args):
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256)
    games=[PublicGame(i,out,90,time.monotonic()+900)for i in args.instances]
    host,guest=games[:2];result={'passed':False,'dll_sha256':args.dll_sha256,'checks':[]}
    def converged(revision):
        rows=[status(g)for g in games]
        if any(r.get('frozen')for r in rows):raise RuntimeError('A participant froze during act editing')
        if not all(r.get('phase')==4 and r.get('revision')==revision and not r.get('gesture')for r in rows):return False
        checks=[g.call('coop.editor check').strip()for g in games]
        return checks[0].startswith('CONVERGED ')and len(set(checks))==1
    def record(label):
        owner.verify_windows();rows=[]
        for g in games:
            row={'instance':g.instance,'status':status(g),'acts':acts(g)[2],'history':g.call('coop.editor history'),'faults':g.call('exc')}
            if 'total faults: 0'not in row['faults']:raise RuntimeError('Native fault during '+label)
            g.shot(label);rows.append(row)
        result['checks'].append({'label':label,'participants':rows});json_write(out/'result.json',result)
    def change(label,game,action,count):
        revision=status(host)['revision'];history=len(parse_history(host.call('coop.editor history')))
        host.wait('previous authors release idle ownership',lambda:all(not re.search(r'^lease ',g.call('coop.editor leases'),re.M)for g in games))
        action();host.wait(label,lambda:converged(revision+1))
        if any(len(acts(g)[1])!=count for g in games):raise RuntimeError(label+' lost native act companions')
        if len(parse_history(host.call('coop.editor history')))!=history+1:raise RuntimeError(label+' did not create one Undo item')
        record(label)
    def undo(game,revision,label,redo=False):
        host.wait('other authors release idle ownership',lambda:all(g is game or not re.search(r'^lease ',g.call('coop.editor leases'),re.M)for g in games))
        EditorPair.hotkey(game,'y'if redo else'z');host.wait(label,lambda:converged(revision+1))
    try:
        baseline=acts(host)[1];count=len(baseline)
        if not 2<=count<8:raise RuntimeError('The roster fixture needs two to seven existing acts')
        revision=status(host)['revision'];host.wait('initial committed editors',lambda:converged(revision))
        for i,g in enumerate(games):
            g.call('coop.uistate 0');target=min(i+1,count-1)
            if acts(g)[0]!=target:g.click_control(0x0743B8E1+target)
            g.call('wait.frames 8 15000')
        views=[acts(g)[1][acts(g)[0]]for g in games]
        if len(set(acts(g)[0]for g in games))<2:raise RuntimeError('Independent local act browsing failed')
        record('independent-act-browsing')
        def preserved():
            if [acts(g)[1][acts(g)[0]]for g in games]!=views:raise RuntimeError('An unrelated player lost their current act')
        change('guest-adds-native-act',guest,lambda:guest.click_control(0x07BD2DD0),count+1);preserved()
        revision=status(host)['revision'];undo(guest,revision,'guest add Undo')
        if acts(host)[1]!=baseline:raise RuntimeError('Act creation Undo did not restore the original order')
        preserved();record('guest-act-add-undo')
        revision+=1;undo(guest,revision,'guest add Redo',True);preserved();record('guest-act-add-redo')
        def move():
            guest.call('coop.uistate 1');guest.call('wait.frames 8 15000');guest.press('Move act left');guest.call('coop.uistate 0')
        change('guest-reorders-native-act',guest,move,count+1);preserved()
        revision=status(host)['revision'];undo(guest,revision,'reorder Undo');preserved();record('guest-act-reorder-undo')
        revision+=1;undo(guest,revision,'addition Undo after own reorder')
        if acts(host)[1]!=baseline:raise RuntimeError('The guest did not restore the original act roster')
        preserved();record('guest-restores-original-roster')
        change('host-deletes-native-act',host,lambda:host.click_control(0x0743BC38),count-1)
        revision=status(host)['revision'];undo(host,revision,'host deletion Undo');record('host-act-delete-undo')
        if acts(host)[1]!=baseline:raise RuntimeError('The fixture did not restore the original act roster')
        record('original-roster-restored');result['passed']=True
    finally:
        json_write(out/'result.json',result)
        for g in games:g.save_log()
        owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--execute',action='store_true')
    p.add_argument('--metadata',type=Path);p.add_argument('--output',type=Path);p.add_argument('--dll-sha256')
    p.add_argument('--instances',type=int,nargs='+',default=[91,92]);args=p.parse_args()
    if not args.execute:print('Prepared native act regression; no game changes performed.');return
    if not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or '')or not 2<=len(set(args.instances))==len(args.instances)<=4:
        p.error('Execution needs private ownership metadata, output, frozen DLL hash and two to four unique instances')
    run(args)


if __name__=='__main__':main()
