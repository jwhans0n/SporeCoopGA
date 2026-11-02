"""Passive marker reader contracts; never opens process or native handles."""
import struct
import unittest

from coop_editor_marker_rpm import MODE_PTR, sample_marker


class MarkerReaderContract(unittest.TestCase):
    def fixture(self):
        memory={}
        def put(address,raw):
            for index,value in enumerate(raw):memory[address+index]=value
        put(MODE_PTR,struct.pack('<I',0x10000))
        mode=bytearray(0xE0);struct.pack_into('<I',mode,0xCC,1);struct.pack_into('<I',mode,0x74,0x20000);put(0x10000,mode)
        data=bytearray(0x88);struct.pack_into('<I',data,0x10,0x30000);put(0x20000,data)
        resource=bytearray(0x18);struct.pack_into('<III',resource,8,101,103,107);put(0x30000,resource)
        pool=bytearray(0x1C);struct.pack_into('<II',pool,0,0x40000,0x40000+254*0x238);put(0x32C10,pool)
        address=0x40000+253*0x238;row=bytearray(0x238);struct.pack_into('<I',row,4,17)
        struct.pack_into('<3f4ff',row,8,1,2,3,-.588576674,-.170447186,.266058296,.744144559,2)
        struct.pack_into('<II',row,4+0x28,0x70000,0x70034);struct.pack_into('<I',row,4+0x224,16);put(address,row)
        act=bytearray(0x34);struct.pack_into('<II',act,0x1C,0x71000,0x7100C);put(0x70000,act);put(0x71000,struct.pack('<3f',4,5,6))
        class Reader:
            base=0x400000;calls=0;bytes=0
            def read(self,address,size):
                self.calls+=1;self.bytes+=size
                return bytes(memory[address+n] for n in range(size))
            def pointer(self,address):return struct.unpack('<I',self.read(address,4))[0]
        return Reader(),memory,address

    def actor_fixture(self):
        reader,memory,address=self.fixture();reader.image_size=0x1000000
        def put(at,raw):
            for n,value in enumerate(raw):memory[at+n]=value
        lookup=bytes.fromhex('8b44240883ec08568b3033d2578b79088bc6f7f78b49048b04918d149185c0740b3b30742e8b400885c075f5')
        put(0x645E40,lookup);put(0x200B0,struct.pack('<IIII',0,0x80000,5,0))
        put(address+4+0x228,struct.pack('<I',253));put(0x8000C,struct.pack('<I',0x81000));put(0x80014,struct.pack('<I',0x8FFFF))
        put(0x81000,struct.pack('<III',258,0,0x81020));put(0x81020,struct.pack('<III',253,0x82000,0))
        header=bytearray(0x80);struct.pack_into('<I',header,0,0x430000);put(0x82000,header)
        table=bytearray(0x80);struct.pack_into('<I',table,0x0C,0x440000);struct.pack_into('<I',table,0x20,0x450000);put(0x430000,table)
        put(0x440000,bytes([0xCC])*0x80);put(0x450000,bytes([0xC3])*0x80)
        return reader,memory,address

    def test_exact_raw_pose_and_companions_without_claiming_bindings(self):
        reader,unused,address=self.fixture();result=sample_marker(reader)
        self.assertEqual(result['rotation']['bits'],['BF16ACF6','BE2E89B5','3E8838CB','3F3E8042'])
        self.assertEqual(result['position']['values'],[1,2,3]);self.assertEqual(result['scale']['values'],[2])
        self.assertEqual(result['class_index'],17);self.assertEqual(result['marker'],address+4)
        self.assertEqual(result['default_logical_id'],4294967549);self.assertFalse(result['logical_binding_verified'])
        self.assertEqual(len(result['companions']),7);self.assertEqual(result['act_count'],1)
        self.assertEqual(result['paths'][0]['points'],1);self.assertEqual(result['mutations'],0)

    def test_all_owner_row_and_act_data_drift_reject(self):
        for at in (MODE_PTR,0x10074,0x20010,0x30008,0x32C10,0x70000,0x71000):
            reader,memory,unused=self.fixture();original=reader.read;count=0
            def changing(address,size):
                nonlocal count
                if address<=at<address+size:
                    count+=1
                    if count==2:memory[at]^=1
                return original(address,size)
            reader.read=changing
            with self.subTest(address=at),self.assertRaises(ValueError):sample_marker(reader)
        reader,memory,address=self.fixture();original=reader.read;count=0
        def changing(at,size):
            nonlocal count
            if at==address:
                count+=1
                if count==2:memory[address+0x14]^=1
            return original(at,size)
        reader.read=changing
        with self.assertRaises(ValueError):sample_marker(reader)

    def test_bounds_and_free_slot_reject(self):
        for slot in (-1,4096,254):
            reader,unused,unused_address=self.fixture()
            with self.subTest(slot=slot),self.assertRaises(ValueError):sample_marker(reader,slot)
        reader,memory,address=self.fixture();memory[address+3]=0x80
        with self.assertRaises(ValueError):sample_marker(reader)
        for field in (0x32C14,0x70020):
            reader,memory,unused=self.fixture();memory[field]^=1
            with self.subTest(field=field),self.assertRaises(ValueError):sample_marker(reader)

    def test_exact_actor_hash_lookup_does_not_execute_getters(self):
        reader,unused,unused_address=self.actor_fixture();result=sample_marker(reader,include_actor=True)
        actor=result['actor_lookup'];self.assertEqual(actor['actor'],0x82000);self.assertEqual(actor['key'],253)
        self.assertEqual(actor['bucket'],3);self.assertEqual(len(actor['nodes']),2)
        self.assertEqual(actor['functions']['cast']['preferred'],0x440000);self.assertEqual(result['native_calls'],0)
        for at in (0x645E40,0x200B8,0x43000C):
            reader,memory,unused_address=self.actor_fixture();memory[at]^=1
            with self.subTest(address=at),self.assertRaises((ValueError,KeyError)):sample_marker(reader,include_actor=True)
        reader,memory,unused_address=self.actor_fixture();memory[0x81020]^=1
        self.assertEqual(sample_marker(reader,include_actor=True)['actor_lookup']['actor'],0)
        reader,memory,unused_address=self.actor_fixture()
        for n,value in enumerate(struct.pack('<III',258,0,0x81000)):memory[0x81000+n]=value
        with self.assertRaisesRegex(ValueError,'cyclic'):sample_marker(reader,include_actor=True)

    def test_actor_map_and_object_drift_reject(self):
        for at in (0x200B4,0x8000C,0x81008,0x82000,0x440020):
            reader,memory,unused_address=self.actor_fixture();original=reader.read;count=0
            def changing(address,size):
                nonlocal count
                if address<=at<address+size:
                    count+=1
                    if count==2:memory[at]^=1
                return original(address,size)
            reader.read=changing
            with self.subTest(address=at),self.assertRaises(ValueError):sample_marker(reader,include_actor=True)

    def test_verified_creature_spatial_pose_without_getter_calls(self):
        def fixture():
            reader,memory,address=self.actor_fixture();reader.image_size=0x2000000
            def put(at,raw):
                for n,value in enumerate(raw):memory[at+n]=value
            put(0x82000,struct.pack('<I',0x146A080));table=bytearray(0x80)
            struct.pack_into('<I',table,0x0C,0x440000);struct.pack_into('<I',table,0x20,0xC03270);put(0x146A080,table)
            put(0xC03270,bytes.fromhex('b85eb48e01c3')+bytes([0xCC])*122)
            spatial=bytearray(0x80);struct.pack_into('<I',spatial,0,0x1469EC0);struct.pack_into('<3f4ff',spatial,4,1,2,3,0,0,0,1,2);put(0x820C0,spatial)
            table=bytearray(0x80);struct.pack_into('<I',table,0x2C,0x6E6560);struct.pack_into('<I',table,0x30,0x97E7C0)
            struct.pack_into('<I',table,0x3C,0xC0DF10);put(0x1469EC0,table)
            put(0x6E6560,bytes.fromhex('8d4104c3'));put(0x97E7C0,bytes.fromhex('8d4110c3'))
            return reader,memory,address
        reader,unused,unused_address=fixture();result=sample_marker(reader,creature_spatial=True)
        self.assertEqual(result['actor_lookup']['spatial']['rotation']['bits'],['00000000']*3+['3F800000'])
        self.assertEqual(result['actor_lookup']['spatial']['orientation_setter_preferred'],0xC0DF10)
        self.assertNotIn('scale',result['actor_lookup']['spatial'])
        self.assertEqual(result['actor_lookup']['spatial']['uninterpreted_word20'],'40000000')
        for at in (0x82000,0xC03270,0x820C0,0x1469EF0,0x97E7C0):
            reader,memory,unused_address=fixture();memory[at]^=1
            with self.subTest(address=at),self.assertRaises((ValueError,KeyError)):sample_marker(reader,creature_spatial=True)


if __name__=='__main__':unittest.main()
