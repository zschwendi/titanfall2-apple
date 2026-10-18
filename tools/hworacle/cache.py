#!/usr/bin/env python3
"""cache.py - the answer store of the hardware oracle (tools/hworacle).

A *case* (one question to the real x86-64 CPU) is hashed to a 32-byte key; the answer is an opaque
bytes value.  Answers are deterministic, so they are stored for good and tests can run with the
oracle machine switched off.  Several git worktrees share one cache directory and may write to it
at the same moment, and the machine holding it can lose power at any time, so the store is built
from immutable files that are only ever created, never modified.

On-disk layout
--------------
    <root>/                         DEFAULT_ROOT = <repo root>/inventory/hworacle_cache
        <family>/                   one directory per family; name matches [A-Za-z0-9._-]+, no leading '.'
            seg-<ns>-<pid>-<rand>.hwc    immutable segment files (the data)
            .tmp-seg-....hwc             a segment being written (ignored by readers)

A segment is named seg-<time_ns, 20 digits>-<pid>-<16 hex digits of os.urandom(8)>.hwc, which is
unique across processes and machines and sorts roughly in time order.  It is written under the
name ".tmp-" + final name in the same directory, flushed, fsynced (plus F_FULLFSYNC on macOS, where
a plain fsync does not reach the platter), moved into place with os.replace, and the directory is
fsynced.  A segment is never modified afterwards; a reader therefore sees either no file or a
complete one, and a power cut leaves at worst a stale ".tmp-" file or a segment that fails its
checksum (it is ignored on load, listed in Cache.damaged, and never deleted automatically).

Segment file format (all integers little-endian):

    magic     b"HWORACLE-CACHE-SEGMENT v1\\n"                      (26 bytes)
    meta_len  u32
    meta      meta_len bytes: UTF-8 JSON object (compact, keys sorted) - who/what produced the entries
    count     u64                                                 number of entries
    entries   count times: key (32 bytes) | value_len u32 | value (value_len bytes)
    trailer   SHA-256 (32 bytes) of every byte before the trailer

A compacted segment carries the metadata {"merged": [<distinct metadata dicts of the inputs>]}
(a merged input is flattened, so merging merged segments does not nest).

Semantics
---------
* Loading is lazy.  The first get/get_many/meta_of/put_many/len/stats/compact reads every seg-*.hwc
  of the family into one in-memory dict.  put_many does NOT look at the disk again: segments other
  processes wrote after the load become visible with refresh().
* A key stored twice with identical values (two worktrees answered the same case) is fine.  With
  different values the hardware gave two answers for one case: the key is recorded in
  Cache.conflicts as (key, value_a, value_b), get()/get_many() raise CacheConflict for it, put_many()
  refuses to write it, and compact() refuses to run (merging would destroy one of the two answers).
* compact() merges the segments this instance has loaded into one and then deletes exactly those
  files; a segment that appeared after the load is left alone.  A reader that listed a file which a
  compaction has since deleted catches FileNotFoundError, lists again and retries.  Stale ".tmp-"
  files (older than one hour) are removed by compact(); younger ones are never touched.
* Not thread-safe (use one Cache per thread); any number of processes is fine.

Command line:

    python tools/hworacle/cache.py stats [--root DIR]
    python tools/hworacle/cache.py compact FAMILY [--root DIR]
"""

import argparse
import copy
import hashlib
import json
import os
import re
import struct
import sys
import time

try:  # Windows has no fcntl; the cache is only used on POSIX machines but stay importable.
    import fcntl
except ImportError:  # pragma: no cover
    fcntl = None

__all__ = ["DEFAULT_ROOT", "CacheConflict", "Cache", "families"]

DEFAULT_ROOT = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "inventory",
    "hworacle_cache",
)

KEY_LEN = 32
_MAGIC = b"HWORACLE-CACHE-SEGMENT v1\n"
_FAMILY_RE = re.compile(r"[A-Za-z0-9._-]+")
_SEG_PREFIX = "seg-"
_SEG_SUFFIX = ".hwc"
_TMP_PREFIX = ".tmp-"
_STALE_TMP_SECONDS = 3600
_MAX_VALUE_LEN = 0xFFFFFFFF
_WRITE_CHUNK = 4 << 20
_SCAN_RETRIES = 100
_DIGEST_LEN = hashlib.sha256().digest_size

_U32 = struct.Struct("<I")
_U64 = struct.Struct("<Q")
_F_FULLFSYNC = getattr(fcntl, "F_FULLFSYNC", None) if fcntl is not None else None


class CacheConflict(Exception):
    """The same key has two different values on disk (the hardware gave two answers for one case).

    `conflicts` is a list of (key, value_a, value_b) tuples (one entry when raised by get()).
    """

    def __init__(self, message, conflicts=()):
        super().__init__(message, list(conflicts))
        self.message = message
        self.conflicts = list(conflicts)

    def __str__(self):
        return self.message


class _BadSegment(Exception):
    """Internal: a segment file failed its magic / structure / checksum check."""


# ------------------------------------------------------------------------------------ validation

def _check_family(family):
    if not isinstance(family, str) or not _FAMILY_RE.fullmatch(family) or family.startswith("."):
        raise ValueError(
            "family must be a non-empty str matching [A-Za-z0-9._-]+ and not start with '.': %r" % (family,)
        )
    return family


def _check_key(key):
    if type(key) is bytes and len(key) == KEY_LEN:
        return key
    if isinstance(key, (bytes, bytearray, memoryview)):
        key = bytes(key)
        if len(key) == KEY_LEN:
            return key
        raise ValueError("key must be exactly %d bytes, got %d" % (KEY_LEN, len(key)))
    raise ValueError("key must be exactly %d bytes, got a %s" % (KEY_LEN, type(key).__name__))


def _check_value(value):
    if type(value) is not bytes:
        if not isinstance(value, (bytes, bytearray, memoryview)):
            raise TypeError("value must be bytes, got a %s" % type(value).__name__)
        value = bytes(value)
    if len(value) > _MAX_VALUE_LEN:
        raise ValueError("value is too long for a segment (%d bytes)" % len(value))
    return value


def _encode_meta(meta):
    if not isinstance(meta, dict):
        raise TypeError("meta must be a dict, got a %s" % type(meta).__name__)
    try:
        blob = json.dumps(meta, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise TypeError("meta is not JSON-serialisable: %s" % exc) from exc
    if len(blob) > _MAX_VALUE_LEN:
        raise ValueError("meta is too large")
    return blob


def _hex(key):
    return key.hex()


# -------------------------------------------------------------------------------------- file I/O

def _fsync(fd):
    os.fsync(fd)
    if _F_FULLFSYNC is not None:
        try:
            fcntl.fcntl(fd, _F_FULLFSYNC)
        except OSError:
            pass


def _fsync_dir(path):
    try:
        fd = os.open(path, os.O_RDONLY)
    except OSError:
        return
    try:
        _fsync(fd)
    except OSError:
        pass
    finally:
        os.close(fd)


def _read_file(path):
    """Return (bytes, signature) of a whole file; signature = (size, mtime_ns).  Raises OSError."""
    with open(path, "rb") as f:
        st = os.fstat(f.fileno())
        data = f.read()
    return data, (st.st_size, st.st_mtime_ns)


def _new_segment_name():
    return "%s%020d-%d-%s%s" % (_SEG_PREFIX, time.time_ns(), os.getpid(), os.urandom(8).hex(), _SEG_SUFFIX)


def _is_segment_name(name):
    return name.startswith(_SEG_PREFIX) and name.endswith(_SEG_SUFFIX)


def _write_segment(dirpath, items, meta_blob):
    """Write {key: value} (already validated) as one new segment; return (path, size)."""
    name = _new_segment_name()
    final = os.path.join(dirpath, name)
    tmp = os.path.join(dirpath, _TMP_PREFIX + name)
    h = hashlib.sha256()
    size = 0
    pack = _U32.pack
    try:
        with open(tmp, "xb") as f:

            def emit(data):
                nonlocal size
                f.write(data)
                h.update(data)
                size += len(data)

            emit(_MAGIC + _U32.pack(len(meta_blob)) + meta_blob + _U64.pack(len(items)))
            parts = []
            pending = 0
            for key, value in items.items():
                parts.append(key)
                parts.append(pack(len(value)))
                parts.append(value)
                pending += KEY_LEN + 4 + len(value)
                if pending >= _WRITE_CHUNK:
                    emit(b"".join(parts))
                    parts.clear()
                    pending = 0
            if parts:
                emit(b"".join(parts))
            trailer = h.digest()
            f.write(trailer)
            size += len(trailer)
            f.flush()
            _fsync(f.fileno())
        os.replace(tmp, final)
    except BaseException:
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise
    _fsync_dir(dirpath)
    return final, size


def _parse_segment(data):
    """Validate and parse a segment; return (meta dict, keys list, values list).  Raises _BadSegment."""
    n = len(data)
    magic_len = len(_MAGIC)
    if n < magic_len + 4 + 8 + _DIGEST_LEN:
        raise _BadSegment("too short (%d bytes)" % n)
    if data[:magic_len] != _MAGIC:
        raise _BadSegment("bad magic")
    body_end = n - _DIGEST_LEN
    if hashlib.sha256(memoryview(data)[:body_end]).digest() != data[body_end:]:
        raise _BadSegment("checksum mismatch")
    try:
        off = magic_len
        (meta_len,) = _U32.unpack_from(data, off)
        off += 4
        if off + meta_len + 8 > body_end:
            raise _BadSegment("metadata overruns the file")
        meta = json.loads(data[off : off + meta_len].decode("utf-8"))
        off += meta_len
        if not isinstance(meta, dict):
            raise _BadSegment("metadata is not an object")
        (count,) = _U64.unpack_from(data, off)
        off += 8
        keys = []
        values = []
        unpack_from = _U32.unpack_from
        for _ in range(count):
            vstart = off + KEY_LEN + 4
            if vstart > body_end:
                raise _BadSegment("entry header overruns the file")
            (vlen,) = unpack_from(data, off + KEY_LEN)
            end = vstart + vlen
            if end > body_end:
                raise _BadSegment("entry value overruns the file")
            keys.append(data[off : off + KEY_LEN])
            values.append(data[vstart:end])
            off = end
        if off != body_end:
            raise _BadSegment("%d stray bytes after the last entry" % (body_end - off))
    except _BadSegment:
        raise
    except (struct.error, ValueError, OverflowError, MemoryError, RecursionError) as exc:
        raise _BadSegment("unparseable: %s" % exc) from exc
    return meta, keys, values


# ------------------------------------------------------------------------------------------ Cache

class Cache:
    """One family of the hardware-oracle answer cache.  See the module docstring."""

    def __init__(self, root=None, family="misc"):
        _check_family(family)
        self.root = os.path.abspath(os.fspath(DEFAULT_ROOT if root is None else root))
        self.family = family
        self.dir = os.path.join(self.root, family)
        self._loaded = False
        self._data = {}  # key -> value (the first value seen)
        self._seg_of = {}  # key -> meta dict of the segment that first supplied it
        self._segs = {}  # name -> (size, meta) of every valid segment loaded
        self._damaged = {}  # name -> (size, mtime_ns) or None, of every unreadable segment
        self._conflicts = []  # (key, value_a, value_b)
        self._conflicted = set()

    # -- loading -----------------------------------------------------------------------------

    def _ensure_loaded(self):
        if not self._loaded:
            self._scan()
            self._loaded = True

    def _list_segments(self):
        try:
            return sorted(n for n in os.listdir(self.dir) if _is_segment_name(n))
        except FileNotFoundError:
            return []

    def _scan(self):
        """Load every segment file not seen yet; forget the ones that have vanished.

        A file listed here may be deleted by a concurrent compact() before it is read: that raises
        FileNotFoundError, and we simply list again (the merged segment, written before the
        deletion, is in the new listing).
        """
        os.makedirs(self.dir, exist_ok=True)
        for attempt in range(_SCAN_RETRIES):
            names = self._list_segments()
            present = set(names)
            for gone in [n for n in self._segs if n not in present]:
                del self._segs[gone]
            for gone in [n for n in self._damaged if n not in present]:
                del self._damaged[gone]
            try:
                for name in names:
                    if name in self._segs:
                        continue
                    if name in self._damaged:
                        sig = self._damaged[name]
                        if sig is not None:
                            st = os.stat(os.path.join(self.dir, name))
                            if (st.st_size, st.st_mtime_ns) == sig:
                                continue
                    self._load_one(name)
                return
            except FileNotFoundError:
                if attempt == _SCAN_RETRIES - 1:
                    raise

    def _load_one(self, name):
        path = os.path.join(self.dir, name)
        try:
            data, sig = _read_file(path)
        except FileNotFoundError:
            raise
        except OSError:
            self._damaged[name] = None
            return
        try:
            meta, keys, values = _parse_segment(data)
        except _BadSegment:
            self._damaged[name] = sig
            return
        self._damaged.pop(name, None)
        items = dict(zip(keys, values))
        if len(items) != len(keys):  # duplicate keys inside one segment: not written by us
            items = {}
            for key, value in zip(keys, values):
                old = items.setdefault(key, value)
                if old != value:
                    self._record_conflict(key, old, value)
        self._absorb(name, meta, items, len(data))

    def _record_conflict(self, key, value_a, value_b):
        self._conflicts.append((key, value_a, value_b))
        self._conflicted.add(key)

    def _absorb(self, name, meta, items, size):
        """Merge a segment's {key: value} into memory, recording conflicts."""
        self._segs[name] = (size, meta)
        data = self._data
        if data:
            common = items.keys() & data.keys()
            if common:
                for key in common:
                    if data[key] != items[key]:
                        self._record_conflict(key, data[key], items[key])
                items = {k: v for k, v in items.items() if k not in common}
        data.update(items)
        self._seg_of.update(dict.fromkeys(items, meta))

    def refresh(self):
        """Pick up segments other processes wrote since loading (and forget deleted ones)."""
        self._scan()
        self._loaded = True

    # -- public properties -------------------------------------------------------------------

    @property
    def conflicts(self):
        """List of (key, value_a, value_b) for keys that have two different values on disk."""
        self._ensure_loaded()
        return self._conflicts

    @property
    def damaged(self):
        """Paths of segment files that failed their checks and are ignored (never auto-deleted)."""
        self._ensure_loaded()
        return [os.path.join(self.dir, n) for n in sorted(self._damaged)]

    # -- reading -----------------------------------------------------------------------------

    def _raise_conflict(self, keys):
        recs = [c for c in self._conflicts if c[0] in keys]
        shown = ", ".join(_hex(k) for k in sorted(keys)[:3])
        more = "" if len(keys) <= 3 else " (and %d more)" % (len(keys) - 3)
        raise CacheConflict(
            "family %r in %s: key %s%s has different values on disk" % (self.family, self.root, shown, more),
            recs,
        )

    def get(self, key):
        """Return the cached value, or None if the key is not cached.  Raises CacheConflict."""
        key = _check_key(key)
        self._ensure_loaded()
        if self._conflicted and key in self._conflicted:
            self._raise_conflict({key})
        return self._data.get(key)

    def get_many(self, keys):
        """Return {key: value} for the requested keys that are cached.  Raises CacheConflict."""
        keys = [_check_key(k) for k in keys]
        self._ensure_loaded()
        if self._conflicted:
            bad = {k for k in keys if k in self._conflicted}
            if bad:
                self._raise_conflict(bad)
        data = self._data
        return {k: data[k] for k in keys if k in data}

    def meta_of(self, key):
        """Metadata dict of the segment that holds the key (a copy), or None if not cached."""
        key = _check_key(key)
        self._ensure_loaded()
        meta = self._seg_of.get(key)
        return None if meta is None else copy.deepcopy(meta)

    def __len__(self):
        self._ensure_loaded()
        return len(self._data)

    def stats(self):
        """{"family", "segments", "entries", "bytes", "conflicts"}; segments/bytes cover valid segments."""
        self._ensure_loaded()
        return {
            "family": self.family,
            "segments": len(self._segs),
            "entries": len(self._data),
            "bytes": sum(size for size, _meta in self._segs.values()),
            "conflicts": len(self._conflicts),
        }

    # -- writing -----------------------------------------------------------------------------

    def put_many(self, items, meta):
        """Store {key: value} as ONE new segment tagged with `meta`; return its path or None.

        Keys already cached with the same value are skipped (nothing left: nothing is written and
        None is returned).  A key already cached with a different value raises CacheConflict and
        nothing is written.  Only the state loaded so far is consulted; see refresh().
        """
        meta_blob = _encode_meta(meta)
        pairs = items.items() if hasattr(items, "items") else items  # a mapping, or an iterable of pairs
        clean = {}
        for key, value in pairs:
            clean[_check_key(key)] = _check_value(value)
        if not clean:
            return None
        self._ensure_loaded()
        data = self._data
        conflicted = self._conflicted
        new = {}
        clashes = []
        for key, value in clean.items():
            old = data.get(key)
            if old is None:
                new[key] = value
            elif key in conflicted:
                clashes.append((key, old, value))
            elif old != value:
                clashes.append((key, old, value))
        if clashes:
            shown = ", ".join(_hex(c[0]) for c in clashes[:3])
            more = "" if len(clashes) <= 3 else " (and %d more)" % (len(clashes) - 3)
            raise CacheConflict(
                "family %r in %s: key %s%s is already cached with a different value; nothing written"
                % (self.family, self.root, shown, more),
                clashes,
            )
        if not new:
            return None
        os.makedirs(self.dir, exist_ok=True)
        path, size = _write_segment(self.dir, new, meta_blob)
        self._absorb(os.path.basename(path), json.loads(meta_blob), new, size)
        return path

    # -- maintenance -------------------------------------------------------------------------

    def _remove_stale_tmp(self):
        now = time.time()
        try:
            names = os.listdir(self.dir)
        except FileNotFoundError:
            return
        for name in names:
            if not name.startswith(_TMP_PREFIX):
                continue
            path = os.path.join(self.dir, name)
            try:
                if now - os.stat(path).st_mtime > _STALE_TMP_SECONDS:
                    os.unlink(path)
            except OSError:
                pass

    def compact(self):
        """Merge the segments this instance has loaded into one, then delete exactly those files.

        Segments that appeared after the load, and damaged segments, are left alone.  Refuses
        (CacheConflict) while a conflict exists.  Call refresh() first to include newer segments.
        """
        self._ensure_loaded()
        self._remove_stale_tmp()
        if self._conflicts:
            raise CacheConflict(
                "family %r in %s has %d conflicting key(s); resolve them (remove the wrong segment) before compacting"
                % (self.family, self.root, len(self._conflicted)),
                self._conflicts,
            )
        inputs = sorted(self._segs)
        if len(inputs) <= 1:
            return
        distinct = []
        seen = set()
        for name in inputs:
            meta = self._segs[name][1]
            if set(meta) == {"merged"} and isinstance(meta["merged"], list):
                parts = meta["merged"]
            else:
                parts = [meta]
            for part in parts:
                ident = json.dumps(part, sort_keys=True, separators=(",", ":"))
                if ident not in seen:
                    seen.add(ident)
                    distinct.append(part)
        merged_blob = _encode_meta({"merged": distinct})
        path, size = _write_segment(self.dir, self._data, merged_blob)
        for name in inputs:  # the merged segment is durable (fsynced and renamed) before these go
            try:
                os.unlink(os.path.join(self.dir, name))
            except FileNotFoundError:
                pass
        _fsync_dir(self.dir)
        merged_meta = json.loads(merged_blob)
        self._segs = {os.path.basename(path): (size, merged_meta)}
        self._seg_of = dict.fromkeys(self._data, merged_meta)


def families(root=None):
    """Names of the families present under root (sub-directories with a valid family name)."""
    root = os.path.abspath(os.fspath(DEFAULT_ROOT if root is None else root))
    try:
        names = os.listdir(root)
    except (FileNotFoundError, NotADirectoryError):
        return []
    out = []
    for name in names:
        if _FAMILY_RE.fullmatch(name) and not name.startswith(".") and os.path.isdir(os.path.join(root, name)):
            out.append(name)
    return sorted(out)


# ------------------------------------------------------------------------------------------ CLI

def _cmd_stats(args):
    names = families(args.root)
    if not names:
        print("no families under %s" % os.path.abspath(args.root or DEFAULT_ROOT), file=sys.stderr)
        return 0
    for fam in names:
        cache = Cache(args.root, fam)
        st = cache.stats()
        damaged = cache.damaged
        print(
            "%s: segments=%d entries=%d bytes=%d conflicts=%d damaged=%d"
            % (fam, st["segments"], st["entries"], st["bytes"], st["conflicts"], len(damaged))
        )
        for path in damaged:
            print("  damaged: %s" % path, file=sys.stderr)
        for key, _a, _b in cache.conflicts[:10]:
            print("  conflict: %s" % _hex(key), file=sys.stderr)
    return 0


def _cmd_compact(args):
    try:
        _check_family(args.family)
    except ValueError as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 2
    if args.family not in families(args.root):
        print("error: no family %r under %s" % (args.family, os.path.abspath(args.root or DEFAULT_ROOT)), file=sys.stderr)
        return 1
    cache = Cache(args.root, args.family)
    before = cache.stats()
    try:
        cache.compact()
    except CacheConflict as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1
    after = cache.stats()
    print(
        "%s: segments %d -> %d, entries=%d bytes %d -> %d"
        % (args.family, before["segments"], after["segments"], after["entries"], before["bytes"], after["bytes"])
    )
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(prog="cache.py", description="Inspect or compact the hardware-oracle cache.")
    sub = parser.add_subparsers(dest="command", required=True)
    p_stats = sub.add_parser("stats", help="one line per family: segments, entries, bytes, conflicts, damaged")
    p_stats.add_argument("--root", default=None, help="cache root (default: %s)" % DEFAULT_ROOT)
    p_stats.set_defaults(func=_cmd_stats)
    p_compact = sub.add_parser("compact", help="merge all segments of FAMILY into one")
    p_compact.add_argument("family")
    p_compact.add_argument("--root", default=None, help="cache root (default: %s)" % DEFAULT_ROOT)
    p_compact.set_defaults(func=_cmd_compact)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
