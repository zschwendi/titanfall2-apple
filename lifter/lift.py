#!/usr/bin/env python3
"""Lift one module to C (docs/DESIGN.md section 1).

    .venv/bin/python -m lifter.lift --module tier0            # game/bin/x64_retail/tier0.dll
    .venv/bin/python -m lifter.lift --dll path/to/x.dll --out lifted/x
    .venv/bin/python -m lifter.lift --module client --lint-only   # relocation census and lint, nothing written
    .venv/bin/python -m lifter.lift --module tier0 --no-patches   # without the guest patches, into build/lifted-unpatched/tier0

Writes <module>_NNNN.c, <module>.h, <module>_table.c, <module>.report.json and
<module>.shapes.json (a sample of translatable instructions per operand shape, used by the
instruction test) under lifted/<module>/.

Exit status: 0, or 3 if the relocation lint (lifter/relocs.py) found a relocated field, or a
value that looks like one, of a kind nobody has looked at.  The files are written either way.
4 if a guest patch listed for the module does not match it (lifter/guest_patches.py): nothing
is written then.
"""

import argparse
import glob
import hashlib
import json
import os
import random
import sys
import time
from collections import Counter

from .discover import Func, JCC, Program
from .image import Image, REPO, find_module
from .relocs import RelocLint, describe as describe_relocs
from .translate import GPR, Translator, Unsupported, c_includes, shape
from . import guest_patches, stops

PER_FILE = 200
SHAPE_SAMPLES = 24
_LIFTER_DIR = os.path.dirname(os.path.abspath(__file__))


def lifter_stamp(image):
    """Identifies what a lift was made from: every source file of the lifter (the core, every
    instruction family under lifter/insns/, the PE reader it uses, and the table of guest
    patches, lifter/guest_patches.toml) and the bytes of the input image.  The report carries it, and the function test refuses to run on lifted C
    whose stamp is not the current one (review finding 5: 'make difftest' after editing the
    lifter used to test the old C)."""
    h = hashlib.sha256()
    for path in sorted(glob.glob(os.path.join(_LIFTER_DIR, "*.py"))) + sorted(
            glob.glob(os.path.join(_LIFTER_DIR, "insns", "*.py"))) + [
            os.path.join(REPO, "tools", "inventory", "pe_inventory.py"), guest_patches.TABLE]:
        with open(path, "rb") as f:
            h.update(os.path.relpath(path, REPO).replace(os.sep, "/").encode() + b"\0" + f.read() + b"\0")
    h.update(("%x:%x:" % (image.image_base, image.size)).encode() + bytes(image.data))
    return h.hexdigest()


def file_digest(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def undecoded_runs(prog, fn, cfg):
    """Review finding 9: bytes inside a function's unwind ranges that recursive descent never
    reached and that are not padding - typically exception-handler code (catch blocks,
    __finally funclets), which is entered only through the scope tables in .xdata, or data
    such as a byte-index table of a switch.  Returns [(rva, length)] with padding (int3
    bytes and nop instructions) trimmed away.  Recovered jump tables are not counted."""
    if fn.ranges is None:
        return []
    image = prog.image
    covered = sorted([(a, a + i.size) for a, i in cfg.insns.items()]
                     + [(t["table"], t["table"] + t["width"] * t["entries"]) for t in cfg.tables]
                     + [(t["byte_table"][0], sum(t["byte_table"])) for t in cfg.tables if t["byte_table"]])
    runs = []
    for b, e in fn.ranges:
        pos = b
        gaps = []
        for cb, ce in covered:
            if ce <= pos or cb >= e:
                continue
            if cb > pos:
                gaps.append((pos, cb))
            pos = max(pos, ce)
        if pos < e:
            gaps.append((pos, e))
        for gb, ge in gaps:
            a = gb
            start = None                                 # start of the current non-padding run
            while a < ge:
                ins = prog.decode(a) if image.data[a] != 0xCC else None
                if image.data[a] == 0xCC or (ins is not None and ins.insn_name() == "nop" and a + ins.size <= ge):
                    if start is not None:
                        runs.append((start, a - start))
                        start = None
                    a += 1 if ins is None else ins.size
                    continue
                if start is None:
                    start = a
                a += ins.size if ins is not None and a + ins.size <= ge else 1
            if start is not None:
                runs.append((start, ge - start))
    return runs


def lift_function(prog, tr, fn, name, single=False, patches=None):
    """Returns (record, c_text or None, [(shape, rva)] of translated instructions,
    {"insns": [rva], "jcc": [rva]} of the decoded instructions and conditional branches, with
    "stops": [rva] if the function has instructions without a translation).

    An instruction the translator refuses is a stop if stops.judge says so: the function is
    lifted and the lifted code halts the run at that instruction (lifter/stops.py).  Any
    other refusal is a reason to skip the function, and so is every problem of its control
    flow.  A one-instruction function of the instruction test is never a stop.

    `patches` (a guest_patches.Plan, from lift_module only) is the one place where what is
    written is NOT a translation: lifter/guest_patches.py."""
    cfg = prog.build_cfg(fn, single)
    reasons = [{"kind": k, "what": text, "rva": "0x%x" % a} for k, a, text in cfg.problems]
    addrs = sorted(cfg.insns)
    stmts = {}
    shapes = []
    untranslated = []                # the stops: {"insn", "what", "rva", "needs"}
    for a in addrs:
        ins = cfg.insns[a]
        try:
            stmts[a] = tr.translate(ins, cfg.flow.get(a))
            shapes.append((shape(ins), a))
        except Unsupported as u:
            stop = None if single else stops.judge(ins, u, cfg.flow.get(a), cfg.fall.get(a))
            if stop is None:
                reasons.append({"kind": u.kind, "what": u.what, "rva": "0x%x" % a})
            else:
                insn, what, needs = stop
                stmts[a] = [stops.c_statement(a, what, needs)]
                untranslated.append({"insn": insn, "what": what, "rva": "0x%x" % a, "needs": needs})
    # A jump table that nothing bounds (discover._open_table_reach): the stop at its jump reports
    # the index, which the load may overwrite, so the load's statements begin by keeping it.
    open_tables = [t for t in cfg.tables if t["bound"] == "unbounded"]
    for t in open_tables:
        if t["load"] in stmts:
            stmts[t["load"]] = ["ix_%x = ctx->r[%d];" % (t["jmp"], t["index"])] + stmts[t["load"]]
    # every label must be a decoded instruction
    for t in sorted(cfg.labels):
        if t not in cfg.insns and not any(r["kind"] == "decode" for r in reasons):
            reasons.append({"kind": "decode", "what": "branch target not decoded", "rva": "0x%x" % t})
    ranges = fn.ranges if fn.ranges is not None else cfg.extents()
    where = {"insns": addrs, "jcc": [a for a in addrs if cfg.insns[a].insn_name() in JCC]}
    if untranslated:
        where["stops"] = [int(x["rva"], 16) for x in untranslated]
    if open_tables:
        # [rva of the jump, number of its register, the cases]: the lifted code stops at the jump
        # when the register holds no case's address, and the function test's oracle must too
        where["table_stops"] = [[t["jmp"], GPR[cfg.insns[t["jmp"]].operands[0].reg][1], t["targets"]] for t in open_tables]
    rec = {"rva": "0x%x" % fn.entry, "name": name, "origin": fn.origin,
           "ranges": [[b, e] for b, e in ranges], "insns": len(addrs), "cond_branches": len(where["jcc"])}
    n_reg_tail = sum(1 for a, f in cfg.flow.items() if f and f[0] == "ijmp_tail"
                     and cfg.insns[a].operands[0].type == 1)
    if n_reg_tail:
        rec["tail_jumps_through_register"] = n_reg_tail
    if cfg.tables:
        rec["jump_tables"] = [{"jmp": "0x%x" % t["jmp"], "table": "0x%x" % t["table"],
                               "entries": t["entries"], "targets": len(t["targets"]),
                               "layout": t["layout"], "bound": t["bound"]} for t in cfg.tables]
    if cfg.table_tails:
        # jumps through a table of function pointers that discovery showed to be tail calls
        # (discover._tail_through_table): where, the load of the pointer, and what showed that
        # the table is no switch table of this function
        rec["tail_calls_through_table"] = [dict({"jmp": "0x%x" % a, "load": "0x%x" % p["load"], "form": p["form"]},
                                                **{k: v for k, v in p.items() if k not in ("jmp", "load", "form")})
                                           for a, p in sorted(cfg.table_tails.items())]
    if untranslated:
        # In a lifted function each of these is a stop.  In a skipped one they are not why it
        # is skipped (the reasons are), so they are listed apart there too.
        rec["untranslated"] = _dedup(untranslated)
        rec["untranslated_insns"] = len(untranslated)
    if reasons:
        rec["status"] = "skipped"
        rec["reasons"] = _dedup(reasons)
        where.pop("stops", None)
        where.pop("table_stops", None)
        return rec, None, shapes, where
    # A function with code that was never decoded is lifted, but not all of it: say so.
    runs = [] if single else undecoded_runs(prog, fn, cfg)
    rec["status"] = "lifted-without-handlers" if runs else "lifted"
    if runs:
        rec["undecoded_bytes"] = sum(n for _, n in runs)
        rec["undecoded_runs"] = [["0x%x" % a, n] for a, n in runs[:16]]
    if patches is not None:
        applied = patches.apply(fn, cfg, stmts)      # deliberate changes of behaviour: lifter/guest_patches.py
        if applied:
            rec["guest_patches"] = applied

    labels = set(cfg.labels)
    for k, a in enumerate(addrs):
        f = cfg.fall.get(a)
        if f and f[0] == "next" and (k + 1 >= len(addrs) or addrs[k + 1] != f[1]):
            labels.add(f[1])
    if addrs[0] != fn.entry:
        labels.add(fn.entry)
    # `restrict`: within a translated function the register file is reached only through `ctx`
    # (the guest never learns its address; guest memory is reached through pointers made from
    # integers), so the compiler may keep registers and flags out of memory across guest loads
    # and stores (docs/findings/25-lifted-speed.md: 3.33 to 2.87 times native on the corpus).
    out = ["void %s(Ctx *restrict ctx) {" % name]
    for t in open_tables:
        out.append("    uint64_t ix_%x = 0;" % t["jmp"])
    if addrs[0] != fn.entry:
        out.append("    goto L_%x;" % fn.entry)
    need_end = False
    for k, a in enumerate(addrs):
        ins = cfg.insns[a]
        if a in labels:
            out.append("L_%x: ;" % a)
        out.append("    /* %x: %s %s */" % (a, ins.mnemonic, ins.op_str))
        body = stmts[a]
        if body:
            out.append("    { " + " ".join(body) + " }")
        f = cfg.fall.get(a)
        if f is None:
            if cfg.flow.get(a) is not None and cfg.flow[a][0] in ("end", "jcc_end"):
                need_end = True
            continue
        if cfg.flow.get(a) is not None and cfg.flow[a][0] == "jcc_end":
            need_end = True
        if f[0] == "next":
            if k + 1 >= len(addrs) or addrs[k + 1] != f[1]:
                out.append("    goto L_%x;" % f[1])
        elif f[0] == "tail":
            out.append("    TAIL_DIRECT(%s, 0x%x);" % (tr.fn_name(f[1]), f[1]))
        elif f[0] == "trap":
            out.append("    TRAP(0x%x);" % f[1])
        elif f[0] == "end":
            need_end = True
            out.append("    goto L_end;")
    if need_end:
        out.append("L_end: ;")
        out.append("    INSN_END();")
    out.append("}")
    return rec, "\n".join(out) + "\n", shapes, where


def is_lifted(rec):
    """'lifted' or 'lifted-without-handlers': C was emitted for the function."""
    return rec["status"].startswith("lifted")


def _dedup(reasons):
    """One entry per (kind, what), with the rva of the first and a count; the stops of a
    function ("untranslated") have no kind and go through here too."""
    seen = {}
    for r in reasons:
        key = (r.get("kind"), r["what"])
        if key in seen:
            seen[key]["count"] += 1
        else:
            seen[key] = dict(r, count=1)
    return list(seen.values())


def skip_reason(rec):
    """What the stub of a skipped function says: the first reason of its record, as the
    report has it.  Returns (text, rva of the instruction the reason is about)."""
    r = rec["reasons"][0]
    return "%s: %s" % (r["kind"], r["what"]), int(r["rva"], 16)


def _c_string(text):
    return '"%s"' % "".join(c if 32 <= ord(c) < 127 and c not in '"\\?' else "\\%03o" % (ord(c) & 0xFF) for c in text)


def _write_sources(out_dir, mod, texts, names_all, table, skipped):
    """texts: [(name, c_text)], table: [(rva, name)] of lifted functions, skipped:
    [(rva, name, why, why_rva)] of skipped ones (skip_reason).

    <mod>_table.c holds, for a really-linked build, one row per function the lifter found:
    a lifted function, or the stub of a skipped one, which stops the run with the reason
    (rt_trap_skipped).  The stub is the same function for a direct call and for a call
    through the table.  <mod>_skips lists the skipped functions with their reasons, so a
    reader can tell a stub's row from a lifted function's, and <mod>_skips_count is how many
    of the <mod>_table_count rows are stubs.  With -DHARNESS_ISOLATED there are no stubs:
    the table and its count are the lifted functions alone."""
    os.makedirs(out_dir, exist_ok=True)
    for f in os.listdir(out_dir):
        if f.startswith(mod + "_") and f.endswith(".c") or f == mod + ".h":
            os.remove(os.path.join(out_dir, f))
    # runtime.h and the C header of every instruction family that has one (translate.c_includes)
    head = ("/* Generated by lifter/lift.py from a game binary. Never commit. */\n"
            + c_includes() + "#include \"%s.h\"\n#define BASE g_base_%s\n\n" % (mod, mod))
    files = []
    for i in range(0, max(len(texts), 1), PER_FILE):
        path = os.path.join(out_dir, "%s_%04d.c" % (mod, i // PER_FILE))
        with open(path, "w") as f:
            f.write(head)
            for _, text in texts[i:i + PER_FILE]:
                f.write(text + "\n")
        files.append(path)
    with open(os.path.join(out_dir, mod + ".h"), "w") as f:
        f.write("/* Generated by lifter/lift.py. */\n#ifndef LIFTED_%s_H\n#define LIFTED_%s_H\n" % (mod, mod))
        f.write("#include \"runtime.h\"\nextern uint64_t g_base_%s;\n" % mod)
        for n in names_all:
            f.write("void %s(Ctx *ctx);\n" % n)
        f.write("#endif\n")
    path = os.path.join(out_dir, mod + "_table.c")
    skipped = sorted(skipped)
    stub_of = {rva: n for rva, n, _, _ in skipped}
    with open(path, "w") as f:
        f.write(head.replace("#define BASE g_base_%s\n" % mod, ""))
        f.write("uint64_t g_base_%s;\n\n" % mod)
        f.write("/* Sorted by rva; the last entry is a terminator.  A SKIPPED row is a function the lifter\n"
                " * skipped: its stub, below, stops the run with the reason.  The isolated build has no\n"
                " * stubs, so it has no such rows. */\n"
                "#ifdef HARNESS_ISOLATED\n#define SKIPPED(rva, fn)\n#else\n#define SKIPPED(rva, fn) {rva, fn},\n#endif\n")
        f.write("const LiftedEntry %s_table[] = {\n" % mod)
        for rva, n in sorted(table + [(rva, n) for rva, n in stub_of.items()]):
            f.write(("    SKIPPED(0x%x, %s)\n" if rva in stub_of else "    {0x%x, %s},\n") % (rva, n))
        f.write("    {0, 0}\n};\n")
        f.write("#ifdef HARNESS_ISOLATED\nconst size_t %s_table_count = %d;\n#else\n" % (mod, len(table)))
        f.write("const size_t %s_table_count = %d;      /* %d lifted + %d skipped */\n\n"
                % (mod, len(table) + len(skipped), len(table), len(skipped)))
        f.write("/* The skipped functions, sorted by rva: the first reason of the function's record in\n"
                " * %s.report.json and the rva of the instruction it is about. */\n" % mod)
        f.write("const LiftedSkip %s_skips[] = {\n" % mod)
        for rva, _, why, at in skipped:
            f.write("    {0x%x, 0x%x, %s},\n" % (rva, at, _c_string(why)))
        f.write("    {0, 0, 0}\n};\nconst size_t %s_skips_count = %d;\n" % (mod, len(skipped)))
        if skipped:
            # One small stub per function (a direct call names it) and one reporter: 16 bytes of
            # code and a 16-byte row each on arm64 (docs/handoff/lifter-skip-entries.md).
            f.write("\n__attribute__((noinline)) static void %s_skip_stop(Ctx *ctx, uint32_t i) {\n"
                    "    rt_trap_skipped(ctx, g_base_%s + %s_skips[i].rva, %s_skips[i].why, g_base_%s + %s_skips[i].at);\n}\n"
                    % ((mod,) * 6))
            for i, (_, n, _, _) in enumerate(skipped):
                f.write("void %s(Ctx *ctx) { %s_skip_stop(ctx, %d); }\n" % (n, mod, i))
        f.write("#endif\n")
    files.append(path)
    return files


def lift_module(image, out_dir, quiet=False, patches=True):
    """Lift the module and write the C, the report and the rest under out_dir.  With out_dir
    None nothing is written (--lint-only): the report is returned, without "generated".

    `patches`: True applies the guest patches the table lists for this module, False none
    (--no-patches); see guest_patches.plan.  A listed patch that does not match raises
    guest_patches.GuestPatchError before anything is written."""
    t0 = time.time()
    plan = guest_patches.plan(image, patches)
    prog = Program(image)
    prog.discover()
    tr = Translator(image, image.name)
    mod = image.name
    rng = random.Random(0x5EED)
    shapes = {}          # shape -> [count, [rva sample]]
    records, texts, table, skipped = [], [], [], []
    where = {}           # lifted function -> addresses of its instructions and conditional branches
    relocs = RelocLint(image, prog)
    for entry in sorted(prog.funcs):
        fn = prog.funcs[entry]
        name = tr.fn_name(entry)
        rec, text, shp, w = lift_function(prog, tr, fn, name, patches=plan)
        records.append(rec)
        relocs.see(prog, w["insns"])
        if text is None:
            skipped.append((entry, name) + skip_reason(rec))
        else:
            texts.append((name, text))
            table.append((entry, name))
            where[name] = w
        for s, a in shp:
            e = shapes.setdefault(s, [0, []])
            e[0] += 1
            if len(e[1]) < SHAPE_SAMPLES:           # reservoir sample, deterministic
                e[1].append(a)
            else:
                k = rng.randrange(e[0])
                if k < SHAPE_SAMPLES:
                    e[1][k] = a
    patched = plan.finish()                 # before anything is written: a patch nobody took stops the lift
    files = []
    if out_dir is not None:
        files = _write_sources(out_dir, mod, texts, [r["name"] for r in records], table, skipped)
        files.append(os.path.join(out_dir, mod + ".h"))
        # What the function test measures coverage against (review finding 1).
        files.append(os.path.join(out_dir, mod + ".cfg.json"))
        with open(files[-1], "w") as f:
            json.dump(where, f, separators=(",", ":"))

    summary = summarise(prog, records)
    summary["relocation_lint"] = relocs.result(texts)
    summary["guest_patches"] = patched
    summary["seconds"] = round(time.time() - t0, 1)
    report = {"module": mod, "image_base": "0x%x" % image.image_base, "size_of_image": image.size,
              "stamp": lifter_stamp(image),
              "summary": summary, "functions": records}
    if out_dir is not None:
        report["generated"] = {os.path.basename(p): file_digest(p) for p in sorted(files)}
        with open(os.path.join(out_dir, mod + ".report.json"), "w") as f:
            json.dump(report, f, indent=1)
        with open(os.path.join(out_dir, mod + ".shapes.json"), "w") as f:
            json.dump({s: {"count": c, "rvas": sorted(set(r))} for s, (c, r) in sorted(shapes.items())}, f, indent=1)
    if not quiet:
        print_summary(mod, summary)
    return report


def relocation_status(report):
    """Exit status of a lift: 3 if the relocation lint found anything of an unknown kind
    (lifter/relocs.py), else 0.  The C is written either way; the status says it must not be
    trusted before somebody has looked."""
    return 3 if report["summary"]["relocation_lint"]["unknown"] else 0


def summarise(prog, records):
    by_origin = Counter(r["origin"] for r in records)
    lifted = [r for r in records if is_lifted(r)]
    partial = [r for r in lifted if r["status"] == "lifted-without-handlers"]
    skipped = [r for r in records if r["status"] == "skipped"]
    by_kinds = Counter()
    blocks, sole = Counter(), Counter()
    for r in skipped:
        kinds = sorted({x["kind"] for x in r["reasons"]})
        by_kinds["+".join(kinds)] += 1
        uns = {x["what"] for x in r["reasons"] if x["kind"] == "unsupported"}
        for m in uns:
            blocks[m] += 1
        if len(r["reasons"]) == 1 and r["reasons"][0]["kind"] == "unsupported":
            sole[r["reasons"][0]["what"]] += 1
    stopped = [r for r in lifted if r.get("untranslated")]
    by_stop = {}
    for r in stopped:
        for x in r["untranslated"]:
            e = by_stop.setdefault((x["insn"], x["needs"]), [set(), 0])
            e[0].add(r["rva"])
            e[1] += x["count"]
    return {
        "unwind_entries": prog.n_pdata_entries,
        "unwind_functions": prog.n_pdata_roots,
        "fragments_merged": prog.n_fragments,
        "functions": len(records),
        "by_origin": dict(by_origin.most_common()),
        "rejected_roots": len(prog.rejected),
        # executable addresses taken by a RIP-relative lea (issue 51): see Program.lea
        "lea_targets": dict(prog.lea),
        "lea_not_functions": [["0x%x" % t, "0x%x" % a, why] for t, a, why in prog.lea_rejected],
        "lifted": len(lifted),
        "lifted_without_handlers": len(partial),
        "undecoded_bytes_in_lifted": sum(r["undecoded_bytes"] for r in partial),
        "skipped": len(skipped),
        "skipped_by_reason_kinds": dict(by_kinds.most_common()),
        # lifted functions that hold instructions without a translation, each a stop (lifter/stops.py):
        # how many functions, how many instructions, and per instruction [name, functions, instructions, needs]
        "lifted_with_stops": len(stopped),
        "stops": sum(r["untranslated_insns"] for r in stopped),
        "stops_by_instruction": [[w, len(fs), n, need] for (w, need), (fs, n) in sorted(
            by_stop.items(), key=lambda kv: (-len(kv[1][0]), kv[0]))],
        "instructions_in_lifted": sum(r["insns"] for r in lifted),
        "instructions_total": sum(r["insns"] for r in records),
        "jump_tables_recovered": sum(len(r.get("jump_tables", [])) for r in records),
        "functions_with_jump_tables": sum(1 for r in records if r.get("jump_tables")),
        "functions_with_tail_jump_through_register": sum(1 for r in lifted if r.get("tail_jumps_through_register")),
        "tail_calls_through_table": sum(len(r.get("tail_calls_through_table", ())) for r in lifted),
        "unsupported_by_functions_blocked": [[m, c, sole.get(m, 0)] for m, c in blocks.most_common()],
    }


def print_summary(mod, s):
    print("%s: %d functions (%d from %d unwind entries, %d chained fragments merged)" % (
        mod, s["functions"], s["unwind_functions"], s["unwind_entries"], s["fragments_merged"]))
    print("  by origin: %s" % ", ".join("%s %d" % kv for kv in s["by_origin"].items()))
    lea = s["lea_targets"]
    print("  addresses in executable sections taken by a RIP-relative lea: %d; %d were functions already, %d are "
          "inside an unwind-described function (not entries), %d are new functions (origin 'lea'), %d do not "
          "decode as a function start%s" % (
              lea["targets"], lea["known"], lea["inside"], lea["roots"], lea["not_functions"],
              "".join("\n    not a function: %s (taken at %s): %s" % tuple(x) for x in s["lea_not_functions"][:8])))
    print("  lifted %d (%.1f%%), skipped %d; %d of %d instructions are in lifted functions" % (
        s["lifted"], 100.0 * s["lifted"] / max(1, s["functions"]), s["skipped"],
        s["instructions_in_lifted"], s["instructions_total"]))
    print("  of the lifted, %d are 'lifted-without-handlers': %d bytes inside their unwind ranges are neither decoded "
          "nor padding" % (s["lifted_without_handlers"], s["undecoded_bytes_in_lifted"]))
    print("  skipped by reason: %s" % ", ".join("%s %d" % kv for kv in s["skipped_by_reason_kinds"].items()))
    print("  lifted with stops: %d functions hold %d instructions without a translation, where the run stops%s" % (
        s["lifted_with_stops"], s["stops"], "".join(
            "\n    %-16s %5d functions %6d instructions   %s" % (w, f, n, need or "(no feature bit known: the lifter's gap)")
            for w, f, n, need in s["stops_by_instruction"][:12])))
    print("  jump tables recovered: %d in %d functions; lifted functions with 'jmp reg' taken as a tail call: %d" % (
        s["jump_tables_recovered"], s["functions_with_jump_tables"], s["functions_with_tail_jump_through_register"]))
    if s["tail_calls_through_table"]:
        print("  jumps through a table of function pointers taken as a tail call (the stack shown to be at its "
              "entry level): %d" % s["tail_calls_through_table"])
    for line in describe_relocs(s["relocation_lint"]) + guest_patches.describe(s["guest_patches"]):
        print("  " + line)
    print("  top unsupported (functions blocked / where it is the only blocker):")
    for m, c, so in s["unsupported_by_functions_blocked"][:25]:
        print("    %-28s %5d %5d" % (m, c, so))
    print("  %.1f s" % s["seconds"])


def lift_instructions(image, rvas, out_dir, prog=None):
    """Instruction-test mode: each rva becomes a one-instruction function <mod>_i_<rva>
    ending in INSN_END().  Returns {rva: record}."""
    prog = prog or Program(image)
    tr = Translator(image, image.name)
    mod = image.name
    texts, table, recs = [], [], {}
    for rva in sorted(set(rvas)):
        ins = prog.decode(rva)
        if ins is None:
            recs[rva] = {"status": "skipped", "reasons": [{"kind": "decode", "what": "cannot decode"}]}
            continue
        fn = Func(rva, [(rva, rva + ins.size)], "insn")
        name = "%s_i_%x" % (mod, rva)
        rec, text, _, _ = lift_function(prog, tr, fn, name, single=True)
        rec["size"] = ins.size
        rec["text"] = "%s %s" % (ins.mnemonic, ins.op_str)
        rec["shape"] = shape(ins)
        recs[rva] = rec
        if text is not None:
            texts.append((name, text))
            table.append((rva, name))
    _write_sources(out_dir, mod, texts, [n for n, _ in texts], table, [])
    return recs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--module", help="module name, looked up under game/")
    ap.add_argument("--dll", help="explicit path to the DLL")
    ap.add_argument("--out", help="output directory (default lifted/<module>; with --no-patches build/lifted-unpatched/<module>)")
    ap.add_argument("--no-patches", action="store_true",
                    help="lift without the guest patches of lifter/guest_patches.toml: the game's own behaviour, for "
                         "measuring it (lifter/guest_patches.py)")
    ap.add_argument("--lint-only", action="store_true",
                    help="lift in memory, write nothing, print only the relocation census and lint")
    args = ap.parse_args()
    if not (args.module or args.dll):
        ap.error("give --module or --dll")
    path = args.dll or find_module(args.module)
    image = Image.from_pe(path, args.module)
    try:
        if args.lint_only:
            report = lift_module(image, None, quiet=True, patches=not args.no_patches)
            print("%s: %d functions, %.1f s" % (image.name, report["summary"]["functions"], report["summary"]["seconds"]))
            for line in describe_relocs(report["summary"]["relocation_lint"]):
                print("  " + line)
        else:
            # An unpatched lift never lands in lifted/<module> by default: make would take it for the patched one.
            out = args.out or (os.path.join(REPO, guest_patches.UNPATCHED_DIR, image.name) if args.no_patches
                               else os.path.join(REPO, "lifted", image.name))
            report = lift_module(image, out, patches=not args.no_patches)
            print("wrote %s" % out)
    except guest_patches.GuestPatchError as e:
        print("lifter: %s: NOT LIFTED, nothing written: %s" % (image.name, e), file=sys.stderr)
        return 4
    status = relocation_status(report)
    if status:
        print("relocation lint: something of an unknown kind (above): exit status %d" % status, file=sys.stderr)
    return status


if __name__ == "__main__":
    sys.exit(main())
