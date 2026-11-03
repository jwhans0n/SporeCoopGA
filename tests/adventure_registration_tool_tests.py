"""Offline native-startup UI contracts: no game, console, account or profile access."""
import itertools
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'tests'))
import coop_adventure as adventure


def window(depth, address, ident, rect, caption='', flags=3, command=0):
    return (f"{'  ' * depth}Window {address:08X} id=0x{ident:08X} cmd=0x{command:08X} "
            f"fl=0x{flags:X} [{' '.join(map(str, rect))}] drw=00000000 vt=00000000 font=00000001 '{caption}'\n")


ROOT_WINDOW = window(0, 0x1000, 0xFFFFFFF0, (0, 0, 1024, 768))
MODAL_LAYER = window(1, 0x1100, 0x05B598F7, (0, 0, 1024, 768), flags=0x13)
# IDs, ancestry and rectangles from retained toolbar-c4/76-failure-ui.txt.
REGISTRATION = (ROOT_WINDOW + MODAL_LAYER
    + window(2, 0x1200, 0xFFFFFFFF, (133, 146, 891, 621), flags=0x43)
    + window(3, 0x1300, 0x0451A4C0, (24, 70, 732, 401), flags=0x403)
    + window(3, 0x1400, 0xFFFFFFF2, (726, 4, 745, 23), command=3)
    + window(3, 0x1500, 0x0519E168, (652, 23, 731, 60), 'Login'))
# IDs/ancestry from C8 registration-current UI; C30 screenshot has this footer.
GALAXY = (ROOT_WINDOW
    + window(1, 0x2100, 0x2EDD95CA, (0, 0, 1024, 768), flags=0x13)
    + window(2, 0x2200, 0xFFFFFFFF, (0, 0, 1024, 768), flags=0x13)
    + window(3, 0x2300, 0x010F33E0, (56, 210, 315, 245), flags=0x13)
    + window(4, 0x2400, 0x048102CA, (21, 524, 92, 564))
    + window(5, 0x2500, 0x04810F47, (20, 6, 53, 31), 'Login', flags=0x13))
# Confirmation is a synthetic contract for the distinctive registration warning,
# not a claim of a newly measured native control ID or rectangle.
CONFIRMATION = (ROOT_WINDOW + MODAL_LAYER
    + window(2, 0x3100, 0x06318E78, (250, 300, 774, 470), flags=0x43)
    + window(3, 0x3200, 0x01510D08, (20, 20, 480, 60), "If you don't register, online features are unavailable.")
    + window(3, 0x3300, 1, (400, 110, 450, 150), 'OK'))
NETWORK = CONFIRMATION.replace("If you don't register, online features are unavailable.", 'Network play disabled')


class FakeGame(adventure.Game):
    def __init__(self, trees):
        self.trees = iter(trees)
        self.commands, self.clicks = [], []
        self.instance = 96

    def call(self, command, required=True):
        self.commands.append(command)
        if command == 'coop.ui tree 12':
            return next(self.trees)
        raise AssertionError('unexpected command: ' + command)

    def caption(self, value):
        return (110, 752) if value == 'Login' else None

    def menu(self):
        return True

    def control(self, ident):
        assert ident == 0x070D74E7
        return 11, 232, 226, 260, 1

    def click(self, x, y):
        self.clicks.append((x, y))

    def wait(self, description, predicate, timeout=None):
        for _ in range(40):
            result = predicate()
            if result:
                return result
        raise adventure.FixtureError('bounded fake wait expired: ' + description)


class RegistrationContracts(unittest.TestCase):
    def run_game(self, trees):
        game = FakeGame(trees)
        with patch.object(adventure.time, 'monotonic', side_effect=itertools.count(1)):
            game.dismiss_registration()
        return game

    def test_actual_registration_owner_targets_close(self):
        kind, _, point = adventure.registration_action(REGISTRATION)
        self.assertEqual((kind, point), ('registration', (868, 159)))

    def test_galaxy_footer_login_never_opens_or_closes_registration(self):
        self.assertIsNone(adventure.registration_action(GALAXY))
        game = self.run_game([GALAXY])
        self.assertEqual(game.clicks, [])
        self.assertEqual(game.commands, ['coop.ui tree 12'])

    def test_login_remains_after_registration_and_confirmation_close(self):
        game = self.run_game([REGISTRATION, REGISTRATION, CONFIRMATION, CONFIRMATION, GALAXY])
        self.assertEqual(game.clicks, [(868, 159), (675, 430)])

    def test_registration_can_close_without_confirmation(self):
        self.assertEqual(self.run_game([REGISTRATION, REGISTRATION, GALAXY]).clicks, [(868, 159)])

    def test_network_popup_with_footer_login_is_not_registration(self):
        tree = NETWORK + GALAXY[len(ROOT_WINDOW):]
        with self.assertRaisesRegex(adventure.FixtureError, 'unrelated native modal: Network play disabled'):
            adventure.registration_action(tree)
        game = FakeGame([tree])
        with self.assertRaises(adventure.FixtureError):
            game.dismiss_registration()
        self.assertEqual(game.clicks, [])

    def test_hidden_cached_registration_and_popup_do_not_block_menu(self):
        for tree in (REGISTRATION, NETWORK):
            hidden = tree.replace('fl=0x43 ', 'fl=0x42 ')
            self.assertIsNone(adventure.registration_action(hidden + GALAXY[len(ROOT_WINDOW):]))

    def test_hidden_ancestor_is_checked_even_if_dialog_flag_is_visible(self):
        hidden = REGISTRATION.replace('fl=0x13 ', 'fl=0x12 ', 1)
        self.assertIsNone(adventure.registration_action(hidden))

    def test_changed_dialog_identity_or_rectangle_is_not_clicked(self):
        for changed in (REGISTRATION.replace('00001200', '00001201'),
                        REGISTRATION.replace('[726 4 745 23]', '[726 5 745 24]'), GALAXY):
            self.assertEqual(self.run_game([REGISTRATION, changed, GALAXY]).clicks, [])

    def test_replacement_unrelated_popup_is_not_clicked(self):
        game = FakeGame([REGISTRATION, NETWORK])
        with self.assertRaises(adventure.FixtureError):
            game.dismiss_registration()
        self.assertEqual(game.clicks, [])

    def test_close_requires_exact_owner_id_command_and_enabled_flag(self):
        for old, new in (('0x05B598F7', '0x05B598F6'), ('0xFFFFFFFF', '0xFFFFFFF1'),
                         ('0x0451A4C0', '0x0451A4C1'), ('0x0519E168', '0x04810F47'),
                         ('cmd=0x00000003', 'cmd=0x00000002'),
                         ('[726 4 745 23]', '[900 4 919 23]')):
            with self.subTest(new=new), self.assertRaises(adventure.FixtureError):
                adventure.registration_action(REGISTRATION.replace(old, new))
        with self.assertRaises(adventure.FixtureError):
            adventure.registration_action(REGISTRATION.replace('fl=0x3 [726', 'fl=0x1 [726'))

    def test_duplicate_visible_modal_or_close_is_ambiguous(self):
        for extra in (window(2, 0x4100, 0xFFFFFFFF, (100, 100, 700, 600), flags=0x43),
                      window(3, 0x4200, 0xFFFFFFF2, (700, 4, 720, 23), command=3)):
            with self.assertRaises(adventure.FixtureError):
                adventure.registration_action(REGISTRATION + extra)

    def test_confirmation_only_uses_enabled_ok_in_same_visible_modal(self):
        foreign_ok = CONFIRMATION.replace(
            window(3, 0x3300, 1, (400, 110, 450, 150), 'OK'),
            window(1, 0x3300, 1, (400, 110, 450, 150), 'OK'))
        disabled = CONFIRMATION.replace('fl=0x3 [400', 'fl=0x1 [400')
        for tree in (foreign_ok, disabled, CONFIRMATION + window(3, 0x3400, 2, (1, 1, 20, 20), 'OK')):
            with self.assertRaises(adventure.FixtureError):
                adventure.registration_action(tree)

    def test_quoted_warning_caption_is_not_misparsed(self):
        tree = CONFIRMATION.replace('online features are unavailable.', "click 'OK' to continue.")
        self.assertEqual(adventure.registration_action(tree)[0], 'confirmation')

    def test_missing_malformed_truncated_or_duplicate_ancestry_is_rejected(self):
        for tree in ('', 'x', REGISTRATION[1:], REGISTRATION.replace('    Window 00001200', '        Window 00001200'),
                     REGISTRATION + window(1, 0x1000, 1, (0, 0, 20, 20)), 'x' * 60000):
            with self.subTest(tree=tree[:50]), self.assertRaises(adventure.FixtureError):
                adventure.registration_action(tree)

    def test_failure_to_dismiss_is_bounded(self):
        game = FakeGame(itertools.repeat(REGISTRATION))
        with patch.object(adventure.time, 'monotonic', side_effect=itertools.count(1)):
            with self.assertRaisesRegex(adventure.FixtureError, 'did not settle'):
                game.dismiss_registration()
        self.assertEqual(len(game.clicks), 16)


if __name__ == '__main__':
    unittest.main()
