#!/usr/bin/env python3
"""Build and validate using only the intended submission's source files."""
import datetime
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
log_path = ROOT / 'results/fresh-build.txt'
with tempfile.TemporaryDirectory(prefix='order-book-10144-') as directory, log_path.open('w') as log:
    temp = Path(directory)
    source, build = temp / 'source', temp / 'build'
    source.mkdir()
    shutil.copy2(ROOT / 'CMakeLists.txt', source)
    for name in ['include', 'src', 'scripts']:
        shutil.copytree(ROOT / name, source / name, ignore=shutil.ignore_patterns('__pycache__'))
    log.write('UTC: ' + datetime.datetime.now(datetime.timezone.utc).isoformat() + '\n')
    def run(command):
        log.write('\n$ ' + ' '.join(map(str, command)) + '\n')
        log.flush()
        result = subprocess.run(list(map(str, command)), stdout=log, stderr=subprocess.STDOUT)
        log.write(f'exit_status={result.returncode}\n')
        log.flush()
        if result.returncode:
            raise RuntimeError('fresh validation failed: ' + str(command))
    run(['cmake', '-S', source, '-B', build, '-DCMAKE_BUILD_TYPE=Release'])
    run(['cmake', '--build', build, '-j', '2'])
    commands = json.loads((build / 'compile_commands.json').read_text())
    for item in commands:
        flags = item['command'].split()
        if '-O2' not in flags or '-O3' in flags or '-std=c++17' not in flags:
            raise RuntimeError('incorrect measured build flags')
        log.write('Actual compile command: ' + item['command'] + '\n')
    run([build / 'bench', '--verify'])
    run([build / 'bench', '--verify', '--seed', '123456', '--ops', '100000'])
    run([sys.executable, source / 'scripts/check_float_rejection.py'])
    run([sys.executable, source / 'scripts/smoke.py', '--bench', build / 'bench'])
    log.write('\nPASS clean source-only out-of-tree build, both documented Release verify commands, API compile rejection, CLI + A/B/C benchmark smokes.\n')
print(log_path)
