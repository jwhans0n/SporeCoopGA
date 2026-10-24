"""Loopback-only UDP proxy for recorded lab traffic and deterministic loss/jitter."""
from collections import Counter
import heapq
import random
import socket
import struct
import threading
import time


class NetworkProxy:
    def __init__(self, listen, target, loss=0, jitter=0, seed=7):
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', listen))
        self.socket.settimeout(.01)
        self.target = ('127.0.0.1', target)
        self.client = None
        self.loss, self.jitter = loss, jitter
        self.random = random.Random(seed)
        self.counts = Counter()
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.error = None

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.stopped.set()
        self.thread.join(timeout=2)
        self.socket.close()

    def snapshot(self):
        if self.error:
            raise RuntimeError(self.error)
        with self.lock:
            return dict(self.counts)

    @staticmethod
    def difference(before, after):
        return {k: after.get(k, 0)-before.get(k, 0) for k in after.keys() | before.keys()}

    def _record(self, direction, data):
        counts = Counter({direction+'_bytes': len(data), direction+'_packets': 1})
        if len(data) >= 31 and data[:4] == b'SPCO':
            offset = 31
            for _ in range(data[30]):
                if offset+4 > len(data):
                    break
                kind, reliable, size = struct.unpack_from('<BBH', data, offset)
                offset += 6 if reliable else 4
                if offset+size > len(data):
                    break
                category = 'players' if kind == 6 else 'other'
                if kind == 52 and size:
                    sub = data[offset]
                    category = ('assets' if 64 <= sub <= 71 else 'captains' if sub >= 192 else
                                'actors' if sub == 128 else 'static' if sub == 136 else 'world')
                counts[direction+'_'+category+'_payload_bytes'] += size
                counts[direction+'_'+category+'_messages'] += 1
                if kind == 52 and size >= 15 and data[offset] == 128:
                    actors = data[offset+14]
                    if size == 15+54*actors:
                        counts[direction+'_actor_records'] += actors
                        for index in range(actors):
                            marker, incarnation = struct.unpack_from('<II', data, offset+15+54*index)
                            counts[f'{direction}_actor_{marker}_{incarnation}'] += 1
                offset += size
        with self.lock:
            self.counts.update(counts)

    def _run(self):
        pending = []
        serial = 0
        try:
            while not self.stopped.is_set():
                try:
                    data, address = self.socket.recvfrom(65536)
                    if address == self.target:
                        direction, destination = 'down', self.client
                    else:
                        if address[0] != '127.0.0.1':
                            continue
                        self.client = address
                        direction, destination = 'up', self.target
                    self._record(direction, data)
                    if destination and self.random.random() >= self.loss:
                        serial += 1
                        delay = self.random.uniform(0, self.jitter)
                        heapq.heappush(pending, (time.monotonic()+delay, serial, data, destination))
                    else:
                        with self.lock:
                            self.counts[direction+'_dropped'] += 1
                except socket.timeout:
                    pass
                now = time.monotonic()
                while pending and pending[0][0] <= now:
                    _, _, data, destination = heapq.heappop(pending)
                    self.socket.sendto(data, destination)
        except OSError as error:
            if not self.stopped.is_set():
                self.error = str(error)
