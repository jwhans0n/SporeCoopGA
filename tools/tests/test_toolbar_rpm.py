"""Offline passive-toolbar contracts; never opens process/native handles."""
import struct
import unittest

from coop_editor_toolbar_rpm import MODE_PTR, sample_toolbar


class ToolbarInventoryContract(unittest.TestCase):
    def fixture(self,pointers=(0x50000,0x50000,0)):
        memory={}
        def put(address,data):
            for n,byte in enumerate(data):memory[address+n]=byte
        def pointer(address,value):put(address,struct.pack('<I',value))
        pointer(MODE_PTR,0x10000);mode=bytearray(0xE0);struct.pack_into('<I',mode,0xCC,1)
        struct.pack_into('<I',mode,0x14,0x20000);struct.pack_into('<I',mode,0x74,0x30000);put(0x10000,mode)
        display=bytearray(0x2C);struct.pack_into('<I',display,0x24,0x40000);put(0x20000,display)
        layout=bytearray(0x18);struct.pack_into('<I',layout,0x14,0x60000);put(0x40000,layout)
        owner=bytearray(0x6C);struct.pack_into('<II',owner,0x64,0x70000,0x70000+len(pointers)*4);put(0x60000,owner)
        data=bytearray(0x88);struct.pack_into('<I',data,0x10,0x80000);put(0x30000,data)
        resource=bytearray(0x18);struct.pack_into('<III',resource,8,123,0x366A930D,0x408A0000);put(0x80000,resource)
        put(0x70000,struct.pack('<'+'I'*len(pointers),*pointers))
        class FakeReader:
            base=0x400000;calls=0;bytes=0
            def read(self,address,size):
                self.calls+=1;self.bytes+=size
                return bytes(memory[address+n] for n in range(size))
            def pointer(self,address):return struct.unpack('<I',self.read(address,4))[0]
        return FakeReader(),memory

    def test_complete_inventory_without_dereferencing_objects(self):
        reader,unused=self.fixture();row=sample_toolbar(reader)
        self.assertEqual(row['owned_pointers'],[0x50000,0x50000,0]);self.assertEqual(row['owned_object_count'],3)
        self.assertEqual(row['duplicate_objects'],{'327680':2});self.assertEqual(row['unique_nonnull_objects'],1)
        self.assertEqual(row['null_objects'],1);self.assertEqual(row['source'],[123,0x366A930D,0x408A0000])

    def test_owner_vector_and_source_drift_reject(self):
        for address in (0x10014,0x20024,0x40014,0x60064,0x70000,0x30010,0x80008):
            reader,memory=self.fixture();original=reader.read;seen=0
            def changed(at,size):
                nonlocal seen
                if at<=address<at+size:
                    seen+=1
                    if seen==2:memory[address]^=1
                return original(at,size)
            reader.read=changed
            with self.subTest(address=address),self.assertRaises(ValueError):sample_toolbar(reader)

    def test_bounds_malformed_span_and_pointer(self):
        for pointers in ((1,),tuple(0 for unused in range(4097))):
            reader,unused=self.fixture(pointers)
            with self.subTest(count=len(pointers)),self.assertRaises(ValueError):sample_toolbar(reader)
        reader,memory=self.fixture();memory[0x60068]^=1
        with self.assertRaises(ValueError):sample_toolbar(reader)


if __name__=='__main__':unittest.main()
