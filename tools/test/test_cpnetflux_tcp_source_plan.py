#!/usr/bin/env python3
import pathlib,sys,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'perf'))
from generate_cpnetflux_tcp_source_plan import generate,rss
FIELDS='TCP over IPV4 flows use these fields for computing Hash flow key:\nIP SA\nIP DA\nL4 bytes 0 & 1 [TCP/UDP src port]\nL4 bytes 2 & 3 [TCP/UDP dst port]\n'
RAW='RX flow hash indirection table for ethX with 4 RX ring(s):\n0: 0 1 2 3\nRSS hash key:\n6d:5a:56:da:25:5b:0e:c2:41:67:25:3d:43:a3:8f:b0:d0:ca:2b:cb:ae:7b:30:b4:77:cb:2d:a3:80:30:f2:0c:6a:42:b7:3b:be:ac:01:fa\nRSS hash function:\n toeplitz: on\n xor: off\n crc32: off\nRSS input transformation:\n symmetric-xor: off\n symmetric-or-xor: on\n'
class SourcePlanGeneratorTest(unittest.TestCase):
 def test_generated_ports_match_independent_bitwise_hash(self):
  text,r=generate(RAW,FIELDS,'192.0.2.1','192.0.2.2',[0,1,2,3],[61000,61001],8)
  self.assertEqual(len(r['rows']),8);self.assertFalse(r['live_nic_verification'])
  for row in r['rows']:
   self.assertEqual(len(set(row['candidates'])),8)
   for source in row['candidates']:self.assertEqual(rss.predict(r['config'],'192.0.2.2','192.0.2.1',row['peer_port'],source)['queue'],row['target_queue'])
  self.assertIn(r['snapshot_sha256'],text.splitlines()[0])
 def test_known_tcp_hash_vector(self):
  cfg=rss.parse(RAW);data=rss.transformed('66.9.149.187','161.142.100.80',2794,1766,'none')
  self.assertEqual(rss.toeplitz(bytes.fromhex(cfg['key']),data),0x51ccc178)
 def test_unsupported_or_budget_invalid(self):
  for raw,fields,queues,ports,count in [(RAW.replace('toeplitz: on','toeplitz: off'),FIELDS,[0],[1],1),(RAW,'IP SA',[0],[1],1),(RAW,FIELDS,[0,0],[1],1),(RAW,FIELDS,[8],[1],1),(RAW,FIELDS,[0],[1],129),(RAW,FIELDS,[0,1,2,3],range(513),128),(RAW,FIELDS,[0],[1],1)]:
   with self.subTest(queues=queues,count=count):
    if ports==[1] and queues==[0] and raw==RAW and fields==FIELDS and count==1:
     with self.assertRaises(ValueError):generate(raw,fields,'::1','192.0.2.2',queues,ports,count)
    else:
     with self.assertRaises(ValueError):generate(raw,fields,'192.0.2.1','192.0.2.2',queues,ports,count)
if __name__=='__main__':unittest.main()
