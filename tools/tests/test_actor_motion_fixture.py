"""Keep clip loop clocks distinct from replacing a running native channel."""
import struct
import unittest

from coop_adventure_actor_motion import clip_summary, current_clip, peer_summary


class Memory:
    def __init__(self, values):
        self.values = values

    def pointer(self, address):
        return self.values.get(address, 0)

    def read(self, address, size):
        return struct.pack('<d', self.values[address])


class ActorMotionTests(unittest.TestCase):
    def test_current_clip_uses_query_pointer_and_selected_layer_slot(self):
        animated, query, index, layer = 0x200000, 0x300000, 3, 2
        slot = query+8+index*0xF0
        memory = Memory({animated+0x184: query, query+0xFA4: layer,
                         query+0xF0C+4*layer: index, slot: 0x400000,
                         slot+0xD8: 123, slot+0xA8: 1.25, slot+0xB4: 8})
        clip = current_clip(memory, animated)
        self.assertEqual((clip['slot'], clip['clip'], clip['time'], clip['generation']),
                         (slot, 123, 1.25, 8))

    def test_loop_clock_wrap_is_not_a_replacement(self):
        history = [(dict(wall=wall), dict(clip=dict(clip=123, slot=0x300000,
                                                   generation=generation, time=clock)))
                   for wall, generation, clock in ((0, 4, .9), (.1, 4, .05), (.2, 5, .02))]
        report = clip_summary(history)
        self.assertEqual(len(report['retained_slot_clock_regressions']), 1)
        self.assertEqual(len(report['same_clip_slot_replacements']), 1)
        self.assertEqual(report['same_clip_slot_replacements'][0]['generation'], 5)

    def test_different_clip_is_a_transition(self):
        history = [(dict(wall=wall), dict(clip=dict(clip=clip, slot=0x300000,
                                                   generation=generation, time=clock)))
                   for wall, clip, generation, clock in ((0, 123, 4, .9), (.1, 456, 5, .05))]
        report = clip_summary(history)
        self.assertEqual(len(report['clip_transitions']), 1)
        self.assertFalse(report['same_clip_slot_replacements'])

    def test_own_model_alignment_does_not_hide_authority_drift(self):
        host = dict(kind='vehicle', marker=9, host_incarnation=123,
                    spatial=[0, 0, 0], model_position=[0, 0, 0])
        guest = dict(host, spatial=[20, 0, 0], model_position=[20, 0, 0])
        report = peer_summary([dict(host=[host], guest=[guest])])[0]
        self.assertEqual(report['spatial_error_p95'], 20)
        self.assertEqual(report['model_error_p95'], 20)

    def test_live_error_excludes_native_dead_samples(self):
        samples = []
        for offset, dead in ((1, False), (50, True), (100, True)):
            host = dict(kind='creature', marker=7, host_incarnation=123,
                        spatial=[0, 0, 0], model_position=[0, 0, 0], dead=dead)
            guest = dict(host, spatial=[offset, 0, 0], model_position=[offset, 0, 0])
            samples.append(dict(host=[host], guest=[guest]))
        report = peer_summary(samples)[0]
        self.assertEqual(report['known_live_samples'], 1)
        self.assertEqual(report['live_spatial_error_p95'], 1)
        self.assertEqual(report['spatial_error_max'], 100)


if __name__ == '__main__':
    unittest.main()
