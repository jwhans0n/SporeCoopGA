"""Offline regression for hidden captions preceding visible Sporepedia cards."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/tests')]
import uifind  # noqa: E402
from coop_adventure import Game  # noqa: E402


def window(ident, flags, rect, caption='', kind='Window'):
    return f"{kind} 12345678 id=0x{ident:08X} cmd=0x00000000 fl=0x{flags:X} [{rect}] '{caption}'"


ROOT_WINDOW = window(0xFFFFFFF0, 3, '0 0 1024 768')
HIDDEN_LOADING = [window(0x076236BC, 3, '23 -4 979 134', 'Welcome to Adventure Town'),
                  window(0x11111111, 0x52, '0 0 1024 768'), ROOT_WINDOW]


def card(overflow=False, visible=True):
    return [window(0x53D6FE29 if overflow else 0x53D6FE30, 0x12 if overflow else 3,
                   '14 15 138 36', 'Adventure Town', '-'),
            window(0x0674AA70, 3 if visible else 2, '0 0 148 204'),
            window(0xF3C6DC19, 3, '183 3 331 207'),
            window(0x53C5B391, 3, '180 129 1023 749'), ROOT_WINDOW]


def findall(*groups):
    return '\n\n'.join(f'match {index}\n' + '\n'.join(group) for index, group in enumerate(groups, 1))


class Console:
    def __init__(self, text, legacy=None, ok=True):
        self.text, self.legacy, self.ok = text, legacy, ok
        self.commands = []

    def call(self, command):
        self.commands.append(command)
        if command.startswith('coop.ui findall '):
            return {'ok': self.ok, 'out': self.text}
        return {'ok': True, 'out': self.legacy or ''}


class CaptionTests(unittest.TestCase):
    def test_visible_card_after_hidden_loading_description(self):
        console = Console(findall(HIDDEN_LOADING, card()))
        self.assertEqual(uifind.shown(console, 'Adventure Town'), (439, 157))

    def test_successful_old_tree_response_falls_back(self):
        console = Console(ROOT_WINDOW, legacy='\n'.join(card()))
        self.assertEqual(uifind.shown(console, 'Adventure Town'), (439, 157))
        self.assertEqual(console.commands, ['coop.ui findall "Adventure Town"', 'coop.ui find "Adventure Town"'])

    def test_missing_caption_does_not_repeat_legacy_search(self):
        console = Console("no window with caption 'missing'", ok=False)
        self.assertEqual(uifind.find(console, 'missing'), [])
        self.assertEqual(len(console.commands), 1)

    def test_overflow_fallback_checks_each_parent_chain(self):
        game = Game.__new__(Game)
        game.console = Console(findall(HIDDEN_LOADING, card(overflow=True, visible=False), card(overflow=True)))
        self.assertEqual(game.caption('Adventure Town'), (437, 234))


if __name__ == '__main__':
    unittest.main()
