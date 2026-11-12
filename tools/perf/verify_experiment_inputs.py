#!/usr/bin/env python3
"""Fail before network/service/fixture side effects if experiment inputs are incomplete."""
import argparse,ast,hashlib,json,re,sys
from pathlib import Path
LOCAL_PREFIXES=("common","guard","live","ring","resource","controller","calibration","preflight","postflight","clock_probe","build","run")
def verify(root, contract=None):
    root=Path(root).resolve(); errors=[]; checked=0
    required=["plan/receiver-full.plan","plan/rss.txt","plan/fields.txt","ring-control/receiver-full.plan"]
    for name in required:
        if not (root/name).is_file(): errors.append("missing required input: "+name)
    for file in sorted(root.rglob("*.py")):
        try: tree=ast.parse(file.read_text(),filename=str(file))
        except (SyntaxError,UnicodeError) as ex: errors.append("invalid Python: "+str(file.relative_to(root))+": "+str(ex));continue
        checked+=1
        for node in ast.walk(tree):
            modules=[]
            if isinstance(node,ast.Import): modules=[x.name for x in node.names]
            if isinstance(node,ast.ImportFrom) and node.module: modules=[node.module]
            for module in modules:
                if re.fullmatch(r"(?:"+"|".join(LOCAL_PREFIXES)+r")\d+",module):
                    if not any((d/(module+".py")).is_file() for d in (file.parent,root,root/"ring-control")):
                        errors.append("missing local import: "+module+" from "+str(file.relative_to(root)))
            if isinstance(node,ast.Constant) and isinstance(node.value,str):
                value=node.value
                if re.fullmatch(r"(?:"+"|".join(LOCAL_PREFIXES)+r")\d+[A-Z]?\.py",value):
                    if not any((d/value).is_file() for d in (file.parent,root,root/"ring-control")):
                        errors.append("missing local script: "+value+" from "+str(file.relative_to(root)))
    a=root/"plan/receiver-full.plan"; b=root/"ring-control/receiver-full.plan"
    if a.exists() and b.exists():
        if a.read_bytes()!=b.read_bytes(): errors.append("calibration plan differs from client plan")
        lines=a.read_text().splitlines()
        try:
            h=lines[0].split(); assert h[:4]==["CPNETFLUX_TCP_SOURCE_PORT_PLAN_V1","192.168.100.1","192.168.100.2","4"]
            rows=[list(map(int,l.split())) for l in lines[1:]]
            selected=[r for r in rows if r[1]==61200]
            assert sorted(r[0] for r in selected)==list(range(4)) and len({r[2] for r in selected})==4
            assert all(len(r)>=4 and all(0<x<=65535 for x in r[3:]) for r in selected)
        except (AssertionError,ValueError,IndexError): errors.append("invalid calibration/client plan")
    if contract:
        for record in contract["inputs"]:
            file=root/record["path"]
            if not file.is_file(): errors.append("contract file missing: "+record["path"])
            elif hashlib.sha256(file.read_bytes()).hexdigest()!=record["sha256"]: errors.append("contract hash mismatch: "+record["path"])
    return {"passed":not errors,"errors":errors,"python_files_checked":checked,"side_effects":"none"}
if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("root",type=Path);p.add_argument("--contract",type=Path);a=p.parse_args()
    result=verify(a.root,json.loads(a.contract.read_text()) if a.contract else None)
    print(json.dumps(result,ensure_ascii=False));raise SystemExit(0 if result["passed"] else 1)
