import json
import pathlib
import re
import shutil
import subprocess
import time

parent=pathlib.Path('/tmp/cpnetflux-runs/V3-GRIDFTP-RAW-STAGE-01')
active=parent/'20261022T044500Z-v3-raw'
result={'epoch':time.time(),'removed':[],'free_before':shutil.disk_usage(active).free}
listeners=subprocess.run(['ss','-ltnH'],capture_output=True,text=True,check=True).stdout
if re.search(r':(?:34261|32811|35201)\s',listeners): raise RuntimeError('experiment listener still active')
for run in [active,parent/'20261022T041500Z-raw-v3-55f7']:
 for name in ['payload','v3-server-root','gridftp/server-root','credentials','tls']:
  path=run/name
  if path.is_symlink() or parent.resolve() not in path.resolve().parents: raise RuntimeError('unsafe cleanup path')
  if path.exists(): shutil.rmtree(path);result['removed'].append(str(path))
result.update(free_after=shutil.disk_usage(active).free,experiment_ports_closed=True,
              payload_removed=not(active/'payload').exists(),credentials_removed=not(active/'credentials').exists(),
              private_tls_removed=not(active/'tls').exists())
(active/'cleanup-final.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
