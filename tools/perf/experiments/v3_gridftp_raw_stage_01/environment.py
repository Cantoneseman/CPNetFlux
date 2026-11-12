import json
import pathlib
import platform
import subprocess
import sys
import time

base=pathlib.Path(sys.argv[1]);interface=sys.argv[2]
commands={
 'clock':['date','-Ins'], 'kernel':['uname','-a'], 'cpu':['lscpu'],
 'memory':['free','-b'],'disks':['lsblk','-o','NAME,TYPE,SIZE,ROTA,MOUNTPOINTS,MODEL'],
 'space':['df','-B1',str(base)],'mount':['findmnt','-T',str(base)],
 'interface':['ip','-details','-statistics','link','show',interface],
 'addresses':['ip','-j','address','show',interface],
 'link':['ethtool',interface], 'network_driver':['ethtool','-i',interface],
 'gridftp_client':['globus-url-copy','-version'],
 'gridftp_server':['globus-gridftp-server','-version'],
 'page_size':['getconf','PAGESIZE'],'clock_ticks':['getconf','CLK_TCK'],
 'tcp_settings':['sysctl','net.ipv4.tcp_rmem','net.ipv4.tcp_wmem','net.core.rmem_max','net.core.wmem_max','net.ipv4.tcp_congestion_control'],
 'ca':['openssl','x509','-in','/etc/grid-security/certificates/30e9dfe9.0','-noout','-subject','-dates','-sha256','-fingerprint'],
 'proxy':['grid-proxy-info','-file',str(base/'credentials/x509up_u1002'),'-all'],
}
result={'epoch':time.time(),'hostname':platform.node(),'commands':{}}
for name,command in commands.items():
 try:
  p=subprocess.run(command,capture_output=True,text=True,timeout=20)
  result['commands'][name]={'exit':p.returncode,'stdout':p.stdout,'stderr':p.stderr}
 except (OSError,subprocess.TimeoutExpired)as e: result['commands'][name]={'error':str(e)}
(base/'environment-public.json').write_text(json.dumps(result,indent=2))
