#!/usr/bin/env python3
"""Collect actual perf evidence; expected capability failures remain explicit."""
import argparse
import datetime
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FULL = 'task-clock,cycles,instructions,branches,branch-misses,cache-misses,L1-dcache-load-misses,dTLB-load-misses,page-faults'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--bench', default=str(ROOT / 'build/bench'))
    p.add_argument('--perf', default='perf')
    p.add_argument('--cpu', type=int, default=0)
    p.add_argument('--out', type=Path, default=ROOT / 'results/perf')
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    statuses = []
    def run(name, cmd):
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True)
        except FileNotFoundError as error:
            proc = subprocess.CompletedProcess(cmd, 127, '', str(error) + '\n')
        (args.out / f'{name}.stdout.txt').write_text(proc.stdout)
        (args.out / f'{name}.stderr.txt').write_text(proc.stderr)
        info = {'name': name, 'command': shlex.join(cmd), 'exit_status': proc.returncode,
                'date_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        statuses.append(info)
        print(name, 'exit', proc.returncode, flush=True)
        return proc.returncode
    bench = str(Path(args.bench).resolve())
    perf = args.perf
    run('version', [perf, '--version'])
    for impl in 'ABC':
        # No warmup: exactly one primary replay per external repetition.
        command = ['taskset', '-c', str(args.cpu), perf, 'stat', '-r', '5', '-e', FULL,
                   bench, '--impl', impl]
        rc = run(f'stat-{impl}-full', command)
        text = (args.out / f'stat-{impl}-full.stderr.txt').read_text()
        if rc or '<not supported>' in text or 'Permission' in text:
            # Consistent smaller groups; never substitute unavailable events with zero.
            for group, events in [('hardware', 'cycles,instructions,branches,branch-misses'),
                                  ('cache', 'cache-misses,L1-dcache-load-misses,dTLB-load-misses'),
                                  ('software', 'task-clock,page-faults')]:
                run(f'stat-{impl}-{group}', ['taskset', '-c', str(args.cpu), perf, 'stat', '-r', '5',
                                            '-e', events, bench, '--impl', impl])
        data = args.out / f'perf-{impl}.data'
        rc = run(f'record-{impl}', ['taskset', '-c', str(args.cpu), perf, 'record', '-g', '-o', str(data),
                                  '--', bench, '--impl', impl])
        if rc == 0:
            run(f'report-{impl}', [perf, 'report', '--stdio', '--percent-limit', '1', '-i', str(data)])
        else:
            # Try software sampling if the default cycles event was denied.
            rc = run(f'record-{impl}-software', ['taskset', '-c', str(args.cpu), perf, 'record',
                     '-e', 'cpu-clock', '-g', '-o', str(data), '--', bench, '--impl', impl])
            if rc == 0:
                run(f'report-{impl}', [perf, 'report', '--stdio', '--percent-limit', '1', '-i', str(data)])
    (args.out / 'commands.json').write_text(json.dumps(statuses, indent=2) + '\n')
    # Nonzero summary if any requested command failed, even if fallbacks succeed.
    return int(any(x['exit_status'] for x in statuses))

if __name__ == '__main__':
    raise SystemExit(main())
