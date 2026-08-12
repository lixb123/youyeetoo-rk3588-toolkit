#!/usr/bin/env python3
"""Dispatch to the audited project generator kept with the source documents."""

from pathlib import Path
import runpy
import sys

PROGRAM = Path(__file__).resolve().parents[4] / "tools" / "ethernet-campaign" / "random_vectors.py"
if not PROGRAM.is_file():
    raise SystemExit(f"generator not found: {PROGRAM}")
sys.path.insert(0, str(PROGRAM.parent))
runpy.run_path(str(PROGRAM), run_name="__main__")
