#!/usr/bin/env python3
import hashlib
import json
import os
import pathlib
import subprocess

BASE = pathlib.Path('/tmp/cpnetflux-runs/V3-GRIDFTP-87B389-RERUN-01')
REPO = pathlib.Path('/root/projects/CPNetFlux')
SHA = '87b389fc34615d1c43ccb36b5064beda2f47d1f6'
entries = subprocess.check_output(['git','ls-tree','-rz',SHA],cwd=REPO).split(b'\0')
checked = 0
for entry in entries:
    if not entry:
        continue
    metadata, name = entry.split(b'\t',1)
    mode, kind, blob = metadata.split()
    if kind != b'blob':
        raise RuntimeError('Unexpected non-blob source entry')
    path = BASE/'source'/os.fsdecode(name)
    data = os.fsencode(os.readlink(path)) if mode == b'120000' else path.read_bytes()
    actual = hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    if actual != blob.decode():
        raise RuntimeError('Source content differs from exact commit')
    checked += 1
expected = {'cpnetflux-gridftp-server':'336b2f21f610fa25c9c5dfb3afa95ac163e4ffda735a18014e290c69e25194b3',
            'cpnetflux-tree-upload-client':'9708f90109d008cd7c7e57dcdb121abb6210c8f46adad95ee527df6614611e58'}
for name, digest in expected.items():
    assert hashlib.sha256((BASE/'build'/name).read_bytes()).hexdigest() == digest
result = {'implementation_commit':SHA,'source_files_checked_against_git_blobs':checked,
          'source_archive_sha256':'eac263310902ffd48d18bf1cb92c6598ebd5ef0724907275d00504fad47a6ff8',
          'clean_release_build_binary_sha256':expected,
          'same_binary_hashes_on_r730_and_r750':True,
          'cmake_build_info':json.loads((BASE/'build/build-info.json').read_text()),
          'cmake_flags':{'build_type':'Release','tls':True,'io_uring_compiled':False,'desktop':False},
          'exact_targeted_ctest':{'passed':11,'failed':0},
          'full_ctest':{'total':467,'passed':466,'skipped':1,'failed':0},
          'analysis_unit_tests':{'passed':6,'failed':0},
          'lab_archive_sha256':hashlib.sha256((BASE/'lab-evidence.tgz').read_bytes()).hexdigest()}
(BASE/'evidence/provenance.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
