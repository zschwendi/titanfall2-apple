#!/usr/bin/env python3
"""Check a slice against the tool output it was written from.

    tools/trace/check_slice.py docs/trace/<slice>.md build/trace/slices/<input>...

Every call site written module+0xrva, every time written N.NNN and every number of five or more
digits in the slice must occur in one of the input files (commas in numbers are ignored).  What
is listed as "not in the inputs" is either wrong or a sum the writer made: recompute those.
Exit status 0 always: this is a list for a reader, not a gate.
"""
import re
import sys


def main(argv):
    slice_path, inputs = argv[0], argv[1:]
    with open(slice_path) as f:
        text = f.read()
    hay = ''
    for p in inputs:
        with open(p, errors='replace') as f:
            hay += f.read() + '\n'
    hay_nocomma = hay.replace(',', '')
    sites = set(re.findall(r'[\w.]+\+0x[0-9a-f]+', text))
    times = set(re.findall(r'(?<![\w.])\d{1,3}\.\d{3}(?![\w.])', text))
    nums = set(n.replace(',', '') for n in re.findall(r'(?<![\w.+x])\d[\d,]{3,}\d(?![\w.])', text))
    for kind, toks, h in (('site', sites, hay), ('time', times, hay), ('number', nums, hay_nocomma)):
        miss = sorted(t for t in toks if t not in h)
        print('%-6s %4d in the slice, %3d not in the inputs%s' % (kind, len(toks), len(miss),
                                                              (': ' + ' '.join(miss)) if miss else ''))
    print('lines %d' % text.count('\n'))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
