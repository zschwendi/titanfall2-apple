#!/usr/bin/env python3
"""First look at `apitrace dump --color=never` output read from stdin: calls per function in
order of first use, and per block of frames the calls, creators, draws and maps.
usage: apitrace dump --color=never <trace> | apitrace_hist.py [frames-per-block]"""
import collections, re, sys

block = int(sys.argv[1]) if len(sys.argv) > 1 else 100
head = re.compile(r'^(\d+) (?:@\d+ )?([A-Za-z0-9_:]+)\(')
count = collections.Counter(); first = {}; frame = 0; calls = 0
per = collections.defaultdict(lambda: collections.Counter())
for line in sys.stdin:
    m = head.match(line)
    if not m:
        continue
    calls += 1
    fn = m.group(2)
    name = fn.split('::')[-1]
    count[fn] += 1
    first.setdefault(fn, (int(m.group(1)), frame))
    b = per[frame // block]
    b['calls'] += 1
    if name.startswith('Create'):
        b['creates'] += 1
        if name in ('CreateTexture2D', 'CreateTexture3D', 'CreateTexture1D'):
            b['textures'] += 1
        elif name == 'CreateBuffer':
            b['buffers'] += 1
        elif name.endswith('Shader'):
            b['shaders'] += 1
    elif name.startswith('Draw'):
        b['draws'] += 1
    elif name == 'Map':
        b['maps'] += 1
    elif name == 'Present':
        frame += 1
print('%d calls, %d frames (Present calls), %d distinct functions' % (calls, frame, len(count)))
print('\nper %d frames: calls, creators (textures, buffers, shaders), draws, maps' % block)
for k in sorted(per):
    b = per[k]
    print('  frames %5d-%5d  calls %8d  creates %6d (tex %5d, buf %5d, shader %5d)  draws %7d  maps %7d'
          % (k * block, k * block + block - 1, b['calls'], b['creates'], b['textures'], b['buffers'], b['shaders'], b['draws'], b['maps']))
print('\nfunctions in order of first use: first call number, frame of first use, count')
for fn, (c, f) in sorted(first.items(), key=lambda kv: kv[1][0]):
    print('  %9d  frame %5d  %9d  %s' % (c, f, count[fn], fn))
