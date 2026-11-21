"""Offline Crew history contracts; no process, console, or native input."""
import contextlib
import io
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/tests'))
import coop_editor_crew_probe as crew


class Party:
    def __init__(self,stage='capacity',author=95,outcomes=None,duplicate_at=None):
        self.stage=stage;self.author=author;self.state='S0';self.revision=10
        self.forward_count=0;self.keys=[];self.outcomes=outcomes or {};self.duplicate_at=duplicate_at
        self.games=[];self.closed=False;self.clicks=[]

    def forward(self):
        self.forward_count+=1;self.state='S1';self.revision+=1

    def history(self):
        rows=['author=0:2 sequence=1 epoch=8 revision=1 applied=1 redo=0 Foreign edit']
        if self.forward_count:
            applied=not self.keys or self.keys[-1]=='y'
            rows.append(f'author={self.author-95}:2 sequence=2 epoch=8 revision=11 applied={int(applied)} redo={int(not applied)} Edit adventure crew')
        if self.duplicate_at==len(self.keys) and self.keys:rows.append(rows[-1])
        return 'local=0:2 count='+str(len(rows))+'\n'+'\n'.join(rows)+'\n'

    def history_step(self,key):
        self.keys.append(key);self.revision+=1
        self.state=self.outcomes.get(len(self.keys),'S0' if key=='z' else 'S1')

    def game(self,instance,*unused):
        game=Game(self,instance);self.games.append(game);return game


class Game:
    def __init__(self,party,instance):self.party=party;self.instance=instance
    def wait(self,description,predicate):
        if not predicate():raise crew.FixtureError('Offline party failed to settle: '+description)
    def call(self,line,**unused):
        if line=='coop.editor history':return self.party.history()
        if line=='coop.editor check':return 'CONVERGED '+self.party.state
        if line=='coop.editor inspect':return 'owned semantic '+self.party.state
        if line=='exc':return 'total faults: 0'
        if line=='coop.ui where 064A90A0':return '064A90A0 974 451 1005 467 enabled=1\n'
        if line=='coop.ui at 998 489':
            if self.party.stage=='add':return '03D982FF 980 472 1024 518 enabled=1\n03D98298 980 472 1024 518 enabled=1\n'
            return '03D98304 962 459 1044 521 enabled=1\n'
        if line=='coop.ui at 1014 466':return '03D98304 962 459 1044 521 enabled=1\n04ACFCAF 1007 459 1024 476 enabled=1\n'
        if line=='coop.ui at 977 466':return '03D98304 962 459 1044 521 enabled=1\n07C4B4A0 968 457 987 476 enabled=1\n'
        if line=='coop.ui at 500 40':return "B44EC3DB 0 0 1024 80 enabled=1 - 'Choose one or more Creatures'\n"
        if line=='coop.ui at 916 115':return "D4935A8F 836 106 991 127 enabled=1 - ''\n"
        if line.startswith(('coop.uistate ','move ','wait.frames ','text ','coop.ui findall ')):return 'offline fixture'
        raise AssertionError('Unexpected console command: '+line)
    def click_control(self,ident):
        self.party.clicks.append((self.instance,ident))
        if ident==0x0774B060:self.party.forward()
        elif ident in (0xB006EF6E,0xF006EFA5):self.party.history_step('z' if ident==0xB006EF6E else 'y')
        else:raise AssertionError('Unexpected control: '+hex(ident))
    def click(self,x,y):
        if self.party.stage=='remove' and (x,y)==(1014,466):self.party.forward()
        elif self.party.stage=='lock' and (x,y)==(977,466):self.party.forward()
    def choose_card(self,unused):self.party.forward()
    def shot(self,unused):pass
    def save_log(self):pass


class CrewHistoryContracts(unittest.TestCase):
    def run_party(self,party,restore=False,verify=True,stage=None):
        with tempfile.TemporaryDirectory() as temporary:
            output=Path(temporary)/'result'
            args=SimpleNamespace(output=output,metadata=Path('owned-processes.json'),instances=[95,96,97,98],
                dll_sha256='a'*64,deadline=900,timeout=90,author=party.author,stage=stage or party.stage,
                revision=10,history_input='toolbar',history_focus=None,restore_after_history=restore,
                verify_history=verify,member_point=[998,489],slot=[998,489],remove_point=[1014,466],
                lock_point=[977,466],search='Ms Spoffit',card='Ms Spoffit')
            owner=SimpleNamespace(verify_windows=lambda:None,close=lambda:setattr(party,'closed',True))
            def status(game):
                return dict(frozen=False,phase=4,ready=True,gesture=False,revision=party.revision,durable=party.revision,text=f'{game.instance} same context\nnormal')
            with mock.patch.object(crew,'Ownership',return_value=owner),mock.patch.object(crew,'PublicGame',side_effect=party.game), \
                 mock.patch.object(crew,'verify_viewport',return_value=[]),mock.patch.object(crew,'status',side_effect=status), \
                 mock.patch.object(crew,'parse_context',return_value=('project',8)), \
                 mock.patch.object(crew,'parse_identity',side_effect=lambda text:(None,(int(text.split()[0])-95,2))), \
                 mock.patch.object(crew,'semantic',side_effect=lambda unused:party.state),contextlib.redirect_stdout(io.StringIO()):
                try:crew.run(args)
                finally:
                    self.assertTrue(party.closed)
                    self.result=json.loads((output/'result.json').read_text())
            return self.result

    def test_all_single_action_stages_restore_exact_before_and_after(self):
        for author in (95,96,97,98):
          for stage in ('capacity','add','replace','remove','lock'):
            with self.subTest(stage=stage,author=author):
                party=Party(stage,author);result=self.run_party(party)
                self.assertTrue(result['passed']);self.assertEqual(party.keys,['z','y'])
                self.assertEqual(result['history_semantics'],dict(before='S0',after='S1',final='S1'))
                self.assertEqual(result['final_revision'],13);self.assertEqual(party.forward_count,1)
                self.assertEqual(len(crew.checked_history(party.history())),2)

    def test_restore_finishes_each_stage_at_exact_original_state(self):
        for author in (95,96,97,98):
          for stage in ('capacity','add','replace','remove','lock'):
            with self.subTest(stage=stage,author=author):
                party=Party(stage,author);result=self.run_party(party,restore=True)
                self.assertTrue(result['passed']);self.assertEqual(party.keys,['z','y','z'])
                self.assertEqual(result['history_semantics'],dict(before='S0',after='S1',final='S0'))
                self.assertEqual(result['final_revision'],14);self.assertEqual(party.forward_count,1)
                self.assertEqual(len(crew.checked_history(party.history())),2)
                self.assertIn('history-restore-z',[row['label']for row in result['checks']])
                self.assertEqual(party.clicks[-1],(author,0xB006EF6E))

    def test_wrong_undo_that_differs_from_result_is_rejected(self):
        party=Party(outcomes={1:'unrelated-corruption'})
        with self.assertRaisesRegex(crew.FixtureError,'exact pre-action'):
            self.run_party(party)
        self.assertEqual(party.keys,['z']);self.assertFalse(self.result['passed'])

    def test_wrong_redo_is_rejected_before_final_undo(self):
        party=Party(outcomes={2:'unrelated-corruption'})
        with self.assertRaisesRegex(crew.FixtureError,'Redo lost'):
            self.run_party(party,restore=True)
        self.assertEqual(party.keys,['z','y']);self.assertFalse(self.result['passed'])

    def test_wrong_final_undo_cannot_claim_restored_baseline(self):
        party=Party(outcomes={3:'different-from-result-but-not-S0'})
        with self.assertRaisesRegex(crew.FixtureError,'exact pre-action'):
            self.run_party(party,restore=True)
        self.assertEqual(party.keys,['z','y','z']);self.assertFalse(self.result['passed'])

    def test_final_undo_must_preserve_existing_history_entry_count(self):
        party=Party(duplicate_at=3)
        with self.assertRaisesRegex(crew.FixtureError,'duplicate history'):
            self.run_party(party,restore=True)
        self.assertFalse(self.result['passed'])

    def test_standalone_history_keeps_its_unknown_before_state_qualification(self):
        party=Party(outcomes={1:'prior-unknown'});party.forward_count=1;party.state='S1'
        result=self.run_party(party,verify=False,stage='history')
        self.assertTrue(result['passed']);self.assertIsNone(result['history_semantics']['before'])
        self.assertEqual(result['history_semantics']['final'],'S1')

    def test_restore_option_requires_single_action_verify_history_before_any_io(self):
        cases=[['--restore-after-history'],['--restore-after-history','--stage','history'],
               ['--restore-after-history','--verify-history','--stage','history'],
               ['--restore-after-history','--verify-history','--stage','capacity-add']]
        for arguments in cases:
            with self.subTest(arguments=arguments),mock.patch.object(sys,'argv',['crew-probe']+arguments), \
                 mock.patch.object(crew,'Ownership') as owner,contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as stopped:crew.main()
                self.assertEqual(stopped.exception.code,2);owner.assert_not_called()


if __name__=='__main__':unittest.main()
