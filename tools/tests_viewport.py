"""Read native viewport dimensions; preferences alone do not qualify pixel input."""
import re


def viewport_of(console):
    output = console.call('coop.uistate').get('out', '')
    match = re.search(r'\bscreen[= ]+(\d+)[x ,]+(\d+)', output)
    if not match:
        raise RuntimeError('native viewport dimensions unavailable in coop.uistate')
    return tuple(map(int, match.groups()))
