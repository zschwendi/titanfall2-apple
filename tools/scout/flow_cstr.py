#!/usr/bin/env python3
"""The C string at one or more addresses of a module's data (finding 37's tools).

    .venv/bin/python tools/scout/flow_cstr.py MODULE 0xRVA [0xRVA ...]

Prints `module+rva  'text'` for each address: the bytes up to the first 0, at most 200, as
flow_lib's cstr reads them (the same reading flow_show uses for the strings a function loads;
this is for a text too short for that heuristic, such as a two-letter constant a comparison
uses).  Terminal use only.  OriginSDK.dll is refused (flow_lib).
"""

import sys

from flow_lib import get


def main():
    M = get(sys.argv[1])
    for a in sys.argv[2:]:
        rva = int(a, 16)
        print("%s+%#x  %r" % (sys.argv[1].split(".")[0], rva, M.cstr(rva)))


if __name__ == "__main__":
    main()
