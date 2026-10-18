#!/usr/bin/env python3
"""Self-test of the guest patches (lifter/guest_patches.py, lifter/guest_patches.toml).  No game file.

    .venv/bin/python tests/guest_patches_selftest.py          (part of make test)

What must hold: a patch whose module, file hash and bytes match is applied, and the lifted C
and the report say so; the patched function still agrees with the x86 oracle wherever one
thread alone runs it (and a wrong reuse does not); a patch with wrong bytes, a wrong hash, a
wrong rva or a shape its kind does not allow stops the lift before anything is written; a
lift without patches writes, byte for byte, the C of a lift with an empty table; the tracked
table is well formed and lists the 11 push sites of finding 36; the table is part of what
identifies a lift.

Where the expectations come from.  The functions below are constructed here; what each
instruction does is the Intel SDM's MOV, CMP, LEA and Jcc, and the expected results of running
them are the oracle's (Unicorn, through harness/difftest.py), never the lifted code's.  That a
row must be refused follows from the rules written in lifter/guest_patches.py.  The 11 sites
are those of docs/findings/36-queue-race-sites.json.
"""

import glob
import hashlib
import json
import os
import shutil
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path[:0] = [REPO, os.path.join(REPO, "harness"), HERE]

import capstone                                   # noqa: E402

import difftest                                   # noqa: E402
from lifter import guest_patches as G             # noqa: E402
from lifter import lift as L                      # noqa: E402
from lifter.image import Image                    # noqa: E402

OUT = os.path.join(REPO, "harness", "out", "guest-patches-selftest")
FAILS = []
CHECKS = [0]

# cas_fn(comperand, pointer, value) has the shape the one kind of patch is for: a pointer is
# put in a stack slot, the location it points to is loaded, the pointer is reloaded from the
# slot and the location is loaded a second time; the second value decides a store and both
# values are returned added up (so that a wrong second value shows in rax).  The others are
# what a row must not match.
PROGRAMS = {
    "cas_fn": (0x1000, [
        ("4889542408", "mov qword ptr [rsp + 8], rdx"),
        ("4c8b12", "mov r10, qword ptr [rdx]"),            # 0x1005: the first load
        ("488b442408", "mov rax, qword ptr [rsp + 8]"),
        ("4c8b18", "mov r11, qword ptr [rax]"),            # 0x100d: the second load
        ("4939cb", "cmp r11, rcx"),
        ("7503", "jne 0x1018"),
        ("4c8902", "mov qword ptr [rdx], r8"),
        ("4b8d0413", "lea rax, [r11 + r10]"),              # 0x1018
        ("c3", "ret"),
    ]),
    "clobber_fn": (0x1040, [                               # the first value's register is written in between
        ("488b2e", "mov rbp, qword ptr [rsi]"),
        ("89d5", "mov ebp, edx"),
        ("488b0e", "mov rcx, qword ptr [rsi]"),            # 0x1045
        ("c3", "ret"),
    ]),
    "label_fn": (0x1060, [                                 # a branch lands on the second load, past the first
        ("4885d2", "test rdx, rdx"),
        ("7403", "je 0x1068"),
        ("488b2e", "mov rbp, qword ptr [rsi]"),            # 0x1065
        ("488b0e", "mov rcx, qword ptr [rsi]"),            # 0x1068
        ("c3", "ret"),
    ]),
    "call_fn": (0x1080, [                                  # a call between the two loads
        ("4883ec28", "sub rsp, 0x28"),
        ("488b2e", "mov rbp, qword ptr [rsi]"),            # 0x1084
        ("e834000000", "call 0x10c0"),
        ("488b0e", "mov rcx, qword ptr [rsi]"),            # 0x108c
        ("4883c428", "add rsp, 0x28"),
        ("c3", "ret"),
    ]),
    "store_fn": (0x10a0, [                                 # the second instruction is a store, the third a 32-bit load
        ("488b2e", "mov rbp, qword ptr [rsi]"),            # 0x10a0
        ("48890e", "mov qword ptr [rsi], rcx"),            # 0x10a3
        ("8b0e", "mov ecx, dword ptr [rsi]"),              # 0x10a6
        ("c3", "ret"),
    ]),
    "leaf_fn": (0x10c0, [("c3", "ret")]),
    "between_fn": (0x10d0, [                               # something that is not a load stands between the loads
        ("488b2e", "mov rbp, qword ptr [rsi]"),            # 0x10d0
        ("4889542408", "mov qword ptr [rsp + 8], rdx"),
        ("488b0e", "mov rcx, qword ptr [rsi]"),            # 0x10d8
        ("4801d0", "add rax, rdx"),
        ("488b16", "mov rdx, qword ptr [rsi]"),            # 0x10de
        ("c3", "ret"),
    ]),
}
CAS, FIRST, SECOND = 0x1000, 0x1005, 0x100d


def check(cond, what):
    CHECKS[0] += 1
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def make_image(name="gpatch"):
    chunks = {rva: b"".join(bytes.fromhex(h) for h, _ in lines) for rva, lines in PROGRAMS.values()}
    return Image.synthetic(name, 0x4000, chunks, [(0x1000, 0x2000)], sorted(rva for rva, _ in PROGRAMS.values()),
                           writable_ranges=[(0x3000, 0x4000)])


def row(image, at=SECOND, reuse=FIRST, code=None, reuse_code=None, sha=None, module=None):
    """A patch of the one kind for `image`; by default the right one for cas_fn."""
    data = bytes(image.data)
    return G.Patch("selftest-fix", "reuse-load", module or image.name, sha or G.file_sha256(image), at,
                   code if code is not None else data[at:at + 3], reuse,
                   reuse_code if reuse_code is not None else data[reuse:reuse + 3])


def c_files(d):
    """{name: bytes} of everything the lifter wrote except the report (which carries times and digests)."""
    return {os.path.basename(p): open(p, "rb").read() for p in sorted(glob.glob(os.path.join(d, "*")))
            if not p.endswith(".report.json")}


def lift(image, tag, patches):
    d = os.path.join(OUT, tag)
    return d, L.lift_module(image, d, quiet=True, patches=patches)


def refused(image, tag, patches, needle):
    """True if the lift stops with a GuestPatchError that says `needle`, having written nothing."""
    d = os.path.join(OUT, tag)
    try:
        L.lift_module(image, d, quiet=True, patches=patches)
    except G.GuestPatchError as e:
        if needle not in str(e):
            print("       (the error says: %s)" % e)
        return needle in str(e) and not os.path.exists(d)
    return False


def test_programs_decode():
    print("the test's own functions are the instructions their comments say")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    bad = []
    for name, (rva, lines) in PROGRAMS.items():
        for h, text in lines:
            ins = next(md.disasm(bytes.fromhex(h), rva), None)
            got = ("%s %s" % (ins.mnemonic, ins.op_str)).strip() if ins is not None and ins.size == len(h) // 2 else "?"
            if got != text:
                bad.append("%s 0x%x: %s decodes as %s, not %s" % (name, rva, h, got, text))
            rva += len(h) // 2
    check(not bad, "every line decodes to its assembly%s" % ("".join("; " + b for b in bad[:3])))


def test_applies(image):
    print("a patch whose module, file hash and bytes match is applied, and the C and the report say so")
    plain_dir, plain = lift(image, "plain", [])
    d, report = lift(image, "patched", [row(image)])
    a, b = c_files(plain_dir), c_files(d)
    differ = sorted(n for n in a if a[n] != b.get(n))
    check(set(a) == set(b) and differ == ["gpatch_0000.c"], "only the C file that holds the function differs (%s)" % ", ".join(differ))
    la, lb = a["gpatch_0000.c"].decode().splitlines(), b["gpatch_0000.c"].decode().splitlines()
    lines = [i for i in range(min(len(la), len(lb))) if la[i] != lb[i]]
    check(len(la) == len(lb) and len(lines) == 1, "exactly one line of it differs")
    i = lines[0] if lines else 0
    check(lb[i - 1].strip() == "/* %x: mov r11, qword ptr [rax] */" % SECOND and la[i - 1] == lb[i - 1],
          "it is the line after the comment of the patched instruction, and the comment still shows the game's instruction")
    check(lb[i].strip().startswith("{ /* GUEST PATCH selftest-fix (lifter/guest_patches.toml): takes the value loaded at 0x%x" % FIRST)
          and lb[i].strip().endswith("*/ W64(11, R64(10)); }"), "the line begins with GUEST PATCH, names the fix and the first load, and copies r10 to r11")
    check("LD64" in la[i] and "LD64" not in lb[i], "without the patch the line is a load from memory; with it, it reads no memory")
    rec = [r for r in report["functions"] if int(r["rva"], 16) == CAS][0]
    want = [{"fix": "selftest-fix", "kind": "reuse-load", "at": "0x%x" % SECOND, "reuse": "0x%x" % FIRST, "function": "0x%x" % CAS}]
    check(rec.get("guest_patches") == want, "the function's record in the report lists the patch")
    s = report["summary"]["guest_patches"]
    check(s["enabled"] is True and s["applied"] == want, "the report's summary lists it")
    check(not any("guest_patches" in r for r in report["functions"] if int(r["rva"], 16) != CAS)
          and plain["summary"]["guest_patches"]["applied"] == [], "no other function, and no lift without rows, claims a patch")
    check(any("1 applied: selftest-fix at 0x%x" % SECOND in line for line in G.describe(s)) and G.describe(plain["summary"]["guest_patches"]) == [],
          "the printed summary names it, and says nothing for a module without patches")
    return plain_dir, d


def test_oracle(image, patched_dir):
    print("run by one thread the patched function agrees with the x86 oracle; a reuse of the wrong register does not")
    _, results = difftest.function_test(image, patched_dir, os.path.join(OUT, "run_patched"), seeds=8, only={CAS}, quiet=True)
    r = results[0]
    check(r["status"] == "passing" and r["agree"] >= 6 and r["agree"] == r["conclusive"],
          "cas_fn with the patch: passing (%d of %d seeds conclusive, %d agreeing)" % (r.get("conclusive", 0), r.get("seeds", 0), r.get("agree", 0)))
    real = G.reg_rd
    G.reg_rd = lambda reg: "R64(3)"                 # as if the patch took rbx for the first load's register
    try:
        d, _ = lift(image, "mutant", [row(image)])
    finally:
        G.reg_rd = real
    _, results = difftest.function_test(image, d, os.path.join(OUT, "run_mutant"), seeds=8, only={CAS}, quiet=True)
    check(results[0]["status"] == "failing", "cas_fn with a patch that copies another register: failing (%s)" % results[0]["status"])


def test_refusals(image):
    print("a listed patch that does not match stops the lift, and nothing is written")
    data = bytes(image.data)
    check(refused(image, "r_bytes", [row(image, code=bytes.fromhex("4c8b19"))], "the bytes at 0x%x (at) are 4c 8b 18, the table expects 4c 8b 19" % SECOND),
          "wrong bytes at the patched instruction")
    check(refused(image, "r_rbytes", [row(image, reuse_code=bytes.fromhex("4c8b13"))], "the bytes at 0x%x (reuse) are" % FIRST),
          "wrong bytes at the first load")
    other = make_image()
    other.data[0x3000] ^= 0xFF                      # the same code in a file that is not the one reviewed
    check(refused(other, "r_hash", [row(image)], "the file being lifted has %s" % G.file_sha256(other)),
          "wrong hash: the same module name and the same code, another file")
    check(refused(image, "r_hash2", [row(image, sha="0" * 64)], "was written for a gpatch whose file has SHA-256 " + "0" * 64),
          "wrong hash: a row for a file that is not this one")
    check(refused(image, "r_mid", [row(image, at=SECOND + 1)], "0x%x is not an instruction of any function" % (SECOND + 1)),
          "wrong rva: the middle of an instruction")
    check(refused(image, "r_data", [row(image, at=0x3000)], "0x3000 is not an instruction of any function"),
          "wrong rva: not code")
    check(refused(image, "r_reuse", [row(image, reuse=0x1040)], "0x1040 (reuse) is not an instruction of the function that holds"),
          "wrong rva: the first load named is in another function")
    check(refused(image, "r_order", [row(image, at=FIRST, reuse=SECOND)], "must come before the patched one"),
          "wrong rva: the two loads the other way round")
    check(refused(image, "r_clobber", [row(image, at=0x1045, reuse=0x1040)], "writes the register that holds the first load's value"),
          "the register of the first value is written between the loads (through its 32-bit name)")
    check(refused(image, "r_label", [row(image, at=0x1068, reuse=0x1065)], "a branch lands at 0x1068"),
          "a branch lands between the loads")
    check(refused(image, "r_call", [row(image, at=0x108c, reuse=0x1084)], "is not a straight line"),
          "a call between the loads")
    check(refused(image, "r_store", [row(image, at=0x10a3, reuse=0x10a0)], "not a plain 64-bit load"),
          "the patched instruction is a store")
    check(refused(image, "r_between", [row(image, at=0x10d8, reuse=0x10d0)], "only other plain 64-bit loads may"),
          "a store stands between the loads")
    check(refused(image, "r_between2", [row(image, at=0x10de, reuse=0x10d8)], "only other plain 64-bit loads may"),
          "an addition of registers stands between the loads")
    check(refused(image, "r_narrow", [row(image, at=0x10a6, reuse=0x10a0, code=data[0x10a6:0x10a8])], "not a plain 64-bit load"),
          "the patched instruction is a 32-bit load")
    check(refused(image, "r_two", [row(image), row(image, at=0x3000)], "0x3000 is not an instruction"),
          "one row that matches and one that does not: nothing is written")
    d, report = lift(image, "elsewhere", [row(image, module="another_module", sha="0" * 64)])
    check(report["summary"]["guest_patches"]["applied"] == [], "a row for another module is not this lift's business")
    return d


def test_no_patches(image, plain_dir, elsewhere_dir):
    print("a lift without patches writes the C of a lift with an empty table, whatever the table lists")
    bad_table = [row(image, sha="0" * 64), row(image, at=0x3000)]         # every row wrong: with patches on, a refusal
    d, report = lift(image, "off", False)
    plain = c_files(plain_dir)
    check(c_files(d) == plain and len(plain) >= 5, "patches off: every generated file is byte-identical to the empty table's (%d files)" % len(plain))
    check(c_files(elsewhere_dir) == plain, "a table with rows for other modules only: the same files again")
    s = report["summary"]["guest_patches"]
    check(s["enabled"] is False and s["applied"] == [] and "NOT APPLIED" in " ".join(G.describe(s)), "its report and summary say the patches were off")
    real = G.load_table
    G.load_table = lambda path=None: bad_table      # the table file as it would be with those rows
    try:
        check(refused(image, "off_bad_on", True, "was written for a gpatch whose file"), "with such a table the default lift stops")
        d2, _ = lift(image, "off_bad", False)
        check(c_files(d2) == plain, "and the lift without patches does not read the table at all")
    finally:
        G.load_table = real


def write_table(name, text):
    path = os.path.join(OUT, name)
    with open(path, "w") as f:
        f.write(text)
    return path


def test_table():
    print("the tracked table is well formed and lists the 11 push sites of finding 36; a malformed table is refused")
    table = G.load_table()
    got = {(p.module, p.reuse, p.at) for p in table if p.fix == "queue-push-one-read"}
    finding = os.path.join(REPO, "docs", "findings", "36-queue-race-sites.json")
    if os.path.exists(finding):
        with open(finding) as f:
            sites = [s for s in json.load(f)["certain_sites"] if s["form"] == "push"]
        want = {(s["site"].split("+")[0], int(s["read1"], 16), int(s["read2"], 16)) for s in sites}
        check(len(want) == 11 and got == want, "its rows are finding 36's 11 push sites: module, first read, second read (%d rows)" % len(got))
    else:
        # The published tree leaves finding 36's table out (addresses in the game's DLLs): count only.
        check(len(got) == 11, "its rows are 11 push sites (finding 36's table is not in this tree; %d rows)" % len(got))
    check(all(p.kind == "reuse-load" and len(p.sha256) == 64 and p.what and p.why and p.ref for p in table),
          "every row has a kind, a file hash, and its fix says what, why and where from")
    by_module = {}
    for p in table:
        by_module.setdefault(p.module, set()).add(p.sha256)
    check(all(len(v) == 1 for v in by_module.values()), "one file hash per module")

    good = open(G.TABLE).read()
    first = good.index("\n[[patch]]\n")
    one = good[good.index("\n[fix.queue-push-one-read]\n"):good.index("\n[[patch]]\n", first + 1)]

    def bad(name, text, needle):
        try:
            G.load_table(write_table(name, text))
        except G.GuestPatchError as e:
            return needle in str(e)
        return False
    try:
        ok = len(G.load_table(write_table("one.toml", one))) == 1
    except G.GuestPatchError as e:
        ok = False
        print("       (%s)" % e)
    check(ok, "the first fix and its first row, alone, are a table")
    check(bad("t1.toml", one.replace("reuse_bytes =", "reuse_byte ="), "must have exactly"), "a misspelt field in a row")
    check(bad("t2.toml", one.replace('at = 0x', 'at = "0x'  ).replace("\nbytes", '"\nbytes'), "at must be an rva"), "an rva given as a string")
    check(bad("t3.toml", one.replace('fix = "queue-push-one-read"', 'fix = "something-else"'), "there is no [fix.something-else]"), "a row whose fix is not there")
    check(bad("t4.toml", one.replace('kind = "reuse-load"', 'kind = "replace-bytes"'), "the kinds are: reuse-load"), "a kind that does not exist")
    check(bad("t5.toml", one + one[one.index("\n[[patch]]\n"):], "a second patch at"), "two rows for one instruction")
    check(bad("t6.toml", one.replace('sha256 = "', 'sha256 = "XX'), "sha256 must be 64"), "a hash that is not one")
    check(bad("t7.toml", one.replace("why = ", "wherefore = "), "must have kind, what, why and ref"), "a fix that does not say why")
    check(bad("t8.toml", one.replace('bytes = "48 8b 08"', 'bytes = "mov"'), "is not a string of hex bytes"), "bytes that are not hex")
    check(bad("t9.toml", one + "\n[options]\nforce = true\n", "unknown top-level entries: options"), "an entry the table does not have")


def test_stamp(image):
    print("the table is part of what identifies a lift")
    before = L.lifter_stamp(image)
    real = G.TABLE
    copy = write_table("stamp.toml", open(real).read())
    try:
        G.TABLE = copy
        same = L.lifter_stamp(image)
        with open(copy, "a") as f:
            f.write("\n# one more line\n")
        changed = L.lifter_stamp(image)
    finally:
        G.TABLE = real
    check(before != changed and same != changed and L.lifter_stamp(image) == before,
          "lifter_stamp changes when the table's bytes change, and with nothing else")


def test_command_line():
    print("the lifter's command line: status 4 and nothing written for a patch that does not match; --no-patches lifts all the same")
    dll = os.path.join(REPO, "tests", "corpus", "build", "corpus_O1.dll")
    if not os.path.exists(dll):
        print("  SKIPPED: no %s (tests/corpus/build.sh makes it)" % os.path.relpath(dll, REPO))
        return
    image = Image.from_pe(dll, "corpus_O1")
    with open(dll, "rb") as f:
        check(image.file_sha256 == hashlib.sha256(f.read()).hexdigest(), "an image read from a file carries the SHA-256 of the file")
    real_table, real_argv = G.load_table, sys.argv
    G.load_table = lambda path=None: [G.Patch("selftest-fix", "reuse-load", "corpus_O1", "0" * 64, 0x1000, b"\x90", 0x1001, b"\x90")]
    try:
        out = os.path.join(OUT, "cmd_on")
        sys.argv = ["lift.py", "--dll", dll, "--module", "corpus_O1", "--out", out]
        saved, sys.stderr = sys.stderr, open(os.path.join(OUT, "cmd_on.stderr"), "w")
        try:
            status = L.main()
        finally:
            sys.stderr.close()
            sys.stderr = saved
        with open(os.path.join(OUT, "cmd_on.stderr")) as f:
            said = f.read()
        check("NOT LIFTED, nothing written" in said and "the file being lifted has %s" % image.file_sha256 in said,
              "it says on stderr that nothing was lifted, and why")
        check(status == 4 and not os.path.exists(out), "a row for another file of that module: exit status 4, no output directory (status %s)" % status)
        out = os.path.join(OUT, "cmd_off")
        sys.argv = ["lift.py", "--dll", dll, "--module", "corpus_O1", "--out", out, "--no-patches"]
        saved = sys.stdout
        sys.stdout = open(os.devnull, "w")
        try:
            status = L.main()
        finally:
            sys.stdout.close()
            sys.stdout = saved
        with open(os.path.join(out, "corpus_O1.report.json")) as f:
            s = json.load(f)["summary"]["guest_patches"]
        check(status in (0, 3) and s["enabled"] is False, "--no-patches with the same table: lifted, and the report says patches were off")
    finally:
        G.load_table, sys.argv = real_table, real_argv


def main():
    t0 = time.time()
    shutil.rmtree(OUT, ignore_errors=True)
    os.makedirs(OUT)
    image = make_image()
    test_programs_decode()
    plain_dir, patched_dir = test_applies(image)
    test_oracle(image, patched_dir)
    elsewhere_dir = test_refusals(image)
    test_no_patches(image, plain_dir, elsewhere_dir)
    test_table()
    test_stamp(image)
    test_command_line()
    print("guest patches selftest: %s: %d of %d checks failed (%.0f s)" % ("FAILED" if FAILS else "PASSED", len(FAILS), CHECKS[0], time.time() - t0))
    for f in FAILS:
        print("  failed: " + f)
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
