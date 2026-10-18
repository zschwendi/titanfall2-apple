#!/usr/bin/env python3
"""The reference trace's census held against our runtime: what a start needs next.

    tools/trace/census_runtime.py                 write docs/trace/census-runtime.tsv, print the totals
    tools/trace/census_runtime.py --next [N]      the first N functions a start calls that we lack (20);
                                                  with --imports, only names the game's DLLs import
    tools/trace/census_runtime.py --phase NAME    the lacking functions first called in one phase
    tools/trace/census_runtime.py --check         exit 1 when the committed table is not what this prints

Reads (nothing of the game, nothing remote):
  docs/trace/census-mission1.tsv   `cut.py mission1 census --commit-safe`: per function the first
                                   time, the count, the first call site, the calling modules
  docs/trace/phases.tsv            start time and name of each phase of that run
  runtime/tools/imports.def, imports_extra.def     what the game's DLLs import, by library
  runtime/shims/<library>/<Function>.c             a shim that exists: "have"
  tools/swarm/make_shim_tasks.py   the task rows: a function in a row with no shim yet: "row"

A census line names the DLL that Wine ran the call in, which is not always the library the game
imports from: kernel32 forwards its heap, list and condition-variable functions to ntdll, so the
log says ntdll.RtlAllocateHeap where the import is kernel32!HeapAlloc.  Each line is therefore
matched to an import by name: the same name in the same library, else the same name in another
library, else the name of FORWARDS, else the name with "Rtl" taken off.  A function that matches
no import is one the game reaches without importing it (through GetProcAddress, or it is called
by a DLL we do not lift): it is listed with library "-".

States: have (a shim file exists in a library the game imports the name from), row (a task row
holds it, no shim yet), none (an import with neither), unimported (no game DLL imports the name:
see above; our GetProcAddress answers 0 for it today, and for the C runtime's optional functions
the game then takes its older path).
Lines for calls INTO the game ((wndproc), (dllmain)) are left out: they are not functions we
answer.  Columns of the table: first_time, phase, state, library!Function as the game imports it,
count, the row's unit or -, the name in the log, first call site, calling modules.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CENSUS = os.path.join(ROOT, 'docs', 'trace', 'census-mission1.tsv')
PHASES = os.path.join(ROOT, 'docs', 'trace', 'phases.tsv')
TABLE = os.path.join(ROOT, 'docs', 'trace', 'census-runtime.tsv')

FORWARDS = {      # the name in the log -> the name the game imports (Wine's kernel32.spec forwards)
    'RtlAllocateHeap': 'HeapAlloc', 'RtlFreeHeap': 'HeapFree', 'RtlReAllocateHeap': 'HeapReAlloc',
    'RtlSizeHeap': 'HeapSize', 'RtlDeleteCriticalSection': 'DeleteCriticalSection',
    'RtlMoveMemory': 'RtlMoveMemory', 'RtlZeroMemory': 'RtlZeroMemory', 'RtlFillMemory': 'RtlFillMemory',
}


def imports():
    """{function: [library, ...]} of imports.def and imports_extra.def."""
    table = {}
    for name in ('imports.def', 'imports_extra.def'):
        with open(os.path.join(ROOT, 'runtime', 'tools', name)) as f:
            for line in f:
                p = line.split()
                if len(p) >= 2 and not line.startswith('#'):
                    lib = p[0].lower()
                    lib = lib[:-4] if lib.endswith('.dll') else lib
                    table.setdefault(p[1], []).append(lib)
    return table


def task_rows():
    """{(library, function): unit id} of every task row."""
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'swarm'))
    import make_shim_tasks as m
    rows = {}
    for t in m.TASKS:
        for member in t['members']:
            rows[(t['lib'], member)] = 'shim-' + t['id']
    return rows


def has_shim(lib, fn):
    return os.path.exists(os.path.join(ROOT, 'runtime', 'shims', lib, fn + '.c'))


def phases():
    out = []
    with open(PHASES) as f:
        for line in f:
            if line.strip() and not line.startswith('#'):
                t, name = line.rstrip('\n').split('\t')[:2]
                out.append((float(t), name))
    return sorted(out)


def phase_of(t, table):
    name = '-'
    for start, n in table:
        if t >= start:
            name = n
    return name


def match(dll, fn, imp):
    """(libraries, function) as the game imports it, or ([], fn)."""
    for name in (fn, FORWARDS.get(fn), fn[3:] if fn.startswith('Rtl') else None):
        if name and name in imp:
            libs = imp[name]
            return ([dll] if dll in libs else []) + [l for l in libs if l != dll], name
    return [], fn


def build():
    imp = imports()
    rows = task_rows()
    ph = phases()
    lines = []
    with open(CENSUS) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            t, count, name, site, tid, callers = line.rstrip('\n').split('\t')[:6]
            if name.startswith('('):
                continue
            dll, _, fn = name.partition('.')
            libs, imported = match(dll, fn, imp)
            where = libs or [dll]
            lib = libs[0] if libs else '-'
            unit = '-'
            for l in where:
                if has_shim(l, imported):
                    state, lib = 'have', l
                    break
            else:
                for l in where:
                    if (l, imported) in rows:
                        state, lib, unit = 'row', l, rows[(l, imported)]
                        break
                else:
                    state = 'none' if libs else 'unimported'
            lines.append((float(t), phase_of(float(t), ph), state, '%s!%s' % (lib, imported), int(count), unit,
                          name, site, callers))
    return lines


def render(lines):
    out = ['# GENERATED by tools/trace/census_runtime.py from docs/trace/census-mission1.tsv; do not edit.',
           '# first_time\tphase\tstate\timport\tcount\ttask_row\tname_in_log\tfirst_site\tcallers']
    for l in lines:
        out.append('%.3f\t%s\t%s\t%s\t%d\t%s\t%s\t%s\t%s' % l)
    return '\n'.join(out) + '\n'


def totals(lines):
    by = {}
    for l in lines:
        by.setdefault(l[2], []).append(l)
    calls = sum(l[4] for l in lines)
    text = ['%d functions called by the game\'s DLLs and the executable in the capture, %d calls' % (len(lines), calls)]
    for state in ('have', 'row', 'none', 'unimported'):
        ls = by.get(state, [])
        text.append('  %-4s %3d functions, %8d calls (%.1f%% of the calls)' % (
            state, len(ls), sum(l[4] for l in ls), 100.0 * sum(l[4] for l in ls) / max(calls, 1)))
    return '\n'.join(text)


def show(lines):
    for l in lines:
        print('%9.3f  %-22s %-10s %-44s x%-8d %-22s first at %s' % (l[0], l[1], l[2], l[3], l[4], l[5], l[7]))


def main(argv):
    lines = build()
    lacking = [l for l in lines if l[2] != 'have']
    if '--imports' in argv:          # leave the unimported names out
        argv = [x for x in argv if x != '--imports']
        lacking = [l for l in lacking if l[2] != 'unimported']
    if argv[:1] == ['--next']:
        show(lacking[:int(argv[1]) if len(argv) > 1 else 20])
    elif argv[:1] == ['--phase']:
        show([l for l in lacking if l[1] == argv[1]])
    elif argv[:1] == ['--check']:
        with open(TABLE) as f:
            if f.read() != render(lines):
                print('census_runtime: docs/trace/census-runtime.tsv is stale; run tools/trace/census_runtime.py')
                return 1
    else:
        with open(TABLE, 'w') as f:
            f.write(render(lines))
        print(totals(lines))
        print('written: docs/trace/census-runtime.tsv; the first lacking: tools/trace/census_runtime.py --next')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
