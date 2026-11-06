#!/usr/bin/env python3
"""fakepeer - a scripted SporeCoop guest speaking the real UDP protocol.

Joins a host (a game running `coop.host`), then walks around the host's avatar so
the host sees a remote player's puppet without a second copy of the game.

usage: fakepeer.py [host[:port]] [--name Bot] [--mode circle|follow|static]
                   [--radius 8] [--period 12] [--seconds 60] [--build HEX] [--protocol 6|7]
Mirrors mods/coop/net (protocol.h, connection.cpp). Run with python3 -I.
"""
import argparse
import math
import os
import socket
import struct
import sys
import time

MAGIC = 0x4F435053
# Default to the current protocol. Check the wire format before adding versions.
PROTO = 7
HEADER = '<IHIHHQQB'  # magic, proto, conn, seq, ack, ackbits (2 x u64), count
HEADER_SIZE = struct.calcsize(HEADER)
HELLO, WELCOME, REJECT, JOIN, LEAVE, STATE, CHAT, PING, PONG, BYE = range(1, 11)
PF_IN_WORLD, PF_MOVING, PF_RUNNING = 1, 2, 4


def seq_greater(a, b):
    return (a > b and a - b <= 32768) or (a < b and b - a > 32768)


class Conn:
    """Minimal port of net::Connection (seq/ack/ackbits + reliable ordered)."""

    def __init__(self, sock, addr, protocol=None):
        self.sock, self.addr = sock, addr
        self.protocol = PROTO if protocol is None else protocol
        self.conn = int.from_bytes(os.urandom(4), 'little') or 1  # a new id for every join
        self.local_seq = 1  # like net::Connection (BUG-0030): ack 0 of a peer that got nothing acks nothing
        self.remote_seq = 0
        self.recv_bits = 0
        self.have_remote = False
        self.rel_out = []          # [rel_id, type, payload, last_sent, sends]
        self.next_rel = 0
        self.next_expected = 0
        self.rel_in = {}
        self.sent = {}             # seq -> [rel_ids]
        self.unrel = []
        self.last_send = 0.0
        self.last_recv = time.time()

    def send_reliable(self, typ, payload):
        self.rel_out.append([self.next_rel, typ, payload, 0.0, 0])
        self.next_rel = (self.next_rel + 1) & 0xFFFF

    def send_unreliable(self, typ, payload):
        self.unrel.append((typ, payload))

    def flush(self, now):
        due = [m for m in self.rel_out if m[4] == 0 or now - m[3] > 0.2]
        if not self.unrel and not due and now - self.last_send < 0.1:
            return
        seq = self.local_seq
        self.local_seq = (self.local_seq + 1) & 0xFFFF
        body = b''
        count = 0
        for typ, p in self.unrel:
            body += struct.pack('<BBH', typ, 0, len(p)) + p
            count += 1
        self.unrel = []
        ids = []
        for m in due:
            body += struct.pack('<BBHH', m[1], 1, len(m[2]), m[0]) + m[2]
            m[3] = now
            m[4] += 1
            ids.append(m[0])
            count += 1
        bits = self.recv_bits if self.have_remote else 0
        hdr = struct.pack(HEADER, MAGIC, self.protocol, self.conn, seq, self.remote_seq, bits & (2**64 - 1), bits >> 64, count)
        self.sent[seq] = ids
        if len(self.sent) > 256:
            self.sent.pop(next(iter(self.sent)))
        self.sock.sendto(hdr + body, self.addr)
        self.last_send = now

    def _ack(self, seq):
        ids = self.sent.pop(seq, None)
        if ids:
            self.rel_out = [m for m in self.rel_out if m[0] not in ids]

    def on_packet(self, data, deliver):
        if len(data) < HEADER_SIZE:
            return
        magic, proto, conn, seq, ack, lo, hi, count = struct.unpack_from(HEADER, data)
        if magic != MAGIC or proto != self.protocol or conn != self.conn:
            return
        ackbits = lo | hi << 64
        self.last_recv = time.time()
        if not self.have_remote:
            self.have_remote, self.remote_seq, self.recv_bits = True, seq, 0
        elif seq_greater(seq, self.remote_seq):
            sh = (seq - self.remote_seq) & 0xFFFF
            self.recv_bits = 0 if sh > 128 else ((self.recv_bits << sh) | (1 << (sh - 1))) & (2**128 - 1)
            self.remote_seq = seq
        else:
            d = (self.remote_seq - seq) & 0xFFFF
            if 1 <= d <= 128:
                self.recv_bits |= 1 << (d - 1)
        for s in list(self.sent):
            d = (ack - s) & 0xFFFF
            if s == ack or (1 <= d <= 128 and ackbits & (1 << (d - 1))):
                self._ack(s)
        pos = HEADER_SIZE
        for _ in range(count):
            typ, flags, ln = struct.unpack_from('<BBH', data, pos)
            pos += 4
            rid = None
            if flags & 1:
                rid, = struct.unpack_from('<H', data, pos)
                pos += 2
            p = data[pos:pos + ln]
            pos += ln
            if rid is None:
                deliver(typ, p)
            elif rid == self.next_expected:
                deliver(typ, p)
                self.next_expected = (self.next_expected + 1) & 0xFFFF
                while self.next_expected in self.rel_in:
                    t2, p2 = self.rel_in.pop(self.next_expected)
                    deliver(t2, p2)
                    self.next_expected = (self.next_expected + 1) & 0xFFFF
            elif seq_greater(rid, self.next_expected):
                self.rel_in.setdefault(rid, (typ, p))


def pack_state(pid, t_ms, pos, rot, vel, flags, hp, mhp, species, mode=0, world=0):
    return struct.pack('<BI3f4f3fBBffIII', pid, t_ms & 0xFFFFFFFF, *pos, *rot, *vel, flags, 0, hp, mhp, species,
                       mode, world)


def unpack_state(p):
    v = struct.unpack_from('<BI3f4f3fBBffIII', p)
    return {'id': v[0], 'pos': v[2:5], 'rot': v[5:9], 'vel': v[9:12], 'flags': v[12], 'mode': v[17], 'world': v[18]}


def norm(v):
    l = math.sqrt(sum(c * c for c in v)) or 1.0
    return tuple(c / l for c in v)


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('host', nargs='?', default='127.0.0.1:47810')
    ap.add_argument('--name', default='Bot')
    ap.add_argument('--mode', default='circle', choices=['circle', 'follow', 'static'])
    ap.add_argument('--radius', type=float, default=8.0)
    ap.add_argument('--period', type=float, default=12.0)
    ap.add_argument('--seconds', type=float, default=60.0)
    ap.add_argument('--build', default='dc04aee5', help='first 8 hex digits of the host game build ID')
    ap.add_argument('--protocol', type=int, choices=(6, 7), default=PROTO, help='host protocol version (default: 7)')
    ap.add_argument('--say', default='hello from fakepeer')
    a = ap.parse_args()
    h, _, port = a.host.partition(':')
    addr = (socket.gethostbyname(h), int(port or 47810))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('0.0.0.0', 0))
    sock.setblocking(False)
    c = Conn(sock, addr, a.protocol)
    name = a.name.encode()[:255]
    c.send_reliable(HELLO, struct.pack('<HIB', c.protocol, int(a.build, 16), len(name)) + name)
    st = {'my_id': None, 'host': None, 'said': False}

    def deliver(typ, p):
        if typ == WELCOME:
            st['my_id'] = p[0]
            print(f'welcome: my id {p[0]}, world 0x{struct.unpack_from("<I", p, 1)[0]:08x}')
        elif typ == REJECT:
            print('rejected:', p[2:2 + p[1]].decode(errors='replace'))
            sys.exit(1)
        elif typ == STATE:
            s = unpack_state(p)
            if s['id'] == 0:
                st['host'] = s
        elif typ == CHAT:
            ln, = struct.unpack_from('<H', p, 1)
            print(f'chat <{p[0]}>: {p[3:3 + ln].decode(errors="replace")}')
        elif typ == JOIN:
            print('join', p[0], p[2:2 + p[1]].decode(errors='replace'))
        elif typ == LEAVE:
            print('leave', p[0])
        elif typ == BYE:
            print('host closed the session')
            sys.exit(0 if st['my_id'] is not None else 1)

    t0 = time.time()
    last_state = 0.0
    prev_pos = None
    while time.time() - t0 < a.seconds:
        now = time.time()
        try:
            while True:
                data, frm = sock.recvfrom(2048)
                if frm == addr:
                    c.on_packet(data, deliver)
        except BlockingIOError:
            pass
        host_ok = st['host'] and st['host']['flags'] & PF_IN_WORLD and sum(x * x for x in st['host']['pos']) > 1
        if st['my_id'] is not None and host_ok and now - last_state >= 0.05:
            last_state = now
            hp = st['host']['pos']
            up = norm(hp)
            ref = (0.0, 0.0, 1.0) if abs(up[2]) < 0.9 else (1.0, 0.0, 0.0)
            east = norm(cross(up, ref))
            north = cross(east, up)
            if a.mode == 'circle':
                th = 2 * math.pi * (now - t0) / a.period
            elif a.mode == 'follow':
                th = math.pi
            else:
                th = 0.0
            off = tuple(a.radius * (math.cos(th) * east[i] + math.sin(th) * north[i]) for i in range(3))
            p = tuple(hp[i] + off[i] for i in range(3))
            r = math.sqrt(sum(x * x for x in hp))
            p = tuple(x * r / math.sqrt(sum(y * y for y in p)) for x in p)
            vel = (0.0, 0.0, 0.0) if prev_pos is None else tuple((p[i] - prev_pos[i]) / 0.05 for i in range(3))
            prev_pos = p
            speed = math.sqrt(sum(v * v for v in vel))
            flags = PF_IN_WORLD | (PF_MOVING if speed > 0.3 else 0)
            c.send_unreliable(STATE, pack_state(st['my_id'], int(now * 1000), p, st['host']['rot'], vel,
                                                flags, 10.0, 10.0, 0, st['host']['mode'], st['host']['world']))
            if not st['said'] and now - t0 > 2:
                txt = a.say.encode()
                c.send_reliable(CHAT, struct.pack('<BH', st['my_id'], len(txt)) + txt)
                st['said'] = True
        if now - c.last_recv > 15:
            print('timeout')
            return 1
        c.flush(now)
        time.sleep(0.01)
    if st['my_id'] is None:
        print('no welcome received')
        return 1
    for _ in range(3):  # unreliable, a few copies
        c.send_unreliable(BYE, b'\0')
        c.flush(time.time() + 1)
    print('done')
    return 0


if __name__ == '__main__':
    sys.exit(main())
