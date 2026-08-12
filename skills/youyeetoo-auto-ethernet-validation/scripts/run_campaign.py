#!/usr/bin/env python3
"""Dispatch to the audited project campaign runner."""

from pathlib import Path
import runpy
import sys

PROGRAM = Path(__file__).resolve().parents[4] / "tools" / "ethernet-campaign" / "auto_runner.py"
if not PROGRAM.is_file():
    raise SystemExit(f"campaign runner not found: {PROGRAM}")
sys.path.insert(0, str(PROGRAM.parent))
runpy.run_path(str(PROGRAM), run_name="__main__")
