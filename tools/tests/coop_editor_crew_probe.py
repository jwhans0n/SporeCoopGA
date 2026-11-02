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
from coop_editor_party import Ownership, PublicGame, parse_context, parse_history, parse_identity
from sporectl import quote


def checked_history(text):
    header=re.match(r'^local=(\d+):(\d+) count=(\d+)\n',text);rows=parse_history(text)
    if not header or int(header[3])!=len(rows):raise FixtureError('Complete canonical Crew history is required')
    return rows


def require_crew_hit(hits,action,header=None):
    rectangles={int(ident,16):tuple(map(int,(left,top,right,bottom))) for ident,left,top,right,bottom in re.findall(
        r'^([0-9A-Fa-f]{8}) (-?\d+) (-?\d+) (-?\d+) (-?\d+) enabled=1\b',hits,re.M)}
    ids=set(rectangles)
    required={0x03D98304}
    if header is None:required.add(0x064A90A0)
    else:
        # Crew's title is a sibling above the card body. A body-center hit
        # cannot include it. Pin the fresh unique Crew title rectangle to this
        # card, so Captain's reused lock/card IDs cannot authorize the click.
        title=re.fullmatch(r'064A90A0 (-?\d+) (-?\d+) (-?\d+) (-?\d+) enabled=1\s*',header)
        card=rectangles.get(0x03D98304)
        if not title or not card:raise FixtureError('The owning Crew title/card is unavailable')
        left,top,right,bottom=map(int,title.groups());x=(left+right)//2
        if not (left<right and top<bottom and card[0]<=x<card[2] and card[1]<=bottom-1<card[3] and top<card[1]+16):
            raise FixtureError('The filled card does not belong to the measured Crew title')
    if action=='lock':required.add(0x07C4B4A0)
    elif action=='remove':required.add(0x04ACFCAF)
    elif action!='replace':raise FixtureError('Unknown bounded Crew card action')
    if not required<=ids or action=='replace' and ids&{0x07C4B4A0,0x04ACFCAF}:
        raise FixtureError('The measured '+action+' point is outside its owning filled Crew control')


def require_history_item(before,after,capability):
    if len(after)!=len(before)+1 or after[-1][-1]!='Edit adventure crew' or after[-1][:2]!=capability:
        raise FixtureError('The Crew action did not create exactly one item for the pinned author generation')


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.dll_sha256)
    deadline=time.monotonic()+args.deadline
    games=[PublicGame(i,args.output,args.timeout,deadline) for i in args.instances]
    host=games[0];author=next(game for game in games if game.instance==args.author)
    result=dict(passed=False,stage=args.stage,author=args.author,candidate=args.dll_sha256,history_input=args.history_input,checks=[])
    context=None;before_messages={};capabilities={}
    def write():
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    def settled(revision):
        rows=[status(game) for game in games]
        if any(row.get('frozen') for row in rows):raise FixtureError('A Crew edit froze a participant')
        if context and any(parse_context(row['text'])!=context for row in rows):raise FixtureError('The Crew party context changed')
        if capabilities and {game.instance:parse_identity(row['text'])[1] for game,row in zip(games,rows)}!=capabilities:
            raise FixtureError('A pinned Crew participant generation changed')
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
        context=parse_context(status(host)['text']);capabilities={game.instance:parse_identity(status(game)['text'])[1] for game in games}
        result['capabilities']={str(instance):list(value) for instance,value in capabilities.items()}
        before_messages={game.instance:status(game)['text'].splitlines()[1:2] for game in games}
        record('before');before=checked_history(host.call('coop.editor history'));before_semantic=semantic(host)
        if args.stage not in ('probe','controls'):author.call('coop.uistate 0')
        revision=args.revision
        if args.stage=='controls':
            author.call(f'move {args.member_point[0]} {args.member_point[1]}');author.call('wait.frames 8 15000')
            result['crew_controls']=dict(tree=author.call('coop.ui findall "Crew"'),
                header=author.call('coop.ui where 064A90A0'),
                filled_card=author.call('coop.ui where 03D98304'),remove_first_match=author.call('coop.ui where 04ACFCAF'),
                lock_first_match=author.call('coop.ui where 07C4B4A0'),member_hit=author.call(f'coop.ui at {args.member_point[0]} {args.member_point[1]}'))
            if args.remove_point:result['crew_controls']['remove_hit']=author.call(f'coop.ui at {args.remove_point[0]} {args.remove_point[1]}')
            write()
        if args.stage in ('capacity','capacity-add'):
            author.click_control(0x0774B060);revision+=1
            host.wait('all-participant Crew capacity',lambda:settled(revision))
            after=checked_history(host.call('coop.editor history'));require_history_item(before,after,capabilities[author.instance])
            if semantic(host)==before_semantic:raise FixtureError('Crew capacity had no authored effect')
            record('capacity');before=after;before_semantic=semantic(host);before_messages={game.instance:status(game)['text'].splitlines()[1:2] for game in games}
        if args.stage in ('add','capacity-add','replace'):
            x,y=args.member_point if args.stage=='replace' else args.slot
            hits=author.call(f'coop.ui at {x} {y}');result['slot_hit']=hits;write()
            if args.stage=='replace':
                result['crew_header']=author.call('coop.ui where 064A90A0');write()
                require_crew_hit(hits,'replace',result['crew_header'])
            elif '03D982FF ' not in hits or '03D98298 ' not in hits:raise FixtureError('The measured native Crew slot is absent')
            author.click(x,y);author.wait('visible native Creature picker',lambda:
                "B44EC3DB " in (hit:=author.call('coop.ui at 500 40')) and "'Choose one or more Creatures'" in hit)
            # The visible native title is "Choose one or more Creatures".
            # A hidden "Choose A Crew Member" caption is not a readiness test.
            search=args.search or 'Spice';card=args.card or 'Spice Crawler'
            # Posted Ctrl+A is interpreted as text by this native search box.
            # Use its freshly measured clear button instead of a modifier.
            search_hit=author.call('coop.ui at 916 115');result['search_field_hit']=search_hit
            field=re.search(r"^D4935A8F 836 106 991 127 enabled=1 - '(.*)'$",search_hit,re.M)
            if not field:raise FixtureError('The measured native search field is unavailable')
            if field[1]:
                clear_hit=author.call('coop.ui at 1007 115');result['search_clear_hit']=clear_hit;write()
                if 'B4935A77 999 108 1015 124 enabled=1' not in clear_hit:
                    raise FixtureError('The measured native search clear button is unavailable')
                author.click(1007,115)
            author.click(916,115)
            author.call('text '+quote(search));author.call('wait.frames 30 30000')
            # Search updates while typing. Enter can accept the already
            # selected current Crew member and close this native picker.
            if "'Choose one or more Creatures'" not in author.call('coop.ui at 500 40'):
                raise FixtureError('The native Creature picker closed during search')
            author.shot('crew-search');result['search']=author.call('coop.ui findall "'+search+'"',required=False);write()
            author.choose_card(card);revision+=1
            host.wait('all-participant immutable Crew admission',lambda:settled(revision))
            history=checked_history(host.call('coop.editor history'));require_history_item(before,history,capabilities[author.instance])
            if semantic(host)==before_semantic:raise FixtureError('Crew selection had no authored effect')
            record('replace' if args.stage=='replace' else 'add')
        if args.stage=='remove':
            x,y=args.remove_point;hits=author.call(f'coop.ui at {x} {y}');result['crew_remove_hit']=hits;write()
            result['crew_header']=author.call('coop.ui where 064A90A0');write()
            require_crew_hit(hits,'remove',result['crew_header']);author.click(x,y);revision+=1
            host.wait('all-participant Crew removal',lambda:settled(revision))
            require_history_item(before,checked_history(host.call('coop.editor history')),capabilities[author.instance])
            if semantic(host)==before_semantic:raise FixtureError('Crew removal had no authored effect')
            record('remove')
        if args.stage=='lock':
            author.call(f'move {args.slot[0]} {args.slot[1]}');author.call('wait.frames 8 15000')
            x,y=args.lock_point;hits=author.call(f'coop.ui at {x} {y}')
            result['crew_lock_hit']=hits;write()
            result['crew_header']=author.call('coop.ui where 064A90A0');write()
            require_crew_hit(hits,'lock',result['crew_header'])
            # Captain and Crew reuse the lock ID. The owning Crew point is
            # measured independently; global first-match lookup is unsafe.
            author.click(x,y);revision+=1;host.wait('all-participant Crew lock',lambda:settled(revision))
            history=checked_history(host.call('coop.editor history'));require_history_item(before,history,capabilities[author.instance])
            record('lock')
        if args.stage=='history' or args.verify_history:
            expected_semantic=semantic(host);expected_history=checked_history(host.call('coop.editor history'))
            own=[row for row in expected_history if row[:2]==capabilities[author.instance] and row[5]]
            if not own or own[-1][-1]!='Edit adventure crew':
                raise FixtureError('The pinned author has no applied Crew history item')
            for key in ('z','y'):
                if args.history_input=='toolbar':author.click_control(0xB006EF6E if key=='z' else 0xF006EFA5)
                else:EditorPair.hotkey(author,key)
                revision+=1;host.wait('author-owned native Crew history',lambda:settled(revision))
                if len(checked_history(host.call('coop.editor history')))!=len(expected_history):raise FixtureError('Undo/Redo created a duplicate history entry')
                if key=='z' and semantic(host)==expected_semantic:raise FixtureError('Crew Undo had no authored effect')
                record('history-'+key)
            if semantic(host)!=expected_semantic:raise FixtureError('Crew Redo lost authored values')
        author.shot('after');record('after');result['passed']=True;result['final_revision']=revision
        print(json.dumps(dict(passed=True,stage=args.stage,revision=revision)))
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        write()
        for game in games:game.save_log()
        owner.close()


def self_test():
    checks=0
    def rejects(callback):
        nonlocal checks
        try:callback()
        except FixtureError:checks+=1
        else:raise AssertionError('An unsafe Crew fixture input was accepted')
    crew='064A90A0 974 451 1005 467 enabled=1\n03D98304 962 459 1044 521 enabled=1\n'
    for action,extra in [('replace',''),('lock','07C4B4A0 968 457 987 476 enabled=1\n'),('remove','04ACFCAF 1005 459 1024 478 enabled=1\n')]:
        require_crew_hit(crew+extra,action);checks+=1
        rejects(lambda a=action,e=extra:require_crew_hit(crew.replace('03D98304','03D982FF')+e,a))
        rejects(lambda a=action,e=extra:require_crew_hit(crew.replace('064A90A0','00000000')+e,a))
    rejects(lambda:require_crew_hit(crew+'07C4B4A0 968 457 987 476 enabled=1\n','replace'))
    rejects(lambda:require_crew_hit(crew+'04ACFCAF 1005 459 1024 478 enabled=1\n','replace'))
    rejects(lambda:require_crew_hit(crew,'remove'))
    rejects(lambda:require_crew_hit(crew,'unknown'))
    body='03D98304 962 459 1044 521 enabled=1\n03D982FF 980 472 1024 518 enabled=1\n'
    title='064A90A0 974 451 1005 467 enabled=1\n'
    require_crew_hit(body,'replace',title);checks+=1
    rejects(lambda:require_crew_hit(body.replace('459 1044 521','600 1044 662'),'replace',title))
    rejects(lambda:require_crew_hit(body,'replace',title.replace('enabled=1','enabled=0')))
    rejects(lambda:require_crew_hit(body,'replace',title.replace('974 451 1005 467','100 451 131 467')))
    rejects(lambda:require_crew_hit(body.replace('enabled=1','enabled=0'),'replace',title))
    rejects(lambda:require_crew_hit(body+'07C4B4A0 968 457 987 476 enabled=1\n','replace',title))
    text='local=1:7 count=1\nauthor=1:7 sequence=3 epoch=8 revision=9 applied=1 redo=0 Edit adventure crew\n'
    rows=checked_history(text);require_history_item([],rows,(1,7));checks+=1
    rejects(lambda:checked_history(text.replace('count=1','count=2')))
    rejects(lambda:require_history_item([],rows,(1,8)))
    rejects(lambda:require_history_item(rows,rows,(1,7)))
    rejects(lambda:require_history_item([],rows+[rows[0]],(1,7)))
    rejects(lambda:require_history_item([],[(1,7,3,8,9,1,0,'Edit captain')],(1,7)))
    print(f'Crew fixture guards: {checks} checks passed')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--execute',action='store_true');parser.add_argument('--self-test',action='store_true');parser.add_argument('--metadata',type=Path)
    parser.add_argument('--instances',type=int,nargs='+',default=[95,96,97,98]);parser.add_argument('--author',type=int,default=95)
    parser.add_argument('--dll-sha256');parser.add_argument('--output',type=Path);parser.add_argument('--revision',type=int)
    parser.add_argument('--stage',choices=['probe','controls','capacity','add','capacity-add','replace','remove','lock','history'],default='probe')
    parser.add_argument('--slot',type=int,nargs=2,default=[998,489]);parser.add_argument('--lock-point',type=int,nargs=2,default=[977,466])
    parser.add_argument('--member-point',type=int,nargs=2,default=[998,489]);parser.add_argument('--remove-point',type=int,nargs=2)
    parser.add_argument('--search');parser.add_argument('--card');parser.add_argument('--verify-history',action='store_true')
    parser.add_argument('--history-input',choices=['hotkeys','toolbar'],default='hotkeys')
    parser.add_argument('--timeout',type=int,default=90);parser.add_argument('--deadline',type=int,default=900)
    args=parser.parse_args()
    if args.self_test:self_test();return
    if not args.execute:
        print(json.dumps(dict(prepared=True,launches_games=False,viewport=[1024,768],stages=parser._option_string_actions['--stage'].choices,
            replace_requires='Explicit alternate --search/--card; owning filled Crew body hit',remove_requires='Measured --remove-point hitting Crew03D98304/header064A90A0/button04ACFCAF',
            controls_is_read_only=True)));return
    if (not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or '') or args.revision is None or args.revision<0 or
            len(args.instances) not in (2,4) or len(set(args.instances))!=len(args.instances) or args.author not in args.instances or
            not all(0<=value<limit for value,limit in zip(args.slot,[1024,768])) or not 30<=args.timeout<=300 or not 60<=args.deadline<=1800):
        parser.error('Exact owned metadata/hash, author, baseline revision and bounded2/4-player arguments required')
    if not all(0<=value<limit for value,limit in zip(args.lock_point,[1024,768])):parser.error('Measured Crew lock point must remain inside1024x768')
    if any(not all(0<=value<limit for value,limit in zip(point,[1024,768])) for point in [args.member_point]+([args.remove_point] if args.remove_point else [])):
        parser.error('Measured Crew points must remain inside1024x768')
    if args.stage=='remove' and not args.remove_point:parser.error('Removal requires an explicitly measured --remove-point')
    if args.stage=='replace' and not(args.search and args.card):parser.error('Replacement requires an explicit alternate --search and --card')
    if any(value is not None and (not 1<=len(value)<=96 or any(char in value for char in '\r\n"')) for value in [args.search,args.card]):
        parser.error('Crew picker text must be bounded, nonempty and one line without quotes')
    if args.verify_history and args.stage not in ('capacity','add','replace','remove','lock'):
        parser.error('Combined history verification requires one native Crew action')
    run(args)


if __name__=='__main__':main()
