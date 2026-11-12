#!/usr/bin/env python3
"""Bounded experiment sender policies: no mutually incompatible or expanded profiles."""
import pathlib,sys,unittest
BUILD=pathlib.Path(__file__).resolve().parents[2]/"build"
if "--build-dir" in sys.argv:
 i=sys.argv.index("--build-dir");BUILD=pathlib.Path(sys.argv[i+1]);del sys.argv[i:i+2]
from types import SimpleNamespace
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'perf'))
import private_tmpfs as p
class Policies(unittest.TestCase):
 def args(self,**kw):
  d=dict(profile='admission',native_setup=True,mode='v3',kernel_tcp_pacing=False,sender_pacing='1500000000');d.update(kw);return SimpleNamespace(**d)
 def test_explicit_existing_application_policy(self):
  spec=p.validate_profile(self.args())
  self.assertEqual(spec['sender_pacing'],'1500000000')
  self.assertEqual(spec['size_gib'],1)
 def test_reject_invalid_or_incompatible_before_allocation(self):
  for kw in [dict(sender_pacing='1000000000'),dict(kernel_tcp_pacing=True),dict(mode='smoke'),dict(profile='m2-control'),dict(profile='m2-candidate',kernel_tcp_pacing=True)]:
   with self.subTest(kw=kw),self.assertRaises(ValueError):p.validate_profile(self.args(**kw))
 def test_default_remains_off_for_legacy_callers(self):
  a=self.args();del a.sender_pacing
  self.assertEqual(p.validate_profile(a)['sender_pacing'],'off')
 def test_native_policy_parses_then_stops_at_privilege_boundary(self):
  import os,subprocess,time
  self.assertNotEqual(os.geteuid(),0,'run supervisor policy test as unprivileged Su')
  helper=BUILD/'cpnetflux-private-tmpfs-launcher'
  common=[str(helper),'--enable-private-tmpfs','--payload-root','/dev/shm/cpnetflux-s0-private55-63-unit','--evidence','/home/Su/experiments/CPNetFlux/2026-11-12-field-trial-execution-62/unit-policy','--mode','v3','--commit','1'*40,'--uid',str(os.getuid()),'--gid',str(os.getgid()),'--inode','1','--host-namespace','mnt:[unit]','--setup-started',str(time.monotonic())]
  for value in ('off','1500000000'):
   result=subprocess.run(common+['--sender-pacing',value],capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0)
   self.assertIn('supervisor requires explicit sudo',result.stderr)
  for extra in [['--sender-pacing','7'],['--sender-pacing','1500000000','--kernel-tcp-pacing'],['--sender-pacing','off','--sender-pacing','off'],['--sender-pacing','1500000000','--profile','m2-control']]:
   result=subprocess.run(common+extra,capture_output=True,text=True)
   self.assertNotEqual(result.returncode,0)
   self.assertNotIn('supervisor requires explicit sudo',result.stderr,'invalid policy crossed parser boundary')
if __name__=='__main__':unittest.main()
