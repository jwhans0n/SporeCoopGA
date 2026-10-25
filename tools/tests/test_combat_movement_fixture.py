"""Guard against a spatial-only test falsely accepting a frozen graphics model."""
import unittest
from types import SimpleNamespace

from coop_adventure_combat_movement import MineMemory, actors, summarize


class MineTraceTests(unittest.TestCase):
    def test_same_marker_siblings_keep_host_incarnation(self):
        parsed = actors('marker=7 incarnation=415 noun=26129502 object=0x222000 class=1082\n'
                        'marker=7 incarnation=479 noun=26129502 object=0x333000 class=1082 host_incarnation=445\n')
        self.assertEqual([(a['marker'], a['host_incarnation']) for a in parsed], [(7, 415), (7, 445)])
        self.assertEqual(parsed[1]['incarnation'], 479)

    def test_live_kill_target_discovers_late_low_marker_spawn(self):
        # Native actor lists put spawned siblings after authored actors. Broad
        # bounded batches may never reach this row despite its low marker ID.
        memory = MineMemory.__new__(MineMemory)
        memory.generator = None
        memory.memory = SimpleNamespace(goals=lambda: dict(goals=[dict(type=3, targets=[56])]))
        memory.game = SimpleNamespace(call=lambda command:
            'marker=56 incarnation=4474 noun=26129502 object=0x222000 class=1044 host_incarnation=3573\n'
            if command == 'coop.adventure.world actorsat 56 48' else '')
        found = memory.discover()
        self.assertEqual([(a['marker'], a['host_incarnation']) for a in found], [(56, 3573)])

    def test_equal_replicated_coordinates_do_not_hide_a_frozen_model(self):
        samples = []
        for x in (0, 10, 20, 30):
            h = dict(marker=7, host_incarnation=415, spatial=[x, 0, 0],
                     model_position=[x, 0, 0], model_spatial_error=0)
            g = dict(h, model_position=[0, 0, 0], model_spatial_error=x)
            samples.append(dict(host=dict(actors=[h], captain_position=[0, 0, 0]),
                                guest=dict(actors=[g], captain_position=[0, 0, 0])))
        host, guest = summarize(samples)
        self.assertEqual(host['moving_incarnations'][0]['model_displacement'], 30)
        self.assertEqual(guest['moving_incarnations'][0]['spatial_displacement'], 30)
        self.assertEqual(guest['moving_incarnations'][0]['model_displacement'], 0)
        self.assertGreater(guest['moving_incarnations'][0]['error_p95'], 5)


if __name__ == '__main__':
    unittest.main()
