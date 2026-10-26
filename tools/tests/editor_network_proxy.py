"""Bounded loopback-only impairment proxy for opt-in native editor lab tests.

Counts source UDP bytes and reliable retransmissions before injected duplication.
It never fabricates application commands, connects a game, or touches a profile.
All rates are symmetric PER PEER/PER DIRECTION, excluding IP/UDP overhead.
"""
from collections import Counter, OrderedDict
from dataclasses import dataclass
import heapq
import random
import select
import socket
import struct
import threading
import time


@dataclass(frozen=True)
class Profile:
    rtt_ms: float = 0
    loss: float = 0
    reorder: float = 0
    duplicate: float = 0
    bits_second: int = 0

    def __post_init__(self):
        if not 0 <= self.rtt_ms <= 2000 or not 0 <= self.bits_second <= 1000000000:
            raise ValueError('Invalid bounded network delay/rate')
        if any(not 0 <= value <= 1 for value in (self.loss, self.reorder, self.duplicate)):
            raise ValueError('Invalid network probability')


def messages(data):
    """Validate the complete protocol-16 frame before inspecting its messages."""
    if len(data) < 31 or data[:4] != b'SPCO' or struct.unpack_from('<H', data, 4)[0] != 16:
        return []
    connection = struct.unpack_from('<I', data, 6)[0]
    offset, result = 31, []
    for _ in range(data[30]):
        if offset + 4 > len(data):
            return []
        kind, flags, size = struct.unpack_from('<BBH', data, offset)
        offset += 4
        if flags not in (0, 1) or offset + (2 if flags else 0) + size > len(data):
            return []
        reliable = struct.unpack_from('<H', data, offset)[0] if flags else None
        offset += 2 if flags else 0
        payload = data[offset:offset+size]
        result.append((connection, kind, reliable, payload))
        offset += size
    return result if offset == len(data) else []


class EditorProxy:
    max_packets, max_bytes = 4096, 4 << 20

    def __init__(self, target_port, listen_port=0, seed=0xBEEF1234):
        if not 1 <= target_port <= 65535 or not 0 <= listen_port <= 65535:
            raise ValueError('Invalid loopback port')
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(('127.0.0.1', listen_port))
        self.socket.setblocking(False)
        self.target = ('127.0.0.1', target_port)
        self.client = None
        self.random = random.Random(seed)
        self.profile = Profile()
        self.queue, self.queue_bytes, self.serial = [], 0, 0
        self.next_departure = [0., 0.]
        self.last_delivered, self.ordinals = [0, 0], [0, 0]
        self.seen = [OrderedDict(), OrderedDict()]
        self.stats = [Counter(), Counter()]
        self.lock = threading.Lock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self._pump, daemon=True)
        self.error = None

    @property
    def port(self):
        return self.socket.getsockname()[1]

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *args):
        self.stop.set()
        self.thread.join(timeout=2)
        self.socket.close()
        if self.thread.is_alive():
            raise RuntimeError('Network proxy did not stop')

    def configure(self, profile):
        if not isinstance(profile, Profile):
            raise TypeError('A validated Profile is required')
        with self.lock:
            self.profile = profile
            self.next_departure = [time.monotonic(), time.monotonic()]

    def snapshot(self):
        with self.lock:
            if self.error:
                raise RuntimeError(self.error)
            return {'down': dict(self.stats[0]), 'up': dict(self.stats[1]),
                    'pending_packets': len(self.queue), 'pending_bytes': self.queue_bytes,
                    'profile': vars(self.profile)}

    @staticmethod
    def difference(before, after):
        return {direction: {key: after[direction].get(key, 0)-before[direction].get(key, 0)
                            for key in after[direction].keys() | before[direction].keys()}
                for direction in ('down', 'up')}

    def _record(self, direction, data):
        stats = self.stats[direction]
        stats['source_datagrams'] += 1
        stats['source_udp_payload_bytes'] += len(data)
        for connection, kind, reliable, payload in messages(data):
            if reliable is not None:
                stats['reliable_message_sends'] += 1
                identity = connection, reliable
                if identity in self.seen[direction]:
                    stats['retransmitted_messages'] += 1
                    stats['retransmitted_message_bytes'] += len(payload)+6
                self.seen[direction][identity] = None
                self.seen[direction].move_to_end(identity)
                if len(self.seen[direction]) > 8192:
                    self.seen[direction].popitem(last=False)
            if kind == 53 and len(payload) > 42 and payload[9] == 1:
                stats['editor_messages'] += 1
                stats['editor_payload_bytes'] += len(payload)
                stats['editor_tag_'+str(payload[42])] += 1

    def _enqueue(self, due, data, destination, direction, ordinal):
        if len(self.queue) >= self.max_packets or self.queue_bytes+len(data) > self.max_bytes:
            raise RuntimeError('Bounded impairment queue exceeded; fixture cannot claim acceptance')
        self.serial += 1
        heapq.heappush(self.queue, (due, self.serial, data, destination, direction, ordinal))
        self.queue_bytes += len(data)

    def _pump(self):
        try:
            while not self.stop.is_set():
                now = time.monotonic()
                with self.lock:
                    wait = max(0., min(.01, self.queue[0][0]-now)) if self.queue else .01
                ready, _, _ = select.select([self.socket], [], [], wait)
                if ready:
                    for _ in range(256):
                        try:
                            data, address = self.socket.recvfrom(65536)
                        except BlockingIOError:
                            break
                        if address[0] != '127.0.0.1':
                            continue
                        with self.lock:
                            direction = 0 if address == self.target else 1
                            if direction:
                                if self.client is not None and self.client != address:
                                    raise RuntimeError('Another client attempted to reuse an owned proxy')
                                self.client = address
                            destination = self.client if direction == 0 else self.target
                            self._record(direction, data)
                            if destination is None:
                                continue
                            profile = self.profile
                            self.ordinals[direction] += 1
                            ordinal = self.ordinals[direction]
                            now = time.monotonic()
                            departure = now
                            if profile.bits_second:
                                departure = max(now, self.next_departure[direction])+len(data)*8/profile.bits_second
                                self.next_departure[direction] = departure
                            if self.random.random() < profile.loss:
                                self.stats[direction]['dropped_datagrams'] += 1
                                continue
                            due = departure+profile.rtt_ms/2000
                            if self.random.random() < profile.reorder:
                                due += profile.rtt_ms*.00075+.005
                            self._enqueue(due, data, destination, direction, ordinal)
                            if self.random.random() < profile.duplicate:
                                self.stats[direction]['injected_duplicate_datagrams'] += 1
                                self._enqueue(due+.002, data, destination, direction, ordinal)
                with self.lock:
                    now = time.monotonic()
                    while self.queue and self.queue[0][0] <= now:
                        _, _, data, destination, direction, ordinal = heapq.heappop(self.queue)
                        self.queue_bytes -= len(data)
                        self.socket.sendto(data, destination)
                        self.stats[direction]['forwarded_udp_payload_bytes'] += len(data)
                        if ordinal < self.last_delivered[direction]:
                            self.stats[direction]['reordered_deliveries'] += 1
                        self.last_delivered[direction] = max(ordinal, self.last_delivered[direction])
        except Exception as error:
            with self.lock:
                self.error = str(error)
