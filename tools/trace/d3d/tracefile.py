#!/usr/bin/env python3
"""tracefile: our own reader of apitrace's binary trace file, and what is taken out of one.

    tracefile.py selftest
    tracefile.py check TRACE [--script DIR] [--clean DIR]
                                    every call of TRACE held against the converted script files
                                    (tools/trace/d3d/convert.sh) and the cleaned call lines they
                                    were made from: call number, function, sizes of the blobs
    tracefile.py extract TRACE OUT [--until CALL]
                                    every blob of TRACE as a file OUT/blobs/<2 hex>/<sha256>, once
                                    per distinct content; OUT/index.tsv, OUT/shaders.tsv,
                                    OUT/frames.tsv, OUT/summary.json (see `extract` below)

A TRACE IS DERIVED FROM THE GAME.  Nothing here prints a value of it: no string, no byte of a
blob.  What is printed is counts, sizes, call numbers, hashes and the names of functions and
parameters, which are apitrace's descriptions of Direct3D and not the game's.  OUT belongs
under build/trace/data/ (git-ignored) and nowhere else.

THE FORMAT, written from memory of apitrace's lib/trace/trace_format.hpp and trace_parser.cpp
(public; version 14.0 made the recording).  It was not looked up: `check` is the proof, and
what it found is in docs/handoff/pic-data.md.

  container   unsnap.c says it: "at", then length-prefixed raw Snappy blocks.
  uint        7 bits a byte, low bits first, the top bit says another byte follows.
  string      uint length, then the bytes (no terminator).
  stream      uint version; from version 6 on: uint semantic version, then properties (string
              name, string value) up to an empty name; then events to the end.  (I remembered a
              count and that many bytes of padding after the properties.  The recording has
              none: its first event follows the empty name at once.)
  event       byte 0 ENTER: uint thread, function signature, details.  The call's number is
                  how many ENTERs came before it.
              byte 1 LEAVE: uint call number, details.
  details     byte 0 END | 1 ARG uint index, value | 2 RET value | 3 (thread; never written) |
              4 BACKTRACE uint n, n frames | 5 FLAGS uint (bit 0: a call the recorder made up,
              such as memcpy)
  signature   uint id (any number: the recorder's own, not a count), and where that id occurs
              for the first time, its body:
                  function: string name, uint n, n strings (parameter names)
                  structure: string name, uint n, n strings (member names)
                  enumeration: uint n, n times (string name, signed value)
                  bitmask: uint n, n times (string name, uint value)
                  backtrace frame: bytes 1 module, 2 function, 3 file (strings), 4 line,
                      5 offset (uints), up to a byte 0
  signed      byte 3 (the uint that follows, negated) or byte 4 (the uint that follows)
  value       byte 0 NULL | 1 FALSE | 2 TRUE | 3 negative: uint | 4 uint | 5 float (4 bytes) |
              6 double (8 bytes) | 7 string | 8 blob: uint size, the bytes | 9 enumeration:
              signature, signed value | 10 bitmask: signature, uint | 11 array: uint n, n values |
              12 structure: signature, one value per member | 13 pointer: uint | 14 two values,
              one for people and one for machines (a shader: its disassembly as a string,
              then its bytecode as a blob) | 15 wide string: uint n, n uints

A call is complete at its LEAVE and is handed out then, as apitrace's own reader does (so
`apitrace dump`, and the script files made from one, are in that order).  When the stream
ends, the calls still open are handed out as incomplete, oldest first; a LEAVE the stream
ends inside is dropped with its call.
"""
import sys, os, re, struct, subprocess, hashlib, gzip, glob, json, time, tempfile, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))

(T_NULL, T_FALSE, T_TRUE, T_SINT, T_UINT, T_FLOAT, T_DOUBLE, T_STRING, T_BLOB, T_ENUM, T_BITMASK,
 T_ARRAY, T_STRUCT, T_OPAQUE, T_REPR, T_WSTRING) = range(16)
EVENT_ENTER, EVENT_LEAVE = 0, 1
CALL_END, CALL_ARG, CALL_RET, CALL_THREAD, CALL_BACKTRACE, CALL_FLAGS = range(6)
FLAG_FAKE = 1


class FormatError(Exception):
    pass


class Ptr(int):
    """A pointer as recorded (TYPE_OPAQUE)."""
    __slots__ = ()


class Enum:
    __slots__ = ('sig', 'value')            # sig: ((name, value), ...)

    def __init__(self, sig, value):
        self.sig, self.value = sig, value


class Bitmask:
    __slots__ = ('sig', 'value')

    def __init__(self, sig, value):
        self.sig, self.value = sig, value


class Struct:
    __slots__ = ('sig', 'members')          # sig: (name, (member names))

    def __init__(self, sig, members):
        self.sig, self.members = sig, members


class Repr:
    __slots__ = ('human', 'machine')

    def __init__(self, human, machine):
        self.human, self.machine = human, machine


class WString:
    __slots__ = ('units',)

    def __init__(self, units):
        self.units = units


class Call:
    """args[i] is None for a parameter the recorder wrote no value for; `late` holds the indices
    written at LEAVE (what the device wrote)."""
    __slots__ = ('no', 'name', 'arg_names', 'args', 'ret', 'thread', 'flags', 'incomplete', 'late')


# ---------------------------------------------------------------------------- the container

def _block_py(data, cut, where):
    """One raw Snappy block -> bytes.  cut: the block is not whole (the file ended inside it):
    stop at the first element that is not whole instead of refusing."""
    n = len(data)
    i = want = shift = 0
    while True:
        if i >= n or shift > 28:
            if cut:
                return b''
            raise FormatError('%s: the block\'s length does not end' % where)
        b = data[i]
        i += 1
        want |= (b & 0x7f) << shift
        if b < 0x80:
            break
        shift += 7
    out = bytearray()
    while i < n:
        tag = data[i]
        kind = tag & 3
        j = i + 1
        if kind == 0:
            ln = tag >> 2
            if ln >= 60:
                k = ln - 59
                if j + k > n:
                    if cut:
                        break
                    raise FormatError('%s: a literal\'s length runs past the block' % where)
                ln = int.from_bytes(data[j:j + k], 'little')
                j += k
            ln += 1
            if ln > n - j:
                if cut:
                    break
                raise FormatError('%s: a literal runs past the block' % where)
            if ln > want - len(out):
                raise FormatError('%s: a literal runs past the declared length' % where)
            out += data[j:j + ln]
            i = j + ln
            continue
        need = (0, 1, 2, 4)[kind]
        if j + need > n:
            if cut:
                break
            raise FormatError('%s: a copy\'s offset runs past the block' % where)
        if kind == 1:
            ln, off = 4 + ((tag >> 2) & 7), (tag >> 5) << 8 | data[j]
        else:
            ln, off = 1 + (tag >> 2), int.from_bytes(data[j:j + need], 'little')
        i = j + need
        o = len(out)
        if off == 0 or off > o:
            raise FormatError('%s: a copy reaches before the block\'s first byte' % where)
        if ln > want - o:
            raise FormatError('%s: a copy runs past the declared length' % where)
        if off >= ln:
            out += out[o - off:o - off + ln]
        else:
            for k in range(ln):
                out.append(out[o - off + k])
    if len(out) != want and not cut:
        raise FormatError('%s: the block holds fewer bytes than it declares' % where)
    return bytes(out)


class PyStream:
    """The container read in Python: slow, and the second implementation unsnap.c is held against."""

    def __init__(self, path):
        self.f = open(path, 'rb')
        if self.f.read(2) != b'at':
            raise FormatError('%s does not begin with "at"' % path)
        self.buf, self.chunk, self.cut, self.done = b'', 0, None, False

    def _more(self):
        head = self.f.read(4)
        if not head:
            self.done = True
            return
        if len(head) != 4:
            raise FormatError('chunk %d: the file ends inside a chunk\'s length' % self.chunk)
        n = int.from_bytes(head, 'little')
        data = self.f.read(n)
        cut = len(data) != n
        out = _block_py(data, cut, 'chunk %d' % self.chunk)
        if cut:
            self.cut, self.done = (self.chunk, len(data), n, len(out)), True
        self.buf += out
        self.chunk += 1

    def read(self, n):
        while len(self.buf) < n and not self.done:
            self._more()
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def close(self):
        self.f.close()
        return self.cut


class CStream:
    """The container read by build/tools/unsnap (built here from unsnap.c when it is older)."""

    def __init__(self, path, exe):
        self.err = tempfile.TemporaryFile()
        self.p = subprocess.Popen([exe, path], stdout=subprocess.PIPE, stderr=self.err, bufsize=1 << 22)
        self.read = self.p.stdout.read

    def close(self):
        self.p.stdout.close()
        status = self.p.wait()
        self.err.seek(0)
        note = self.err.read().decode('ascii', 'replace').strip()
        self.err.close()
        if status not in (0, 3, -13):
            raise FormatError(note or 'unsnap exited with status %d' % status)
        m = re.search(r'chunk (\d+): the file ends inside it: (\d+) of (\d+) bytes of the block gave (\d+)', note)
        return tuple(int(x) for x in m.groups()) if m else None


def build_unsnap(out_dir=None):
    """Path of the built decompressor, or None where no C compiler answers."""
    exe = os.path.join(out_dir or os.path.join(ROOT, 'build', 'tools'), 'unsnap')
    src = os.path.join(HERE, 'unsnap.c')
    if os.path.exists(exe) and os.path.getmtime(exe) >= os.path.getmtime(src):
        return exe
    os.makedirs(os.path.dirname(exe), exist_ok=True)
    try:
        subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-o', exe + '.tmp%d' % os.getpid(), src], check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    os.replace(exe + '.tmp%d' % os.getpid(), exe)
    return exe


def open_trace(path, python=False):
    exe = None if python else build_unsnap()
    return CStream(path, exe) if exe else PyStream(path)


# ---------------------------------------------------------------------------- the stream

class Reader:
    MARGIN = 1 << 20
    CHUNK = 1 << 23

    def __init__(self, stream):
        self.f = stream
        self.buf, self.pos, self.base, self.start, self.eof = b'', 0, 0, 0, False
        self.functions, self.structs, self.enums, self.bitmasks, self.frames = {}, {}, {}, {}, {}
        self.sig_at = {}            # (kind, id) -> where in the stream its body begins
        self.again = False          # the event is being read a second time (the buffer was short)
        self.pending = {}           # the calls entered and not left, by number, oldest first
        self.next_no = 0
        self.cut = False            # the stream ended inside an event
        self.dropped = 0            # calls whose LEAVE the stream ended inside
        self.strays = 0             # LEAVEs of a call that was not open
        self.kinds = [0] * 16       # how many values of each kind were read
        self.properties = {}
        self._fill(0)
        try:
            self.version = self._uint()
            self.semantic = self._uint() if self.version >= 6 else self.version
            if self.version >= 6:
                while True:
                    name = self._string()
                    if not name:
                        break
                    self.properties[name] = self._string()
        except IndexError:
            raise FormatError('the stream ends inside its head')
        if self.version > 6:
            raise FormatError('trace version %d: this reader knows versions up to 6' % self.version)

    def _fill(self, keep_from, want=0):
        more = b'' if self.eof else self.f.read(max(self.CHUNK, want))
        if not more:
            self.eof = True
        self.buf = self.buf[keep_from:] + more
        self.base += keep_from
        self.pos -= keep_from
        self.start -= keep_from

    def _uint(self):
        buf, pos = self.buf, self.pos
        b = buf[pos]
        pos += 1
        if b < 0x80:
            self.pos = pos
            return b
        v, shift = b & 0x7f, 7
        while True:
            b = buf[pos]
            pos += 1
            v |= (b & 0x7f) << shift
            if b < 0x80:
                break
            shift += 7
        self.pos = pos
        return v

    def _sint(self):
        t = self.buf[self.pos]
        self.pos += 1
        if t == T_SINT:
            return -self._uint()
        if t == T_UINT:
            return self._uint()
        raise FormatError('byte %d where a signed value begins, at %d' % (t, self.base + self.pos - 1))

    def _bytes(self, n):
        while len(self.buf) - self.pos < n:
            if self.eof:
                raise IndexError
            if len(self.buf) - self.start > 1 << 31:
                raise FormatError('an event of more than 2 GB at %d' % (self.base + self.start))
            self._fill(self.start, n - (len(self.buf) - self.pos))
        p = self.pos
        self.pos = p + n
        return self.buf[p:p + n]

    def _string(self):
        return self._bytes(self._uint()).decode('latin-1')

    def _known(self, kind, table):
        """(id, signature or None when its body follows here)."""
        i = self._uint()
        sig = table.get(i)
        if sig is None:
            self.sig_at[kind, i] = self.base + self.pos
        elif self.again and self.sig_at[kind, i] == self.base + self.pos:
            sig = None
        return i, sig

    def _function_sig(self):
        i, sig = self._known('f', self.functions)
        if sig is None:
            name = self._string()
            sig = self.functions[i] = (name, tuple(self._string() for _ in range(self._uint())))
        return sig

    def _struct_sig(self):
        i, sig = self._known('s', self.structs)
        if sig is None:
            name = self._string()
            sig = self.structs[i] = (name, tuple(self._string() for _ in range(self._uint())))
        return sig

    def _enum_sig(self):
        i, sig = self._known('e', self.enums)
        if sig is None:
            sig = self.enums[i] = tuple((self._string(), self._sint()) for _ in range(self._uint()))
        return sig

    def _bitmask_sig(self):
        i, sig = self._known('b', self.bitmasks)
        if sig is None:
            sig = self.bitmasks[i] = tuple((self._string(), self._uint()) for _ in range(self._uint()))
        return sig

    def _frame(self):
        i, sig = self._known('t', self.frames)
        if sig is None:
            sig = {}
            while True:
                c = self.buf[self.pos]
                self.pos += 1
                if c == 0:
                    break
                if c in (1, 2, 3):
                    sig[c] = self._string()
                elif c in (4, 5):
                    sig[c] = self._uint()
                else:
                    raise FormatError('byte %d in a backtrace frame, at %d' % (c, self.base + self.pos - 1))
            self.frames[i] = sig
        return sig

    def _value(self):
        pos = self.pos
        t = self.buf[pos]
        self.pos = pos + 1
        self.kinds[t & 15] += 1
        if t == T_UINT:
            return self._uint()
        if t == T_OPAQUE:
            return Ptr(self._uint())
        if t == T_NULL:
            return None
        if t == T_ENUM:
            sig = self._enum_sig()
            return Enum(sig, self._sint())
        if t == T_BITMASK:
            sig = self._bitmask_sig()
            return Bitmask(sig, self._uint())
        if t == T_ARRAY:
            value = self._value
            return [value() for _ in range(self._uint())]
        if t == T_STRUCT:
            sig = self._struct_sig()
            value = self._value
            return Struct(sig, [value() for _ in sig[1]])
        if t == T_FLOAT:
            return struct.unpack('<f', self._bytes(4))[0]
        if t == T_DOUBLE:
            return struct.unpack('<d', self._bytes(8))[0]
        if t == T_SINT:
            return -self._uint()
        if t == T_FALSE:
            return False
        if t == T_TRUE:
            return True
        if t == T_STRING:
            return self._string()
        if t == T_BLOB:
            return self._bytes(self._uint())
        if t == T_REPR:
            human = self._value()
            return Repr(human, self._value())
        if t == T_WSTRING:
            return WString([self._uint() for _ in range(self._uint())])
        raise FormatError('byte %d where a value begins, at %d' % (t, self.base + pos))

    def _details(self, args, late):
        """-> (ret or the marker _NONE, flags)."""
        ret, flags = _NONE, 0
        while True:
            c = self.buf[self.pos]
            self.pos += 1
            if c == CALL_ARG:
                i = self._uint()
                v = self._value()
                if i >= len(args):
                    args.extend([None] * (i + 1 - len(args)))
                args[i] = v
                if late is not None:
                    late.append(i)
            elif c == CALL_END:
                return ret, flags
            elif c == CALL_RET:
                ret = self._value()
            elif c == CALL_FLAGS:
                flags |= self._uint()
            elif c == CALL_BACKTRACE:
                for _ in range(self._uint()):
                    self._frame()
            else:
                raise FormatError('byte %d where a call detail begins, at %d' % (c, self.base + self.pos - 1))

    def _event(self):
        c = self.buf[self.pos]
        self.pos += 1
        if c == EVENT_ENTER:
            call = Call()
            call.thread = self._uint() if self.version >= 4 else 0
            call.name, call.arg_names = self._function_sig()
            call.args, call.late, call.incomplete = [], [], False
            ret, call.flags = self._details(call.args, None)
            call.ret = None if ret is _NONE else ret
            call.no = self.next_no
            self.next_no += 1
            self.pending[call.no] = call
            return None
        if c != EVENT_LEAVE:
            raise FormatError('byte %d where an event begins, at %d' % (c, self.base + self.pos - 1))
        no = self._uint()
        call = self.pending.get(no)
        self.leaving = no if call is not None else None
        args = list(call.args) if call is not None else []
        late = []
        ret, flags = self._details(args, late)
        self.leaving = None
        if call is None:
            self.strays += 1
            return None
        del self.pending[no]
        call.args, call.late = args, late
        call.flags |= flags
        if ret is not _NONE:
            call.ret = ret
        return call

    def calls(self):
        self.leaving = None
        while True:
            if len(self.buf) - self.pos < self.MARGIN and not self.eof:
                self._fill(self.pos)
            self.start = self.pos
            if self.pos >= len(self.buf):
                break
            try:
                call = self._event()
            except IndexError:
                if self.eof:
                    self.cut = True
                    if self.leaving is not None:
                        del self.pending[self.leaving]
                        self.dropped += 1
                    break
                if len(self.buf) - self.start > 1 << 31:
                    raise FormatError('an event of more than 2 GB at %d' % (self.base + self.start))
                self.pos = self.start
                self._fill(self.start, len(self.buf) - self.start)
                self.again = True
                continue
            self.again = False
            if call is not None:
                yield call
        for call in list(self.pending.values()):
            call.incomplete = True
            yield call
        self.pending.clear()


_NONE = object()


def blobs(call):
    """Every blob of a call, in the order of its parameters: (kind, where, bytes).
    kind: 'B' a shader's bytecode (the machine half of a two-valued argument), 'W' the bytes of a
    write into mapped memory (the recorder's memcpy), 'd' what the device wrote (a parameter
    recorded at LEAVE), 'b' any other blob the game passed in."""
    late = call.late
    names = call.arg_names
    write = call.name == 'memcpy'
    for i, v in enumerate(call.args):
        if v is None or v.__class__ is int or v.__class__ is Ptr:
            continue
        name = names[i] if i < len(names) else 'arg%d' % i
        for kind, where, data in _walk(v, name, False):
            yield ('B' if kind else 'W' if write else 'd' if i in late else 'b'), where, data


def _walk(v, where, in_repr):
    c = v.__class__
    if c is bytes:
        yield in_repr, where, v
    elif c is list:
        for k, e in enumerate(v):
            ec = e.__class__
            if ec is bytes or ec is list or ec is Struct or ec is Repr:
                yield from _walk(e, '%s[%d]' % (where, k), in_repr)
    elif c is Struct:
        names = v.sig[1]
        for k, e in enumerate(v.members):
            ec = e.__class__
            if ec is bytes or ec is list or ec is Struct or ec is Repr:
                yield from _walk(e, '%s.%s' % (where, names[k]), in_repr)
    elif c is Repr:
        yield from _walk(v.machine, where, True)


# ---------------------------------------------------------------------------- check

def _lines(pattern):
    for path in sorted(glob.glob(pattern)):
        with gzip.open(path, 'rt', encoding='utf-8', errors='replace') as f:
            yield from f


BLOB_TEXT = re.compile(r'blob\((\d+)\)')
SCRIPT_BLOB = re.compile(r'(?:(?<= )|(?<==))[bB](\d+)(?=[ ,}\n]|$)')


def check(trace, script_dir, clean_dir, out=sys.stdout):
    """Every script line and every cleaned call line against the call of the trace it is for:
    the number, the function, the sizes of the blobs.  The lines are in the order of the trace;
    a call of the trace whose number the next line does not carry has no line (`apitrace dump`
    leaves out the calls its own table marks as verbose) and is counted by function.
    Returns the number of lines that have no call or do not agree with it."""
    sys.path.insert(0, HERE)
    from d3dtrace import HEAD, base_name
    t0 = time.time()
    stream = open_trace(trace)
    rd = Reader(stream)
    script = _lines(os.path.join(script_dir, '*.d3ds.gz')) if script_dir else None
    clean = _lines(os.path.join(clean_dir, '*.txt.gz')) if clean_dir else None
    sline = next(script, None) if script is not None else None
    cline = next(clean, None) if clean is not None else None
    n = ok_script = ok_clean = incomplete = fake = n_script = n_clean = 0
    bad = {}                                    # class of disagreement -> [count, first call]
    absent = {}                                 # (which lines, function) -> [count, first call]

    def differ(table, what, no):
        e = table.setdefault(what, [0, no])
        e[0] += 1

    for call in rd.calls():
        n += 1
        incomplete += call.incomplete
        fake += bool(call.flags & FLAG_FAKE)
        found = None
        if sline is not None:
            f = sline.split(' ', 3)
            if int(f[0]) != call.no:
                differ(absent, ('script', call.name), call.no)
            else:
                found = list(blobs(call))
                n_script += 1
                sizes = [int(x) for x in SCRIPT_BLOB.findall(sline)] if f[1] != 'W' else [int(sline.split()[5])]
                mine = [len(d) for k, _, d in found if k != 'd']
                name = base_name(call.name)
                if (f[1] == 'W') != (call.name == 'memcpy') or (f[1] != 'W' and f[2] != name):
                    differ(bad, 'script: function', call.no)
                elif sizes != mine:
                    differ(bad, 'script: blob sizes of ' + name, call.no)
                else:
                    ok_script += 1
                sline = next(script, None)
        elif script is not None:
            differ(absent, ('script', call.name), call.no)
        if cline is not None:
            m = HEAD.match(cline)
            if m is None:
                differ(bad, 'clean: a line that is not a call line', call.no)
                cline = next(clean, None)
            elif int(m.group(1)) != call.no:
                differ(absent, ('clean', call.name), call.no)
            else:
                if found is None:
                    found = list(blobs(call))
                n_clean += 1
                sizes = [int(x) for x in BLOB_TEXT.findall(cline)]
                mine = [len(d) for k, _, d in found if k != 'B']
                if m.group(2) != call.name:
                    differ(bad, 'clean: function', call.no)
                elif sizes != mine:
                    differ(bad, 'clean: blob sizes of ' + call.name, call.no)
                else:
                    ok_clean += 1
                cline = next(clean, None)
        elif clean is not None:
            differ(absent, ('clean', call.name), call.no)
    left_script = (sline is not None) + sum(1 for _ in script) if script is not None else 0
    left_clean = (cline is not None) + sum(1 for _ in clean) if clean is not None else 0
    cut = stream.close()
    print('trace version %d (semantic %d), %d properties; %d bytes of stream' % (rd.version, rd.semantic, len(rd.properties), rd.base + len(rd.buf)), file=out)
    print('%d calls read (%d incomplete at the end, %d made up by the recorder), %d functions, %d structures, %d enumerations, %d bitmasks'
          % (n, incomplete, fake, len(rd.functions), len(rd.structs), len(rd.enums), len(rd.bitmasks)), file=out)
    print('values by kind (0 to 15): %s' % ' '.join(str(k) for k in rd.kinds), file=out)
    if cut:
        print('the file ends inside chunk %d (%d of %d bytes); the stream ends inside an event: %s; calls dropped with their LEAVE: %d; stray LEAVEs: %d'
              % (cut[0], cut[1], cut[2], 'yes' if rd.cut else 'no', rd.dropped, rd.strays), file=out)
    if script is not None:
        print('script: %d of %d lines agree with their call in number, function and blob sizes; %d lines have no call'
              % (ok_script, n_script + left_script, left_script), file=out)
    if clean is not None:
        print('clean:  %d of %d lines agree with their call in number, function and blob sizes; %d lines have no call'
              % (ok_clean, n_clean + left_clean, left_clean), file=out)
    for what in sorted(bad, key=lambda w: -bad[w][0]):
        print('  DIFFERS %-60s %9d calls, first %d' % (what, bad[what][0], bad[what][1]), file=out)
    for which, name in sorted(absent):
        print('  calls of the trace with no %s line: %-50s %6d, first %d' % (which, name, absent[which, name][0], absent[which, name][1]), file=out)
    print('%.0f s' % (time.time() - t0), file=out)
    return sum(e[0] for e in bad.values()) + left_script + left_clean


# ---------------------------------------------------------------------------- extract

STAGES = {'CreateVertexShader': 'vs', 'CreatePixelShader': 'ps', 'CreateGeometryShader': 'gs',
          'CreateGeometryShaderWithStreamOutput': 'gs', 'CreateHullShader': 'hs', 'CreateDomainShader': 'ds',
          'CreateComputeShader': 'cs'}


def extract(trace, out_dir, until=None, limits=(), out=sys.stdout):
    """OUT/blobs/<2 hex>/<sha256>   each distinct blob once
    OUT/index.tsv     call, ordinal among the call's blobs, kind (see blobs()), size, sha256, where
                      (sorted by call; the player wants the b, B and W rows)
    OUT/shaders.tsv   sha256, size, stage(s), creating calls, first call: one line per distinct
                      shader container (the sweep reads it)
    OUT/frames.tsv    frame, the call number of the Present that ends it
    OUT/summary.json  the totals, and for each limit (a call number) the share at or below it"""
    t0 = time.time()
    stream = open_trace(trace)
    rd = Reader(stream)
    blob_dir = os.path.join(out_dir, 'blobs')
    for i in range(256):
        os.makedirs(os.path.join(blob_dir, '%02x' % i), exist_ok=True)
    seen = {}                       # hash -> (size, first call)
    rows, presents, shaders = [], [], {}
    calls = written = 0
    for call in rd.calls():
        calls += 1
        if until is not None and call.no > until:
            continue
        if call.name.endswith('::Present'):
            presents.append(call.no)
        k = -1
        for k, (kind, where, data) in enumerate(blobs(call)):
            h = hashlib.sha256(data).hexdigest()
            if h not in seen:
                seen[h] = (len(data), call.no)
                path = os.path.join(blob_dir, h[:2], h)
                if not os.path.exists(path) or os.path.getsize(path) != len(data):
                    with open(path + '.tmp', 'wb') as f:
                        f.write(data)
                    os.replace(path + '.tmp', path)
                    written += 1
            elif call.no < seen[h][1]:
                seen[h] = (seen[h][0], call.no)
            rows.append((call.no, k, kind, len(data), h, where))
            if kind == 'B':
                e = shaders.setdefault(h, [len(data), set(), 0, call.no])
                e[1].add(STAGES.get(call.name.split('::')[-1], '?'))
                e[2] += 1
                e[3] = min(e[3], call.no)
    cut = stream.close()
    rows.sort()
    presents.sort()
    with open(os.path.join(out_dir, 'index.tsv'), 'w') as f:
        for r in rows:
            f.write('%d\t%d\t%s\t%d\t%s\t%s\n' % r)
    with open(os.path.join(out_dir, 'shaders.tsv'), 'w') as f:
        for h in sorted(shaders, key=lambda h: shaders[h][3]):
            e = shaders[h]
            f.write('%s\t%d\t%s\t%d\t%d\n' % (h, e[0], ','.join(sorted(e[1])), e[2], e[3]))
    with open(os.path.join(out_dir, 'frames.tsv'), 'w') as f:
        for i, no in enumerate(presents):
            f.write('%d\t%d\n' % (i, no))

    def totals(limit):
        sel = [r for r in rows if limit is None or r[0] <= limit]
        distinct = {r[4]: r[3] for r in sel}
        by_kind = {}
        for r in sel:
            e = by_kind.setdefault(r[2], [0, 0, set()])
            e[0] += 1
            e[1] += r[3]
            e[2].add(r[4])
        return {'blobs': len(sel), 'bytes': sum(r[3] for r in sel), 'distinct': len(distinct), 'distinct_bytes': sum(distinct.values()),
                'by_kind': {k: {'blobs': e[0], 'bytes': e[1], 'distinct': len(e[2]), 'distinct_bytes': sum(distinct[h] for h in e[2])}
                            for k, e in sorted(by_kind.items())}}

    summary = {'trace': os.path.basename(trace), 'calls': calls, 'until': until, 'frames': len(presents), 'cut': cut,
               'shader_calls': sum(e[2] for e in shaders.values()), 'distinct_shaders': len(shaders),
               'all': totals(None), 'limits': {str(l): totals(l) for l in limits}}
    with open(os.path.join(out_dir, 'summary.json'), 'w') as f:
        json.dump(summary, f, indent=1)
    a = summary['all']
    print('%d calls, %d frames; %d blobs of %d bytes, %d distinct of %d bytes (%d files written now); %d shader calls, %d distinct containers'
          % (calls, len(presents), a['blobs'], a['bytes'], a['distinct'], a['distinct_bytes'], written, summary['shader_calls'], len(shaders)), file=out)
    for k, e in a['by_kind'].items():
        print('  kind %s: %d blobs of %d bytes, %d distinct of %d bytes' % (k, e['blobs'], e['bytes'], e['distinct'], e['distinct_bytes']), file=out)
    for l in limits:
        s = summary['limits'][str(l)]
        print('  up to call %d: %d blobs of %d bytes (%.1f%% of all bytes), %d distinct of %d bytes (%.1f%% of the distinct bytes)'
              % (l, s['blobs'], s['bytes'], 100.0 * s['bytes'] / max(a['bytes'], 1), s['distinct'], s['distinct_bytes'],
                 100.0 * s['distinct_bytes'] / max(a['distinct_bytes'], 1)), file=out)
    print('%.0f s' % (time.time() - t0), file=out)
    return summary


# ---------------------------------------------------------------------------- a writer, for the self-test

def _u(v):
    out = bytearray()
    while v >= 0x80:
        out.append(v & 0x7f | 0x80)
        v >>= 7
    out.append(v)
    return bytes(out)


def _s(text):
    b = text.encode('latin-1')
    return _u(len(b)) + b


class Writer:
    """The stream of a made-up trace, by the description at the head of this file."""

    def __init__(self, properties=(('os', 'made-up'), ('arch', 'none'))):
        self.out = bytearray(_u(6) + _u(6))
        for k, v in properties:
            self.out += _s(k) + _s(v)
        self.out += _u(0)
        self.ids = {}
        self.n = 0

    def _sig(self, kind, key, body):
        table = self.ids.setdefault(kind, {})
        if key in table:
            return _u(table[key])
        table[key] = len(table)
        return _u(table[key]) + body()

    def value(self, v):
        if v is None:
            return bytes([T_NULL])
        if v is True or v is False:
            return bytes([T_TRUE if v else T_FALSE])
        if isinstance(v, Ptr):
            return bytes([T_OPAQUE]) + _u(v)
        if isinstance(v, int):
            return bytes([T_UINT]) + _u(v) if v >= 0 else bytes([T_SINT]) + _u(-v)
        if isinstance(v, float):
            return bytes([T_DOUBLE]) + struct.pack('<d', v)
        if isinstance(v, tuple) and v[0] == 'float':
            return bytes([T_FLOAT]) + struct.pack('<f', v[1])
        if isinstance(v, str):
            return bytes([T_STRING]) + _s(v)
        if isinstance(v, bytes):
            return bytes([T_BLOB]) + _u(len(v)) + v
        if isinstance(v, list):
            return bytes([T_ARRAY]) + _u(len(v)) + b''.join(self.value(e) for e in v)
        if isinstance(v, WString):
            return bytes([T_WSTRING]) + _u(len(v.units)) + b''.join(_u(u) for u in v.units)
        if isinstance(v, Repr):
            return bytes([T_REPR]) + self.value(v.human) + self.value(v.machine)
        if isinstance(v, Enum):
            sig = self._sig('e', v.sig, lambda: _u(len(v.sig)) + b''.join(_s(n) + self.value(x) for n, x in v.sig))
            return bytes([T_ENUM]) + sig + self.value(v.value)
        if isinstance(v, Bitmask):
            sig = self._sig('b', v.sig, lambda: _u(len(v.sig)) + b''.join(_s(n) + _u(x) for n, x in v.sig))
            return bytes([T_BITMASK]) + sig + _u(v.value)
        if isinstance(v, Struct):
            sig = self._sig('s', v.sig, lambda: _s(v.sig[0]) + _u(len(v.sig[1])) + b''.join(_s(n) for n in v.sig[1]))
            return bytes([T_STRUCT]) + sig + b''.join(self.value(e) for e in v.members)
        raise TypeError(v)

    def enter(self, name, arg_names, args, thread=0, flags=0, backtrace=None):
        """args: {index: value}.  -> the call's number."""
        sig = self._sig('f', name, lambda: _s(name) + _u(len(arg_names)) + b''.join(_s(a) for a in arg_names))
        self.out += bytes([EVENT_ENTER]) + _u(thread) + sig
        if backtrace is not None:
            self.out += bytes([CALL_BACKTRACE]) + _u(len(backtrace))
            for frame in backtrace:
                self.out += self._sig('t', frame, lambda: bytes([1]) + _s(frame[0]) + bytes([2]) + _s(frame[1]) + bytes([5]) + _u(frame[2]) + bytes([0]))
        if flags:
            self.out += bytes([CALL_FLAGS]) + _u(flags)
        for i, v in args.items():
            self.out += bytes([CALL_ARG]) + _u(i) + self.value(v)
        self.out += bytes([CALL_END])
        self.n += 1
        return self.n - 1

    def leave(self, no, args=None, ret=_NONE):
        self.out += bytes([EVENT_LEAVE]) + _u(no)
        for i, v in (args or {}).items():
            self.out += bytes([CALL_ARG]) + _u(i) + self.value(v)
        if ret is not _NONE:
            self.out += bytes([CALL_RET]) + self.value(ret)
        self.out += bytes([CALL_END])


def snappy_block(data, copies=True):
    """A raw Snappy block of data.  Not a good compressor: one that uses every element form the
    format has (short and long literals, the three copies, copies that overlap themselves)."""
    out = bytearray(_u(len(data)))
    n, i, lit, matches, table = len(data), 0, 0, 0, {}

    def literal(upto):
        nonlocal lit
        p = lit
        while p < upto:
            ln = min(upto - p, 70000)
            if ln <= 60:
                out.append((ln - 1) << 2)
            else:
                k = 1 if ln - 1 < 256 else 2 if ln - 1 < 65536 else 3
                out.append((59 + k) << 2)
                out.extend((ln - 1).to_bytes(k, 'little'))
            out.extend(data[p:p + ln])
            p += ln
        lit = upto

    while copies and i + 4 <= n:
        key = data[i:i + 4]
        j = table.get(key)
        table[key] = i
        if j is None:
            i += 1
            continue
        off, ln = i - j, 4
        while i + ln < n and ln < 64 and data[j + ln] == data[i + ln]:
            ln += 1
        literal(i)
        matches += 1
        if ln <= 11 and off < 2048 and matches % 3:
            out.append(1 | (ln - 4) << 2 | (off >> 8) << 5)
            out.append(off & 255)
        elif off < 65536 and matches % 7:
            out.append(2 | (ln - 1) << 2)
            out.extend(off.to_bytes(2, 'little'))
        else:
            out.append(3 | (ln - 1) << 2)
            out.extend(off.to_bytes(4, 'little'))
        i += ln
        lit = i
    literal(n)
    return bytes(out)


def container(stream, chunk=1 << 16, copies=True):
    out = bytearray(b'at')
    for p in range(0, len(stream), chunk):
        block = snappy_block(stream[p:p + chunk], copies)
        out += len(block).to_bytes(4, 'little') + block
    return bytes(out)


# ---------------------------------------------------------------------------- selftest

def selftest():
    fails = []
    checks = 0

    def ok(cond, what):
        nonlocal checks
        checks += 1
        if not cond:
            fails.append(what)

    usage = (('D3D11_USAGE_DEFAULT', 0), ('D3D11_USAGE_DYNAMIC', 2), ('MADE_UP_NEGATIVE', -3))
    bind = (('D3D11_BIND_VERTEX_BUFFER', 1), ('D3D11_BIND_INDEX_BUFFER', 2))
    desc = ('D3D11_BUFFER_DESC', ('ByteWidth', 'Usage', 'BindFlags'))
    data = ('D3D11_SUBRESOURCE_DATA', ('pSysMem', 'SysMemPitch', 'SysMemSlicePitch'))
    big = bytes((i * 7 + (i >> 9)) & 255 for i in range(200000)) + bytes(150000) + b'abcdabcdabcd' * 9000
    code = b'DXBC' + bytes(range(256)) * 3
    w = Writer()
    c0 = w.enter('ID3D11Device5::CreateBuffer', ('this', 'pDesc', 'pInitialData', 'ppBuffer'),
                 {0: Ptr(0x10), 1: Struct(desc, [300000, Enum(usage, 2), Bitmask(bind, 3)]), 2: [Struct(data, [big, 16, 0]), Struct(data, [b'\1\2\3', 0, 0])]},
                 thread=1, backtrace=[('made-up.dll', 'function', 16), ('made-up.dll', 'other', 32)])
    c1 = w.enter('ID3D11Device5::CreateVertexShader', ('this', 'pShaderBytecode', 'BytecodeLength', 'pClassLinkage', 'ppVertexShader'),
                 {0: Ptr(0x10), 1: Repr('vs_5_0\nmade-up line\nret\n', code), 2: len(code), 3: None}, thread=2)
    w.leave(c1, {4: [Ptr(0x30)]}, ret=0)
    w.leave(c0, {3: [Ptr(0x20)]}, ret=Enum((('S_OK', 0),), 0))
    c2 = w.enter('memcpy', ('dest', 'src', 'n'), {0: Ptr(0x7010), 1: b'\xaa' * 48, 2: 48}, flags=FLAG_FAKE)
    w.leave(c2)
    c3 = w.enter('ID3D11DeviceContext4::GetData', ('this', 'pAsync', 'pData', 'DataSize', 'GetDataFlags'), {0: Ptr(0x40), 1: Ptr(0x50), 3: 8, 4: 0})
    w.leave(c3, {2: b'\0' * 8}, ret=1)
    c4 = w.enter('Made::Up', ('f', 'd', 'neg', 't', 'no', 'w', 'e'),
                 {0: ('float', 0.5), 1: -2.25, 2: -7, 3: True, 4: False, 5: WString([72, 105, 0x20ac]), 6: Enum(usage, -3)})
    w.leave(c4)
    c5 = w.enter('ID3D11Device5::CreateBuffer', ('this', 'pDesc', 'pInitialData', 'ppBuffer'),
                 {0: Ptr(0x10), 1: Struct(desc, [3, Enum(usage, 0), Bitmask(bind, 1)]), 2: [Struct(data, [b'\1\2\3', 0, 0])]})
    w.leave(c5, {3: [Ptr(0x60)]}, ret=0)
    c6 = w.enter('IDXGISwapChain4::Present', ('this', 'SyncInterval', 'Flags'), {0: Ptr(0x70), 1: 0, 2: 0})
    w.leave(c6, ret=0)
    whole = len(w.out)
    c7 = w.enter('ID3D11DeviceContext4::Draw', ('this', 'VertexCount', 'StartVertexLocation'), {0: Ptr(0x40), 1: 3, 2: 0})
    c8 = w.enter('ID3D11DeviceContext4::Flush', ('this',), {0: Ptr(0x40)}, thread=2)
    w.leave(c7, {1: b'x' * 100000})                         # the cut file ends inside this LEAVE
    stream = bytes(w.out)

    tmp = tempfile.mkdtemp(prefix='tracefile-selftest-')
    try:
        exe = build_unsnap(tmp)
        ok(exe is not None, 'unsnap.c compiles (cc)')
        path = os.path.join(tmp, 'made-up.trace')
        cutpath = os.path.join(tmp, 'cut.trace')
        plain = os.path.join(tmp, 'plain.trace')
        packed = container(stream)
        with open(path, 'wb') as f:
            f.write(packed)
        with open(cutpath, 'wb') as f:
            f.write(packed[:-777])
        with open(plain, 'wb') as f:
            f.write(container(stream, 1 << 15, copies=False))
        ok(len(packed) < len(stream) * 3 // 4, 'the made-up container uses copies')
        forms = set()
        p = 2
        while p < len(packed):
            n = int.from_bytes(packed[p:p + 4], 'little')
            blk = packed[p + 4:p + 4 + n]
            q = 0
            while blk[q] & 0x80:
                q += 1
            q += 1
            while q < len(blk):
                tag = blk[q]
                forms.add((tag & 3, (tag >> 2) >= 60) if tag & 3 == 0 else (tag & 3, False))
                if tag & 3 == 0:
                    ln = tag >> 2
                    k = ln - 59 if ln >= 60 else 0
                    ln = int.from_bytes(blk[q + 1:q + 1 + k], 'little') if k else ln
                    q += 1 + k + ln + 1
                else:
                    q += 1 + (0, 1, 2, 4)[tag & 3]
            p += 4 + n
        ok(forms == {(0, False), (0, True), (1, False), (2, False), (3, False)}, 'the made-up container uses every element form: %r' % sorted(forms))

        def read_all(p, python):
            s = PyStream(p) if python else CStream(p, exe)
            out = bytearray()
            while True:
                b = s.read(1 << 16)
                if not b:
                    break
                out += b
            return bytes(out), s.close()

        for python in (True, False) if exe else (True,):
            who = 'python' if python else 'unsnap'
            got, cut = read_all(path, python)
            ok(got == stream and cut is None, '%s: the container gives the stream back' % who)
            got, cut = read_all(plain, python)
            ok(got == stream and cut is None, '%s: a container of literals only gives the stream back' % who)
            got, cut = read_all(cutpath, python)
            ok(cut is not None and len(got) < len(stream) and stream.startswith(got) and len(got) > whole,
               '%s: a file that ends inside its last chunk gives the beginning of the stream (%d of %d bytes)' % (who, len(got), len(stream)))
        if exe:
            ok(read_all(cutpath, True) == read_all(cutpath, False), 'both decoders give the same bytes and the same note for the cut file')
            ok(subprocess.run([exe, '--strict', cutpath], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 1, 'unsnap --strict refuses the cut file')
            bad = bytearray(packed)
            bad[6:9] = b'\xff\xff\xff'
            with open(os.path.join(tmp, 'bad.trace'), 'wb') as f:
                f.write(bad)
            ok(subprocess.run([exe, os.path.join(tmp, 'bad.trace')], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 1, 'unsnap refuses a damaged block')
        try:
            read_all(os.path.join(tmp, 'bad.trace'), True) if exe else None
            ok(not exe, 'the Python decoder refuses a damaged block')
        except FormatError:
            ok(True, '')

        for small in (False, True):                 # small: a buffer so short that events are read twice
            s = open_trace(path, python=small)
            rd = Reader(s)
            if small:
                rd.MARGIN, rd.CHUNK = 16, 64
            calls = list(rd.calls())
            s.close()
            tag = 'small buffer: ' if small else ''
            ok(rd.version == 6 and rd.properties == {'os': 'made-up', 'arch': 'none'}, tag + 'the head')
            ok([c.no for c in calls] == [1, 0, 2, 3, 4, 5, 6, 7, 8], tag + 'calls come in the order they were left, the open ones last: %r' % [c.no for c in calls])
            ok([c.incomplete for c in calls] == [False] * 8 + [True], tag + 'only the call that was never left is incomplete')
            by = {c.no: c for c in calls}
            a = by[0]
            ok(a.name == 'ID3D11Device5::CreateBuffer' and a.thread == 1 and a.args[0] == 0x10 and isinstance(a.args[0], Ptr), tag + 'call 0')
            ok(a.args[1].sig == desc and a.args[1].members[0] == 300000 and a.args[1].members[1].value == 2 and a.args[1].members[2].value == 3, tag + 'a structure')
            ok(a.args[2][0].members[0] == big and a.args[2][1].members[0] == b'\1\2\3', tag + 'a blob larger than a chunk')
            ok(a.late == [3] and a.args[3] == [0x20] and a.ret.value == 0 and a.ret.sig == (('S_OK', 0),), tag + 'what LEAVE adds')
            ok([(k, wh, len(d)) for k, wh, d in blobs(a)] == [('b', 'pInitialData[0].pSysMem', len(big)), ('b', 'pInitialData[1].pSysMem', 3)], tag + 'blobs of a creator')
            b = by[1]
            ok(isinstance(b.args[1], Repr) and b.args[1].human.startswith('vs_5_0') and b.args[1].machine == code and b.args[3] is None, tag + 'a shader')
            ok([(k, wh, len(d)) for k, wh, d in blobs(b)] == [('B', 'pShaderBytecode', len(code))], tag + 'a shader is one blob, its bytecode')
            ok(by[2].flags & FLAG_FAKE and [(k, len(d)) for k, _, d in blobs(by[2])] == [('W', 48)], tag + 'a write')
            ok([(k, wh, len(d)) for k, wh, d in blobs(by[3])] == [('d', 'pData', 8)] and by[3].ret == 1, tag + 'what the device wrote')
            m = by[4]
            ok(m.args[0] == 0.5 and m.args[1] == -2.25 and m.args[2] == -7 and m.args[3] is True and m.args[4] is False
               and m.args[5].units == [72, 105, 0x20ac] and m.args[6].value == -3 and m.args[6].sig == usage, tag + 'the scalar kinds')
            ok(by[5].args[1].sig == desc and by[5].args[1].members[1].sig == usage and by[5].args[2][0].members[0] == b'\1\2\3', tag + 'signatures used a second time')
            ok(by[7].args[1] == b'x' * 100000 and by[7].late == [1], tag + 'a late blob')
            ok(rd.kinds[T_BLOB] == 7 and rd.kinds[T_REPR] == 1 and rd.kinds[T_STRUCT] == 5, tag + 'value counts: %r' % rd.kinds)

        s = open_trace(cutpath)
        rd = Reader(s)
        calls = list(rd.calls())
        cut = s.close()
        ok(cut is not None and rd.cut and rd.dropped == 1, 'the cut file: the stream ends inside an event and its call is dropped')
        ok([(c.no, c.incomplete) for c in calls] == [(1, False), (0, False), (2, False), (3, False), (4, False), (5, False), (6, False), (8, True)],
           'the cut file: %r' % [(c.no, c.incomplete) for c in calls])

        out = os.path.join(tmp, 'out')
        sink = open(os.devnull, 'w')
        summary = extract(path, out, limits=(1, 5), out=sink)
        rows = [l.rstrip('\n').split('\t') for l in open(os.path.join(out, 'index.tsv'))]
        ok([(r[0], r[1], r[2], r[3]) for r in rows] == [('0', '0', 'b', str(len(big))), ('0', '1', 'b', '3'), ('1', '0', 'B', str(len(code))), ('2', '0', 'W', '48'),
                                                        ('3', '0', 'd', '8'), ('5', '0', 'b', '3'), ('7', '0', 'd', '100000')], 'extract: the index')
        ok(all(open(os.path.join(out, 'blobs', r[4][:2], r[4]), 'rb').read() == {'0/0': big, '1/0': code}.get(r[0] + '/' + r[1], None)
               for r in rows if r[0] + '/' + r[1] in ('0/0', '1/0')), 'extract: the files hold the bytes')
        ok(all(hashlib.sha256(open(os.path.join(out, 'blobs', r[4][:2], r[4]), 'rb').read()).hexdigest() == r[4] for r in rows), 'extract: a file is named by its hash')
        ok(rows[1][4] == rows[5][4] and sum(len(os.listdir(os.path.join(out, 'blobs', d))) for d in os.listdir(os.path.join(out, 'blobs'))) == 6, 'extract: equal contents are one file')
        ok(summary['all']['blobs'] == 7 and summary['all']['distinct'] == 6 and summary['limits']['1']['blobs'] == 3 and summary['limits']['5']['distinct'] == 5
           and summary['all']['bytes'] == len(big) + 3 + len(code) + 48 + 8 + 3 + 100000, 'extract: the totals %r' % (summary['all']['blobs'],))
        ok(open(os.path.join(out, 'shaders.tsv')).read().split('\t')[1:] == [str(len(code)), 'vs', '1', '1\n'], 'extract: the shader list')
        ok(open(os.path.join(out, 'frames.tsv')).read() == '0\t6\n', 'extract: the frames')

        # check: against call lines and a script written here for the made-up calls
        os.makedirs(os.path.join(tmp, 'clean'))
        os.makedirs(os.path.join(tmp, 'script'))
        clean_lines = ['1 ID3D11Device5::CreateVertexShader(this = 0x10, pShaderBytecode = shader(vs_5_0, 3 lines), BytecodeLength = %d, pClassLinkage = NULL, ppVertexShader = &0x30) = S_OK' % len(code),
                       '0 ID3D11Device5::CreateBuffer(this = 0x10, pDesc = &{ByteWidth = 300000}, pInitialData = {{pSysMem = blob(%d)}, {pSysMem = blob(3)}}, ppBuffer = &0x20) = S_OK' % len(big),
                       '2 memcpy(dest = 0x7010, src = blob(48), n = 48)', '3 ID3D11DeviceContext4::GetData(this = 0x40, pAsync = 0x50, pData = blob(8), DataSize = 8, GetDataFlags = 0x0) = S_FALSE',
                       '4 Made::Up(f = 0.5)', '5 ID3D11Device5::CreateBuffer(this = 0x10, pInitialData = &{pSysMem = blob(3)}) = S_OK', '6 IDXGISwapChain4::Present(this = 0x70) = S_OK',
                       '7 ID3D11DeviceContext4::Draw(this = 0x40, VertexCount = blob(100000))', '8 ID3D11DeviceContext4::Flush(this = 0x40) // incomplete']
        script_lines = ['1 . ID3D11Device::CreateVertexShader =0 o1 B%d i%d n >o3' % (len(code), len(code)),
                        '0 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=300000} SD3D11_SUBRESOURCE_DATA[{pSysMem=b%d,SysMemPitch=16}{pSysMem=b3,SysMemPitch=0}] >o2' % len(big),
                        '2 W 2 0 16 48', '3 . ID3D11DeviceContext::GetData =1 o4 o5 >d8 i8 i0', '4 U Made::Up -', '5 . ID3D11Device::CreateBuffer =0 o1 sD3D11_SUBRESOURCE_DATA{pSysMem=b3} >o6',
                        '6 . IDXGISwapChain::Present =0 o7 i0 i0', '7 . ID3D11DeviceContext::Draw - o4 i3 i0', '8 . ID3D11DeviceContext::Flush - o4']

        def put(lines_c, lines_s):
            with gzip.open(os.path.join(tmp, 'clean', 'a.txt.gz'), 'wt') as f:
                f.write('\n'.join(lines_c) + '\n')
            with gzip.open(os.path.join(tmp, 'script', 'a.d3ds.gz'), 'wt') as f:
                f.write('\n'.join(lines_s) + '\n')

        put(clean_lines, script_lines)
        ok(check(path, os.path.join(tmp, 'script'), os.path.join(tmp, 'clean'), out=sink) == 0, 'check: the made-up trace agrees with its lines')
        for k, (old, new, n) in enumerate((('b3}', 'b4}', 1), ('B%d' % len(code), 'B5', 1), ('W 2 0 16 48', 'W 2 0 16 47', 1), ('ID3D11DeviceContext::Draw', 'ID3D11DeviceContext::DrawAuto', 1),
                                           ('6 . IDXGI', '9 . IDXGI', 3))):
            put(clean_lines, [l.replace(old, new) if old in l and not (k == 0 and l.startswith('0 ')) else l for l in script_lines])
            ok(check(path, os.path.join(tmp, 'script'), os.path.join(tmp, 'clean'), out=sink) == n, 'check: a script that differs is counted (%s)' % new)
        put([l.replace('blob(48)', 'blob(49)') for l in clean_lines], script_lines)
        ok(check(path, os.path.join(tmp, 'script'), os.path.join(tmp, 'clean'), out=sink) == 1, 'check: a call line that differs is counted')
        put(clean_lines + ['9 ID3D11DeviceContext4::Flush(this = 0x40)'], script_lines[:-1])
        import io
        said = io.StringIO()
        ok(check(path, os.path.join(tmp, 'script'), os.path.join(tmp, 'clean'), out=said) == 1, 'check: a line with no call is counted')
        ok(re.search(r'no script line: ID3D11DeviceContext4::Flush +1, first 8', said.getvalue()) is not None
           and 'script: 8 of 8 lines agree' in said.getvalue() and 'clean:  9 of 10 lines agree' in said.getvalue(), 'check: a call with no line is named, by function')
        sink.close()
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    for f in fails:
        print('FAIL', f)
    print('tracefile selftest: %d checks, %d failed' % (checks, len(fails)))
    return 1 if fails else 0


def main(argv):
    if len(argv) >= 2 and argv[1] == 'selftest':
        return selftest()
    if len(argv) >= 3 and argv[1] == 'check':
        trace_dir = os.path.join(ROOT, 'build', 'trace')
        script, clean = os.path.join(trace_dir, 'script'), os.path.join(trace_dir, 'clean')
        rest = argv[3:]
        while rest:
            if rest[0] == '--script' and len(rest) > 1:
                script = rest[1]
            elif rest[0] == '--clean' and len(rest) > 1:
                clean = rest[1]
            else:
                break
            rest = rest[2:]
        if not rest:
            return 1 if check(argv[2], script, clean) else 0
    if len(argv) >= 4 and argv[1] == 'extract':
        rest, until, limits = argv[4:], None, []
        while rest:
            if rest[0] == '--until' and len(rest) > 1:
                until = int(rest[1])
            elif rest[0] == '--limit' and len(rest) > 1:
                limits.append(int(rest[1]))
            else:
                break
            rest = rest[2:]
        if not rest:
            extract(argv[2], argv[3], until, tuple(limits))
            return 0
    sys.stderr.write(__doc__.split('\n\n')[1] + '\n')
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
