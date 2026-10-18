#!/usr/bin/env python3
"""sweep: does DXMT's standalone converter take every shader of the recording?

    sweep.py pairs TRACE DATA           which vertex shader stood before each geometry shader at a
                                        draw: DATA/gs-pairs.tsv (one pass over the trace)
    sweep.py run AIRCONV DATA NAME [--jobs 6] [--limit N] [--skip-flagged OTHER]
                                        AIRCONV over every distinct container of DATA/shaders.tsv;
                                        DATA/sweep-NAME.tsv and a table on standard output.
                                        --skip-flagged: leave out the runs for which
                                        DATA/sweep-OTHER.tsv has an `unhandled` line (see below)
    sweep.py table DATA NAME            the table again from DATA/sweep-NAME.tsv

DATA is what tracefile.py `extract` wrote (build/trace/data of the main checkout: derived from
the game, git-ignored).  Nothing of a shader is printed or kept: the converter's output goes to
a scratch directory and is deleted after each shader; what is kept per shader is its hash, its
stage, how the tool ended, the tool's message with every digit replaced by `#` (its class), and
the numbers N of its lines `unhandled dxbc instruction N`.

How each stage is run (read in third_party/dxmt/src/airconv/airconv_cli.cpp; the clone is not
changed and nothing in it is built here):
    vs, ps, cs   airconv FILE -o OUT
    gs           airconv FILE --vertex-before-geometry VS -o OUT, once for each vertex shader the
                 recording drew it with (gs-pairs.tsv); and the vertex shader of each pair as
                 airconv VS --geometry-after-vertex GS -o OUT (rows of stage `vs+gs`)
A shader counts as converted when the tool exits 0; `died` is a signal (an assert build aborts
where the release build prints its `unhandled` line).

THE ORDER OF THE TWO BUILDS (docs/handoff/pic-fork.md, "For pic-data"; the planner's rule).  The
release build first: it gives the opcode numbers.  Then the assert build with `--skip-flagged
release`, so that it aborts only where something else is wrong: every abort of the assert build
makes macOS write a crash report `airconv-*.ips` into ~/Library/Logs/DiagnosticReports, and a
crash report may carry bytes of the process, which here holds a shader of the game.  `run`
counts those files before and after and names the new ones; it never opens or removes one.
"""
import sys, os, re, subprocess, tempfile, shutil, time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
UNHANDLED = re.compile(r'^unhandled dxbc instruction (\d+)\s*$')
DRAWS = ('Draw', 'DrawIndexed', 'DrawInstanced', 'DrawIndexedInstanced', 'DrawAuto', 'DrawInstancedIndirect', 'DrawIndexedInstancedIndirect')


def blob(data, h):
    return os.path.join(data, 'blobs', h[:2], h)


def pairs(trace, data):
    sys.path.insert(0, HERE)
    import tracefile as T
    import hashlib
    stream = T.open_trace(trace)
    code, vs, gs, found = {}, None, None, {}
    for call in T.Reader(stream).calls():
        m = call.name.split('::')[-1]
        if m in ('CreateVertexShader', 'CreateGeometryShader', 'CreateGeometryShaderWithStreamOutput'):
            b = [d for k, _, d in T.blobs(call) if k == 'B']
            out = call.args[-1] if call.args else None
            if b and isinstance(out, list) and out and out[0]:
                code[int(out[0])] = hashlib.sha256(b[0]).hexdigest()
        elif m == 'VSSetShader':
            vs = code.get(int(call.args[1])) if call.args[1] else None
        elif m == 'GSSetShader':
            gs = code.get(int(call.args[1])) if call.args[1] else None
        elif m in DRAWS and gs and vs:
            e = found.setdefault((vs, gs), [0, call.no])
            e[0] += 1
    stream.close()
    with open(os.path.join(data, 'gs-pairs.tsv'), 'w') as f:
        for (v, g), e in sorted(found.items(), key=lambda x: x[1][1]):
            f.write('%s\t%s\t%d\t%d\n' % (v, g, e[0], e[1]))
    print('%d pairs of a vertex and a geometry shader at a draw (%d geometry shaders, %d draws)'
          % (len(found), len({g for _, g in found}), sum(e[0] for e in found.values())))


def one(job):
    exe, data, tmp, i, stage, h, other = job
    out = os.path.join(tmp, '%d.out' % i)
    cmd = [exe, blob(data, h), '-o', out]
    if stage == 'gs' and other:
        cmd[2:2] = ['--vertex-before-geometry', blob(data, other)]
    if stage == 'vs+gs':
        cmd[2:2] = ['--geometry-after-vertex', blob(data, other)]
    t0 = time.time()
    try:
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=300)
        status, so, se = p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        status, so, se = 'timeout', b'', b''
    size = os.path.getsize(out) if os.path.exists(out) else 0
    if os.path.exists(out):
        os.remove(out)
    ops, stray = [], 0
    for line in so.decode('latin-1').splitlines():
        m = UNHANDLED.match(line)
        if m:
            ops.append(int(m.group(1)))
        elif line.strip():
            stray += 1
    lines = [l for l in se.decode('latin-1').splitlines() if l.strip()]
    msg = ''
    if status != 0 and lines:
        pick = [l for l in lines if 'ssert' in l or 'rror' in l or 'nsupported' in l or 'nhandled' in l] or lines
        msg = re.sub(r'0x[0-9a-fA-F]+|\d', '#', pick[0])[:160].replace('\t', ' ')
    result = 'converted' if status == 0 else 'timeout' if status == 'timeout' else 'died (signal %d)' % -status if status < 0 else 'refused'
    return (h, stage, other or '-', result, msg, ','.join(str(o) for o in sorted(set(ops))), len(ops), stray, size > 0, time.time() - t0)


def reports():
    d = os.path.expanduser('~/Library/Logs/DiagnosticReports')
    try:
        return {n for n in os.listdir(d) if n.startswith('airconv') and n.endswith('.ips')}
    except OSError:
        return set()


def run(exe, data, name, jobs=6, limit=None, skip=None):
    os.nice(10)
    shaders = [l.rstrip('\n').split('\t') for l in open(os.path.join(data, 'shaders.tsv'))]
    pair_file = os.path.join(data, 'gs-pairs.tsv')
    gs_pairs = [l.split('\t')[:2] for l in open(pair_file)] if os.path.exists(pair_file) else []
    todo = []
    for h, _, stages, _, _ in shaders:
        for stage in stages.split(','):
            if stage == 'gs':
                todo += [(stage, h, v) for v, g in gs_pairs if g == h] or [(stage, h, None)]
            else:
                todo.append((stage, h, None))
    todo += [('vs+gs', v, g) for v, g in gs_pairs]
    if skip:
        flagged = {(r[0], r[1], r[2]) for r in (l.rstrip('\n').split('\t') for l in open(os.path.join(data, 'sweep-%s.tsv' % skip))) if r[5]}
        kept = [t for t in todo if (t[1], t[0], t[2] or '-') not in flagged]
        print('%d runs left out: sweep-%s.tsv has an `unhandled` line for them' % (len(todo) - len(kept), skip))
        todo = kept
    if limit:
        todo = todo[:limit]
    before = reports()
    tmp = tempfile.mkdtemp(prefix='sweep-')
    t0 = time.time()
    try:
        with ThreadPoolExecutor(jobs) as pool:
            rows = list(pool.map(one, [(exe, data, tmp, i, s, h, o) for i, (s, h, o) in enumerate(todo)]))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    with open(os.path.join(data, 'sweep-%s.tsv' % name), 'w') as f:
        for r in rows:
            f.write('\t'.join(str(x) for x in r[:9]) + '\t%.2f\n' % r[9])
    print('%d runs of %s in %.0f s (%d at once)' % (len(rows), exe, time.time() - t0, jobs))
    if any(r[3].startswith('died') for r in rows):
        time.sleep(10)                              # the system writes its report after the process is gone
    new = sorted(reports() - before)
    print('crash reports of the tool in ~/Library/Logs/DiagnosticReports: %d before, %d new%s\n'
          % (len(before), len(new), ': ' + ', '.join(new) if new else ''))
    table(data, name)


def table(data, name):
    rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(data, 'sweep-%s.tsv' % name))]
    stages = sorted({r[1] for r in rows})
    print('| stage | runs | converted, no `unhandled` line | converted with `unhandled` lines | refused | died | timeout | no output file |')
    print('|---|---|---|---|---|---|---|---|')
    for s in stages + ['all']:
        sel = [r for r in rows if s == 'all' or r[1] == s]
        conv = [r for r in sel if r[3] == 'converted']
        print('| %s | %d | %d | %d | %d | %d | %d | %d |' % (s, len(sel), sum(1 for r in conv if not r[5]), sum(1 for r in conv if r[5]),
              sum(1 for r in sel if r[3] == 'refused'), sum(1 for r in sel if r[3].startswith('died')), sum(1 for r in sel if r[3] == 'timeout'),
              sum(1 for r in conv if r[8] != 'True')))
    ops = {}
    for r in rows:
        for o in filter(None, r[5].split(',')):
            e = ops.setdefault(int(o), {})
            e[r[1]] = e.get(r[1], 0) + 1
    print('\n`unhandled dxbc instruction N`, by N: runs it touches, by stage\n')
    for o in sorted(ops):
        print('- %d: %s' % (o, ', '.join('%s %d' % kv for kv in sorted(ops[o].items()))))
    if not ops:
        print('- none')
    classes = {}
    for r in rows:
        if r[3] != 'converted':
            e = classes.setdefault((r[3], r[4]), {})
            e[r[1]] = e.get(r[1], 0) + 1
    print('\nRuns that did not convert, by how the tool ended and its message (digits as #)\n')
    for (how, msg), e in sorted(classes.items(), key=lambda x: -sum(x[1].values())):
        print('- %s, `%s`: %s' % (how, msg, ', '.join('%s %d' % kv for kv in sorted(e.items()))))
    if not classes:
        print('- none')
    print('\nstray lines on standard output (not `unhandled`): %d; seconds per run: median %.2f, longest %.2f'
          % (sum(int(r[7]) for r in rows), sorted(float(r[9]) for r in rows)[len(rows) // 2], max(float(r[9]) for r in rows)))


if __name__ == '__main__':
    a = sys.argv
    if len(a) == 4 and a[1] == 'pairs':
        pairs(a[2], a[3])
    elif len(a) >= 5 and a[1] == 'run':
        opts = dict(zip(a[5::2], a[6::2]))
        run(a[2], a[3], a[4], int(opts.get('--jobs', 6)), int(opts['--limit']) if '--limit' in opts else None, opts.get('--skip-flagged'))
    elif len(a) == 4 and a[1] == 'table':
        table(a[2], a[3])
    else:
        sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
        sys.exit(2)
