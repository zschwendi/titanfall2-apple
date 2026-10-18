#!/usr/bin/env python3
"""trace_peek.py: print a few consecutive lines of one process, for learning the log's format.

Only the channels given are printed; quoted strings are passed through redact_path() and
truncated; registry lines carry key and value NAMES only (never data) and are scrubbed further; debug
strings are never shown (channel debugstr is refused here).  Used to read the shapes of the file, loaddll, seh and process lines.

usage: trace_peek.py LOG PID_HEX CHANNELS(comma) START_TS_REL COUNT [LIMIT_BYTES [FUNC_SUBSTR_REGEX]]
       START_TS_REL is seconds after the first log line.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_lib as T

Q = re.compile(r'(L?)"((?:[^"\\]|\\.)*)"')


def show(msg, ch):
    s = msg.decode('utf-8', 'replace')
    f = T.scrub_raw if ch == 'reg' else T.redact_path
    s = Q.sub(lambda m: '"%s"' % f(m.group(2))[:150], s)
    return s[:260]


def main():
    log, pid, chans, start, count = sys.argv[1], int(sys.argv[2], 16), set(sys.argv[3].split(',')), float(sys.argv[4]), int(sys.argv[5])
    limit = int(sys.argv[6]) if len(sys.argv) > 6 else None
    frx = re.compile(sys.argv[7]) if len(sys.argv) > 7 else None
    if chans & {'debugstr'}:
        T.die('refused: debugstr lines are never peeked')
    t0 = None
    shown = 0
    for n, ts, p, tid, cls, ch, fn, msg in T.records(log, limit):
        if ts is None:
            continue
        if t0 is None:
            t0 = ts
        if p != pid or ch not in chans or ts - t0 < start:
            continue
        if frx and not frx.search(fn):
            continue
        print('%.3f %04x %s:%s:%s %s' % (ts - t0, tid, cls, ch, fn, show(msg, ch)))
        shown += 1
        if shown >= count:
            break


main()
