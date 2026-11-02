#!/usr/bin/env python3
"""Bounded ordinary Crew UI steps on an already-owned hidden lab party.

Preparation makes no game connections. Execution never deploys, launches,
writes profiles, calls native functions, or changes game memory. The measured
empty-slot point is checked against native UI IDs before it receives input.
"""
import argparse
import json
from pathlib import Path
import re
import time

from coop_adventure import FixtureError
from coop_adventure_editor import EditorPair, status
from coop_editor_creations import verify_viewport
from coop_editor_crew_terrain import semantic
from coop_editor_party import Ownership, PublicGame, parse_context, parse_history


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256)
    deadline=time.monotonic()+args.deadline
    games=[PublicGame(i,args.output,args.timeout,deadline) for i in args.instances]
    host=games[0];author=next(game for game in games if game.instance==args.author)
    result=dict(passed=False,stage=args.stage,author=args.author,candidate=args.dll_sha256,checks=[])
    context=None;before_messages={}
    def write():
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def settled(revision):
        rows=[status(game) for game in games]
        if any(row.get('frozen') for row in rows):raise FixtureError('A Crew edit froze a participant')
        if context and any(parse_context(row['text'])!=context for row in rows):raise FixtureError('The Crew party context changed')
        if any(row.get('revision',0)>revision for row in rows):raise FixtureError('A native action produced duplicate transactions')
        for game,row in zip(games,rows):
            message=row['text'].splitlines()[1:2]
            if message!=before_messages.get(game.instance,message) and any('The edit was rejected' in text for text in message):
                raise FixtureError('Native Crew edit rejected: '+message[0])
        if not all(row.get('phase')==4 and row.get('ready') and not row.get('gesture') and row.get('revision')==row.get('durable')==revision for row in rows):return False
        checks=[game.call('coop.editor check').strip() for game in games]
        return checks[0].startswith('CONVERGED ') and len(set(checks))==1
    def record(label):
        owner.verify_windows();rows=[]
        for game in games:
            faults=game.call('exc')
            if 'total faults: 0' not in faults:raise FixtureError('Native fault: '+faults)
            rows.append(dict(instance=game.instance,status=status(game),check=game.call('coop.editor check'),
                             inspect=game.call('coop.editor inspect'),history=game.call('coop.editor history'),faults=faults))
        result['checks'].append(dict(label=label,participants=rows));write()
    try:
        result['windows']=verify_viewport(owner,games,[1024,768])
        host.wait('exact durable baseline',lambda:settled(args.revision))
        context=parse_context(status(host)['text']);before_messages={game.instance:status(game)['text'].splitlines()[1:2] for game in games}
        record('before');before=len(parse_history(host.call('coop.editor history')));before_semantic=semantic(host)
        if args.stage!='probe':author.call('coop.uistate 0')
        revision=args.revision
        if args.stage in ('capacity','capacity-add'):
            author.click_control(0x0774B060);revision+=1
            host.wait('all-participant Crew capacity',lambda:settled(revision))
            if len(parse_history(host.call('coop.editor history')))!=before+1:raise FixtureError('Capacity did not create one Undo item')
            record('capacity');before+=1;before_messages={game.instance:status(game)['text'].splitlines()[1:2] for game in games}
        if args.stage in ('add','capacity-add'):
            x,y=args.slot;hits=author.call(f'coop.ui at {x} {y}');result['slot_hit']=hits;write()
            if '03D982FF ' not in hits or '03D98298 ' not in hits:raise FixtureError('The measured native Crew slot is absent')
            author.click(x,y);author.wait('native Creature picker',lambda:author.control(0x53C6DC80))
            # The visible native title is "Choose one or more Creatures".
            # A hidden "Choose A Crew Member" caption is not a readiness test.
            author.click(916,115);author.call('text Spice');author.call('key enter');author.call('wait.frames 30 30000')
            author.shot('spice-search');result['search']=author.call('coop.ui findall "Spice"',required=False);write()
            author.choose_card('Spice Crawler');revision+=1
            host.wait('all-participant immutable Crew admission',lambda:settled(revision))
            history=parse_history(host.call('coop.editor history'))
            if len(history)!=before+1 or history[-1][-1]!='Edit adventure crew':raise FixtureError('Crew Add did not create exactly one Crew Undo item')
            if history[-1][0]!=author.player_id():raise FixtureError('Crew history belongs to another author')
            record('add')
        if args.stage=='lock':
            author.call(f'move {args.slot[0]} {args.slot[1]}');author.call('wait.frames 8 15000')
            author.click_control(0x07C4B4A0);revision+=1;host.wait('all-participant Crew lock',lambda:settled(revision))
            history=parse_history(host.call('coop.editor history'))
            if len(history)!=before+1 or history[-1][-1]!='Edit adventure crew' or history[-1][0]!=author.player_id():raise FixtureError('Crew lock history is not one author-owned item')
            record('lock')
        if args.stage=='history':
            for key in ('z','y'):
                EditorPair.hotkey(author,key);revision+=1;host.wait('author-owned native Crew history',lambda:settled(revision))
                if len(parse_history(host.call('coop.editor history')))!=before:raise FixtureError('Undo/Redo created a duplicate history entry')
                record('history-'+key)
            if semantic(host)!=before_semantic:raise FixtureError('Crew Redo lost authored values')
        author.shot('after');record('after');result['passed']=True;result['final_revision']=revision
        print(json.dumps(dict(passed=True,stage=args.stage,revision=revision)))
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        write()
        for game in games:game.save_log()
        owner.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true');parser.add_argument('--metadata',type=Path)
    parser.add_argument('--instances',type=int,nargs='+',default=[95,96,97,98]);parser.add_argument('--author',type=int,default=95)
    parser.add_argument('--dll-sha256');parser.add_argument('--output',type=Path);parser.add_argument('--revision',type=int)
    parser.add_argument('--stage',choices=['probe','capacity','add','capacity-add','lock','history'],default='probe')
    parser.add_argument('--slot',type=int,nargs=2,default=[998,489]);parser.add_argument('--timeout',type=int,default=90);parser.add_argument('--deadline',type=int,default=900)
    args=parser.parse_args()
    if not args.execute:
        print(json.dumps(dict(prepared=True,launches_games=False,viewport=[1024,768],stages=parser._option_string_actions['--stage'].choices)));return
    if (not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or '') or args.revision is None or args.revision<0 or
            len(args.instances) not in (2,4) or len(set(args.instances))!=len(args.instances) or args.author not in args.instances or
            not all(0<=value<limit for value,limit in zip(args.slot,[1024,768])) or not 30<=args.timeout<=300 or not 60<=args.deadline<=1800):
        parser.error('Exact owned metadata/hash, author, baseline revision and bounded2/4-player arguments required')
    run(args)


if __name__=='__main__':main()
