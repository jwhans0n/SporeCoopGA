"""Offline contract tests; no process, native function or profile access."""
import struct
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from coop_editor_complexity_fixture import (ACTS, CLASSES, CORE_KEY, MARKERS, MARKER_ACTS, SERIALIZER, OUT,
    attributes, main, native, package, pinned_file, record, refpack, replace_record, sha, splice_native)


def serialized(fields):
    directory = b''.join(struct.pack('>II',key,len(value)) for key,value in fields)
    payload = b''.join(value for unused,value in fields)
    return struct.pack('>III',SERIALIZER,4+len(directory)+len(payload),len(fields))+directory+payload


def literals(data):
    result = bytearray(b'\x10\xFB'+len(data).to_bytes(3,'big'))
    cursor = 0
    while len(data)-cursor >= 4:
        count = min((len(data)-cursor)//4*4,112)
        result.append(0xE0+count//4-1);result.extend(data[cursor:cursor+count]);cursor+=count
    result.append(0xFC+len(data)-cursor);result.extend(data[cursor:])
    return bytes(result)


def dbpf(rows, flags=4):
    """Independent DBPF3 index encoder including all constant-field shapes."""
    data = bytearray(96);data[:4]=b'DBPF';struct.pack_into('<I',data,4,3)
    keys = [key for key,unused,unused_compressed in rows]
    constants = [keys[0][2],keys[0][0],0]
    index = bytearray(struct.pack('<I',flags))
    for n,flag in enumerate((1,2,4)):
        if flags&flag:index.extend(struct.pack('<I',constants[n]))
    for key,raw,compressed in rows:
        offset = len(data);stored=literals(raw) if compressed else raw;data.extend(stored)
        for flag,value in zip((1,2,4),(key[2],key[0],0)):
            if not flags&flag:index.extend(struct.pack('<I',value))
        index.extend(struct.pack('<IIIIHH',key[1],offset,len(stored)|0x80000000,len(raw),0xFFFF if compressed else 0,1))
    struct.pack_into('<I',data,0x24,len(rows));struct.pack_into('<I',data,0x2C,len(index))
    struct.pack_into('<I',data,0x3C,3);struct.pack_into('<I',data,0x40,len(data));data.extend(index)
    return bytes(data)


def fixture(count=2, compressed=False):
    marker = serialized([(0x071147E5,struct.pack('>I',17)),
        (0x071147E6,struct.pack('>3f',1,2,3)),(0x071147E7,struct.pack('>4f',0,0,0,1)),
        (0x071147E8,struct.pack('>f',1)),(MARKER_ACTS,struct.pack('>I',1)+serialized([(0x07114773,b'full marker act subtree')])),
        (0xDEADBEEF,b'unknown marker payload\x00\xff')])
    core = serialized([(0x01234567,b'unknown root before'),(MARKERS,struct.pack('>I',count)+marker*count),
        (CLASSES,struct.pack('>II',1,17)+serialized([(0xABCDEF12,b'full class subtree')])),
        (ACTS,struct.pack('>I',1)+serialized([(0xABCDE123,b'full act subtree')])),
        (0x76543210,b'unknown root after')])
    inner = dbpf([(CORE_KEY,core,compressed)],7)
    prefix = b'opaque native metadata and planet properties\0\xff'+struct.pack('<I',len(inner))
    return prefix+inner


class ComplexityFixture(unittest.TestCase):
    def build(self,source,count,marker=0):
        old=native(source);row=old['markers'][marker]
        return splice_native(source,marker,row['class_index'],row['sha256'],count)

    def test_real_boundary_sizes_and_all_unknown_authored_values_retained(self):
        for count in (1023,1024,1025,4095,4096):
            with self.subTest(count=count):
                source=fixture(compressed=True);result,report=self.build(source,count)
                self.assertEqual(native(result)['count'],count)
                self.assertEqual(native(source)['count'],2)
                self.assertTrue(report['retained_markers_byte_exact'])
                self.assertTrue(report['appended_markers_byte_exact'])
                self.assertTrue(report['dependency_references_unchanged'])
                self.assertFalse(report['native_parser_verified'])
                before=native(source);after=native(result)
                for key in (0x01234567,CLASSES,ACTS,0x76543210):
                    a,n,unused=before['attributes'][key];b,m,unused=after['attributes'][key]
                    self.assertEqual(before['decoded'][a:a+n],after['decoded'][b:b+m])

    def test_carrier_append_preserves_other_raw_records_and_metadata(self):
        source=fixture();root=(0x408A0000,0x29000001,0x366A930D)
        other=(0x408A0000,0x29000001,0x030BDEE3)
        metadata=b'unchanged pollen metadata and thumbnail dependency'*3
        carrier=dbpf([(root,source,True),(other,metadata,True)])
        generated,unused_report=self.build(source,4095)
        output=replace_record(carrier,root,generated)
        old,old_index,unused_size,unused_primary,unused_legacy=package(carrier)
        after,new_index,unused_size,unused_primary,unused_legacy=package(output)
        self.assertNotEqual(old_index,new_index)
        self.assertEqual(record(output,after[0]),generated)
        self.assertEqual(record(output,after[1]),metadata)
        self.assertEqual(carrier[old[1].offset:old[1].offset+old[1].size],output[after[1].offset:after[1].offset+after[1].size])
        # Only header index offset changes in the retained original file prefix.
        prefix=bytearray(output[:len(carrier)]);prefix[0x40:0x44]=carrier[0x40:0x44]
        self.assertEqual(bytes(prefix),carrier)

    def test_all_reviewed_dbpf_constant_flags_and_compressed_input(self):
        for flags in range(8):
            with self.subTest(flags=flags):
                source=dbpf([(CORE_KEY,b'payload'*30,True)],flags)
                changed=replace_record(source,CORE_KEY,b'new payload')
                entries,*unused=package(changed)
                self.assertEqual(record(changed,entries[0]),b'new payload')

    def test_wrong_marker_hash_class_count_and_overflow_reject(self):
        source=fixture();row=native(source)['markers'][0]
        for marker,cls,digest,count in [(2,17,row['sha256'],3),(0,18,row['sha256'],3),
            (0,17,'0'*64,3),(0,17,row['sha256'],2),(0,17,row['sha256'],4097),
            (-1,17,row['sha256'],3),(0,17,row['sha256'],-1)]:
            with self.subTest(values=(marker,cls,digest,count)),self.assertRaises(ValueError):
                splice_native(source,marker,cls,digest,count)

    def test_malformed_native_tlv_and_missing_references_reject(self):
        source=fixture();info=native(source);decoded=info['decoded'];attrs=info['attributes']
        marker=info['markers'][0];fields,unused_end=attributes(decoded,marker['start'])
        for offset,value in [(4,len(decoded)),(attrs[MARKERS][0],4097),
                             (fields[0x071147E5][0],18),(fields[0x071147E8][0],0x7F800000),
                             (fields[MARKER_ACTS][0],2)]:
            bad=bytearray(decoded);struct.pack_into('>I',bad,offset,value)
            inner=replace_record(source[info['base']:],CORE_KEY,bytes(bad))
            prefix=bytearray(source[:info['base']]);struct.pack_into('<I',prefix,len(prefix)-4,len(inner))
            with self.subTest(offset=offset),self.assertRaises(ValueError):native(bytes(prefix)+inner)
        with self.assertRaises(ValueError):native(source+b'trailer')
        bad=bytearray(source);bad[info['base']-4]^=1
        with self.assertRaises(ValueError):native(bytes(bad))

    def test_malformed_dbpf_shapes_and_conflicting_keys_reject(self):
        source=dbpf([(CORE_KEY,b'payload',False)],7)
        for offset,value in [(4,2),(0x24,16385),(0x2C,1),(0x40,0xFFFFFFF0),
                             (word_at(source,0x40),8)]:
            bad=bytearray(source);struct.pack_into('<I',bad,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):package(bytes(bad))
        with self.assertRaises(ValueError):replace_record(source,(1,2,3),b'payload')
        with self.assertRaises(ValueError):package(dbpf([(CORE_KEY,b'a',False),(CORE_KEY,b'b',False)],7))
        overlap=bytearray(dbpf([(CORE_KEY,b'aaaa',False),((1,2,3),b'bbbb',False)]))
        entries,*unused=package(overlap)
        struct.pack_into('<I',overlap,entries[1].location+4,entries[0].offset)
        with self.assertRaises(ValueError):package(overlap)
        for cut in (0,1,95,len(source)-1):
            with self.assertRaises(ValueError):package(source[:cut])

    def test_refpack_literal_lengths_terminators_and_bounds(self):
        for size in (1,2,3,4,7,112,113,1024):
            plain=bytes(n%251 for n in range(size));packed=literals(plain)
            self.assertEqual(refpack(packed,size),plain)
            for bad in (packed[:-1],packed+b'junk',b'\x10\xfb\x00\x00\x01\x00\x00\xfc'):
                with self.assertRaises(ValueError):refpack(bad,size)

    def test_hashed_out_inputs_and_new_output_only(self):
        with tempfile.TemporaryDirectory(prefix='complexity-fixture-contract-',dir=OUT) as temporary:
            folder=Path(temporary);source=fixture();path=folder/'copied.native';path.write_bytes(source)
            self.assertEqual(pinned_file(path,sha(source))[1],source)
            with self.assertRaises(ValueError):pinned_file(path,'0'*64)
            with self.assertRaises(ValueError):pinned_file(path,sha(source),1)
            with self.assertRaises(ValueError):pinned_file(Path(__file__),sha(Path(__file__).read_bytes()))
            row=native(source)['markers'][0];output=folder/'new-private-artifact'
            args=['fixture','--native',str(path),'--native-sha256',sha(source),'--marker','0',
                  '--marker-class',str(row['class_index']),'--marker-sha256',row['sha256'],'--count','3','--output',str(output)]
            with patch('sys.argv',args),contextlib.redirect_stdout(io.StringIO()):self.assertEqual(main(),0)
            self.assertEqual(path.read_bytes(),source)
            generated=(output/'scenario.native').read_bytes();self.assertEqual(native(generated)['count'],3)
            with patch('sys.argv',args),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):main()
            self.assertEqual((output/'scenario.native').read_bytes(),generated)

    def test_cli_rejects_carrier_with_another_native_root_before_output(self):
        with tempfile.TemporaryDirectory(prefix='complexity-carrier-contract-',dir=OUT) as temporary:
            folder=Path(temporary);source=fixture();path=folder/'copied.native';path.write_bytes(source)
            key=(0x408A0000,0x29000001,0x366A930D);carrier=dbpf([(key,fixture(3),True)])
            carrier_path=folder/'copied.package';carrier_path.write_bytes(carrier)
            row=native(source)['markers'][0];output=folder/'should-not-exist'
            args=['fixture','--native',str(path),'--native-sha256',sha(source),'--marker','0','--marker-class','17',
                  '--marker-sha256',row['sha256'],'--count','3','--output',str(output),
                  '--carrier',str(carrier_path),'--carrier-sha256',sha(carrier),'--carrier-key','408A0000!29000001.366A930D']
            with patch('sys.argv',args),contextlib.redirect_stderr(io.StringIO()),self.assertRaises(SystemExit):main()
            self.assertFalse(output.exists());self.assertEqual(path.read_bytes(),source);self.assertEqual(carrier_path.read_bytes(),carrier)


def word_at(data,offset):return struct.unpack_from('<I',data,offset)[0]


if __name__ == '__main__':unittest.main()
