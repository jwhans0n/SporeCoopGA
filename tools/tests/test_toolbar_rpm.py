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
        object_header=bytearray(0x80);struct.pack_into('<I',object_header,0,0x410000);put(0x50000,object_header)
        table=bytearray(0x80);struct.pack_into('<I',table,0x0C,0x420000);put(0x410000,table);put(0x420000,bytes([0xCC])*0x80)
        class FakeReader:
            base=0x400000;image_size=0x300000;calls=0;bytes=0
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

    def test_root_headers_are_passive_bounded_and_module_checked(self):
        reader,unused=self.fixture();row=sample_toolbar(reader,True)
        self.assertEqual(len(row['owned_objects']),1);self.assertEqual(row['owned_objects'][0]['cast_preferred'],0x420000)
        self.assertEqual(row['owned_objects'][0]['cast_code'],'cc'*0x80)
        for address in (0x50000,0x41000C):
            reader,memory=self.fixture();memory[address+2]=0
            with self.subTest(address=address),self.assertRaises(ValueError):sample_toolbar(reader,True)

    def test_raw_topology_requires_verified_interface_and_is_bounded(self):
        reader,memory=self.fixture((0x50000,));reader.image_size=0x800000
        def put(address,data):
            for n,byte in enumerate(data):memory[address+n]=byte
        def pointer(address,value):put(address,struct.pack('<I',value))
        pointer(0x41000C,0x7F3290);pointer(0x410010,0x95F930);pointer(0x41001C,0xA02C70)
        put(0x7F3290,bytes.fromhex('83e904e928c61600')+bytes([0xCC])*120)
        put(0x95F930,bytes.fromhex('8b413485c0740483c004c333c0c3'));put(0xA02C70,bytes.fromhex('8b417cc3'))
        pointer(0x50038,0x51004);pointer(0x5003C,0x51004)
        child=bytearray(0x80);struct.pack_into('<I',child,0,0x410000)
        struct.pack_into('<II',child,4,0x50038,0x50038);struct.pack_into('<I',child,0x34,0x4FFFC)
        struct.pack_into('<II',child,0x38,0x51038,0x51038);struct.pack_into('<I',child,0x7C,0xB006EF6E);put(0x51000,child)
        row=sample_toolbar(reader,topology=True);self.assertTrue(row['topology']['complete'])
        self.assertEqual(row['topology']['node_count'],2);self.assertEqual(row['topology']['edge_count'],1)
        self.assertEqual(row['topology']['undo'],[0x51000])
        pointer(0x51004,0x51004);reader.calls=reader.bytes=0
        row=sample_toolbar(reader,topology=True);self.assertFalse(row['topology']['complete'])
        self.assertTrue(any('cyclic' in error for error in row['topology']['errors']))
        for address in (0x50020,0x410020,0x420020):
            reader,memory=self.fixture();original=reader.read;seen=0
            def changed(at,size):
                nonlocal seen
                if at<=address<at+size:
                    seen+=1
                    if seen==2:memory[address]^=1
                return original(at,size)
            reader.read=changed
            with self.subTest(address=address),self.assertRaises(ValueError):sample_toolbar(reader,True)

    def test_bounds_malformed_span_and_pointer(self):
        for pointers in ((1,),tuple(0 for unused in range(4097))):
            reader,unused=self.fixture(pointers)
            with self.subTest(count=len(pointers)),self.assertRaises(ValueError):sample_toolbar(reader)
        reader,memory=self.fixture();memory[0x60068]^=1
        with self.assertRaises(ValueError):sample_toolbar(reader)


if __name__=='__main__':unittest.main()
