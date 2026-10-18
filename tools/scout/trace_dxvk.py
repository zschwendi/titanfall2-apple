#!/usr/bin/env python3
"""trace_dxvk.py: question 9.  Print DXVK's own log for the game process, line by line, after the
identifier scrub (home path and user name hidden, long numbers, hex runs and GUIDs replaced).

usage: trace_dxvk.py LOGFILE [MAXLINES]
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_lib as T

path = sys.argv[1]
mx = int(sys.argv[2]) if len(sys.argv) > 2 else 400
n = 0
for line in open(path, errors='replace'):
    line = line.rstrip('\n')
    print(T.scrub_light(line, 200))
    n += 1
    if n >= mx:
        print('... truncated')
        break
