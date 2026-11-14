#!/usr/bin/env python3
"""Coverage gate for the firmware logic (used by `make coverage`).

usage: coverage.py LLVM_COV PROFDATA REPO_ROOT TEST_BINARY...

Prints line / branch / function coverage of every firmware header that the
host tests compile, and fails unless each of them is fully covered by lines
and functions and at least MIN_BRANCH percent by branches. (A missed branch
is usually the false side of a defensive check that cannot happen through
the public interface; mutation testing - `make mutation` - is what shows
whether the tests would notice a wrong decision.)
"""
import json
import os
import re
import subprocess
import sys

MIN_BRANCH = 95.0

# Headers that are firmware logic. Wiring / credentials / host-only helpers are not.
LOGIC_DIRS = ('hhj-common/include/hhj/', 'hhj-sensor/', 'hhj-timer/include/')
NOT_LOGIC = ('wifi_config.h', 'wifi_secrets.h', 'wifi_secrets.example.h', 'config.h', 'describe.h')


def uncovered(llvm_cov, profdata, binaries, path):
    """Source lines never run, and branch sides never taken, in one file."""
    cmd = [llvm_cov, 'show', f'-instr-profile={profdata}', binaries[0]]
    for b in binaries[1:]:
        cmd += ['-object', b]
    cmd += ['-show-branches=count', path]
    text = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    out, last = [], ''
    for ln in text.split('\n'):
        m = re.match(r'\s*(\d+)\|\s*([0-9.kMG]+)?\|(.*)', ln)
        if m:
            last = f'{m.group(1)}: {m.group(3).strip()[:100]}'
            if m.group(2) == '0':
                out.append(f'line   {last}')
        elif 'Branch (' in ln and re.search(r'(True|False): 0[,\]]', ln):
            side = 'true' if re.search(r'True: 0,', ln) else 'false'
            col = re.search(r'Branch \((\d+):(\d+)\)', ln)
            out.append(f'branch {last}   [column {col.group(2)} never {side}]')
    return out


def main():
    llvm_cov, profdata, root, *binaries = sys.argv[1:]
    root = os.path.realpath(root)
    cmd = [llvm_cov, 'export', '-summary-only', f'-instr-profile={profdata}', binaries[0]]
    for b in binaries[1:]:
        cmd += ['-object', b]
    data = json.loads(subprocess.run(cmd, check=True, capture_output=True, text=True).stdout)

    rows = []
    for f in data['data'][0]['files']:
        path = os.path.realpath(f['filename'])
        if not path.startswith(root + os.sep):
            continue
        rel = os.path.relpath(path, root)
        if not rel.endswith('.h') or os.path.basename(rel) in NOT_LOGIC:
            continue
        if not any(rel.startswith(d) and '/test/' not in rel for d in LOGIC_DIRS):
            continue
        s = f['summary']
        rows.append((rel, s['lines'], s['branches'], s['functions']))

    if not rows:
        print('coverage: no firmware headers found in the profile')
        return 1

    def pct(part):
        return 100.0 if part['count'] == 0 else 100.0 * part['covered'] / part['count']

    failed = False
    print(f"{'file':44} {'lines':>14} {'branches':>16} {'functions':>12}")
    for rel, lines, branches, functions in sorted(rows):
        ok = lines['covered'] == lines['count'] and functions['covered'] == functions['count'] \
            and pct(branches) >= MIN_BRANCH
        failed = failed or not ok
        print(f"{rel:44} {lines['covered']:5}/{lines['count']:<5} {pct(lines):3.0f}%"
              f" {branches['covered']:5}/{branches['count']:<5} {pct(branches):5.1f}%"
              f" {functions['covered']:4}/{functions['count']:<4}{'' if ok else '   <-- below the bar'}")
    if failed or os.environ.get('COVERAGE_MISSING'):
        for rel, *_ in sorted(rows):
            missing = uncovered(llvm_cov, profdata, binaries, os.path.join(root, rel))
            if missing:
                print(f'\n{rel}: not exercised')
                for line in missing:
                    print('  ' + line)
    if failed:
        print(f'\ncoverage: FAILED (need 100% lines, 100% functions, >= {MIN_BRANCH:.0f}% branches per file)')
        return 1
    print(f'\ncoverage: OK (100% lines and functions, >= {MIN_BRANCH:.0f}% branches in every firmware logic header)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
