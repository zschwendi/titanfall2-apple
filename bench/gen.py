#!/usr/bin/env python3
"""Turn the tables in bench/functions/ into C for the benchmark driver (bench/README.md).

    .venv/bin/python bench/gen.py --table bench/functions/*.tbl --lifted bench/build/lifted \
        --dll-dir tests/corpus/build --native-dir bench/build/native --out bench/build/gen/cases.c
    .venv/bin/python bench/gen.py --table bench/functions/abi.tbl --files      # only check that it parses

For every described function it writes
  - the argument descriptors the driver uses to generate inputs and to place them in a Ctx
    according to the Windows x64 convention;
  - a native call with the function's real C prototype (so the compiler, not the driver,
    applies the arm64 calling convention), once as a single call and once as the timed loop.
It also writes the list of modules, with the entry RVAs the lifter skipped (read from the
<module>.report.json the lifter writes), so that the driver can tell "this function needs
code the lifter does not translate yet" from a trap inside translated code.

Table format, one function per line (# starts a comment):

    file  name  kind  result  [argument ...]  [@option ...]

  file      corpus source without extension (the module is one_<file>)
  kind      arith | mem | call | switch | virtual | atomic | probe   (how timings are grouped)
  result    void | i8 u8 i16 u16 i32 u32 i64 u64 | ptr | sNaM (struct of N bytes, alignment M) | u128
  argument  TYPE[:modifier...]
     i8 u8 i16 u16 i32 u32 i64 u64    integer.  Modifiers: lt=N (0 <= v < N), mask=M (v & M),
                                      eq=V, pick=a|b|c (three times in four one of these)
     buf=N                            pointer to N bytes owned by the case.  Modifiers:
                                      fill=bytes|str|hex|i32sorted|zero|list16|small|small32|small64
                                      (small*: every byte, 4 bytes or 8 bytes is 0..3),
                                      nocmp (contents not compared: layout differs by ABI),
                                      copyof=I (starts as a copy of argument I's buffer), mut
                                      (with copyof: one byte changed, three cases in four),
                                      alias=I+K (points K bytes into argument I's buffer),
                                      off=a|b|c (pointer is the buffer start plus one of these)
     sNaM                             struct by value, N bytes, alignment M, no floats
     u128                             unsigned __int128 by value
     cb0                              the driver's callback, i64 (*)(i32, void *)
     ...                              the arguments after this are variadic
  option    @restore (timing: restore buffers before every call), @notime (compare only)
"""

import argparse
import json
import os
import re
import sys

INTS = {"i8": (8, 1), "u8": (8, 0), "i16": (16, 1), "u16": (16, 0), "i32": (32, 1), "u32": (32, 0),
        "i64": (64, 1), "u64": (64, 0)}
CTYPE = {"i8": "int8_t", "u8": "uint8_t", "i16": "int16_t", "u16": "uint16_t", "i32": "int32_t",
         "u32": "uint32_t", "i64": "int64_t", "u64": "uint64_t"}
KINDS = ("arith", "mem", "call", "switch", "virtual", "atomic", "probe")
FILLS = {"bytes": "F_BYTES", "str": "F_STR", "hex": "F_HEX", "i32sorted": "F_I32SORTED", "zero": "F_ZERO",
         "list16": "F_LIST16", "small": "F_SMALL", "small32": "F_SMALL32", "small64": "F_SMALL64"}
STRUCT_RE = re.compile(r"^s(\d+)a(\d+)$")
# Modules whose native side is not the corpus source built for arm64.
NATIVE_NOTES = {
    "intrare": "hand-written C reference (bench/native/intrare_ref.c); the corpus source is x86 inline assembly",
}


class TableError(Exception):
    pass


def num(s):
    return int(s, 0)


def parse_struct(tok):
    m = STRUCT_RE.match(tok)
    if not m:
        return None
    size, align = int(m.group(1)), int(m.group(2))
    if align not in (1, 2, 4, 8) or size % align or size == 0:
        raise TableError("struct %s: the size must be a multiple of the alignment (1, 2, 4 or 8)" % tok)
    return size, align


def parse_arg(tok, index, variadic):
    parts = tok.split(":")
    base, mods = parts[0], parts[1:]
    a = {"kind": "A_INT", "bits": 0, "signed": 0, "dist": "D_MIXED", "p0": 0, "picks": [], "size": 0, "align": 0,
         "fill": "F_BYTES", "nocmp": 0, "mut": 0, "copyof": -1, "alias": -1, "alias_off": 0, "offs": [],
         "variadic": int(variadic), "ctype": None, "tok": tok}
    if base in INTS:
        a["bits"], a["signed"] = INTS[base]
        a["ctype"] = CTYPE[base]
        for m in mods:
            k, _, v = m.partition("=")
            if k == "lt":
                a["dist"], a["p0"] = "D_LT", num(v)
            elif k == "mask":
                a["dist"], a["p0"] = "D_MASK", num(v)
            elif k == "eq":
                a["dist"], a["p0"] = "D_EQ", num(v) & 0xFFFFFFFFFFFFFFFF
            elif k == "pick":
                a["dist"], a["picks"] = "D_PICK", [num(x) & 0xFFFFFFFFFFFFFFFF for x in v.split("|")]
            else:
                raise TableError("argument %d (%s): unknown integer modifier %r" % (index, tok, m))
    elif base.startswith("buf="):
        a["kind"], a["size"], a["ctype"] = "A_BUF", num(base[4:]), "void *"
        for m in mods:
            k, _, v = m.partition("=")
            if k == "fill":
                if v not in FILLS:
                    raise TableError("argument %d (%s): unknown fill %r" % (index, tok, v))
                a["fill"] = FILLS[v]
            elif k == "nocmp":
                a["nocmp"] = 1
            elif k == "mut":
                a["mut"] = 1
            elif k == "copyof":
                a["copyof"] = num(v)
            elif k == "alias":
                i, _, off = v.partition("+")
                a["alias"], a["alias_off"] = num(i), num(off or "0")
            elif k == "off":
                a["offs"] = [num(x) for x in v.split("|")]
            else:
                raise TableError("argument %d (%s): unknown buffer modifier %r" % (index, tok, m))
    elif parse_struct(base):
        a["kind"] = "A_STRUCT"
        a["size"], a["align"] = parse_struct(base)
        a["ctype"] = "S%da%d" % (a["size"], a["align"])
        if mods:
            raise TableError("argument %d (%s): a struct takes no modifiers" % (index, tok))
    elif base == "u128":
        a["kind"], a["size"], a["align"], a["ctype"] = "A_U128", 16, 16, "unsigned __int128"
    elif base == "cb0":
        a["kind"], a["ctype"] = "A_CB", "int64_t (*)(int32_t, void *)"
    else:
        raise TableError("argument %d: unknown type %r" % (index, tok))
    return a


def parse_table(path, funcs, seen):
    with open(path) as f:
        for ln, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            tok = line.split()
            try:
                if len(tok) < 4:
                    raise TableError("need at least: file name kind result")
                file, name, kind, res = tok[:4]
                if kind not in KINDS:
                    raise TableError("kind %r is not one of %s" % (kind, ", ".join(KINDS)))
                if (file, name) in seen:
                    raise TableError("%s %s is described twice" % (file, name))
                seen.add((file, name))
                if file + ".tbl" != os.path.basename(path):
                    raise TableError("rows for %s belong in %s.tbl" % (file, file))
                fn = {"file": file, "name": name, "kind": kind, "line": ln, "args": [], "restore": 0, "notime": 0,
                      "has_va": False}
                if res == "void":
                    fn["ret"] = ("R_VOID", 0, 0, "void")
                elif res in INTS:
                    fn["ret"] = ("R_INT", INTS[res][0], 0, CTYPE[res])
                elif res == "ptr":
                    fn["ret"] = ("R_INT", 64, 0, "void *")
                elif res == "u128":
                    fn["ret"] = ("R_U128", 0, 16, "unsigned __int128")
                elif parse_struct(res):
                    size, align = parse_struct(res)
                    fn["ret"] = ("R_STRUCT", 0, size, "S%da%d" % (size, align))
                else:
                    raise TableError("unknown result type %r" % res)
                variadic = False
                for t in tok[4:]:
                    if t == "...":
                        variadic = fn["has_va"] = True
                    elif t.startswith("@"):
                        if t == "@restore":
                            fn["restore"] = 1
                        elif t == "@notime":
                            fn["notime"] = 1
                        else:
                            raise TableError("unknown option %r" % t)
                    else:
                        fn["args"].append(parse_arg(t, len(fn["args"]), variadic))
                if len(fn["args"]) > 12:
                    raise TableError("more than 12 arguments")
                for i, a in enumerate(fn["args"]):
                    for key in ("copyof", "alias"):
                        j = a[key]
                        if j >= 0 and (j >= len(fn["args"]) or j == i or fn["args"][j]["kind"] != "A_BUF"
                                       or fn["args"][j]["alias"] >= 0):
                            raise TableError("argument %d: %s=%d must name another buffer argument" % (i, key, j))
                    if a["copyof"] >= 0 and a["copyof"] > i:
                        raise TableError("argument %d: copyof must name an earlier argument" % i)
                funcs.append(fn)
            except (TableError, ValueError) as e:
                raise SystemExit("%s:%d: %s" % (path, ln, e))


def struct_types(funcs):
    out = set()
    for fn in funcs:
        for a in fn["args"]:
            if a["kind"] == "A_STRUCT":
                out.add((a["size"], a["align"]))
        if fn["ret"][0] == "R_STRUCT":
            out.add(parse_struct(fn["ret"][3].lower()))
    return sorted(out)


def call_expr(fn):
    """(typedef of the function pointer type, list of statements that load struct
    arguments, the call expression)."""
    fixed = [a["ctype"] for a in fn["args"] if not a["variadic"]]
    proto = ", ".join(fixed) if fixed else "void"
    if fn["has_va"]:
        proto += ", ..."
    typedef = "typedef %s (*F)(%s);" % (fn["ret"][3], proto)
    pre, actual = [], []
    for i, a in enumerate(fn["args"]):
        if a["kind"] == "A_INT":
            actual.append("(%s)v[%d]" % (a["ctype"], i))
        elif a["kind"] == "A_BUF":
            actual.append("(void *)(uintptr_t)v[%d]" % i)
        elif a["kind"] == "A_CB":
            actual.append("(int64_t (*)(int32_t, void *))(uintptr_t)v[%d]" % i)
        else:
            pre.append("%s a%d; __builtin_memcpy(&a%d, (const void *)(uintptr_t)v[%d], %d);"
                       % (a["ctype"], i, i, i, a["size"]))
            actual.append("a%d" % i)
    return typedef, pre, "((F)fn)(%s)" % ", ".join(actual)


def emit_function(fn, out):
    sym = "%s_%s" % (fn["file"], fn["name"])
    for i, a in enumerate(fn["args"]):
        if a["picks"]:
            out.append("static const uint64_t picks_%s_%d[] = { %s };"
                       % (sym, i, ", ".join("UINT64_C(0x%x)" % p for p in a["picks"])))
        if a["offs"]:
            out.append("static const uint8_t offs_%s_%d[] = { %s };" % (sym, i, ", ".join(str(o) for o in a["offs"])))
    if fn["args"]:
        out.append("static const ArgDesc args_%s[] = {" % sym)
        for i, a in enumerate(fn["args"]):
            out.append("    { %s, %d, %d, %s, UINT64_C(0x%x), %s, %d, %d, %d, %s, %d, %d, %d, %d, %d, %s, %d, %d },  /* %s */" % (
                a["kind"], a["bits"], a["signed"], a["dist"], a["p0"],
                "picks_%s_%d" % (sym, i) if a["picks"] else "0", len(a["picks"]),
                a["size"], a["align"], a["fill"], a["nocmp"], a["mut"], a["copyof"], a["alias"], a["alias_off"],
                "offs_%s_%d" % (sym, i) if a["offs"] else "0", len(a["offs"]), a["variadic"], a["tok"]))
        out.append("};")
    typedef, pre, call = call_expr(fn)
    rk, rbits, rsize, rtype = fn["ret"]
    # single call
    out.append("static uint64_t call_%s(void *fn, const uint64_t *v, void *ret) {" % sym)
    out.append("    %s" % typedef)
    for p in pre:
        out.append("    %s" % p)
    if rk == "R_VOID":
        out.append("    (void)ret; %s; return 0;" % call)
    elif rk == "R_INT":
        cast = "(uint64_t)(uintptr_t)" if rtype == "void *" else "(uint64_t)(uint%d_t)" % rbits
        out.append("    (void)ret; return %s%s;" % (cast, call))
    else:
        out.append("    %s r = %s; __builtin_memcpy(ret, &r, %d); return 0;" % (rtype, call, rsize))
    out.append("}")
    # timed loop
    out.append("static uint64_t loop_%s(void *fn, const Case *cs, int n, long reps, int restore) {" % sym)
    out.append("    %s" % typedef)
    out.append("    uint64_t sum = 0;")
    out.append("    for (long r = 0; r < reps; r++) for (int k = 0; k < n; k++) {")
    out.append("        const uint64_t *v = cs[k].v;")
    out.append("        if (restore) __builtin_memcpy(cs[k].chunk, cs[k].pristine, cs[k].chunk_size);")
    for p in pre:
        out.append("        %s" % p)
    if rk == "R_VOID":
        out.append("        %s;" % call)
    elif rk == "R_INT":
        cast = "(uint64_t)(uintptr_t)" if rtype == "void *" else "(uint64_t)(uint%d_t)" % rbits
        out.append("        sum += %s%s;" % (cast, call))
    else:
        out.append("        %s res = %s; uint8_t b0; __builtin_memcpy(&b0, &res, 1); sum += b0;" % (rtype, call))
    out.append("    }")
    out.append("    return sum;")
    out.append("}")
    return ("    { \"%s\", \"%s\", \"%s\", %s, %d, %d, %d, %s, %d, %d, call_%s, loop_%s, %d }," % (
        fn["file"], fn["name"], fn["kind"], rk, rbits, rsize, len(fn["args"]),
        "args_%s" % sym if fn["args"] else "0", fn["restore"], fn["notime"], sym, sym, fn["line"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--table", required=True, nargs="+", help="one <file>.tbl per corpus source")
    ap.add_argument("--lifted", help="directory holding one_<file>/one_<file>.report.json")
    ap.add_argument("--dll-dir")
    ap.add_argument("--native-dir")
    ap.add_argument("--out")
    ap.add_argument("--files", action="store_true", help="print the corpus files the table uses and exit")
    args = ap.parse_args()
    funcs, seen = [], set()
    for path in sorted(args.table):
        parse_table(path, funcs, seen)
    files = sorted({fn["file"] for fn in funcs})
    if args.files:
        print(" ".join(files))
        return 0
    if not (args.lifted and args.dll_dir and args.native_dir and args.out):
        ap.error("--lifted, --dll-dir, --native-dir and --out are required")
    out = ["/* Generated by bench/gen.py from %s.  Do not edit. */" % ", ".join(sorted(args.table)),
           "#include \"bench.h\"", ""]
    for size, align in struct_types(funcs):
        out.append("typedef struct { uint%d_t m[%d]; } S%da%d;" % (8 * align, size // align, size, align))
    out.append("")
    rows = [emit_function(fn, out) for fn in funcs]
    out.append("")
    out.append("const FuncDesc bench_funcs[] = {")
    out.extend(rows)
    out.append("};")
    out.append("const int bench_nfuncs = %d;" % len(funcs))
    out.append("")
    mods = []
    for file in files:
        mod = "one_" + file
        with open(os.path.join(args.lifted, mod, mod + ".report.json")) as f:
            report = json.load(f)
        skipped = sorted(int(r["rva"], 16) for r in report["functions"] if not r["status"].startswith("lifted"))
        out.append("extern const BenchLiftedEntry %s_table[]; extern const size_t %s_table_count; extern uint64_t g_base_%s;"
                   % (mod, mod, mod))
        out.append("static const uint32_t skipped_%s[] = { %s };" % (mod, ", ".join("0x%x" % r for r in skipped) or "0"))
        note = NATIVE_NOTES.get(file)
        mods.append("    { \"%s\", \"%s\", \"%s\", %s_table, &%s_table_count, &g_base_%s, skipped_%s, %d, %s }," % (
            file, os.path.join(args.dll_dir, "%s_O2.dll" % mod), os.path.join(args.native_dir, "%s.dylib" % file),
            mod, mod, mod, mod, len(skipped), "\"%s\"" % note if note else "0"))
    out.append("const ModuleDesc bench_modules[] = {")
    out.extend(mods)
    out.append("};")
    out.append("const int bench_nmodules = %d;" % len(files))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    text = "\n".join(out) + "\n"
    if not os.path.exists(args.out) or open(args.out).read() != text:
        with open(args.out, "w") as f:
            f.write(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
