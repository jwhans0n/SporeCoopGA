"""Pure summary checks for the native adventure outcome observation fixture."""
import unittest
from unittest.mock import patch

from coop_adventure_outcome import analyze, lifecycle_peer
from coop_adventure_results import choose_reward


def sample(wall, states, acts=(0, 0)):
    return dict(wall=wall, peers=[dict(goals=dict(state=state, act=act))
                                 for state, act in zip(states, acts)])


class OutcomeFixtureTests(unittest.TestCase):
    def test_allow_asynchronous_failure_delivery(self):
        result = analyze([sample(0, (6, 3)), sample(.1, (6, 6)), sample(1, (6, 6))])
        self.assertTrue(result['native_terminal_monotonic'])

    def test_reject_guest_escaping_failure(self):
        result = analyze([sample(0, (6, 6)), sample(1, (6, 3))])
        self.assertFalse(result['native_terminal_monotonic'])
        self.assertEqual(result['terminal_regressions'][0]['peer'], 1)

    def test_reject_chapter_progress_while_terminal(self):
        result = analyze([sample(0, (6, 6)), sample(1, (6, 6), (0, 1))])
        self.assertFalse(result['native_terminal_monotonic'])

    def test_require_both_native_results(self):
        self.assertFalse(analyze([sample(0, (6, 3))])['native_terminal_monotonic'])

    def test_ignore_prior_playing_state(self):
        result = analyze([sample(0, (3, 3)), sample(1, (6, 6))])
        self.assertTrue(result['native_terminal_monotonic'])

    def test_menu_lifecycle_never_reads_retired_scenario(self):
        class Game:
            instance = 84

            def call(self, command):
                return 'GalaxyGameEntry' if command == 'game.mode' else 'eax=0x00100000 (1048576)'

            def adventure(self):
                return dict(active=0)

        class Memory:
            def pointer(self, address):
                assert address == 0x100088
                return 0x02CCDA9C

        with patch('coop_adventure_outcome.read_peer', side_effect=AssertionError('retired pointer read')):
            result = lifecycle_peer(Game(), Memory())
        self.assertNotIn('native', result)
        self.assertEqual(result['application_state'], 0x02CCDA9C)

    def test_lifecycle_records_scenario_retiring_during_sample(self):
        class Game:
            instance = 84

            def call(self, command):
                return 'Scenario' if command == 'game.mode' else 'eax=0x00100000 (1048576)'

            def adventure(self):
                return dict(active=0)

        class Memory:
            def pointer(self, address):
                return 0x02CCDA9C

        with patch('coop_adventure_outcome.read_peer', side_effect=OSError('retired')):
            result = lifecycle_peer(Game(), Memory())
        self.assertEqual(result['native_retired_during_sample'], 'retired')

    def test_reward_wait_accepts_asynchronous_party_return_after_one_click(self):
        class Game:
            returned = False
            clicks = []

            def caption(self, caption):
                return (1, 1) if caption == 'Choose a Reward' else None

            def menu(self):
                return self.returned

            def control(self, ident):
                assert ident == 0x52DF67AF
                return (569, 209, 816, 520, 1)

            def click(self, x, y):
                self.clicks.append((x, y))

            def wait(self, label, predicate, timeout):
                self.returned = True
                assert predicate()

        game = Game()
        self.assertTrue(choose_reward(game))
        self.assertEqual(game.clicks, [(631, 235)])

    def test_reward_selection_does_not_click_an_existing_done_panel(self):
        class Game:
            def caption(self, caption):
                return (1, 1)

            def click(self, x, y):
                raise AssertionError('ordinary Done panel received a reward click')

        self.assertFalse(choose_reward(Game()))


if __name__ == '__main__':
    unittest.main()
