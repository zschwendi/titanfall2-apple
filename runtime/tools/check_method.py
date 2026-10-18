#!/usr/bin/env python3
"""The "done" check for one method body of the do-nothing device, as check_shim.py is for a shim.

    make method-check METHOD=<Interface>/<Method> [METHOD_FAMILY=<Interface>/<Method>,...]
                      [RT_BASE=<commit>] [RT_BASE_REF=<branch>]

A method task adds two files and edits nothing else (docs/handoff/d3d-interface-layer.md,
section 2; docs/handoff/d3d-contract.md, section 6):

    runtime/nulldev/<Interface>/<Method>.c           the body
    runtime/tests/nulldev/<Interface>/<Method>.c     its test

Items (exit 0 only when none fails):
    [1] only those two files (and the family's) differ from the base, committed or not
    [2] the body is the body of a described method: one COM_METHOD(<Interface>, ..., <Method>, ...)
        line; the four tags RT-STATUS (full or partial), RT-DOC (a learn.microsoft.com address),
        RT-CASES, RT-TRAPS; the test has TEST(<Interface>, <Method>)
    [3] lint: the body includes only rt/nulldev.h, rt/window.h and the C headers listed below,
        allocates nothing itself and calls no other body
    [4] the runtime and the tests build (warnings are errors; the generator compares the
        COM_METHOD line with the description there)
    [5] the method's test passes
    [6] every RT-CASES entry has a T_CASE of the same text that ran and checked something, and
        the test has no other case
    [7] the tags are true: status full has no RT_UNIMPLEMENTED and RT-TRAPS none; status partial
        lists in RT-TRAPS a piece of every RT_UNIMPLEMENTED message of the body, and every
        RT-TRAPS entry is a piece of the text of a T_TRAPS of the test, so each listed stop is
        reached in a test (a stop made by a helper of rt/nulldev.h counts: only the test proves it)
    [8] the whole suite passes
"""

import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_shim as cs                                             # noqa: E402

ARG = re.compile(r"([A-Za-z_]\w*)/([A-Za-z_]\w*)\Z")
ALLOWED_INCLUDES = {"rt/nulldev.h", "rt/window.h", "string.h", "stdint.h", "stddef.h", "stdbool.h"}
STRING = r'"(?:\\.|[^"\\\n])*"'
TITLES = {1: "only the body and its test changed", 2: "names, the COM_METHOD line and the four tags",
          3: "lint", 4: "the runtime and the tests build", 5: "the method's test passes",
          6: "the test covers the declared cases", 7: "the tags are true and every listed stop is reached",
          8: "the whole suite passes"}


def target(arg):
    m = ARG.match(arg)
    if not m:
        return None
    i, n = m.groups()
    return cs.Target(i, n, "runtime/nulldev/%s/%s.c" % (i, n), "runtime/tests/nulldev/%s/%s.c" % (i, n))


def strip_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)


def literal(seq):
    """The text of adjacent C string literals."""
    return cs.norm("".join(re.findall(r'"((?:\\.|[^"\\\n])*)"', seq)).replace('\\"', '"').replace("\\\\", "\\"))


def tags(body):
    """{'STATUS': ..., 'DOC': ..., 'CASES': [...], 'TRAPS': [...]} from the head comment."""
    head = re.match(r"\s*/\*(.*?)\*/", body, re.S)
    out = {}
    if not head:
        return out
    cur = None
    for line in head.group(1).split("\n"):
        line = re.sub(r"^\s*\*?\s?", "", line).rstrip()
        m = re.match(r"RT-(STATUS|DOC|CASES|TRAPS):\s*(.*)", line)
        if m:
            cur = m.group(1)
            out[cur] = m.group(2)
        elif cur and line.strip():
            out[cur] += " " + line.strip()
    for key in ("CASES", "TRAPS"):
        if key in out:
            out[key] = [cs.norm(e) for e in out[key].split(";") if e.strip()]
    return out


def described(iface, method):
    """True if the description of `iface`, or of an interface that derives from it, has the method."""
    desc = os.path.join("runtime", "com", "desc")
    for name in sorted(os.listdir(desc)):
        text = open(os.path.join(desc, name)).read()
        if name != iface + ".idef" and not re.search(r"^inherits\s+%s\s" % re.escape(iface), text, re.M):
            continue
        if re.search(r"^\s*\d+\s+\S+\s+.*\b%s\(" % re.escape(method), text, re.M):
            return True
    return False


def result(n, problems, notes=()):
    return cs.Result("FAIL" if problems else "PASS", TITLES[n], list(problems) + list(notes))


def static_items(t, body, test):
    code, tcode = strip_comments(body), strip_comments(test)
    tg = tags(body)
    p2 = []
    if not described(t.lib, t.stem):
        p2.append("no description under runtime/com/desc has a method %s of %s (or of an interface derived from it)" % (t.stem, t.lib))
    lines = re.findall(r"^COM_METHOD\((\w+),\s*[^,]+,\s*(\w+)\b.*\)\s*$", code, re.M)
    if lines != [(t.lib, t.stem)]:
        p2.append("%s must have exactly one line COM_METHOD(%s, <return type>, %s, ...) in column 1; found %s" % (
            t.shim_path, t.lib, t.stem, lines or "none"))
    for key in ("STATUS", "DOC", "CASES", "TRAPS"):
        if key not in tg:
            p2.append("%s: the head comment has no RT-%s tag" % (t.shim_path, key))
    if tg.get("STATUS") not in (None, "full", "partial"):
        p2.append("RT-STATUS is '%s': full or partial (a method with no behaviour needs no file)" % tg["STATUS"])
    if "DOC" in tg and not tg["DOC"].startswith("https://learn.microsoft.com/"):
        p2.append("RT-DOC must be the method's page on https://learn.microsoft.com/")
    if "CASES" in tg and not tg["CASES"]:
        p2.append("RT-CASES is empty")
    if not re.search(r"^TEST\(\s*%s\s*,\s*%s\s*\)" % (t.lib, t.stem), tcode, re.M):
        p2.append("%s has no TEST(%s, %s)" % (t.test_path, t.lib, t.stem))
    p3 = []
    for m in re.finditer(r'^[ \t]*#[ \t]*include[ \t]*["<]([^">\n]*)[">]', code, re.M):
        if m.group(1) not in ALLOWED_INCLUDES:
            p3.append("%s includes %s; a body includes only %s" % (t.shim_path, m.group(1), ", ".join(sorted(ALLOWED_INCLUDES))))
    for word in ("malloc", "calloc", "realloc", "free", "rt_galloc", "printf", "fprintf"):
        if re.search(r"\b%s\s*\(" % word, code):
            p3.append("%s calls %s: a body allocates through the object model (rt/nulldev.h) and prints nothing" % (t.shim_path, word))
    if re.search(r"\bimpl_\w+\s*\(", code):
        p3.append("%s calls another method's body (impl_...): a body calls only rt/nulldev.h and rt/com.h" % t.shim_path)
    p7 = []
    stops = []
    for m in re.finditer(r"\bRT_UNIMPLEMENTED\s*\(\s*ctx\s*,\s*((?:%s\s*)*)" % STRING, code):
        if not m.group(1):
            p7.append("%s: the message of an RT_UNIMPLEMENTED is not a string literal" % t.shim_path)
        else:
            stops.append(literal(m.group(1)))
    traps = [e for e in tg.get("TRAPS", []) if e.lower() != "none"]
    proved = [literal(m.group(1)) for m in re.finditer(r"\bT_TRAPS\s*\(.*?,\s*((?:%s\s*)+)\)\s*;" % STRING, tcode, re.S)]
    if tg.get("STATUS") == "full":
        if traps:
            p7.append("RT-STATUS is full but RT-TRAPS lists %s: status partial, or RT-TRAPS: none" % "; ".join(traps))
        if stops:
            p7.append("RT-STATUS is full but the body calls RT_UNIMPLEMENTED: status partial, with RT-TRAPS")
    elif tg.get("STATUS") == "partial":
        if not traps:
            p7.append("RT-STATUS is partial but RT-TRAPS lists nothing")
        for msg in stops:
            if not any(e.lower() in msg.lower() for e in traps):
                p7.append("RT_UNIMPLEMENTED(\"%s\") is not listed in RT-TRAPS (an entry is a piece of the message)" % msg)
        for e in traps:
            if not any(e.lower() in text.lower() for text in proved):
                p7.append("RT-TRAPS entry '%s' is reached in no test: add T_TRAPS(<the call>, \"... %s ...\")" % (e, e))
    cases = cs.test_case_texts(test)
    p6 = []
    for e in tg.get("CASES", []):
        if e not in cases:
            p6.append("RT-CASES entry '%s' has no T_CASE(\"%s\") in the test (the text must be the same)" % (e, e))
    for c in cases:
        if c not in tg.get("CASES", []):
            p6.append("the test has a case the body does not list: T_CASE(\"%s\")" % c)
    return tg, p2, p3, p6, p7


def selftest():
    """check_method.py --selftest.  This tool reads the status of what it runs only through
    check_shim's failed(): a program that ended in a fatal report has failed whichever way the
    report ended, by SIGABRT (-6) or quietly with exit status 134 (rt/crash.h, THE QUIET END)."""
    bad = [rc for rc in (-6, cs.QUIET_ABORT_STATUS, 1, 2) if not cs.failed(cs.Run(rc, "", "", None))]
    bad += [rc for rc in (0,) if cs.failed(cs.Run(rc, "", "", None))]
    if cs.QUIET_ABORT_STATUS != 134 or not cs.aborted(134) or not cs.aborted(-6) or cs.aborted(1):
        bad.append("aborted()")
    print("check_method selftest: %s" % ("FAILED for %r" % bad if bad else "ok"))
    return 1 if bad else 0


def main():
    if sys.argv[1:] == ["--selftest"]:
        sys.exit(selftest() or cs.selftest())
    ap = argparse.ArgumentParser(description="the done check for one method body (see the head of this file)")
    ap.add_argument("method", metavar="<Interface>/<Method>")
    ap.add_argument("--base", default="")
    ap.add_argument("--base-ref", default="wip/lifter-v0")
    ap.add_argument("--family", default="")
    ap.add_argument("--make", default="make")
    a = ap.parse_args()
    t = target(a.method)
    family = [target(f) for f in a.family.split(",") if f]
    if t is None or None in family:
        sys.exit("usage: make method-check METHOD=<Interface>/<Method> [METHOD_FAMILY=<Interface>/<Method>,...]")
    res = {1: cs.item1_changes(t, a.base, a.base_ref, None, family)}
    res[1] = cs.Result(res[1].status, TITLES[1], res[1].details)
    missing = [p for p in (t.shim_path, t.test_path) if not os.path.isfile(p)]
    if missing:
        res[2] = result(2, ["%s does not exist" % p for p in missing])
    else:
        body, test = open(t.shim_path).read(), open(t.test_path).read()
        tg, p2, p3, p6, p7 = static_items(t, body, test)
        res[2], res[3], res[7] = result(2, p2), result(3, p3), result(7, p7)
        run = cs.run_cmd([a.make, "rt-test-build"], merge=True)
        res[4] = result(4, ["'%s rt-test-build' failed:" % a.make] + cs.tail(run.out, 25) if cs.failed(run) else [])
        if res[4].status == "PASS":
            sid = "%s/%s" % (t.lib, t.stem)
            run = cs.run_cmd([cs.TEST_BINARY, "--report", sid])
            rep = cs.parse_report(run.out or "")
            ok = rep.result is not None and rep.result[0] == "pass" and not cs.failed(run)
            res[5] = result(5, [] if ok else ["%s --report %s failed:" % (cs.TEST_BINARY, sid)] + cs.tail((run.out or "") + (run.err or ""), 25))
            ran = {}
            for checks, _fails, text in rep.cases:
                ran[text] = ran.get(text, 0) + checks
            for e in tg.get("CASES", []):
                if ok and ran.get(e, 0) < 1:
                    p6.append("RT-CASES entry '%s' did not run or made no check" % e)
            res[6] = result(6, p6)
            run = cs.run_cmd([cs.TEST_BINARY], merge=True)
            res[8] = result(8, ["%s failed:" % cs.TEST_BINARY] + cs.tail(run.out, 12) if cs.failed(run) else [])
        else:
            res[6] = result(6, p6, [] if p6 else ["static half only: the build failed"])
    bad = 0
    for n in range(1, 9):
        r = res.get(n) or cs.Result("SKIP", TITLES[n], ["not run: an earlier item failed"])
        bad += r.status != "PASS"
        print("[%d] %-4s %s" % (n, r.status, r.title))
        for d in r.details:
            print("        " + d)
    print("method-check %s: %s" % (a.method, "%d item(s) not passed" % bad if bad else "ok"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
