#!/usr/bin/env python3
"""Census of the shared objects (`coop.census`, mods/coop/census.h; docs/06-testing.md#testing-census).

  dump(con)                  -> Census of one machine (parsed `coop.census`)
  diff(host, guest, ...)     -> what differs between the host's objects and the guest's (net id pairs)
  moved(a, b, own, min_m)    -> objects of one machine that moved between two dumps (by object)

Command line (a lane with both games running):
  SPORE_LANE=N python3 tools/tests/census.py            diff of the host and the guest now
"""
import math
import re

OBJ = re.compile(r'obj ([cv])(-?\d+) key=([0-9A-F]{8}) alive=(-?\d+) hp=(\S+) ai=(-?\d+) own=(\S*) owner=(-?\d+) '
                 r'pos=\(([-\d.e+]+) ([-\d.e+]+) ([-\d.e+]+)\)')
HEAD = re.compile(r'census v1 role=(\S+) me=(\d+) mode=(.*?) modeid=([0-9A-F]+) world=([0-9A-F]+) t=([\d.]+)')


class Census:
    def __init__(self, text):
        self.role = self.mode = None
        self.world = self.modeid = None
        self.t = 0.0
        self.rows = []
        self.complete = False
        for line in text.splitlines():
            m = HEAD.match(line.strip())
            if m:
                self.role, self.mode, self.modeid, self.world, self.t = m.group(1), m.group(3), m.group(4), m.group(5), float(m.group(6))
                continue
            m = OBJ.match(line.strip())
            if m:
                self.rows.append(dict(kind=m.group(1), id=int(m.group(2)), key=m.group(3), alive=int(m.group(4)),
                                      hp=float(m.group(5)), ai=int(m.group(6)), own=m.group(7), owner=int(m.group(8)),
                                      pos=tuple(float(m.group(i)) for i in (9, 10, 11))))
                continue
            if line.startswith('end objects='):
                self.complete = int(line.split('=')[1]) == len(self.rows)

    def by_id(self, kind, own=None):
        """{net id: row} of the objects of a kind that have a net id (own: only these owners)"""
        return {r['id']: r for r in self.rows if r['kind'] == kind and r['id'] >= 0 and (own is None or r['own'] in own)}

    def count(self, kind, own=None):
        return len([r for r in self.rows if r['kind'] == kind and (own is None or r['own'] in own)])


def dump(con):
    return Census(con.call('coop.census')['out'])


def diff(host, guest, pos_m=10.0):
    """problems between a host census and a guest census taken right after it (same mode and world):
    objects only on one side, dead on one and alive on the other, another owner, more than pos_m apart"""
    out = []
    if host.modeid != guest.modeid or host.world != guest.world:
        return [f'not in the same place: host {host.mode} {host.world}, guest {guest.mode} {guest.world}']
    for kind, hown, gown in (('c', ('npc',), ('proxy',)), ('v', ('host',), ('mirror',))):
        h, g = host.by_id(kind, hown), guest.by_id(kind, gown)
        for i in sorted(set(h) - set(g)):
            out.append(f'{kind}{i}: on the host only ({h[i]["own"]} at {fmt(h[i]["pos"])})')
        for i in sorted(set(g) - set(h)):
            out.append(f'{kind}{i}: on the guest only ({g[i]["own"]} at {fmt(g[i]["pos"])})')
        for i in sorted(set(h) & set(g)):
            a, b = h[i], g[i]
            if a['alive'] != b['alive']:
                out.append(f'{kind}{i}: alive={a["alive"]} on the host, {b["alive"]} on the guest')
            if kind == 'v' and a['owner'] != b['owner']:
                out.append(f'{kind}{i}: owner {a["owner"]} on the host, {b["owner"]} on the guest')
            d = math.dist(a['pos'], b['pos'])
            if d > pos_m:
                out.append(f'{kind}{i}: {d:.1f} m apart, host {fmt(a["pos"])} guest {fmt(b["pos"])}')
    return out


def moved(a, b, kind='c', own=('local', 'proxy', 'npc'), min_m=1.0):
    """objects of `kind` and owners `own` in both dumps of one machine (matched by net id, else by
    species and the nearest position) and how far each moved: [(row in b, metres)]"""
    res = []
    used = set()
    for r in b.rows:
        if r['kind'] != kind or r['own'] not in own:
            continue
        cand = [(math.dist(r['pos'], q['pos']), k) for k, q in enumerate(a.rows)
                if k not in used and q['kind'] == kind and q['own'] == r['own'] and q['key'] == r['key']
                and (r['id'] < 0 or q['id'] == r['id'])]
        if not cand:
            continue
        d, k = min(cand)
        used.add(k)
        res.append((r, d))
    return res


def fmt(p):
    return '(' + ', '.join(f'{x:.0f}' for x in p) + ')'


if __name__ == '__main__':
    import os
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    from sporectl import Console  # noqa: E402
    import lane  # noqa: E402
    h, g = Console(lane.HOST), Console(lane.GUEST)
    hc, gc = dump(h), dump(g)
    print(f'host: {len(hc.rows)} objects ({hc.mode}), guest: {len(gc.rows)} objects ({gc.mode})')
    for p in diff(hc, gc):
        print('  ' + p)
