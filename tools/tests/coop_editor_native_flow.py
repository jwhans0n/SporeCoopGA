#!/usr/bin/env python3
"""Short opt-in automatic editor/native toolbar checks on an owned private cohort.

This harness never launches processes, deploys DLLs or rewrites native save files.
Mutations use ordinary game input and public party controls in isolated profiles.
Ownership verifies the supported executable, frozen DLL, isolated profiles and
hidden/muted windows. Run empty, toolbar, then handoff with the same fixture tag;
handoff reopens the two native local copies created by toolbar. A preparation
invocation performs no game input. This is not overnight release acceptance.
"""
import argparse
import json
from pathlib import Path
import re
import sys
import time

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from coop_editor_party import Ownership,PublicGame
from coop_adventure_editor import EditorPair,status
from coop_editor_save import edit_browser,select_exact_card
from coop_editor_lifecycle import inspected
from adventure_interaction_fixture import _verify_game
from windows_lab import json_write
from sporectl import quote

def settle(game,*,fresh=False,seconds=60):
 deadline=time.monotonic()+seconds;seen=False;stable=None;attempts=0;next_click=0
 while time.monotonic()<deadline:
  warning=game.caption("If you don't register")
  login=game.caption('Login')
  if warning or login:
   seen=True;stable=None
   if time.monotonic()>=next_click:
    if warning:game.press('OK')
    else:game.modal_click(868,159)
    attempts+=1;next_click=time.monotonic()+.75
    if attempts>=16:raise RuntimeError('Native registration did not close through normal input')
  elif fresh and not seen:
   # Fresh Galaxy precedes its first-use dialog. Observe that actual dialog;
   # do not mistake a usable underlying menu for completed dismissal.
   if game.caption('letterbox_top'):game.call('key esc')
  else:
   stable=stable or time.monotonic()
   if time.monotonic()-stable>=2:return dict(observed=seen,close_attempts=attempts)
  time.sleep(.2)
 raise RuntimeError('Native registration settling timed out before authoring')

def run_empty(args):
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sha=args.dll_sha256;tag=args.fixture_tag
 owner=Ownership(args.metadata,args.instances,sha)
 games=[PublicGame(i,out,90,time.monotonic()+600) for i in args.instances]
 host,guest=games[:2]
 deployment={'dll_sha256':sha}
 active=games[:2]
 result={'complete_acceptance':False,'checks':[],'sha256':deployment['dll_sha256']}
 def record(label):
  owner.verify_windows()
  for g in games:g.record(label)
  result['checks'].append({'label':label,'status':[status(g) for g in games]})
  json_write(out/'result.json',result)
 try:
  for game in games:
   _verify_game(game,owner.records[game.instance]);game.dismiss_registration();settle(game)
   game.call('coop.uistate 0')
  if host.caption('Welcome to Spore Galactic Adventures'):
   host.click(539,501)
  host.wait('native menu Create',lambda:host.caption('Create'))
  record('galaxy')
  host.press('Create');host.wait('native Create radial',lambda:host.control(0x07A7B140))
  host.click(553,522);host.wait('native adventure option',lambda:host.control(0x07A7F798))
  host.click(820,350);host.wait_caption('Choose a Planet');host.choose_card('Alpine Valley')
  host.wait('never-saved native editor',lambda:status(host).get('ready'))
  record('unsaved-editor-before-party')
  host.call(f'coop.host {args.port} NativeFlowHost');guest.call(f'coop.join 127.0.0.1:{args.port} NativeFlowGuest')
  def converged():
   states=[status(g) for g in games]
   if any(s.get('frozen') for s in states):raise RuntimeError('Native shared editor froze: '+repr(states))
   if not all(s.get('phase')==4 for s in states):return False
   values=[g.call('coop.editor check').strip() for g in games]
   return values[0].startswith('CONVERGED ') and values[0]==values[1]
  host.wait('automatic unsaved editor join',converged)
  for g in games:g.call('wait.frames 60 15000')
  time.sleep(.7)
  for g in games:
   text=g.call('coop.editor status')
   if 'saved_expected=0' not in text:raise RuntimeError('Unsaved snapshot was claimed as a native save: '+text)
  record('automatic-unsaved-convergence')
  for g in games:
   g.call('coop.uistate 1');g.wait('Adventure Editor lobby label',lambda g=g:'Adventure Editor' in g.call('coop.uistate'))
  record('ordinary-lobby-editor-label')
  for g in games:g.call('coop.uistate 0')
  host.click_control(0x90439D7C)
  deadline=time.monotonic()+10
  while time.monotonic()<deadline and not host.menu():
   if host.caption('Quit'):host.press('Quit');break
   time.sleep(.2)
  host.wait('host native editor exit',host.menu);guest.wait('guest follows native exit',guest.menu)
  host.wait('shared editor detached after native exit',lambda:all(status(g).get('phase')==0 for g in games))
  record('native-host-exit-party-preserved')
  for g in games:
   g.call('coop.uistate 1')
   text=g.call('coop.uistate')
   if 'NativeFlowHost' not in text or 'NativeFlowGuest' not in text:raise RuntimeError('Host editor exit did not preserve the party')
   g.call('coop.uistate 0')
  result['passed']=True;json_write(out/'result.json',result)
  print('automatic empty draft passed',flush=True)
 except Exception as error:
  result['passed']=False;result['error']=repr(error)
  try:record('failure')
  except Exception as secondary:result['record_error']=repr(secondary)
  json_write(out/'result.json',result);raise
 finally:
  for g in games:g.save_log();g.console.close() if hasattr(g.console,'close') else None
  owner.close()

def run_toolbar(args):
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sha=args.dll_sha256;tag=args.fixture_tag
 owner=Ownership(args.metadata,args.instances,sha)
 games=[PublicGame(i,out,90,time.monotonic()+1500) for i in args.instances]
 host,guest=games[:2]
 deployment={'dll_sha256':sha}
 active=games[:2]
 result={'complete_acceptance':False,'sha256':sha,'checks':[]};active=games[:2]
 def record(label):
  owner.verify_windows()
  for g in games:g.record(label)
  result['checks'].append({'label':label,'states':[status(g) for g in games]})
  json_write(out/'result.json',result)
 def converge():
  states=[status(g) for g in active]
  if any(s.get('frozen') for s in states):raise RuntimeError('Editor froze: '+repr(states))
  if not all(s.get('phase')==4 and not s.get('gesture') for s in states):return False
  values=[g.call('coop.editor check').strip() for g in active]
  return values[0].startswith('CONVERGED ') and len(set(values))==1
 def save_from_toolbar(g,label):
  g.call('coop.uistate 0');g.click_control(0x06FCB638)
  g.wait('normal native save confirmation',lambda:' saved.' in g.call('coop.ui tree 7'))
  g.record(label+'-confirmation');g.modal_click(772,470)
 def prompt_button(g,number):
  def button():
   text=g.call('coop.uistate')
   m=re.search(r'^editor_prompt_button'+str(number)+r' (\d+) (\d+) (\d+) (\d+) 1 ',text,re.M)
   return tuple(map(int,m.groups())) if m else None
  r=g.wait('compact native confirmation control',button);g.click((r[0]+r[2])//2,(r[1]+r[3])//2)
 try:
  for g in games:
   g.call('coop.leave');g.call('coop.uistate 0')
   if g.menu() and g.control(0x062D88E9):g.click(860,302)
   g.dismiss_registration();settle(g)
  # The native game gates its creator radial on first-use captain onboarding.
  if args.onboard_guest:guest.onboard('NativeToolbar74');guest.exit_adventure()
  for g in [host,guest]:
   next_close=[0.]
   def main_menu(g=g):
    if g.caption('Create'):return True
    if g.caption('Welcome to Spore Galactic Adventures') and time.monotonic()>next_close[0]:
     g.click(539,501);next_close[0]=time.monotonic()+1
    return False
   g.wait('native main menu',main_menu)
  host.press('Create');host.wait('Create radial',lambda:host.control(0x07A7B140));host.click(553,522)
  host.wait('adventure creator',lambda:host.control(0x07A7F798));host.click(820,350)
  host.wait_caption('Choose a Planet');host.choose_card('Alpine Valley');host.wait('native editor',lambda:status(host).get('ready'))
  host.call('wait.frames 60 15000');host.click(88,165);host.call('wait.frames 30 15000');host.choose_card('Ace')
  host.call('wait.frames 30 15000');host.call('drag 88 170 601 470');host.call('wait.frames 35 15000')
  host.call('drag 967 655 700 470');host.call('wait.frames 35 15000')
  title='Native Toolbar '+tag
  host.click(515,751);host.call('wait.frames 20 15000');host.call('text '+quote(title));host.click(669,748);host.call('wait.frames 20 15000')
  record('unsaved-authored-setup')
  host.call(f'coop.host {args.port} NativeFlowHost');guest.call(f'coop.join 127.0.0.1:{args.port} NativeFlowGuest')
  host.wait('automatic authored draft join',converge)
  if any('saved_expected=0' not in g.call('coop.editor status') for g in active):raise RuntimeError('Sharing implicitly saved the draft')
  record('unsaved-authored-convergence')
  pair=object.__new__(EditorPair);pair.host=host;pair.guest=guest;pair.games=active
  pair.output=out;pair.result=result;pair.records=owner.records;pair.verify=lambda game:owner.verify_windows()
  pair.record=record;pair.write=lambda:json_write(out/'result.json',result)
  visible=[key for key,row in pair.objects(host).items() if row['screen'] and 340<row['screen'][0]<900 and 160<row['screen'][1]<680]
  if not visible:raise RuntimeError('No authored placement is visible for native history check')
  object_id=visible[0]
  def move():
   source=pair.body(host,object_id);EditorPair.slow_drag(host,source,(source[0]-35,source[1]))
  pair.gesture(host,'native-move',move)
  pair.gesture(host,'native-toolbar-undo',lambda:host.click(655,751))
  pair.gesture(host,'native-keyboard-redo',lambda:EditorPair.hotkey(host,'y'))
  save_from_toolbar(host,'host-save')
  host.wait('native save acknowledged on both editors',lambda:'saved_applied=2 saved_expected=2' in host.call('coop.editor status'))
  before=[g.call('coop.editor check').strip() for g in active]
  saved_root=re.search(r'saved_source_root=(\S+)',host.call('coop.editor status'))[1]
  record('native-host-save-acknowledged')
  save_from_toolbar(guest,'guest-copy')
  host.wait('local export released on both editors',lambda:all('exporting=0' in g.call('coop.editor status') for g in active))
  if [g.call('coop.editor check').strip() for g in active]!=before:raise RuntimeError('Guest copy changed canonical state')
  if re.search(r'saved_source_root=(\S+)',host.call('coop.editor status'))[1]!=saved_root:raise RuntimeError('Guest copy changed host save coverage')
  record('native-guest-copy-isolated')
  for g in games[2:]:g.call(f'coop.join 127.0.0.1:{args.port} NativeFlowGuest'+str(g.instance))
  active=games
  host.wait('four editors join automatically',converge)
  record('four-player-automatic-convergence')
  guest.click_control(0x07104131)
  host.wait('compact host approval request',lambda:'editor_prompt=2' in host.call('coop.uistate'))
  record('native-play-request')
  prompt_button(host,0)
  host.wait('all four explicit readiness prompts',lambda:all('editor_prompt=3' in g.call('coop.uistate') for g in games))
  if any(not re.search(r"^editor_prompt_button0 .* 1 'Ready'$",g.call('coop.uistate'),re.M) for g in games):raise RuntimeError('Unexpected implicit confirmation')
  record('four-player-unready-confirmation')
  for g in games[1:]:prompt_button(g,0)
  time.sleep(.5)
  if any(status(g).get('playtesting') for g in games):raise RuntimeError('Playtest launched before host confirmation')
  prompt_button(host,0)
  host.wait('four native playtests',lambda:all(status(g).get('playtesting') for g in games))
  record('four-player-native-playtest')
  host.click_control(0x07104130)
  host.wait('four native editors return',converge)
  record('four-player-native-build-return')
  host.click_control(0x90439D7C)
  deadline=time.monotonic()+8
  while time.monotonic()<deadline and not host.menu():
   if host.caption('Quit'):host.press('Quit');break
   time.sleep(.2)
  host.wait('all native editors exit',lambda:all(g.menu() and status(g).get('phase')==0 for g in games))
  record('four-player-native-exit')
  for g in games:g.call('coop.leave');g.call('coop.uistate 0')
  for g in [host,guest]:
   edit_browser(g);select_exact_card(g,title);g.wait('saved local copy reopens',lambda g=g:status(g).get('ready'))
   if status(g).get('frozen'):raise RuntimeError('Saved copy could not reopen')
   result.setdefault('reopened',[]).append({'instance':g.instance,'inspect':inspected(g.call('coop.editor inspect'))})
  record('host-and-guest-local-copies-reopened')
  result['passed']=True;json_write(out/'result.json',result);print('native toolbar flow passed',flush=True)
 except Exception as error:
  result['passed']=False;result['error']=repr(error)
  try:record('failure')
  except Exception as secondary:result['record_error']=repr(secondary)
  json_write(out/'result.json',result);raise
 finally:
  for g in games:g.save_log()
  owner.close()

def run_handoff(args):
 out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
 sha=args.dll_sha256;tag=args.fixture_tag
 owner=Ownership(args.metadata,args.instances,sha)
 games=[PublicGame(i,out,90,time.monotonic()+700) for i in args.instances]
 host,guest=games[:2]
 deployment={'dll_sha256':sha}
 active=games[:2]
 result={'complete_acceptance':False,'sha256':sha,'checks':[]}
 def record(label):
  owner.verify_windows()
  for g in games:g.record(label)
  result['checks'].append({'label':label,'status':[status(g) for g in games]});json_write(out/'result.json',result)
 def button(g,index):
  g.call('wait.frames 15 15000');time.sleep(.25)
  def ready():return re.search(r'^editor_prompt_button'+str(index)+r' (\d+) (\d+) (\d+) (\d+) 1 ',g.call('coop.uistate'),re.M)
  m=g.wait('current compact prompt button',ready)
  r=tuple(map(int,m.groups()));g.click((r[0]+r[2])//2,(r[1]+r[3])//2)
 def join_prompt():
  guest.call(f'coop.join 127.0.0.1:{args.port} ProtectedGuest');guest.wait('unrelated draft choice',lambda:'editor_prompt=1' in guest.call('coop.uistate'))
 def converge():
  if any(status(g).get('frozen') for g in games):raise RuntimeError('Shared editor froze')
  return all(status(g).get('phase')==4 for g in games) and host.call('coop.editor check').strip()==guest.call('coop.editor check').strip()
 def native_cancel(g):
  g.wait_caption('Quit');g.call('wait.frames 20 15000');time.sleep(.6)
  g.click_control(0x05107B17)
  g.wait('native discard dialog closes',lambda:not g.caption('Quit'))
 def native_quit(g):
  g.wait_caption('Quit');g.call('wait.frames 20 15000');time.sleep(.6);g.click_control(0x05107B1A)
 def fresh(g,title):
  if g.control(0x062D88E9):g.click(860,302)
  next_close=[0.]
  def main_menu():
   if g.caption('Create'):return True
   if time.monotonic()>next_close[0]:
    if g.control(0x062D88E9):g.click_control(0x062D88E9)
    elif g.caption('Welcome to Spore Galactic Adventures'):g.click(539,501)
    next_close[0]=time.monotonic()+1
   return False
  g.wait('stable native main menu',main_menu);g.press('Create');g.wait('Create radial',lambda:g.control(0x07A7B140));g.click(553,522)
  g.wait('adventure creator',lambda:g.control(0x07A7F798));g.click(820,350);g.wait_caption('Choose a Planet');g.choose_card('Alpine Valley')
  g.wait('unrelated unsaved native editor',lambda:status(g).get('ready'));g.call('wait.frames 35 15000')
  g.click(88,165);g.call('wait.frames 30 15000');g.choose_card('Ace');g.call('drag 88 170 601 470');g.call('wait.frames 30 15000');g.call('drag 967 655 700 470');g.call('wait.frames 30 15000')
  g.click(515,751);g.call('wait.frames 20 15000');g.call('text '+quote(title));g.click(669,748);g.call('wait.frames 20 15000')
 try:
  for g in games:
   g.call('coop.leave');g.call('coop.uistate 0')
   if status(g).get('ready'):
    g.click_control(0x90439D7C)
    if g.caption('Quit'):g.press('Quit')
   g.wait('native local editor exit',g.menu)
   if g==host:edit_browser(g);select_exact_card(g,'Native Toolbar '+tag);g.wait('saved host file opens',lambda g=g:status(g).get('ready'))
  fresh(guest,'Discarded local '+tag)
  before=inspected(guest.call('coop.editor inspect'))
  host.call(f'coop.host {args.port} ProtectedHost');host.wait('automatic sharing of existing creation',lambda:status(host).get('phase')==4)
  source=inspected(host.call('coop.editor inspect'))['source']
  host.click_control(0x06FCB638);host.wait('unchanged existing creation saves a new copy',lambda:' saved.' in host.call('coop.ui tree 7'));host.modal_click(772,470)
  host.wait('local host copy acknowledged',lambda:'saved_applied=1 saved_expected=1' in host.call('coop.editor status'))
  if inspected(host.call('coop.editor inspect'))['source']==source:raise RuntimeError('First host Save reused the source creation identity')
  record('existing-source-first-save-is-copy')
  join_prompt();record('unrelated-draft-choice')
  button(guest,2)
  guest.wait('Cancel leaves party without replacing local draft',lambda:guest.call('coop.status').startswith('offline'))
  if inspected(guest.call('coop.editor inspect'))!=before:raise RuntimeError('Cancel changed the unrelated native draft')
  record('join-cancel-preserves-draft')
  join_prompt();button(guest,1);native_cancel(guest)
  guest.wait('native discard cancellation restores choice',lambda:'editor_prompt=1' in guest.call('coop.uistate'))
  if inspected(guest.call('coop.editor inspect'))!=before:raise RuntimeError('Native Cancel changed the unrelated draft')
  record('native-discard-cancel-preserves-draft')
  button(guest,1);native_quit(guest);host.wait('discard follows shared editor',converge)
  record('discard-automatic-follow')
  pair=object.__new__(EditorPair);pair.host=host;pair.guest=guest;pair.games=games;pair.output=out;pair.result=result;pair.records=owner.records
  pair.verify=lambda game:owner.verify_windows();pair.record=record;pair.write=lambda:json_write(out/'result.json',result)
  visible=[key for key,row in pair.objects(host).items() if row['screen'] and 340<row['screen'][0]<900 and 160<row['screen'][1]<680]
  if not visible:raise RuntimeError('No visible shared placement for dirty exit confirmation')
  def move():
   point=pair.body(host,visible[0]);EditorPair.slow_drag(host,point,(point[0]-35,point[1]))
  pair.gesture(host,'shared-move-before-exit',move)
  guest.click_control(0x90439D7C);native_cancel(guest);host.wait('guest exit Cancel keeps shared draft',converge)
  record('guest-native-exit-cancel')
  guest.click_control(0xBBBB1001)
  guest.wait('guest Save+Exit native copy confirmation',lambda:' saved.' in guest.call('coop.ui tree 7'));guest.modal_click(772,470)
  guest.wait('guest Save+Exit leaves party',lambda:guest.menu() and guest.call('coop.status').startswith('offline'))
  record('guest-save-exit-local-copy')
  # A second unrelated, never-saved draft exercises Save before switching.
  title='Protected draft '+tag;fresh(guest,title)
  protected=inspected(guest.call('coop.editor inspect'))
  join_prompt();button(guest,0)
  guest.wait('protect local draft native save confirmation',lambda:' saved.' in guest.call('coop.ui tree 7'));guest.modal_click(772,470)
  host.wait('saved local draft then automatic follow',converge);record('save-before-automatic-follow')
  host.click_control(0x90439D7C);native_cancel(host);host.wait('host exit cancellation',converge);record('host-native-exit-cancel')
  host.click_control(0xBBBB1001);host.wait('host Save+Exit confirmation',lambda:' saved.' in host.call('coop.ui tree 7'));host.modal_click(772,470)
  host.wait('host Save+Exit returns connected party',lambda:all(g.menu() and status(g).get('phase')==0 for g in games));record('host-save-exit-party-preserved')
  for g in games:g.call('coop.leave')
  edit_browser(guest);select_exact_card(guest,title);guest.wait('protected local save reopens',lambda:status(guest).get('ready'))
  after=inspected(guest.call('coop.editor inspect'))
  if after['semantic_sha256']!=protected['semantic_sha256']:raise RuntimeError('Protected local save lost its authored state')
  record('protected-local-copy-reopened');result['passed']=True;json_write(out/'result.json',result);print('native handoff passed',flush=True)
 except Exception as error:
  result['error']=repr(error);result['passed']=False
  try:record('failure')
  except Exception as secondary:result['record_error']=repr(secondary)
  json_write(out/'result.json',result);raise
 finally:
  for g in games:g.save_log()
  owner.close()

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--execute',action='store_true')
 parser.add_argument('--stage',choices=('empty','toolbar','handoff'),default='empty')
 parser.add_argument('--metadata',type=Path)
 parser.add_argument('--dll-sha256')
 parser.add_argument('--output',type=Path)
 parser.add_argument('--instances',type=int,nargs='+',default=[73,74])
 parser.add_argument('--port',type=int,default=47906)
 parser.add_argument('--fixture-tag',default='native-flow')
 parser.add_argument('--onboard-guest',action='store_true',help='Only toolbar: complete native first-use captain creation')
 args=parser.parse_args()
 count=4 if args.stage=='toolbar' else 2
 if not args.execute:
  print(json.dumps(dict(prepared=True,stage=args.stage,required_instances=count,launches_games=False)));return
 if len(args.instances)!=count or len(set(args.instances))!=count:
  parser.error('This stage requires '+str(count)+' distinct owned instances')
 if not args.metadata or not args.output or not re.fullmatch('[0-9a-f]{64}',args.dll_sha256 or ''):
  parser.error('--execute requires metadata, output and exact lowercase DLL SHA-256')
 if not 1024<=args.port<=65535 or not re.fullmatch('[a-zA-Z0-9_-]{1,40}',args.fixture_tag):
  parser.error('Invalid isolated port or fixture tag')
 globals()['run_'+args.stage](args)

if __name__=='__main__':main()
