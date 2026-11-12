#!/usr/bin/env python3
"""One bounded 1GiB exact-read feasibility task; same pacing/resource gates as task51."""
import argparse
from pathlib import Path
import run_sender_pacing as pacing

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--evidence',type=Path,required=True)
    p.add_argument('--payload-root',type=Path,required=True)
    p.add_argument('--commit',required=True)
    args=p.parse_args()
    args.evidence.mkdir(parents=True,exist_ok=False)
    pacing.run(args,tcp_exact_read='on')
