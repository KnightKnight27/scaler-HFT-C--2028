#!/usr/bin/env python3
"""Sequential, rotated five-run measurement. No third-party Python packages."""
import argparse
import csv
import datetime
import hashlib
import json
from pathlib import Path
import platform
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]

def output_row(text):
    lines = text.strip().splitlines()
    for i, line in enumerate(lines):
        if line.startswith('impl,mode,'):
            return next(csv.DictReader(lines[i:i+2]))
    raise ValueError('benchmark CSV header missing')

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bench', default=str(ROOT / 'build/bench'))
    p.add_argument('--cpu', type=int, default=0)
    p.add_argument('--out', type=Path, default=ROOT / 'results')
    p.add_argument('--mode', choices=['instrumented', 'bulk'], default='instrumented')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    rows = []
    final_bbos = set()
    for rep in range(5):
        order = 'ABC'[rep % 3:] + 'ABC'[:rep % 3]
        for impl in order:
            cmd = ['taskset', '-c', str(args.cpu), str(Path(args.bench).resolve()),
                   '--impl', impl, '--mode', args.mode, '--warmup']
            run = subprocess.run(cmd, capture_output=True, text=True)
            stem = args.out / f'{args.mode}-{rep+1}-{impl}'
            stem.with_suffix('.txt').write_text(
                f'date_utc: {datetime.datetime.now(datetime.timezone.utc).isoformat()}\n'
                f'command: {" ".join(cmd)}\nexit_status: {run.returncode}\n'
                f'STDOUT:\n{run.stdout}\nSTDERR:\n{run.stderr}')
            if run.returncode:
                raise RuntimeError(f'{cmd} failed; see {stem}.txt')
            row = output_row(run.stdout)
            final_bbos.add(next(line for line in run.stdout.splitlines() if line.startswith('Final BBO ')))
            warmup = next(line.split(': ', 1)[1] for line in run.stdout.splitlines() if line.startswith('Warmup checksum: '))
            if warmup != row['checksum']:
                raise RuntimeError('warmup/measured checksum mismatch')
            row['repetition'] = rep + 1
            rows.append(row)
            print(f'{args.mode} run {rep+1} {impl}: {row["total_ms"]} ms', flush=True)
    if len(final_bbos) != 1:
        raise RuntimeError('cross-implementation final BBO mismatch')
    for key in ['trace_hash', 'checksum', 'adds', 'cancels', 'bbo_queries', 'depth_calls']:
        if len({str(row[key]) for row in rows}) != 1:
            raise RuntimeError(f'cross-implementation mismatch: {key}')
    if any(row['successful_cancels'] != row['cancels'] for row in rows):
        raise RuntimeError('cancel failure')
    with (args.out / f'{args.mode}-runs.csv').open('w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    medians = {impl: {metric: statistics.median(float(r[metric]) for r in rows if r['impl'] == impl)
                       for metric in ['total_ms', 'ns_add', 'ns_cancel', 'ns_bbo']} for impl in 'ABC'}
    (args.out / f'{args.mode}-medians.json').write_text(json.dumps(medians, indent=2) + '\n')
    # Exact source identity without a circular reference to an evidence commit.
    files = sorted([ROOT / 'CMakeLists.txt', *ROOT.glob('include/*.hpp'), *ROOT.glob('src/*')])
    identity = {str(f.relative_to(ROOT)): hashlib.sha256(f.read_bytes()).hexdigest() for f in files}
    meta = {'date_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'platform': platform.platform(), 'cpu_affinity': args.cpu,
            'mode': args.mode, 'seed': 42, 'ops': 1000000,
            'repetitions': 5, 'final_bbo': next(iter(final_bbos)), 'warmup': 'identical trace, separate fresh book each process',
            'source_sha256': identity,
            'binary_sha256': hashlib.sha256(Path(args.bench).read_bytes()).hexdigest()}
    compile_commands = Path(args.bench).resolve().parent / 'compile_commands.json'
    if compile_commands.exists():
        meta['compile_commands'] = json.loads(compile_commands.read_text())
    (args.out / f'{args.mode}-metadata.json').write_text(json.dumps(meta, indent=2) + '\n')
    print(json.dumps(medians, indent=2))

if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        sys.exit(str(e))
