#!/usr/bin/env python3
"""Mutation test of the firmware logic (used by `make mutation`).

usage: mutate.py REPO_ROOT WORK_DIR [header-name[,header-name...]]

For every firmware logic header this makes hundreds of copies with ONE small
change each ("<" -> "<=", "&&" -> "||", 20 -> 21, a statement removed, ...),
builds the host tests against the changed copy and runs them. A test suite
worth its name fails for (kills) every such mutant; a mutant that survives
is either a hole in the tests or a change that cannot alter behaviour.

The second kind is listed, with the reason, in mutation_equivalent.txt next
to this script. The run fails if a mutant survives that is not listed there,
and also if an entry there no longer matches a surviving mutant.

The real sources are never touched: mutants live in WORK_DIR.

Environment: MUT_JOBS (parallel workers, default 2), MUT_TIMEOUT (seconds a
test binary may run, default 60).
"""
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys

# header (relative to the repo root) -> test sources that should kill its mutants, quickest first
TARGETS = {
    'hhj-common/include/hhj/json_scan.h': ['hhj-common/test/test_json_scan.cpp', 'hhj-common/test/test_link.cpp'],
    'hhj-common/include/hhj/link.h': ['hhj-common/test/test_link.cpp'],
    'hhj-common/include/hhj/wifi_plan.h': ['hhj-common/test/test_wifi_plan.cpp'],
    'hhj-common/include/hhj/net.h': ['hhj-common/test/test_net.cpp', 'hhj-sensor/test/test_gate_app.cpp',
                                     'hhj-timer/test/test_timer_app.cpp'],
    'hhj-sensor/beam_logic.h': ['hhj-sensor/test/test_beam_logic.cpp'],
    'hhj-sensor/outbox.h': ['hhj-sensor/test/test_outbox.cpp'],
    'hhj-sensor/gate_app.h': ['hhj-sensor/test/test_gate_app.cpp', 'hhj-common/test/test_system.cpp'],
    'hhj-timer/include/display.h': ['hhj-timer/test/test_display.cpp'],
    'hhj-timer/include/stopwatch.h': ['hhj-timer/test/test_stopwatch.cpp'],
    'hhj-timer/include/timer_app.h': ['hhj-timer/test/test_timer_app.cpp', 'hhj-common/test/test_system.cpp'],
}
HEADER_DIRS = ['hhj-common/include/hhj', 'hhj-sensor', 'hhj-timer/include']

# operator swaps; all operators in these sources are written with a space on both sides
SWAPS = [('<=', '<'), ('<', '<='), ('>=', '>'), ('>', '>='), ('==', '!='), ('!=', '=='),
         ('&&', '||'), ('||', '&&'), ('+', '-'), ('-', '+')]
OP_RE = re.compile(r'(?<= )(<=|>=|==|!=|<|>|&&|\|\||\+|-)(?= )')
NUM_RE = re.compile(r'(?<![\w.])(\d+)(?![\w.])')
# "T name[N]": the size of a buffer or table. One more or one less element is
# not a change of behaviour as long as the content fits, and that it fits is
# what the tests check (ASan catches the buffer that is too small).
ARRAY_DECL_RE = re.compile(r'\b(?!return\b|case\b|else\b)[A-Za-z_]\w*[\s*&]+[A-Za-z_]\w*\[(\d+)\]')
BOOL_RE = re.compile(r'\b(true|false)\b')
NOT_RE = re.compile(r'!(?=[A-Za-z_(])')
STMT_RE = re.compile(r'^(\s*)[A-Za-z_][\w.\[\]>-]*(\s*(=|\+=|-=|\|=)\s*[^=].*|\+\+|--);\s*$')


def code_part(line):
    """The line without its // comment (string literals are respected)."""
    in_str = False
    i = 0
    while i < len(line):
        c = line[i]
        if c == '\\' and in_str:
            i += 2
            continue
        if c == '"':
            in_str = not in_str
        elif c == '/' and not in_str and line[i:i + 2] == '//':
            return line[:i]
        i += 1
    return line


def string_spans(code):
    spans, start, i = [], None, 0
    while i < len(code):
        c = code[i]
        if c == '\\' and start is not None:
            i += 2
            continue
        if c == '"':
            if start is None:
                start = i
            else:
                spans.append((start, i + 1))
                start = None
        i += 1
    return spans


def mutants_of(text):
    """Yields (line number, description, mutated text)."""
    lines = text.split('\n')
    in_block_comment = False
    for idx, line in enumerate(lines):
        stripped = line.strip()
        if in_block_comment:
            in_block_comment = '*/' not in line
            continue
        if stripped.startswith('/*'):
            in_block_comment = '*/' not in line
            continue
        if not stripped or stripped.startswith(('//', '#', 'namespace', 'using ', 'template', 'public:', 'private:',
                                                'protected:', 'static_assert')):
            continue
        code = code_part(line)
        tail = line[len(code):]
        spans = string_spans(code)

        def outside_strings(pos):
            return not any(a <= pos < b for a, b in spans)

        def emit(new_code, what):
            new_lines = lines[:idx] + [new_code + tail] + lines[idx + 1:]
            return idx + 1, what, '\n'.join(new_lines)

        for m in OP_RE.finditer(code):
            if not outside_strings(m.start()):
                continue
            for old, new in SWAPS:
                if m.group(1) == old:
                    yield emit(code[:m.start()] + new + code[m.end():], f'{old} -> {new}')
        array_sizes = {m.start(1) for m in ARRAY_DECL_RE.finditer(code)}
        for m in NUM_RE.finditer(code):
            if not outside_strings(m.start()) or m.start() in array_sizes:
                continue
            value = int(m.group(1))
            yield emit(code[:m.start()] + str(value + 1) + code[m.end():], f'{value} -> {value + 1}')
            if value > 1:
                yield emit(code[:m.start()] + str(value - 1) + code[m.end():], f'{value} -> {value - 1}')
        for m in BOOL_RE.finditer(code):
            if outside_strings(m.start()):
                new = 'false' if m.group(1) == 'true' else 'true'
                yield emit(code[:m.start()] + new + code[m.end():], f'{m.group(1)} -> {new}')
        for m in NOT_RE.finditer(code):
            if outside_strings(m.start()):
                yield emit(code[:m.start()] + code[m.end():], 'remove !')
        m = STMT_RE.match(code)
        if m:
            yield emit(m.group(1) + ';', 'remove statement')


SEP = ' @@ '


def load_equivalents(path):
    """file @@ original line @@ mutated line @@ reason   (all on one line)"""
    known = {}
    if os.path.exists(path):
        for raw in open(path, encoding='utf-8'):
            if not raw.strip() or raw.startswith('#'):
                continue
            parts = [p.strip() for p in raw.rstrip('\n').split(SEP)]
            if len(parts) != 4 or not parts[3]:
                sys.exit(f'{path}: every entry is "file{SEP}original{SEP}mutated{SEP}reason": {raw!r}')
            known[(parts[0], parts[1], parts[2])] = parts[3]
    return known


def main():
    root = os.path.realpath(sys.argv[1])
    work = os.path.realpath(sys.argv[2])
    only = sys.argv[3] if len(sys.argv) > 3 else ''
    jobs = int(os.environ.get('MUT_JOBS', '2'))
    timeout = int(os.environ.get('MUT_TIMEOUT', '60'))
    cxx = os.environ.get('CXX', 'c++')
    equivalents = load_equivalents(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mutation_equivalent.txt'))

    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    env = dict(os.environ, HHJ_TEST_QUICK='1')
    env.pop('HHJ_TEST_VERBOSE', None)

    def tree_for(slot):
        """A private copy of all firmware headers for one worker."""
        tree = os.path.join(work, f'tree{slot}')
        for d in HEADER_DIRS:
            os.makedirs(os.path.join(tree, d), exist_ok=True)
            for name in os.listdir(os.path.join(root, d)):
                if name.endswith('.h'):
                    shutil.copy(os.path.join(root, d, name), os.path.join(tree, d, name))
        return tree

    def build_and_run(tree, slot, test):
        """'killed' if the test fails to pass, 'survived' if it passes, 'invalid' if it does not compile."""
        exe = os.path.join(work, f'bin{slot}')
        # The same sanitizers as `make test-firmware`: several tests rely on
        # them to notice a read past the end of a buffer.
        cmd = [cxx, '-std=c++17', '-O0', '-w', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               f'-DHHJ_VECTORS="{root}/hhj-common/protocol/vectors.tsv"',
               f'-I{tree}/hhj-common/include', f'-I{root}/hhj-common/test',
               f'-I{tree}/hhj-sensor', f'-I{tree}/hhj-timer/include',
               os.path.join(root, test), '-o', exe]
        if subprocess.run(cmd, capture_output=True).returncode != 0:
            return 'invalid'
        try:
            passed = subprocess.run([exe], capture_output=True, env=env, timeout=timeout).returncode == 0
        except subprocess.TimeoutExpired:
            return 'killed'
        return 'survived' if passed else 'killed'

    # 0. the unmodified sources must pass, otherwise every mutant would look "killed"
    tree0 = tree_for(0)
    for header, tests in TARGETS.items():
        for test in tests:
            if build_and_run(tree0, 0, test) != 'survived':
                sys.exit(f'mutation: {test} does not pass on the unmodified sources - fix that first')

    work_items = []
    for header, tests in TARGETS.items():
        if only and not any(part in header for part in only.split(',')):
            continue
        original = open(os.path.join(root, header), encoding='utf-8').read()
        for lineno, what, mutated in mutants_of(original):
            work_items.append((header, tests, lineno, what, original, mutated))
    print(f'mutation: {len(work_items)} mutants, {jobs} worker(s)', flush=True)

    trees = {slot: tree_for(slot) for slot in range(1, jobs + 1)}
    free_slots = list(trees)

    def run_one(item):
        header, tests, lineno, what, original, mutated = item
        slot = free_slots.pop()
        try:
            path = os.path.join(trees[slot], header)
            with open(path, 'w', encoding='utf-8') as f:
                f.write(mutated)
            verdict = 'survived'
            for test in tests:
                verdict = build_and_run(trees[slot], slot, test)
                if verdict != 'survived':
                    break
            with open(path, 'w', encoding='utf-8') as f:
                f.write(original)
            return verdict
        finally:
            free_slots.append(slot)

    counts = {'killed': 0, 'survived': 0, 'invalid': 0}
    explained, unexplained = [], []
    used_entries = set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        for n, (item, verdict) in enumerate(zip(work_items, pool.map(run_one, work_items)), 1):
            header, _, lineno, what, original, mutated = item
            counts[verdict] += 1
            if verdict == 'survived':
                before = original.split('\n')[lineno - 1].strip()
                after = mutated.split('\n')[lineno - 1].strip()
                reason = equivalents.get((header, before, after))
                used_entries.add((header, before, after))
                (explained if reason else unexplained).append((header, lineno, what, before, after, reason))
            if n % 50 == 0:
                print(f'  {n}/{len(work_items)}  killed {counts["killed"]}  survived {counts["survived"]}  '
                      f'not compilable {counts["invalid"]}', flush=True)

    tested = counts['killed'] + counts['survived']
    print(f'\nmutation: {len(work_items)} mutants: {counts["killed"]} killed, {counts["survived"]} survived, '
          f'{counts["invalid"]} did not compile')
    if tested:
        print(f'mutation score: {100.0 * counts["killed"] / tested:.1f}% of {tested} compilable mutants killed '
              f'({len(explained)} survivors are documented as equivalent)')
    for title, group in (('SURVIVED - not explained', unexplained), ('survived - documented equivalent', explained)):
        if group:
            print(f'\n{title}:')
            for header, lineno, what, before, after, reason in group:
                print(f'  {header}:{lineno}  [{what}]')
                print(f'      was: {before}')
                print(f'      now: {after}')
                if reason:
                    print(f'      why it cannot matter: {reason}')
                else:
                    print(f'      entry: {header}{SEP}{before}{SEP}{after}{SEP}<reason>')
    # an entry that no surviving mutant matched is out of date (the line changed, or a test kills it now)
    stale = [k for k in equivalents if k not in used_entries and (not only or any(p in k[0] for p in only.split(',')))]
    if stale:
        print('\nentries in mutation_equivalent.txt that match no surviving mutant any more (remove them):')
        for header, before, after in stale:
            print(f'  {header}{SEP}{before}{SEP}{after}')
    if unexplained or stale:
        print(f'\nmutation: FAILED - {len(unexplained)} surviving mutant(s) without an explanation, '
              f'{len(stale)} outdated entr(ies).\n          For a survivor: add a test that fails for it, or, if the '
              'change truly cannot alter behaviour,\n          add its "entry" line with the reason to '
              'hhj-common/tools/mutation_equivalent.txt')
        return 1
    print('\nmutation: OK - every mutant is killed by a test or documented as equivalent')
    return 0


if __name__ == '__main__':
    sys.exit(main())
