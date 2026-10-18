#!/usr/bin/env python3
"""Bring game data from the owner's install to this Mac: a whole file, or only some byte ranges.

    tools/gamedata/fetch.py PATH                       the whole file
    tools/gamedata/fetch.py PATH OFF+LEN [OFF+LEN ...] these bytes (decimal or 0x..), each range
                                                       rounded outwards to whole blocks
    tools/gamedata/fetch.py --open PATH [PATH ...]     the file in its place at its true size
                                                       with none of its bytes (what an open, a
                                                       size or a listing needs)
    tools/gamedata/fetch.py --open-matching 'DIR/PAT'  --open for every install entry of DIR
                                                       whose name matches PAT (* only) and is
                                                       not here
    tools/gamedata/fetch.py --manifest                 the install's listing (names and sizes)
                                                       and its directory tree
    tools/gamedata/fetch.py --requests FILE            many at once: lines "PATH<TAB>SPEC", SPEC
                                                       being whole, open or OFF+LEN[,OFF+LEN...]
    tools/gamedata/fetch.py --status [--files]         what this Mac holds
    tools/gamedata/fetch.py --verify                   every map against its file (sizes; no
                                                       bytes are compared)

The runtime prints one of the first five forms as the last line of its stop when the game
asks for bytes that are not here (runtime/core/file_partial.c, whose first comment has the
format of the map); tools/gamedata/run_fetch.sh runs the game, obeys that line and runs again.

WHAT IS WHERE.  GAME is the real path of game/ (one directory shared by every worktree) and
MAP is GAME.partial beside it.  EVERY FILE UNDER GAME IS WHOLE.  A PARTIAL file is not there:
its bytes are the sparse file MAP/files/<key> of the true size and MAP/ranges/<key>.ranges
says which of them are the install's (<key>: the install path in lower case).  Only a runtime
that knows the map (runtime/core/file_partial.c) finds a partial file; any other sees a file
that is not there, never zeros.  MAP/manifest.tsv is the install's listing; MAP/fetch.log has
one line per fetch; MAP/staging holds a new whole file until it is complete; MAP/lock makes
one fetch wait for another; MAP/source/<key>.source holds the size and modification time a
partial file had on the other machine at its first fetch.

RULES.
  - The other machine is only read: `find` for the listing and `dd` for bytes, one command at
    a time, under `nice`.  Nothing is written or started there.
  - A whole file that is here is never overwritten, truncated or deleted.
  - Bytes first, map second: a range is added to the map only after its bytes are in the
    data file and flushed to the disk (F_FULLFSYNC: this house loses power), and the ranges
    file is replaced by a rename.  A data file with no ranges file is nothing to anybody.
    A new WHOLE file is made complete in MAP/staging and then linked into GAME (never over
    another).  A partial file whose last range arrives is linked into GAME the same way and
    its map is removed after that.  Stopped anywhere, no state says that a byte is here
    which is not.
  - A file that changed on the other machine is not mixed: its size and modification time
    there are asked before the first byte and after the last of every transfer, compared with
    the manifest's size and with the record of its first fetch, and a difference ends the
    fetch with status 5 naming the file (Store.check_source says what this does not catch).
    Over ssh that is two more commands for each file fetched.
  - Blocks: a range is rounded outwards to --block (1 MiB).  The game reads its archives in
    pieces of 64 KiB that follow one another (docs/trace/files-start.md: 9,928 reads of
    exactly 65,536 bytes each for one streaming archive), every stop costs a run of the game
    and a connection, and 1 MiB is 16 such reads for at most 1 MiB of waste at each end.
  - Holes are punched.  This volume (APFS) fills a gap of less than about 16 MiB between two
    writes with allocated zeros (measured: 1 MiB written at 0 and at 8 MiB of an empty 1 GiB
    file allocates 9 MiB), so after each range is written the gaps beside it are given back
    with F_PUNCHHOLE: a partial file occupies what the map says it has, to within a block.
  - Small files whole: a file of at most --small (16 MiB), and every .dll (the loader and the
    lifter read those without the map), is fetched whole whatever was asked.
  - The floor: nothing is fetched that would leave this disk less than --floor-gb (30) free;
    exit status 3.  The free space is asked before every transfer.
  - Titanfall2.exe and OriginSDK.dll are not fetched (swarm/briefs/_round3-rules.md); status 4.
  - Path case: a path is looked up in the manifest without regard to ASCII case, as the
    runtime does, and the install's spelling is used (r2/paks/Win64).
  - Names are data from the other machine (bad_name): a name of the listing or of the
    manifest with a control character, a backslash, a leading /, or an empty, "." or ".."
    component is refused when the manifest is made (status 5, no manifest written) and when
    it is read (status 6); the listing's lines end at a newline and at nothing else.  Every
    path under GAME and MAP is built from checked components.
  - What goes to ssh: --source HOST:DIR takes a host that is a name ([user@]host: it cannot
    begin with "-") and an absolute DIR; ssh gets its options, then "--", the host, and one
    command in which every path is a quoted word (after "if=" for dd, after "--" for stat).
  - One block size per file: over ssh a segment is read in whole blocks, so bytes that do
    not begin on a block of this fetch (a map made with a smaller --block) are refused with
    status 6 before anything is read; the message names a block that works.

  - --whole-max-gb G: whole files only.  Whatever is asked of a file, the whole file is
    fetched if it is at most G GiB, and a larger one is refused (status 7): for as long as
    partial files are not wanted on this Mac.  --open-matching is refused the same way if
    the files it names are more than G GiB together.

Exit status: 0 done (also "already here"), 2 usage or a path the install does not have,
3 the floor, 4 a file that is not fetched by rule, 5 the source failed or disagrees with the
manifest, 6 this Mac's state is inconsistent (a map that does not fit its file), 7 a file
larger than --whole-max-gb.
"""
import argparse
import errno
import fcntl
import os
import re
import shlex
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
SSH = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=15']
NEVER = ('titanfall2.exe', 'originsdk.dll')
MIB = 1 << 20
F_PUNCHHOLE = getattr(fcntl, 'F_PUNCHHOLE', 99 if sys.platform == 'darwin' else None)   # 99: <sys/fcntl.h> of macOS; no such command on Linux
F_FULLFSYNC = getattr(fcntl, 'F_FULLFSYNC', None)    # the same
FS_BLOCK = 4096
SEGMENTS_PER_COMMAND = 64


def ref_setting(key, env):
    """The reference PC's setting: $env, then $TF_REF_<KEY>, then the line KEY=... of .ref-host at the
    checkout's root (git-ignored: each owner names their own machine there).  '' if none."""
    for name in (env, 'TF_REF_' + key.upper()):
        if name and os.environ.get(name):
            return os.environ[name]
    try:
        with open(os.path.join(REPO, '.ref-host'), encoding='utf-8') as f:
            for line in f:
                k, _, v = line.strip().partition('=')
                if k.strip() == key and v.strip():
                    return v.strip()
    except OSError:
        pass
    return ''


class Fail(Exception):
    def __init__(self, status, text):
        super().__init__(text)
        self.status = status


def full_sync(fd):
    """The bytes of `fd` on the disk itself: F_FULLFSYNC where it exists (macOS, where fsync
    stops at the drive's cache), fsync elsewhere (Linux, where the self-test also runs)."""
    if F_FULLFSYNC is not None:
        fcntl.fcntl(fd, F_FULLFSYNC)
    else:
        os.fsync(fd)


def bad_name(path):
    """Why install path `path` (a line of the listing or of the manifest) cannot be kept, or
    None.  A name is data from the other machine: it becomes part of paths under GAME and MAP
    and of commands, so nothing in it may leave those directories or end a line anywhere."""
    if not path:
        return 'an empty name'
    if path.startswith('/') or '\\' in path:
        return 'a name that begins with / or has a backslash'
    for c in path:
        if ord(c) < 0x20 or 0x7f <= ord(c) < 0xa0 or c in '\u2028\u2029':
            return 'a control character (%r) in a name' % c
    for part in path.split('/'):
        if part in ('', '.', '..'):
            return 'a component %r in a name' % part
    return None


def parse_listing(raw):
    """(kind, size, path) of every line of the listing `raw` (bytes: kind TAB size TAB path).
    Lines end at a newline and at nothing else: a carriage return, a form feed or another
    character that str.splitlines takes for a line end stays inside its name, where bad_name
    refuses it.  A line that is not of the three fields ends the listing with status 5."""
    lines = raw.decode('utf-8', errors='surrogateescape').split('\n')
    if lines and lines[-1] == '':
        lines.pop()
    for line in lines:
        fields = line.split('\t', 2)
        if len(fields) != 3 or fields[0] not in ('f', 'd') or not fields[1].isascii() or not fields[1].isdigit():
            raise Fail(5, 'the listing has a line that is not "f or d, a size, a name": %r' % line[:200])
        yield fields[0], 0 if fields[0] == 'd' else int(fields[1]), fields[2]


def fold(path):
    return ''.join(chr(ord(c) + 32) if 'A' <= c <= 'Z' else c for c in path)


def human(n):
    for unit, size in (('GiB', 1 << 30), ('MiB', 1 << 20), ('KiB', 1 << 10)):
        if n >= size:
            return '%.1f %s' % (n / size, unit)
    return '%d B' % n


# ---- ranges: sorted lists of [lo, hi) that neither overlap nor touch ---------------------------

def merge(ranges):
    out = []
    for lo, hi in sorted(r for r in ranges if r[1] > r[0]):
        if out and lo <= out[-1][1]:
            out[-1][1] = max(out[-1][1], hi)
        else:
            out.append([lo, hi])
    return [tuple(r) for r in out]


def subtract(want, have):
    """The parts of `want` that are not in `have` (both merged)."""
    out = []
    for lo, hi in want:
        for a, b in have:
            if b <= lo:
                continue
            if a >= hi:
                break
            if a > lo:
                out.append((lo, a))
            lo = max(lo, b)
            if lo >= hi:
                break
        if lo < hi:
            out.append((lo, hi))
    return out


def total(ranges):
    return sum(hi - lo for lo, hi in ranges)


# ---- the sources -------------------------------------------------------------------------------

def gcd_block(segments, block):
    """The largest block size on which every segment begins (for the refusal's advice)."""
    import math
    g = block
    for k, (lo, hi) in enumerate(segments):
        g = math.gcd(g, lo)
        if k + 1 < len(segments):
            g = math.gcd(g, hi)
    return g


class DirSource:
    """A directory on this machine that stands for the install (tests, and a made-up install)."""

    def __init__(self, root):
        self.root = os.path.realpath(root)
        self.name = self.root

    def listing(self):
        for base, dirs, files in os.walk(self.root):
            dirs.sort()
            rel = os.path.relpath(base, self.root)
            rel = '' if rel == '.' else rel + '/'
            for d in dirs:
                yield 'd', 0, rel + d
            for f in sorted(files):
                yield 'f', os.path.getsize(os.path.join(base, f)), rel + f

    def stat(self, path):
        st = os.stat(os.path.join(self.root, path))
        return st.st_size, int(st.st_mtime)

    def read(self, path, segments, block):
        with open(os.path.join(self.root, path), 'rb') as fp:
            for lo, hi in segments:
                fp.seek(lo)
                left = hi - lo
                while left:
                    piece = fp.read(min(left, MIB))
                    if not piece:
                        break
                    left -= len(piece)
                    yield piece


class SshSource:
    """The owner's install on the other machine, read with find and dd over ssh."""

    def __init__(self, host, root):
        # HOST and DIR are arguments of ssh and of the remote shell: a host is a name
        # ([user@]host, nothing that ssh could read as an option) and the directory is absolute,
        # so that find and dd take it for a path whatever it is called.
        if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.@-]*', host):
            raise Fail(2, '--source: %r is not a host ([user@]name; it cannot begin with - or hold other characters)' % host)
        if not root.startswith('/') or bad_name(root.strip('/') or 'x'):
            raise Fail(2, '--source: the directory on %s must be an absolute path with no .. component or control character, not %r' % (host, root))
        self.host, self.root = host, root.rstrip('/')
        self.name = '%s:%s' % (host, self.root)

    def argv(self, command):
        """The local command line for one remote command: the host stands after `--`, where ssh
        reads no option, and the command is one argument."""
        return SSH + ['--', self.host, command]

    def remote(self, path):
        """`path` of the install as one word of the remote shell: data, never syntax."""
        why = bad_name(path)
        if why:
            raise Fail(6, 'not sent to %s: %s (%r)' % (self.host, why, path))
        return shlex.quote(self.root + '/' + path)

    def listing(self):
        cmd = "nice find %s -mindepth 1 \\( -type f -o -type d \\) -printf '%%y\\t%%s\\t%%P\\n'" % shlex.quote(self.root)
        p = subprocess.run(self.argv(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if p.returncode:
            raise Fail(5, 'the listing failed (ssh status %d): %s' % (p.returncode, p.stderr.decode(errors='replace').strip()))
        return parse_listing(p.stdout)

    def stat(self, path):
        """(size, modification time in whole seconds) of the install's file: GNU stat, or the
        BSD one where the "other machine" is this Mac (the self-test)."""
        remote = self.remote(path)
        cmd = "stat -c '%%s %%Y' -- %s 2>/dev/null || stat -f '%%z %%m' -- %s" % (remote, remote)
        p = subprocess.run(self.argv(cmd), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        fields = p.stdout.decode(errors='replace').split()
        if p.returncode or len(fields) != 2 or not all(f.isdigit() for f in fields):
            raise Fail(5, 'the size and time of %s could not be asked (ssh status %d): %s'
                       % (path, p.returncode, p.stderr.decode(errors='replace').strip()))
        return int(fields[0]), int(fields[1])

    def read(self, path, segments, block):
        """The bytes of `segments` (each begins on a block boundary) in order, one command per
        SEGMENTS_PER_COMMAND of them."""
        remote = self.remote(path)
        # dd is told whole blocks.  A segment that does not begin on a block boundary would be
        # read from another offset than it is written to, and the count of bytes would still
        # come out right (finding 4 of docs/reviews/night-2026-10-11-code-review.md: this was an
        # assert, which python -O removes).  It happens when a file's map was made with a
        # smaller --block than this fetch has.  Refused before any command is sent.
        for k, (lo, hi) in enumerate(segments):
            # (Only the last segment may end inside a block: at the end of the file.)
            if lo % block or lo >= hi or (hi % block and k + 1 < len(segments)):
                raise Fail(6, '%s: the bytes %d to %d do not begin and end on blocks of %d bytes, so they cannot be read with this '
                              '--block: the file\'s map was made with a smaller one; fetch it again with --block %d (or a divisor of '
                              'it). Nothing was read and the map is as it was' % (path, lo, hi, block, gcd_block(segments, block)))
        for i in range(0, len(segments), SEGMENTS_PER_COMMAND):
            parts = []
            for lo, hi in segments[i:i + SEGMENTS_PER_COMMAND]:
                parts.append('nice dd if=%s bs=%d skip=%d count=%d status=none' % (remote, block, lo // block, -(-(hi - lo) // block)))
            p = subprocess.Popen(self.argv(' && '.join(parts)), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                while True:
                    piece = p.stdout.read(MIB)
                    if not piece:
                        break
                    yield piece
            finally:
                p.stdout.close()
                err = p.stderr.read().decode(errors='replace').strip()
                p.stderr.close()
                status = p.wait()
            if status:
                raise Fail(5, 'reading %s failed (ssh status %d): %s' % (path, status, err))


# ---- this Mac ----------------------------------------------------------------------------------

class Store:
    def __init__(self, game, floor, block, small, whole_max=None, out=sys.stdout):
        if not os.path.isdir(game):
            raise Fail(2, 'no game directory at %s' % game)
        self.game = os.path.realpath(game)
        self.map = self.game + '.partial'
        self.floor, self.block, self.small, self.whole_max, self.out = floor, block, small, whole_max, out
        self.manifest = None
        self.lock_fp = None
        self.rows = []              # (path, asked, bytes fetched, state after)

    def say(self, text):
        print(text, file=self.out, flush=True)

    # -- the lock, the log --

    def lock(self):
        if self.lock_fp:
            return
        os.makedirs(os.path.join(self.map, 'ranges'), exist_ok=True)
        os.makedirs(os.path.join(self.map, 'staging'), exist_ok=True)
        os.makedirs(os.path.join(self.map, 'files'), exist_ok=True)
        self.lock_fp = open(os.path.join(self.map, 'lock'), 'w')
        fcntl.flock(self.lock_fp, fcntl.LOCK_EX)
        for name in os.listdir(os.path.join(self.map, 'staging')):     # what a stopped fetch left
            os.unlink(os.path.join(self.map, 'staging', name))

    def log(self, path, asked, fetched, why):
        with open(os.path.join(self.map, 'fetch.log'), 'a') as fp:
            fp.write('%s\t%s\t%s\t%d\t%s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), path, asked, fetched, why.replace('\t', ' ').replace('\n', ' ')))

    def free(self):
        st = os.statvfs(self.game)
        return st.f_bavail * st.f_frsize

    # -- the manifest --

    def manifest_path(self):
        return os.path.join(self.map, 'manifest.tsv')

    def load_manifest(self):
        if self.manifest is not None:
            return True
        try:
            fp = open(self.manifest_path(), encoding='utf-8', errors='surrogateescape')
        except FileNotFoundError:
            return False
        with fp:
            lines = fp.read().split('\n')
        if lines[0] != 'gamedata-manifest 1' or lines[-1] != '':
            raise Fail(6, '%s is not a manifest' % self.manifest_path())
        manifest = {}
        for n, line in enumerate(lines[1:-1], 2):
            fields = line.split('\t', 2)
            why = None if len(fields) == 3 and fields[0] in ('f', 'd') and fields[1].isascii() and fields[1].isdigit() else 'not "f or d, a size, a name"'
            why = why or bad_name(fields[2])
            if why:
                raise Fail(6, '%s line %d: %s (%r): not used, nothing fetched; make it again with --manifest'
                           % (self.manifest_path(), n, why, line[:200]))
            manifest[fold(fields[2])] = (fields[0], int(fields[1]), fields[2])
        self.manifest = manifest
        return True

    def write_manifest(self, source):
        self.lock()
        entries = sorted(source.listing(), key=lambda e: fold(e[2]))
        if not any(kind == 'f' for kind, _, _ in entries):
            raise Fail(5, 'the listing of %s has no file' % source.name)
        for kind, size, path in entries:
            why = bad_name(path) if kind in ('f', 'd') else 'neither a file nor a directory'
            if why:
                raise Fail(5, 'the listing of %s has an entry this tool cannot keep: %s (%r); no manifest was written' % (source.name, why, path))
        tmp = self.manifest_path() + '.new'
        with open(tmp, 'w', encoding='utf-8', errors='surrogateescape') as fp:
            fp.write('gamedata-manifest 1\n')
            for kind, size, path in entries:
                fp.write('%s\t%d\t%s\n' % (kind, size, path))
            fp.flush()
            full_sync(fp.fileno())
        os.replace(tmp, self.manifest_path())
        self.manifest = None
        self.load_manifest()
        made = 0
        for kind, size, path in entries:
            if kind == 'd':
                host = self.host_path(path)
                if not os.path.isdir(host):
                    os.makedirs(host)
                    made += 1
        files = [e for e in entries if e[0] == 'f']
        self.say('manifest: %d files, %s, and %d directories in %s; %d directories made here'
                 % (len(files), human(sum(e[1] for e in files)), len(entries) - len(files), source.name, made))

    def entry(self, path):
        e = self.manifest.get(fold(path.replace('\\', '/').strip('/')))
        if not e:
            raise Fail(2, 'the install has no %s (a miss stays a miss: nothing to fetch)' % path)
        return e

    # -- paths --

    def host_path(self, path):
        """Where install path `path` is, or belongs, under GAME: each component as it is spelled
        here if it exists in any case (a case-sensitive volume), else as the install spells it."""
        host = self.game
        for part in self.parts(path):
            exact = os.path.join(host, part)
            if not os.path.lexists(exact):
                try:
                    same = [n for n in os.listdir(host) if fold(n) == fold(part)]
                except OSError:
                    same = []
                if same:
                    exact = os.path.join(host, same[0])
            host = exact
        return host

    def parts(self, path):
        """The components of install path `path`.  Every path under GAME and MAP is built from
        these, so a name that could leave those directories ends the fetch here, whatever let
        it through (the manifest refuses such names when it is made and when it is read)."""
        why = bad_name(path)
        if why:
            raise Fail(6, 'not used as a path on this Mac: %s (%r)' % (why, path))
        return path.split('/')

    def ranges_path(self, path):
        return os.path.join(self.map, 'ranges', *self.parts(fold(path))) + '.ranges'

    def data_path(self, path):
        return os.path.join(self.map, 'files', *self.parts(fold(path)))

    def source_path(self, path):
        return os.path.join(self.map, 'source', *self.parts(fold(path))) + '.source'

    def check_source(self, source, path, size):
        """The install's file as it is now on the other machine, against the manifest and against
        what was recorded at the first fetch of this file: (size, mtime), or Fail(5) naming the
        file that changed.  Asked before the first byte of every transfer and again after the
        last (fetch).  The record, MAP/source/<key>.source, is written before any range of the
        file is taken into the map and removed when the file is whole under GAME.

        WHAT THIS DOES NOT CATCH: a change that keeps both the size and the modification time
        (whole seconds); a change between the manifest and the first fetch that keeps the size
        (the record begins at the first fetch); a partial file fetched before this record
        existed (its record begins at its next fetch, and says so); bytes are never compared."""
        now = source.stat(path)
        if now[0] != size:
            raise Fail(5, '%s CHANGED ON THE OTHER MACHINE: it has %d bytes there now and %d in the manifest; nothing fetched '
                          '(make the manifest again with --manifest; ranges of the old file that are here are not the new file\'s)'
                       % (path, now[0], size))
        try:
            with open(self.source_path(path)) as fp:
                lines = fp.read().split('\n')
            then = (int(lines[1][5:]), int(lines[2][6:])) if lines[0] == 'gamedata-source 1' and lines[1].startswith('size ') and lines[2].startswith('mtime ') else None
        except FileNotFoundError:
            return now, False
        except (IndexError, ValueError):
            then = None
        if then is None:
            raise Fail(6, 'the record %s does not parse' % self.source_path(path))
        if then != now:
            raise Fail(5, '%s CHANGED ON THE OTHER MACHINE since its first fetch here: then %d bytes, modified at %d; now %d bytes, modified '
                          'at %d. Nothing fetched: the ranges that are here are the old file\'s. Remove its map (%s, %s, %s) and fetch it again'
                       % (path, then[0], then[1], now[0], now[1], self.ranges_path(path), self.data_path(path), self.source_path(path)))
        return now, True

    def record_source(self, path, stat):
        final = self.source_path(path)
        os.makedirs(os.path.dirname(final), exist_ok=True)
        with open(final + '.new', 'w') as fp:
            fp.write('gamedata-source 1\nsize %d\nmtime %d\npath %s\n' % (stat[0], stat[1], path))
            fp.flush()
            full_sync(fp.fileno())
        os.replace(final + '.new', final)

    def drop_partial(self, path):
        """The file is whole under GAME: its map goes first, then its bytes beside the tree."""
        for gone in (self.ranges_path(path), self.data_path(path), self.source_path(path)):
            try:
                os.unlink(gone)
            except FileNotFoundError:
                pass

    def read_ranges(self, path):
        """(size, ranges) of the partial file `path`, or None if it has no ranges file."""
        try:
            with open(self.ranges_path(path)) as fp:
                lines = fp.read().split('\n')
        except FileNotFoundError:
            return None
        try:
            ok = lines[0] == 'gamedata-ranges 1' and lines[1].startswith('path ') and lines[2].startswith('size ') \
                and lines[-1] == '' and lines[-2].startswith('end ')
            size = int(lines[2][5:])
            ranges = [tuple(int(x) for x in line.split(' ')) for line in lines[3:-2]]
            ok = ok and int(lines[-2][4:]) == len(ranges) and ranges == merge(ranges) and all(hi <= size for _, hi in ranges)
        except (IndexError, ValueError):
            ok = False
        if not ok:
            raise Fail(6, 'the map %s does not parse' % self.ranges_path(path))
        return size, ranges

    def write_ranges(self, path, size, ranges):
        final = self.ranges_path(path)
        os.makedirs(os.path.dirname(final), exist_ok=True)
        with open(final + '.new', 'w') as fp:
            fp.write('gamedata-ranges 1\npath %s\nsize %d\n' % (path, size))
            for lo, hi in ranges:
                fp.write('%d %d\n' % (lo, hi))
            fp.write('end %d\n' % len(ranges))
            fp.flush()
            full_sync(fp.fileno())
        os.replace(final + '.new', final)

    # -- one file --

    def wanted(self, path, size, spec):
        """The bytes to have after this fetch, from what was asked: 'whole', 'open' or ranges."""
        if self.whole_max is not None:
            if size > self.whole_max:
                raise Fail(7, '%s is %s, more than --whole-max-gb allows (%s), and only whole files are fetched in this mode'
                           % (path, human(size), human(self.whole_max)))
            spec = 'whole'
        if spec == 'whole' or size <= self.small or fold(path).endswith('.dll'):
            return [(0, size)] if size else []
        if spec == 'open':
            return []
        b = self.block
        return merge((lo // b * b, min(size, -(-hi // b) * b)) for lo, hi in spec if lo < size)

    def fetch(self, source, path, spec, why=''):
        self.lock()
        kind, size, path = self.entry(path)
        asked = spec if isinstance(spec, str) else ','.join('0x%x+0x%x' % (lo, hi - lo) for lo, hi in spec)
        host = self.host_path(path)
        if kind == 'd':
            os.makedirs(host, exist_ok=True)
            self.rows.append((path + '/', asked, 0, 'a directory'))
            return
        if fold(os.path.basename(path)) in NEVER:
            raise Fail(4, '%s is not fetched: swarm/briefs/_round3-rules.md, "Three things nobody does"' % path)
        data = self.data_path(path)
        if os.path.lexists(host):                               # under GAME: whole, never touched
            have = os.path.getsize(host)
            note = 'already here, whole' if have == size else \
                'ALREADY HERE WITH %d BYTES WHERE THE INSTALL HAS %d: not touched, look at it' % (have, size)
            if have == size:
                self.drop_partial(path)                         # what a fetch stopped after its link left
            self.rows.append((path, asked, 0, note))
            return
        known = self.read_ranges(path)
        if known and (known[0] != size or not os.path.isfile(data) or os.path.getsize(data) != size):
            raise Fail(6, '%s: the map says %d bytes, its data file has %d, the manifest %d'
                       % (path, known[0], os.path.getsize(data) if os.path.isfile(data) else -1, size))
        present = known[1] if known else []
        want = merge(list(present) + self.wanted(path, size, spec))
        need = subtract(want, present)
        free = self.free()
        if total(need) and free - total(need) < self.floor:
            raise Fail(3, '%s: %s more would leave %s free on this disk, below the floor of %s; nothing fetched'
                       % (path, human(total(need)), human(free - total(need)), human(self.floor)))
        whole = want == [(0, size)] or size == 0
        if known:
            target = data
            fd = os.open(data, os.O_RDWR)
        else:
            # New here.  A whole file is made in staging; a partial one where its bytes stay,
            # which means nothing to anybody until its ranges file exists.
            target = os.path.join(self.map, 'staging', '%d.part' % os.getpid()) if whole else data
            os.makedirs(os.path.dirname(target), exist_ok=True)
            fd = os.open(target, os.O_RDWR | os.O_CREAT | os.O_TRUNC, 0o644)
            os.ftruncate(fd, size)
        try:
            # The file on the other machine, before the first byte and after the last (finding 5
            # of docs/reviews/night-2026-10-11-code-review.md): a file that changed since its
            # first fetch here, or while it was read, is not mixed into the map.
            before, recorded = self.check_source(source, path, size) if need else (None, True)
            got = self.transfer(source, path, need, fd, want, size)
            full_sync(fd)
            if need and source.stat(path) != before:
                raise Fail(5, '%s CHANGED ON THE OTHER MACHINE while it was read: nothing of this fetch is taken into the map' % path)
        finally:
            os.close(fd)
        note = ''
        if need and not whole:
            if known and not recorded:
                note = ' (its size and time on the other machine are recorded from this fetch on: the ranges fetched before are not covered)'
            self.record_source(path, before)
        if whole:
            os.makedirs(os.path.dirname(host), exist_ok=True)
            try:
                os.link(target, host)                           # complete, and never over another
            except FileExistsError:
                note = ' (a file appeared here during the fetch: left as it is)'
            if known:
                self.drop_partial(path)
            else:
                os.unlink(target)
        else:
            self.write_ranges(path, size, want)                 # the map says what is here
        self.log(path, asked, got, why)
        state = 'whole, %s' % human(size) if whole else 'partial: %d ranges, %s of %s' % (len(want), human(total(want)), human(size))
        self.rows.append((path, asked, got, state + note))

    def punch(self, fd, want, size, i):
        """Give back the gaps before and after want[i]: bytes no map claims (see RULES, holes)."""
        gaps = [(want[i - 1][1] if i else 0, want[i][0]), (want[i][1], want[i + 1][0] if i + 1 < len(want) else size)]
        for lo, hi in gaps:
            lo, hi = -(-lo // FS_BLOCK) * FS_BLOCK, hi // FS_BLOCK * FS_BLOCK
            if hi > lo and F_PUNCHHOLE is not None:
                try:
                    fcntl.fcntl(fd, F_PUNCHHOLE, struct.pack('IIqq', 0, 0, lo, hi - lo))
                except OSError:
                    pass                # a volume that cannot: the file is then larger on disk, and still right

    def transfer(self, source, path, need, fd, want, size):
        if not need:
            return 0
        got = 0
        it = iter(need)
        lo, hi = next(it)
        at = lo
        where = 0                       # the index in `want` of the range that holds the segment being written

        def done_segment():
            nonlocal where
            while want[where][1] < hi:
                where += 1
            self.punch(fd, want, size, where)

        for piece in source.read(path, need, self.block):
            while piece:
                if at == hi:
                    done_segment()
                    try:
                        lo, hi = next(it)
                    except StopIteration:
                        raise Fail(5, '%s: the source sent more than was asked' % path)
                    at = lo
                n = min(len(piece), hi - at)
                os.pwrite(fd, piece[:n], at)
                at += n
                got += n
                piece = piece[n:]
        if got != total(need):
            raise Fail(5, '%s: the source sent %d bytes of %d: nothing is taken into the map' % (path, got, total(need)))
        done_segment()
        return got

    # -- reports --

    def table(self):
        if not self.rows:
            return
        w = max(len(r[0]) for r in self.rows)
        for path, asked, got, state in self.rows:
            self.say('  %-*s  asked %-24s fetched %-10s now %s' % (w, path, asked[:60], human(got), state))
        self.say('  %d entries, %s fetched; %s free on this disk (floor %s)'
                 % (len(self.rows), human(sum(r[2] for r in self.rows)), human(self.free()), human(self.floor)))

    def partial_files(self):
        root = os.path.join(self.map, 'ranges')
        for base, _, files in os.walk(root):
            for f in sorted(files):
                if f.endswith('.ranges'):
                    with open(os.path.join(base, f)) as fp:
                        fp.readline()
                        path = fp.readline().rstrip('\n')[5:]
                    size, ranges = self.read_ranges(path)
                    yield path, size, ranges

    def status(self, files=False):
        whole_n = whole_bytes = 0
        partial = {fold(p): (p, s, r) for p, s, r in self.partial_files()}
        for base, _, names in os.walk(self.game):
            for n in names:
                host = os.path.join(base, n)
                if os.path.isfile(host):
                    whole_n += 1
                    whole_bytes += os.path.getsize(host)
        present = sum(total(r) for _, _, r in partial.values())
        holes = sum(s for _, s, _ in partial.values()) - present
        on_disk = 0
        for p, _, _ in partial.values():
            try:
                on_disk += os.stat(self.data_path(p)).st_blocks * 512
            except OSError:
                pass
        if files:
            for path, size, ranges in sorted(partial.values()):
                self.say('  %-60s %5d ranges %12s of %12s' % (path, len(ranges), human(total(ranges)), human(size)))
        have_manifest = self.load_manifest()
        install = [e for e in self.manifest.values() if e[0] == 'f'] if have_manifest else []
        self.say('game data under %s' % self.game)
        self.say('  whole files    %6d  %s' % (whole_n, human(whole_bytes)))
        self.say('  partial files  %6d  %s present, %s of holes; they occupy %s of this disk' % (len(partial), human(present), human(holes), human(on_disk)))
        if have_manifest:
            self.say('  the install    %6d  %s (the manifest)' % (len(install), human(sum(e[1] for e in install))))
        else:
            self.say('  the install    no manifest yet: tools/gamedata/fetch.py --manifest')
        self.say('  this disk      %s free; the floor is %s' % (human(self.free()), human(self.floor)))

    def verify(self):
        bad = 0
        for path, size, ranges in self.partial_files():
            data = self.data_path(path)
            have = os.path.getsize(data) if os.path.isfile(data) else -1
            want = self.manifest.get(fold(path), (None, -1, None))[1] if self.manifest else size
            if have != size or want != size:
                bad += 1
                self.say('  BAD %s: the map says %d bytes, its data file has %d, the manifest %d' % (path, size, have, want))
            if os.path.lexists(self.host_path(path)):
                self.say('  note %s is under the game directory as well (whole): the next fetch of it tidies up' % path)
        self.say('verify: %d maps do not fit their files' % bad if bad else 'verify: every map fits its file')
        return bad


def parse_range(text):
    try:
        off, length = text.split('+')
        lo, n = int(off, 0), int(length, 0)
        if lo < 0 or n < 0:
            raise ValueError
        return (lo, lo + n)
    except ValueError:
        raise Fail(2, 'a range is OFFSET+LENGTH (decimal or 0x..), not %r' % text)


def glob_match(pat, name):
    """The runtime's FindFirstFile match: * is any run of characters, ASCII case is ignored."""
    if pat == '*.*':
        return True
    rx = ''.join('.*' if c == '*' else re.escape(c) for c in fold(pat))
    return re.fullmatch(rx, fold(name), re.S) is not None


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0], usage='see the first lines of %(prog)s')
    ap.add_argument('args', nargs='*')
    ap.add_argument('--open', action='store_true')
    ap.add_argument('--open-matching', metavar='DIR/PATTERN')
    ap.add_argument('--manifest', action='store_true')
    ap.add_argument('--requests', metavar='FILE')
    ap.add_argument('--status', action='store_true')
    ap.add_argument('--files', action='store_true')
    ap.add_argument('--verify', action='store_true')
    ap.add_argument('--source', default='ssh', help='"ssh" (the install named in .ref-host), HOST:DIR, or a directory on this machine')
    ap.add_argument('--game', default=os.path.join(REPO, 'game'), help='the install root on this Mac (default: game/ of this checkout)')
    ap.add_argument('--floor-gb', type=float, default=30.0)
    ap.add_argument('--block', type=int, default=MIB)
    ap.add_argument('--small', type=int, default=16 * MIB)
    ap.add_argument('--whole-max-gb', type=float, default=None)
    ap.add_argument('--why', default='')
    o = ap.parse_args(argv)
    store = None
    try:
        if o.block <= 0 or o.block % 4096:
            raise Fail(2, '--block is a positive multiple of 4096')
        store = Store(o.game, int(o.floor_gb * (1 << 30)), o.block, o.small,
                      None if o.whole_max_gb is None else int(o.whole_max_gb * (1 << 30)))
        if o.status:
            store.status(o.files)
            return 0
        if o.source == 'ssh':
            host, root = ref_setting('host', None), ref_setting('game', None)
            if not host or not root:
                raise Fail(2, '--source ssh needs the reference PC: host=user@host and game=/its/Titanfall2 in .ref-host (or TF_REF_HOST, TF_REF_GAME)')
            source = SshSource(host, root)
        elif ':' in o.source and not os.path.isdir(o.source):
            source = SshSource(*o.source.split(':', 1))
        else:
            source = DirSource(o.source)
        if o.manifest or not store.load_manifest():
            store.write_manifest(source)
        if o.verify:
            return 6 if store.verify() else 0
        jobs = []
        if o.requests:
            with open(o.requests) as fp:
                for line in fp:
                    line = line.rstrip('\n')
                    if not line or line.startswith('#'):
                        continue
                    path, spec = line.split('\t')[:2]
                    jobs.append((path, spec if spec in ('whole', 'open') else merge(parse_range(r) for r in spec.split(','))))
        elif o.open_matching:
            d, _, pat = o.open_matching.replace('\\', '/').rpartition('/')
            prefix = fold(d) + '/' if d else ''
            for key, (kind, size, path) in sorted(store.manifest.items()):
                if key.startswith(prefix) and '/' not in key[len(prefix):] and glob_match(pat, path[len(prefix):]) \
                        and not os.path.lexists(store.host_path(path)) and not os.path.exists(store.ranges_path(path)):
                    jobs.append((path, 'open'))
            if store.whole_max is not None:
                # Whole files only: a listing may name hundreds of files.  All of them together
                # are held to the limit that one file is held to.
                all_of_it = sum(store.entry(p)[1] for p, _ in jobs)
                if all_of_it > store.whole_max:
                    raise Fail(7, '%s matches %d files that are not here, %s together, more than --whole-max-gb allows (%s) for one request'
                               % (o.open_matching, len(jobs), human(all_of_it), human(store.whole_max)))
        elif o.open:
            if not o.args:
                raise Fail(2, '--open needs a path')
            jobs = [(p, 'open') for p in o.args]
        elif o.args:
            jobs = [(o.args[0], merge(parse_range(r) for r in o.args[1:]) if o.args[1:] else 'whole')]
        elif not o.manifest:
            raise Fail(2, 'nothing asked: a path, --open, --open-matching, --manifest, --requests, --status or --verify')
        for path, spec in jobs:
            store.fetch(source, path, spec, o.why)
        store.table()
        return 0
    except Fail as f:
        if store:
            store.table()
        print('fetch.py: %s' % f, file=sys.stderr)
        return f.status
    except OSError as e:
        if e.errno == errno.ENOSPC:
            print('fetch.py: this disk is full: %s' % e, file=sys.stderr)
            return 3
        raise


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
