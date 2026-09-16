#!/usr/bin/env python3
from __future__ import annotations

import sys
from pathlib import Path

if __package__ is None or __package__ == "":
    sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from tools.experiments.gridftp_compare.runner import main


if __name__ == "__main__":
    raise SystemExit(main())
