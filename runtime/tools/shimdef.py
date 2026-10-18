"""Parsing shared by gen_shims.py and check_shim.py (docs/RUNTIME.md sections 3.2, 10.2).

Reads runtime/tools/types.def, runtime/modules.def, and from one shim source file the four
RT-* header tags and the one-line SHIM / SHIM_ORD / SHIM_RAW declaration.  Every problem is
a ShimError whose message starts with the file (and line, when known), so both tools report
the same thing in the same words.

Single process, standard library only.
"""

import os
import re

# class name in types.def -> (RtArgClass enumerator, fixed size in bytes or None)
CLASSES = {
    "void":    ("RT_AC_VOID", None),
    "i8":      ("RT_AC_I8", 1),
    "u8":      ("RT_AC_U8", 1),
    "i16":     ("RT_AC_I16", 2),
    "u16":     ("RT_AC_U16", 2),
    "i32":     ("RT_AC_I32", 4),
    "u32":     ("RT_AC_U32", 4),
    "i64":     ("RT_AC_I64", 8),
    "u64":     ("RT_AC_U64", 8),
    "ptr":     ("RT_AC_PTR", 8),
    "str":     ("RT_AC_STR", 8),
    "wstr":    ("RT_AC_WSTR", 8),
    "handle":  ("RT_AC_HANDLE", 8),
    "guestfn": ("RT_AC_GUESTFN", 8),
    "f32":     ("RT_AC_F32", 4),
    "f64":     ("RT_AC_F64", 8),
    "struct8": ("RT_AC_STRUCT8", None),     # size given in types.def: 1, 2, 4 or 8
    "struct":  ("RT_AC_STRUCT", None),      # size given in types.def
}
INT_CLASSES = ("i8", "u8", "i16", "u16", "i32", "u32", "i64", "u64")
POINTER_CLASSES = ("ptr", "str", "wstr", "handle")

# RUNTIME.md section 3.3: these differ between x64 Windows and arm64 macOS.
FORBIDDEN_WORDS = ("long", "wchar_t", "size_t")
FORBIDDEN_WHY = ("long is 64-bit here and 32-bit on Windows, wchar_t is 32-bit here and 16-bit on "
                 "Windows, and size_t is not a Windows name: use LONG, WCHAR, SIZE_T and the other "
                 "names of rt/wintypes.h (docs/RUNTIME.md section 3.3)")

STATUSES = ("full", "partial", "absent", "advisory", "trap")
TAGS = ("RT-STATUS", "RT-DOC", "RT-CASES", "RT-TRAPS")
MODULE_KINDS = {"lifted": "RT_MOD_LIFTED", "system": "RT_MOD_SYSTEM", "stubbed": "RT_MOD_STUBBED",
                "exe": "RT_MOD_EXE"}


class ShimError(Exception):
    pass


class TypeInfo:
    def __init__(self, name, cls, size, where):
        self.name, self.cls, self.size, self.where = name, cls, size, where


class Param:
    """One declared parameter, or the return type (name None)."""
    def __init__(self, ctype, name, cls, size):
        self.ctype = ctype          # C type as written, e.g. "const WCHAR *"
        self.name = name
        self.cls = cls              # key of CLASSES
        self.size = size            # bytes that are significant in the register or slot

    @property
    def enum(self):
        return CLASSES[self.cls][0]


class Shim:
    def __init__(self):
        self.path = None            # as given
        self.rel = None             # relative to the repository root, forward slashes
        self.line = 0               # line number of the SHIM declaration
        self.kind = None            # "SHIM", "SHIM_ORD" or "SHIM_RAW"
        self.lib = None
        self.name = None
        self.ordinal = 0
        self.ret = None             # Param (None for SHIM_RAW)
        self.params = []
        self.tags = {}              # tag -> text
        self.cases = []             # RT-CASES entries
        self.traps = []             # RT-TRAPS entries
        self.source = ""            # file text
        self.decl = ""              # the SHIM line

    @property
    def status(self):
        return self.tags["RT-STATUS"]

    @property
    def raw(self):
        return self.kind == "SHIM_RAW"

    @property
    def sret(self):
        return (not self.raw) and self.ret.cls == "struct"


def lib_id(dll):
    """DLL file name -> the <lib> of section 10.2: lower case, no .dll, other punctuation as _."""
    s = dll.lower()
    for ext in (".dll", ".exe"):
        if s.endswith(ext):
            s = s[:-len(ext)]
            break
    return re.sub(r"[^a-z0-9_]", "_", s)


# ---- types.def -------------------------------------------------------------------------------

def load_types(paths):
    """-> (name -> TypeInfo, [headers named by @include lines])"""
    types, includes = {}, []
    for path in paths:
        for n, raw in enumerate(open(path), 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            where = "%s:%d" % (path, n)
            if line.startswith("@include"):
                includes.append(line.split(None, 1)[1].strip())
                continue
            f = line.split()
            size = None
            if f[-1].isdigit():
                size = int(f.pop())
            if len(f) < 2:
                raise ShimError("%s: expected '<type name> <class> [size]'" % where)
            cls, name = f[-1], " ".join(f[:-1])
            if cls not in CLASSES:
                raise ShimError("%s: unknown class '%s' for type '%s' (classes: %s)" % (
                    where, cls, name, " ".join(CLASSES)))
            fixed = CLASSES[cls][1]
            if cls in ("struct8", "struct"):
                if size is None:
                    raise ShimError("%s: %s '%s' needs its size in bytes" % (where, cls, name))
                if cls == "struct8" and size not in (1, 2, 4, 8):
                    raise ShimError("%s: struct8 '%s' must be 1, 2, 4 or 8 bytes, not %d" % (where, name, size))
                if cls == "struct" and size in (1, 2, 4, 8):
                    raise ShimError("%s: a struct of %d bytes is passed by value: declare '%s' as struct8" % (
                        where, size, name))
            elif size is not None and size != fixed:
                raise ShimError("%s: class %s is %s bytes, not %d" % (where, cls, fixed, size))
            else:
                size = fixed
            if any(w in FORBIDDEN_WORDS for w in name.split()):
                raise ShimError("%s: '%s' may not be a shim type: %s" % (where, name, FORBIDDEN_WHY))
            if name in types:
                raise ShimError("%s: type '%s' is already defined at %s" % (where, name, types[name].where))
            types[name] = TypeInfo(name, cls, size, where)
    return types, includes


# ---- modules.def -----------------------------------------------------------------------------

def dll_identity(path, where):
    """The identity the loader checks (docs/RUNTIME.md section 1.3), read from the DLL file."""
    import hashlib
    import struct
    try:
        data = open(path, "rb").read()
        nt = struct.unpack_from("<I", data, 0x3C)[0]
        if data[:2] != b"MZ" or data[nt:nt + 4] != b"PE\0\0":
            raise ShimError("%s: %s is not a PE file" % (where, path))
        return {"size_of_image": struct.unpack_from("<I", data, nt + 24 + 56)[0], "file_size": len(data),
                "timestamp": struct.unpack_from("<I", data, nt + 8)[0], "sha256": hashlib.sha256(data).hexdigest()}
    except (OSError, struct.error) as e:
        raise ShimError("%s: cannot read the DLL of a lifted module (dll=%s): %s" % (where, path, e))


def load_modules(path, allow_unchecked=False):
    """-> list of dicts: kind, file, lib, and for lifted modules the identity fields, the
    lifter's module name (`module`), the guest directory (`dir`), `late` and `unchecked`.

    A lifted row is   lifted <file> dll=<path> [module=<name>] [dir=<guest directory>] [place=late]
      dll=     the DLL the lifted C was made from, as a path from the repository root; the
               identity (size_of_image, file_size, timestamp, sha256) is read from it now
      module=  the lifter's --module name, the prefix of the C symbols (default: the library id)
      dir=     the DLL's directory under C:\\Titanfall2, with forward slashes; `.` is the install
               root (default bin/x64_retail; also accepted on a stubbed row)
      place=late  the module's base comes from the late group of the image arena (rt/module.h,
               RT_MODDEF_LATE): a row added to a list whose bases are in use moves no other base
    or, in a test manifest only (allow_unchecked), a row whose file is made when the test runs:
               lifted <file> identity=unchecked size_of_image=<n> [module=] [dir=]
    The four identity keys may also be written out instead of dll=."""
    out, seen = [], {}
    for n, raw in enumerate(open(path), 1):
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        where = "%s:%d" % (path, n)
        f = line.split()
        if len(f) < 2 or f[0] not in MODULE_KINDS:
            raise ShimError("%s: expected '<%s> <file name> [key=value ...]'" % (where, "|".join(MODULE_KINDS)))
        m = {"kind": f[0], "file": f[1], "lib": lib_id(f[1]), "where": where,
             "size_of_image": 0, "file_size": 0, "timestamp": 0, "sha256": None,
             "module": None, "dir": None, "unchecked": False, "dll": None, "late": False}
        if f[0] in ("lifted", "stubbed"):
            m["dir"] = "bin\\x64_retail"
        if f[0] == "lifted":
            m["module"] = m["lib"]
        for kv in f[2:]:
            if "=" not in kv:
                raise ShimError("%s: expected key=value, got '%s'" % (where, kv))
            k, v = kv.split("=", 1)
            if k in ("size_of_image", "file_size", "timestamp"):
                m[k] = int(v, 0)
            elif k == "sha256":
                if not re.fullmatch(r"[0-9a-f]{64}", v):
                    raise ShimError("%s: sha256 must be 64 lower-case hex digits" % where)
                m[k] = v
            elif k == "dir" and m["dir"] is not None:
                if not re.fullmatch(r"\.|[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*", v):
                    raise ShimError("%s: dir must be `.` or a relative path with forward slashes" % where)
                m["dir"] = "" if v == "." else v.replace("/", "\\")
            elif k == "module" and f[0] == "lifted":
                if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", v):
                    raise ShimError("%s: module must be a C identifier" % where)
                m["module"] = v
            elif k == "dll" and f[0] == "lifted":
                m["dll"] = v
            elif k == "place" and f[0] == "lifted":
                if v != "late":
                    raise ShimError("%s: place= takes `late` only" % where)
                m["late"] = True
            elif k == "identity" and v == "unchecked" and f[0] == "lifted":
                if not allow_unchecked:
                    raise ShimError("%s: identity=unchecked is for test manifests only" % where)
                m["unchecked"] = True
            else:
                raise ShimError("%s: unknown key '%s' for a %s module" % (where, k, f[0]))
        if m["dll"]:
            if m["unchecked"] or m["size_of_image"] or m["sha256"]:
                raise ShimError("%s: dll= reads the identity from the file; do not also write it out" % where)
            m.update(dll_identity(m["dll"], where))
        if m["kind"] == "lifted" and not m["size_of_image"]:
            raise ShimError("%s: a lifted module needs dll=<path> (or size_of_image, file_size, timestamp, sha256)" % where)
        if m["kind"] == "lifted" and not m["unchecked"] and not m["sha256"]:
            raise ShimError("%s: a lifted module needs dll=<path> or all four identity keys" % where)
        if m["lib"] in seen:
            raise ShimError("%s: '%s' and '%s' (%s) are the same library '%s'" % (
                where, m["file"], seen[m["lib"]]["file"], seen[m["lib"]]["where"], m["lib"]))
        seen[m["lib"]] = m
        out.append(m)
    return out


# ---- one shim source file --------------------------------------------------------------------

_COMMENT = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
_STRING = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')


def blank(match):
    """Replacement that keeps newlines, so line numbers survive."""
    return re.sub(r"[^\n]", " ", match.group(0))


def strip_comments(text):
    return _COMMENT.sub(blank, text)


def strip_strings(text):
    """Blank the contents of string and character literals (after strip_comments)."""
    return _STRING.sub(lambda m: m.group(0)[0] + re.sub(r"[^\n]", " ", m.group(0)[1:-1]) + m.group(0)[-1], text)


def parse_tags(text, path):
    """The four RT-* tags from the comment that starts the file."""
    m = re.match(r"\s*/\*(.*?)\*/", text, re.S)
    if not m:
        raise ShimError("%s: the file must start with the header comment that holds the %s tags "
                        "(docs/RUNTIME.md section 10.2)" % (path, ", ".join(TAGS)))
    tags, current = {}, None
    for raw in m.group(1).split("\n"):
        line = raw.strip()
        if line.startswith("*"):
            line = line[1:].strip()
        t = re.match(r"(RT-[A-Z]+):\s*(.*)$", line)
        if t:
            current = t.group(1)
            if current not in TAGS:
                raise ShimError("%s: unknown header tag %s (the tags are %s)" % (path, current, ", ".join(TAGS)))
            if current in tags:
                raise ShimError("%s: header tag %s appears twice" % (path, current))
            tags[current] = t.group(2).strip()
        elif current and line:
            tags[current] = (tags[current] + " " + line).strip()
    for tag in TAGS:
        if tag not in tags:
            raise ShimError("%s: header tag %s is missing" % (path, tag))
        if not tags[tag]:
            raise ShimError("%s: header tag %s is empty (write 'none' if there is nothing to list)" % (path, tag))
    if tags["RT-STATUS"] not in STATUSES:
        raise ShimError("%s: RT-STATUS is '%s'; it must be one of %s" % (path, tags["RT-STATUS"], ", ".join(STATUSES)))
    if not re.match(r"https?://\S+$", tags["RT-DOC"]) and tags["RT-DOC"] != "none":
        raise ShimError("%s: RT-DOC must be the URL of the function's documentation (or 'none' for a "
                        "function that has none)" % path)
    return tags


def split_list(text):
    """An RT-CASES or RT-TRAPS value -> entries.  Entries are separated by ';'."""
    if text.strip().lower() == "none":
        return []
    return [" ".join(e.split()) for e in text.split(";") if e.strip()]


def _split_args(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        depth += ch in "([{"
        depth -= ch in ")]}"
        cur += ch
    out.append(cur.strip())
    return out


def parse_type(text, types, where, want_name):
    """'const WCHAR *name' -> Param.  The class comes from types.def; never a guess."""
    if any(c in text for c in "()[]"):
        raise ShimError("%s: '%s': function-pointer and array declarators are not supported in a SHIM "
                        "line; use GUESTFN or a pointer type" % (where, text))
    tokens = re.findall(r"[A-Za-z_][A-Za-z0-9_]*|\*|\S", text)
    bad = [t for t in tokens if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*|\*", t)]
    if bad:
        raise ShimError("%s: cannot parse '%s'" % (where, text))
    name = None
    if want_name:
        if len(tokens) < 2 or tokens[-1] == "*":
            raise ShimError("%s: '%s': a parameter needs a type and a name" % (where, text))
        name = tokens.pop()
    stars = tokens.count("*")
    const = "const" in tokens
    base_words = [t for t in tokens if t not in ("*", "const", "volatile", "restrict")]
    base = " ".join(base_words)
    ctype = " ".join(t for t in tokens if t != "*")
    if stars:
        ctype += " " + "*" * stars
    for w in base_words:
        if w in FORBIDDEN_WORDS:
            raise ShimError("%s: type '%s' is forbidden in a shim signature: %s" % (where, ctype.strip(), FORBIDDEN_WHY))
    if not base:
        raise ShimError("%s: '%s' has no type" % (where, text))
    info = types.get(base)
    if info is None:
        raise ShimError("%s: unknown type '%s': it is not in runtime/tools/types.def.  Do not guess its "
                        "class; if the Windows prototype really uses it, stop and report "
                        "(docs/RUNTIME.md section 10.1)" % (where, base))
    if stars == 0:
        if info.cls == "void" and want_name:
            raise ShimError("%s: a parameter cannot be void; write SHIM(lib, ret, Name) for a function "
                            "without parameters" % where)
        return Param(ctype, name, info.cls, info.size)
    cls = "ptr"
    if stars == 1 and const and base in ("char", "CHAR"):
        cls = "str"
    elif stars == 1 and const and base == "WCHAR":
        cls = "wstr"
    return Param(ctype, name, cls, 8)


def parse_shim(path, types, root="."):
    """Parse one shim file.  Checks everything that can be checked from the file alone."""
    s = Shim()
    s.path = path
    s.rel = os.path.relpath(path, root).replace(os.sep, "/")
    text = open(path, encoding="utf-8", errors="strict").read()
    s.source = text
    s.tags = parse_tags(text, s.rel)
    s.cases = split_list(s.tags["RT-CASES"])
    s.traps = split_list(s.tags["RT-TRAPS"])

    code = strip_comments(text)
    decls = []
    for n, line in enumerate(code.split("\n"), 1):
        if re.match(r"\s*SHIM(_ORD|_RAW)?\s*\(", line):
            decls.append((n, line))
    if not decls:
        raise ShimError("%s: no SHIM, SHIM_ORD or SHIM_RAW declaration found" % s.rel)
    if len(decls) > 1:
        raise ShimError("%s:%d: a second SHIM declaration; one import per file (docs/RUNTIME.md section 10.1)" % (
            s.rel, decls[1][0]))
    s.line, line = decls[0]
    where = "%s:%d" % (s.rel, s.line)
    m = re.fullmatch(r"(SHIM(?:_ORD|_RAW)?)\s*\((.*)\)\s*", line)
    if not m or line != line.lstrip():
        raise ShimError("%s: the SHIM declaration must be ONE line that starts in column 1 and ends with "
                        "the closing parenthesis; the opening brace goes on the next line" % where)
    s.kind, s.decl = m.group(1), line.strip()
    args = _split_args(m.group(2))
    need = {"SHIM": 3, "SHIM_ORD": 4, "SHIM_RAW": 2}[s.kind]
    if len(args) < need or (s.kind == "SHIM_RAW" and len(args) != 2) or any(a == "" for a in args):
        raise ShimError("%s: expected %s" % (where, {
            "SHIM": "SHIM(lib, return type, Name, parameters...)",
            "SHIM_ORD": "SHIM_ORD(lib, ordinal, return type, Name, parameters...)",
            "SHIM_RAW": "SHIM_RAW(lib, Name)"}[s.kind]))
    s.lib = args.pop(0)
    if s.kind == "SHIM_ORD":
        o = args.pop(0)
        if not o.isdigit() or not 0 < int(o) < 65536:
            raise ShimError("%s: the ordinal must be a decimal number from 1 to 65535, not '%s'" % (where, o))
        s.ordinal = int(o)
    if s.kind != "SHIM_RAW":
        s.ret = parse_type(args.pop(0), types, where, False)
    s.name = args.pop(0)
    if not re.fullmatch(r"[a-z][a-z0-9_]*", s.lib):
        raise ShimError("%s: library '%s' must be the DLL name in lower case without .dll, other "
                        "punctuation as _ (docs/RUNTIME.md section 10.2)" % (where, s.lib))
    if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", s.name):
        raise ShimError("%s: '%s' is not a function name" % (where, s.name))
    if s.kind != "SHIM_RAW":
        s.params = [parse_type(a, types, where, True) for a in args]
        names = [p.name for p in s.params]
        for nm in names:
            if names.count(nm) > 1:
                raise ShimError("%s: parameter name '%s' is used twice" % (where, nm))
            if nm == "ctx":
                raise ShimError("%s: a parameter cannot be named ctx" % where)
        if len(s.params) + (1 if s.sret else 0) > 16:
            raise ShimError("%s: more than 16 arguments" % where)

    # File name and directory must agree with the declaration (section 10.2).
    d = os.path.basename(os.path.dirname(os.path.abspath(path)))
    stem = os.path.splitext(os.path.basename(path))[0]
    if d != s.lib:
        raise ShimError("%s: the file is in directory '%s' but declares library '%s'" % (where, d, s.lib))
    ok = [s.name]
    if s.kind == "SHIM_ORD":
        ok = ["ord%d_%s" % (s.ordinal, s.name), s.name]
    if stem not in ok:
        raise ShimError("%s: the file is named '%s.c' but declares '%s'; expected %s" % (
            where, stem, s.name, " or ".join("'%s.c'" % x for x in ok)))

    # Status rules that need only the file (section 9.2).
    body = strip_strings(code)
    has_unimpl = re.search(r"\bRT_UNIMPLEMENTED\s*\(", body) is not None
    st = s.status
    if st == "partial" and not s.traps:
        raise ShimError("%s: RT-STATUS is partial but RT-TRAPS lists nothing; a shim with no unhandled "
                        "case is 'full'" % s.rel)
    if st in ("full", "absent", "advisory"):
        if s.traps:
            raise ShimError("%s: RT-STATUS is %s but RT-TRAPS lists cases; a shim that traps on some "
                            "cases is 'partial'" % (s.rel, st))
        if has_unimpl:
            raise ShimError("%s: RT-STATUS is %s but the body calls RT_UNIMPLEMENTED; that is 'partial' "
                            "with the case listed in RT-TRAPS" % (s.rel, st))
    if st == "trap" and not has_unimpl:
        raise ShimError("%s: RT-STATUS is trap but the body does not call RT_UNIMPLEMENTED" % s.rel)
    if st != "trap" and not s.cases:
        raise ShimError("%s: RT-CASES lists nothing; say what the shim handles" % s.rel)
    return s


def find_shims(dirs):
    """Every <dir>/<lib>/<Name>.c, sorted."""
    out = []
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for lib in sorted(os.listdir(d)):
            sub = os.path.join(d, lib)
            if not os.path.isdir(sub):
                continue
            for f in sorted(os.listdir(sub)):
                if f.endswith(".c"):
                    out.append(os.path.join(sub, f))
    return out
