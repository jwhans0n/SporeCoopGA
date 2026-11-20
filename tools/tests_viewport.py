"""Read native viewport dimensions; preferences alone do not qualify pixel input."""
import re


def viewport_of(console):
    output = console.call('coop.uistate').get('out', '')
    match = re.search(r'\bscreen[= ]+(\d+)[x ,]+(\d+)', output)
    if not match:
        raise RuntimeError('native viewport dimensions unavailable in coop.uistate')
    return tuple(map(int, match.groups()))


def canvas_of(console):
    output = console.call('coop.ui where FFFFFFF0').get('out', '')
    match = re.search(r'^FFFFFFF0 (-?\d+) (-?\d+) (-?\d+) (-?\d+) enabled=\d+', output, re.M)
    if not match:
        raise RuntimeError('native main UI canvas is unavailable')
    return tuple(map(int, match.groups()))
