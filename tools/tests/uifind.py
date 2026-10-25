"""Spore UI windows by caption for test bots (coop.ui findall): where a button is on the screen and
whether it is shown, so a bot clicks it like a player would (pause menu, dialogs)."""
import re

LINE = re.compile(r'(?:- |Window )\S+ id=\S+ cmd=\S+ fl=0x([0-9A-Fa-f]+) \[(-?\d+) (-?\d+) (-?\d+) (-?\d+)\]')


def match_groups(con, caption):
    """Each matching window followed by its ancestors; support older find-only builds."""
    quoted = '"' + caption.replace('\\', '\\\\').replace('"', '\\"') + '"'
    response = con.call('coop.ui findall ' + quoted)
    text = response.get('out', '')
    if not any(re.fullmatch(r'match \d+', line) for line in text.splitlines()):
        if 'no window with caption' in text:
            return []
        # Old c_ui treats an unknown subcommand as a depth-zero tree dump and
        # can return ok=true. A match header distinguishes the additive command.
        text = con.call('coop.ui find ' + quoted).get('out', '')
    groups, current = [], []
    for line in text.splitlines() + ['']:
        if not line.strip() or re.fullmatch(r'match \d+', line):
            if current:
                groups.append(current)
                current = []
        elif LINE.match(line):
            current.append(line)
    return groups


def find(con, caption):
    """[(x, y, visible)] for every window with this caption: its centre on the screen and whether it
    and all its parents are visible (flag 0x1); coordinates of a window are relative to its parent"""
    res = []
    for group in match_groups(con, caption):
        nodes = [LINE.match(line) for line in group]
        g = [(int(node.group(1), 16), *(int(node.group(i)) for i in range(2, 6))) for node in nodes]
        ox = sum(w[1] for w in g[1:])
        oy = sum(w[2] for w in g[1:])
        f, x0, y0, x1, y1 = g[0]
        res.append((ox + (x0 + x1) // 2, oy + (y0 + y1) // 2, all(w[0] & 1 for w in g)))
    return res


def shown(con, caption):
    """(x, y) of the first visible window with this caption, or None"""
    for x, y, vis in find(con, caption):
        if vis:
            return x, y
    return None


def press(con, caption, expect=None, timeout=10):
    """click a shown button like a player (hover, then click) and wait for what it opens (expect: a
    caption) or for the button to go away; click again only when nothing happened (a click right
    after a menu opened is sometimes ignored). A menu stays "shown" under a dialog it opened."""
    import time
    t0 = time.time()
    while time.time() - t0 < timeout:
        p = shown(con, caption)
        if not p:
            time.sleep(0.5)
            continue
        con.call(f'move {p[0]} {p[1]}')
        time.sleep(0.3)
        con.call(f'click {p[0]} {p[1]}')
        t1 = time.time()
        while time.time() - t1 < 3:
            time.sleep(0.5)
            if expect and shown(con, expect):
                return True
            if not expect and not shown(con, caption):
                return True
    return False
