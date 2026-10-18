#!/usr/bin/env python3
"""Self-tests for the lifter and the differential harness.  No game files are needed.

    .venv/bin/python tests/run_selftests.py          (or: make test)

Small x86-64 programs and single instructions, given under tests/vectors/ as hex bytes with
their assembly, are wrapped in a minimal in-memory image and pushed through exactly the
same lift -> compile -> oracle -> compare path as a game module.  Several tests install a
deliberately wrong translation and require the harness to report it; one makes the lifted
code write through a wild pointer and requires the harness to survive and report it.

The single-instruction vectors of an instruction family are in tests/vectors/<family>.py and
are found by listing that directory (tests/vectors/__init__.py); each family's vectors run in
an image of their own.
"""

import json
import os
import random
import re
from collections import Counter
import shutil
import signal
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path[:0] = [REPO, os.path.join(REPO, "harness"), HERE]

import glob                                       # noqa: E402
import subprocess                                 # noqa: E402

import capstone                                   # noqa: E402

import difftest                                   # noqa: E402
import floatcase as FC                            # noqa: E402
import oraclefix as OF                            # noqa: E402
import statefix as SF                             # noqa: E402
import vecstate as VS                             # noqa: E402
from lifter import discover as D                  # noqa: E402
from lifter import lift as L                      # noqa: E402
from lifter import stops as lifter_stops          # noqa: E402
from lifter import translate                      # noqa: E402
from lifter.image import Image                    # noqa: E402
from vectors import (FAMILY_REFUSED, FAMILY_VECTORS, JUMP_ABS_TABLES, JUMP_BYTES, JUMP_LABELS, JUMP_PROGRAMS, JUMP_PTR_TABLES,  # noqa: E402
                     JUMP_TABLES, LABELS, LEA_DATA, LEA_LABELS, LEA_PROGRAMS, NAME_CLASH_VECTORS, ORACLE_BUG_VECTORS,
                     PROGRAMS, RET_PROGRAMS, TAIL_PROGRAMS, UNSUPPORTED_RELOCS, UNSUPPORTED_VECTORS)
from vectors._stops import (STOP_CPUID, STOP_LABELS, STOP_PROGRAMS, STOP_REFUSED, STOP_RELOCS, STOP_TABLE,  # noqa: E402
                            STOP_VECTORS)

OUT = os.path.join(REPO, "harness", "out", "selftest")
PREFERRED_BASE = 0x180000000
FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def prog_bytes(name):
    return b"".join(bytes.fromhex(h) for h, _ in PROGRAMS[name][1])


def prog_end(name):
    return PROGRAMS[name][0] + len(prog_bytes(name))


def insn_rva(name, text):
    """RVA of the first instruction in program `name` whose assembly is `text`."""
    rva = PROGRAMS[name][0]
    for h, t in PROGRAMS[name][1]:
        if t == text:
            return rva
        rva += len(h) // 2
    raise KeyError(text)


def program_image(name="selftest"):
    """Code at 0x1000, writable data at 0x3000, two import slots at 0x4000, unwind data at 0x5000."""
    chunks = {rva: prog_bytes(n) for n, (rva, _) in PROGRAMS.items()}
    chunks[prog_end("noret_fn")] = b"\xcc\xcc"                       # int3 padding after a no-return call
    # data0, data1, data2, ptr0 (a relocated pointer to data1)
    chunks[0x3000] = struct.pack("<QQIIQ", 0x0123456789ABCDEF, 0x1111111111111111, 5, 0, PREFERRED_BASE + 0x3008)
    # jump table of switch_fn: four RVAs, then something that is not a target
    chunks[0x3040] = struct.pack("<5I", LABELS["sw_case0"], LABELS["sw_case1"], LABELS["sw_case2"],
                                 LABELS["sw_case3"], 0xFFFFFFFF)
    # unwind info: a plain one, and one chained (UNW_FLAG_CHAININFO) to split_main
    chunks[0x5100] = bytes([0x01, 0, 0, 0])
    chunks[0x5110] = bytes([0x01 | (4 << 3), 0, 0, 0]) + struct.pack(
        "<III", LABELS["split_main"], prog_end("split_main"), 0x5100)
    pdata = [(LABELS["split_main"], prog_end("split_main"), 0x5100),
             (LABELS["split_frag"], prog_end("split_frag"), 0x5110),
             (LABELS["switch_fn"], prog_end("switch_fn"), 0x5100),
             (LABELS["noret_fn"], prog_end("noret_fn"), 0x5100),
             (LABELS["fall_a"], prog_end("fall_a"), 0x5100)]
    relocs = [insn_rva("mem_ops", "movabs r8, 0x180003008") + 2, 0x3018]
    entries = [LABELS[n] for n in ("sum_loop", "calls_out", "helper", "cond_tail", "reg_writes", "stack_ops",
                                   "flags_mix", "mem_ops", "sse_moves", "string_ops", "div_fn", "args_fn",
                                   "twocalls_fn", "div_trap_fn", "trap_fn", "cov_fn", "fourargs_fn", "gptr_fn",
                                   "memint_fn", "jmp_rsp_fn", "far_read_fn", "far_write_fn", "imp_data_fn",
                                   "noncanon_fn", "page_walk_fn", "call_loop_fn", "count_loop_fn", "spin_fn",
                                   "probe_fn", "probe_stub_fn", "probe_ok_fn")]
    return Image.synthetic(name, 0x8000, chunks, [(0x1000, 0x2000)], entries, import_slots=[0x4000, 0x4008],
                           reloc_sites=relocs, pdata=pdata, image_base=PREFERRED_BASE,
                           writable_ranges=[(0x3000, 0x4000)])


def insn_image(family=None, name="selftest_i"):
    """The image of one family's single-instruction vectors (tests/vectors/<family>.py), or,
    without a family, of the vectors that test the core (UNSUPPORTED_VECTORS,
    NAME_CLASH_VECTORS and ORACLE_BUG_VECTORS).  One instruction per 16-byte slot from 0x1000; branch target at
    0xf00; data at 0x8000.

    Every image has the same name on purpose: the instruction test seeds its random states
    with the image name and the rva, so a vector gets the same states whichever file it is
    in (and the ones it had before the vectors were split by family)."""
    if family is None:
        chunks = {rva: bytes.fromhex(h) for rva, h, _, _ in UNSUPPORTED_VECTORS + NAME_CLASH_VECTORS}
        chunks.update({rva: bytes.fromhex(h) for rva, h, _ in ORACLE_BUG_VECTORS})
    else:
        chunks = {rva: bytes.fromhex(h) for rva, h, _ in FAMILY_VECTORS[family]}
        chunks.update({rva: bytes.fromhex(h) for rva, h, _, _ in FAMILY_REFUSED[family]})
    chunks[0x8000] = bytes((i * 167 + 13) & 0xFF for i in range(0x200))
    return Image.synthetic(name, 0x10000, chunks, [(0x1000, 0x5000)], [], image_base=PREFERRED_BASE,
                           reloc_sites=UNSUPPORTED_RELOCS if family is None else ())


FAMILY_IMAGES = {}          # family -> its image, built once


def family_image(family):
    if family not in FAMILY_IMAGES:
        FAMILY_IMAGES[family] = insn_image(family)
    return FAMILY_IMAGES[family]


def pick(pred):
    """{family: [(rva, text)]}: the family vectors whose assembly satisfies pred."""
    out = {}
    for fam in sorted(FAMILY_VECTORS):
        hit = [(rva, t) for rva, _, t in FAMILY_VECTORS[fam] if pred(t)]
        if hit:
            out[fam] = hit
    return out


def insn_test(tag, picked, states, **mutant):
    """The instruction test on the picked vectors, family by family, with `mutant` installed
    (see `patched`).  Returns (the summaries added up, the per-instruction records, the
    number of vectors)."""
    total, insns = Counter(), []
    for fam in sorted(picked):
        with patched(**mutant):
            summary, _, recs = difftest.instruction_test(
                family_image(fam), os.path.join(OUT, "lifted_%s_%s" % (tag, fam)),
                os.path.join(OUT, "run_%s_%s" % (tag, fam)), states=states, quiet=True,
                rvas=[rva for rva, _ in picked[fam]])
        total.update({k: v for k, v in summary.items() if isinstance(v, int)})
        insns += [dict(r, family=fam) for r in recs]
    return total, insns, sum(len(v) for v in picked.values())


def jump_image(name="jumps"):
    """JUMP_PROGRAMS at their addresses (0x1000 to 0x2000, and the open_* programs from 0x6000); tables at
    0x3100 and up, and behind their functions for the regbound_* and open_* programs; plain unwind info
    at 0x5100."""
    chunks = {rva: b"".join(bytes.fromhex(h) for h, _ in lines) for rva, lines in JUMP_PROGRAMS.values()}
    end = {n: rva + len(chunks[rva]) for n, (rva, _) in JUMP_PROGRAMS.items()}
    chunks[0x3000] = struct.pack("<QQ", 0x1122334455667788, 0x0123456789abcdef)   # a pointer slot, a key slot
    chunks[0x3100] = struct.pack("<QQ", PREFERRED_BASE + JUMP_LABELS["abs_switch"] + 0x13,
                                 PREFERRED_BASE + JUMP_LABELS["abs_switch"] + 0x19)
    for rva, (rel, targets, more) in JUMP_TABLES.items():
        vals = [t - (rel or 0) for t in targets + more]
        chunks[rva] = struct.pack("<%di" % len(vals), *vals) + (b"" if rel else struct.pack("<I", 0xFFFFFFFF))
    relocs = [0x3100, 0x3108]
    for rva, (targets, relocated) in JUMP_ABS_TABLES.items():
        chunks[rva] = struct.pack("<%dQ" % len(targets), *[PREFERRED_BASE + t for t in targets])
        relocs += [rva + 8 * i for i in relocated]
    for rva, slots in JUMP_PTR_TABLES.items():
        chunks[rva] = struct.pack("<%dQ" % len(slots), *[0 if t is None else PREFERRED_BASE + t for t in slots]) + b"\x01" * 8
        relocs += [rva + 8 * i for i, t in enumerate(slots) if t is not None]
    for rva, h in JUMP_BYTES.items():
        chunks[rva] = bytes.fromhex(h)
    chunks[0x5100] = bytes([0x01, 0, 0, 0])
    unwound = ("abs_switch", "two_checks", "handler_fn", "padded_fn", "cmov_local", "cmov_mixed", "loop_switch",
               "loop_nobound", "loop_call", "loop_clobber", "tail_rt", "tail_frame", "tail_wr", "tail_wr_label",
               "tail_shift", "tail_home", "tail_member")
    pdata = [(JUMP_LABELS[n], end[n], 0x5100) for n in unwound]
    entries = [JUMP_LABELS[n] for n in JUMP_PROGRAMS if n not in unwound]
    return Image.synthetic(name, 0x8000, chunks, [(0x1000, 0x2000), (0x6000, 0x7000)], entries, reloc_sites=relocs,
                           pdata=pdata, image_base=PREFERRED_BASE, writable_ranges=[(0x3000, 0x3100)])


def skipped_for_jump(r):
    return r["status"] == "skipped" and [x["kind"] for x in r["reasons"]] == ["indirect-jump"]


def test_jump_through_register(jimage):
    print("lifter: 'jmp reg' is a tail call only after an unindexed pointer load, a pointer decoded with a key "
          "from the image, or a call that set rax (finding 10)")
    lifted = os.path.join(OUT, "lifted_jumps")
    report = L.lift_module(jimage, lifted, quiet=True)
    by = {r["name"].split("_", 1)[1]: r for r in report["functions"]}
    rec = {n: by["%x" % rva] for n, rva in JUMP_LABELS.items()}
    # Where the line is since unit lifter-tail-tables (it was: "an indexed load is a table and must be
    # read or skipped").  A table whose address the file fixes can be a switch table of the function
    # and must still be read, with a bound, or the function skipped (unb_switch, norel_switch,
    # tail_roslot, wr_switch).  A table behind a pointer that exists only at run time cannot be one:
    # the jump is an indirect tail call through the dispatcher, where the stack is shown to be at its
    # entry level (test_tail_call_through_table has the shapes and the near misses).
    t = (rec["rt_switch"].get("tail_calls_through_table") or [{}])[0]
    check(rec["rt_switch"]["status"] == "lifted" and t.get("form") == "run-time pointer"
          and rec["rt_switch"].get("tail_jumps_through_register") == 1,
          "rt_switch (jmp through 'mov rax, [rcx + rdx*8]': a table behind the caller's pointer, which nobody can read and "
          "which therefore is no switch table of this function) is lifted as an indirect tail call, its stack being at "
          "the entry level; it was skipped until unit lifter-tail-tables (%s, %s)" % (rec["rt_switch"]["status"], t.get("form")))
    check(skipped_for_jump(rec["xor_jmp"]), "xor_jmp (pointer modified after the load) is skipped")
    check(skipped_for_jump(rec["call_rcx"]), "call_rcx (jmp rcx after a call) is skipped")
    check(skipped_for_jump(rec["decode_indexed"]),
          "decode_indexed (an indexed load, then decoded with a key from the image) is skipped")
    check(skipped_for_jump(rec["decode_regkey"]),
          "decode_regkey (decoded with a key found through a register) is skipped")
    tails = ("thunk_call", "slot_jmp", "decode_jmp", "decode_null")
    for n in tails:
        check(rec[n]["status"] == "lifted" and rec[n].get("tail_jumps_through_register") == 1,
              "%s is lifted with the jump as a tail call" % n)
    summary, results = difftest.function_test(jimage, lifted, os.path.join(OUT, "run_jumps"), seeds=8, quiet=True)
    res = {r["name"].split("_", 1)[1]: r for r in results}
    for n in tails:
        r = res["%x" % JUMP_LABELS[n]]
        check(r["status"] == "passing", "%s passes the function test (%d of %d seeds)" % (n, r["agree"], r["seeds"]))
    return rec, {n: res.get("%x" % rva) for n, rva in JUMP_LABELS.items()}, report["summary"]


def test_undecoded_code_is_reported(rec, res, summary):
    print("lifter: code inside a function's ranges that was never decoded is reported (review finding 9)")
    r = rec["handler_fn"]
    check(r["status"] == "lifted-without-handlers" and r.get("undecoded_bytes") == 6
          and r.get("undecoded_runs") == [["0x%x" % (JUMP_LABELS["handler_fn"] + 7), 6]],
          "handler_fn: status 'lifted-without-handlers', 6 undecoded bytes at +7 (got %s, %s)"
          % (r["status"], r.get("undecoded_runs")))
    check(rec["padded_fn"]["status"] == "lifted" and "undecoded_bytes" not in rec["padded_fn"],
          "padded_fn: int3 and nop padding is not counted")
    check(summary["lifted_without_handlers"] == 1 and summary["undecoded_bytes_in_lifted"] == 6,
          "the lift summary counts 1 such function and 6 bytes")
    check(res["handler_fn"] is not None and res["handler_fn"]["status"] == "passing",
          "handler_fn is still tested (and passes on the code that was lifted)")


def test_jump_tables(rec, res, summary):
    print("lifter: jump tables in the clang and Microsoft layouts, bounded by a check on the index register (issue 6)")
    for n, entries, layout, bound in (("clang_switch", 4, "table-relative", "cmp"), ("and_switch", 4, "table-relative", "and"),
                                      ("two_checks", 6, "image-relative", "cmp"),
                                      ("hoisted_switch", 4, "table-relative", "and"),
                                      ("two_lea_switch", 4, "table-relative", "and"),
                                      ("abs_switch", 2, "absolute", "and"), ("goto_fn", 4, "absolute", "and"),
                                      ("fnptr_tail", 2, "absolute, functions", "and")):
        t = (rec[n].get("jump_tables") or [{}])[0]
        check(rec[n]["status"] == "lifted" and (t.get("entries"), t.get("targets"), t.get("layout"), t.get("bound"))
              == (entries, entries, layout, bound),
              "%-14s lifted: %d entries, %s, bound from '%s' (got %s %s)" % (
                  n, entries, layout, bound, rec[n]["status"],
                  {k: t.get(k) for k in ("entries", "layout", "bound")}))
        r = res[n]
        check(r is not None and r["status"] == "passing", "%-14s passes the function test%s" % (
            n, "" if r is None else " (%d of %d seeds; %d of %d instructions executed)" % (
                r["agree"], r["seeds"], r["insns_executed"], r["insns"])))
    r = res["two_checks"]
    check(r is not None and r["insns_executed"] >= 20,
          "two_checks: cases beyond the first two were executed (an unrelated 'cmp edx, 1; ja' used to cut the table to 2)")
    check(skipped_for_jump(rec["ambig_switch"]), "ambig_switch (two possible tables in the base register) is skipped")
    # tables of absolute addresses ('jmp [base + index*8]', issue 13)
    for n in ("abs_switch", "goto_fn"):
        r = res[n]
        check(r is not None and r["insns_executed"] == r["insns"],
              "%-14s every case was executed (%s of %s instructions)" % (
                  n, r and r["insns_executed"], r and r["insns"]))
    src = "".join(open(f).read() for f in sorted(glob.glob(os.path.join(OUT, "lifted_jumps", "jumps_*.c"))))
    body = src[src.index("void jumps_%x(Ctx *restrict ctx) {" % JUMP_LABELS["goto_fn"]):]
    body = body[:body.index("\n}\n")]
    check(body.count("case 0x") == 4 and "TAIL_" not in body and body.count("RET(0)") == 4,
          "goto_fn: the four labels are part of the function (4 cases, 4 returns, no tail call)")
    body = src[src.index("void jumps_%x(Ctx *restrict ctx) {" % JUMP_LABELS["fnptr_tail"]):]
    body = body[:body.index("\n}\n")]
    check("TAIL_INDIRECT(" in body and "switch" not in body,
          "fnptr_tail: a table of two functions is an indirect tail call")
    for n, why in (("wr_switch", "the table is in a writable section"),
                   ("norel_switch", "a slot has no relocation entry"), ("unb_switch", "nothing bounds the index")):
        check(skipped_for_jump(rec[n]), "%-14s is skipped: %s (%s)" % (n, why, rec[n]["status"]))


def test_jumps_judged_over_the_function(rec, res, summary):
    print("lifter: indirect jumps that one straight-line run does not explain, judged over every path of the "
          "function (unit lifter-indirect-jumps)")

    def passing(n):
        r = res[n]
        return r is not None and r["status"] == "passing"

    def tail(n, what):
        check(rec[n]["status"] == "lifted" and rec[n].get("tail_jumps_through_register") == 1 and passing(n),
              "%-14s %s: lifted as a tail call, passes the function test (%s)" % (n, what, rec[n]["status"]))

    def table(n, what):
        t = (rec[n].get("jump_tables") or [{}])[0]
        check(rec[n]["status"] == "lifted" and (t.get("entries"), t.get("layout"), t.get("bound")) == (4, "image-relative", "cmp")
              and passing(n), "%-14s %s: lifted with its 4 entries, passes the function test (%s)" % (n, what, rec[n]["status"]))

    def skipped(n, why):
        check(skipped_for_jump(rec[n]), "%-14s is skipped: %s (%s)" % (n, why, rec[n]["status"]))

    tail("branch_tail", "a pointer loaded whole on the other side of a branch")
    skipped("branch_mixed", "one of the two ways to the jump is an indexed load")
    tail("arg_tail", "an argument, copied and never written")
    tail("rax_stub", "a function that is only 'jmp rax'")
    skipped("rbx_stub", "rbx is not a register a caller hands a target over in")
    tail("cmov_tail", "one of two functions, chosen by a cmov")
    src = "".join(open(f).read() for f in sorted(glob.glob(os.path.join(OUT, "lifted_jumps", "jumps_*.c"))))
    body = ""
    if rec["cmov_local"]["status"] == "lifted":
        body = src[src.index("void jumps_%x(Ctx *restrict ctx) {" % JUMP_LABELS["cmov_local"]):]
        body = body[:body.index("\n}\n")]
    check(rec["cmov_local"]["status"] == "lifted" and body.count("case 0x") == 2 and "TAIL_" not in body
          and passing("cmov_local") and res["cmov_local"]["insns_executed"] == res["cmov_local"]["insns"],
          "cmov_local     one of two labels of the function: a switch with 2 cases, both executed (%s)"
          % rec["cmov_local"]["status"])
    skipped("cmov_mixed", "one address is inside the function and the other is not")
    table("two_reg_switch", "the add takes the image base from a second register")
    skipped("two_reg_bad", "the second register is not set by a lea")
    table("swapped_switch", "'add base, entry ; jmp base'")
    table("loop_switch", "a label between the bounds check and the load")
    r = res["loop_switch"]
    check(r is not None and r["insns_executed"] >= r["insns"] - 2,
          "loop_switch    the cases that jump back to the load were executed (%s of %s instructions)"
          % (r and r["insns_executed"], r and r["insns"]))
    skipped("loop_nobound", "a case comes back to the load with an unchecked index")
    table("loop_call", "a call to a closed callee that leaves the base register alone")
    skipped("loop_clobber", "a call to a callee that writes the base register")


def test_register_writes():
    """Which general registers an instruction writes, as the lifter's discovery believes it
    (Program._writes): the decoder's report, completed by hand where the decoder leaves a write
    out (finding 7 of docs/reviews/night-2026-10-11-code-review.md; unit fix-review-night).
    The registers each instruction writes are the Intel manual's description, from memory;
    nothing here comes from the decoder or from lifted code."""
    print("lifter: the registers an instruction writes: the decoder's report, the writes it leaves out, and the "
          "instructions the rule 'cmp-reg' accepts between its constant and its compare")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    R = {n: i for i, n in enumerate("rax rcx rdx rbx rsp rbp rsi rdi r8 r9 r10 r11 r12 r13 r14 r15".split())}

    def decode(hx):
        code = bytes.fromhex(hx)
        ins = next(md.disasm(code, 0x1000), None)
        return ins if ins is not None and ins.size == len(code) else None

    def reported(ins):
        _, w = ins.regs_access()
        return {D.GPR[r][1] for r in w if r in D.GPR}

    def believed(ins):
        got = {i: D.Program._writes(ins, i) for i in range(16)}
        return None if None in got.values() else {i for i, w in got.items() if w}

    # 1. The instructions of the brief's list: _writes must report every register each one writes.
    listed = [("f00fb10f", "lock cmpxchg [rdi], ecx", "rax"), ("480fb10f", "cmpxchg [rdi], rcx", "rax"), ("0fb00f", "cmpxchg [rdi], cl", "rax"),
              ("0fb1ca", "cmpxchg edx, ecx", "rax rdx"), ("0fc70f", "cmpxchg8b [rdi]", "rax rdx"), ("480fc70f", "cmpxchg16b [rdi]", "rax rdx"),
              ("f7e1", "mul ecx", "rax rdx"), ("f6e1", "mul cl", "rax"), ("f7e9", "imul ecx", "rax rdx"), ("f7f1", "div ecx", "rax rdx"),
              ("48f7f9", "idiv rcx", "rax rdx"), ("0fa2", "cpuid", "rax rbx rcx rdx"), ("0f31", "rdtsc", "rax rdx"), ("0f01d0", "xgetbv", "rax rdx"),
              ("f3a4", "rep movsb", "rcx rsi rdi"), ("f348ab", "rep stosq", "rcx rdi"), ("f3ad", "rep lodsd", "rax rcx rsi"), ("f2ae", "repne scasb", "rcx rdi"),
              ("f3a6", "repe cmpsb", "rcx rsi rdi"), ("48a5", "movsq", "rsi rdi"), ("ac", "lodsb", "rax rsi"),
              ("91", "xchg eax, ecx", "rax rcx"), ("8707", "xchg [rdi], eax", "rax"), ("87ca", "xchg ecx, edx", "rcx rdx"), ("0fc107", "xadd [rdi], eax", "rax"),
              ("9f", "lahf", "rax"), ("6699", "cwd", "rdx"), ("99", "cdq", "rdx"), ("4899", "cqo", "rdx"), ("98", "cwde", "rax"), ("4898", "cdqe", "rax"),
              ("e2fe", "loop", "rcx"), ("e1fe", "loope", "rcx"), ("e0fe", "loopne", "rcx"), ("d7", "xlatb", "rax"), ("c8080000", "enter 8, 0", "rsp rbp"),
              ("c9", "leave", "rsp rbp"), ("50", "push rax", "rsp"), ("59", "pop rcx", "rsp rcx")]
    bad = []
    for hx, text, truth in listed:
        ins = decode(hx)
        got = believed(ins) if ins is not None else None
        if got is None or not {R[r] for r in truth.split()} <= got:
            bad.append("%s (%s): believed to write %s" % (text, truth, got))
    check(not bad, "each of %d instructions with an implicit register write (the brief's list) is believed to write every register it "
          "writes%s" % (len(listed), "".join("; " + b for b in bad)))
    for hx, text in (("e800000000", "call rel32"), ("ffd0", "call rax")):
        got = believed(decode(hx))
        check(got is not None and set(D.VOLATILE) <= got, "%s is believed to write every volatile register (%s)" % (text, got))

    # 2. The rows of UNREPORTED_WRITES are measured: the decoder really leaves the write out (if a
    #    later capstone reports it, the row is no longer needed and this says so), and the table has
    #    no row without a sample here.
    gaps = {"cmpxchg": ("f00fb10f", "480fb10f", "0fb00f"), "xlatb": ("d7",), "enter": ("c8080000",)}
    check(set(gaps) == set(D.UNREPORTED_WRITES), "every row of UNREPORTED_WRITES has a sample (%s)" % sorted(D.UNREPORTED_WRITES))
    for name, samples in gaps.items():
        for hx in samples:
            ins = decode(hx)
            check(ins is not None and ins.insn_name() == name and not (set(D.UNREPORTED_WRITES.get(name, ())) & reported(ins))
                  and set(D.UNREPORTED_WRITES.get(name, ())) <= (believed(ins) or set()),
                  "%-10s (%s): the decoder does not report the write of register(s) %s, and _writes does"
                  % (name, hx, list(D.UNREPORTED_WRITES.get(name, ()))))
    for hx, name in (("0f05", "syscall"), ("0f34", "sysenter"), ("cd2e", "int"), ("0f01c1", "vmcall")):
        ins = decode(hx)
        check(ins is not None and ins.insn_name() == name and name in D.UNKNOWN_WRITES and believed(ins) is None
              and D.Program._writes(ins, 3) is None, "%-10s: what it writes is unknown (None), for every register" % name)

    # 3. SURE_WRITES, the instructions 'cmp-reg' accepts between the constant and the compare: for a
    #    sample of EVERY one of them the decoder reports exactly the registers written, no more and
    #    no fewer, and the set has no member without a sample.
    cc = range(16)
    sure = [("89c8", "rax"), ("8907", ""), ("b805000000", "rax"), ("8a07", "rax"), ("0fb6c1", "rax"), ("0fbec1", "rax"), ("4863c1", "rax"),
            ("488d0411", "rax"), ("01c8", "rax"), ("0107", ""), ("29c8", "rax"), ("21c8", "rax"), ("09c8", "rax"), ("31c8", "rax"), ("4531c0", "r8"),
            ("39c8", ""), ("85c8", ""), ("ffc0", "rax"), ("ffc8", "rax"), ("f7d8", "rax"), ("f7d0", "rax"), ("d1e0", "rax"), ("d3e0", "rax"),
            ("d1e8", "rax"), ("c1ea04", "rdx"), ("d1f8", "rax"), ("90", ""), ("0f1f4000", "")]
    sure += [("0f4%xc1" % c, "rax") for c in cc] + [("0f4%xd0" % c, "rdx") for c in cc] + [("0f9%xc0" % c, "rax") for c in cc] + [("7%x00" % c, "") for c in cc]
    seen, bad = set(), []
    for hx, truth in sure:
        ins = decode(hx)
        if ins is None:
            bad.append("%s does not decode" % hx)
            continue
        seen.add(ins.insn_name())
        want = {R[r] for r in truth.split()}
        if ins.insn_name() not in D.SURE_WRITES or reported(ins) != want or believed(ins) != want:
            bad.append("%s %s (%s): the decoder reports %s, _writes believes %s, it writes %s"
                       % (ins.mnemonic, ins.op_str, hx, sorted(reported(ins)), believed(ins), sorted(want)))
    check(not bad, "for %d samples of the instructions in SURE_WRITES the decoder reports exactly the registers written%s"
          % (len(sure), "".join("; " + b for b in bad)))
    check(seen == set(D.SURE_WRITES), "every instruction of SURE_WRITES has a checked sample, and no other is in it (without a sample: %s; "
          "sampled and not in it: %s)" % (sorted(set(D.SURE_WRITES) - seen), sorted(seen - set(D.SURE_WRITES))))
    for name in ("cmpxchg", "xlatb", "enter", "syscall", "call", "xchg", "mul", "div", "cpuid", "rdtsc", "loop", "lahf", "cdq", "movsb", "stosq", "pop",
                 "push", "xadd", "imul", "rdsspq"):
        check(name not in D.SURE_WRITES, "%-8s is not an instruction 'cmp-reg' accepts between its constant and its compare" % name)


def test_jump_table_bound_in_register(jimage, rec, res, summary):
    print("lifter: a jump table in a leaf whose bound is a constant held in a register, with the table inside "
          "the code (unit lifter-jt-regbound: the shape of materialsystem_dx11+0x148370)")
    lifted = os.path.join(OUT, "lifted_jumps")
    table_of = {n: rva for n, start in JUMP_LABELS.items() if n.startswith("regbound_")
                for rva, (_, targets, _) in JUMP_TABLES.items() if start <= targets[0] < start + 0x80}
    both = (("regbound_switch", 6, "'mov eax, 5 ... cmp edx, eax ; ja'"),
            ("regbound_jae", 3, "'mov eax, 6 ... cmp edx, eax ; jae'"))
    for n, targets, what in both:
        t = (rec[n].get("jump_tables") or [{}])[0]
        got = (t.get("table"), t.get("entries"), t.get("targets"), t.get("layout"), t.get("bound"))
        check(rec[n]["status"] == "lifted" and got == ("0x%x" % table_of[n], 6, targets, "image-relative", "cmp-reg"),
              "%-16s %s: lifted, the table at 0x%x with 6 entries and %d targets, bound 'cmp-reg' (got %s %s)"
              % (n, what, table_of[n], targets, rec[n]["status"], got))
    # Every path through each table: the function test again for these two alone, with more seeds,
    # counting the seeds that execute the first instruction of each case.
    cases = {n: sorted(set(JUMP_TABLES[table_of[n]][1])) for n, _, _ in both}
    _, results = difftest.function_test(jimage, lifted, os.path.join(OUT, "run_regbound"), seeds=40, quiet=True,
                                        only={JUMP_LABELS[n] for n, _, _ in both},
                                        watch=[a for v in cases.values() for a in v])
    by = {int(r["rva"], 16): r for r in results}
    for n, targets, _ in both:
        r = by.get(JUMP_LABELS[n])
        seen = {} if r is None else r.get("watched", {})
        check(r is not None and r["status"] == "passing" and r["agree"] == r["conclusive"]
              and r["insns_executed"] == r["insns"] and len(seen) == targets and all(seen.values()),
              "%-16s passes the function test with every case of the table and the default executed (%s)"
              % (n, "not tested" if r is None else "%s, %d of %d seeds agree, %d of %d instructions, seeds per case: %s" % (
                  r["status"], r["agree"], r["conclusive"], r["insns_executed"], r["insns"],
                  " ".join("%d" % seen[k] for k in sorted(seen)))))
    for n, why in (("regbound_written", "the register is written between the constant and the compare"),
                   ("regbound_noconst", "the bound register is copied from another: not a constant"),
                   ("regbound_outside", "one of the six entries is another function's entry"),
                   ("regbound_far", "one of the six entries lands in the code of the next function (code that would lift)"),
                   ("regbound_label", "a branch arrives between the constant and the compare"),
                   ("regbound_narrow", "the compare is 32 bits wide and the index is the 64-bit register"),
                   ("regbound_partial", "the index is copied into 16 bits of its register, behind a full copy"),
                   ("regbound_inside", "one entry points into the middle of an instruction"),
                   ("regbound_fallin", "the last arm runs into the table's first byte"),
                   # finding 7 of docs/reviews/night-2026-10-11-code-review.md: each of these four was LIFTED, with
                   # the bound 'cmp-reg', by the rule as it was (measured with the lifter of commit 369a6e91).
                   # (A 'syscall' in between is no fifth: a function with one is skipped for the syscall itself.)
                   ("regbound_cmpxchg", "a 'lock cmpxchg' between the constant and the compare writes eax; the decoder does not report it"),
                   ("regbound_xlat", "an 'xlatb' in between writes al; the decoder does not report it"),
                   ("regbound_enter", "the bound is in ebp and an 'enter' in between writes rbp; the decoder does not report it"),
                   ("regbound_unsure", "an 'xchg' in between: not an instruction whose writes the rule has a checked description of")):
        check(skipped_for_jump(rec[n]), "%-16s is skipped: %s (%s%s)" % (
            n, why, rec[n]["status"], "".join("; " + x["what"] for x in rec[n].get("reasons", ()))))


def table_of(n):
    """The rva of the table of the open_* jump program n: right behind its code."""
    return JUMP_LABELS[n] + len(prog_bytes_of(JUMP_PROGRAMS[n]))


def prog_bytes_of(program):
    return b"".join(bytes.fromhex(h) for h, _ in program[1])


def test_jump_table_without_a_bound(jimage, rec, res, summary):
    print("lifter: a jump table in a leaf that nothing bounds is read exactly or the run stops loudly "
          "(unit lifter-leaf-tables: the shape of mileswin64+0xa720)")
    lifted = os.path.join(OUT, "lifted_jumps")
    src = "".join(open(f).read() for f in sorted(glob.glob(os.path.join(lifted, "jumps_*.c"))))

    def jmp_of(n):
        return next(a for a, t in zip(_addresses(JUMP_PROGRAMS[n]), (t for _, t in JUMP_PROGRAMS[n][1])) if t.startswith("jmp r"))

    read = (("open_switch", 5, 4, "five entries, then int3 padding"),
            ("open_short", 2, 2, "the third entry lands in the next function: the reading stops there"),
            ("open_beyond", 2, 2, "the third entry lands behind the table: the reading stops there"),
            ("open_far", 2, 2, "the table lies apart and its third entry lands behind the next function's entry: it stops there"),
            ("open_whole", 4, 3, "the index is a register used whole that nothing writes (mileswin64+0x14ab0)"),
            ("open_overwrite", 3, 3, "the run starts at a label, the load overwrites its index (engine+0xfdeb0)"))
    for n, entries, targets, what in read:
        t = (rec[n].get("jump_tables") or [{}])[0]
        table = 0x35e0 if n == "open_far" else table_of(n)
        got = (t.get("table"), t.get("entries"), t.get("targets"), t.get("layout"), t.get("bound"))
        check(rec[n]["status"] == "lifted" and got == ("0x%x" % table, entries, targets, "image-relative", "unbounded"),
              "%-14s %s: lifted, the table at 0x%x with %d entries and %d targets, bound 'unbounded' (got %s %s)"
              % (n, what, table, entries, targets, rec[n]["status"], got))
        body = src[src.index("void jumps_%x(Ctx *restrict ctx) {" % JUMP_LABELS[n]):] if rec[n]["status"] == "lifted" else ""
        body = body[:body.index("\n}\n")] if body else ""
        check(body.count("case 0x") == targets and "UNTRANSLATED(0x%x, w_, \"\")" % jmp_of(n) in body and "TRAP(" not in body
              and "uint64_t ix_%x = 0;" % jmp_of(n) in body
              and "ix_%x = ctx->r[%d];" % (jmp_of(n), 1 if n == "open_whole" else 0) in body,
              "%-14s the switch has %d cases and its default is the reporting stop, with the index kept at the load"
              % (n, targets))
    for n, why in (("open_writable", "the table is in a writable section"),
                   ("open_inside", "the second entry points into the middle of an instruction"),
                   ("open_none", "the first entry is another function's entry: no entry qualifies"),
                   ("open_fallin", "the last arm runs into the table's first byte"),
                   ("open_label", "a case jumps back between the lea of the base and the load"),
                   ("open_checked", "a bounds check is there and cannot be read (the bound is the caller's register)")):
        check(skipped_for_jump(rec[n]), "%-14s is skipped: %s (%s%s)" % (
            n, why, rec[n]["status"], "".join("; " + x["what"] for x in rec[n].get("reasons", ()))))
    # The cap on the entries read is OPEN_TABLE_CAP: with a cap of 3, three of the five.
    with patched_attrs(D, OPEN_TABLE_CAP=3):
        capped = D.Program(jimage)
        capped.discover()
        tables = capped.build_cfg(capped.funcs[JUMP_LABELS["open_switch"]]).tables
    check(len(tables) == 1 and tables[0]["entries"] == 3, "open_switch    with the cap at 3 entries, three are read (%s)"
          % [t["entries"] for t in tables])

    # The function test with the index aimed (difftest.parse_aim, `functest.py one --reg`): every entry
    # is taken and agrees with the emulator, and an index past the entries read, or before the table,
    # ends at the jump on both sides, in the same state.  The harness's own seeds put arena pointers
    # and a few small integers in ecx: most of them end at the stop.
    for n, ok, miss in (("open_switch", (0, 1, 2, 3, 4), (5, 6, 7, -1, 0x7fffffff)),
                        ("open_short", (0, 1, 3), (2, 4, -1)),
                        ("open_whole", (0, 1, 2, 3), (4, 5, 6)),
                        ("open_overwrite", (3, 4, 5), (6, 7, 2))):
        # open_whole reads all 64 bits of rcx; open_overwrite takes its early way out unless ecx > edx
        aim = difftest.parse_aim(["%s=%s" % ("rcx" if n == "open_whole" else "ecx", ",".join(str(v) for v in ok + miss))]
                                 + (["edx=0"] if n == "open_overwrite" else []))
        cases = sorted(set(JUMP_TABLES[table_of(n)][1]))
        _, results = difftest.function_test(jimage, lifted, os.path.join(OUT, "run_open"), seeds=4, quiet=True,
                                            only={JUMP_LABELS[n]}, watch=cases, aim=aim)
        r = results[0] if results else None
        aimed = {} if r is None else r.get("aimed", {})
        label = "rcx=0x%x" if n == "open_whole" else "ecx=0x%x edx=0x0" if n == "open_overwrite" else "ecx=0x%x"
        took = [aimed.get(label % (v & 0xFFFFFFFF), {}) for v in ok]
        stop = [aimed.get(label % (v & 0xFFFFFFFF), {}) for v in miss]
        check(r is not None and r["status"] == "passing" and not r.get("failures") and r.get("table_stops") == 1
              and all(a.get("cases", 0) > 0 and a.get("pass") == a["cases"] for a in took)
              and all(a.get("cases", 0) > 0 and a.get("stopped") == a["cases"] for a in stop)
              and r.get("stopped_at") == ["0x%x" % jmp_of(n)],
              "%-14s index aimed at %s: every case of each passes; at %s: every case stops at the jump, in agreement (%s)"
              % (n, list(ok), list(miss), "not tested" if r is None else "%s; %s" % (
                  r["status"], " ".join("%s:%s/%s/%s" % (k.split("=")[1].split(" ")[0], a["pass"], a["stopped"], a["cases"])
                                        for k, a in aimed.items()))))
        if n == "open_switch":
            seen = {} if r is None else r.get("watched", {})
            check(r is not None and r["insns_executed"] == r["insns"] and len(seen) == 4 and all(seen.values()),
                  "open_switch    every instruction and each of the four case bodies was executed (%s)"
                  % (r and (r["insns_executed"], r["insns"], seen),))
    r = res["open_switch"]
    check(r is not None and r["status"] != "failing" and r.get("stopped", 0) > 0,
          "open_switch    on the harness's own seeds no case fails and the ones with no entry end at the stop (%s)"
          % (r and (r["status"], r["agree"], r.get("stopped"), r["seeds"]),))
    # the settings parser
    aim = difftest.parse_aim(["rcx=7", "edx=0,-1,0x10"])
    check([a[0] for a in aim] == ["rcx=0x7 edx=0x0", "rcx=0x7 edx=0xffffffff", "rcx=0x7 edx=0x10"]
          and aim[1][1] == [(1, 64, 7), (2, 32, 0xFFFFFFFF)]
          and difftest.aim_regs([0x1111111122222222] * 16, aim[1][1])[1:3] == [7, 0x11111111FFFFFFFF],
          "--reg rcx=7 --reg edx=0,-1,0x10 is three settings; a 32-bit name keeps the seed's upper half (%s)" % (aim,))
    aim = difftest.parse_aim(["[rcx]:32=3,4", "[rdx+0x44]:32=0x1000", "rdx=%d" % (difftest.ARENA_BASE + 0x100)])
    regs = difftest.aim_regs([difftest.ARENA_BASE + 0x40, 5] * 8, aim[1][1])
    check([a[0] for a in aim] == ["[rcx]:32=0x3 [rdx+0x44]:32=0x1000 rdx=0x%x" % (difftest.ARENA_BASE + 0x100),
                                  "[rcx]:32=0x4 [rdx+0x44]:32=0x1000 rdx=0x%x" % (difftest.ARENA_BASE + 0x100)]
          and difftest.aim_pokes(regs, aim[1][1]) == [(difftest.ARENA_BASE + 0x144, (0x1000).to_bytes(4, "little"))],
          "--reg '[rcx]:32=3,4' '[rdx+0x44]:32=0x1000' rdx=<arena>: two settings; the memory is written where the register "
          "(aimed first) points into the arena, and left alone where it is no pointer (rcx = 5) (%s)"
          % (difftest.aim_pokes(regs, aim[1][1]),))
    bad = []
    for spec in (["rsp=1"], ["xmm0=1"], ["rcx"], ["rcx=1", "ecx=2"], ["ecx=0x100000000"], ["rcx=seven"],
                 ["[ecx]:32=1"], ["[rcx]:12=1"], ["[rcx+8]:8=256"], ["[rcx]:32=1", "[rcx+0]:32=2"]):
        try:
            difftest.parse_aim(spec)
            bad.append(spec)
        except ValueError:
            pass
    check(not bad, "the stack pointer, an unknown register, a missing value, a register named twice, a value that does "
          "not fit and a word are refused (%s)" % bad)


def _addresses(program):
    a = program[0]
    for h, _ in program[1]:
        yield a
        a += len(h) // 2


def test_linked_table_stop(jimage):
    print("really-linked build: a jump through an unbounded table runs its cases and stops with a report for an "
          "index that was not read")
    lifted = os.path.join(OUT, "lifted_jumps")
    image_file = os.path.join(OUT, "jumps.image.bin")
    with open(image_file, "wb") as f:
        f.write(bytes(jimage.data))
    entry = JUMP_LABELS["open_switch"]
    jmp, table = entry + 21, table_of("open_switch")
    exe = link_lifted("jumps", lifted, entry, 'IMAGE_FILE="%s"' % image_file, tag="_open")

    def run(arg):
        if exe is None:
            return None
        r = subprocess.run([exe, arg], capture_output=True, text=True)
        return r.returncode, r.stdout.split(), r.stderr
    for arg, ecx in (("0", 0x30), ("1", 0x31), ("2", 0x42), ("3", 0x31), ("4", 0x33)):
        r = run(arg)
        check(r == (0, ["%d" % ecx, "8"], ""), "open_switch entered with index %s runs that entry's case and returns (%s)" % (arg, r))
    text = ("untranslated instruction at guest address 0x%x: jmp rdx computed an address that is none of the 5 entries "
            "the lifter read from the jump table at +0x%x of the function at +0x%x (the table has no bounds check); "
            "index 0x%%x (needs: no feature bit known)\n" % (jmp, table, entry))
    for arg, index in (("5", 5), ("6", 6), ("9", 9), ("0xffffffff", 0xFFFFFFFFFFFFFFFF), ("0xfffffffe", 0xFFFFFFFFFFFFFFFE)):
        r = run(arg)
        check(r == (8, [], text % index), "entered with index %s it stops: status 8 and one line that names the jump, "
              "the function, the table and the index (%s)" % (arg, r))
    # The load overwrites its own index register: the report still has the index the load saw (ecx - 3).
    exe = link_lifted("jumps", lifted, JUMP_LABELS["open_overwrite"], 'IMAGE_FILE="%s"' % image_file, tag="_over")
    for arg, want in (("4", (0, ["49", "8"], "")), ("0", (0, ["0", "8"], ""))):
        r = None if exe is None else subprocess.run([exe, arg], capture_output=True, text=True)
        check(r is not None and (r.returncode, r.stdout.split(), r.stderr) == want,
              "open_overwrite entered with ecx = %s returns (%s)" % (arg, r and (r.returncode, r.stdout, r.stderr)))
    for arg, index in (("6", 3), ("7", 4)):
        r = None if exe is None else subprocess.run([exe, arg], capture_output=True, text=True)
        check(r is not None and r.returncode == 8 and "none of the 3 entries" in r.stderr
              and r.stderr.rstrip().endswith("index 0x%x (needs: no feature bit known)" % index),
              "open_overwrite entered with ecx = %s stops and reports index 0x%x, which the load has overwritten by then (%s)"
              % (arg, index, r and (r.returncode, r.stderr[-60:])))
    exe = link_lifted("jumps", lifted, JUMP_LABELS["open_short"], 'IMAGE_FILE="%s"' % image_file, tag="_short")
    r = None if exe is None else subprocess.run([exe, "3"], capture_output=True, text=True)
    check(r is not None and (r.returncode, r.stdout.split()) == (0, ["49", "8"]),
          "open_short entered with index 3, past the two entries read: the address computed is a case, and the case runs, "
          "as in the original (%s)" % (r and (r.returncode, r.stdout, r.stderr),))
    r = None if exe is None else subprocess.run([exe, "2"], capture_output=True, text=True)
    check(r is not None and r.returncode == 8 and "none of the 2 entries" in r.stderr and r.stderr.rstrip().endswith(
        "index 0x2 (needs: no feature bit known)"),
          "open_short entered with index 2 stops (%s)" % (r and (r.returncode, r.stderr),))


def body_of(src, n):
    """The C text of the lifted function of jump program n, or '' if it is not in `src`."""
    head = "void jumps_%x(Ctx *restrict ctx) {" % JUMP_LABELS[n]
    if head not in src:
        return ""
    body = src[src.index(head):]
    return body[:body.index("\n}\n")]


def test_tail_call_through_table(jimage, rec, res, summary):
    print("lifter: a jump through a table of function pointers is an indirect tail call where the table cannot be a "
          "switch table of the function and the stack is shown to be at its entry level (unit lifter-tail-tables)")
    lifted = os.path.join(OUT, "lifted_jumps")
    src = "".join(open(f).read() for f in sorted(glob.glob(os.path.join(lifted, "jumps_*.c"))))
    accepted = (("rt_switch", "run-time pointer", "the table is behind the caller's pointer; no stack use"),
                ("tail_rt", "run-time pointer", "the table hangs off the object, a frame and its epilogue (client+0x25bb90)"),
                ("tail_frame", "run-time pointer", "two saved registers, a call on the way, the index in a saved register"),
                ("tail_ro", "read-only table of functions", "a leaf, a sparse read-only table with no bound (engine+0x2526d0)"),
                ("tail_wr", "writable table", "16-byte records in writable data, 'jmp [table + index*8 + 8]' (engine+0xf130)"),
                ("tail_shift", "writable table", "records in writable data, the index shifted, a load without a scale (engine+0x5c0b0)"),
                ("tail_home", "writable table", "registers saved in the home area, one of them late and on one path (vphysics+0x11c5b0)"),
                ("tail_member", "run-time pointer, unscaled", "a member of the caller's object at an offset read from memory (client+0x306540)"))
    for n, form, what in accepted:
        t = (rec[n].get("tail_calls_through_table") or [{}])[0]
        body = body_of(src, n)
        check(rec[n]["status"] == "lifted" and t.get("form") == form and body.count("TAIL_INDIRECT(") == 1
              and "switch" not in body,
              "%-15s %s: lifted, the jump an indirect tail call, form '%s' (got %s, %s)"
              % (n, what, form, rec[n]["status"], t.get("form")))
    stack = (("tail_deep", "the epilogue leaves the stack 8 below its entry level"),
             ("tail_unrestored", "the stack is at its entry level but rbx is not restored"),
             ("tail_swapped", "two saved registers are popped from each other's slots"),
             ("tail_twolevels", "one path to the jump pushes and the other does not"),
             ("tail_retslot", "the return address is overwritten on the way"),
             ("tail_home_other", "rsi is loaded back from the home slot that holds rbx"),
             ("tail_home_lost", "rsi's home slot is overwritten between the save and the load back"),
             ("tail_home_late", "rsi is saved after it was changed, so the slot does not hold the caller's value"))
    other = (("tail_modified", "the loaded pointer is changed before the jump"),
             ("tail_roslot", "the table's address is in a read-only slot of the image: the table can be read, so it must be"),
             ("tail_rbxbase", "the base is what the function was entered with in rbx, which is no argument"),
             ("tail_computed", "the base is a loaded pointer with a constant added"),
             ("tail_ro_label", "a read-only table without a bound in which one slot is a label of this function"),
             ("tail_ro_none", "a read-only table without a bound that holds no function, only empty slots"),
             ("tail_wr_label", "a writable table that holds, in the file, a label of this function"),
             ("tail_shift_ro", "a load without a scale from a read-only table"),
             ("tail_two_leas", "a load without a scale whose two registers are both addresses the file fixes"),
             ("tail_member_sum", "a load without a scale whose offset is computed after it was read"),
             ("wr_switch", "a writable table whose slots are, in the file, this function's labels: a switch that cannot be read"),
             ("unb_switch", "a read-only table without a bound whose slots are this function's labels: a switch, "
                            "which must still be read, and cannot be without a bound"),
             ("norel_switch", "a read-only table with a bound in which a slot has no relocation"))
    for n, why in stack + other:
        check(skipped_for_jump(rec[n]), "%-15s is skipped: %s (%s)" % (n, why, rec[n]["status"]))
    t = (rec["tail_ro"].get("tail_calls_through_table") or [{}])[0]
    check((t.get("table"), t.get("slots"), t.get("functions")) == ("0x3600", 5, 2),
          "tail_ro         the table is read as evidence only: 5 slots, 2 of them functions, then other data (%s)"
          % {k: t.get(k) for k in ("table", "slots", "functions")})
    summary_n = summary.get("tail_calls_through_table")
    check(summary_n == sum(len(r.get("tail_calls_through_table", ())) for r in rec.values()) and summary_n >= len(accepted),
          "the lift summary counts the jumps taken this way (%s)" % summary_n)

    # Each half of the rule is what refuses its near misses: with the stack proof switched off the
    # five stack cases are tail calls and the others are still refused; with the test of the base
    # switched off, the three base cases are.
    names = [n for n, _ in stack + other] + ["rt_switch"]

    def verdicts(**mutant):
        with patched_attrs(D.Program, **mutant):
            prog = D.Program(jimage)
            prog.discover()
            return {n: bool(prog.build_cfg(prog.funcs[JUMP_LABELS[n]]).table_tails) for n in names}
    got = verdicts(_stack_at_entry=lambda *a: True)
    check(all(got[n] for n, _ in stack) and not any(got[n] for n, _ in other),
          "without the stack proof the stack cases would be tail calls, and nothing else changes (%s)"
          % sorted(n for n in got if got[n]))
    got = verdicts(_run_time_pointer=lambda *a, **k: True)
    check(all(got[n] for n in ("tail_roslot", "tail_rbxbase", "tail_computed")) and not got["tail_modified"]
          and not any(got[n] for n, _ in stack),
          "without the test of the table's base the three base cases would be tail calls (and every table at an address "
          "the file fixes with them); the stack cases and tail_modified stay refused (%s)"
          % sorted(n for n in got if got[n]))
    got = verdicts(_own_label=lambda *a: False)
    check(got["tail_ro_label"] and got["unb_switch"] and got["norel_switch"] and got["tail_wr_label"] and got["wr_switch"]
          and not got["tail_ro_none"],
          "without the test for the function's own labels, tail_ro_label, tail_wr_label and the switches unb_switch, "
          "norel_switch and wr_switch would be tail calls; tail_ro_none stays refused (%s)"
          % sorted(n for n in got if got[n]))
    got = verdicts(_run_time_value=lambda *a, **k: True)
    check(got["tail_member_sum"] and not got["tail_shift_ro"] and not got["tail_two_leas"] and not any(got[n] for n, _ in stack),
          "without the test of the second register of an unscaled load, tail_member_sum would be a tail call; the two loads "
          "from fixed addresses and the stack cases stay refused (%s)" % sorted(n for n in got if got[n]))
    got = verdicts(_tail_through_table=lambda *a: None)
    check(not any(got.values()), "without the rule rt_switch is skipped again (%s)" % sorted(n for n in got if got[n]))

    # The function test with the index aimed and the object's table pointer aimed into the arena
    # (the seeds' rdx is a pointer in some cases only; elsewhere the load faults on both sides).  It
    # shows that the lifted code arrives at the tail call with the address the emulator jumps to,
    # the stack pointer at its entry level and the same registers and memory.  It cannot show what
    # the dispatcher does with the address: the harness records a tail call and ends the case.
    table = difftest.ARENA_BASE + 0x1000
    for n, aim in (("tail_rt", ["ecx=0,1,2,3,0xffff", "[rdx+8]:64=0x%x" % table]),
                   ("tail_frame", ["edx=0,1,2,3", "[rcx+8]:64=0x%x" % table]),
                   ("rt_switch", ["edx=0,1", "rcx=0x%x" % table]),
                   # slots 1 and 3 hold functions, 0, 2 and 4 are empty (the way out), 5 is the data behind
                   # the table and -1 the data before it: the address loaded there is jumped to all the same
                   ("tail_ro", ["edx=0,1,2,3,4,5,-1"]),
                   # records 0 and 1 hold functions; 2 is behind the table
                   ("tail_wr", ["[rcx+0x1c]:32=0,1,2"]),
                   # records 0 and 1 hold functions (slots 0 and 2), 2 is behind the table, 3 is other data, 4 is
                   # past the function's own bound
                   ("tail_shift", ["ecx=0,1,2,3,4"]),
                   # slots 1 and 3 hold functions, 0 and 2 are empty; 4 is past the function's own bound
                   ("tail_home", ["[rdx+0x10]:32=0,1,2,3,4"]),
                   # the offset of the member, read from where rdx points
                   ("tail_member", ["[rdx]:32=0,8,0x40,0x1000", "rcx=0x%x" % table])):
        _, results = difftest.function_test(jimage, lifted, os.path.join(OUT, "run_tail"), seeds=6, quiet=True,
                                            only={JUMP_LABELS[n]}, aim=difftest.parse_aim(aim))
        r = results[0] if results else None
        aimed = {} if r is None else r.get("aimed", {})
        check(r is not None and r["status"] == "passing" and not r.get("failures") and r["insns_executed"] == r["insns"]
              and all(a.get("pass", 0) > 0 and not a.get("fail") for a in aimed.values()),
              "%-15s aimed at %s: passes, every instruction executed, each setting has passing cases and none failing (%s)"
              % (n, " ".join(aim), "not tested" if r is None else "%s; %d of %d instructions; %s" % (
                  r["status"], r["insns_executed"], r["insns"],
                  " ".join("%s:%s/%s" % (k.split(" ")[0].split("=")[1], a.get("pass"), a.get("cases")) for k, a in aimed.items()))))


def test_linked_table_tail(jimage):
    print("really-linked build: a tail call through a table of function pointers runs the function the slot names and "
          "returns to the caller; a slot that names no lifted function stops at the dispatcher")
    lifted = os.path.join(OUT, "lifted_jumps")
    a, b = JUMP_LABELS["tail_a"], JUMP_LABELS["tail_b"]
    exe = link_lifted("jumps", lifted, JUMP_LABELS["tail_rt"], "WITH_TABLE", "SLOTS=0x%x,0x%x,0x%x,0" % (a, b, a + 1),
                      "SLOTS_BEHIND=8", tag="_tailrt")

    def run(arg):
        if exe is None:
            return None
        r = subprocess.run([exe, arg], capture_output=True, text=True)
        return r.returncode, r.stdout.split(), r.stderr
    for arg, ecx, what in (("0", 0x51, "slot 0: tail_a"), ("1", 0x52, "slot 1: tail_b")):
        r = run(arg)
        check(r == (0, ["%d" % ecx, "8"], ""), "tail_rt entered with index %s (%s) runs it and comes back to the caller "
              "with the stack balanced (%s)" % (arg, what, r))
    r = run("0xffff")
    check(r == (0, ["65535", "8"], ""), "tail_rt entered with the index that means 'none' returns without a tail call (%s)" % (r,))
    for arg, target, what in (("2", a + 1, "an address inside tail_a"), ("3", 0, "an empty slot")):
        r = run(arg)
        check(r == (5, [], "dispatch miss: guest address 0x%x\n" % target),
              "tail_rt entered with index %s (%s) stops at the dispatcher, which names the address (%s)" % (arg, what, r))


# ---- tests ----------------------------------------------------------------------------------------

def test_vectors_match_their_assembly():
    print("vectors: every hex string disassembles to the assembly written next to it")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    bad = 0
    items = [(rva, h, t) for rva, h, t in ORACLE_BUG_VECTORS]
    items += [(rva, h, t) for rva, h, t, _ in UNSUPPORTED_VECTORS + NAME_CLASH_VECTORS]
    for fam in sorted(FAMILY_VECTORS):
        items += [(rva, h, t) for rva, h, t in FAMILY_VECTORS[fam]]
        items += [(rva, h, t) for rva, h, t, _ in FAMILY_REFUSED[fam]]
    items += [(rva, h, t) for rva, h, t, _ in STOP_VECTORS] + [(rva, h, t) for rva, h, t in STOP_REFUSED]
    for n, (rva, lines) in (list(PROGRAMS.items()) + list(TAIL_PROGRAMS.items()) + list(JUMP_PROGRAMS.items())
                            + list(LEA_PROGRAMS.items()) + list(RET_PROGRAMS.items()) + list(STOP_PROGRAMS.items())):
        for h, t in lines:
            items.append((rva, h, t))
            rva += len(h) // 2
    for rva, h, t in items:
        ins = next(md.disasm(bytes.fromhex(h), rva, 1), None)
        got = None if ins is None else (ins.mnemonic + " " + ins.op_str).strip()
        if ins is None or ins.size != len(h) // 2 or got != t:
            bad += 1
            print("    0x%x %s: file says %r, Capstone says %r" % (rva, h, t, got))
    check(bad == 0, "%d byte strings checked" % len(items))
    # The name-clash vectors are only that while the decoder's id name is another instruction's.
    stale = []
    for rva, h, t, idname in NAME_CLASH_VECTORS:
        ins = next(md.disasm(bytes.fromhex(h), rva, 1), None)
        if ins is None or ins.insn_name() != idname or idname == t.split(" ")[0]:
            stale.append("%s: id name %r, the list says %r" % (t, ins and ins.insn_name(), idname))
    check(not stale, "%d name-clash vectors carry the id name of another instruction%s" % (
        len(NAME_CLASH_VECTORS), "".join("; " + x for x in stale)))


def test_discovery_and_lift(image, lifted):
    print("lifter: discovery, chained fragments, jump table, relocation to BASE")
    report = L.lift_module(image, lifted, quiet=True)
    by = {int(r["rva"], 16): r for r in report["functions"]}
    s = report["summary"]
    check(s["skipped"] == 0, "every function lifted (%d functions)" % s["functions"])
    check(s["fragments_merged"] == 1, "one chained unwind fragment merged")
    sm = by.get(LABELS["split_main"])
    check(sm is not None and len(sm["ranges"]) == 2, "split_main has two ranges")
    check(LABELS["split_frag"] not in by, "the fragment is not a function of its own")
    sw = by.get(LABELS["switch_fn"], {})
    check(len(sw.get("jump_tables", [])) == 1 and sw["jump_tables"][0]["targets"] == 4,
          "switch_fn: jump table recovered with 4 targets")
    check(by.get(LABELS["fall_b"], {}).get("origin") == "tail", "fall_b found as a fallthrough tail target")
    check(by.get(LABELS["helper"], {}).get("status") == "lifted", "helper lifted")
    src = ""
    for f in sorted(os.listdir(lifted)):
        if f.endswith(".c") or f.endswith(".h"):
            src += open(os.path.join(lifted, f)).read()
    body = src[src.index("void %s_%x(Ctx *restrict ctx) {" % (image.name, LABELS["split_main"])):]
    body = body[:body.index("\n}\n")]
    check("goto L_%x;" % LABELS["split_frag"] in body and "TAIL_" not in body,
          "the jump into the fragment is a goto, not a tail call")
    code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)        # the disassembly comments quote the original
    check("180003008" not in code and "180000000" not in code, "no preferred image base in the generated C")
    check("(BASE + UINT64_C(0x3008))" in src, "relocated movabs became BASE + rva")
    lint = s["relocation_lint"]
    check(lint["unrelocated_constants_in_c"] == 0 and lint["values_without_relocation"] == 0 and not lint["unknown"],
          "relocation lint: nothing in the C or the image looks like an unrelocated address")
    check(lint["fields_by_kind"] == {"data": 1, "code-imm64": 1},
          "relocation census: the image's two relocated fields are a data slot and a movabs immediate (%s)"
          % lint["fields_by_kind"])
    # The same image with two relocation entries missing: the movabs immediate and the pointer ptr0.
    broken = program_image("selftest_reloc")
    broken.reloc_sites = []
    lint = L.lift_module(broken, os.path.join(OUT, "lifted_reloc"), quiet=True)["summary"]["relocation_lint"]
    check(lint["unrelocated_constants_in_c"] == 1 and lint["constants_in_c"][0][1] == "0x180003008",
          "relocation lint: a movabs of an image address with no relocation entry is reported (%s)" % lint["constants_in_c"])
    kinds = {k: e["rvas"] for k, e in lint["values_by_kind"].items()}
    check(kinds == {"field64-without-relocation": ["0x%x" % (insn_rva("mem_ops", "movabs r8, 0x180003008") + 2)],
                    "aligned-pointer": ["0x3018"]} and set(lint["unknown"]) == set(kinds) | {"constant-in-c"},
          "relocation lint: both unrelocated pointers in the image are reported, each as an unknown kind (%s)" % kinds)
    return report


def reloc_kinds_image(name, relocs, entries=()):
    """One function and a page of data holding one example of every way an 8-byte value can
    look like an address (lifter/relocs.py).  Code at 0x1000, data at 0x3000, 'PE headers'
    up to 0x400."""
    code = bytes.fromhex("48b80830008001000000"      # 1000: movabs rax, 0x180003008          field at 0x1002
                         "48a11030008001000000"      # 100a: movabs rax, qword ptr [0x180003010]       0x100c
                         "c7802030008001000000"      # 1014: mov dword ptr [rax - 0x7fffcfe0], 1
                         "c3")                       #       its displacement and immediate spell 0x180003020 at 0x1016
    data = bytearray(0x60)
    struct.pack_into("<Q", data, 0x18, PREFERRED_BASE + 0x3008)     # 3018: a pointer to data
    struct.pack_into("<Q", data, 0x23, PREFERRED_BASE + 0x3008)     # 3023: the same bits at an odd address
    struct.pack_into("<Q", data, 0x30, PREFERRED_BASE + 0x29)       # 3030: 0x80000029 followed by a 32-bit 1
    struct.pack_into("<Q", data, 0x38, PREFERRED_BASE + 0x1000)     # 3038: a pointer to the function
    struct.pack_into("<Q", data, 0x43, PREFERRED_BASE + 0x3010)     # 3043: an odd address again
    im = Image.synthetic(name, 0x8000, {0x1000: code, 0x3000: bytes(data)}, [(0x1000, 0x2000)], list(entries),
                         reloc_sites=relocs, image_base=PREFERRED_BASE, writable_ranges=[(0x3000, 0x4000)])
    im.header_size = 0x400
    return im


def test_relocation_kinds():
    print("lifter: every relocated field and every value that looks like one gets a kind; an unknown kind fails the lift")
    from lifter.image import walk_relocations
    all_relocs = [0x1002, 0x100c, 0x3018, 0x3038]

    def lint_of(name, relocs, entries=(), problems=()):
        im = reloc_kinds_image(name, relocs, entries)
        im.reloc_problems = list(problems)
        report = L.lift_module(im, None, quiet=True)
        return report, report["summary"]["relocation_lint"], L.relocation_status(report)

    out = os.path.join(OUT, "lifted_rk")
    report = L.lift_module(reloc_kinds_image("selftest_rk", all_relocs), out, quiet=True)
    lint = report["summary"]["relocation_lint"]
    check(lint["fields_by_kind"] == {"data": 2, "code-imm64": 1, "code-moffs64": 1}
          and lint["fields_point_at"] == {"code": 1, "data": 3},
          "the four relocated fields: two data slots, a 64-bit immediate, a 64-bit absolute address (%s, pointing at %s)"
          % (lint["fields_by_kind"], lint["fields_point_at"]))
    src = "".join(open(os.path.join(out, f)).read() for f in sorted(os.listdir(out)) if f.endswith(".c"))
    code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    check("W64(0, (BASE + UINT64_C(0x3008)))" in code and "= ((BASE + UINT64_C(0x3010))); W64(0, LD64(" in code
          and "0x18000" not in code,
          "both relocated instruction fields became BASE + rva in the C: the immediate as a value, the absolute "
          "address as the address of a load")
    kinds = {k: e["rvas"] for k, e in lint["values_by_kind"].items()}
    check(kinds == {"instruction-bytes": ["0x1016"], "header-offset": ["0x3030"], "unaligned": ["0x3023", "0x3043"]},
          "the four values with no relocation: instruction bytes, a would-be pointer into the headers, two at odd "
          "addresses (%s)" % kinds)
    check(not lint["unknown"] and L.relocation_status(report) == 0 and lint["unrelocated_constants_in_c"] == 0,
          "nothing of an unknown kind: the lift's exit status is 0")
    check(lint["unrelocated_pointers_in_image"] == 3 and lint["values_pointing_into_the_headers"] == 1,
          "the counts the lint gave before it knew kinds are still reported (3 and 1 into the headers)")

    _, lint, status = lint_of("selftest_rk_none", [], entries=[0x1000])
    check(lint["unknown"] == {"field64-without-relocation": 2, "aligned-pointer": 2, "constant-in-c": 2} and status == 3,
          "without the relocation entries the two instruction fields, the two aligned pointers and the two constants "
          "in the C are of unknown kinds and the status is 3 (%s)" % lint["unknown"])
    _, lint, status = lint_of("selftest_rk_nolift", all_relocs)
    check(status == 0 and lint["unrelocated_constants_in_c"] == 0, "a lift that writes nothing gives the same verdict")

    _, lint, status = lint_of("selftest_rk_packed", all_relocs + [0x3023])
    check(lint["fields_by_kind"].get("data-unaligned") == 1 and lint["fields_not_8_aligned_outside_code"] == 1
          and lint["unknown"] == {"unaligned-pointer": 1} and lint["values_by_kind"]["unaligned-pointer"]["rvas"] == ["0x3043"]
          and status == 3,
          "in a module with one relocated field at an odd address, an unrelocated value at an odd address is no longer "
          "excused (%s)" % lint["unknown"])

    report, lint, status = lint_of("selftest_rk_partial", all_relocs + [0x1016])
    reasons = report["functions"][0].get("reasons", [])
    check(lint["fields_by_kind"].get("code-partial") == 1 and status == 0
          and report["functions"][0]["status"] == "skipped" and any(r["kind"] == "reloc" for r in reasons),
          "a relocated field over part of an instruction is a known kind, and the function is skipped for it (%s)"
          % [r["what"] for r in reasons])

    _, lint, status = lint_of("selftest_rk_sites", all_relocs + [0x301c, 0x3000, 0x3ff9, 0x200, 0x7ffc])
    check(lint["unknown"] == {"site-overlap": 1, "site-in-headers": 1, "target-outside-image": 2,
                              "site-crosses-section-end": 1} and status == 3,
          "relocated fields that overlap, lie in the headers, hold a value outside the image or run past the end "
          "of the image are unknown kinds (%s)" % lint["unknown"])

    _, lint, status = lint_of("selftest_rk_dir", all_relocs, problems=["the block for page 0x5000 has size 0x0"])
    check(lint["unknown"] == {"directory": 1} and status == 3, "a relocation directory that does not parse is one too")

    good = struct.pack("<II4H", 0x3000, 16, 0xA018, 0xA038, 0x0000, 0x0000)
    check(walk_relocations(good, 0, len(good)) == ([0x3018, 0x3038], []), "relocation directory: DIR64 entries read, padding skipped")
    for what, tail in (("a block of size 0", struct.pack("<II", 0x4000, 0)),
                       ("a block that runs past the end of the directory", struct.pack("<II2H", 0x4000, 16, 0xA000, 0xA008)),
                       ("a block of odd size", struct.pack("<II2H", 0x4000, 11, 0xA000, 0xA008)),
                       ("four stray bytes", b"\0\0\0\0")):
        sites, problems = walk_relocations(good + tail, 0, len(good + tail))
        check(sites == [0x3018, 0x3038] and len(problems) == 1,
              "relocation directory: %s is a problem, not a silent end (%s)" % (what, problems))
    check(len(walk_relocations(good, 8, len(good))[1]) == 1, "relocation directory: one that leaves the file is a problem")


EXPECT_PASS = ("sum_loop", "calls_out", "helper", "cond_tail", "reg_writes", "stack_ops", "flags_mix", "mem_ops",
               "sse_moves", "string_ops", "split_main", "switch_fn", "fall_a", "fall_b", "div_fn", "args_fn",
               "twocalls_fn", "div_trap_fn", "trap_fn", "noret_fn", "cov_fn", "fourargs_fn", "memint_fn",
               "jmp_rsp_fn")


def test_functions(image, lifted):
    print("function test: loops, calls out, tail calls, sub-registers, stack, memory, SSE moves, strings")
    summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_func"), seeds=8, quiet=True)
    by = {int(r["rva"], 16): r for r in results}
    for n in EXPECT_PASS:
        r = by[LABELS[n]]
        check(r["status"] == "passing" and r["agree"] >= 6,
              "%-11s passing (%d of %d seeds conclusive and agreeing)%s" % (
                  n, r["agree"], r["seeds"], "" if r["status"] == "passing" else "  " + json.dumps(r)[:300]))
    # noret_fn runs off its end into int3 padding, trap_fn ends in int3, div_trap_fn raises #DE
    # for most inputs: all three pass only because both sides trap at the same instruction.
    check(summary["cases_ending_in_a_defined_trap"] > 16,
          "noret_fn, trap_fn and div_trap_fn are compared at their traps (%d cases end in a defined trap)"
          % summary["cases_ending_in_a_defined_trap"])
    check(summary["failing"] == 0, "no failing function")
    return summary, by


def test_coverage_and_seed_families(image, lifted, summary, by):
    print("coverage and seed families (review finding 1, issue 44)")
    r = by[LABELS["cov_fn"]]
    check((r["insns"], r["insns_executed"]) == (5, 3),
          "cov_fn: 3 of its 5 instructions executed (reported %s of %s)" % (r.get("insns_executed"), r.get("insns")))
    check((r["cond_branches"], r["cond_branches_both_ways"], r["cond_branches_one_way"], r["cond_branches_never"])
          == (1, 0, 1, 0), "cov_fn: its one conditional branch only ever went one way")
    r = by[LABELS["sum_loop"]]
    check(r["insns_executed"] == r["insns"] == 8 and r["cond_branches_both_ways"] == r["cond_branches"] == 2,
          "sum_loop: all 8 instructions executed, both branches seen both ways")
    t = summary["coverage_passing"]
    check(t["functions"] == summary["passing"] and t["insns_executed"] < t["insns"]
          and t["insns"] == sum(x["insns"] for x in by.values() if x["status"] == "passing"),
          "module totals: %d of %d instructions of passing functions executed" % (t["insns_executed"], t["insns"]))

    def families_conclusive(r):
        bad = {c["seed"].split(":")[0] for c in r.get("inconclusive_cases", [])}
        return {f[0] for f in difftest.FAMILIES} - bad
    r = by[LABELS["fourargs_fn"]]
    check({"args-ptr", "globals", "ints"} <= families_conclusive(r),
          "fourargs_fn: every seed whose four argument registers are pointers is conclusive")
    r = by[LABELS["memint_fn"]]
    check(r["cond_branches_both_ways"] == 1, "memint_fn: the branch on an integer loaded from memory goes both ways")

    # The same two functions with the base seeds only, and gptr_fn, which needs the image variant.
    only = {LABELS[n] for n in ("memint_fn", "gptr_fn")}
    _, res = difftest.function_test(image, lifted, os.path.join(OUT, "run_base_only"), seeds=8, only=only,
                                    quiet=True, family_seeds=0)
    base = {int(x["rva"], 16): x for x in res}
    r = base[LABELS["memint_fn"]]
    check(r["status"] == "passing" and r["cond_branches_both_ways"] == 0 and r["insns_executed"] == 4,
          "memint_fn on pointer-only memory: passing, but the branch goes one way and 2 instructions never run")
    r = base[LABELS["gptr_fn"]]
    check(r["status"] == "inconclusive" and r["conclusive"] == 0, "gptr_fn on the base seeds: never conclusive")
    load = "0x%x" % (LABELS["gptr_fn"] + 7)
    check(len(r["inconclusive_cases"]) == 8 and all(
        c["rva"] == load and c["address"] == "0x0" and "READ_UNMAPPED" in c["reason"] for c in r["inconclusive_cases"]),
        "gptr_fn: every inconclusive case records the faulting instruction (%s) and address (0x0)" % load)
    _, res = difftest.function_test(image, lifted, os.path.join(OUT, "run_families"), seeds=2, only=only,
                                    quiet=True, family_seeds=3)
    r = {int(x["rva"], 16): x for x in res}[LABELS["gptr_fn"]]
    ok = {c["seed"] for c in r.get("inconclusive_cases", [])}
    filled = [f[0] for f in difftest.FAMILIES if f[3] == "zfill"]         # globals, regs-ptr
    check(r["status"] == "passing" and r["conclusive"] == 3 * len(filled) and "globals" in filled
          and not any(x.split(":")[0] in filled for x in ok),
          "gptr_fn passes on the three seeds of each family whose image has no null global (%s), and on no other"
          % ", ".join(filled))


def test_demand_pages(image, lifted, by):
    print("demand pages: memory outside the image, arena and stack (issue 44, proposal 3)")

    def ptr_seeds(r):
        return {c["seed"] for c in r.get("inconclusive_cases", [])} & {"%s:%d" % (f, k) for f in ("args-ptr", "globals", "ints")
                                                                       for k in range(3)}
    for n, what in (("far_read_fn", "a pointer loaded off the arena is followed"),
                    ("far_write_fn", "a store off the arena is compared")):
        r = by[LABELS[n]]
        check(r["status"] == "passing" and not ptr_seeds(r) and r["insns_executed"] == r["insns"],
              "%s passes on every seed whose rcx is a pointer: %s (%d of %d seeds)" % (n, what, r["agree"], r["seeds"]))
        bad = [c for c in r.get("inconclusive_cases", [])]
        check(bad and all("[32-bit value used as an address]" in c["reason"] or "[near null]" in c["reason"] for c in bad),
              "%s with a small integer in rcx stays inconclusive: nothing can be mapped below 4 GiB" % n)
    r = by[LABELS["imp_data_fn"]]
    check(r["status"] == "passing" and r["agree"] == r["seeds"],
          "imp_data_fn: data behind an import slot is read on every seed (%d of %d)" % (r["agree"], r["seeds"]))
    r = by[LABELS["noncanon_fn"]]
    check(r["status"] == "inconclusive" and r["conclusive"] == 0 and all(
        c["address"] == "0xf290210000300000" and "[above the 47-bit address space]" in c["reason"]
        for c in r["inconclusive_cases"]),
        "noncanon_fn: an address the emulator would cut to 52 bits is no demand page, and is reported whole")
    r = by[LABELS["page_walk_fn"]]
    lim = [c for c in r.get("inconclusive_cases", []) if c["reason"] == difftest.DEMAND_LIMIT]
    check(r["status"] == "inconclusive" and len(lim) >= 9 and all(c["rva"] == "0x1905" for c in lim),
          "page_walk_fn: a case that needs more than %d pages is inconclusive (%d cases)" % (difftest.DEMAND_MAX, len(lim)))

    # Without paging the first three are what they were before: never conclusive.
    names = ("far_read_fn", "far_write_fn", "imp_data_fn")
    _, res = difftest.function_test(image, lifted, os.path.join(OUT, "run_nopaging"), seeds=8, quiet=True,
                                    only={LABELS[n] for n in names}, paging=False)
    check(all(x["status"] == "inconclusive" and x["conclusive"] == 0 for x in res),
          "with paging off the three are never conclusive, as before")

    # The two sides must start from the same pages: a driver whose demand pages differ is noticed.
    summary, res = difftest.function_test(image, lifted, os.path.join(OUT, "run_demand_mutant"), seeds=8, quiet=True,
                                          only={LABELS[n] for n in names}, opt="-O1 -DTF_MUTANT_DEMAND_CONTENT")
    m = {int(x["rva"], 16): x for x in res}
    check(m[LABELS["far_read_fn"]]["status"] == "failing" and m[LABELS["imp_data_fn"]]["status"] == "failing",
          "harness can fail: a driver that fills demand pages differently fails far_read_fn and imp_data_fn")
    check(m[LABELS["far_write_fn"]]["status"] == "failing"
          and "memory at" in first_mismatch(m[LABELS["far_write_fn"]]) + " ".join(m[LABELS["far_write_fn"]]["failures"][0]["mismatches"]),
          "and far_write_fn, whose page then differs from the oracle's after the store")

    # Wrong translations of an access to a demand page.
    def store_one_page_on(orig):
        def f(self, flow):
            out = orig(self, flow)
            if self.ins.op_str == "qword ptr [rcx + 0x301000], rdx":
                out = [x.replace("ST64(m0, ", "ST64(m0 + 0x1000, ") for x in out]
            return out
        return f

    def store_plus_one(orig):
        def f(self, flow):
            out = orig(self, flow)
            if self.ins.op_str == "qword ptr [rcx + 0x301000], rdx":
                out = [x.replace("R64(2));", "R64(2) + 1);") for x in out]
            return out
        return f
    r = mutant_function_result(image, "d_drop", "far_write_fn", mov=drop_insn("mov qword ptr [rcx + 0x301000], rdx"))
    check(r["status"] == "failing" and "changed only on the oracle side" in " ".join(r["failures"][0]["mismatches"]),
          "harness can fail: the store to a demand page dropped: %s" % first_mismatch(r))
    r = mutant_function_result(image, "d_plus1", "far_write_fn", mov=store_plus_one)
    check(r["status"] == "failing" and "memory at" in first_mismatch(r),
          "harness can fail: the store writes a value off by one: %s" % first_mismatch(r)[:80])
    r = mutant_function_result(image, "d_page", "far_write_fn", mov=store_one_page_on)
    said = " ".join(m for f in r.get("failures", []) for m in f["mismatches"])
    check(r["status"] == "failing" and r["agree"] == 0 and ("changed only on the candidate side" in said or "signal 11" in said),
          "harness can fail: the store goes one page further, a page the oracle never mapped: %s" % first_mismatch(r)[:90])
    check("changed only on the candidate side" in said,
          "and where that page shares a 16 KiB host page with the oracle's, the write there is found by comparing it")


def test_limits_end_a_case_at_an_event(image, lifted, summary, by):
    print("limits: a case that fills the call record or passes the instruction limit ends at its next event (issue 44, proposal 4)")
    for n, what in (("call_loop_fn", "the call record is full (%d events)" % difftest.MAX_CALLS),
                    ("count_loop_fn", "the second call comes after instruction %d" % difftest.INSN_LIMIT)):
        r = by[LABELS[n]]
        check(r["status"] == "passing" and r["agree"] == r["seeds"] and r["insns_executed"] == r["insns"],
              "%s: every seed is compared at the event where %s (%d of %d)" % (n, what, r["agree"], r["seeds"]))
    r = by[LABELS["spin_fn"]]
    check(r["status"] == "inconclusive" and r["conclusive"] == 0 and r["inconclusive_reasons"] == ["instruction limit"],
          "spin_fn, a loop without an event, is still inconclusive at the instruction limit")
    check(summary["cases_ending_at_an_event_limit"] == 2 * by[LABELS["call_loop_fn"]]["seeds"],
          "%d cases ended at an event" % summary["cases_ending_at_an_event_limit"])

    names = {LABELS[n] for n in ("call_loop_fn", "count_loop_fn")}
    _, res = difftest.function_test(image, lifted, os.path.join(OUT, "run_nocut"), seeds=4, quiet=True, only=names,
                                    family_seeds=0, cut=False)
    check(sorted(x["inconclusive_reasons"][0] for x in res) == ["call record overflow", "instruction limit"]
          and all(x["conclusive"] == 0 for x in res), "without it the two are never conclusive, as before")

    # The state at the event is compared whole: rsi reaches no callee and no call record.
    for n in ("call_loop_fn", "count_loop_fn"):
        r = mutant_function_result(image, "cut_" + n, n, seeds=4, inc=drop_insn("inc rsi"))
        check(r["status"] == "failing" and r["agree"] == 0 and first_mismatch(r).startswith("rsi: "),
              "harness can fail: %s with 'inc rsi' dropped differs at the event: %s" % (n, first_mismatch(r)[:60]))
    # A candidate that leaves the loop is not at the event when the oracle is.
    r = mutant_function_result(image, "cut_exit", "call_loop_fn", seeds=4, jmp=drop_insn("jmp 0x1940"))
    check(r["status"] == "failing" and "where the case's limit ends it" in first_mismatch(r),
          "harness can fail: the loop's jump dropped: %s" % first_mismatch(r)[:110])


def test_stack_probe(image, lifted, summary, by):
    print("stack probe: a callee whose callers take rax off rsp after it returns with rax as it was")
    r = by[LABELS["probe_fn"]]
    check(summary["stack_probes"] == ["0x%x" % LABELS["probe_stub_fn"]] and summary["functions_calling_a_stack_probe"] == 2,
          "the probe is found by what its two callers do (%s)" % summary["stack_probes"])
    check(r["status"] == "passing" and r["agree"] == r["seeds"] and r["insns_executed"] == r["insns"],
          "probe_fn, with 0x1040 bytes of locals, passes on every seed (%d of %d)" % (r["agree"], r["seeds"]))
    check(summary["cases_run_again_with_the_probe_contract"] == r["seeds"]
          and summary["cases_conclusive_with_the_probe_contract"] == r["seeds"],
          "only its %d cases ran again with the probe's contract: probe_ok_fn's were conclusive without it" % r["seeds"])
    ok = by[LABELS["probe_ok_fn"]]
    check(ok["status"] == "passing" and ok["agree"] == ok["seeds"] and by[LABELS["probe_stub_fn"]]["status"] == "passing",
          "probe_ok_fn and the probe itself pass as ordinary functions")
    r = mutant_function_result(image, "probe_sub", "probe_fn", sub=drop_insn("sub rsp, rax"))
    check(r["status"] == "failing" and r["agree"] == 0,
          "harness can fail: 'sub rsp, rax' dropped after the probe: %s" % first_mismatch(r)[:70])


def test_families(lifted):
    print("families: found by listing lifter/insns/, harness/insns/ and tests/vectors/; a mnemonic has one family")
    fams = translate.FAMILIES
    check(sorted(fams) == sorted(FAMILY_VECTORS) and len(fams) > 0,
          "every family has a file of vectors and every file of vectors a family (%s)" % ", ".join(sorted(fams)))
    headers = sorted(f for f in os.listdir(os.path.join(REPO, "harness", "insns")) if f.endswith(".h"))
    check(headers == sorted(f.header for f in fams.values() if f.header),
          "every C header in harness/insns/ is named by its family (%s)" % ", ".join(headers))
    block = "#include \"runtime.h\"\n" + "".join("#include \"insns/%s\"\n" % h for h in headers)
    check(translate.c_includes() == block, "the include block is runtime.h and then every family header, in name order")
    text = open(os.path.join(lifted, "selftest_0000.c")).read()
    check(text.split("\n", 1)[1].startswith(block + "#include \"selftest.h\"\n"), "and generated C starts with it")

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    def name(h, rva=0x1000):
        ins = next(md.disasm(bytes.fromhex(h), rva, 1), None)
        return None if ins is None else translate.mnemonic_of(ins)
    bad_slot, wrong, covered = [], [], {f: set() for f in FAMILY_VECTORS}
    for fam in sorted(FAMILY_VECTORS):
        seen = set()
        for rva, h, t in FAMILY_VECTORS[fam] + [x[:3] for x in FAMILY_REFUSED[fam]]:
            if rva % 16 or not 0x1000 <= rva < 0x5000 or rva in seen or len(h) > 30:
                bad_slot.append("%s 0x%x" % (fam, rva))
            seen.add(rva)
        for rva, h, t in FAMILY_VECTORS[fam]:
            m = name(h, rva)
            if translate.FAMILY_OF.get(m) != fam:
                wrong.append("%s: '%s' is %s of %s" % (fam, t, m, translate.FAMILY_OF.get(m, "no family")))
            covered[fam].add(m)
    check(not bad_slot, "every vector has a 16-byte slot of its own in 0x1000-0x4ff0%s" % (
        "" if not bad_slot else ": " + ", ".join(bad_slot[:6])))
    check(not wrong, "every vector is in the file of the family that translates it%s" % (
        "" if not wrong else ": " + "; ".join(wrong[:6])))
    missing = ["%s (%s)" % (m, f) for f in sorted(fams) for m in fams[f].mnemonics if m not in covered.get(f, ())]
    check(not missing, "each of the %d registered mnemonics has a hand-written vector%s" % (
        len(translate.HANDLERS), "" if not missing else "; none for: " + ", ".join(missing)))

    # The lifter's name for an instruction (translate.mnemonic_of): the printed mnemonic without
    # prefixes, except that the doubleword string instructions end in 'l'.
    names = (("a5", "movsl"), ("f3a5", "movsl"), ("f20f10c1", "movsd"), ("a7", "cmpsl"), ("f20fc2c109", "cmpsd"),
             ("f3ab", "stosl"), ("f348ab", "stosq"), ("ad", "lodsl"), ("f2af", "scasl"), ("66a5", "movsw"),
             ("0fc2c102", "cmpleps"), ("f20fc2c107", "cmpordsd"), ("f0480fc101", "xadd"), ("f3c3", "ret"))
    got = [(h, name(h)) for h, _ in names]
    check(got == list(names), "mnemonic_of: printed mnemonic without prefixes; movsd and cmpsd are the SSE2 instructions, "
          "the doubleword string instructions end in l%s" % ("" if got == list(names) else " (got %s)" % [
              g for g, w in zip(got, names) if g != w]))

    def error_of(f):
        try:
            f()
        except translate.RegistryError as e:
            return str(e)
        return ""
    before = (dict(translate.HANDLERS), dict(translate.OPCODES), set(translate.VECTOR_OK), dict(translate.FAMILY_OF))
    probe = translate.Family("lifter.insns.selftest_probe")
    try:
        msg = error_of(lambda: probe.register("mov", lambda tr, flow: [], translate.enc(0x88)))
        check("'data_mov'" in msg and "'selftest_probe'" in msg and "'mov'" in msg,
              "a mnemonic registered by a second family is an error that names both (%s)" % msg)
        probe.register("selftest_insn", lambda tr, flow: [], translate.enc(0x0F70, mand=0x66), vector=True)
        msg = error_of(lambda: probe.register("selftest_insn", lambda tr, flow: [], translate.enc(0x0F70)))
        check("twice" in msg, "and so is one registered twice by the same family (%s)" % msg)
        check(error_of(lambda: probe.register("selftest_other", lambda tr, flow: [], ())) != "",
              "a translator registered without encodings is an error")
    finally:
        translate.FAMILIES.pop("selftest_probe", None)
        for m in probe.mnemonics:
            for table in (translate.HANDLERS, translate.OPCODES, translate.FAMILY_OF):
                table.pop(m, None)
            translate.VECTOR_OK.discard(m)
    check(before == (translate.HANDLERS, translate.OPCODES, translate.VECTOR_OK, translate.FAMILY_OF),
          "the refused registrations left the tables as they were")
    msg = error_of(lambda: translate.Family("lifter.insns.selftest_probe", header="other.h"))
    check("harness/insns/selftest_probe.h" in msg, "a family may only name the header harness/insns/<family>.h (%s)" % msg)
    msg = error_of(lambda: translate.Family("lifter.insns.selftest_probe", header="selftest_probe.h"))
    check("does not exist" in msg, "and that header has to exist")
    msg = error_of(lambda: translate.Family("lifter.insns.int_arith", header="int_arith.h"))
    check("twice" in msg and "selftest_probe" not in translate.FAMILIES, "a family defined twice is an error")

    # The stamp of a lift covers the family modules: lifted C is stale after any of them changes.
    probe_dir = os.path.join(OUT, "stamp_probe")
    shutil.copytree(os.path.join(REPO, "lifter"), os.path.join(probe_dir, "lifter"),
                    ignore=shutil.ignore_patterns("__pycache__"))
    image, real = program_image(), L._LIFTER_DIR
    try:
        L._LIFTER_DIR = os.path.join(probe_dir, "lifter")
        a = L.lifter_stamp(image)
        with open(os.path.join(probe_dir, "lifter", "insns", sorted(fams)[0] + ".py"), "a") as f:
            f.write("# edited\n")
        b = L.lifter_stamp(image)
    finally:
        L._LIFTER_DIR = real
    check(a != b, "the stamp of a lift changes when a family module changes")


def test_instructions():
    print("instruction test: %d single instructions in %d families, random registers and flags" % (
        sum(len(v) for v in FAMILY_VECTORS.values()), len(FAMILY_VECTORS)))
    summary, insns = Counter(), []
    for fam in sorted(FAMILY_VECTORS):
        s, recs, n = insn_test("i", {fam: [(rva, t) for rva, _, t in FAMILY_VECTORS[fam]]}, 48)
        summary.update(s)
        insns += recs
        check(s["not_lifted"] == 0 and len(recs) == n, "%-10s every one of its %d vectors was translated" % (fam, n))
        check(s["fail"] == 0, "%-10s no case disagrees (%d pass, %d inconclusive)" % (fam, s["pass"], s["inconclusive"]))
        for r in recs:
            if r["fail"]:
                print("    FAIL %s %s: %s" % (r["rva"], r["text"], r["first_failure"]["mismatches"][:2]))
        never = [r["text"] for r in recs if r["pass"] + r["fail"] == 0]
        check(not never, "%-10s every vector had at least one conclusive case%s" % (
            fam, "" if not never else ": " + "; ".join(never[:8])))
    # Locked operands across a 16-byte boundary: x86 allows them, the host's atomics do not,
    # and the lifted code must take the slow path instead of dying with SIGBUS.
    atomic = [r for r in insns if (r["text"].startswith(("lock ", "xchg ")) and "ptr [" in r["text"])
              and "byte ptr" not in r["text"] and "cmpxchg16b" not in r["text"]]
    steered = [r for r in atomic if "[rip" not in r["text"]]         # the test chooses the address
    untested = [r["text"] for r in steered if r.get("atomic_straddling_pass", 0) == 0]
    check(len(steered) > 30 and not untested,
          "all %d locked instructions with a register-based, wider-than-byte operand pass with it straddling a "
          "16-byte boundary (%d such cases)%s" % (len(steered), summary["atomic_straddling_pass"],
                                                  "" if not untested else "; never: " + "; ".join(untested[:6])))
    fixed = [r for r in atomic if "[rip" in r["text"] and int(r["rva"], 16) >= 0x3d90]
    check(len(fixed) == 8 and all(r.get("atomic_straddling_pass") == 48 for r in fixed),
          "the 8 RIP-relative locked operands that straddle a 16-byte boundary pass in all 48 states")
    based = [r for r in atomic if "[rsp" in r["text"]]
    check(len(based) == 3 and all(r.get("atomic_straddling_pass", 0) > 0 for r in based),
          "including the rsp-based ones (%s)" % ", ".join(r["text"] for r in based))
    cx16 = [r for r in insns if "cmpxchg16b" in r["text"]]
    check(len(cx16) == 3 and all(r.get("atomic_straddling_pass", 0) >= 12 and r["pass"] == 48 for r in cx16),
          "cmpxchg16b with a misaligned operand raises #GP on both sides (3 vectors, half of their states)")
    return insns


class patched:
    """Install deliberately wrong translations (mutants) for the duration of a with-block.
    Each keyword is a mnemonic; its value maps the real handler to the broken one."""

    def __init__(self, **handlers):
        self.h = handlers

    def __enter__(self):
        self.saved = {k: translate.HANDLERS[k] for k in self.h}
        for k, f in self.h.items():
            translate.HANDLERS[k] = f(self.saved[k])

    def __exit__(self, *a):
        translate.HANDLERS.update(self.saved)


def drop_insn(text):
    """Mutant: the instruction whose assembly is `text` is translated to nothing."""
    def make(orig):
        def f(self, flow):
            return [] if (self.ins.mnemonic + " " + self.ins.op_str).strip() == text else orig(self, flow)
        return f
    return make


def mutant_function_result(image, tag, fn, seeds=6, **mutant):
    """Lift `image` with the mutant installed and return the function-test record of `fn`."""
    lifted = os.path.join(OUT, "lifted_" + tag)
    with patched(**mutant):
        L.lift_module(image, lifted, quiet=True)
        _, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_" + tag), seeds=seeds,
                                            only={LABELS[fn]}, quiet=True)
    return results[0]


def first_mismatch(r):
    return r["failures"][0]["mismatches"][0] if r.get("failures") else ""


def test_call_record_covers_what_a_callee_reads(image):
    print("harness can fail: values that are only passed to a callee (review finding 3)")

    def movss_off_by_one(orig):
        return lambda self, flow: [s.replace("= LD32(", "= 1 + LD32(") for s in orig(self, flow)]

    def first_call_pushes_wrong_return(orig):
        def f(self, flow):
            out = orig(self, flow)
            if self.ins.address == LABELS["twocalls_fn"] + 4:
                ret = self.ins.address + self.ins.size
                out = [s.replace(", 0x%x);" % ret, ", 0x%x);" % (ret + 1)) for s in out]
            return out
        return f
    for tag, fn, field, what, mutant in (
            ("m3_r10", "args_fn", "r10", "'mov r10, rcx' before the call dropped", {"mov": drop_insn("mov r10, rcx")}),
            ("m3_rax", "args_fn", "rax", "'mov eax, 5' before the call dropped", {"mov": drop_insn("mov eax, 5")}),
            ("m3_xmm1", "args_fn", "xmm1", "'movss xmm1, [mem]' loads a value off by one", {"movss": movss_off_by_one}),
            ("m3_xmm2", "args_fn", "xmm2", "'movaps xmm2, xmm7' dropped", {"movaps": drop_insn("movaps xmm2, xmm7")}),
            ("m3_ret", "twocalls_fn", "[rsp]", "the first call pushes a wrong return address",
             {"call": first_call_pushes_wrong_return})):
        r = mutant_function_result(image, tag, fn, **mutant)
        m = first_mismatch(r)
        check(r["status"] == "failing" and ("differs in " + field) in m,
              "%s: %s fails on the call record's %s%s" % (what, fn, field, "" if r["status"] == "failing" else
                                                          " (got %s)" % r["status"]))


def test_unsupported_vectors_are_refused(iimage):
    print("lifter refuses: %d encodings that must not be translated" % len(UNSUPPORTED_VECTORS))
    recs = L.lift_instructions(iimage, [rva for rva, _, _, _ in UNSUPPORTED_VECTORS], os.path.join(OUT, "lifted_refused"))
    for rva, h, text, why in UNSUPPORTED_VECTORS:
        r = recs[rva]
        got = "; ".join(x["what"] for x in r.get("reasons", []))
        check(r["status"] == "skipped" and why in got,
              "%-12s %-36s refused (%s)" % (h, text, got if r["status"] == "skipped" else "TRANSLATED"))
    for fam in sorted(FAMILY_REFUSED):
        if not FAMILY_REFUSED[fam]:
            continue
        recs = L.lift_instructions(family_image(fam), [rva for rva, _, _, _ in FAMILY_REFUSED[fam]],
                                   os.path.join(OUT, "lifted_refused_" + fam))
        for rva, h, text, why in FAMILY_REFUSED[fam]:
            r = recs[rva]
            got = "; ".join(x["what"] for x in r.get("reasons", []))
            check(r["status"] == "skipped" and why in got,
                  "%s: %-12s %-36s refused (%s)" % (fam, h, text, got if r["status"] == "skipped" else "TRANSLATED"))


def test_name_clash_guards(iimage):
    print("lifter: an SSE compare whose id name is another instruction's is handled under the mnemonic the decoder "
          "prints (issue 6)")
    out = os.path.join(OUT, "lifted_clash")

    def reasons_of(which):
        recs = L.lift_instructions(iimage, [v[0] for v in which], out)
        return ["; ".join(x["what"] for x in recs[v[0]].get("reasons", [])) or "TRANSLATED" for v in which]
    # The lifter names an instruction by the mnemonic the decoder prints (translate.mnemonic_of).
    # So each of these is the business of the family that registered that mnemonic (the SSE
    # compare families: translated), or of nobody: then it is an instruction without a
    # translator, reported under its real name.  Which of the two depends only on whether a
    # family has the name, so the test holds with any subset of the compare families in the tree.
    names = [t.split(" ")[0] for _, _, t, _ in NAME_CLASH_VECTORS]
    for (rva, h, text, idname), name, got in zip(NAME_CLASH_VECTORS, names, reasons_of(NAME_CLASH_VECTORS)):
        fam = translate.FAMILY_OF.get(name)
        if fam is None:
            check(got == name, "%-20s %-32s no family has %s: refused under that name (%s)" % (h, text, name, got))
        else:
            check(got == "TRANSLATED", "%-20s %-32s translated: family %s registered %s (%s)" % (h, text, fam, name, got))
    # Translated by whom?  Put a marker in place of every translator: the one that runs must be the
    # one registered under the printed mnemonic, after the core has accepted the encoding for that
    # name (0F C2 with its mandatory prefix).  The translator of the id name (the string compares
    # cmpsq and cmpsb; nobody has 'cmppd') must never be reached.
    saved_h = dict(translate.HANDLERS)

    def marker(tag):
        def f(tr, flow):
            raise translate.Unsupported("reached " + tag)
        return f
    try:
        for n in saved_h:
            translate.HANDLERS[n] = marker("the translator of " + n)
        got = reasons_of(NAME_CLASH_VECTORS)
    finally:
        translate.HANDLERS.clear()
        translate.HANDLERS.update(saved_h)
    want = ["reached the translator of " + n if n in saved_h else n for n in names]
    check(got == want and not any(("translator of " + v[3]) in g for v, g in zip(NAME_CLASH_VECTORS, got)),
          "the translator that runs is the one registered under the printed mnemonic, never the id name's (%s)"
          % "; ".join(sorted(set(got))))
    # A compare that is translated must be refused again once its family's opcode entry is gone:
    # nothing but that entry lets it through (the same guard that keeps pshufw from pshufd).
    have = [v for v, n in zip(NAME_CLASH_VECTORS, names) if n in saved_h]
    saved_o = {n: translate.OPCODES[n] for n in set(names) if n in translate.OPCODES}
    try:
        for n in saved_o:
            translate.OPCODES[n] = translate.enc(0x0F58)
        got = reasons_of(have)
    finally:
        translate.OPCODES.update(saved_o)
    check(all("opcode fc2" in g for g in got),
          "%d of them have a translator; with its opcode entry replaced each is refused by the opcode table%s"
          % (len(have), " (%s)" % got[0] if got else ""))
    # cmpneqps and cmpleps have no prefix byte, so against the string translators only the three
    # guards below stand in the way.
    clash = [v for v in NAME_CLASH_VECTORS if v[1] in ("0fc2c104", "0fc2c102")]

    def reasons():
        return reasons_of(clash)
    # Behind the name stand three more layers, each enough on its own.  Make the lifter trust the
    # decoder's id name, which is cmpsq / cmpsb for these two; then take the first layer away,
    # then the second: the SSE compares must still not be translated as string compares.
    saved = {n: translate.OPCODES[n] for n in ("cmpsq", "cmpsb")}
    vec = translate.VECTOR_OK
    name_of = translate.mnemonic_of
    try:
        translate.mnemonic_of = lambda ins: ins.insn_name()
        got = reasons()
        check(all(r.startswith(("cmpsq (opcode fc2", "cmpsb (opcode fc2")) for r in got),
              "with the decoder's id name trusted: refused by the opcode table (%s)" % got[0])
        for n in saved:
            translate.OPCODES[n] = saved[n] | translate.enc(0x0FC2)
        got = reasons()
        check(all("vector or other non-general register" in r for r in got),
              "with the opcode table made to accept them: refused because of the xmm operands (%s)" % got[0])
        translate.VECTOR_OK = vec | {"cmpsq", "cmpsb"}
        got = reasons()
        check(all("is not a string instruction" in r for r in got),
              "with that guard off as well: refused by the string translator's own opcode check (%s)" % got[0])
    finally:
        translate.OPCODES.update(saved)
        translate.VECTOR_OK = vec
        translate.mnemonic_of = name_of
    check(set(translate.HANDLERS) == set(translate.OPCODES), "every translator has an opcode table entry")


def test_wrong_translation_is_caught(image):
    print("harness can fail: 'sub' deliberately translated as 'add' (function test must notice)")
    orig = translate.HANDLERS["sub"]

    def bad(self, flow):
        return [line.replace("x_sub", "x_add") for line in orig(self, flow)]
    translate.HANDLERS["sub"] = bad
    try:
        lifted = os.path.join(OUT, "lifted_bad_sub")
        L.lift_module(image, lifted, quiet=True)
        summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_bad_sub"), seeds=4, quiet=True)
    finally:
        translate.HANDLERS["sub"] = orig
    by = {int(r["rva"], 16): r for r in results}
    check(by[LABELS["calls_out"]]["status"] == "failing", "calls_out (uses sub rsp) is reported failing")
    check(by[LABELS["sum_loop"]]["status"] == "passing", "sum_loop (no sub) still passes")


def test_wrong_flag_is_caught():
    print("harness can fail: 'inc' deliberately clobbers CF (instruction test must notice)")
    orig = translate.HANDLERS["inc"]

    def bad(self, flow):
        out = []
        for line in orig(self, flow):
            for b in (8, 16, 32, 64):
                line = line.replace("x_inc%d(ctx, a)" % b, "x_add%d(ctx, a, 1)" % b)
            out.append(line)
        return out
    summary, insns, n = insn_test("bad_inc", pick(lambda t: t.startswith("inc ")), 48, inc=lambda real: bad)
    check(n > 0 and summary["instructions_failing"] == n, "all %d inc vectors are reported failing" % n)
    only_cf = all(m.startswith("cf:") for r in insns if r["fail"] for m in r["first_failure"]["mismatches"])
    check(only_cf, "and the only difference reported is CF")


MEM_DEST = ("inc", "dec", "neg", "add", "sub", "and", "or", "xor", "adc", "sbb", "xadd")


def test_boundary_values_in_memory_operands():
    print("harness can fail: flags that only change at boundary values of a memory operand (review finding 2)")

    def lock_dec_never_sets_zf(orig):
        def f(self, flow):
            out = orig(self, flow)
            return out + ["ctx->zf = 0;"] if self.lock else out
        return f
    summary, insns, n = insn_test("m2_dec", pick(lambda t: t.startswith("lock dec")), 24, dec=lock_dec_never_sets_zf)
    check(n > 0 and summary["instructions_failing"] == n,
          "'lock dec' that never sets ZF: all %d lock dec vectors fail (%d did)" % (n, summary["instructions_failing"]))
    check(all(m.startswith("zf:") for r in insns if r["fail"] for m in r["first_failure"]["mismatches"]),
          "and the only difference reported is ZF")

    def zf_clear_on_memory_destination(orig):
        def f(self, flow):
            out = orig(self, flow)
            return out + ["ctx->zf = 0;"] if self.ops and self.is_mem(0) else out
        return f
    # 'or mem, non-zero immediate' can never set ZF, so the mutant is right for those two vectors.
    picked = pick(lambda t: t.replace("lock ", "").split(" ")[0] in MEM_DEST and "ptr [" in t.split(",")[0]
                  and not (t.replace("lock ", "").startswith("or ") and t.endswith(", 0x7f")))
    summary, insns, n = insn_test("m2_zf", picked, 24, **{m: zf_clear_on_memory_destination for m in MEM_DEST})
    missed = [r["text"] for r in insns if not r["fail"]]
    check(n > 0 and len(insns) == n and not missed, "ZF forced to 0 on every memory-destination form: all %d vectors "
          "fail%s" % (n, "" if not missed else "; missed: " + "; ".join(missed)))


def vector_states(fam, text, states=24):
    """The states the instruction test gives one hand vector, without running anything:
    [(k, the 16-byte values of its first two operands, misaligned, (regs, xmm, pokes))]."""
    rva, hx = next((r, h) for r, h, t in FAMILY_VECTORS[fam] if t == text)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    ins = next(md.disasm(bytes.fromhex(hx), rva))
    name = family_image(fam).name
    out = []
    for k in range(states):
        mem = difftest.make_mem("rand", k % 4)

        def peek(addr, n):
            off = addr - difftest.ARENA_BASE
            return int.from_bytes(mem[off:off + n], "little") if 0 <= off and off + n <= difftest.ARENA_SIZE else None
        regs, _, xmm, pokes = difftest.insn_state(ins, random.Random("insn:%s:%x:%d" % (name, rva, k)), peek, k,
                                                  random.Random("insn-mem:%s:%x:%d" % (name, rva, k)))
        seen = difftest._overlay(peek, pokes)
        vals = []
        for op in ins.operands[:2]:
            if op.type == capstone.x86_const.X86_OP_REG and op.reg in translate.XMM:
                vals.append(xmm[16 * translate.XMM[op.reg]:16 * translate.XMM[op.reg] + 16])
            elif op.type == capstone.x86_const.X86_OP_MEM:
                vals.append(seen(difftest.ea_of(ins, op, regs), 16).to_bytes(16, "little"))
        out.append((k, vals, bool(difftest.place_aligned_operand(ins, regs, None)), (regs, xmm, pokes)))
    return out


VEC_REGISTER_FORMS = ("xmm1, xmm2", "xmm9, xmm12")
VEC_MEMORY_FORMS = ("xmm1, xmmword ptr [rbx]", "xmm2, xmmword ptr [rax + rcx*4 + 0x20]")


def test_vector_values_are_tried():
    print("harness can fail: packed integer translations wrong only at values random bytes do not hold "
          "(insn-wave review, finding 1)")
    # What the states hold, vector by vector (nothing is run for this).
    missing = []
    for fam, mn, w in (("sse_psll", "psllw", 16), ("sse_psll", "pslld", 32), ("sse_psll", "psllq", 64),
                       ("sse_psra", "psraw", 16), ("sse_psra", "psrad", 32)):
        for form in VEC_REGISTER_FORMS + ("xmm3, xmm3",) + VEC_MEMORY_FORMS:
            got = {int.from_bytes(vals[1][:8], "little") for _, vals, mis, _ in vector_states(fam, mn + " " + form) if not mis}
            if not {0, 1, w - 1, w, w + 1, 1 << 32, (1 << 32) + 1} <= got or not any(1 < c < w - 1 for c in got):
                missing.append(mn + " " + form)
    check(not missing, "a packed shift with its count in a register or in memory: 24 states hold the counts 0, 1, width-1, "
          "width, width+1, 2^32, 2^32+1 and one in between, each where the result is compared%s" % (
              "" if not missing else "; not: " + "; ".join(missing)))
    missing = []
    for mn, w in (("pcmpeqb", 8), ("pcmpeqw", 16), ("pcmpeqd", 32)):
        for form in VEC_REGISTER_FORMS + VEC_MEMORY_FORMS:
            mixed = [sum(x == y for x, y in zip(VS._split(vals[0], w), VS._split(vals[1], w)))
                     for _, vals, mis, _ in vector_states("sse_pcmpeq", mn + " " + form) if not mis]
            if sum(1 for e in mixed if 0 < e < 128 // w) < 3:
                missing.append(mn + " " + form)
    check(not missing, "a packed compare: at least 3 of 24 states have equal lanes among unequal ones%s" % (
        "" if not missing else "; not: " + "; ".join(missing)))
    missing = []
    for mn, w, want in (("packsswb", 16, (-129, -128, -127, 126, 127, 128)), ("packuswb", 16, (-1, 0, 1, 254, 255, 256)),
                        ("packssdw", 32, (-32769, -32768, -32767, 32766, 32767, 32768))):
        for form in VEC_REGISTER_FORMS + VEC_MEMORY_FORMS:
            got = {x for _, vals, mis, _ in vector_states("sse_pack", mn + " " + form) if not mis
                   for half in vals for x in VS._split(half, w)}
            if not {x & ((1 << w) - 1) for x in want} <= got:
                missing.append(mn + " " + form)
    check(not missing, "a pack: 24 states hold both limits of the narrow type and both neighbours of each%s" % (
        "" if not missing else "; not: " + "; ".join(missing)))
    text = "packssdw xmm1, xmmword ptr [rbx]"
    a, b = vector_states("sse_pack", text), vector_states("sse_pack", text)
    VS.ENABLED = False
    try:
        old = vector_states("sse_pack", text)
    finally:
        VS.ENABLED = True
    check([x[3] for x in a] == [x[3] for x in b] and all(x[3] == y[3] for x, y in zip(a, old) if x[0] % 2)
          and all(x[3][0] == y[3][0] for x, y in zip(a, old)) and sum(x[3] != y[3] for x, y in zip(a, old)) == 12,
          "the same seed gives the same states; the odd states and every state's general registers are what they were")
    check(VS.classify("psrlq") == ("count", 64) and VS.classify("paddusb") == ("limits", 8)
          and VS.classify("packusdw") == ("pack", 32) and VS.classify("psrldq") == ("general", None)
          and VS.classify("pxor") == ("general", None), "the class of a mnemonic comes from one table; an unclassified one "
          "(psrldq, pxor) gets the general mix")

    # Three wrong translations that lie in the gap: the states of before accept each, these refuse it.
    def zero_below_the_width(orig):          # the count forced to 64 or more: wrong only for a count below the lane width
        return lambda self, flow: [s.replace("c.q[0]", "(c.q[0] | 64)") for s in orig(self, flow)]

    def never_equal(orig):                   # every lane 'not equal'
        def f(self, flow):
            return orig(self, flow) + ["ctx->xmm[%d].q[0] = 0; ctx->xmm[%d].q[1] = 0;" % (self.xr(0), self.xr(0))]
        return f

    def saturates_one_late(orig):            # the upper limit one too high: 128 (256, 32768) is narrowed as it is, and wraps
        def f(self, flow):
            out = orig(self, flow)
            fld, n, limit, wrapped = {"packsswb": ("sw", 8, 128, -128), "packuswb": ("sw", 8, 256, 0),
                                      "packssdw": ("i", 4, 32768, -32768)}[self.name]
            d = "ctx->xmm[%d].%s[k]" % (self.xr(0), fld)
            return out[:-1] + ["for (int k = 0; k < %d; k++) { if (%s == %d) %s = %d; if (s.%s[k] == %d) s.%s[k] = %d; }" % (
                n, d, limit, d, wrapped, fld, limit, fld, wrapped)] + out[-1:]
        return f
    for tag, what, mns, forms, mutant in (
            ("vv_shift", "a shift that gives zero for every count (wrong only below the lane width)",
             ("psllw", "pslld", "psllq"), VEC_REGISTER_FORMS + ("xmm3, xmm3",), zero_below_the_width),
            ("vv_cmp", "a compare that never reports an equal lane", ("pcmpeqw", "pcmpeqd"), VEC_REGISTER_FORMS, never_equal),
            ("vv_pack", "a pack that saturates one late (128, 256, 32768 wrap)", ("packsswb", "packuswb", "packssdw"),
             VEC_REGISTER_FORMS, saturates_one_late)):
        texts = {"%s %s" % (m, f) for m in mns for f in forms}
        picked = pick(lambda t: t in texts)
        VS.ENABLED = False
        try:
            old, _, n = insn_test(tag + "_old", picked, 24, **{m: mutant for m in mns})
        finally:
            VS.ENABLED = True
        new, insns, _ = insn_test(tag, picked, 24, **{m: mutant for m in mns})
        check(n == len(texts) and old["fail"] == 0 and old["pass"] == 24 * n,
              "%s: the random states of before accept it in all %d register-form vectors (%d cases pass, %d fail)" % (
                  what, n, old["pass"], old["fail"]))
        missed = [r["text"] for r in insns if not r["fail"]]
        check(len(insns) == n and not missed, "and with the vector values every one of them fails (%d cases)%s" % (
            new["fail"], "" if not missed else "; missed: " + "; ".join(missed)))


def test_defined_faults_are_compared(image):
    print("harness can fail: defined guest faults - #DE, int3, ud2 (review finding 4)")

    def never_traps(orig):
        return lambda self, flow: ["(void)x_%s%d(ctx, %s);" % (self.name, self.bits(0), self.val(0, self.bits(0)))]
    summary, insns, n = insn_test("m4_div", pick(lambda t: t.split(" ")[0] in ("div", "idiv")), 24,
                                  div=never_traps, idiv=never_traps)
    check(n > 0 and summary["instructions_failing"] == n,
          "div/idiv without the divide-error check: all %d vectors fail (%d did)" % (n, summary["instructions_failing"]))
    r = mutant_function_result(image, "m4_div", "div_trap_fn", div=never_traps)
    check(r["status"] == "failing" and "#DE" in first_mismatch(r),
          "and 'div ecx; ret' fails the function test (%s)" % (first_mismatch(r) or r["status"])[:70])

    r = mutant_function_result(image, "m4_state", "trap_fn", mov=drop_insn("mov eax, 7"))
    check(r["status"] == "failing" and first_mismatch(r).startswith("rax:"),
          "'mov eax, 7' dropped before an int3: trap_fn fails on rax at the trap")

    def trap_one_byte_late(orig):
        return lambda self, flow: ["TRAP(0x%x);" % (self.ins.address + 1)]
    summary, insns, n = insn_test("m4_rva", pick(lambda t: t in ("int3", "ud2", "hlt", "int 0x29")), 8,
                                  int3=trap_one_byte_late, ud2=trap_one_byte_late, hlt=trap_one_byte_late,
                                  int=trap_one_byte_late)
    check(n == 4 and summary["instructions_failing"] == 4 and summary["pass"] == 0,
          "int3, ud2, hlt and int 0x29 reporting the wrong trap address all fail")


def test_stale_lift_is_refused(image):
    print("harness refuses stale C: lifted output that is not from the current lifter and image (review finding 5)")
    lifted = os.path.join(OUT, "lifted_stale")
    report_path = os.path.join(lifted, image.name + ".report.json")

    def refused(what):
        try:
            difftest.function_test(image, lifted, os.path.join(OUT, "run_stale"), seeds=3,
                                   only={LABELS["sum_loop"]}, quiet=True)
        except difftest.StaleLift:
            return True
        return False
    L.lift_module(image, lifted, quiet=True)
    check(not refused("fresh"), "a fresh lift is tested")

    real = L.lifter_stamp
    L.lifter_stamp = lambda im: real(im)[::-1]            # as if a lifter source file had been edited
    try:
        check(refused("lifter edited"), "refused after the lifter changed")
    finally:
        L.lifter_stamp = real

    c_file = os.path.join(lifted, image.name + "_0000.c")
    text = open(c_file).read()
    with open(c_file, "w") as f:
        f.write(text.replace("x_sub", "x_add", 1))
    check(refused("C edited"), "refused when a generated C file is not the one the report describes")
    with open(c_file, "w") as f:
        f.write(text)
    check(not refused("restored"), "accepted again once the file is restored")

    other = program_image()
    other.data[0x3000] ^= 0xFF                               # the same program lifted from different bytes
    try:
        difftest.function_test(other, lifted, os.path.join(OUT, "run_stale"), seeds=3, only={LABELS["sum_loop"]},
                               quiet=True)
        ok = False
    except difftest.StaleLift:
        ok = True
    check(ok, "refused when the image is not the one that was lifted")


def test_locked_instruction_without_atomic_stops_the_lift():
    print("lifter stops: a locked instruction translated without an atomic operation (review finding 12)")
    locked = [(fam, rva, t) for fam, vs in pick(lambda t: t.startswith("lock ")).items() for rva, t in vs]
    xchg = [(fam, rva, t) for fam, vs in pick(lambda t: t.startswith("xchg ") and "ptr [" in t).items() for rva, t in vs]
    out = os.path.join(OUT, "lifted_m12")

    def stopped(fam, rva):
        try:
            L.lift_instructions(family_image(fam), [rva], out)
        except translate.LifterBug:
            return True
        return False
    check(not any(stopped(fam, rva) for fam, rva, _ in locked + xchg), "the real translations of all %d pass the check"
          % len(locked + xchg))

    def as_if_unlocked(orig):                  # the plain, non-atomic form of the same instruction
        def f(self, flow):
            self.lock = False
            return orig(self, flow)
        return f
    names = sorted({t.split(" ")[1] for _, _, t in locked})
    with patched(**{n: as_if_unlocked for n in names}):
        missed = [t for fam, rva, t in locked if not stopped(fam, rva)]
    check(not missed, "every lock-prefixed vector emitted as a plain read-modify-write stops the lift (%d vectors, "
          "%d mnemonics)%s" % (len(locked), len(names), "" if not missed else "; not stopped: " + "; ".join(missed[:5])))

    def plain_exchange(orig):
        def f(self, flow):
            b = self.bits(0)
            mi = 0 if self.is_mem(0) else 1
            return ["uint%d_t m = %s;" % (b, self.val(mi, b)), "uint%d_t r = %s;" % (b, self.val(1 - mi, b)),
                    self.wr(mi, "r"), self.wr(1 - mi, "m")]
        return f
    with patched(xchg=plain_exchange):
        missed = [t for fam, rva, t in xchg if not stopped(fam, rva)]
    check(len(xchg) >= 5 and not missed, "every xchg with memory emitted as a load and a store stops the lift (%d vectors)"
          % len(xchg))


def link_lifted(mod, lifted, entry, *defines, tag=""):
    """The lifted C of module `mod` as a really-linked program around tests/linked_main.c,
    entered at rva `entry`.  -O0 on purpose: with optimisation the compiler would turn a
    nested call in tail position into a jump anyway; the macros have to guarantee it.
    Returns the path of the executable, or None (after a failed check) if it does not build."""
    exe = os.path.join(OUT, "linked_" + mod + tag)
    cmd = ["clang", "-O0", "-g0", "-fno-strict-aliasing", "-fwrapv", "-Wall", "-Werror",
           "-DENTRY=%s_%x" % (mod, entry), "-DMODULE=" + mod] + ["-D" + d for d in defines]
    cmd += ["-I" + os.path.join(REPO, "harness"), "-I" + lifted, "-o", exe, os.path.join(HERE, "linked_main.c")]
    cmd += sorted(glob.glob(os.path.join(lifted, mod + "_*.c")))
    r = subprocess.run(cmd, capture_output=True, text=True)
    check(r.returncode == 0, "%s: the lifted C compiles and links without -DHARNESS_ISOLATED%s"
          % (mod, "" if r.returncode == 0 else ": " + r.stderr[:300]))
    return exe if r.returncode == 0 else None


def test_lea_discovery():
    print("lifter: a function whose address is taken only by a RIP-relative lea is discovered (issue 51)")
    chunks = {rva: b"".join(bytes.fromhex(h) for h, _ in lines) for rva, lines in LEA_PROGRAMS.values()}
    chunks.update(LEA_DATA)
    chunks[0x3100] = bytes([0x01, 0, 0, 0])
    unw = LEA_LABELS["lea_unwound"]
    image = Image.synthetic("leaimg", 0x4000, chunks, [(0x1000, 0x2000)],
                            [LEA_LABELS["lea_caller"], LEA_LABELS["lea_known"]],
                            pdata=[(unw, unw + len(chunks[unw]), 0x3100)], image_base=PREFERRED_BASE)
    lifted = os.path.join(OUT, "lifted_lea")
    report = L.lift_module(image, lifted, quiet=True)
    by = {int(r["rva"], 16): r for r in report["functions"]}
    leaf = by.get(LEA_LABELS["lea_leaf"], {})
    check(leaf.get("origin") == "lea" and leaf.get("status") == "lifted",
          "lea_leaf (no unwind entry, export, caller or relocation) is found from the lea and lifted (%s)"
          % (leaf.get("origin"),))
    check(sorted(by) == sorted(LEA_LABELS.values()),
          "nothing else became a function: not the undecodable bytes, the string, the padding or the label "
          "inside lea_unwound (%s)" % ["0x%x" % a for a in sorted(set(by) - set(LEA_LABELS.values()))])
    check(by[LEA_LABELS["lea_known"]]["origin"] == "given", "a function known another way keeps its origin")
    lea = report["summary"]["lea_targets"]
    check(lea == {"targets": 6, "known": 1, "inside": 1, "roots": 1, "not_functions": 3},
          "the lift summary counts 6 addresses: 1 known, 1 inside an unwind-described function, 1 new function, "
          "3 that are not function starts (%s)" % lea)
    why = {int(t, 16): w for t, _, w in report["summary"]["lea_not_functions"]}
    check("decode" in why.get(0x1060, "") and "insb" in why.get(0x1068, "") and "padding" in why.get(0x1070, ""),
          "and says why for each of the three (%s)" % sorted(why.values()))
    summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_lea"), seeds=6, quiet=True)
    check(summary["passing"] == 4 and summary["failing"] == 0, "all four functions pass the function test (%d passing)"
          % summary["passing"])
    # The indirect call has to arrive: in the linked program a function that was not lifted
    # is a miss in the dispatch table.
    exe = link_lifted("leaimg", lifted, LEA_LABELS["lea_caller"], "WITH_TABLE")
    if exe is None:
        return
    r = subprocess.run([exe, "7"], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.split() == ["107", "8"],
          "really linked: the call through the lea'd pointer reaches lea_leaf, which adds 100 to ecx "
          "(exit status %d, %r)" % (r.returncode, r.stdout))


def test_linked_tail_calls():
    print("really-linked build: a guest tail call does not use host stack (review finding 13)")
    chunks = {rva: b"".join(bytes.fromhex(h) for h, _ in lines) for rva, lines in TAIL_PROGRAMS.values()}
    entries = sorted(rva for rva, _ in TAIL_PROGRAMS.values())
    image = Image.synthetic("tailimg", 0x4000, chunks, [(0x1000, 0x2000)], entries, image_base=PREFERRED_BASE)
    lifted = os.path.join(OUT, "lifted_tail")
    report = L.lift_module(image, lifted, quiet=True)
    check(report["summary"]["lifted"] == 4, "ping, pong, iping and ipong lifted")
    exe = link_lifted("tailimg", lifted, 0x1000)
    if exe is None:
        return
    r = subprocess.run([exe, "7"], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.split() == ["0", "8"],
          "7 tail calls between two lifted functions: ecx reaches 0 and one return address is popped (%r)" % r.stdout)
    r = subprocess.run([exe, "3000000"], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.split() == ["0", "8"],
          "3,000,000 tail calls at -O0 complete (exit status %d)" % r.returncode)

    print("really-linked build: nor does a tail call through a register or a slot (issue 49)")
    iping, ipong = TAIL_PROGRAMS["iping"][0], TAIL_PROGRAMS["ipong"][0]
    body = "".join(open(p).read() for p in sorted(glob.glob(os.path.join(lifted, "tailimg_*.c"))))
    check(body.count("TAIL_INDIRECT(") == 2, "'jmp rax' and 'jmp [rdx]' are both lifted as TAIL_INDIRECT")
    slots = "SLOTS=0x%x,0x%x" % (iping, ipong)
    exe = link_lifted("tailimg", lifted, iping, "WITH_TABLE", slots, tag="_ind")
    if exe is None:
        return
    r = subprocess.run([exe, "7"], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.split() == ["0", "8"],
          "7 indirect tail calls: ecx reaches 0 and one return address is popped (%r)" % r.stdout)
    r = subprocess.run([exe, "3000000"], capture_output=True, text=True)
    check(r.returncode == 0 and r.stdout.split() == ["0", "8"],
          "3,000,000 indirect tail calls at -O0 complete (exit status %d)" % r.returncode)
    # The same program with TAIL_INDIRECT as the nested host call it used to be: right for a
    # short chain, out of host stack for the long one.  So the check above tests the macro.
    exe = link_lifted("tailimg", lifted, iping, "WITH_TABLE", slots, "TF_MUTANT_NESTED_TAIL_INDIRECT", tag="_nested")
    if exe is None:
        return
    r = subprocess.run([exe, "7"], capture_output=True, text=True)
    short_ok = r.returncode == 0 and r.stdout.split() == ["0", "8"]
    r = subprocess.run([exe, "3000000"], capture_output=True, text=True)
    check(short_ok and r.returncode in (-signal.SIGSEGV, -signal.SIGBUS),
          "as a nested call (mutant) 7 work and 3,000,000 overflow the host stack (short chain %s, exit status %d)"
          % ("right" if short_ok else "wrong", r.returncode))
    # A target that is no function is still a loud stop, not a jump somewhere.
    exe = link_lifted("tailimg", lifted, iping, "WITH_TABLE", "SLOTS=0x%x,0x%x" % (iping, ipong + 8), tag="_miss")
    if exe is None:
        return
    r = subprocess.run([exe, "7"], capture_output=True, text=True)
    check(r.returncode == 5 and "dispatch miss: guest address 0x%x" % (ipong + 8) in r.stderr,
          "an indirect tail call to an address that is not a function stops with a dispatch miss (exit status %d)"
          % r.returncode)


def test_linked_return_check():
    print("really-linked build: a call that does not come back the way it was called is a loud stop (issue 49)")
    at = {n: rva for n, (rva, _) in RET_PROGRAMS.items()}
    chunks = {rva: b"".join(bytes.fromhex(h) for h, _ in lines) for rva, lines in RET_PROGRAMS.values()}
    image = Image.synthetic("retimg", 0x4000, chunks, [(0x1000, 0x2000)], sorted(at.values()), image_base=PREFERRED_BASE)
    lifted = os.path.join(OUT, "lifted_ret")
    report = L.lift_module(image, lifted, quiet=True)
    check(report["summary"]["lifted"] == len(at), "all %d functions lifted (%d)" % (len(at), report["summary"]["lifted"]))

    def run(entry, *defines):
        exe = link_lifted("retimg", lifted, at[entry], *defines, tag="_" + entry + ("_unchecked" if "TF_NO_RSP_CHECK" in defines else ""))
        if exe is None:
            return None
        r = subprocess.run([exe, "7"], capture_output=True, text=True)
        return r.returncode, r.stdout.split(), r.stderr

    def stopped(r, text):
        return r is not None and r[0] == 6 and text in r[2]

    r = run("ok_caller")
    check(r is not None and r[:2] == (0, ["107", "8"]), "a call and a return that match pass the check (%s)" % (r,))
    r = run("slot_caller", "WITH_TABLE", "SLOTS=0x%x" % at["ok_leaf"])
    check(r is not None and r[:2] == (0, ["107", "8"]), "so does a matching call through a slot (%s)" % (r,))
    ret = at["ret8_caller"] + 9
    r = run("ret8_caller")
    check(stopped(r, "the call returning to 0x%x came back with rsp off by 8" % ret),
          "a callee that leaves with 'ret 8' stops at the call site, which is named (%s)" % (r,))
    r = run("slot_caller", "WITH_TABLE", "SLOTS=0x%x" % at["ret8"])
    check(stopped(r, "the call returning to 0x%x came back with rsp off by 8" % (at["slot_caller"] + 6)),
          "the same through a slot (CALL_INDIRECT) (%s)" % (r,))
    r = run("pushret_caller")
    check(stopped(r, "came back with rsp off by -8"), "'push rcx; ret', a ret used as a jump, stops (%s)" % (r,))
    r = run("smash_caller")
    check(stopped(r, "the return address 0x%x was overwritten with 0x7" % (at["smash_caller"] + 9)),
          "a callee that overwrites its return address and returns with rsp right stops too (%s)" % (r,))
    # Without the check both go on as if nothing had happened: that is what it is for.
    r = run("ret8_caller", "TF_NO_RSP_CHECK")
    check(r is not None and r[:2] == (0, ["7", "16"]), "unchecked build (mutant): 'ret 8' goes unnoticed, rsp 8 off (%s)" % (r,))
    r = run("smash_caller", "TF_NO_RSP_CHECK")
    check(r is not None and r[:2] == (0, ["7", "8"]),
          "unchecked build (mutant): the overwritten return address goes unnoticed (%s)" % (r,))


def refuse_insn(text):
    """Mutant: the instruction whose assembly is `text` is unsupported.  Until the stops
    (lifter/stops.py) that made the lifter skip the function it is in; now it is a stop there."""
    def make(orig):
        def f(self, flow):
            if (self.ins.mnemonic + " " + self.ins.op_str).strip() == text:
                raise translate.Unsupported(self.ins.mnemonic)
            return orig(self, flow)
        return f
    return make


def test_linked_skipped_function():
    print("really-linked build: a function the lifter skipped stops with its reason, however it is reached")
    at = {n: RET_PROGRAMS[n][0] for n in ("ok_caller", "ok_leaf", "slot_caller")}
    chunks = {RET_PROGRAMS[n][0]: b"".join(bytes.fromhex(h) for h, _ in RET_PROGRAMS[n][1]) for n in at}
    # The callee, and a last function so that the stub is not the last row of the table, end in
    # a jump the lifter cannot resolve: 'lea rax, [rcx + rcx]; jmp rax'.  (An instruction that
    # is merely unsupported no longer makes a function skipped: test_stops.)
    leaf, last, jmp = at["ok_leaf"], 0x1200, 4
    chunks[leaf] = chunks[last] = bytes.fromhex("488d0409" "ffe0")
    image = Image.synthetic("skipimg", 0x4000, chunks, [(0x1000, 0x2000)], sorted(at.values()) + [last],
                            image_base=PREFERRED_BASE)
    lifted = os.path.join(OUT, "lifted_skip")
    report = L.lift_module(image, lifted, quiet=True)
    by = {int(r["rva"], 16): r for r in report["functions"]}
    skipped = sorted(a for a, r in by.items() if r["status"] == "skipped")
    check(skipped == [leaf, last] and by[leaf]["reasons"][0]["what"] == "jmp rax",
          "ok_leaf and the last function, which end in a jump through a computed register, are skipped; the two "
          "callers are lifted (%s)" % ["0x%x" % a for a in skipped])
    line = "skipped by the lifter: the function at guest address 0x%x (indirect-jump: jmp rax at 0x%x)\n"

    def stop(entry, *defines, tag):
        exe = link_lifted("skipimg", lifted, entry, *defines, tag=tag)
        if exe is None:
            return None
        r = subprocess.run([exe, "7"], capture_output=True, text=True)
        return r.returncode, r.stderr
    r = stop(at["ok_caller"], tag="_direct")
    check(r == (7, line % (leaf, leaf + jmp)), "a direct call to it stops: the first line has the function's rva and the "
          "reason from the report (%s)" % (r,))
    r = stop(at["slot_caller"], "WITH_TABLE", "SLOTS=0x%x" % leaf, tag="_slot")
    check(r == (7, line % (leaf, leaf + jmp)), "a call through the table reaches the same stub: the same line, not a "
          "dispatch miss (%s)" % (r,))
    r = stop(at["slot_caller"], "WITH_TABLE", "SLOTS=0x%x" % last, tag="_slot_last")
    check(r == (7, line % (last, last + jmp)), "each skipped function has its own row and its own reason (%s)" % (r,))
    r = stop(at["slot_caller"], "WITH_TABLE", "SLOTS=0x%x" % (leaf + 1), tag="_slot_miss")
    check(r is not None and r[0] == 5 and "dispatch miss" in r[1],
          "an address that is no function start is still a dispatch miss (%s)" % (r,))
    # What the lifted C says about itself: the readers of the count are not left to guess.
    exe = link_lifted("skipimg", lifted, at["slot_caller"], "WITH_TABLE", "SLOTS=0", tag="_table")
    r = subprocess.run([exe, "table"], capture_output=True, text=True) if exe else None
    check(r is not None and r.stdout.splitlines() == ["4 rows, 2 skipped"] + ["0x%x: indirect-jump: jmp rax at 0x%x" % (a, a + jmp)
                                                                               for a in skipped],
          "the table has 4 rows and says that 2 of them are stubs, which ones and why (%r)" % (r and r.stdout,))
    # The isolated build has no stubs, so no rows for them: its table is the lifted functions.
    table = open(os.path.join(lifted, "skipimg_table.c")).read()
    check(re.search(r"#ifdef HARNESS_ISOLATED\nconst size_t skipimg_table_count = 2;\n#else\n"
                    r"const size_t skipimg_table_count = 4;", table) is not None
          and table.count("SKIPPED(0x") == 2,
          "under -DHARNESS_ISOLATED the table is the 2 lifted functions and the count says 2")
    summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_skip"), seeds=6, quiet=True)
    check(summary["passing"] == 2 and summary["failing"] == 0 and len(results) == 2,
          "and the function test builds and passes on those two (%d passing of %d)" % (summary["passing"], len(results)))


class patched_attrs:
    """Replace attributes of a module or object for the duration of a with-block."""

    def __init__(self, obj, **attrs):
        self.obj, self.attrs = obj, attrs

    def __enter__(self):
        self.saved = {k: getattr(self.obj, k) for k in self.attrs}
        for k, v in self.attrs.items():
            setattr(self.obj, k, v)

    def __exit__(self, *a):
        for k, v in self.saved.items():
            setattr(self.obj, k, v)


def stop_image(name="stopimg"):
    """STOP_PROGRAMS at their addresses, every one a given entry; the table of case_fn."""
    chunks = {rva: b"".join(bytes.fromhex(h) for h, _ in lines) for rva, lines in STOP_PROGRAMS.values()}
    rva, rel, targets = STOP_TABLE
    chunks[rva] = struct.pack("<%di" % len(targets), *[t - rel for t in targets])
    return Image.synthetic(name, 0x4000, chunks, [(0x1000, 0x2000)], sorted(STOP_LABELS.values()),
                           reloc_sites=STOP_RELOCS, image_base=PREFERRED_BASE)


def stop_needs(features):
    """The text the lifted code must carry for these features, from the test's own table."""
    return " ".join("%s=%s" % (f, STOP_CPUID[f]) for f in features.split())


def test_stops():
    print("lifter: an instruction without a translation is a stop inside a lifted function, not a reason to skip it "
          "(finding 38)")
    image = stop_image()
    lifted = os.path.join(OUT, "lifted_stops")
    report = L.lift_module(image, lifted, quiet=True)
    rec = {n: r for n, rva in STOP_LABELS.items() for r in report["functions"] if int(r["rva"], 16) == rva}
    with open(os.path.join(lifted, "stopimg.cfg.json")) as f:
        where = json.load(f)

    def stops_of(n):
        return where.get("stopimg_%x" % STOP_LABELS[n], {}).get("stops")

    def at(n, text):
        rva = STOP_LABELS[n]
        for h, t in STOP_PROGRAMS[n][1]:
            if t == text:
                return rva
            rva += len(h) // 2
        raise KeyError(text)
    xgetbv = at("cap_fn", "xgetbv")
    r = rec["cap_fn"]
    check(r["status"] == "lifted" and r.get("untranslated") == [
        {"insn": "xgetbv", "what": "xgetbv", "rva": "0x%x" % xgetbv, "needs": stop_needs("OSXSAVE"), "count": 1}]
          and stops_of("cap_fn") == [xgetbv],
          "cap_fn (cpuid; skip xgetbv unless OSXSAVE) is lifted, with one stop at the xgetbv that waits for OSXSAVE "
          "(%s, %s)" % (r["status"], r.get("untranslated")))
    r = rec["guard_fn"]
    check(r["status"] == "lifted" and r.get("untranslated_insns") == 2
          and stops_of("guard_fn") == [at("guard_fn", "vmovdqa ymm0, ymm1"), at("guard_fn", "vzeroupper")]
          and all(x["needs"] == stop_needs("AVX") for x in r["untranslated"]),
          "guard_fn (a plain path beside an AVX path) is lifted, with two stops that wait for AVX")
    r = rec["mmx_fn"]
    check(r["status"] == "lifted" and r.get("untranslated_insns") == 2
          and all(x["needs"] == stop_needs("MMX") for x in r["untranslated"]),
          "mmx_fn is lifted too: its two MMX instructions are stops that wait for MMX, which every processor has")
    r = rec["tail_fn"]
    check(r["status"] == "lifted" and r.get("tail_jumps_through_register") == 1 and r.get("untranslated_insns") == 1,
          "tail_fn: the jump analysis walks back over the stop to the pointer load, and the jump is a tail call")
    r = rec["case_fn"]
    check(r["status"] == "lifted" and len(r.get("jump_tables", [])) == 1 and r["jump_tables"][0]["targets"] == 4
          and stops_of("case_fn") == [STOP_TABLE[2][1]],
          "case_fn: the jump table is recovered with its 4 targets, and the stop sits in one case")
    src = "".join(open(p).read() for p in sorted(glob.glob(os.path.join(lifted, "stopimg_0*.c"))))
    check('UNTRANSLATED(0x%x, "xgetbv", "%s");' % (xgetbv, stop_needs("OSXSAVE")) in src,
          "the lifted C stops at the instruction: UNTRANSLATED(rva, what, the features it waits for)")

    # The neighbours that must stay skipped.
    def kinds(n):
        return [x["kind"] for x in rec[n].get("reasons", [])]
    r = rec["writer_fn"]
    check(r["status"] == "skipped" and kinds("writer_fn") == ["indirect-jump"],
          "writer_fn ('xgetbv; jmp rax': the jump goes where the missing instruction says) is skipped for the jump (%s)"
          % (r.get("reasons"),))
    r = rec["mixed_fn"]
    check(r["status"] == "skipped" and kinds("mixed_fn") == ["indirect-jump"]
          and [x["insn"] for x in r.get("untranslated", [])] == ["vzeroupper"] and stops_of("mixed_fn") is None
          and L.skip_reason(r)[0] == "indirect-jump: jmp rax",
          "mixed_fn (a would-be stop and an unresolved jump) is skipped for the jump alone; the instruction is "
          "listed apart and is not the stub's reason (%s)" % (r.get("reasons"),))
    r = rec["reloc_fn"]
    check(r["status"] == "skipped" and kinds("reloc_fn") == ["reloc"] and "untranslated" not in r,
          "reloc_fn (bytes of the instruction lie in a relocation slot) is skipped: a 'reloc' refusal is never a stop")
    s = report["summary"]
    want = sorted([["vzeroupper", 3, 3, stop_needs("AVX")], ["vmovdqa", 1, 1, stop_needs("AVX")],
                   ["xgetbv", 1, 1, stop_needs("OSXSAVE")], ["movq", 1, 1, stop_needs("MMX")],
                   ["emms", 1, 1, stop_needs("MMX")]])
    check(s["lifted_with_stops"] == 5 and s["stops"] == 7 and s["skipped"] == 3
          and s["skipped_by_reason_kinds"] == {"indirect-jump": 2, "reloc": 1} and sorted(s["stops_by_instruction"]) == want,
          "the lift summary: 5 functions lifted with 7 stops, by instruction; 3 skipped, none of them for an "
          "unsupported instruction (%s)" % s["stops_by_instruction"])

    # Without the rule: what the lifter did until now.  Every check above on a lifted function fails then.
    with patched_attrs(lifter_stops, judge=lambda *a: None):
        old = L.lift_module(stop_image("stopold"), os.path.join(OUT, "lifted_stopold"), quiet=True)
    was = {int(r["rva"], 16): r for r in old["functions"]}
    check(all(was[STOP_LABELS[n]]["status"] == "skipped" for n in ("cap_fn", "guard_fn", "mmx_fn", "tail_fn", "case_fn"))
          and was[STOP_LABELS["cap_fn"]]["reasons"][0]["what"] == "xgetbv" and old["summary"]["skipped"] == 8
          and old["summary"]["lifted_with_stops"] == 0,
          "with the rule switched off (mutant) the five are skipped whole, cap_fn for its one xgetbv, as before")

    print("function test: a function with a stop passes on the paths that avoid it, and a case that reaches the stop "
          "must arrive there in the oracle's state")
    summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_stops"), seeds=12, quiet=True)
    res = {n: r for n, rva in STOP_LABELS.items() for r in results if int(r["rva"], 16) == rva}
    r = res["cap_fn"]
    check(r["status"] == "passing" and r["agree"] >= 3 and r.get("stops") == 1 and r["stopped"] >= 3
          and r["stopped_at"] == ["0x%x" % xgetbv] and r["agree"] + r["stopped"] == r["seeds"],
          "cap_fn passes: %d seeds leave by the path CPUID closes for us, %d reach the xgetbv and stop there in agreement"
          % (r["agree"], r.get("stopped", -1)))
    check(r["insns_executed"] == r["insns"] - 2,
          "the xgetbv and the instruction only it leads to are not counted as executed (%d of %d)"
          % (r["insns_executed"], r["insns"]))
    r = res["guard_fn"]
    check(r["status"] == "passing" and r["stopped"] >= 3 and r["stopped_at"] == ["0x%x" % at("guard_fn", "vmovdqa ymm0, ymm1")]
          and r["insns_executed"] == 4,
          "guard_fn passes on its plain path (%d seeds); %d seeds stop at the first AVX instruction"
          % (r["agree"], r.get("stopped", -1)))
    r = res["case_fn"]
    check(r["status"] == "passing" and r["stopped"] >= 1 and r["stopped_at"] == ["0x%x" % STOP_TABLE[2][1]],
          "case_fn passes on three cases of its switch and stops in the fourth (%d seeds)" % r.get("stopped", -1))
    r = res["mmx_fn"]
    check(r["status"] == "inconclusive" and r["agree"] == 0 and r["stopped"] == r["seeds"],
          "mmx_fn, where no path avoids the stop, is not passing: all %d seeds stop at its first instruction (%s)"
          % (r["seeds"], r["status"]))
    check(summary["failing"] == 0 and summary["functions_with_stops"] == 5 and summary["functions_with_stops_passing"] == 3
          and summary["cases_ending_at_a_stop"] == sum(x.get("stopped", 0) for x in results)
          and summary["inconclusive_functions_by_reason"].get(difftest.UNTRANSLATED_WHAT) == 2,
          "the summary: 5 functions with stops, 3 passing, 2 inconclusive because of the stop, none failing (%s)"
          % summary["inconclusive_functions_by_reason"])

    # The function test can fail on a stop: three wrong lifters.
    def cap_fn_under(tag, obj=None, **mutant):
        out = os.path.join(OUT, "lifted_stops_" + tag)
        with (patched_attrs(obj, **mutant) if obj else patched(**mutant)):
            L.lift_module(image, out, quiet=True)
            _, rs = difftest.function_test(image, out, os.path.join(OUT, "run_stops_" + tag), seeds=12,
                                           only={STOP_LABELS["cap_fn"]}, quiet=True)
        return rs[0]
    real = lifter_stops.c_statement
    r = cap_fn_under("late", lifter_stops, c_statement=lambda rva, what, need: real(rva + 1, what, need))
    check(r["status"] == "failing" and "at rva 0x%x; candidate: trap (at rva 0x%x)" % (xgetbv, xgetbv + 1) in first_mismatch(r),
          "a stop that names the wrong address fails (%s)" % (first_mismatch(r) or r["status"])[-60:])
    r = cap_fn_under("none", lifter_stops, c_statement=lambda rva, what, need: "")
    check(r["status"] == "failing" and first_mismatch(r).startswith("oracle: " + difftest.UNTRANSLATED_WHAT),
          "lifted code that runs on past the instruction fails (%s)" % (first_mismatch(r) or r["status"])[-40:])
    r = cap_fn_under("state", xor=drop_insn("xor ecx, ecx"))
    check(r["status"] == "failing" and first_mismatch(r).startswith("rcx: oracle 0x0 "),
          "'xor ecx, ecx' dropped before the stop: fails on rcx at the stop (%s)" % (first_mismatch(r) or r["status"])[:40])
    return image, lifted


def test_stop_vectors():
    print("lifter: which refused instructions become a stop and what each waits for (%d), and which still make it "
          "skip the function (%d)" % (len(STOP_VECTORS), len(STOP_REFUSED)))
    chunks = {v[0]: bytes.fromhex(v[1]) + b"\xc3" for v in STOP_VECTORS + STOP_REFUSED}

    def lift(tag):
        image = Image.synthetic("stopvec", 0x4000, chunks, [(0x1000, 0x2000)], sorted(chunks), image_base=PREFERRED_BASE)
        return {int(r["rva"], 16): r for r in L.lift_module(image, os.path.join(OUT, "lifted_stopvec" + tag),
                                                             quiet=True)["functions"]}
    by = lift("")
    for rva, h, text, features in STOP_VECTORS:
        r = by[rva]
        u = r.get("untranslated", [])
        # The lifter's name for the instruction (DESIGN.md section 3): the mnemonic the decoder prints,
        # without a prefix, and 'movsl' for the string instruction on doublewords that it prints as movsd.
        name = "movsl" if h == "f2a5" else text.split()[1] if text.startswith("lock ") else text.split()[0]
        check(r["status"] == "lifted" and len(u) == 1 and u[0]["rva"] == "0x%x" % rva and u[0]["needs"] == stop_needs(features)
              and u[0]["insn"] == name and name in u[0]["what"],
              "%-14s %-38s a stop; waits for %s%s" % (h, text, features or "no feature the lifter knows", "" if r["status"] == "lifted"
                                                    else " (SKIPPED: %s)" % r["reasons"]))
    check(all(lifter_stops.CPUID_BITS[f] == (int(v.split(".")[0], 16), v.split(".")[1], int(v.split(".")[2]))
              for f, v in STOP_CPUID.items()),
          "the lifter's table places those %d features where the test's copy of the CPUID tables does" % len(STOP_CPUID))
    for rva, h, text in STOP_REFUSED:
        r = by[rva]
        first = r.get("reasons", [{}])[0]
        check(r["status"] == "skipped" and first.get("rva") == "0x%x" % rva and first.get("kind") == "unsupported"
              and "untranslated" not in r,
              "%-14s %-38s not a stop: its function is skipped (%s)" % (h, text, first.get("what", "LIFTED")))
    # What holds them back.  Five are held by recursive descent's own verdict (a jump, a call, a
    # branch it does not follow); the others only by the decoder's groups and the list of names.
    with patched_attrs(lifter_stops, NOT_STRAIGHT_GROUPS=frozenset(), NOT_STRAIGHT=frozenset()):
        open_ = lift("_open")
    loose = [text for rva, h, text in STOP_REFUSED if open_[rva]["status"] == "lifted"]
    held = [text for rva, h, text in STOP_REFUSED if open_[rva]["status"] == "skipped"]
    check(sorted(held) == sorted(["jmp ptr [rbx]", "call ptr [rbx]", "ret", "jmp 0x1626", "jmp rdx"]) and len(loose) == 18,
          "with the two fences taken down (mutant) %d of them would be lifted with a stop; the %d that recursive descent "
          "itself treats as transfers stay skipped" % (len(loose), len(held)))


def test_linked_stops(image, lifted):
    print("really-linked build: lifted code runs up to a stop and halts there, naming the instruction and what it waits for")
    guard, avx = STOP_LABELS["guard_fn"], STOP_LABELS["guard_fn"] + 9
    line = "untranslated instruction at guest address 0x%x: vmovdqa (needs: %s)\n" % (avx, stop_needs("AVX"))

    def run(entry, arg, *defines, tag):
        exe = link_lifted("stopimg", lifted, entry, *defines, tag=tag)
        if exe is None:
            return None
        r = subprocess.run([exe, arg], capture_output=True, text=True)
        return r.returncode, r.stdout.split(), r.stderr
    r = run(guard, "2", tag="_guard")
    check(r == (0, ["2", "8"], ""), "guard_fn entered with the bit clear takes its plain path and returns (%s)" % (r,))
    r = run(guard, "3", tag="_guard")
    check(r == (8, [], line), "entered with the bit set it stops at the AVX instruction: status 8 and one line (%s)" % (r,))
    r = run(STOP_LABELS["stop_caller"], "3", tag="_direct")
    check(r == (8, [], line), "the same when a lifted caller reaches it by a direct call (%s)" % (r,))
    r = run(STOP_LABELS["stop_slot_caller"], "3", "WITH_TABLE", "SLOTS=0x%x" % guard, tag="_slot")
    check(r == (8, [], line), "and through the dispatch table (%s)" % (r,))
    r = run(STOP_LABELS["stop_slot_caller"], "2", "WITH_TABLE", "SLOTS=0x%x" % guard, tag="_slot")
    check(r == (0, ["2", "8"], ""), "through the table with the bit clear the function runs: its row is the function, "
          "not a stub (%s)" % (r,))
    r = run(STOP_LABELS["mmx_fn"], "0", tag="_mmx")
    check(r == (8, [], "untranslated instruction at guest address 0x%x: movq (opcode f6f /0 is not a form the lifter "
                "translates) (needs: %s)\n" % (STOP_LABELS["mmx_fn"], stop_needs("MMX"))),
          "an MMX instruction stops with its reason and the feature it waits for (%s)" % (r,))
    exe = link_lifted("stopimg", lifted, STOP_LABELS["stop_slot_caller"], "WITH_TABLE", "SLOTS=0", tag="_table")
    r = subprocess.run([exe, "table"], capture_output=True, text=True) if exe else None
    check(r is not None and r.stdout.splitlines()[0] == "%d rows, 3 skipped" % len(STOP_LABELS),
          "the table has a row for each of the %d functions; 3 are stubs of skipped functions, none of them a function "
          "with a stop (%r)" % (len(STOP_LABELS), r and r.stdout.splitlines()[0]))
    # An instruction with no feature bit: what test_linked_skipped_function's functions used to be skipped for.
    chunks = {RET_PROGRAMS[n][0]: b"".join(bytes.fromhex(h) for h, _ in RET_PROGRAMS[n][1]) for n in ("ok_caller", "ok_leaf")}
    plain = Image.synthetic("plainimg", 0x4000, chunks, [(0x1000, 0x2000)], sorted(chunks), image_base=PREFERRED_BASE)
    out = os.path.join(OUT, "lifted_plain")
    with patched(add=refuse_insn("add ecx, 0x64")):
        rep = L.lift_module(plain, out, quiet=True)
    exe = link_lifted("plainimg", out, RET_PROGRAMS["ok_caller"][0])
    r = subprocess.run([exe, "7"], capture_output=True, text=True) if exe else None
    check(rep["summary"]["skipped"] == 0 and r is not None and (r.returncode, r.stderr) == (
        8, "untranslated instruction at guest address 0x%x: add (needs: no feature bit known)\n" % RET_PROGRAMS["ok_leaf"][0]),
          "a plain instruction made unsupported (mutant) no longer skips its function: the call arrives and stops at "
          "the instruction (%s)" % (r and (r.returncode, r.stderr),))


def test_known_oracle_bug_is_inconclusive(iimage):
    print("harness: an instruction the oracle executes wrongly is inconclusive, not a failure (review finding 8)")
    summary, _, insns = difftest.instruction_test(
        iimage, os.path.join(OUT, "lifted_obug"), os.path.join(OUT, "run_obug"), states=24, quiet=True,
        rvas=[rva for rva, _, _ in ORACLE_BUG_VECTORS])
    check(summary["not_lifted"] == 0 and summary["pass"] == 0 and summary["fail"] == 0
          and summary["inconclusive"] == 24 * len(ORACLE_BUG_VECTORS),
          "shld/shrd with a RIP-relative operand and an immediate count: all %d cases inconclusive (pass %d, fail %d)"
          % (24 * len(ORACLE_BUG_VECTORS), summary["pass"], summary["fail"]))
    check(all(r.get("inconclusive_reasons") == [difftest.ORACLE_BUG_SHIFTD] for r in insns),
          "and the reason names the oracle bug")


def test_ret_with_a_large_count_is_inconclusive():
    """engine+0x570a80 (data that a lea points at, lifted as a function) ends in `ret 0x8401`:
    the emulator moved rsp down, the lifted code up, and the function failed on every seed."""
    print("harness: ret imm16 with a count of 0x8000 or more is inconclusive: the emulator takes the count as negative")
    image = bytearray(0x4000)
    image[0x1000:0x1003] = bytes.fromhex("c20184")          # ret 0x8401
    image[0x1010:0x1013] = bytes.fromhex("c2ff7f")          # ret 0x7fff
    image[0x1020:0x1021] = bytes.fromhex("c3")              # ret

    def run(rva, size):
        o = difftest.Oracle(bytes(image))
        o.load_mem(difftest.make_mem("ptr", 0))
        regs, eflags, xmm = difftest.func_state(random.Random(7))
        return o.run(difftest.Case(id=0, key="ret", func_rva=rva, ranges=[[rva, rva + size]], mem_id=0, regs=regs,
                                   eflags=eflags, xmm=xmm, seed=7))
    r = run(0x1000, 3)
    check(r["status"] == "inconclusive" and r["reason"] == difftest.ORACLE_BUG_RET_IMM,
          "ret 0x8401: inconclusive, and the reason names the oracle bug (%s)" % r.get("reason", r["status"]))
    difftest.RET_IMM_RULE = False
    try:
        raw = run(0x1000, 3)
    finally:
        difftest.RET_IMM_RULE = True
    check(raw["status"] == "ok" and raw["regs"][4] == difftest.RSP0 + 8 - 0x7BFF,
          "without the rule the emulator leaves rsp at start + 8 - 0x7bff, below where it began: the rule is still "
          "needed (rsp - start = %#x)" % (raw["regs"][4] - difftest.RSP0 if raw["status"] == "ok" else -1))
    r = run(0x1010, 3)
    check(r["status"] == "ok" and r["regs"][4] == difftest.RSP0 + 8 + 0x7FFF, "not touched: ret 0x7fff releases 0x7fff bytes")
    r = run(0x1020, 1)
    check(r["status"] == "ok" and r["regs"][4] == difftest.RSP0 + 8, "not touched: ret")


def test_bit_scan_of_zero_keeps_the_register():
    """engine+0xb4bb0 scans a mask that can be zero and indexes a table with the register."""
    print("oracle compensation: bsf/bsr r32 with a zero source leaves all 64 bits of the destination (measured; "
          "the emulator clears the upper half)")
    image = bytearray(0x4000)
    for at, code in ((0x1000, "0fbccac3"),          # bsf ecx, edx; ret
                     (0x1010, "0fbdcac3"),          # bsr ecx, edx; ret
                     (0x1020, "480fbccac3"),        # bsf rcx, rdx; ret
                     (0x1030, "660fbccac3")):       # bsf cx, dx; ret
        image[at:at + len(code) // 2] = bytes.fromhex(code)
    OLD = 0x1122334455667788

    def run(rva, size, rdx):
        o = difftest.Oracle(bytes(image))
        o.load_mem(difftest.make_mem("ptr", 0))
        regs, eflags, xmm = difftest.func_state(random.Random(7))
        regs[1], regs[2] = OLD, rdx
        r = o.run(difftest.Case(id=0, key="bscan", func_rva=rva, ranges=[[rva, rva + size]], mem_id=0, regs=regs,
                                eflags=eflags, xmm=xmm, seed=7))
        return r["status"], r["regs"][1], r["fixes"].get("bscan", 0)
    for name, rva in (("bsf", 0x1000), ("bsr", 0x1010)):
        got = run(rva, 4, 0)
        check(got == ("ok", OLD, 1), "%s ecx, edx with edx = 0: rcx keeps all 64 bits, and the case is counted (rcx %#x, %d)"
              % (name, got[1], got[2]))
    got = run(0x1000, 4, 1 << 32)
    check(got == ("ok", OLD, 1), "the source is the low half: bsf ecx, edx with rdx = 2^32 is a zero source (rcx %#x)" % got[1])
    difftest.BSCAN_RULE = False
    try:
        raw = run(0x1000, 4, 0)
    finally:
        difftest.BSCAN_RULE = True
    check(raw == ("ok", OLD & 0xFFFFFFFF, 0),
          "without the correction the emulator clears the upper half: it is still needed (rcx %#x)" % raw[1])
    check(run(0x1000, 4, 0x50) == ("ok", 4, 0) and run(0x1010, 4, 0x50) == ("ok", 6, 0),
          "not touched: a source that is not zero (bsf 4, bsr 6, upper half cleared as for any 32-bit result)")
    got = run(0x1020, 5, 0), run(0x1030, 5, 0)
    check(got == (("ok", OLD, 0), ("ok", OLD, 0)),
          "not touched: 64-bit and 16-bit destinations, which the emulator leaves whole (%#x, %#x)" % (got[0][1], got[1][1]))


def test_wild_write_is_a_failed_case(image):
    print("harness survives: 'setcc' deliberately also writes through a wild pointer")
    orig = translate.HANDLERS["seto"]

    def bad(self, flow):
        return orig(self, flow) + ["ST8(UINT64_C(0x10), 1);"]
    translate.HANDLERS["seto"] = bad
    try:
        lifted = os.path.join(OUT, "lifted_wild")
        L.lift_module(image, lifted, quiet=True)
        summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_wild"), seeds=4, quiet=True)
    finally:
        translate.HANDLERS["seto"] = orig
    by = {int(r["rva"], 16): r for r in results}
    r = by[LABELS["reg_writes"]]
    check(r["status"] == "failing" and "signal" in r["failures"][0]["mismatches"][0],
          "reg_writes (uses seto) fails with a signal in the child")
    check(by[LABELS["sum_loop"]]["status"] == "passing", "the other functions were still tested")


# ---- float instructions (harness/floatcase.py, harness/fpmode.h) --------------------------------------

FLOAT_FAMILY = "sse_scalar_arith"


def float_session(tag, opt="-O1", family=FLOAT_FAMILY):
    """A Session over the float family's vectors, with the driver built with `opt`."""
    img = family_image(family)
    d = os.path.join(OUT, "lifted_%s_insn" % tag)
    L.lift_instructions(img, [rva for rva, _, _ in FAMILY_VECTORS[family]], d)
    s = difftest.Session(img, d, os.path.join(OUT, "run_" + tag), opt)
    s.build_driver()
    s.add_mem("rand", 0)
    return s


def float_case(text, a, b, mxcsr=FC.MXCSR_DEFAULT, upper=0, family=FLOAT_FAMILY):
    """One case of the vector `text` (xmm1 op= xmm2, single precision) with lane 0 of xmm1 = a and
    of xmm2 = b; every other lane of both registers holds `upper`."""
    rva = next(r for r, _, t in FAMILY_VECTORS[family] if t == text)
    size = next(len(h) // 2 for r, h, _ in FAMILY_VECTORS[family] if r == rva)
    xmm = bytearray(256)
    for reg, v in ((1, a), (2, b)):
        xmm[16 * reg:16 * reg + 16] = v.to_bytes(4, "little") + upper.to_bytes(4, "little") * 3
    regs = [0] * 16
    regs[4] = difftest.RSP0
    return difftest.Case(id=0, key=rva, func_rva=rva, ranges=[[rva, rva + size]], end=rva + size, insn_rva=rva,
                         mem_id=0, regs=regs, eflags=2, xmm=bytes(xmm), seed=1, mxcsr=mxcsr)


def lane0(result, reg=1):
    return int.from_bytes(result["xmm"][16 * reg:16 * reg + 4], "little")


def test_float_modes():
    print("float mode: lifted code runs in the guest mode, host code in the host mode (driver.c)")
    inf_minus_inf = float_case("addss xmm1, xmm2", 0x7F800000, 0xFF800000)
    s = float_session("fp")
    r = s.run_candidate([inf_minus_inf])[0]
    check(r["status"] == 0 and lane0(r) == 0xFFC00000,
          "inf + -inf in lifted code is ffc00000, the x86 default NaN (got %08x, status %s)" % (
              lane0(r), difftest.ST_NAMES.get(r["status"])))
    o = s.run_oracle([inf_minus_inf])[0]
    check(o["status"] == "ok" and lane0(o) == 0xFFC00000, "and so it is in the oracle")
    # the same sum, rounded: 1 + 2^-24 + 2^-47 lies between 1 and its neighbour
    up = float_case("addss xmm1, xmm2", 0x3F800000, 0x33000001, mxcsr=0x5F80)
    check(lane0(s.run_candidate([up])[0]) == 0x3F800001, "1 + (2^-25 and a bit) under round-up is the next float above 1")
    m = float_session("fp_noguest", "-O1 -DTF_MUTANT_NO_GUEST_MODE")
    r = m.run_candidate([inf_minus_inf])[0]
    check(lane0(r) == 0x7FC00000, "harness can fail: a driver that does not enter the guest mode gives 7fc00000 (got %08x)" % lane0(r))
    m = float_session("fp_rn", "-O1 -DTF_MUTANT_ROUND_NEAREST")
    check(lane0(m.run_candidate([up])[0]) == 0x3F800000,
          "harness can fail: a driver that ignores the rounding mode gives 1.0 under round-up")
    m = float_session("fp_nohost", "-O1 -DTF_MUTANT_NO_HOST_MODE")
    r = m.run_candidate([inf_minus_inf])[0]
    check(r["status"] == 8 and r["aux"] == 2,
          "harness can fail: a driver whose host code runs in the guest mode is reported (%s, %s)" % (
              difftest.ST_NAMES.get(r["status"]), difftest.FPMODE_WHAT.get(r["aux"])))
    v = difftest.compare(inf_minus_inf, o, r, "insn", s.md, s.image.data)
    check(v and "float mode violated" in v[0], "and the comparison fails such a case: %s" % (v[0] if v else v))


def test_nan_correction():
    print("oracle correction: with two NaN operands SSE returns the first one, quieted (the emulator does not)")
    s = float_session("fp_nan")
    t = "addss xmm1, xmm2"

    def run(a, b, upper=0):
        o = s.run_oracle([float_case(t, a, b, upper=upper)])[0]
        return lane0(o), o["nan_fixed"], o["xmm"][20:32]
    got = run(0x7F800001, 0x7FE27B08)
    check(got[:2] == (0x7FC00001, 1), "addss(7f800001, 7fe27b08): corrected to 7fc00001, and counted (got %08x, %d)" % got[:2])
    got = run(0x7FC12345, 0xFFC54321)
    check(got[:2] == (0x7FC12345, 1), "two quiet NaNs: the first (got %08x, %d)" % got[:2])
    saved = FC.NAN_FIRST_OPERAND
    FC.NAN_FIRST_OPERAND = frozenset()
    try:
        raw = s.run_oracle([float_case(t, 0x7F800001, 0x7FE27B08)])[0]     # a new emulator: nothing is remembered
    finally:
        FC.NAN_FIRST_OPERAND = saved
    check(lane0(raw) == 0x7FE27B08 and raw["nan_fixed"] == 0,
          "without the correction the emulator gives 7fe27b08 (the x87 rule): the correction is still needed (got %08x)" % lane0(raw))
    for a, b, want, what in ((0x7F800001, 0x3F800000, 0x7FC00001, "a signalling NaN and a number"),
                             (0x3F800000, 0x7FA00000, 0x7FE00000, "a number and a signalling NaN"),
                             (0x3F800000, 0x40000000, 0x40400000, "two numbers"),
                             (0x7F800000, 0xFF800000, 0xFFC00000, "inf - inf")):
        got = run(a, b)
        check(got[:2] == (want, 0), "not touched: %s (%08x, changed %d)" % (what, got[0], got[1]))
    got = run(0x3F800000, 0x40000000, upper=0x7FA00001)
    check(got[1] == 0 and got[2] == bytes.fromhex("0100a07f") * 3,
          "not touched: NaN pairs in the upper lanes of a scalar instruction")
    check(FC.NAN_FIRST_OPERAND == {o + x for o in ("add", "sub", "mul", "div") for x in ("ss", "sd", "ps", "pd")}
          and FC.nan_spec(next(s.md.disasm(bytes.fromhex("f30f10ca"), 0x1000))) is None,
          "it applies to the float arithmetic of SDM table 4-7 only, and only where a family declared it float")


def test_alignment_fault(insns):
    print("oracle compensation: a misaligned 16-byte operand of an SSE instruction is #GP (the emulator runs on)")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    dec = lambda hx: next(md.disasm(bytes.fromhex(hx), 0x1000))
    yes = ("0f2803 movaps load", "0f2903 movaps store", "660f6f03 movdqa", "0f5803 addps", "660f70031b pshufd", "660fef0b pxor")
    no = ("0f1003 movups", "f30f6f03 movdqu", "660f1003 movupd", "f20ff003 lddqu", "f30f5803 addss (m32)", "0f5a03 cvtps2pd (m64)",
          "0f58c1 addps, registers", "c5f05803 vaddps (VEX)", "480fc70b cmpxchg16b (its own rule)",
          "660f2f03 comisd (m64 that the decoder calls 16 bytes)", "660f2e03 ucomisd (m64)")
    check(all(OF.aligned_operand(dec(x.split()[0])) is not None for x in yes)
          and all(OF.aligned_operand(dec(x.split()[0])) is None for x in no),
          "the rule: legacy SSE with an m128 (%s), not %s" % (", ".join(x.split(None, 1)[1] for x in yes),
                                                              ", ".join(x.split(None, 1)[1] for x in no)))
    check(OF.UNALIGNED_OK == translate.UNALIGNED_M128 and OF.DECODER_M64 == translate.DECODER_M64,
          "the lifter's lists of exempt mnemonics and of wrongly sized operands are the harness's")
    fam = "sse_addsub_p"
    s = float_session("align", family=fam)
    text = "addps xmm1, xmmword ptr [rbx]"
    rva, hx = next((r, h) for r, h, t in FAMILY_VECTORS[fam] if t == text)

    def case(rbx):
        regs = [0] * 16
        regs[3], regs[4] = rbx, difftest.RSP0
        xmm = bytearray(256)
        xmm[16:32] = struct.pack("<4f", 1.0, 2.0, 3.0, 4.0)
        return difftest.Case(id=0, key=rva, func_rva=rva, ranges=[[rva, rva + len(hx) // 2]], end=rva + len(hx) // 2,
                             insn_rva=rva, mem_id=0, regs=regs, eflags=2, xmm=bytes(xmm), seed=1,
                             pokes=[(rbx, struct.pack("<4f", 0.5, 0.25, 0.125, 8.0))])
    base = difftest.ARENA_BASE + 0x40000
    good, bad = case(base), case(base + 8)
    o, c = s.run_oracle([good])[0], s.run_candidate([good])[0]
    want = struct.pack("<4f", 1.5, 2.25, 3.125, 12.0)
    check(o["status"] == "ok" and o["xmm"][16:32] == want and not o["fixes"].get("align") and c["status"] == 0
          and c["xmm"][16:32] == want, "not touched: an aligned operand (oracle and lifted code add the four lanes)")
    o, c = s.run_oracle([bad])[0], s.run_candidate([bad])[0]
    check(o["status"] == "trap" and o["trap_rva"] == rva and o["fixes"].get("align") == 1 and o["xmm"] == bad.xmm,
          "the operand at 8 modulo 16: the oracle reports #GP at the instruction, nothing written, and counts it")
    check(c["status"] == 1 and c["aux"] == rva and not difftest.compare(bad, o, c, "insn", s.md, s.image.data),
          "and the lifted code traps there too (status %s at 0x%x)" % (difftest.ST_NAMES.get(c["status"]), c["aux"]))
    saved = OF.aligned_operand
    OF.aligned_operand = lambda ins: None
    try:
        raw = s.run_oracle([bad])[0]
    finally:
        OF.aligned_operand = saved
    check(raw["status"] == "ok" and raw["xmm"][16:32] == want,
          "without the compensation the emulator executes it: the compensation is still needed")

    def unchecked(orig):
        def f(tr, flow):
            out = orig(tr, flow)
            tr.pre[:] = [ln for ln in tr.pre if "TRAP" not in ln]
            return out
        return f
    summary, recs, n = insn_test("align_nocheck", {fam: [(rva, text)]}, 24, addps=unchecked)
    m = recs[0]["m128_operand"]
    check(m["misaligned_cases"] > 0 and m["misaligned_pass"] == 0 and m["aligned_pass"] == m["aligned_cases"] > 0
          and summary["fail"] == m["misaligned_cases"],
          "harness can fail: addps without the alignment check fails its %d misaligned cases and no other (of %d)" % (
              m["misaligned_cases"], summary["cases"]))
    # what the vectors of every family showed (test_instructions)
    al = [r for r in insns if "m128_operand" in r]
    names = {translate.mnemonic_of_text(r["text"]) for r in al}
    check({"movaps", "movdqa", "addps", "subpd"} <= names and not names & OF.UNALIGNED_OK
          and all(r["m128_operand"]["aligned_pass"] == r["m128_operand"]["aligned_cases"]
                  and r["m128_operand"]["misaligned_pass"] == r["m128_operand"]["misaligned_cases"] for r in al)
          and sum(r["m128_operand"]["misaligned_cases"] for r in al) > 100
          and sum(r["m128_operand"]["aligned_cases"] for r in al) > 400,
          "vectors: %d instructions of %d mnemonics have such an operand; %d aligned and %d misaligned cases, all pass; "
          "movups, movupd, movdqu are not among them" % (
              len(al), len(names), sum(r["m128_operand"]["aligned_cases"] for r in al),
              sum(r["m128_operand"]["misaligned_cases"] for r in al)))


def state_case(family, text, xmm=None, eflags=2, pokes=(), fpcw=SF.FPCW_DEFAULT):
    """One case of the vector `text` of `family`: zero registers but rsp, the given xmm bytes, flags, pokes, control word."""
    rva, size = next((r, len(h) // 2) for r, h, t in FAMILY_VECTORS[family] if t == text)
    regs = [0] * 16
    regs[4] = difftest.RSP0
    return difftest.Case(id=0, key=rva, func_rva=rva, ranges=[[rva, rva + size]], end=rva + size, insn_rva=rva, mem_id=0,
                         regs=regs, eflags=eflags, xmm=bytes(xmm or bytes(256)), seed=1, pokes=pokes, fpcw=fpcw)


def test_statefix():
    print("statefix: pcmpistri equal ordered, the x87 control word, RFLAGS in user mode (harness/statefix.py)")
    both = lambda s, c: (s.run_oracle([c])[0], s.run_candidate([c])[0])
    word = lambda r, addr, n: int.from_bytes(r["pages"][addr & ~0xFFF][addr & 0xFFF:(addr & 0xFFF) + n], "little")
    AR = 0x8D5
    # two answers of the real CPU (Core i5-7600K, cache family fo-pcmpistri) that the emulator alone gets wrong:
    # the needle's first seven bytes at 9..15, and the needle's first word at 7
    s = float_session("sf_pcmp", family="sse_pcmpistri")

    def pc(text, a, b):
        xmm = bytearray(256)
        xmm[16:48] = bytes.fromhex(a) + bytes.fromhex(b)
        o, k = both(s, state_case("sse_pcmpistri", text, xmm, eflags=2 | AR))
        return o["regs"][1], o["eflags"] & AR, o["fixes"].get("pcmpistri", 0), k["regs"][1], k["eflags"] & AR
    a8, b8 = "08030702060201070108040107010205", "01010102010101010208030702060201"
    a16, b16 = "02010100010002010100020102010201", "04000600040008000700020007000201"
    got = pc("pcmpistri xmm1, xmm2, 0xc", a8, b8)
    check(got == (9, 1, 1, 9, 1), "0x0c, a match that runs off the end: ECX 9, CF, as the CPU; counted; lifted code agrees %r" % (got,))
    got = pc("pcmpistri xmm1, xmm2, 0xd", a16, b16)
    check(got == (7, 0x1, 1, 7, 0x1), "0x0d, the same on words: ECX 7, CF %r" % (got,))
    saved = SF.pcmpistri_plan
    SF.pcmpistri_plan = lambda ins: None
    try:
        xmm = bytearray(256)
        xmm[16:48] = bytes.fromhex(a8) + bytes.fromhex(b8)
        raw = s.run_oracle([state_case("sse_pcmpistri", "pcmpistri xmm1, xmm2, 0xc", xmm)])[0]
    finally:
        SF.pcmpistri_plan = saved
    check(raw["regs"][1] == 16 and not raw["eflags"] & 1 and not raw["fixes"].get("pcmpistri"),
          "without the compensation the emulator gives ECX 16, no CF: it is still needed (ECX %d)" % raw["regs"][1])
    got = pc("pcmpistri xmm1, xmm2, 0xc", a8, a8)
    check(got == (0, 0x801, 0, 0, 0x801), "not touched: equal ordered, the same string twice (ECX 0, CF OF) %r" % (got,))
    got = pc("pcmpistri xmm1, xmm2, 0x40", a8, b8)
    check(got[2] == 0 and got[:2] == got[3:] == SF.pcmpistri_model(bytes.fromhex(a8), bytes.fromhex(b8), 0x40),
          "not touched: equal any (0x40) on the first operands; emulator, lifted code and model agree %r" % (got,))
    check(all(SF.pcmpistri_model(bytes.fromhex(a), bytes.fromhex(b), c) == w for a, b, c, w in (
        (a8, b8, 0x0C, (9, 1)), (a16, b16, 0x0D, (7, 1)), ("41420000" * 4, "58414258" * 4, 0x0C, (1, 0x81)),
        ("3039" + "00" * 14, "61353900" * 4, 0x04, (1, 0xC1)), ("41004200" * 4, "58004100" * 4, 0x41, (7, 1)),
        ("30003900" + "00" * 12, "3500" * 7 + "7800", 0x15, (7, 0x81)))),
          "the model: the two CPU answers, a needle found at 1, a digit range, the last match of a set, the first non-digit")
    # x87 control word
    s = float_session("sf_x87", family="fp_x87cw")
    sp = difftest.RSP0
    for cw in (SF.FPCW_DEFAULT, 0x0C7F):
        o, k = both(s, state_case("fp_x87cw", "fnstcw word ptr [rsp]", fpcw=cw))
        check(word(o, sp, 2) == word(k, sp, 2) == cw, "fnstcw stores the control word the case starts with, %#06x, on both sides" % cw)
    o, k = both(s, state_case("fp_x87cw", "fldcw word ptr [rsp]", pokes=[(sp, bytes.fromhex("7f0f"))]))
    check(o["fpcw"] == k["fpcw"] == 0x0F7F, "fldcw loads it, and the two sides are compared (%#x, %#x)" % (o["fpcw"], k["fpcw"]))
    c = state_case("fp_x87cw", "fnclex")
    o, k = both(s, c)
    k2 = dict(k, fpcw=0x037F)
    check(difftest.compare(c, o, k, "insn", s.md, s.image.data) == [] and o["fpcw"] == SF.FPCW_DEFAULT
          and any("x87 control word" in x for x in difftest.compare(c, o, k2, "insn", s.md, s.image.data)),
          "fnclex changes nothing; the oracle starts with 0x027f, and a different control word is a mismatch")
    # RFLAGS
    s = float_session("sf_pushf", family="int_pushf")
    o, k = both(s, state_case("int_pushf", "pushfq", eflags=2 | 0x4C5))
    check(word(o, sp - 8, 8) == word(k, sp - 8, 8) == 0x6C7, "pushfq stores CF PF ZF SF DF, bit 1 and IF = 1 on both sides (%#x, %#x)" % (
        word(o, sp - 8, 8), word(k, sp - 8, 8)))
    c = state_case("int_pushf", "popfq", pokes=[(sp, (0xFFFFFFFF00003CD5).to_bytes(8, "little"))])
    o, k = both(s, c)
    check(o["status"] == "ok" and k["status"] == 0 and o["eflags"] == k["eflags"] & difftest.FLAG_MASK == 0xCD5
          and o["regs"][4] == k["regs"][4] == sp + 8 and o["fixes"].get("popfq") == 1,
          "popfq loads the seven flags; IOPL, a clear IF and reserved bits are ignored (the emulator's are put back: counted)")
    for bit, nm in ((0x100, "TF"), (0x4000, "NT"), (0x40000, "AC"), (0x200000, "ID")):
        c = state_case("int_pushf", "popfq", pokes=[(sp, (bit | 0x41).to_bytes(8, "little"))])
        o, k = both(s, c)
        check(o["status"] == "trap" and k["status"] == 1 and k["aux"] == c.insn_rva and k["regs"][4] == sp
              and k["eflags"] & difftest.FLAG_MASK == 0 and difftest.compare(c, o, k, "insn", s.md, s.image.data) == [],
              "popfq of a value with %s is a trap at the instruction on both sides, nothing changed" % nm)


def test_m64_operand_read_as_16():
    print("oracle compensation: the emulator loads 16 bytes for the 8-byte operand of cvtdq2pd and cvtps2pd "
          "(insn-wave review, finding 2)")
    end = difftest.ARENA_BASE + difftest.ARENA_SIZE
    for fam, text, src, want in (
            ("sse_cvtdq2p", "cvtdq2pd xmm1, qword ptr [rbx]", struct.pack("<ii", -2 ** 31, 2 ** 31 - 1),
             struct.pack("<dd", -2.0 ** 31, 2.0 ** 31 - 1)),
            ("sse_cvt_pd_ps", "cvtps2pd xmm1, qword ptr [rbx]", struct.pack("<ff", 1.5, -0.25), struct.pack("<dd", 1.5, -0.25))):
        s = float_session("m64_" + fam, family=fam)
        rva, hx = next((r, h) for r, h, t in FAMILY_VECTORS[fam] if t == text)
        mn = text.split()[0]

        def case(rbx, i=0, poke=True):
            regs = [0] * 16
            regs[3], regs[4] = rbx, difftest.RSP0
            return difftest.Case(id=i, key=rva, func_rva=rva, ranges=[[rva, rva + len(hx) // 2]], end=rva + len(hx) // 2,
                                 insn_rva=rva, mem_id=0, regs=regs, eflags=2, xmm=bytes(256), seed=1,
                                 pokes=[(rbx, src)] if poke else [])
        last = case(end - 8)
        o, c = s.run_oracle([last])[0], s.run_candidate([last])[0]
        check(o["status"] == "ok" and o["xmm"][16:32] == want and o["fixes"].get("m64") == 1 and c["status"] == 0
              and c["xmm"][16:32] == want and not difftest.compare(last, o, c, "insn", s.md, s.image.data),
              "%s with its operand in the last 8 mapped bytes: the oracle completes with the manual's value, counts it, "
              "and the lifted code agrees (oracle: %s)" % (mn, o.get("reason", o["status"])))
        twice = s.run_oracle([last, case(end - 8, 1), case(end - 16, 2), case(end - 4, 3, poke=False)])
        check(twice[1]["status"] == "ok" and twice[1]["fixes"].get("m64") == 1,
              "the page lent to the emulator is gone again: the next such case needs it as much")
        check(twice[2]["status"] == "ok" and twice[2]["xmm"][16:32] == want and not twice[2]["fixes"].get("m64"),
              "not touched: an operand with 16 mapped bytes")
        check(twice[3]["status"] == "inconclusive" and "UNMAPPED" in twice[3]["reason"],
              "not touched: an operand whose own 8 bytes run off the arena is still an invalid read")
        saved = OF.overread_operand
        OF.overread_operand = lambda ins: None
        try:
            raw = s.run_oracle([last])[0]
        finally:
            OF.overread_operand = saved
        check(raw["status"] == "inconclusive" and "UNMAPPED" in raw["reason"],
              "without the compensation the emulator reports an invalid read: the compensation is still needed")


def test_minmax_daz_correction():
    print("oracle compensation: min and max under DAZ return a zero for a selected denormal (the emulator returns the denormal)")
    fam, t = "sse_minmax_s", "minss xmm1, xmm2"
    s = float_session("fp_minmax", family=fam)

    def run(a, b, mxcsr, text=t):
        c = float_case(text, a, b, mxcsr=mxcsr, family=fam)
        o = s.run_oracle([c])[0]
        return lane0(o), o["fixes"].get("minmax", 0), lane0(s.run_candidate([c])[0])
    got = run(0x00000001, 0x7F800000, 0x1FC0)
    check(got == (0, 1, 0), "minss(denormal, inf) under DAZ: corrected to 00000000 and counted; lifted code agrees "
                            "(oracle %08x, changed %d, lifted %08x)" % got)
    got = run(0x3F800000, 0x80000001, 0x1FC0)
    check(got == (0x80000000, 1, 0x80000000), "minss(1.0, -denormal) under DAZ: 80000000 (%08x, %d, %08x)" % got)
    got = run(0xFF800000, 0x00000001, 0x1FC0, "maxss xmm1, xmm2")
    check(got == (0, 1, 0), "maxss(-inf, denormal) under DAZ: 00000000 (%08x, %d, %08x)" % got)
    saved = OF.MINMAX
    OF.MINMAX = frozenset()
    try:
        raw = s.run_oracle([float_case(t, 0x00000001, 0x7F800000, mxcsr=0x1FC0, family=fam)])[0]
    finally:
        OF.MINMAX = saved
    check(lane0(raw) == 1 and not raw["fixes"].get("minmax"),
          "without the correction the emulator gives 00000001: the correction is still needed (got %08x)" % lane0(raw))
    for a, b, mx, want, what in ((0x00000001, 0x7F800000, 0x1F80, 0x00000001, "the same operands without DAZ"),
                                 (0x3F800000, 0x40000000, 0x1FC0, 0x3F800000, "two normal numbers under DAZ"),
                                 (0x7FC00001, 0x3F800000, 0x1FC0, 0x3F800000, "a NaN and a number under DAZ (the second operand)"),
                                 (0x00800000, 0x7F800000, 0x1FC0, 0x00800000, "the smallest normal under DAZ")):
        got = run(a, b, mx)
        check(got == (want, 0, want), "not touched: %s (%08x, changed %d, lifted %08x)" % ((what,) + got))
    check(OF.MINMAX == {o + x for o in ("min", "max") for x in ("ss", "sd", "ps", "pd")},
          "it applies to min and max only")

    def compare_select(orig):
        def f(tr, flow):
            d = tr.xr(0)
            return ["float s = %s;" % tr.fval(1, 32), "ctx->xmm[%d].f[0] = ctx->xmm[%d].f[0] < s ? ctx->xmm[%d].f[0] : s;" % (d, d, d)]
        return f
    summary, recs, n = insn_test("fp_minmax_cs", {fam: [(r, x) for r, _, x in FAMILY_VECTORS[fam] if x.startswith("minss")]},
                                 24, minss=compare_select)
    daz_only = all(int(r["first_failure"]["mxcsr"], 16) & 0x40 for r in recs if r["fail"])
    check(summary["instructions_failing"] >= n - 2 and 0 < summary["fail"] < summary["pass"] and daz_only,
          "harness can fail: minss as `a < b ? a : b` fails %d of %d vectors, under DAZ only (%d of %d cases)" % (
              summary["instructions_failing"], n, summary["fail"], summary["cases"]))


def test_ftz_correction():
    print("oracle compensation: MXCSR.FTZ, a tiny result is a signed zero (the emulator ignores FTZ)")
    s = float_session("fp_ftz")
    t = "addss xmm1, xmm2"

    def run(a, b, mxcsr):
        c = float_case(t, a, b, mxcsr=mxcsr)
        o = s.run_oracle([c])[0]
        return lane0(o), o["fixes"].get("ftz", 0), lane0(s.run_candidate([c])[0])
    got = run(0x00000001, 0x00000001, 0x9F80)
    check(got == (0, 1, 0), "addss(denormal, denormal) under FTZ: corrected to 00000000 and counted; lifted code agrees "
                            "(oracle %08x, changed %d, lifted %08x)" % got)
    got = run(0x80800000, 0x00000001, 0x9F80)
    check(got == (0x80000000, 1, 0x80000000), "a tiny negative sum: 80000000 (%08x, %d, %08x)" % got)
    saved = OF.FTZ_ARITH
    OF.FTZ_ARITH = frozenset()
    try:
        raw = s.run_oracle([float_case(t, 0x00000001, 0x00000001, mxcsr=0x9F80)])[0]
    finally:
        OF.FTZ_ARITH = saved
    check(lane0(raw) == 2 and not raw["fixes"].get("ftz"),
          "without the correction the emulator gives 00000002: the correction is still needed (got %08x)" % lane0(raw))
    for a, b, mx, want, what in ((0x00000001, 0x00000001, 0x1F80, 0x00000002, "the same operands without FTZ"),
                                 (0x3F800000, 0x40000000, 0x9F80, 0x40400000, "1 + 2 under FTZ"),
                                 (0x007FFFFF, 0x00000001, 0x9F80, 0x00800000, "a sum that is the smallest normal"),
                                 (0x00000001, 0x80000001, 0x9F80, 0x00000000, "an exact zero (nothing to flush)"),
                                 (0x00000001, 0x7F800000, 0x9F80, 0x7F800000, "an infinite operand")):
        got = run(a, b, mx)
        check(got == (want, 0, want), "not touched: %s (%08x, changed %d, lifted %08x)" % ((what,) + got))
    lanes = lambda *v: b"".join(x.to_bytes(4, "little") for x in v) + bytes(16 - 4 * len(v))
    w = lambda name, a, b, mx=0x9F80: OF.ftz_wants(name, lanes(a), lanes(b), 32, 1, mx)
    check(w("mulss", 0x1B6A9CC0, 0x248BAB31) == [] and w("mulss", 0x00800000, 0x3F000000) == [(0, 0)]
          and w("mulss", 0x80800000, 0x3F000000) == [(0, 0x80000000)] and w("divss", 0x00800000, 0x40000000) == [(0, 0)]
          and w("mulss", 0x1B6A9CC0, 0x248BAB31, 0xFF80) == [(0, 0)],
          "tiny is decided after rounding: 0.99999998 x 2^-126 rounds up to 2^-126 and is kept (flushed under "
          "round-toward-zero); 2^-127 is flushed, with its sign")
    check(OF.FTZ_ARITH == {o + x for o in ("add", "sub", "mul", "div") for x in ("ss", "sd", "ps", "pd")}
          and OF.plan(next(s.md.disasm(bytes.fromhex("f30f10ca"), 0x1000))) is None,
          "it applies to add, sub, mul and div only, and only where a family declared them float")


def test_recip_is_judged_by_the_cpu_only():
    print("rcp/rsqrt: a table approximation on the CPU, the exact quotient in the emulator; judged by the real CPU only "
          "(harness/oraclefix.py, HARDWARE_ONLY)")
    fam = "sse_recip"
    reg = [(r, x) for r, _, x in FAMILY_VECTORS[fam] if x.endswith(" xmm1, xmm2")]
    check(sorted(translate.mnemonic_of_text(x) for _, x in reg) == sorted(OF.HARDWARE_ONLY),
          "the four mnemonics have a register vector (%s)" % ", ".join(x for _, x in reg))
    s = float_session("fp_recip", family=fam)
    for text, a, want in (("rcpss xmm1, xmm2", 0x3F800000, 0x3F7FF000), ("rcpss xmm1, xmm2", 0x40400000, 0x3EAAA000),
                          ("rsqrtss xmm1, xmm2", 0x40000000, 0x3F34F800)):
        c = float_case(text, 0x12345678, a, family=fam)
        got = lane0(s.run_candidate([c])[0])
        check(got == want, "%s(%08x) in lifted code is %08x, the value measured on the Intel CPU (got %08x)" % (
            text.split()[0], a, want, got))
    o = s.run_oracle([c])[0]
    check(o["status"] == "inconclusive" and o["reason"] == OF.HARDWARE_ONLY_WHAT,
          "the emulator gives no result for a case that executes one: %s" % o.get("reason", o["status"]))

    # a translation that returns the exact quotient, which is what the emulator computes, must fail
    def exact(orig):
        def f(tr, flow):
            return [ln.replace("tf_frecpe32(", "1.0f / (").replace("tf_frsqrte32(", "1.0f / __builtin_sqrtf(") for ln in orig(tr, flow)]
        return f
    summary, insns, n = insn_test("fp_recip_exact", {fam: reg}, 24, rcpss=exact, rcpps=exact, rsqrtss=exact, rsqrtps=exact)
    check(n == 4 and summary["instructions_failing"] == 4 and summary["fail"] > 0,
          "harness can fail: a translation that returns the exact quotient fails all 4 mnemonics (%d of %d cases; "
          "it passes where the operand is a zero, an infinity or a NaN)" % (summary["fail"], summary["cases"]))

    # no hardware answer: not judged, under the default MXCSR either.  An image of another name
    # gives the instructions other random states, which nobody ever asked the CPU about.
    def unasked(family, picked):
        old = os.environ.get("HWORACLE")
        os.environ["HWORACLE"] = "offline"
        try:
            summary, _, recs = difftest.instruction_test(
                insn_image(family, name="selftest_unasked"), os.path.join(OUT, "lifted_unasked_" + family),
                os.path.join(OUT, "run_unasked_" + family), states=5, quiet=True, rvas=[r for r, _ in picked])
        finally:
            os.environ.pop("HWORACLE")
            if old is not None:
                os.environ["HWORACLE"] = old
        f = Counter()
        for r in recs:
            f.update({k: v for k, v in r["float"].items() if isinstance(v, int)})
        return summary, f
    summary, f = unasked(fam, reg)
    check(summary["pass"] == 0 and summary["fail"] == 0 and summary["inconclusive"] == f["no_hardware_answer"] == 4 * 5 * len(FC.VARIANTS)
          and f["emulator_only"] == 0,
          "without a hardware answer no rcp/rsqrt case is judged, the default MXCSR included (%d cases: pass %d, "
          "fail %d, emulator only %d)" % (summary["cases"], summary["pass"], summary["fail"], f["emulator_only"]))
    summary, f = unasked(FLOAT_FAMILY, [(r, x) for r, _, x in FAMILY_VECTORS[FLOAT_FAMILY] if x == "addss xmm1, xmm2"])
    check(f["emulator_only"] == summary["pass"] == 5 and summary["fail"] == 0 and f["no_hardware_answer"] == 5 * (len(FC.VARIANTS) - 1),
          "the rule is narrow: addss without a hardware answer is still judged by the corrected emulator under the "
          "default MXCSR (%d cases pass)" % summary["pass"])


def test_mask_hazard_check():
    print("build check: no fneg/fabs in compiled lifted code (the sign-mask hazard of the guest float mode)")
    src = os.path.join(OUT, "mask_hazard.c")
    with open(src, "w") as f:
        f.write('#include "runtime.h"\n'
                "void f(Ctx *ctx) {      /* andpd xmm0, [a known mask], as the lifter writes it */\n"
                "    ctx->xmm[0].q[0] &= UINT64_C(0x7fffffffffffffff);\n"
                "    ctx->xmm[0].q[1] &= UINT64_C(0x7fffffffffffffff);\n}\n")

    def hazards(flags):
        obj = src[:-2] + ".o"
        subprocess.run(["clang", "-O2", "-I" + os.path.join(REPO, "harness"), "-c", src, "-o", obj] + flags, check=True)
        return difftest.mask_hazards(obj)
    check(hazards(difftest.LIFTED_CFLAGS) == [], "with the harness's flags at -O2 the mask is an integer constant")
    dropped = [f for f in difftest.LIFTED_CFLAGS if "vectorize" not in f]
    h = hazards(dropped)
    check(len(h) == 1 and "fneg" in h[0],
          "harness can fail: without -fno-vectorize -fno-slp-vectorize the compiler builds it with fneg, and the "
          "check reports it (%s)" % (h[0] if h else "nothing reported"))
    check({"-ffp-contract=off", "-fno-vectorize", "-fno-slp-vectorize"} <= set(difftest.LIFTED_CFLAGS),
          "lifted C is compiled with -ffp-contract=off -fno-vectorize -fno-slp-vectorize")


def test_float_translations_can_fail():
    print("harness can fail: float mutants (operands swapped, upper lanes zeroed) on the float vectors")
    picked = {FLOAT_FAMILY: [(rva, t) for rva, _, t in FAMILY_VECTORS[FLOAT_FAMILY] if t.startswith("addss xmm1")]}

    def swapped(orig):
        def f(tr, flow):
            return [ln.replace("tf_fadd32(ctx->xmm[1].f[0], s)", "tf_fadd32(s, ctx->xmm[1].f[0])") for ln in orig(tr, flow)]
        return f

    def zeroing(orig):
        def f(tr, flow):
            return orig(tr, flow) + ["ctx->xmm[%d].d[1] = 0;" % tr.xr(0)]
        return f
    summary, insns, n = insn_test("fp_swapped", picked, 24, addss=swapped)
    only_nans = all(FC.is_nan(int.from_bytes(bytes.fromhex(r["first_failure"]["xmm"])[16:20], "little"), 32)
                    for r in insns if r["fail"] and "rip" not in r["text"] and "rbx" not in r["text"])
    check(n == 3 and summary["instructions_failing"] == n and 0 < summary["fail"] < summary["pass"] and only_nans,
          "addss with its operands swapped fails all %d vectors, in the cases with two NaNs only (%d of %d cases)" % (
              n, summary["fail"], summary["cases"]))
    summary, insns, n = insn_test("fp_upper", picked, 24, addss=zeroing)
    check(summary["pass"] == 0 and summary["fail"] == summary["cases"],
          "addss that zeroes lane 1 fails every case (the upper lanes hold random bits and are compared)")


def test_float_vectors_ran_as_float(insns):
    recs = [r for r in insns if translate.mnemonic_of_text(r["text"]) in translate.FLOAT]
    names = {translate.mnemonic_of_text(r["text"]) for r in recs}
    check(names == set(translate.FLOAT) and all("float" in r for r in recs),
          "every mnemonic declared float has vectors that ran as float cases (%s)" % " ".join(sorted(names)))
    check(all(set(r["float"]["variants"]) == set(FC.VARIANT_NAMES) and len(set(r["float"]["variants"].values())) == 1
              for r in recs), "each of them under all %d MXCSR variants (%s)" % (len(FC.VARIANTS), ", ".join(FC.VARIANT_NAMES)))
    tot = Counter()
    for r in recs:
        tot.update({k: v for k, v in r["float"].items() if isinstance(v, int)})
    print("    float cases %d: expected side the real CPU %d, emulator only %d, no hardware answer %d; the emulator "
          "differs from the CPU in %d; the NaN correction changed %d" % (
              tot["cases"], tot["hardware"], tot["emulator_only"], tot["no_hardware_answer"],
              tot["emulator_differs_from_hardware"], tot["nan_correction_changed"]))
    check(tot["hardware"] + tot["emulator_only"] + tot["no_hardware_answer"] + tot["hardware_fault"] == tot["cases"],
          "and every float case says where its expected result came from")


def test_function_test_in_pieces(image, lifted):
    """tools/functest tests a large module in many processes from one prepared directory, each
    process a part of the functions, in rounds of a few functions.  The records must be exactly
    those of the whole module in one round, and nobody may write into the prepared directory."""
    print("function test in pieces: a prepared directory, parts of the functions, rounds")
    kw = dict(seeds=4, quiet=True, jobs=1)
    _, whole = difftest.function_test(image, lifted, os.path.join(OUT, "pieces_whole"), **kw)
    static = os.path.join(OUT, "pieces_static")
    difftest.function_test_prepare(image, lifted, static, seeds=4, jobs=2)

    def listing():
        return {n: (os.stat(os.path.join(static, n)).st_mtime_ns, os.path.getsize(os.path.join(static, n)))
                for n in sorted(os.listdir(static))}
    before = listing()
    rvas = [int(r["rva"], 16) for r in whole]
    parts = [set(rvas[k::3]) for k in range(3)]
    got = []
    for k, only in enumerate(parts):
        _, res = difftest.function_test(image, lifted, static, only=only, prepared=True, batch=2,
                                        work_dir=os.path.join(OUT, "pieces_work_%d" % k), **kw)
        check(len(res) == len(only), "part %d of 3: %d functions, in rounds of 2" % (k, len(res)))
        got += res
    by = {r["rva"]: r for r in got}
    differ = [r["name"] for r in whole if by.get(r["rva"]) != r]
    check(len(got) == len(whole) and not differ,
          "the %d records of the three parts are those of the whole module in one round%s" % (
              len(whole), "" if not differ else ": differ in %s" % differ[:5]))
    check(any(r["status"] == "passing" for r in whole) and any(r["status"] != "passing" for r in whole),
          "... and they are not all alike (passing and other verdicts among them)")
    check(listing() == before, "the prepared directory was only read")
    _, none = difftest.function_test(image, lifted, static, only=set(), prepared=True,
                                     work_dir=os.path.join(OUT, "pieces_work_none"), **kw)
    check(none == [], "an empty set of functions is no function, not all of them")
    # a prepared directory that is not what this session would have written is refused
    for name, what in (("mem_ptr_0.bin", "a memory file with other contents"), ("image.bin", "another image"),
                       ("driver", "no driver")):
        bad = os.path.join(OUT, "pieces_bad")
        shutil.rmtree(bad, ignore_errors=True)
        shutil.copytree(static, bad)
        if name == "driver":
            os.remove(os.path.join(bad, name))
        else:
            with open(os.path.join(bad, name), "r+b") as f:
                f.seek(0x100)
                b = f.read(1)
                f.seek(0x100)
                f.write(bytes([b[0] ^ 1]))
        try:
            difftest.function_test(image, lifted, bad, only=parts[0], prepared=True,
                                   work_dir=os.path.join(OUT, "pieces_work_bad"), **kw)
            refused = False
        except RuntimeError:
            refused = True
        check(refused, "a prepared directory with %s is refused" % what)


def stack_names_of(name, code):
    """The guest memory macro names (LD64, STS64, ...) in the C the lifter makes from the
    bytes `code` at 0x1000, a function of its own; comments are not counted."""
    im = Image.synthetic(name, 0x8000, {0x1000: code}, [(0x1000, 0x2000)], [0x1000], image_base=PREFERRED_BASE)
    out = os.path.join(OUT, "lifted_" + name)
    os.makedirs(out, exist_ok=True)
    L.lift_module(im, out, quiet=True)
    src = "".join(open(os.path.join(out, f)).read() for f in sorted(os.listdir(out)) if f.endswith(".c"))
    return set(re.findall(r"\b((?:LD|ST)S?(?:8|16|32|64|X|F32|F64)?)\(", re.sub(r"/\*.*?\*/", "", src, flags=re.S)))


def test_stack_macro_names():
    print("stack names: an access through [rsp] (no index, no segment) is named with the S macros; others are not")
    cases = [
        ("mov rax, [rsp+8]", "488b442408c3", {"LDS64"}),
        ("mov [rsp+0x10], rcx", "48894c2410c3", {"STS64"}),
        ("mov rax, [rbx]", "488b03c3", {"LD64"}),
        ("mov rax, [rsp+rcx*8]", "488b04ccc3", {"LD64"}),
        ("mov rax, [rbp-8]", "488b45f8c3", {"LD64"}),
        ("push rcx", "51c3", {"STS64"}),
        ("pop rdx", "5ac3", {"LDS64"}),
        ("pop qword ptr [rbx]", "8f03c3", {"LDS64", "ST64"}),
    ]
    for n, (text, code, want) in enumerate(cases):
        got = stack_names_of("selftest_sn%d" % n, bytes.fromhex(code))
        check(got == want, "%s lifts to %s (got %s)" % (text, sorted(want), sorted(got)))


def test_stack_plain_build(image, lifted):
    print("stack plain build: the lifted functions still pass with the stack names as plain copies (-DTF_GUEST_STACK_PLAIN)")
    flags = "-O1 -DTF_GUEST_MEM_SEQCST -DTF_GUEST_MEM_TSO -DTF_GUEST_STACK_PLAIN"
    summary, results = difftest.function_test(image, lifted, os.path.join(OUT, "run_stackplain"), seeds=4,
                                              only={LABELS[n] for n in EXPECT_PASS}, opt=flags, quiet=True)
    by = {int(r["rva"], 16): r for r in results}
    bad = [n for n in EXPECT_PASS if by.get(LABELS[n], {}).get("status") != "passing"]
    check(not bad and summary["failing"] == 0, "with %s, every expected-passing function passes (not passing: %s)" % (flags, bad))


def main():
    t0 = time.time()
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    test_vectors_match_their_assembly()
    image = program_image()
    lifted = os.path.join(OUT, "lifted")
    test_discovery_and_lift(image, lifted)
    test_relocation_kinds()
    test_stack_macro_names()
    summary, by = test_functions(image, lifted)
    test_stack_plain_build(image, lifted)
    test_coverage_and_seed_families(image, lifted, summary, by)
    test_demand_pages(image, lifted, by)
    test_limits_end_a_case_at_an_event(image, lifted, summary, by)
    test_stack_probe(image, lifted, summary, by)
    test_function_test_in_pieces(image, lifted)
    jimage = jump_image()
    jumps = test_jump_through_register(jimage)
    test_jump_tables(*jumps)
    test_jumps_judged_over_the_function(*jumps)
    test_register_writes()
    test_jump_table_bound_in_register(jimage, *jumps)
    test_jump_table_without_a_bound(jimage, *jumps)
    test_linked_table_stop(jimage)
    test_tail_call_through_table(jimage, *jumps)
    test_linked_table_tail(jimage)
    test_undecoded_code_is_reported(*jumps)
    test_families(lifted)
    iimage = insn_image()                    # the vectors that test the core, not a family
    insns = test_instructions()
    test_float_vectors_ran_as_float(insns)
    test_float_modes()
    test_nan_correction()
    test_alignment_fault(insns)
    test_m64_operand_read_as_16()
    test_minmax_daz_correction()
    test_statefix()
    test_ftz_correction()
    test_recip_is_judged_by_the_cpu_only()
    test_mask_hazard_check()
    test_float_translations_can_fail()
    test_unsupported_vectors_are_refused(iimage)
    test_name_clash_guards(iimage)
    test_wrong_translation_is_caught(image)
    test_call_record_covers_what_a_callee_reads(image)
    test_wrong_flag_is_caught()
    test_boundary_values_in_memory_operands()
    test_vector_values_are_tried()
    test_defined_faults_are_compared(image)
    test_stale_lift_is_refused(image)
    test_locked_instruction_without_atomic_stops_the_lift()
    test_lea_discovery()
    test_linked_tail_calls()
    test_linked_return_check()
    test_linked_skipped_function()
    test_linked_stops(*test_stops())
    test_stop_vectors()
    test_known_oracle_bug_is_inconclusive(iimage)
    test_ret_with_a_large_count_is_inconclusive()
    test_bit_scan_of_zero_keeps_the_register()
    test_wild_write_is_a_failed_case(image)
    print("%s: %d checks failed (%.0f s)" % ("FAILED" if FAILS else "PASSED", len(FAILS), time.time() - t0))
    for f in FAILS:
        print("  failed: " + f)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
