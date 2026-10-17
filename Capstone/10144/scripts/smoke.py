#!/usr/bin/env python3
"""Check documented CLI modes, invalid arguments and small cross-book replays."""
import argparse
from pathlib import Path
import subprocess
from measure import output_row

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bench', default=str(ROOT / 'build/bench'))
a = p.parse_args()

def run(args, success=True):
    result = subprocess.run([a.bench, *args], text=True, capture_output=True)
    if (result.returncode == 0) != success:
        raise RuntimeError(f'unexpected status {result.returncode}: {args}\n{result.stdout}{result.stderr}')
    if not success and not result.stderr.startswith('ERROR:'):
        raise RuntimeError('missing useful argument error: ' + str(args))
    return result.stdout

run(['--help'])
for args in [['--impl', 'D'], ['--impl'], ['--ops', '0'], ['--ops', '-1'], ['--ops', '1x'],
             ['--ops', '100000001'], ['--seed', '18446744073709551616'],
             ['--seed', '-1'], ['--mode', 'bad'], ['--unknown']]:
    run(args, False)
print('PASS help and 10 invalid argument cases')
rows = []
for mode in ['instrumented', 'bulk']:
    for impl in 'ABC':
        row = output_row(run(['--impl', impl, '--ops', '1000', '--seed', '42', '--mode', mode, '--warmup']))
        assert row['successful_cancels'] == row['cancels']
        assert row['depth_calls'] == '20'
        rows.append(row)
assert len({r['trace_hash'] for r in rows}) == 1
assert len({r['checksum'] for r in rows}) == 1
print('PASS A/B/C instrumented+bulk, warmup, live cancels, depth count, checksum agreement')
