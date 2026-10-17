#!/usr/bin/env python3
"""Verify the integer tick API cannot silently truncate floating-point prices."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
compiler = os.environ.get('CXX', 'c++')
for impl in 'abc':
    for price in ['5000.5', '5000.5f', '5000.5L']:
        source = (f'#include "book_{impl}.hpp"\nint main() {{ ob::Book{impl.upper()} b; '
                  f'b.add_order(1, ob::Side::Buy, {price}, 1); }}\n')
        result = subprocess.run([compiler, '-std=c++17', '-I', str(root / 'include'),
                                 '-x', 'c++', '-fsyntax-only', '-'], input=source,
                                text=True, capture_output=True)
        if result.returncode == 0 or 'deleted' not in result.stderr:
            sys.exit(f'FAIL {impl} {price}: expected deleted overload diagnostic\n{result.stderr}')
print('PASS: A/B/C reject double, float, long double price calls at compile time')
