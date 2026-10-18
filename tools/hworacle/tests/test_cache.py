#!/usr/bin/env python3
"""Tests for tools/hworacle/cache.py (standard library only).

    cd <checkout> && .venv/bin/python tools/hworacle/tests/test_cache.py

All data lives in tempfile.mkdtemp() roots, never in inventory/hworacle_cache/.  The concurrency
tests start real processes with subprocess running this same file:

    test_cache.py --writer ROOT FAMILY SEED N [fresh]
"""

import hashlib
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

sys.dont_write_bytecode = True  # do not litter tools/hworacle with __pycache__
HERE = os.path.dirname(os.path.abspath(__file__))
HWORACLE_DIR = os.path.dirname(HERE)
sys.path.insert(0, HWORACLE_DIR)

import cache as cache_mod  # noqa: E402
from cache import Cache, CacheConflict, families  # noqa: E402

THIS_FILE = os.path.abspath(__file__)
CACHE_PY = os.path.join(HWORACLE_DIR, "cache.py")


# ----------------------------------------------------------------------------------------- helpers

def key_of(i):
    return hashlib.sha256(b"hwc-test-key-%d" % i).digest()


def value_of(key):
    """Deterministic value for a key: 32..192 bytes."""
    return hashlib.sha256(b"hwc-test-value" + key).digest() * (1 + key[0] % 6)


def listing(root, family):
    try:
        return os.listdir(os.path.join(root, family))
    except FileNotFoundError:
        return []


def seg_names(root, family):
    return sorted(n for n in listing(root, family) if n.startswith("seg-") and n.endswith(".hwc"))


def tmp_names(root, family):
    return sorted(n for n in listing(root, family) if n.startswith(".tmp-"))


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path, data):
    with open(path, "wb") as f:
        f.write(data)


def build_segment(meta_blob, count, body):
    """A structurally arbitrary segment with a CORRECT checksum (for testing the structure checks)."""
    raw = cache_mod._MAGIC + struct.pack("<I", len(meta_blob)) + meta_blob + struct.pack("<Q", count) + body
    return raw + hashlib.sha256(raw).digest()


class TempRootCase(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="hwc-test-")
        self.addCleanup(shutil.rmtree, self.root, True)

    def three_segments(self, family="fam"):
        """Three segments from three instances; returns (paths, [dicts])."""
        paths, dicts = [], []
        for n in range(3):
            items = {key_of(100 * n + i): b"seg%d-value%d" % (n, i) for i in range(5)}
            c = Cache(self.root, family)
            paths.append(c.put_many(items, {"n": n}))
            dicts.append(items)
            time.sleep(0.002)  # distinct time_ns prefixes in a stable order
        return paths, dicts


# --------------------------------------------------------------------------------------- 1. basics

class BasicTests(TempRootCase):
    def test_default_root(self):
        repo = os.path.dirname(os.path.dirname(HWORACLE_DIR))
        self.assertTrue(os.path.isabs(cache_mod.DEFAULT_ROOT))
        self.assertEqual(cache_mod.DEFAULT_ROOT, os.path.join(repo, "inventory", "hworacle_cache"))
        c = Cache()  # constructing must not touch the disk
        self.assertEqual(c.root, cache_mod.DEFAULT_ROOT)
        self.assertEqual(c.family, "misc")

    def test_construction_creates_nothing_and_first_use_creates_the_directory(self):
        root = os.path.join(self.root, "a", "b")
        c = Cache(root, "fam")
        self.assertFalse(os.path.exists(root))
        self.assertIsNone(c.get(key_of(1)))
        self.assertTrue(os.path.isdir(os.path.join(root, "fam")))

    def test_round_trip_and_persistence(self):
        k1, k2, k3 = key_of(1), key_of(2), key_of(3)
        c = Cache(self.root, "fam")
        path = c.put_many({k1: b"one", k2: b"two"}, {"cpu": "TestCPU", "microcode": 240})
        self.assertTrue(os.path.isfile(path))
        self.assertEqual(os.path.dirname(path), os.path.join(self.root, "fam"))
        self.assertRegex(os.path.basename(path), r"^seg-\d{20}-\d+-[0-9a-f]{16}\.hwc$")
        self.assertEqual(c.get(k1), b"one")
        self.assertEqual(c.get(k2), b"two")
        self.assertIsNone(c.get(k3))
        self.assertEqual(len(c), 2)

        d = Cache(self.root, "fam")  # a different instance sees it
        self.assertEqual(d.get(k1), b"one")
        self.assertEqual(d.get(k2), b"two")
        self.assertIsNone(d.get(k3))
        self.assertEqual(len(d), 2)
        self.assertEqual(d.stats()["entries"], 2)

    def test_get_many(self):
        keys = [key_of(i) for i in range(6)]
        c = Cache(self.root, "fam")
        c.put_many({k: value_of(k) for k in keys[:4]}, {})
        d = Cache(self.root, "fam")
        got = d.get_many(keys[2:] + [keys[0], keys[0]])
        self.assertEqual(got, {k: value_of(k) for k in (keys[2], keys[3], keys[0])})
        self.assertEqual(d.get_many([]), {})
        self.assertEqual(d.get_many(iter([keys[5]])), {})

    def test_meta_of(self):
        k1, k2, k3 = key_of(1), key_of(2), key_of(3)
        Cache(self.root, "fam").put_many({k1: b"a"}, {"cpu": "A", "microcode": 240})
        Cache(self.root, "fam").put_many({k2: b"b"}, {"cpu": "B", "list": (1, 2)})
        c = Cache(self.root, "fam")
        self.assertEqual(c.meta_of(k1), {"cpu": "A", "microcode": 240})
        self.assertEqual(c.meta_of(k2), {"cpu": "B", "list": [1, 2]})  # JSON round trip
        self.assertIsNone(c.meta_of(k3))
        c.meta_of(k1)["cpu"] = "mutated"  # a copy: the cache's own copy is unharmed
        self.assertEqual(c.meta_of(k1)["cpu"], "A")
        # the writing instance answers the same way without reloading
        w = Cache(self.root, "other")
        w.put_many({k1: b"a"}, {"t": (3,)})
        self.assertEqual(w.meta_of(k1), {"t": [3]})

    def test_len_and_stats(self):
        c = Cache(self.root, "fam")
        self.assertEqual(len(c), 0)
        self.assertEqual(c.stats(), {"family": "fam", "segments": 0, "entries": 0, "bytes": 0, "conflicts": 0})
        p1 = c.put_many({key_of(1): b"x" * 10, key_of(2): b"y"}, {"a": 1})
        p2 = c.put_many({key_of(3): b""}, {"a": 2})
        st = Cache(self.root, "fam").stats()
        self.assertEqual(set(st), {"family", "segments", "entries", "bytes", "conflicts"})
        self.assertEqual((st["segments"], st["entries"], st["conflicts"]), (2, 3, 0))
        self.assertEqual(st["bytes"], os.path.getsize(p1) + os.path.getsize(p2))
        self.assertEqual(c.stats(), st)

    def test_wrong_key_length(self):
        c = Cache(self.root, "fam")
        good = key_of(1)
        for bad in (b"", b"x" * 31, b"x" * 33, b"x" * 64, "x" * 32, None, 5):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    c.get(bad)
                with self.assertRaises(ValueError):
                    c.get_many([good, bad])
                with self.assertRaises(ValueError):
                    c.meta_of(bad)
                with self.assertRaises(ValueError):
                    c.put_many({good: b"v", bad: b"w"}, {})
        self.assertEqual(seg_names(self.root, "fam"), [])  # the failed puts wrote nothing
        self.assertIsNone(c.get(good))

    def test_bad_family_name(self):
        for bad in ("", ".", "..", ".hidden", "a/b", "a b", "a\x00b", "café", "x/../y", "a\n", 5, None, b"fam"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    Cache(self.root, bad)
        for good in ("a", "A.b_c-d", "x..y", "1", "misc"):
            with self.subTest(good=good):
                Cache(self.root, good)
        self.assertEqual(os.listdir(self.root), [])

    def test_empty_value(self):
        k1, k2 = key_of(1), key_of(2)
        c = Cache(self.root, "fam")
        c.put_many({k1: b"", k2: b"nonempty"}, {})
        d = Cache(self.root, "fam")
        self.assertEqual(d.get(k1), b"")
        self.assertIsNotNone(d.get(k1))
        self.assertEqual(d.get_many([k1, k2]), {k1: b"", k2: b"nonempty"})
        self.assertEqual(len(d), 2)

    def test_large_value_and_odd_types(self):
        big = os.urandom(512 * 1024)
        k1, k2 = key_of(1), key_of(2)
        c = Cache(self.root, "fam")
        c.put_many([(k1, big), (bytearray(k2), bytearray(b"ba"))], {})  # pairs, since a bytearray is unhashable
        d = Cache(self.root, "fam")
        self.assertEqual(d.get(k1), big)
        self.assertEqual(d.get(k2), b"ba")
        self.assertIs(type(d.get(k2)), bytes)

    def test_bad_value_or_meta_writes_nothing(self):
        c = Cache(self.root, "fam")
        with self.assertRaises(TypeError):
            c.put_many({key_of(1): "text"}, {})
        with self.assertRaises(TypeError):
            c.put_many({key_of(1): b"v"}, {"bad": {1, 2}})  # a set is not JSON
        with self.assertRaises(TypeError):
            c.put_many({key_of(1): b"v"}, ["not", "a", "dict"])
        with self.assertRaises(TypeError):
            c.put_many({key_of(1): b"v"}, {"nan": float("nan")})
        self.assertEqual(listing(self.root, "fam"), [])
        self.assertIsNone(c.get(key_of(1)))


# ------------------------------------------------------------------------- 2. skipping, no leftovers

class PutManyTests(TempRootCase):
    def test_skips_known_keys_and_returns_none_when_nothing_is_new(self):
        k1, k2, k3 = key_of(1), key_of(2), key_of(3)
        c = Cache(self.root, "fam")
        self.assertIsNone(c.put_many({}, {"m": 1}))
        self.assertIsNotNone(c.put_many({k1: b"1", k2: b"2"}, {"m": 1}))
        self.assertIsNone(c.put_many({k1: b"1"}, {"m": 2}))
        self.assertIsNone(c.put_many({k1: b"1", k2: b"2"}, {"m": 2}))
        self.assertEqual(len(seg_names(self.root, "fam")), 1)

        p3 = c.put_many({k1: b"1", k3: b"3"}, {"m": 3})
        self.assertIsNotNone(p3)
        _meta, keys, values = cache_mod._parse_segment(read_bytes(p3))
        self.assertEqual((keys, values), ([k3], [b"3"]))  # k1 was skipped
        self.assertEqual(len(seg_names(self.root, "fam")), 2)

        d = Cache(self.root, "fam")  # a fresh instance knows everything, too
        self.assertIsNone(d.put_many({k1: b"1", k2: b"2", k3: b"3"}, {"m": 4}))
        self.assertEqual(len(seg_names(self.root, "fam")), 2)
        self.assertEqual(c.meta_of(k1), {"m": 1})  # not re-attributed to the skipped write
        self.assertEqual(c.meta_of(k3), {"m": 3})

    def test_no_tmp_file_left_behind(self):
        c = Cache(self.root, "fam")
        for n in range(5):
            c.put_many({key_of(10 * n + i): b"v" for i in range(4)}, {"n": n})
        self.assertEqual(len(seg_names(self.root, "fam")), 5)
        self.assertEqual(tmp_names(self.root, "fam"), [])

    def test_failed_write_removes_its_tmp_file(self):
        c = Cache(self.root, "fam")
        c.put_many({key_of(1): b"v"}, {})
        with mock.patch.object(cache_mod, "_fsync", side_effect=OSError("disk on fire")):
            with self.assertRaises(OSError):
                c.put_many({key_of(2): b"v"}, {})
        self.assertEqual(tmp_names(self.root, "fam"), [])
        self.assertEqual(len(seg_names(self.root, "fam")), 1)
        self.assertIsNone(c.get(key_of(2)))  # memory was not told about the failed write
        self.assertIsNotNone(c.put_many({key_of(2): b"v"}, {}))  # and a retry works

    def test_segment_is_written_under_a_tmp_name_and_fsynced_before_it_appears(self):
        c = Cache(self.root, "fam")
        c.put_many({key_of(1): b"v"}, {})  # the directory exists now
        seen = []
        real = cache_mod._fsync

        def spy(fd):
            seen.append((len(seg_names(self.root, "fam")), len(tmp_names(self.root, "fam"))))
            return real(fd)

        with mock.patch.object(cache_mod, "_fsync", spy):
            c.put_many({key_of(2): b"v"}, {})
        # first the file (old segment only, the new one still under its .tmp- name), then the directory
        self.assertEqual(seen, [(1, 1), (2, 0)])

    def test_two_instances_get_distinct_segment_names(self):
        a, b = Cache(self.root, "fam"), Cache(self.root, "fam")
        self.assertEqual((len(a), len(b)), (0, 0))  # both loaded before either writes
        pa = a.put_many({key_of(1): b"v"}, {})
        pb = b.put_many({key_of(1): b"v"}, {})
        self.assertIsNotNone(pb)
        self.assertNotEqual(pa, pb)
        self.assertEqual(len(seg_names(self.root, "fam")), 2)


# ------------------------------------------------------------------------------------ 3. conflicts

class ConflictTests(TempRootCase):
    def test_conflict_on_write_raises_and_writes_nothing(self):
        k1, k2 = key_of(1), key_of(2)
        c = Cache(self.root, "fam")
        c.put_many({k1: b"first"}, {})
        with self.assertRaises(CacheConflict) as cm:
            c.put_many({k1: b"second", k2: b"innocent"}, {})
        self.assertEqual(cm.exception.conflicts, [(k1, b"first", b"second")])
        self.assertIn(k1.hex(), str(cm.exception))
        self.assertEqual(len(seg_names(self.root, "fam")), 1)
        self.assertEqual(tmp_names(self.root, "fam"), [])
        self.assertIsNone(c.get(k2))
        d = Cache(self.root, "fam")
        self.assertEqual(d.get(k1), b"first")
        self.assertIsNone(d.get(k2))
        self.assertEqual(d.conflicts, [])

    def test_conflict_on_write_seen_after_refresh(self):
        k1 = key_of(1)
        a, b = Cache(self.root, "fam"), Cache(self.root, "fam")
        self.assertEqual(len(b), 0)
        a.put_many({k1: b"first"}, {})
        b.refresh()
        with self.assertRaises(CacheConflict):
            b.put_many({k1: b"second"}, {})
        self.assertEqual(len(seg_names(self.root, "fam")), 1)

    def two_blind_writers(self, family="fam"):
        """Two instances that both loaded before either wrote, so neither can see the other."""
        k1, k3, k4 = key_of(1), key_of(3), key_of(4)
        a, b = Cache(self.root, family), Cache(self.root, family)
        self.assertEqual((len(a), len(b)), (0, 0))
        pa = a.put_many({k1: b"from-a", k3: b"three"}, {"who": "a"})
        pb = b.put_many({k1: b"from-b", k4: b"four"}, {"who": "b"})
        self.assertTrue(pa and pb)
        return k1, k3, k4

    def test_conflict_on_load(self):
        k1, k3, k4 = self.two_blind_writers()
        c = Cache(self.root, "fam")
        with self.assertRaises(CacheConflict) as cm:
            c.get(k1)
        self.assertEqual(cm.exception.conflicts[0][0], k1)
        self.assertEqual(len(c.conflicts), 1)
        key, va, vb = c.conflicts[0]
        self.assertEqual(key, k1)
        self.assertEqual({va, vb}, {b"from-a", b"from-b"})
        # other keys are unaffected
        self.assertEqual(c.get(k3), b"three")
        self.assertEqual(c.get(k4), b"four")
        self.assertEqual(c.get_many([k3, k4]), {k3: b"three", k4: b"four"})
        with self.assertRaises(CacheConflict):
            c.get_many([k3, k1, k4])
        self.assertEqual(len(c), 3)
        self.assertEqual(c.stats()["conflicts"], 1)
        # a conflicted key cannot be written again, whichever value is offered
        with self.assertRaises(CacheConflict):
            c.put_many({k1: b"from-a"}, {})
        self.assertEqual(len(seg_names(self.root, "fam")), 2)
        # and compact refuses, leaving both answers on disk
        with self.assertRaises(CacheConflict):
            c.compact()
        self.assertEqual(len(seg_names(self.root, "fam")), 2)

    def test_conflict_found_by_refresh(self):
        observer = Cache(self.root, "fam")
        self.assertEqual(len(observer), 0)
        k1, k3, _k4 = self.two_blind_writers()
        self.assertEqual(observer.conflicts, [])  # has not looked yet
        self.assertIsNone(observer.get(k1))
        observer.refresh()
        self.assertEqual(len(observer.conflicts), 1)
        with self.assertRaises(CacheConflict):
            observer.get(k1)
        self.assertEqual(observer.get(k3), b"three")
        observer.refresh()  # a second refresh must not record the same conflict again
        self.assertEqual(len(observer.conflicts), 1)

    def test_identical_values_in_two_segments_are_not_a_conflict(self):
        k1, k2 = key_of(1), key_of(2)
        a, b = Cache(self.root, "fam"), Cache(self.root, "fam")
        self.assertEqual((len(a), len(b)), (0, 0))
        a.put_many({k1: b"same", k2: b"x"}, {"who": "a"})
        b.put_many({k1: b"same"}, {"who": "b"})
        self.assertEqual(len(seg_names(self.root, "fam")), 2)
        c = Cache(self.root, "fam")
        self.assertEqual(c.get(k1), b"same")
        self.assertEqual(c.conflicts, [])
        self.assertEqual(len(c), 2)
        self.assertEqual(c.stats()["conflicts"], 0)

    def test_duplicate_keys_inside_one_segment(self):
        k1 = key_of(1)
        body = k1 + struct.pack("<I", 1) + b"a" + k1 + struct.pack("<I", 1) + b"b"
        name = cache_mod._new_segment_name()
        os.makedirs(os.path.join(self.root, "fam"))
        write_bytes(os.path.join(self.root, "fam", name), build_segment(b"{}", 2, body))
        c = Cache(self.root, "fam")
        with self.assertRaises(CacheConflict):
            c.get(k1)
        self.assertEqual(c.damaged, [])


# --------------------------------------------------------------------------------------- 4. damage

class DamageTests(TempRootCase):
    def check_one_ignored(self, paths, dicts, victim=1):
        c = Cache(self.root, "fam")
        self.assertEqual(c.damaged, [paths[victim]])
        self.assertTrue(os.path.exists(paths[victim]))  # never deleted
        for n, items in enumerate(dicts):
            for k, v in items.items():
                self.assertEqual(c.get(k), None if n == victim else v)
        self.assertEqual(len(c), sum(len(d) for n, d in enumerate(dicts) if n != victim))
        st = c.stats()
        self.assertEqual(st["segments"], len(dicts) - 1)
        self.assertEqual(c.conflicts, [])
        return c

    def test_truncated_segment_is_ignored(self):
        for cut in (0, 1, 10, 30, 70, "half", "minus1", "minus32"):
            with self.subTest(cut=cut):
                fam = "fam"
                shutil.rmtree(os.path.join(self.root, fam), ignore_errors=True)
                paths, dicts = self.three_segments(fam)
                data = read_bytes(paths[1])
                n = {"half": len(data) // 2, "minus1": len(data) - 1, "minus32": len(data) - 32}.get(cut, cut)
                write_bytes(paths[1], data[:n])
                self.check_one_ignored(paths, dicts)

    def test_flipped_byte_is_ignored(self):
        magic = len(cache_mod._MAGIC)
        probes = [0, magic - 1, magic, magic + 2, "meta", "middle", "last-entry", "trailer-first", "trailer-last"]
        for probe in probes:
            with self.subTest(offset=probe):
                shutil.rmtree(os.path.join(self.root, "fam"), ignore_errors=True)
                paths, dicts = self.three_segments()
                data = bytearray(read_bytes(paths[1]))
                off = {
                    "meta": magic + 4 + 1,
                    "middle": len(data) // 2,
                    "last-entry": len(data) - 33,
                    "trailer-first": len(data) - 32,
                    "trailer-last": len(data) - 1,
                }.get(probe, probe)
                data[off] ^= 0x01
                write_bytes(paths[1], bytes(data))
                self.check_one_ignored(paths, dicts)

    def test_garbage_empty_and_structurally_bad_segments_are_ignored(self):
        os.makedirs(os.path.join(self.root, "fam"))
        k1 = key_of(1)
        entry = k1 + struct.pack("<I", 2) + b"ok"
        bad = {
            "empty": b"",
            "garbage": os.urandom(500),
            "text": b"this is not a segment\n" * 20,
            "magic-only": cache_mod._MAGIC,
            "count-too-big": build_segment(b"{}", 5, entry),
            "count-too-small": build_segment(b"{}", 0, entry),
            "stray-bytes": build_segment(b"{}", 1, entry + b"junk"),
            "value-overrun": build_segment(b"{}", 1, k1 + struct.pack("<I", 99) + b"ok"),
            "meta-overrun": cache_mod._MAGIC + struct.pack("<I", 10**6) + b"{}" + struct.pack("<Q", 0),
            "meta-not-json": build_segment(b"{nope", 1, entry),
            "meta-not-utf8": build_segment(b"\xff\xfe", 1, entry),
            "meta-not-object": build_segment(b"[1,2]", 1, entry),
            "huge-count": build_segment(b"{}", 2**63, entry),
        }
        want = []
        for label, data in bad.items():
            name = cache_mod._new_segment_name()
            write_bytes(os.path.join(self.root, "fam", name), data)
            want.append(os.path.join(self.root, "fam", name))
        os.mkdir(os.path.join(self.root, "fam", "seg-adirectory.hwc"))  # unreadable as a file
        want.append(os.path.join(self.root, "fam", "seg-adirectory.hwc"))
        write_bytes(os.path.join(self.root, "fam", ".tmp-seg-garbage.hwc"), b"not a segment")  # ignored entirely
        good = build_segment(b'{"ok":true}', 1, entry)
        good_name = cache_mod._new_segment_name()
        write_bytes(os.path.join(self.root, "fam", good_name), good)

        c = Cache(self.root, "fam")
        self.assertEqual(sorted(c.damaged), sorted(want))
        self.assertEqual(c.get(k1), b"ok")
        self.assertEqual(c.meta_of(k1), {"ok": True})
        self.assertEqual(len(c), 1)
        self.assertEqual(c.stats()["segments"], 1)
        for path in want:
            self.assertTrue(os.path.exists(path))

    def test_damaged_segments_survive_a_compaction(self):
        paths, dicts = self.three_segments()
        data = read_bytes(paths[1])
        write_bytes(paths[1], data[:-5])
        c = Cache(self.root, "fam")
        self.assertEqual(c.damaged, [paths[1]])
        c.compact()
        self.assertEqual(c.damaged, [paths[1]])
        self.assertTrue(os.path.exists(paths[1]))
        self.assertEqual(len(seg_names(self.root, "fam")), 2)  # merged + the damaged one
        d = Cache(self.root, "fam")
        self.assertEqual(d.damaged, [paths[1]])
        self.assertEqual(len(d), len(dicts[0]) + len(dicts[2]))

    def test_repaired_segment_is_picked_up_by_refresh_and_unchanged_one_is_not_reread(self):
        paths, dicts = self.three_segments()
        good = read_bytes(paths[1])
        write_bytes(paths[1], good[:40])
        c = Cache(self.root, "fam")
        self.assertEqual(c.damaged, [paths[1]])
        reads = []
        real = cache_mod._read_file

        def counting(path):
            reads.append(path)
            return real(path)

        with mock.patch.object(cache_mod, "_read_file", counting):
            c.refresh()
            self.assertEqual(reads, [])  # same size and mtime: not read again
            write_bytes(paths[1], good)
            c.refresh()
        self.assertEqual(reads, [paths[1]])
        self.assertEqual(c.damaged, [])
        self.assertEqual(len(c), sum(len(d) for d in dicts))
        for items in dicts:
            for k, v in items.items():
                self.assertEqual(c.get(k), v)

    def test_deleting_a_damaged_file_clears_it_on_refresh(self):
        paths, _dicts = self.three_segments()
        write_bytes(paths[0], b"")
        c = Cache(self.root, "fam")
        self.assertEqual(c.damaged, [paths[0]])
        os.unlink(paths[0])
        c.refresh()
        self.assertEqual(c.damaged, [])


# -------------------------------------------------------------------------------------- 5. families

class FamilyTests(TempRootCase):
    def test_families_are_isolated_and_listed(self):
        k1, k2 = key_of(1), key_of(2)
        a, b = Cache(self.root, "alpha"), Cache(self.root, "beta.v2")
        a.put_many({k1: b"in-alpha", k2: b"only-alpha"}, {"f": "a"})
        b.put_many({k1: b"in-beta"}, {"f": "b"})  # same key, different value: no conflict across families
        a2, b2 = Cache(self.root, "alpha"), Cache(self.root, "beta.v2")
        self.assertEqual(a2.get(k1), b"in-alpha")
        self.assertEqual(b2.get(k1), b"in-beta")
        self.assertIsNone(b2.get(k2))
        self.assertEqual((len(a2), len(b2)), (2, 1))
        self.assertEqual(a2.conflicts, [])
        self.assertEqual(b2.meta_of(k1), {"f": "b"})
        self.assertEqual(len(seg_names(self.root, "alpha")), 1)
        self.assertEqual(len(seg_names(self.root, "beta.v2")), 1)
        self.assertIsNone(Cache(self.root, "gamma").get(k1))
        self.assertEqual(families(self.root), ["alpha", "beta.v2", "gamma"])  # reading creates the directory

    def test_families_ignores_non_families(self):
        self.assertEqual(families(os.path.join(self.root, "does-not-exist")), [])
        self.assertEqual(families(self.root), [])
        Cache(self.root, "zeta").put_many({key_of(1): b"v"}, {})
        Cache(self.root, "alpha").put_many({key_of(1): b"v"}, {})
        os.mkdir(os.path.join(self.root, ".hidden"))
        os.mkdir(os.path.join(self.root, "has space"))
        write_bytes(os.path.join(self.root, "README"), b"a file, not a family")
        self.assertEqual(families(self.root), ["alpha", "zeta"])

    def test_compaction_is_per_family(self):
        a, b = Cache(self.root, "alpha"), Cache(self.root, "beta")
        for n in range(3):
            a.put_many({key_of(n): b"a"}, {"n": n})
            b.put_many({key_of(n): b"b"}, {"n": n})
        a.compact()
        self.assertEqual(len(seg_names(self.root, "alpha")), 1)
        self.assertEqual(len(seg_names(self.root, "beta")), 3)


# ------------------------------------------------------------------------ 6. concurrent processes

POOL = 1500  # size of the key pool the writers share


def writer_plan(seed, nseg):
    """Per segment: 25 ids from the shared pool (distinct within this writer) + 25 private ids."""
    shared = random.Random(seed).sample(range(POOL), 25 * nseg)
    plan = []
    for j in range(nseg):
        ids = shared[25 * j : 25 * (j + 1)]
        ids += [POOL + 1_000_000 * (seed + 1) + 100 * j + i for i in range(25)]
        plan.append(ids)
    return plan


def run_writer(root, family, seed, nseg, fresh):
    c = Cache(root, family)
    for j, ids in enumerate(writer_plan(seed, nseg)):
        items = {key_of(i): value_of(key_of(i)) for i in ids}
        (Cache(root, family) if fresh else c).put_many(items, {"writer": seed, "seg": j})


def start_writer(root, family, seed, nseg, fresh=False):
    argv = [sys.executable, THIS_FILE, "--writer", root, family, str(seed), str(nseg)]
    if fresh:
        argv.append("fresh")
    return subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)


def finish(testcase, procs, timeout=120):
    for p in procs:
        out, err = p.communicate(timeout=timeout)
        testcase.assertEqual(p.returncode, 0, "writer failed:\n" + err.decode("utf-8", "replace"))


def expected_keys(seeds, nseg):
    keys = set()
    for seed in seeds:
        for ids in writer_plan(seed, nseg):
            keys.update(key_of(i) for i in ids)
    return keys


class ConcurrentWriterTests(TempRootCase):
    def verify(self, family, want_keys):
        c = Cache(self.root, family)
        self.assertEqual(len(c), len(want_keys))
        self.assertEqual(c.conflicts, [])
        self.assertEqual(c.damaged, [])
        got = c.get_many(want_keys)
        self.assertEqual(len(got), len(want_keys))
        for k, v in got.items():
            self.assertEqual(v, value_of(k))
        self.assertEqual(tmp_names(self.root, family), [])
        return c

    def test_six_writers_forty_segments_each(self):
        seeds = range(6)
        t0 = time.time()
        procs = [start_writer(self.root, "race", seed, 40) for seed in seeds]
        finish(self, procs)
        elapsed = time.time() - t0
        c = self.verify("race", expected_keys(seeds, 40))
        self.assertEqual(len(seg_names(self.root, "race")), 6 * 40)
        on_disk = 0
        for name in seg_names(self.root, "race"):
            on_disk += len(cache_mod._parse_segment(read_bytes(os.path.join(self.root, "race", name)))[1])
        print("\n  6 writers x 40 segments in %.1f s: %d entries on disk, %d distinct keys" % (elapsed, on_disk, len(c)))
        self.assertGreaterEqual(on_disk, len(c))
        # every segment names its writer, and a key's metadata is that of a segment that holds it
        k = key_of(writer_plan(3, 40)[7][30])
        self.assertIn(c.meta_of(k)["writer"], range(6))

    def test_compaction_while_writers_and_readers_are_running(self):
        seeds = range(3)
        procs = [start_writer(self.root, "busy", seed, 15, fresh=True) for seed in seeds]
        compactions = merges = 0
        deadline = time.time() + 100
        while True:
            c = Cache(self.root, "busy")  # a fresh instance: loads while the writers are adding segments
            if c.stats()["segments"] > 1:
                merges += 1
            c.compact()
            compactions += 1
            if all(p.poll() is not None for p in procs) or time.time() > deadline:
                break
        finish(self, procs)
        want = expected_keys(seeds, 15)
        self.verify("busy", want)
        Cache(self.root, "busy").compact()
        self.verify("busy", want)
        self.assertEqual(len(seg_names(self.root, "busy")), 1)
        print("\n  %d compactions (%d of them merging 2+ segments) ran while 3 writers wrote 15 segments each"
              % (compactions, merges))
        self.assertGreaterEqual(compactions, 1)


# ----------------------------------------------------------------------------------- 7. compaction

class CompactionTests(TempRootCase):
    def test_keeps_every_entry_and_leaves_one_segment(self):
        metas = [{"cpu": "A", "microcode": 240}, {"cpu": "B", "microcode": 241}, {"cpu": "A", "microcode": 240}]
        allitems = {}
        for n, meta in enumerate(metas):
            items = {key_of(10 * n + i): value_of(key_of(10 * n + i)) for i in range(7)}
            items[key_of(999)] = value_of(key_of(999))  # shared by all three
            Cache(self.root, "fam").put_many(items, meta)
            allitems.update(items)
        c = Cache(self.root, "fam")
        self.assertEqual(c.stats()["segments"], 3)
        before = c.stats()
        old_files = seg_names(self.root, "fam")
        c.compact()

        names = seg_names(self.root, "fam")
        self.assertEqual(len(names), 1)
        self.assertNotIn(names[0], old_files)
        self.assertEqual(tmp_names(self.root, "fam"), [])
        want_meta = {"merged": [metas[0], metas[1]]}  # distinct, in segment order
        for who in (c, Cache(self.root, "fam")):
            self.assertEqual(len(who), len(allitems))
            for k, v in allitems.items():
                self.assertEqual(who.get(k), v)
            self.assertEqual(who.meta_of(key_of(0)), want_meta)
            self.assertEqual(who.meta_of(key_of(999)), want_meta)
            self.assertEqual(who.stats()["segments"], 1)
            self.assertEqual(who.stats()["entries"], before["entries"])
            self.assertEqual(who.conflicts, [])
            self.assertEqual(who.damaged, [])
        self.assertEqual(c.stats()["bytes"], os.path.getsize(os.path.join(self.root, "fam", names[0])))
        self.assertLess(c.stats()["bytes"], before["bytes"])  # the duplicate key and two headers are gone

        # compacting again changes nothing
        c.compact()
        Cache(self.root, "fam").compact()
        self.assertEqual(seg_names(self.root, "fam"), names)

    def test_compacting_empty_and_single_segment_families_is_a_noop(self):
        c = Cache(self.root, "empty")
        c.compact()
        self.assertEqual(seg_names(self.root, "empty"), [])
        d = Cache(self.root, "one")
        path = d.put_many({key_of(1): b"v"}, {"m": 1})
        d.compact()
        self.assertEqual(seg_names(self.root, "one"), [os.path.basename(path)])
        self.assertEqual(d.meta_of(key_of(1)), {"m": 1})  # not wrapped in "merged"

    def test_merging_merged_segments_does_not_nest(self):
        Cache(self.root, "fam").put_many({key_of(1): b"a"}, {"m": "A"})
        Cache(self.root, "fam").put_many({key_of(2): b"b"}, {"m": "B"})
        c = Cache(self.root, "fam")
        c.compact()
        Cache(self.root, "fam").put_many({key_of(3): b"c"}, {"m": "C"})
        Cache(self.root, "fam").put_many({key_of(4): b"d"}, {"m": "B"})
        d = Cache(self.root, "fam")
        self.assertEqual(d.stats()["segments"], 3)
        d.compact()
        self.assertEqual(d.meta_of(key_of(1)), {"merged": [{"m": "A"}, {"m": "B"}, {"m": "C"}]})
        self.assertEqual(Cache(self.root, "fam").meta_of(key_of(4)), {"merged": [{"m": "A"}, {"m": "B"}, {"m": "C"}]})

    def test_stale_tmp_files_are_removed_but_young_ones_are_not(self):
        c = Cache(self.root, "fam")
        c.put_many({key_of(1): b"v"}, {})
        c.put_many({key_of(2): b"v"}, {})
        d = os.path.join(self.root, "fam")
        now = time.time()
        made = {}
        for label, age in (("old", 2 * 3600), ("older", 40 * 3600), ("young", 60), ("justunder", 3600 - 120), ("fresh", 0)):
            p = os.path.join(d, ".tmp-seg-%s.hwc" % label)
            write_bytes(p, b"partial")
            os.utime(p, (now - age, now - age))
            made[label] = p
        c.compact()
        self.assertFalse(os.path.exists(made["old"]))
        self.assertFalse(os.path.exists(made["older"]))
        for label in ("young", "justunder", "fresh"):
            self.assertTrue(os.path.exists(made[label]), label)
        self.assertEqual(len(seg_names(self.root, "fam")), 1)
        # a compaction that has nothing to merge still cleans up
        old2 = os.path.join(d, ".tmp-seg-old2.hwc")
        write_bytes(old2, b"x")
        os.utime(old2, (now - 5 * 3600, now - 5 * 3600))
        Cache(self.root, "fam").compact()
        self.assertFalse(os.path.exists(old2))

    def test_segment_written_after_the_compactor_loaded_survives(self):
        keys = [key_of(i) for i in range(6)]
        Cache(self.root, "fam").put_many({keys[0]: b"0", keys[1]: b"1"}, {"m": 0})
        Cache(self.root, "fam").put_many({keys[2]: b"2"}, {"m": 1})
        compactor = Cache(self.root, "fam")
        self.assertEqual(len(compactor), 3)  # the load
        late = Cache(self.root, "fam").put_many({keys[3]: b"late", keys[4]: b"later"}, {"m": "late"})
        self.assertTrue(os.path.exists(late))
        compactor.compact()
        self.assertTrue(os.path.exists(late))  # not merged, not deleted
        self.assertEqual(len(seg_names(self.root, "fam")), 2)
        fresh = Cache(self.root, "fam")
        for i, v in enumerate((b"0", b"1", b"2", b"late", b"later")):
            self.assertEqual(fresh.get(keys[i]), v)
        self.assertEqual(fresh.meta_of(keys[3]), {"m": "late"})
        self.assertEqual(fresh.meta_of(keys[0]), {"merged": [{"m": 0}, {"m": 1}]})
        # the compactor learns about it with refresh(), and a second compaction then merges it
        self.assertIsNone(compactor.get(keys[3]))
        compactor.refresh()
        self.assertEqual(compactor.get(keys[3]), b"late")
        self.assertEqual(compactor.stats()["segments"], 2)
        compactor.compact()
        self.assertEqual(len(seg_names(self.root, "fam")), 1)
        self.assertEqual(len(Cache(self.root, "fam")), 5)

    def test_reader_loaded_before_compaction_still_answers(self):
        keys = [key_of(i) for i in range(9)]
        for n in range(3):
            Cache(self.root, "fam").put_many({k: value_of(k) for k in keys[3 * n : 3 * n + 3]}, {"n": n})
        reader = Cache(self.root, "fam")
        self.assertEqual(len(reader), 9)
        Cache(self.root, "fam").compact()  # deletes the three files the reader loaded
        self.assertEqual(len(seg_names(self.root, "fam")), 1)
        for k in keys:  # from memory
            self.assertEqual(reader.get(k), value_of(k))
        reader.refresh()  # knows nothing of the old files any more, loads the merged one
        self.assertEqual(reader.stats()["segments"], 1)
        self.assertEqual(reader.stats()["entries"], 9)
        self.assertEqual(reader.conflicts, [])
        self.assertEqual(reader.damaged, [])
        for k in keys:
            self.assertEqual(reader.get(k), value_of(k))
        self.assertEqual(reader.get_many(keys), {k: value_of(k) for k in keys})

    def test_reader_created_before_compaction_but_loading_after(self):
        keys = [key_of(i) for i in range(6)]
        for n in range(2):
            Cache(self.root, "fam").put_many({k: value_of(k) for k in keys[3 * n : 3 * n + 3]}, {"n": n})
        reader = Cache(self.root, "fam")  # created, not loaded
        Cache(self.root, "fam").compact()
        self.assertEqual(reader.get_many(keys), {k: value_of(k) for k in keys})
        self.assertEqual(reader.meta_of(keys[0]), {"merged": [{"n": 0}, {"n": 1}]})

    def test_reader_whose_listed_file_is_deleted_underneath_it_retries(self):
        keys = [key_of(i) for i in range(8)]
        for n in range(4):
            Cache(self.root, "fam").put_many({k: value_of(k) for k in keys[2 * n : 2 * n + 2]}, {"n": n})
        reader = Cache(self.root, "fam")
        real = cache_mod._read_file
        state = {"compacted": False, "calls": 0}

        def racy(path):
            state["calls"] += 1
            if not state["compacted"]:
                state["compacted"] = True
                Cache(self.root, "fam").compact()  # deletes every file the reader has listed
            return real(path)  # FileNotFoundError for the file the reader was about to read

        with mock.patch.object(cache_mod, "_read_file", racy):
            self.assertEqual(reader.get_many(keys), {k: value_of(k) for k in keys})
        self.assertGreaterEqual(state["calls"], 3)  # the compactor's own reads + the failed one + the retry
        self.assertEqual(reader.damaged, [])
        self.assertEqual(reader.conflicts, [])
        self.assertEqual(len(reader), 8)
        self.assertEqual(reader.stats()["segments"], 1)

    def test_reader_loses_some_files_midway_and_keeps_what_it_read(self):
        keys = [key_of(i) for i in range(8)]
        for n in range(4):
            Cache(self.root, "fam").put_many({k: value_of(k) for k in keys[2 * n : 2 * n + 2]}, {"n": n})
        reader = Cache(self.root, "fam")
        real = cache_mod._read_file
        calls = []

        def racy(path):
            calls.append(path)
            if len(calls) == 3:  # two files read, the third is about to be: compact now
                Cache(self.root, "fam").compact()
            return real(path)

        with mock.patch.object(cache_mod, "_read_file", racy):
            self.assertEqual(len(reader), 8)
        self.assertEqual(reader.conflicts, [])
        self.assertEqual(reader.get_many(keys), {k: value_of(k) for k in keys})

    def test_compact_inside_the_deleting_instance_updates_its_own_view(self):
        c = Cache(self.root, "fam")
        for n in range(4):
            c.put_many({key_of(n): b"v%d" % n}, {"n": n})
        c.compact()
        self.assertEqual(c.stats()["segments"], 1)
        c.put_many({key_of(9): b"nine"}, {"n": 9})
        self.assertEqual(c.stats()["segments"], 2)
        self.assertEqual(c.meta_of(key_of(9)), {"n": 9})
        self.assertEqual(c.meta_of(key_of(1))["merged"], [{"n": 0}, {"n": 1}, {"n": 2}, {"n": 3}])
        self.assertEqual(Cache(self.root, "fam").stats()["entries"], 5)


# ------------------------------------------------------------------------------------- 8. load time

class LoadTimeTests(TempRootCase):
    def test_200000_entries_in_4_segments_load_in_under_5_seconds(self):
        per_segment, nseg = 50_000, 4
        c = Cache(self.root, "big")
        for s in range(nseg):
            items = {}
            for i in range(s * per_segment, (s + 1) * per_segment):
                k = key_of(i)
                items[k] = value_of(k)
            self.assertIsNotNone(c.put_many(items, {"segment": s}))
        total_bytes = sum(os.path.getsize(os.path.join(self.root, "big", n)) for n in seg_names(self.root, "big"))

        t0 = time.perf_counter()
        fresh = Cache(self.root, "big")
        n = len(fresh)
        elapsed = time.perf_counter() - t0
        print("\n  loaded %d entries in %d segments (%.1f MB) in %.2f s" % (n, nseg, total_bytes / 1e6, elapsed))
        self.assertEqual(n, per_segment * nseg)
        self.assertLess(elapsed, 5.0)
        self.assertEqual(fresh.stats()["segments"], nseg)
        for i in (0, 49_999, 50_000, 123_456, 199_999):
            k = key_of(i)
            self.assertEqual(fresh.get(k), value_of(k))
            self.assertEqual(fresh.meta_of(k), {"segment": i // per_segment})
        self.assertIsNone(fresh.get(key_of(200_000)))


# ------------------------------------------------------------------------------------------- CLI

class CommandLineTests(TempRootCase):
    def run_cli(self, *args):
        r = subprocess.run([sys.executable, CACHE_PY, *args], capture_output=True, text=True, timeout=60)
        return r.returncode, r.stdout, r.stderr

    def test_stats_and_compact(self):
        for n in range(3):
            Cache(self.root, "alpha").put_many({key_of(n): b"v"}, {"n": n})
        Cache(self.root, "beta").put_many({key_of(1): b"v1", key_of(2): b"v2"}, {})
        write_bytes(os.path.join(self.root, "beta", cache_mod._new_segment_name()), b"damaged")
        rc, out, err = self.run_cli("stats", "--root", self.root)
        self.assertEqual(rc, 0, err)
        lines = out.strip().splitlines()
        self.assertEqual(len(lines), 2)
        self.assertRegex(lines[0], r"^alpha: segments=3 entries=3 bytes=\d+ conflicts=0 damaged=0$")
        self.assertRegex(lines[1], r"^beta: segments=1 entries=2 bytes=\d+ conflicts=0 damaged=1$")
        rc, out, err = self.run_cli("compact", "alpha", "--root", self.root)
        self.assertEqual(rc, 0, err)
        self.assertIn("alpha", out)
        self.assertEqual(len(seg_names(self.root, "alpha")), 1)
        rc, out, err = self.run_cli("stats", "--root", self.root)
        self.assertRegex(out.splitlines()[0], r"^alpha: segments=1 entries=3 ")
        rc, out, err = self.run_cli("compact", "nosuchfamily", "--root", self.root)
        self.assertEqual(rc, 1)
        self.assertFalse(os.path.exists(os.path.join(self.root, "nosuchfamily")))
        rc, out, err = self.run_cli("compact", "bad/name", "--root", self.root)
        self.assertEqual(rc, 2)

    def test_stats_and_compact_with_a_conflict(self):
        a, b = Cache(self.root, "fam"), Cache(self.root, "fam")
        self.assertEqual((len(a), len(b)), (0, 0))
        a.put_many({key_of(1): b"x"}, {})
        b.put_many({key_of(1): b"y"}, {})
        rc, out, err = self.run_cli("stats", "--root", self.root)
        self.assertEqual(rc, 0, err)
        self.assertIn("conflicts=1", out)
        self.assertIn(key_of(1).hex(), err)
        rc, out, err = self.run_cli("compact", "fam", "--root", self.root)
        self.assertEqual(rc, 1)
        self.assertEqual(len(seg_names(self.root, "fam")), 2)

    def test_stats_with_no_cache(self):
        rc, out, err = self.run_cli("stats", "--root", os.path.join(self.root, "nothing"))
        self.assertEqual(rc, 0)
        self.assertEqual(out, "")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "--writer":
        _, _, w_root, w_family, w_seed, w_n, *w_rest = sys.argv
        run_writer(w_root, w_family, int(w_seed), int(w_n), "fresh" in w_rest)
        sys.exit(0)
    unittest.main()
