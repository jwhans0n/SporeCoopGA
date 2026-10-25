"""Guard natural targeting evidence against absent or distant host puppets."""
import unittest
import struct

from coop_adventure_enemy_targeting import analyze, behavior_tree, deque_count, scenario_sensor, sensed_records


def sample(wall, *, near=(2, 0, 0), owner=(2, 0, 0), target=None, hp=100, records=()):
    host = {0: dict(position=[200, 0, 0], health=100),
            1: dict(position=near, health=hp)}
    guest = {0: dict(position=[200, 0, 0], health=100),
             1: dict(position=owner, health=hp)}
    return dict(wall=wall, enemy_position=[0, 0, 0], peers=[host, guest],
                target_player=target, target_combatant=0, sensed=dict(records=list(records)))


class EnemyTargetingTests(unittest.TestCase):
    def test_scenario_sensor_contains_spatial_interfaces(self):
        class Memory:
            def read(self, address, size):
                if address == 0x1006D8 and size == 12:
                    return struct.pack('<3I', 0x300000, 0x300008, 0x300010)
                if address == 0x300000 and size == 8:
                    return struct.pack('<2I', 0x2000C0, 0x400000)
                raise AssertionError((address, size))

        result = scenario_sensor(Memory(), 0x100000, {0: 0x200000, 1: 0x400000})
        self.assertEqual(result['captain_players'], [0])

    def test_behavior_children_are_inline_nodes_with_native_stride(self):
        class Memory:
            def pointer(self, address):
                return {0x100B4C: 0x200000, 0x200000: 0x300000}[address]

            def read(self, address, size):
                self.assert_size(size)
                raw = bytearray(0x88)
                struct.pack_into('<I', raw, 0, address)
                if address == 0x300000:
                    struct.pack_into('<II', raw, 0x7C, 2, 0x400000)
                elif address not in (0x400000, 0x400088):
                    raise AssertionError(hex(address))
                return raw

            def assert_size(self, size):
                if size != 0x88:
                    raise AssertionError(size)

        result = behavior_tree(Memory(), 0x100000)
        self.assertEqual([node['address'] for node in result['nodes']], [0x300000, 0x400000, 0x400088])
        self.assertEqual(result['errors'], [])

    def test_sensed_vector_dereferences_four_byte_record_pointers(self):
        class Memory:
            def read(self, address, size):
                if address == 0x101124 and size == 8:
                    return struct.pack('<II', 0x200000, 0x200008)
                if address in (0x300000, 0x400000) and size == 0x14:
                    return struct.pack('<fBB2xI4xI', 2.5, 1, 0, address+0x100, address+0x100)
                raise AssertionError((address, size))

            def pointer(self, address):
                return {0x200000: 0x300000, 0x200004: 0x400000}[address]

        records = sensed_records(Memory(), 0x100000)['records']
        self.assertEqual([record['address'] for record in records], [0x300000, 0x400000])
        self.assertEqual([record['target'] for record in records], [0x300100, 0x400100])

    def test_owner_proximity_does_not_hide_distant_host_puppet(self):
        rows = [sample(0, near=(100, 0, 0)), sample(1, near=(100, 0, 0))]
        report = analyze(rows, 1, 100, {0: 0x100000, 1: 0x200000})
        self.assertEqual(report['actual_host_near_seconds'], 0)
        self.assertEqual(report['owner_host_pose_error_max'], 98)

    def test_target_and_natural_health_evidence_are_separate(self):
        rows = [sample(0, target=1), sample(.1, target=1, hp=90), sample(.2, target=1, hp=90)]
        rows[0]['target_getter_player'] = 1
        report = analyze(rows, 1, 100, {0: 0x100000, 1: 0x200000})
        self.assertEqual(report['target_samples'], 3)
        self.assertEqual(report['checked_getter_target_samples'], 1)
        self.assertEqual(report['natural_owner_health_loss'], 10)

    def test_sensed_duplicates_are_counted_per_native_record(self):
        record = dict(target=0x200000, reference=0x200000, sight=0)
        rows = [sample(0, records=[record, record]), sample(.1)]
        report = analyze(rows, 1, 100, {0: 0x100000, 1: 0x200000})
        self.assertEqual(report['sensed_near_captain_records_max'], 2)

    def test_sight_continuity_does_not_bridge_an_unobserved_gap(self):
        record = dict(target=0x200000, reference=0x200000, sight=1)
        rows = [sample(0, records=[record]), sample(.1, records=[record]),
                sample(.2, records=[record]), sample(1, records=[record]), sample(1.1)]
        report = analyze(rows, 1, 100, {0: 0x100000, 1: 0x200000})
        self.assertAlmostEqual(report['longest_consecutive_native_sight_seconds'], .2)

    def test_deque_count_crosses_native_eight_record_blocks(self):
        # Begin has consumed six records in one block; end has two in next.
        self.assertEqual(deque_count([0x100078, 0x100000, 0x1000A0, 0x300000,
                                      0x200028, 0x200000, 0x2000A0, 0x300004]), 4)

    def test_malformed_native_deque_read_is_unknown(self):
        self.assertIsNone(deque_count([0x100001, 0x100000, 0x1000A0, 0x300000,
                                      0x200028, 0x200000, 0x2000A0, 0x300004]))


if __name__ == '__main__':
    unittest.main()
