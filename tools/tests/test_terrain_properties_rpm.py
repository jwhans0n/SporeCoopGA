"""Offline contract checks: no process handles or game commands are opened."""
import struct
import unittest

from coop_editor_terrain_properties_rpm import MODE_PTR, bounded_vector, property_rows, sample, transform_property


def header(count):
    result = bytearray(0x38)
    struct.pack_into('<III', result, 0x18, 0x10000, 0x10000+count*24, 0x10000+count*24)
    return bytes(result)


def entry(ident=0x02A907B6, pointer=0x20000, stride=56, count=2, flags=0x9C, kind=0x38):
    return struct.pack('<IIII4xHH', ident, pointer, stride, count, flags, kind)


class TerrainPropertiesContract(unittest.TestCase):
    def fixture(self):
        memory = {}
        def add(address, value):
            for n, byte in enumerate(value): memory[address+n] = byte
        def pointer(address, value): add(address, struct.pack('<I', value))
        pointer(MODE_PTR,0x10000); mode=bytearray(0xDC)
        struct.pack_into('<I',mode,0x18,0x30000); struct.pack_into('<I',mode,0x74,0x20000)
        struct.pack_into('<I',mode,0xCC,1); add(0x10000,mode); pointer(0x20010,0x80000)
        resource=bytearray(0x18); struct.pack_into('<III',resource,8,123,0x366A930D,0x408A0000); add(0x80000,resource)
        tool=bytearray(0x1CC); struct.pack_into('<I',tool,0xC,0x40000)
        struct.pack_into('<III',tool,0x28,0x72000,0x72038,0x72038)
        struct.pack_into('<III',tool,0xB4,0x73000,0x73038,0x73038); add(0x30000,tool)
        prop=bytearray(header(2)); struct.pack_into('<I',prop,0,0x50000)
        struct.pack_into('<III',prop,0x18,0x60000,0x60030,0x60030); add(0x40000,prop)
        pointer(0x50014,0x6A2E20); add(0x60000,entry(pointer=0x70000,count=1)+entry(ident=0x03A90C5F,pointer=0x71000,count=1))
        transform=bytes.fromhex('07000100')+bytes(52); add(0x70000,transform); add(0x71000,transform)
        live=bytes.fromhex('07000200')+bytes(52); add(0x72000,live); add(0x73000,transform)
        class FakeReader:
            base=0x400000; calls=0; bytes=0
            def read(self,address,size):
                self.calls+=1;self.bytes+=size
                return bytes(memory[address+n] for n in range(size))
            def pointer(self,address):return struct.unpack('<I',self.read(address,4))[0]
        return FakeReader(),memory

    def test_complete_passive_sample_and_exact_counter_difference(self):
        reader,unused=self.fixture();result=sample(reader)
        self.assertEqual(result['source'],[123,0x366A930D,0x408A0000])
        self.assertFalse(result['arrays']['effects']['exact_equal'])
        self.assertEqual(result['arrays']['effects']['counters'],[1])
        self.assertEqual(result['arrays']['effects']['live_counters'],[2])
        self.assertTrue(result['arrays']['models']['exact_equal'])

    def test_sample_rejects_owner_payload_and_implementation_drift(self):
        for address in [0x80008,0x70002,0x3000C,0x40034]:
            reader,memory=self.fixture();original=reader.read;seen=0
            def changing(at,size):
                nonlocal seen
                if at<=address<at+size:
                    seen+=1
                    if seen==2:memory[address]^=1
                return original(at,size)
            reader.read=changing
            with self.subTest(address=address),self.assertRaises(ValueError):sample(reader)
        reader,memory=self.fixture();memory[0x50014]^=1
        with self.assertRaises(ValueError):sample(reader)

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
