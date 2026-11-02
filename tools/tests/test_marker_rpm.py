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


if __name__=='__main__':unittest.main()
