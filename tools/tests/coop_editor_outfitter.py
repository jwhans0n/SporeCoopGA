"""Native Create Creature -> Outfitter -> independent return regression.

Inert without --execute. Requires an owned hidden/muted Windows pair already
sharing a private adventure. Uses normal native input and bounded read-only
validation, never process writes, native calls or personal profiles.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path[:0]=[str(ROOT/'tools/tests'),str(ROOT/'tools')]
from coop_adventure import FixtureError
from coop_adventure_editor import status
from coop_editor_party import Ownership,parse_context
from coop_editor_creations import CreationGame,input_steps
from coop_editor_creation_save import (asset_snapshot,canonical_row,history_rows,
    request_errors,steps_file,saved_asset_result,edit_save_new_modal,cancel_quit_modal)

CONTINUE,DONE,CANCEL=0x0615FF74,0x058A3350,0x90439D7C


def outfit_errors(row,root):
    errors=[]
    if row.get('chain_root')!=root or type(row.get('request'))is not int or not 0<row['request']<=0xFFFFFFFF or row['request']==root:errors.append('owned request chain changed')
    if row.get('request_editor_id')!=0xA56567F7 or row.get('model_type')!=0x4178B8E8:errors.append('wrong Outfitter request/model')
    key=row.get('source_key',[])
    if not isinstance(key,list)or len(key)!=3 or not key[0]or key[1]!=0x2B978C46 or not key[2]:errors.append('missing native intermediate creature save')
    if row.get('return_mode')!=0x01654C10 or row.get('new_asset')is not False:errors.append('native return/source mode changed')
    if row.get('save_phase_38C')not in(0,6)or row.get('ui_pending_command_cc')!=0:errors.append('native command already pending')
    return errors


def run(args):
    args.output.mkdir(parents=True,exist_ok=False)
    owner=Ownership(args.metadata,args.instances,args.candidate)
    meta=json.loads(args.metadata.read_text(encoding='utf-8-sig'))
    pins={row['instance']:row['pid']for row in meta['processes']}
    games=[CreationGame(i,args.output,60,time.monotonic()+300,[1024,768])for i in args.instances]
    host=games[0];author=next(g for g in games if g.instance==args.author)
    result=dict(passed=False,candidate=args.candidate,author=args.author,cancel=args.cancel,processes=pins)
    def write():
        (args.output/'result.json').write_text(json.dumps(result,indent=2)+'\n')
        for g in games:g.save_log()
    def canonical():
        states=[status(g)for g in games]
        if any(s['frozen']for s in states):raise FixtureError('Native return entered recovery')
        if any(parse_context(s['text'])!=context for s in states):raise FixtureError('Shared project/epoch changed')
        rows=[canonical_row(g.asset_text())for g in games]
        return rows[0]if all(row==rows[0]and row['revision']==row['durable']for row in rows)else None
    def check():
        if not all(status(g)['ready']for g in games):return None
        rows=[g.call('coop.editor check').strip()for g in games]
        return rows if rows[0].startswith('CONVERGED ')and len(set(rows))==1 else None
    def sample(label,profile,root=None):
        owner.verify_windows()
        path=args.output/(label+'-native.json')
        cmd=[sys.executable,str(ROOT/'tools/tests/coop_editor_creation_validation.py'),'--execute',
            '--metadata',str(args.metadata),'--candidate',args.candidate,'--participant',
            f'{args.author}:{pins[args.author]}:{profile}','--output',str(path),'--exit-confirmation']
        if root:cmd+=['--chain-root',str(root)]
        done=subprocess.run(cmd,cwd=ROOT,capture_output=True,text=True,timeout=20)
        (args.output/(label+'-reader.txt')).write_text(done.stdout+done.stderr)
        if done.returncode:raise FixtureError('Native chain reader rejected: '+done.stderr)
        row=json.loads(path.read_text())['rows'][0];result[label]=row;write();return row
    try:
        owner.verify_windows()
        context=parse_context(status(host)['text'])
        result['context']=context
        result['before_checks']=host.wait('initial native convergence',check)
        before=host.wait('durable canonical baseline',canonical);result['before']=before
        inventory=asset_snapshot(host);history=history_rows(host.call('coop.editor history'))
        result.update(before_assets=inventory,before_history=history);write()
        author.native_create('creature')
        author.wait('entered native creature',lambda:'phase=1'in author.call('coop.editor creations')and 'entered=1'in author.call('coop.editor creations'))
        entered=sample('body-entered','creature');root=entered['request']
        if request_errors(entered,'creature'):raise FixtureError('Not the requested new Creature editor')
        steps,digest=steps_file(ROOT/'tools/tests/fixtures/editor_creation/creature-held-c39.json','creature')
        result['recipe_sha256']=digest;write()
        input_steps(author,steps,args.name)
        body=sample('body-before-continue','creature')
        if body['request']!=root or request_errors(body,'creature')or body['finish_state_if_debug_disabled']!=0 or not any(w['visible']for w in body['widgets'].get('0615FF74',[])):
            raise FixtureError('Native creature is not ready for its exact Continue action')
        if canonical()!=before:raise FixtureError('Design changed shared draft before admission')
        author.click_control(CONTINUE)
        author.wait('visible native Outfitter Done',lambda:author.control(DONE))
        author.call('wait.frames 20 15000')
        outfit=sample('outfitter-entered','creature-outfitter',root)
        if outfit_errors(outfit,root):raise FixtureError(', '.join(outfit_errors(outfit,root)))
        if not args.cancel:
            # Rename through the same measured native name box as the body
            # recipe, with explicit key release even if input fails.
            author.click(500,741);author.call('wait.frames 4 15000');author.call('key ctrl down')
            try:author.call('key a');author.call('wait.frames 2 15000')
            finally:author.call('key ctrl up')
            input_steps(author,[{'text':args.name+'Outfit'},{'click':[850,600]},{'frames':20}],args.name)
        preflight=sample('outfitter-before-exit','creature-outfitter',root)
        if outfit_errors(preflight,root)or preflight['request']!=outfit['request']:raise FixtureError('Outfitter identity expired')
        if not args.cancel and(preflight['finish_state_if_debug_disabled']!=0 or not any(preflight['changed_4B3_4B4'])or preflight['model_type']not in {r['type']for r in preflight['allowed_types']}):
            raise FixtureError('Native Outfitter is not saveable')
        if canonical()!=before:raise FixtureError('Intermediate native save changed the draft/history')
        result['exit_dispatched']=True;write();author.click_control(CANCEL if args.cancel else DONE)
        confirmed=False
        def returned():
            nonlocal confirmed
            creation=author.call('coop.editor creations')
            if 'active=0'in creation:
                return status(author)['ready']
            if confirmed or 'phase=1'not in creation or not author.control(CANCEL):return False
            tree=author.call('coop.ui tree 7')
            modal=(cancel_quit_modal if args.cancel else edit_save_new_modal)(tree)
            if modal:
                # The exact native dialog parser checks identity, text, enabled
                # state and viewport. Recheck the owned native request first.
                row=sample('exit-confirmation','creature-outfitter',root)
                if row['request']!=outfit['request']or row['ui_pending_command_cc']!=(0x103 if args.cancel else 0x102):raise FixtureError('Pending exit identity changed')
                fresh=(cancel_quit_modal if args.cancel else edit_save_new_modal)(author.call('coop.ui tree 7'))
                if fresh!=modal:raise FixtureError('Native confirmation changed')
                result['confirmation']=modal;confirmed=True;write();author.click(*modal['point'])
            return False
        author.wait('owned Outfitter returns without recovery',returned,timeout=60)
        target=before['revision']+(0 if args.cancel else 1)
        host.wait('exact durable admission',lambda:(row:=canonical())and row['revision']==target)
        result['after_checks']=host.wait('actual observer scene convergence',check)
        result['after_assets']=asset_snapshot(host);result['after_history']=history_rows(host.call('coop.editor history'))
        if args.cancel:
            if result['after_assets']!=inventory or result['after_history']!=history:raise FixtureError('Cancellation changed cast/history')
        else:
            result.update(saved_asset_result(inventory,result['after_assets'],'creature'))
            if len(result['after_history'])!=len(history)+1 or result['after_history'][:-1]!=history or result['after_history'][-1][-1]!='Create adventure asset':raise FixtureError('Expected exactly one creation Undo item')
        result['after_creations']=author.call('coop.editor creations')
        for g in games:
            if 'total faults: 0'not in g.call('exc'):raise FixtureError('Native fault occurred')
        result['passed']=True
    except Exception as error:
        result['error']=repr(error);raise
    finally:
        write();owner.close()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--execute',action='store_true');p.add_argument('--self-test',action='store_true')
    p.add_argument('--metadata',type=Path);p.add_argument('--candidate');p.add_argument('--instances',nargs=2,type=int,default=[45,46])
    p.add_argument('--author',type=int);p.add_argument('--output',type=Path);p.add_argument('--name',default='OutfitterRegression');p.add_argument('--cancel',action='store_true')
    a=p.parse_args()
    if a.self_test:
        good=dict(chain_root=1,request=2,request_editor_id=0xA56567F7,model_type=0x4178B8E8,source_key=[7,0x2B978C46,8],return_mode=0x01654C10,new_asset=False,save_phase_38C=0,ui_pending_command_cc=0)
        assert not outfit_errors(good,1)
        for key in good:
            row=dict(good);row[key]=None;assert outfit_errors(row,1),key
        print('Outfitter chain receipt guards passed; no game connections');return
    if not a.execute:print('Prepared only; explicit private metadata, candidate and author required.');return
    if not a.metadata or not a.output or a.author not in a.instances or not re.fullmatch('[0-9a-f]{64}',a.candidate or'')or not a.output.resolve().is_relative_to((ROOT/'out').resolve()):p.error('Exact owned private pair, candidate, author and new OUT output required')
    run(a)


if __name__=='__main__':main()
