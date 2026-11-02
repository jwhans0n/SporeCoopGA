"""Offline contract checks: no process handles or game commands are opened."""
import struct
import unittest

from coop_editor_terrain_properties_rpm import bounded_vector, property_rows, transform_property


def header(count):
    result = bytearray(0x38)
    struct.pack_into('<III', result, 0x18, 0x10000, 0x10000+count*24, 0x10000+count*24)
    return bytes(result)


def entry(ident=0x02A907B6, pointer=0x20000, stride=56, count=2, flags=0x9C, kind=0x38):
    return struct.pack('<IIII4xHH', ident, pointer, stride, count, flags, kind)


class TerrainPropertiesContract(unittest.TestCase):
    def test_sorted_exact_properties(self):
        first = entry(); second = entry(ident=0x03A90C5F)
        rows = property_rows(header(2), first+second)
        self.assertEqual(list(rows), [0x02A907B6, 0x03A90C5F])
        self.assertEqual(transform_property(rows[0x02A907B6]), dict(pointer=0x20000, stride=56, count=2, flags=0x9C, type=0x38))

    def test_bad_vector_shapes(self):
        for args in [(0x10000,0x10010,0x10018,24,4096), (0x10000,0x10018,0x10000,24,4096),
                     (1,25,25,24,4096), (0x10000,0x10018,0x28018,24,4096),
                     (0x10000,0x10018,0x10019,24,4096), (0x10000,0x10018,1<<32,24,4096)]:
            with self.subTest(args=args), self.assertRaises(ValueError): bounded_vector(*args)
        self.assertEqual(bounded_vector(0,0,0,24,4096), 0)
        self.assertEqual(bounded_vector(0x10000,0x10018,0x10030,24,4096), 1)

    def test_bad_property_shapes(self):
        for rows in [entry()+entry(), entry(ident=3)+entry(ident=2), entry()[:-1]]:
            with self.subTest(size=len(rows)), self.assertRaises(ValueError): property_rows(header(2 if len(rows)==48 else 1), rows)
        with self.assertRaises(ValueError): property_rows(header(0)[:-1], b'')
        for changed in [dict(stride=55), dict(count=8193), dict(flags=0x1C), dict(flags=0x80), dict(kind=0x20),
                        dict(pointer=1), dict(pointer=0xFFFFFFF0), dict(count=8192,pointer=0xFFF90000)]:
            with self.subTest(changed=changed), self.assertRaises(ValueError): transform_property(entry(**changed))
        self.assertEqual(transform_property(entry(pointer=0,count=0))['count'], 0)
        with self.assertRaises(ValueError): transform_property(entry()[:-1])


if __name__ == '__main__': unittest.main()
