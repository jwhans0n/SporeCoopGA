"""Passive native attack evidence must retain identity and reliable duplicates."""
from collections import Counter
import struct
import threading
import unittest

from coop_adventure import FixtureError
from coop_adventure_guest_attack import HitAudit, messages, project


def packet(payload, kind=52, reliable=True):
    header = bytearray(31)
    header[:4] = b'SPCO'
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

    def test_projection_matches_native_camera_row_convention(self):
        view = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        state = view, (.5, .5), (0, 0, 1024, 768)
        x, y = project(state, (1, 10, 2))
        self.assertAlmostEqual(x, 614.4)
        self.assertAlmostEqual(y, 230.4)
        with self.assertRaises(FixtureError):
            project(state, (1, -10, 2))


if __name__ == '__main__':
    unittest.main()
