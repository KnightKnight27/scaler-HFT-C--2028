#!/usr/bin/env python3
"""Recalculate the rubric table from saved benchmark CSV and real perf text."""
import csv
import json
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parents[1]
RESULTS = ROOT / 'results'
EVENTS = ['cycles', 'instructions', 'branches', 'branch-misses',
          'L1-dcache-load-misses', 'cache-misses', 'dTLB-load-misses', 'page-faults']

def counters(path):
    found = {}
    if not path.exists():
        return found
    for line in path.read_text().splitlines():
        # Human perf stat output: COUNT [unit] EVENT. Ignore unsupported/not-counted.
        match = re.match(r'^\s*([\d,]+(?:\.\d+)?)\s+(?:(?:msec|nsec|sec)\s+)?(\S+)', line)
        if not match:
            continue
        value, event = match.groups()
        for name in EVENTS:
            # Exact event token or core-PMU event; do not conflate branch-misses/branches.
            if event.split(':')[0] == name or event.startswith('cpu_core/' + name + '/'):
                found[name] = float(value.replace(',', ''))
    return found

def main():
    with (RESULTS / 'instrumented-runs.csv').open() as f:
        runs = list(csv.DictReader(f))
    if len(runs) != 15 or any(sum(r['impl'] == i for r in runs) != 5 for i in 'ABC'):
        raise RuntimeError('expected exactly five runs per implementation')
    for field in ['trace_hash', 'checksum', 'adds', 'cancels', 'depth_calls']:
        if len({r[field] for r in runs}) != 1:
            raise RuntimeError('inconsistent ' + field)
    medians = {i: {m: statistics.median(float(r[m]) for r in runs if r['impl'] == i)
                    for m in ['total_ms', 'ns_add', 'ns_cancel', 'ns_bbo']} for i in 'ABC'}
    recorded = json.loads((RESULTS / 'instrumented-medians.json').read_text())
    if medians != recorded:
        raise RuntimeError('saved medians disagree with raw CSV')
    events = {i: {} for i in 'ABC'}
    sources = {}
    for event in EVENTS:
        group = 'software' if event == 'page-faults' else ('cache' if 'cache' in event or 'TLB' in event else 'hardware')
        for suffix in ['full', group]:
            values = {i: counters(RESULTS / 'perf' / f'stat-{i}-{suffix}.stderr.txt').get(event) for i in 'ABC'}
            if all(v is not None for v in values.values()):
                for i in 'ABC':
                    events[i][event] = values[i]
                sources[event] = suffix
                break
        else:
            sources[event] = 'N/A; see raw perf event errors/not-supported lines'
    table = {}
    for i in 'ABC':
        e = events[i]
        table[i] = dict(medians[i])
        # Ratios only within the same collection group/scope.
        table[i]['IPC'] = (e['instructions']/e['cycles'] if e.get('cycles') and
                            'instructions' in e and sources['cycles'] == sources['instructions'] else None)
        table[i]['branch_misses_pct'] = (100*e['branch-misses']/e['branches'] if e.get('branches') and
                            'branch-misses' in e and sources['branches'] == sources['branch-misses'] else None)
        for k in ['L1-dcache-load-misses', 'cache-misses', 'page-faults']:
            table[i][k] = e.get(k)
    (RESULTS / 'table.json').write_text(json.dumps({'values': table, 'counter_sources': sources,
                                                  'raw_counter_means': events}, indent=2) + '\n')
    rows = [('Total time (ms)', 'total_ms'), ('ns / add', 'ns_add'), ('ns / cancel', 'ns_cancel'),
            ('ns / BBO', 'ns_bbo'), ('IPC', 'IPC'), ('Branch misses (%)', 'branch_misses_pct'),
            ('L1-dcache-load-misses', 'L1-dcache-load-misses'), ('Cache misses', 'cache-misses'),
            ('Page faults', 'page-faults')]
    text = '| Metric | A | B | C |\n|---|---:|---:|---:|\n'
    for label, key in rows:
        values = ['N/A' if table[i][key] is None else f'{table[i][key]:,.3f}' for i in 'ABC']
        text += '| ' + label + ' | ' + ' | '.join(values) + ' |\n'
    text += ('\nTiming rows: independent medians of five instrumented runs, seed42, 1M primary ops, CPU0, '
             '-O2; full raw runs are in instrumented-runs.csv. Counter rows: perf -r5 arithmetic means '
             'over full process lifetime, scaled by perf where multiplexed. IPC and branch % are ratios '
             'of the comparable mean counts (not means of per-run ratios). On the hybrid CPU only numeric '
             'cpu_core events are used; not-counted atom events are excluded. User-only event filtering '
             'and multiplexing are preserved in raw logs.\n\nCounter collection groups: ' +
             ', '.join(f'{e}: {s}' for e, s in sources.items()) + '.\n')
    (RESULTS / 'table.md').write_text(text)
    print(text)

if __name__ == '__main__':
    main()
