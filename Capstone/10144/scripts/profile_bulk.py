#!/usr/bin/env python3
"""Supplementary same-binary bulk call graphs to investigate timer overhead."""
import argparse
import datetime
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--bench', default=str(ROOT / 'build/bench'))
p.add_argument('--perf', default='perf')
p.add_argument('--cpu', type=int, default=0)
a = p.parse_args()
out = ROOT / 'results/perf'
out.mkdir(parents=True, exist_ok=True)
statuses = []
for impl in 'ABC':
    data = out / f'bulk-{impl}.data'
    commands = [(f'flat-report-{impl}', [a.perf, 'report', '--stdio', '--no-children', '--call-graph', 'none',
                 '--percent-limit', '1', '-i', str(out / f'perf-{impl}.data')]),
                (f'bulk-record-{impl}', ['taskset', '-c', str(a.cpu), a.perf, 'record', '-g',
                 '-o', str(data), '--', str(Path(a.bench).resolve()), '--impl', impl, '--mode', 'bulk']),
                (f'bulk-report-{impl}', [a.perf, 'report', '--stdio', '--percent-limit', '1', '-i', str(data)]),
                (f'bulk-flat-{impl}', [a.perf, 'report', '--stdio', '--no-children', '--call-graph', 'none',
                 '--percent-limit', '1', '-i', str(data)])]
    for name, cmd in commands:
        run = subprocess.run(cmd, capture_output=True, text=True)
        (out / f'{name}.stdout.txt').write_text(run.stdout)
        (out / f'{name}.stderr.txt').write_text(run.stderr)
        statuses.append({'name': name, 'command': shlex.join(cmd), 'exit_status': run.returncode,
                         'date_utc': datetime.datetime.now(datetime.timezone.utc).isoformat()})
        print(name, 'exit', run.returncode, flush=True)
        if run.returncode:
            break
(out / 'bulk-commands.json').write_text(json.dumps(statuses, indent=2) + '\n')
raise SystemExit(int(any(s['exit_status'] for s in statuses)))
