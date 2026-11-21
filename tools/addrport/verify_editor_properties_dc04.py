#!/usr/bin/env python3
"""Read-only receipts for native cast-property gesture and ownership layouts.

These checks do not enable controls, introduce callable symbols or prove native
multiplayer property editing. Verify the existing editor ABI first.
"""
import argparse
import json
from pathlib import Path
import struct

from pe import PE
from verify_editor_dc04 import SHA, verify as verify_editor

BYTES = (
    (0xEE538A, '8b4514', 'ClassInput interface owns the selected class ID at +14'),
    (0xEE53BF, '69c0e0040000034370', 'current act indexes class+70 with stride4E0'),
    (0xEE53D1, '8b4608', 'Message type at +08'),
    (0xEE59B6, '3d4947a407', 'slider end event07A44749'),
    (0xEE59C1, '3d9c48a407', 'slider begin event07A4489C'),
    (0xEE59CC, '817e0c10cd4207', 'slider component0742CD10 at Message+0C'),
    (0xEE59F4, '3d78bf9207', 'awareness slider control0792BF78'),
    (0xEE5A11, 'd98784040000', 'begin reads class-act float+484'),
    (0xEE5AE3, '3d78bf9207', 'end resolves the same awareness control'),
    (0xEE62D5, '3d84a800ef', 'slider value eventEF00A884'),
    (0xEE6314, '3d78bf9207', 'value event resolves the same awareness control'),
    (0xEE6454, 'f30f590500a74801', 'value scales native slider fraction by constant148A700'),
    (0xEE6473, 'f30f118784040000', 'value writes only float+484 on this branch'),
    (0xEE6716, 'c20800', 'ClassInput has two stack arguments'),
    (0xF42660, '83ec1c568bf1', 'end helper receives native ScenarioData in ECX'),
    (0xF42890, '836614fe', 'end helper can clear an authored goal flag at +14'),
    (0xF42858, 'c3', 'end helper returns without stack arguments'),
)
CALLS = (
    (0xEE53AB, 0xF3E740, 'resolve selected native class'),
    (0xEE53BA, 0xEFC410, 'resolve current native act'),
    (0xEE5A22, 0xF45810, 'awareness begin owns native StartHistory'),
    (0xEE5B11, 0xEDE300, 'awareness end uses its native finalizer'),
    (0xEE5B22, 0xF42660, 'end then validates authored goals'),
)

def verify(pe):
    verify_editor(pe)
    for address, encoded, note in BYTES:
        expected=bytes.fromhex(encoded)
        if pe.read(address,len(expected))!=expected:
            raise ValueError(f'Cast-property receipt mismatch {address:08X}: {note}')
    for address,target,note in CALLS:
        raw=pe.read(address,5)
        if not raw or raw[0]!=0xE8 or address+5+struct.unpack('<i',raw[1:])[0]!=target:
            raise ValueError(f'Cast-property caller mismatch {address:08X}: {note}')
    if pe.read(0x148A700,4)!=struct.pack('<f',175.0):
        raise ValueError('Awareness range constant is not175')
    return dict(executable_sha256=SHA,static_receipts=len(BYTES)+len(CALLS)+1,
        class_act_stride=0x4E0,class_act_vector=0x70,
        awareness=dict(control=0x0792BF78,offset=0x484,slider_maximum=175.0),
        native_controls_enabled=False,
        limitation='Native goal side effects and refresh still need live transaction validation.')

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('exe');p.add_argument('--report',type=Path)
    a=p.parse_args()
    try:
        result=verify(PE(a.exe))
        if a.report:a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
        print(f"Verified {result['static_receipts']} cast-property receipts; native controls remain disabled")
    except (OSError,ValueError) as error:p.exit(1,str(error)+'\n')

if __name__=='__main__':main()
