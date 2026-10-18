#!/usr/bin/env python3
"""Which lock-like functions each surveyed module imports, and tier0's lock-like exports
(finding 36, the queue-race survey).

    .venv/bin/python tools/scout/qrace_imports.py

Prints import and export names only. It answers "through which calls can a critical region
be entered in this module", which decides what qrace_scan.py has to recognise.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import qrace_lib as Q  # noqa: E402

PAT = re.compile(r"CriticalSection|SRWLock|Mutex|Lock|SList", re.I)


def main():
    for name in Q.survey_modules():
        m = Q.Module(name)
        by = {}
        for slot, (dll, n) in m.import_slots().items():
            if PAT.search(n):
                by.setdefault(dll, set()).add(n)
        print(name)
        for dll in sorted(by):
            print("   %-14s %s" % (dll, " ".join(sorted(by[dll]))))
    m = Q.Module("tier0.dll")
    print("tier0 exports matching:")
    for n, o, r in m.exports():
        if PAT.search(n) or re.search(r"TSQueue|TSList|Interlocked", n):
            print("   0x%-6x %s" % (r, n[:90]))


if __name__ == "__main__":
    main()
