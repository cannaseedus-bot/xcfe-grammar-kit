#!/usr/bin/env python3
"""Run the XCFE grammar kit validation checks."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
MINI = ROOT / 'tools/grammar/mini_transpilers.py'
COMMANDS = [
    [sys.executable, str(MINI), 'control-check', '--repo-root', str(ROOT)],
    [sys.executable, str(MINI), 'capability-gap', '--check', '--repo-root', str(ROOT)],
]

def main() -> int:
    for cmd in COMMANDS:
        proc = subprocess.run(cmd, cwd=ROOT, text=True)
        if proc.returncode != 0:
            return proc.returncode
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
