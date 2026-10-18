#!/usr/bin/env python3
"""The "done" check for one row of the back-end seam (tools/swarm/back_rows.py), as
check_method.py is for a method body.

    make back-check ROW=<id> [RT_BASE=<commit>] [RT_BASE_REF=<branch>]

A row hands one or two methods of the do-nothing device on to the back end (rt/d3dback.h): it
edits each method's body, runtime/nulldev/<Interface>/<Method>.c, and adds each method's test,
runtime/tests/back/<Interface>/<Method>.c, and touches nothing else.

Items (exit 0 only when none fails):
    [1] only the row's bodies and tests differ from the base, committed or not
    [2] each body hands its own entry on and no other: one of RT_BACK, RT_BACK_SAME, RT_BACK_CREATE,
        RT_BACK_ANSWER names the method's entry, none names another entry, the table is never
        called directly (`rt_d3dback()->`), and the skeleton's comment is gone; each test has
        BACK_TEST(<Interface>, <Method>)
    [3] every stop comes before the back end: the test has at least as many T_BACK_STOPS_BEFORE
        as the body has RT_UNIMPLEMENTED, plus one where the body takes an object through
        rt_nulldev_arg (the wrong class is a stop); a body with none needs none
    [4] the runtime and the tests build (warnings are errors)
    [5] each test passes with the recording back end (back_<Interface>/<Method>): its wrapper
        (rt/test_back.h) fails unless the method reached the recording back end exactly as often
        as the front accepted it
    [6] each test passes with no back end (none_<Interface>/<Method>): the same calls behave as
        before, and the seam counted nothing
    [7] the method's older test, runtime/tests/nulldev/<Interface>/<Method>.c, where there is
        one, still passes

The whole suite is not run here (the item list is meant to take under two minutes); the wave's
end runs it, and `make d3d-replay-back` is the measure over the recording.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(HERE)), "tools", "swarm"))
import check_shim as cs                                             # noqa: E402
import back_rows                                                    # noqa: E402

TITLES = {1: "only the row's bodies and tests changed", 2: "each body hands its own entry on, and each test is a BACK_TEST",
          3: "every stop is proved to come before the back end", 4: "the runtime and the tests build",
          5: "the tests pass with the recording back end, and the counts add up", 6: "the tests pass with no back end",
          7: "the methods' older tests still pass"}
MACRO = re.compile(r"\bRT_BACK(_SAME|_CREATE|_ANSWER|_ALONE)?\s*\(")


def strip_comments(text):
    return re.sub(r"/\*.*?\*/|//[^\n]*", lambda m: re.sub(r"[^\n]", " ", m.group(0)), text, flags=re.S)


def result(n, problems, notes=()):
    return cs.Result("FAIL" if problems else "PASS", TITLES[n], list(problems) + list(notes))


def macro_args(code, open_idx):
    """The top-level arguments of the macro use whose opening parenthesis is at open_idx
    (check_shim's matching_close pairs braces; a macro's arguments need parentheses)."""
    depth, args, start = 0, [], open_idx + 1
    for i in range(open_idx, len(code)):
        c = code[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                args.append(code[start:i].strip())
                return args
        elif c == "," and depth == 1:
            args.append(code[start:i].strip())
            start = i + 1
    return args


def entry_of_macro(code, m):
    """The entry a macro use names: the argument after `ctx` (after the front's result or the
    made object as well, for RT_BACK_SAME and RT_BACK_CREATE)."""
    args = macro_args(code, m.end() - 1)
    idx = 2 if (m.group(1) or "") in ("_SAME", "_CREATE") else 1
    return args[idx] if len(args) > idx else "?"


def static_items(methods):
    entries = {e for e, _ in back_rows.entries()}
    p2, p3 = [], []
    for iface, method in methods:
        body_rel, test_rel = back_rows.body_path((iface, method)), back_rows.test_path((iface, method))
        body, test = back_rows.read(body_rel), back_rows.read(test_rel)
        if body is None or test is None:
            p2 += ["%s does not exist" % rel for rel, text in ((body_rel, body), (test_rel, test)) if text is None]
            continue
        code = strip_comments(body)
        named = [(m.group(1) or "", entry_of_macro(code, m)) for m in MACRO.finditer(code)]
        if not any(e == method and k != "_ALONE" for k, e in named):
            p2.append("%s: no RT_BACK, RT_BACK_SAME, RT_BACK_CREATE or RT_BACK_ANSWER names the entry %s" % (body_rel, method))
        for k, e in named:
            if e != method:
                p2.append("%s: RT_BACK%s names the entry %s; this body hands on %s and nothing else" % (body_rel, k, e, method))
        if re.search(r"rt_d3dback\s*\(\s*\)\s*->", cs.strip_strings(code) if hasattr(cs, "strip_strings") else code) and \
                not any(k == "_ANSWER" for k, _ in named):
            p2.append("%s: the back end's table is called directly; a body uses the macros of rt/d3dback.h" % body_rel)
        if "THE SKELETON OF A BACK-END ROW" in body:
            p2.append("%s: the skeleton's comment is still there" % body_rel)
        for e in set(re.findall(r"\bRT_BACK_(\w+)\b", code)) - {"SAME", "CREATE", "ANSWER", "ALONE"}:
            if e in entries and e != method:
                p2.append("%s: names RT_BACK_%s, another method's entry" % (body_rel, e))
        tcode = strip_comments(test)
        if not re.search(r"\bBACK_TEST\s*\(\s*%s\s*,\s*%s\s*\)" % (iface, method), tcode):
            p2.append("%s: no BACK_TEST(%s, %s)" % (test_rel, iface, method))
        stops = len(re.findall(r"\bRT_UNIMPLEMENTED\s*\(", code)) + bool(re.search(r"\brt_nulldev_(?:resource_)?arg\s*\(", code))
        proved = len(re.findall(r"\bT_BACK_STOPS_BEFORE\s*\(", tcode))
        if proved < stops:
            p3.append("%s has %d stops (each RT_UNIMPLEMENTED, and one for the wrong class of object where it takes one) and "
                      "%s has %d T_BACK_STOPS_BEFORE: one for each stop of the body" % (
                body_rel, stops, test_rel, proved))
    return p2, p3


def run_tests(names):
    bad = []
    for name in names:
        run = cs.run_cmd([cs.TEST_BINARY, name], merge=True)
        if cs.failed(run):
            bad += ["%s %s failed:" % (cs.TEST_BINARY, name)] + cs.tail(run.out or "", 14)
    return bad


def main():
    ap = argparse.ArgumentParser(description="the done check for one row of the back-end seam (see the head of this file)")
    ap.add_argument("row", metavar="<row id>")
    ap.add_argument("--base", default="")
    ap.add_argument("--base-ref", default="wip/lifter-v0")
    ap.add_argument("--make", default="make")
    a = ap.parse_args()
    r = back_rows.row(a.row)
    if r is None:
        sys.exit("back-check: no row %s (tools/swarm/back_rows.py --list)" % a.row)
    methods = r[2]
    targets = [cs.Target(i, m, back_rows.body_path((i, m)), back_rows.test_path((i, m))) for i, m in methods]
    res = {1: cs.item1_changes(targets[0], a.base, a.base_ref, None, targets[1:])}
    res[1] = cs.Result(res[1].status, TITLES[1], res[1].details)
    p2, p3 = static_items(methods)
    res[2], res[3] = result(2, p2), result(3, p3)
    if not p2:
        run = cs.run_cmd([a.make, "rt-test-build"], merge=True)
        res[4] = result(4, ["'%s rt-test-build' failed:" % a.make] + cs.tail(run.out, 25) if cs.failed(run) else [])
        if res[4].status == "PASS":
            res[5] = result(5, run_tests(["back_%s/%s" % p for p in methods]))
            res[6] = result(6, run_tests(["none_%s/%s" % p for p in methods]))
            older = ["%s/%s" % p for p in methods if os.path.exists("runtime/tests/nulldev/%s/%s.c" % p)]
            res[7] = result(7, run_tests(older), [] if older else ["no older test: the method was a line of the accept list"])
    bad = 0
    for n in range(1, 8):
        x = res.get(n) or cs.Result("SKIP", TITLES[n], ["not run: an earlier item failed"])
        bad += x.status != "PASS"
        print("[%d] %-4s %s" % (n, x.status, x.title))
        for d in x.details:
            print("        " + d)
    print("back-check %s: %s" % (a.row, "%d item(s) not passed" % bad if bad else "ok"))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
