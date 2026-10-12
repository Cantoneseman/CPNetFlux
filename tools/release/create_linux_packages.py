#!/usr/bin/env python3
"""Build internal Linux package candidates from a clean recorded revision."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

def run(args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir",type=Path,required=True)
    parser.add_argument("--output-dir",type=Path,required=True)
    args=parser.parse_args()
    build=args.build_dir.resolve()
    info=json.loads((build/"build-info.json").read_text())
    if info["tls"] != 1:
        parser.error("delivery candidate requires TLS support")
    cache=(build/"CMakeCache.txt").read_text().splitlines()
    source=Path(next(x.split("=",1)[1] for x in cache if x.startswith("CMAKE_HOME_DIRECTORY:INTERNAL=")))
    if run(["git","-C",str(source),"status","--porcelain"]):
        parser.error("package input must be a clean committed checkout")
    sha=run(["git","-C",str(source),"rev-parse","HEAD"])
    if info["source_revision"]!=sha:
        parser.error("build metadata revision differs from HEAD; reconfigure and rebuild")
    out=args.output_dir.resolve()
    if out.exists() and any(out.iterdir()):
        parser.error("output directory must be empty (no overwrite)")
    out.mkdir(parents=True,exist_ok=True)
    subprocess.run(["cpack","--config",str(build/"CPackConfig.cmake"),"-B",str(out)],check=True,cwd=build)
    artifacts=[]
    for path in sorted(out.iterdir()):
        if path.suffix not in {".deb",".gz"}: continue
        artifacts.append(dict(name=path.name,bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    if len(artifacts)!=2: raise RuntimeError("expected exactly TGZ and DEB")
    deb=next(out/a["name"] for a in artifacts if a["name"].endswith(".deb"))
    dependencies=run(["dpkg-deb","-f",str(deb),"Depends"])
    manifest=dict(format_version=1,build=info,platform=run(["lsb_release","-ds"]),
                  dependencies=dependencies,service_auto_start=False,real_desktop_adapter=True,artifacts=artifacts)
    (out/"linux-package-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    names=[a["name"] for a in artifacts]+["linux-package-manifest.json"]
    (out/"SHA256SUMS").write_text("".join(hashlib.sha256((out/n).read_bytes()).hexdigest()+"  "+n+"\n" for n in names))
    print(json.dumps(manifest,indent=2))
if __name__=="__main__": main()
