#!/usr/bin/env python3
"""Build afp_safety.c at several optimisation levels and run it: does leaving FPCR.AH on change
anything observable for well-defined inputs?  Writes out/safety_*.txt and prints a digest."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
os.makedirs(OUT, exist_ok=True)
SRC = os.path.join(HERE, "afp_safety.c")
BUILDS = [("O0", ["-O0"]), ("O2", ["-O2"]), ("O3", ["-O3"]), ("O3-fastmath", ["-O3", "-ffast-math"]),
          ("O2-accelerate", ["-O2", "-DUSE_ACCELERATE", "-framework", "Accelerate", "-Wno-deprecated-declarations"])]
tot_real = 0
for name, flags in BUILDS:
    exe = os.path.join(OUT, f"afp_safety_{name}")
    r = subprocess.run(["clang", *flags, "-Wall", "-Wno-unused-function", SRC, "-o", exe], capture_output=True, text=True)
    if r.returncode:
        print(name, "BUILD FAILED", r.stderr[-400:])
        continue
    r = subprocess.run([exe], capture_output=True, text=True)
    txt = r.stdout
    with open(os.path.join(OUT, f"safety_{name}.txt"), "w") as f:
        f.write(txt)
    lines = txt.splitlines()
    rows = []
    for l in lines:
        if l.startswith("#") or l.startswith("RESULT") or l.startswith("      "):
            continue
        parts = l.split("|")
        if len(parts) < 4:
            continue
        real = [int(c.split("REAL=")[1].split()[0]) for c in parts[1:4]]
        nanres = [int(c.split("nan-res=")[1].split()[0]) for c in parts[1:4]]
        rows.append((parts[0].strip(), real, nanres))
    bad_ah = [(n, r[0]) for n, r, _ in rows if r[0]]
    bad_nep = [(n, r[1]) for n, r, _ in rows if r[1]]
    bad_fiz = [(n, r[2]) for n, r, _ in rows if r[2]]
    print(f"== {name}: {len(rows)} functions/kernels; REAL differences vs FPCR=0 under AH: {len(bad_ah)}; under AH+NEP: {len(bad_nep)}; under AH+FIZ+FZ: {len(bad_fiz)}")
    for n, c in bad_ah:
        print(f"     AH: {n}: {c}")
    tot_real += len(bad_ah)
print("TOTAL function/build pairs with REAL differences under AH:", tot_real)
