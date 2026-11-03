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
# Exact warning IDs, sibling order and rectangles from C31 four-player startup.
CONFIRMATION = (ROOT_WINDOW + MODAL_LAYER
    + window(2, 0x3100, 0x065CF488, (238, 304, 785, 464), flags=0x43)
    + window(3, 0x3200, 0x01510D08, (20, 20, 480, 60), adventure._REGISTRATION_WARNING)
    + window(3, 0x3300, 0xFFFFFFF1, (464, 105, 527, 142), 'OK')
    + window(3, 0x3400, 0xFFFFFFF2, (377, 105, 464, 142), 'Cancel'))
STACK = CONFIRMATION + REGISTRATION[len(ROOT_WINDOW + MODAL_LAYER):]
NETWORK = CONFIRMATION.replace(adventure._REGISTRATION_WARNING, 'Network play disabled')


class FakeGame(adventure.Game):
    def __init__(self, trees):
        self.trees = iter(trees)
        self.commands, self.clicks = [], []
        self.instance = 96

    def registration_snapshot(self):
        self.commands.append('snapshot')
        return next(self.trees)

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


class ScopedGame(FakeGame):
    registration_snapshot = adventure.Game.registration_snapshot

    def __init__(self, tree, on_call=None):
        super().__init__([])
        self.tree, self.on_call = tree, on_call

    def call(self, command, required=True):
        self.commands.append(command)
        if self.on_call:
            self.on_call(self, command)
        nodes = adventure._startup_tree(self.tree)
        parts = command.split()
        if parts == ['coop.ui', 'tree', '1']:
            root = nodes[0]
        elif len(parts) == 5 and parts[:4] == ['coop.ui', 'tree', '1', 'visible']:
            root = next(node for node in nodes if node['address'] == int(parts[4], 16))
        else:
            raise AssertionError('unexpected command: ' + command)
        selected = [node for node in nodes if node['visible'] and (node is root or node['parent'] is root)]
        return '\n'.join('  ' * (node['depth'] - root['depth']) + node['line'] for node in selected)


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
        self.assertEqual(game.commands, ['snapshot'])

    def test_login_remains_after_registration_and_confirmation_close(self):
        game = self.run_game([REGISTRATION, REGISTRATION, CONFIRMATION, CONFIRMATION, GALAXY])
        self.assertEqual(game.clicks, [(868, 159), (733, 427)])

    def test_actual_warning_above_registration_is_the_only_allowed_stack(self):
        self.assertEqual(adventure.registration_action(STACK)[::2], ('confirmation', (733, 427)))
        game = self.run_game([STACK, STACK, GALAXY])
        self.assertEqual(game.clicks, [(733, 427)])
        reversed_stack = REGISTRATION + CONFIRMATION[len(ROOT_WINDOW + MODAL_LAYER):]
        for tree in (reversed_stack, STACK + NETWORK[len(ROOT_WINDOW + MODAL_LAYER):],
                     REGISTRATION + NETWORK[len(ROOT_WINDOW + MODAL_LAYER):]):
            with self.assertRaises(adventure.FixtureError):
                adventure.registration_action(tree)

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
            window(3, 0x3300, 0xFFFFFFF1, (464, 105, 527, 142), 'OK'),
            window(1, 0x3300, 0xFFFFFFF1, (464, 105, 527, 142), 'OK'))
        disabled = CONFIRMATION.replace('fl=0x3 [464', 'fl=0x1 [464')
        for tree in (foreign_ok, disabled,
                     CONFIRMATION + window(3, 0x3500, 0xFFFFFFF1, (1, 1, 20, 20), 'OK'),
                     CONFIRMATION.replace("'Cancel'", "'Other'")):
            with self.assertRaises(adventure.FixtureError):
                adventure.registration_action(tree)

    def test_quoted_warning_caption_is_not_misparsed(self):
        tree = CONFIRMATION + window(3, 0x3500, 9, (20, 80, 40, 90), "Don't prompt me again")
        self.assertEqual(adventure.registration_action(tree)[0], 'confirmation')
        with self.assertRaises(adventure.FixtureError):
            adventure.registration_action(CONFIRMATION.replace('Quit registration?', 'Quit something else?'))

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

    def test_scoped_snapshot_preserves_full_ancestry_without_deep_tree_cap(self):
        # Native embedded registration browser can exhaust the whole-tree cap;
        # none of its descendants is needed to identify the direct close button.
        deep = ''.join(window(4, 0x5000 + i, i, (0, 0, 30, 30), 'x' * 30) for i in range(100))
        tree = STACK.replace(window(3, 0x1300, 0x0451A4C0, (24, 70, 732, 401), flags=0x403),
                             window(3, 0x1300, 0x0451A4C0, (24, 70, 732, 401), flags=0x403) + deep)
        self.assertGreater(len(tree), 8192)
        game = ScopedGame(tree)
        snapshot = game.registration_snapshot()
        self.assertEqual(adventure.registration_action(snapshot)[::2], ('confirmation', (733, 427)))
        self.assertEqual(game.commands, ['coop.ui tree 1', 'coop.ui tree 1 visible 00001100',
            'coop.ui tree 1 visible 00003100', 'coop.ui tree 1 visible 00001200',
            'coop.ui tree 1', 'coop.ui tree 1 visible 00001100'])
        self.assertLess(len(snapshot), 8192)

    def test_scoped_snapshot_rechecks_main_overlay_and_modal_identity(self):
        for at, before, after in ((3, 'id=0x065CF488', 'id=0x065CF489'),
                                  (5, '[0 0 1024 768]', '[1 0 1025 768]'),
                                  (6, '00001200', '00001201')):
            def mutate(game, command):
                if len(game.commands) == at:
                    game.tree = game.tree.replace(before, after)
            with self.subTest(at=at):
                self.assertIsNone(ScopedGame(STACK, mutate).registration_snapshot())

    def test_scoped_snapshot_rejects_truncated_and_unknown_overlay(self):
        class Truncated(ScopedGame):
            def call(self, command, required=True):
                result = super().call(command, required)
                return result[:80] if len(self.commands) == 2 else result
        with self.assertRaises(adventure.FixtureError):
            Truncated(STACK).registration_snapshot()
        class Capped(ScopedGame):
            def call(self, command, required=True):
                result = super().call(command, required)
                return result + ' ' * (8191 - len(result)) if len(self.commands) == 2 else result
        with self.assertRaises(adventure.FixtureError):
            Capped(STACK).registration_snapshot()
        unknown = STACK.replace('id=0x065CF488', 'id=0x06318E78')
        with self.assertRaises(adventure.FixtureError):
            adventure.registration_action(ScopedGame(unknown).registration_snapshot())
        nonmodal = ROOT_WINDOW + MODAL_LAYER + window(2, 0x8000, 5, (0, 0, 20, 20))
        with self.assertRaises(adventure.FixtureError):
            ScopedGame(nonmodal).registration_snapshot()

    def test_scoped_snapshot_ignores_hidden_cached_modals_and_checks_offsets(self):
        hidden = REGISTRATION.replace('fl=0x43 ', 'fl=0x42 ')
        self.assertIsNone(adventure.registration_action(ScopedGame(hidden).registration_snapshot()))
        moved = STACK.replace('[0 0 1024 768]', '[7 9 1031 777]', 1)
        self.assertEqual(adventure.registration_action(ScopedGame(moved).registration_snapshot())[2], (740, 436))


if __name__ == '__main__':
    unittest.main()
