import socket
import struct
import sys
from pathlib import Path
import time
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/tests'))
from editor_network_proxy import EditorProxy, Profile, messages


def packet(reliable=7,payload=b'x',flags=1):
    header=b'SPCO'+struct.pack('<HIHHQQB',16,1234,1,0,0,0,1)
    return header+struct.pack('<BBH',53,flags,len(payload))+(struct.pack('<H',reliable) if flags else b'')+payload


class ProxyTests(unittest.TestCase):
    def test_complete_frame_and_reliable_identity(self):
        self.assertEqual(messages(packet()),[(1234,53,7,b'x')])
        self.assertEqual(messages(packet(flags=0)),[(1234,53,None,b'x')])
        for invalid in (packet()[:-1],packet()+b'x',packet(flags=2),packet()[:4]+b'\x0f\x00'+packet()[6:],b''):
            self.assertEqual(messages(invalid),[])

    def test_validated_bounded_profiles(self):
        for kwargs in ({'rtt_ms':float('nan')},{'rtt_ms':-1},{'loss':1.1},{'bits_second':-1},{'duplicate':float('inf')}):
            with self.assertRaises(ValueError):
                Profile(**kwargs)

    def test_real_loopback_loss_duplicates_and_source_retransmissions(self):
        with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as host, socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as guest:
            host.bind(('127.0.0.1',0));host.settimeout(2)
            guest.bind(('127.0.0.1',0));guest.settimeout(.1)
            with EditorProxy(host.getsockname()[1]) as proxy:
                proxy.configure(Profile(loss=1))
                guest.sendto(packet(),('127.0.0.1',proxy.port))
                deadline=time.monotonic()+2
                while proxy.snapshot()['up'].get('dropped_datagrams',0)!=1 and time.monotonic()<deadline:
                    time.sleep(.005)
                self.assertEqual(proxy.snapshot()['up']['dropped_datagrams'],1)
                before=proxy.snapshot()
                proxy.configure(Profile(rtt_ms=100,duplicate=1))
                begun=time.monotonic();guest.sendto(packet(),('127.0.0.1',proxy.port))
                data,address=host.recvfrom(2048)
                self.assertGreaterEqual(time.monotonic()-begun,.045)
                self.assertEqual(data,packet());self.assertEqual(host.recvfrom(2048)[0],packet())
                host.sendto(packet(8),address)
                guest.settimeout(2)
                self.assertEqual(guest.recvfrom(2048)[0],packet(8));self.assertEqual(guest.recvfrom(2048)[0],packet(8))
                after=proxy.snapshot();delta=EditorProxy.difference(before,after)
                self.assertEqual(delta['up']['source_datagrams'],1)
                self.assertEqual(delta['up']['source_udp_payload_bytes'],len(packet()))
                self.assertEqual(delta['up']['retransmitted_messages'],1)
                self.assertEqual(delta['up']['injected_duplicate_datagrams'],1)
                self.assertEqual(delta['up']['forwarded_udp_payload_bytes'],2*len(packet()))
                self.assertEqual(delta['down']['source_datagrams'],1)

    def test_queue_bound_is_explicit_not_silent_loss(self):
        with EditorProxy(9) as proxy:
            proxy.max_packets=0
            with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as guest:
                guest.sendto(packet(),('127.0.0.1',proxy.port))
                deadline=time.monotonic()+2
                while proxy.error is None and time.monotonic()<deadline:
                    time.sleep(.005)
                with self.assertRaisesRegex(RuntimeError,'queue exceeded'):
                    proxy.snapshot()


if __name__=='__main__':
    unittest.main()
