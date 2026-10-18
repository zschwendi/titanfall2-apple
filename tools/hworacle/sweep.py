#!/usr/bin/env python3
"""Are Intel's RCPSS / RSQRTSS and the M3's FRECPE / FRSQRTE the same function?

    .venv/bin/python tools/hworacle/sweep.py            (or: make hworacle-sweep)
    .venv/bin/python tools/hworacle/sweep.py --rc 0     only round-to-nearest (4 MXCSR settings instead of 16)
    .venv/bin/python tools/hworacle/sweep.py --offline  from the cache only

The design question (brief, item 5): if the two are bit-identical for every input, under the
float mode lifted code runs in (harness/fpmode.h, tf_fp_enter_guest), the lifter can translate
rcpps / rsqrtps as one native instruction.

Every one of the 2^32 binary32 bit patterns is compared, for both operations, under each MXCSR
setting (rounding control x DAZ x FTZ).  The x86 side is a small free-running guest program run
by the hardware oracle, once per chunk of 2^23 inputs (one sign and exponent): it executes the
instruction on every input and folds the results into a 64-bit hash, h = (h ^ result) * K.  The
arm64 side (sweep_m3.c) computes the same hash over FRECPE / FRSQRTE, scalar and vector form.
Equal hashes: the chunk is identical (two streams that differ in exactly one result cannot
collide, since each step is a bijection of h; several differences collide with probability about
2^-52).  For chunks whose hashes differ, and for a fixed set of chunks in any case (as a check of
the method), a second guest program writes the x86 results as run-length records, from which
sweep_m3 reconstructs the x86 result of every input and lists the inputs that differ.

Controls: the same arm64 instructions with the host's default FPCR must NOT match (they do not);
the run-length path must reproduce the hash path's verdict on the chunks both cover; known values
from the 2026-10-08 probe (rcpss(1.0) = 3f7ff000, ...) must appear in the records.
"""

import argparse
import json
import os
import struct
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)
import client as C                                    # noqa: E402

OUT = os.path.join(REPO, "build", "hworacle")
M3 = os.path.join(OUT, "sweep_m3")
K, SEED = 0x9E3779B97F4A7C15, 0x0123456789ABCDEF      # as in sweep_m3.c
CODE_PAGE = 0x0000200000100000
STOP = CODE_PAGE + 0x1000                             # the page after the code: unmapped, so arriving there ends the run
DATA = 0x0000210000000000
DATA_SIZE = 1 << 17
OPCODE = {"rcp": 0x53, "rsqrt": 0x52}
ALWAYS_RLE = (0, 1, 2, 126, 127, 128, 129, 253, 254, 255, 256, 383, 384, 510, 511)     # chunks checked both ways
KNOWN = {"rcp": {0x3F800000: 0x3F7FF000, 0x40400000: 0x3EAAA000}, "rsqrt": {0x3F800000: 0x3F7FF000, 0x40000000: 0x3F34F800}}


def log(msg):
    print(msg, file=sys.stderr, flush=True)


def hash_program(op):
    """loop: movd xmm1, esi; rcpss xmm0, xmm1; movd eax, xmm0; xor rbx, rax; imul rbx, r8;
             inc rsi; cmp rsi, rdx; jne loop        (falls off the end of the page into STOP)"""
    body = bytes([0x66, 0x0F, 0x6E, 0xCE, 0xF3, 0x0F, OPCODE[op], 0xC1, 0x66, 0x0F, 0x7E, 0xC0,
                  0x48, 0x31, 0xC3, 0x49, 0x0F, 0xAF, 0xD8, 0x48, 0xFF, 0xC6, 0x48, 0x39, 0xD6])
    return body + bytes([0x75, (-(len(body) + 2)) & 0xFF])


def rle_program(op, kind):
    """The same loop, writing a record (input, result) to [rdi] whenever the result (kind A) or
    result XOR input (kind B) differs from the previous one, which is kept in rbx."""
    head = bytes([0x66, 0x0F, 0x6E, 0xCE, 0xF3, 0x0F, OPCODE[op], 0xC1, 0x66, 0x0F, 0x7E, 0xC0])
    store = bytes([0x89, 0x37, 0x89, 0x47, 0x04, 0x48, 0x83, 0xC7, 0x08])      # mov [rdi], esi; mov [rdi+4], eax; add rdi, 8
    if kind == "A":
        mid = bytes([0x48, 0x39, 0xD8, 0x74, len(store) + 3]) + store + bytes([0x48, 0x89, 0xC3])    # cmp rax, rbx; je; ...; mov rbx, rax
    else:
        mid = bytes([0x89, 0xC1, 0x31, 0xF1, 0x48, 0x39, 0xD9, 0x74, len(store) + 3]) + store + bytes([0x48, 0x89, 0xCB])
    body = head + mid + bytes([0x48, 0xFF, 0xC6, 0x48, 0x39, 0xD6])
    return body + bytes([0x75, (-(len(body) + 2)) & 0xFF])


def code_regions(code, with_data):
    page = bytearray(0x1000)
    page[0x1000 - len(code):] = code
    regs = [C.Region(CODE_PAGE, bytes(page), C.PROT_R | C.PROT_X)]
    if with_data:
        regs.append(C.Region(DATA, DATA_FILL, C.PROT_R | C.PROT_W))
    return tuple(regs), STOP - len(code), len(code)


DATA_FILL = C.Blob(b"\xA5" * DATA_SIZE)               # not zeroes: a record byte equal to its old value is not reported
_REGIONS = {}


def program_case(op, kind, mxcsr, start, end):
    key = (op, kind)
    if key not in _REGIONS:
        code = hash_program(op) if kind == "hash" else rle_program(op, kind)
        _REGIONS[key] = code_regions(code, kind != "hash")
    regs, rip, n = _REGIONS[key]
    if kind == "hash":
        gpr = C.gpr_list(rsi=start, rdx=end, rbx=SEED, r8=K)
    else:
        gpr = C.gpr_list(rsi=start, rdx=end, rbx=0xFFFFFFFF00000000, rdi=DATA)
    return C.Case(regs, rip, gpr=gpr, mxcsr=mxcsr, max_insns=0, code_len=n, stops=[STOP])


def build_m3():
    src = os.path.join(HERE, "sweep_m3.c")
    if not os.path.exists(M3) or os.path.getmtime(M3) < os.path.getmtime(src):
        os.makedirs(OUT, exist_ok=True)
        subprocess.run(["clang", "-O2", "-fno-vectorize", "-fno-slp-vectorize", "-Wall", "-Wextra", "-Werror",
                        "-I", os.path.join(REPO, "harness"), "-o", M3, src], check=True)


def m3_hashes(op, mxcsr, fpmode, form, jobs=8):
    def part(k):
        p = subprocess.run([M3, "hash", op, "%x" % mxcsr, fpmode, form, str(k * 512 // jobs), str(512 // jobs)],
                           capture_output=True, text=True, check=True)
        return [int(ln.split()[1], 16) for ln in p.stdout.splitlines()]
    with ThreadPoolExecutor(jobs) as ex:
        return [h for chunk in ex.map(part, range(jobs)) for h in chunk]


def x86_records(ora, op, mxcsr, ranges, kind_of_range):
    """Run-length records of the x86 results for each (start, end) range: {range: bytes}.  A
    range whose records do not fit the output region is split in two and asked again."""
    out, todo = {}, [(r, r) for r in ranges]
    parts = {r: {} for r in ranges}
    while todo:
        cases = [program_case(op, kind_of_range(top), mxcsr, a, b) for top, (a, b) in todo]
        res = ora.run(cases)
        nxt = []
        for (top, (a, b)), c, r in zip(todo, cases, res):
            if r.outcome == C.NO_ANSWER:
                parts[top] = None
                continue
            if parts[top] is None:
                continue
            full = (r.outcome == C.FAULT and r.sig_addr == DATA + DATA_SIZE) or r.mem_truncated
            if full and b - a > 1:
                mid = (a + b) // 2
                nxt += [(top, (a, mid)), (top, (mid, b))]
                continue
            if r.outcome != C.DONE or not r.at_stop or r.gpr[6] != b or r.mxcsr != mxcsr:
                raise RuntimeError("run-length program over %#x..%#x: %s (mxcsr %#x)" % (a, b, r.describe(), r.mxcsr))
            parts[top][a] = C.final_mem(c, r, DATA, r.gpr[7] - DATA)
        todo = nxt
    for top in ranges:
        out[top] = None if parts[top] is None else b"".join(parts[top][a] for a in sorted(parts[top]))
    return out


def m3_check(op, mxcsr, fpmode, form, start, count, kind, records, tag):
    path = os.path.join(OUT, "sweep", "%s.rle" % tag)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(records)
    p = subprocess.run([M3, "check", op, "%x" % mxcsr, fpmode, form, "%x" % start, str(count), kind, path],
                       capture_output=True, text=True, check=True)
    os.remove(path)
    diffs, total = [], None
    for ln in p.stdout.splitlines():
        f = ln.split()
        if f[0] == "diff":
            diffs.append({"input": f[1], "arm": f[2], "x86": f[3]})
        elif f[0] == "total":
            total = {"differing_inputs": int(f[1])}
            total.update({f[i]: int(f[i + 1]) for i in range(2, len(f), 2)})
    return total, diffs


def lookup(records, kind, x):
    """x86 result for input x from run-length records."""
    best = None
    for i in range(0, len(records), 8):
        a, v = struct.unpack_from("<II", records, i)
        if a <= x:
            best = (a, v)
    return best[1] if kind == "A" else x ^ best[0] ^ best[1]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--rc", default="0,1,2,3", help="MXCSR rounding-control values to cover (default all four)")
    ap.add_argument("--ops", default="rcp,rsqrt")
    ap.add_argument("--offline", action="store_true")
    args = ap.parse_args()
    build_m3()
    ora = C.Oracle(family="sweep", offline=args.offline, batch_seconds=20.0, time_limit_ms=5000, verbose=True)
    kind_of_range = lambda r: "B" if (r[0] >> 23) & 0xFF == 0xFF else "A"                 # noqa: E731
    settings = [(rc, daz, ftz) for rc in [int(x) for x in args.rc.split(",")] for daz in (0, 1) for ftz in (0, 1)]
    report = {"settings": [], "inputs_per_setting": 1 << 32}
    t_x86 = t_arm = 0.0
    try:
        for op in args.ops.split(","):
            for rc, daz, ftz in settings:
                mxcsr = 0x1F80 | rc << 13 | daz << 6 | ftz << 15
                name = "%s rc=%d daz=%d ftz=%d" % (op, rc, daz, ftz)
                t0 = time.time()
                res = ora.run([program_case(op, "hash", mxcsr, c << 23, (c + 1) << 23) for c in range(512)])
                t_x86 += time.time() - t0
                if any(r.outcome == C.NO_ANSWER for r in res):
                    log("%s: no hardware answer for some chunks (offline)" % name)
                    report["settings"].append({"name": name, "status": "no hardware answer"})
                    continue
                for c, r in enumerate(res):
                    if r.outcome != C.DONE or not r.at_stop or r.gpr[6] != (c + 1) << 23 or r.mxcsr != mxcsr:
                        raise RuntimeError("%s chunk %d: %s, rsi %#x, mxcsr %#x" % (name, c, r.describe(), r.gpr[6], r.mxcsr))
                x86 = [r.gpr[3] for r in res]
                t0 = time.time()
                arm = m3_hashes(op, mxcsr, "guest", "scalar")
                arm_v = m3_hashes(op, mxcsr, "guest", "vector")
                host = m3_hashes(op, mxcsr, "host", "scalar")
                t_arm += time.time() - t0
                bad = [c for c in range(512) if x86[c] != arm[c]]
                bad_v = [c for c in range(512) if x86[c] != arm_v[c]]
                ctl = sum(1 for c in range(512) if x86[c] != host[c])
                # the run-length path: every differing chunk, and a fixed set as a check of the method
                chunks = sorted(set(bad) | set(bad_v) | set(ALWAYS_RLE))
                ranges = [(c << 23, (c + 1) << 23) for c in chunks]
                t0 = time.time()
                recs = x86_records(ora, op, mxcsr, ranges, kind_of_range)
                t_x86 += time.time() - t0
                differing, examples, by_kind, method_ok, n_records = 0, [], {}, True, 0
                for c, rg in zip(chunks, ranges):
                    if recs[rg] is None:
                        continue
                    n_records += len(recs[rg]) // 8
                    for form, badset in (("scalar", bad), ("vector", bad_v)):
                        total, diffs = m3_check(op, mxcsr, "guest", form, rg[0], 1 << 23, kind_of_range(rg), recs[rg],
                                                "%s_%x_%d_%s" % (op, mxcsr, c, form))
                        if (total["differing_inputs"] > 0) != (c in badset):
                            method_ok = False          # the two ways of comparing a chunk must agree
                        if form == "scalar":
                            differing += total["differing_inputs"]
                            for k, v in total.items():
                                if k != "differing_inputs" and v:
                                    by_kind[k] = by_kind.get(k, 0) + v
                            examples += diffs[:6]
                known_ok = True
                for x, want in KNOWN[op].items():
                    rg = ((x >> 23) << 23, ((x >> 23) + 1) << 23)
                    if rg in recs and recs[rg] is not None and lookup(recs[rg], "A", x) != want:
                        known_ok = False
                entry = {"name": name, "mxcsr": "%#x" % mxcsr, "inputs_compared": 1 << 32, "chunks": 512,
                         "chunks_differing_scalar": len(bad), "chunks_differing_vector": len(bad_v),
                         "differing_chunk_list": bad[:64], "differing_inputs_in_listed_chunks": differing,
                         "differing_by_kind_of_input": by_kind, "examples": examples[:24],
                         "chunks_also_compared_by_records": len(chunks), "x86_records": n_records,
                         "both_methods_agree": method_ok, "known_values_present": known_ok,
                         "control_host_fpcr_chunks_differing": ctl}
                report["settings"].append(entry)
                log("%-26s x86 = arm scalar on %3d/512 chunks, vector on %3d/512; %d differing inputs; methods agree: %s; "
                    "known values: %s; control (host FPCR) differs on %d chunks" % (
                        name, 512 - len(bad), 512 - len(bad_v), differing, method_ok, known_ok, ctl))
    except C.OracleUnavailable as e:
        print("x86 machine unavailable: %s" % e, file=sys.stderr)
        return 3
    report["x86_seconds"], report["arm_seconds"] = round(t_x86, 1), round(t_arm, 1)
    report["cpu"] = ora.header["cpu"] if ora.header else None
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "sweep.json"), "w") as f:
        json.dump(report, f, indent=1)
    done = [s for s in report["settings"] if "inputs_compared" in s]
    same = [s for s in done if s["chunks_differing_scalar"] == 0 and s["chunks_differing_vector"] == 0]
    sound = all(s["both_methods_agree"] and s["known_values_present"] and s["control_host_fpcr_chunks_differing"] > 0 for s in done)
    print("rcp/rsqrt sweep: %d settings, %d inputs each, %d results compared in all" % (len(done), 1 << 32, len(done) << 32))
    print("  bit-identical in %d of %d settings (scalar and vector FRECPE/FRSQRTE, guest float mode)" % (len(same), len(done)))
    for s in done:
        if s not in same:
            print("  DIFFERENT %-26s %d chunks, %d inputs: %s; e.g. %s" % (
                s["name"], s["chunks_differing_scalar"], s["differing_inputs_in_listed_chunks"],
                s["differing_by_kind_of_input"], s["examples"][:3]))
    print("  method checks (hash and run-length comparison agree, known values present, host-FPCR control differs): %s"
          % ("all hold" if sound else "FAILED"))
    print("  sent to the x86 machine: %d cases, %d from the cache; x86 %.0f s, arm %.0f s" % (
        ora.sent, ora.from_cache, t_x86, t_arm))
    return 0 if sound and len(done) == len(report["settings"]) else 1


if __name__ == "__main__":
    sys.exit(main())
