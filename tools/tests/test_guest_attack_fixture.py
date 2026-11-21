"""Passive native attack evidence must retain identity and reliable duplicates."""
from collections import Counter
import struct
import threading
import unittest

from coop_adventure import FixtureError
from coop_adventure_guest_attack import HitAudit, messages, project
from coop_adventure_pickups import CaptureProxy, adventure_injection_packet


def packet(payload, kind=52, reliable=True, version=23):
    header = bytearray(31)
    header[:4] = b'SPCO'
    struct.pack_into('<H', header, 4, version)
    header[30] = 1
    return bytes(header)+struct.pack('<BBH', kind, reliable, len(payload))+(b'\x01\x00' if reliable else b'')+payload


class GuestAttackFixtureTests(unittest.TestCase):
    def audit(self):
        # The parser checks are offline; do not allocate a live UDP socket.
        value = HitAudit.__new__(HitAudit)
        value.lock = threading.Lock()
        value.counts = Counter()
        value.hits = {}
        return value

    def test_native_target_identity_and_retransmission_are_distinct(self):
        audit = self.audit()
        data = packet(bytes([130])+struct.pack('<QQIIfI', 41, 7, 56, 12345, 90.0, 0))
        audit._record('up', data)
        audit._record('up', data)
        events = audit.audit()
        self.assertEqual(len(events), 1)
        self.assertEqual((events[0]['marker'], events[0]['incarnation']), (56, 12345))
        self.assertEqual(events[0]['wire_receptions'], 2)
        self.assertEqual(events[0]['damage'], 90)

    def test_truncated_or_nonreliable_payload_is_not_attack_evidence(self):
        audit = self.audit()
        data = packet(bytes([130])+struct.pack('<QQIIfI', 41, 7, 56, 12345, 90.0, 0))
        audit._record('up', data[:-1])
        audit._record('down', data)
        audit._record('up', packet(data[-33:], reliable=False))
        self.assertFalse(audit.audit())
        self.assertFalse(list(messages(data[:34])))

    def test_captain_callback_cannot_be_mistaken_for_npc_hit(self):
        audit = self.audit()
        audit._record('up', packet(bytes([131])+struct.pack('<QQBBfI', 41, 8, 1, 255, 3.0, 7)))
        self.assertEqual(audit.audit()[0]['kind'], 'captain')
        self.assertEqual(audit.audit()[0]['victim'], 1)

    def test_versioned_adventure_hit_id_does_not_accept_nest_or_future_id(self):
        payload = bytes([130])+struct.pack('<QQIIfI', 41, 7, 56, 12345, 90.0, 0)
        for version in range(7, 27):
            with self.subTest(version=version):
                audit = self.audit()
                correct, wrong = (63, 61) if version >= 26 else (61, 52) if version >= 24 else (52, 61)
                audit._record('up', packet(payload, kind=wrong, version=version))
                self.assertFalse(audit.audit())
                audit._record('up', packet(payload, kind=correct, version=version))
                self.assertEqual(len(audit.audit()), 1)
                self.assertEqual(audit.audit()[0]['marker'], 56)
                self.assertEqual(audit.audit()[0]['wire_receptions'], 1)
        for version in (0, 6, 27, 65535):
            with self.subTest(invalid_version=version):
                audit = self.audit()
                for kind in (52, 61, 63):
                    data = packet(payload, kind=kind, version=version)
                    audit._record('up', data)
                    self.assertFalse(list(messages(data)))
                self.assertFalse(audit.audit())

    def test_versioned_network_accounting_preserves_exact_actor_identity(self):
        payload = bytearray(15+54)
        payload[0], payload[14] = 128, 1
        struct.pack_into('<II', payload, 15, 56, 12345)
        for version, correct, wrong in ((7, 52, 61), (23, 52, 61), (24, 61, 52), (25, 61, 52), (26, 63, 61)):
            with self.subTest(version=version):
                audit = self.audit()
                audit._record('down', packet(payload, kind=wrong, version=version))
                self.assertEqual(audit.counts['down_actor_records'], 0)
                self.assertEqual(audit.counts['down_other_messages'], 1)
                audit._record('down', packet(payload, kind=correct, version=version))
                self.assertEqual(audit.counts['down_actors_messages'], 1)
                self.assertEqual(audit.counts['down_actor_records'], 1)
                self.assertEqual(audit.counts['down_actor_56_12345'], 1)
                self.assertEqual(audit.counts['down_packets'], 2)
                self.assertEqual(audit.counts['down_bytes'], 2*len(packet(payload)))

    def test_projection_matches_native_camera_row_convention(self):
        view = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        state = view, (.5, .5), (0, 0, 1024, 768)
        x, y = project(state, (1, 10, 2))
        self.assertAlmostEqual(x, 614.4)
        self.assertAlmostEqual(y, 230.4)
        with self.assertRaises(FixtureError):
            project(state, (1, -10, 2))

    def test_pickup_injection_uses_the_same_owned_observed_header(self):
        class Socket:
            def __init__(self):
                self.sent = []

            def sendto(self, data, destination):
                self.sent.append((data, destination))

        for version, expected in ((10, 52), (23, 52), (24, 61), (25, 61), (26, 63)):
            with self.subTest(version=version):
                proxy = CaptureProxy.__new__(CaptureProxy)
                proxy.lock, proxy.counts = threading.Lock(), Counter()
                proxy.states, proxy.history, proxy.error = {}, {}, None
                proxy.socket, proxy.target = Socket(), ('127.0.0.1', 9)
                state = bytearray(95)
                state[0] = 2
                struct.pack_into('<Q', state, 71, 41)
                struct.pack_into('<Q', state, 87, 3)
                observed = packet(state, kind=6, version=version)
                proxy._record('up', observed)
                self.assertEqual(proxy.state(2)['run'], 41)
                payload = bytes([152])+b'owned-negative-test'
                length = proxy.inject_adventure(2, payload)
                self.assertEqual(len(proxy.socket.sent), 1)
                actual, destination = proxy.socket.sent[0]
                self.assertEqual(destination, proxy.target)
                self.assertEqual(length, len(actual))
                self.assertEqual(actual[:31], observed[:31])
                self.assertEqual(list(messages(actual)), [(expected, False, payload)])
                for invalid in (b'', observed[:30], observed[:31]+b'x',
                                packet(state, kind=6, version=9)[:31],
                                packet(state, kind=6, version=27)[:31]):
                    proxy.state = lambda player, bad=invalid: {'header': bad}
                    with self.assertRaises(FixtureError):
                        proxy.inject_adventure(2, payload)
                self.assertEqual(len(proxy.socket.sent), 1)

    def test_pickup_injection_rejects_unsupported_and_bad_magic_headers(self):
        for version in (0, 7, 9, 27, 65535):
            with self.subTest(version=version):
                with self.assertRaises(FixtureError):
                    adventure_injection_packet(packet(b'', version=version)[:31], b'event')
        with self.assertRaises(FixtureError):
            adventure_injection_packet(b'NOPE'+packet(b'')[4:31], b'event')


if __name__ == '__main__':
    unittest.main()
