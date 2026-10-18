#!/usr/bin/env python3
"""make hworacle-live: the known answers of tests/known_answers.py, asked of the real CPU.

Needs the x86 machine.  Everything sent is hand-written; the sandbox is tested with instructions
that would be harmless even if it failed (getpid, getppid).  Exit status 0 only if every answer is
the expected one, the sandbox killed what it must, the time limit held, the second pass opened no
ssh connection, and nothing of ours is left running on the machine.
"""

import os
import shutil
import sys
import tempfile
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [os.path.abspath(os.path.join(HERE, "..")), HERE]

import client as C                                    # noqa: E402
import known_answers as K                             # noqa: E402

FAILS = []


def check(cond, what):
    print("  %s %s" % ("ok  " if cond else "FAIL", what))
    if not cond:
        FAILS.append(what)


def verify_all(title, sel, res):
    wrong = []
    for (name, _kind, case, exp), r in zip(sel, res):
        bad = K.verify(case, exp, r)
        if bad:
            wrong.append("%s: %s" % (name, "; ".join(bad[:3])))
    check(not wrong, "%s: %d of %d answers as expected" % (title, len(sel) - len(wrong), len(sel)))
    for w in wrong[:20]:
        print("       " + w)


def main():
    entries = K.build()
    by_kind = lambda *kinds: [e for e in entries if e[1] in kinds]            # noqa: E731
    tmp = tempfile.mkdtemp(prefix="hworacle-live-")
    try:
        print("the x86 machine")
        ora = C.Oracle(family="known", cache_root=tmp)
        st = ora.status()
        check(st["status"] == 0 and st["runner_build"] == C.RUNNER_HASH,
              "reachable; runner %s built there; its self-test passes (%s)" % (C.RUNNER_HASH, st["message"]))
        print("       %s, signature %s, microcode %s, %s" % (st["cpu"], st["cpu_signature"], st["microcode"], st["kernel"]))
        check(st["aslr_off"], "the runner runs with a fixed address-space layout")

        print("known answers")
        plain = by_kind("plain", "program", "unchecked-refused")
        t0 = time.time()
        res = ora.run([e[2] for e in plain])
        dt = time.time() - t0
        verify_all("integer, memory, faults, control transfers, SSE, steps, programs, refusals", plain, res)
        check(ora.sent == len(plain) and ora.from_cache == 0, "all %d went to the machine (%.1f s, %d ssh processes)" % (
            len(plain), dt, ora.ssh_calls))
        plain_res = res
        named = {e[0]: r for e, r in zip(plain, res)}
        # what the cache keeps: facts about the case (not the refusal of an unchecked case, which
        # depends on how the runner was started)
        kept = [(e, r) for e, r in zip(plain, res) if C.cacheable(r.outcome, r.detail)]
        check(len(kept) == len(plain) - 1, "%d of them are answers the cache keeps" % len(kept))
        r = named["rcpss(1.0)"]
        check(r.xmm[:4].hex() == "00f07f3f", "rcpss(1.0) = 3f7ff000 on %s" % r.cpu)
        r = named["read from an unmapped address"]
        check((r.signo, r.sig_code, r.sig_addr) == (11, 1, K.UNMAPPED + 0x18), "unmapped read: " + r.describe())
        r = named["divide by zero"]
        check((r.signo, r.sig_code, r.trapno) == (8, 1, 0), "divide error: " + r.describe())
        r = named["refused: syscall"]
        check(r.outcome == C.REFUSED and r.detail == 17, "syscall: " + r.describe())

        print("sandbox")
        sb = by_kind("sandbox")
        res = ora.run([e[2] for e in sb], allow_unchecked=True)
        verify_all("inspection bypassed: syscall and int 0x80 are killed by seccomp, the next case still runs", sb, res)
        res = ora.run([e[2] for e in sb if e[2].unchecked])
        check(all(r.outcome == C.REFUSED and r.detail == 34 for r in res),
              "the same cases without allow_unchecked are refused, not run")

        print("time limit")
        short = C.Oracle(family="known", cache_root=tmp, time_limit_ms=400)
        tl = by_kind("timeout")
        t0 = time.time()
        res = short.run([e[2] for e in tl])
        verify_all("endless cases end at the limit (%.1f s for %d of them at 400 ms), the next case still runs" % (
            time.time() - t0, sum(1 for e in tl if "timeout" in e[3])), tl, res)

        print("determinism")
        again = C.Oracle(family="known", cache_root=tmp)
        try:
            res2 = again.run([e[2] for e in plain], refresh=True, isolate=True)
            same = all(a.raw == b.raw for a, b in zip(plain_res, res2))
            check(same and again.sent == len(plain),
                  "asked again, one forked child per case: the same %d answers byte for byte" % len(plain))
        except Exception as e:                           # CacheConflict: a different answer than the stored one
            check(False, "asked again: %s: %s" % (type(e).__name__, e))

        print("cache")
        real_ssh = C.SSH
        C.SSH = "/nonexistent/ssh-must-not-be-started"
        try:
            cold = C.Oracle(family="known", cache_root=tmp)
            res3 = cold.run([e[2] for e, _ in kept])
            check(cold.ssh_calls == 0 and cold.sent == 0 and cold.from_cache == len(kept),
                  "second run: all %d answers from the cache, no ssh connection opened" % len(kept))
            check(all(a.raw == b.raw and b.cached for (_, a), b in zip(kept, res3)), "and they are the same answers")
            check(all(r.cpu and "Intel" in r.cpu for r in res3), "each with the CPU that gave it (%s)" % res3[0].cpu)
            res4 = cold.run([e[2] for e in tl + sb], allow_unchecked=True, offline=True)
            check(all(r.outcome == C.NO_ANSWER for e, r in zip(tl + sb, res4) if "timeout" in e[3] or "killed" in e[3]),
                  "timeouts and kills are not stored: they describe a run, not the instruction")
            off = C.Oracle(family="never-asked", cache_root=tmp, offline=True)
            res5 = off.run([e[2] for e in plain[:10]])
            check(off.ssh_calls == 0 and all(r.outcome == C.NO_ANSWER for r in res5),
                  "offline mode with an empty cache: 'no hardware answer', no ssh")
        finally:
            C.SSH = real_ssh

        print("shared cache")
        shared = C.Oracle(family="known")
        try:
            shared.run([e[2] for e, _ in kept], refresh=True)
            check(True, "the %d answers are in %s (family 'known') for `make hworacle-test`; they agree with any "
                  "stored earlier" % (len(kept), os.path.relpath(shared.cache.root, C.REPO)))
        except Exception as e:
            check(False, "storing the answers: %s: %s" % (type(e).__name__, e))

        print("afterwards")
        procs = ora.remote_processes()
        check(not procs, "nothing of ours is running on the x86 machine%s" % ("" if not procs else ": " + "; ".join(procs)))
    except C.OracleUnavailable as e:
        print("x86 machine unavailable: %s" % e)
        FAILS.append("machine unavailable")
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print("%d check(s) failed" % len(FAILS) if FAILS else "all live checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
