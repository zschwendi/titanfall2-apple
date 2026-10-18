#!/usr/bin/env python3
"""Offline mode against the shared cache: the known answers of known_answers.py, checked without
the x86 machine.  Every case `make hworacle-live` has stored must still decode to the expected
answer; cases never asked are reported as "no hardware answer" and are not failures (a fresh
checkout has an empty cache).  No ssh process is started: the test makes starting one impossible.
"""

import os
import sys
import unittest

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path[:0] = [os.path.abspath(os.path.join(HERE, "..")), HERE]

import client as C                                    # noqa: E402
import known_answers as K                             # noqa: E402


class OfflineKnownAnswers(unittest.TestCase):
    def test_known_answers_from_the_cache(self):
        entries = [e for e in K.build() if e[1] in ("plain", "program")]
        real = C.SSH
        C.SSH = "/nonexistent/ssh-must-not-be-started"
        try:
            ora = C.Oracle(family="known", offline=True)
            res = ora.run([e[2] for e in entries])
        finally:
            C.SSH = real
        self.assertEqual(ora.ssh_calls, 0)
        answered = [(e, r) for e, r in zip(entries, res) if r.outcome != C.NO_ANSWER]
        wrong = ["%s: %s" % (e[0], "; ".join(K.verify(e[2], e[3], r)[:3])) for e, r in answered if K.verify(e[2], e[3], r)]
        print("\n  %d of %d known answers are in the cache (family 'known'), %d as expected, %d never asked" % (
            len(answered), len(entries), len(answered) - len(wrong), len(entries) - len(answered)))
        self.assertEqual(wrong, [])
        for e, r in answered:
            self.assertTrue(r.cached and r.cpu, e[0])

    def test_offline_never_starts_ssh(self):
        real = C.SSH
        C.SSH = "/nonexistent/ssh-must-not-be-started"
        try:
            ora = C.Oracle(use_cache=False, offline=True)
            regs = (C.code_region(b"\x90"),)
            res = ora.run([C.Case(regs, C.CODE_ADDR)])
            self.assertEqual(res[0].outcome, C.NO_ANSWER)
            self.assertEqual(res[0].describe(), "no hardware answer")
            with self.assertRaises(C.OracleUnavailable):
                ora.status()
            self.assertEqual(ora.ssh_calls, 0)
        finally:
            C.SSH = real


if __name__ == "__main__":
    unittest.main()
