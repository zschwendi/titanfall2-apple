#!/usr/bin/env python3
"""gate3-check: every module of gate3/bands.def is `present` in a `gate3 registry` log with a
count of lifted functions inside its band, and the log has no module the bands do not name.

    check_registry.py gate3/bands.def build/gate3/registry.log

Prints one line and exits 0, or names every module that is wrong and exits 1.  Reads the two
files given and nothing else.
"""

import sys


def main():
    bands_path, log_path = sys.argv[1:3]
    bands = {}
    for raw in open(bands_path):
        f = raw.split("#", 1)[0].split()
        if f:
            bands[f[0]] = (int(f[1]), int(f[2]))
    seen, bad = {}, []
    for line in open(log_path):
        f = line.split()
        if len(f) >= 3 and f[0] == "module":
            kv = dict(x.split("=", 1) for x in f[3:] if "=" in x)
            seen[f[1]] = (f[2], int(kv.get("lifted", -1)))
    for mod, (lo, hi) in bands.items():
        if mod not in seen:
            bad.append("%s is not in the registry" % mod)
        elif seen[mod][0] != "present":
            bad.append("%s is %s" % (mod, seen[mod][0]))
        elif not lo <= seen[mod][1] <= hi:
            bad.append("%s has %d lifted functions, outside %d to %d" % (mod, seen[mod][1], lo, hi))
    bad += ["%s is in the registry and has no band in %s" % (m, bands_path) for m in seen if m not in bands]
    if bad:
        print("gate3-check: FAILED: " + "; ".join(bad))
        return 1
    print("gate3-check: ok: %d modules present, %d lifted functions, each count inside its band"
          % (len(bands), sum(n for _, n in seen.values())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
