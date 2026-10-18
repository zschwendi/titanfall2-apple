#!/usr/bin/env python3
"""Interfaces from Wine's IDL files, in the shape tools/scout/d3d11_vtables.load() gives.

    load(idl_dir, defines, files=None)   -> {name: Iface}   Iface/Method/Param of d3d11_vtables
    type_text(idl_dir, defines, files)   -> text            the typedefs, enums and structs of the
                                                            same files, for gen_desc.resolve

The files are the ones of Wine 10.0's include/ (LGPL 2.1 or later); they are read from
third_party/wine-idl, which is not part of the repository.  Nothing is copied from them into
the repository except names, IIDs, slot order and types, as the generator writes them.

PREPROCESSING.  Done here, with a small evaluator and no compiler: #if, #ifdef, #ifndef, #elif,
#else, #endif and object-like #define; conditions are C integer expressions of == != <= >= < >
&& || with `defined`; an undefined name is 0, as in C.  Any other directive or operator is an
error.  Lines that begin with cpp_quote(  and import lines are dropped; comments are removed.

INTERFACES.  `[attributes] interface NAME : BASE { ... }` (the attribute list supplies uuid(); a
missing base means no IUnknown: such an interface is read with its own methods from slot 0 and is
not described by gen_desc).  Method attributes and parameter attributes, `[ ... ]` whose content
starts with a letter, are removed; an array declarator is an error.  Slots number from the base's
slot count; IUnknown's three are added with their names.  Parameter 0 is `This`.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "scout"))
import d3d11_vtables as vt          # noqa: E402

IDL_DIR = os.path.join(ROOT, "third_party", "wine-idl")


class IdlError(Exception):
    pass


def _macro_int(macros, name):
    v = macros.get(name, "0")
    if not re.fullmatch(r"\d+", v.strip()):
        raise IdlError("macro %s = %r is not an integer" % (name, v))
    return int(v)


def eval_condition(expr, macros, where):
    e = re.sub(r"defined\s*\(\s*(\w+)\s*\)", lambda m: "1" if m.group(1) in macros else "0", expr)
    e = re.sub(r"defined\s+(\w+)", lambda m: "1" if m.group(1) in macros else "0", e)
    e = re.sub(r"\b[A-Za-z_]\w*\b", lambda m: str(_macro_int(macros, m.group(0))), e)
    if "!" in re.sub(r"!=", "", e):
        raise IdlError("%s: `!` is not supported: %s" % (where, expr))
    e = e.replace("&&", " and ").replace("||", " or ")
    if not re.fullmatch(r"(?:\s|\d+|==|!=|<=|>=|<|>|\(|\)|and|or|\+|-|\*|/)*", e):
        raise IdlError("%s: unsupported condition %s" % (where, expr))
    return bool(eval(e, {"__builtins__": {}}, {}))


def preprocess(text, defines, where):
    """-> the lines that remain, with directives done and cpp_quote/import lines removed."""
    macros = dict(defines)
    out, stack, active = [], [], True       # stack entries: [parent active, a branch was taken]
    for n, line in enumerate(text.split("\n"), 1):
        s = line.strip()
        loc = "%s:%d" % (where, n)
        if s.startswith("#"):
            m = re.match(r"#\s*(\w+)\s*(.*)$", s)
            if not m:
                raise IdlError("%s: bad directive" % loc)
            d, arg = m.group(1), m.group(2).strip()
            if d in ("if", "ifdef", "ifndef"):
                if d == "if":
                    cond = eval_condition(arg, macros, loc) if active else False
                else:
                    cond = (arg.split()[0] in macros) == (d == "ifdef") if active else False
                stack.append([active, cond])
                active = active and cond
            elif d == "elif":
                if not stack:
                    raise IdlError("%s: #elif without #if" % loc)
                parent, taken = stack[-1]
                cond = (not taken) and parent and eval_condition(arg, macros, loc)
                stack[-1][1] = taken or cond
                active = bool(cond)
            elif d == "else":
                if not stack:
                    raise IdlError("%s: #else without #if" % loc)
                parent, taken = stack[-1]
                stack[-1][1] = True
                active = parent and not taken
            elif d == "endif":
                if not stack:
                    raise IdlError("%s: #endif without #if" % loc)
                active = stack.pop()[0]
            elif d == "define":
                if active:
                    parts = arg.split(None, 1)
                    if not parts or "(" in parts[0]:
                        raise IdlError("%s: unsupported #define" % loc)
                    macros[parts[0]] = parts[1].strip() if len(parts) > 1 else ""
            else:
                raise IdlError("%s: unsupported directive #%s" % (loc, d))
            out.append("")
            continue
        if not active:
            out.append("")
            continue
        if s.startswith("cpp_quote(") or s.startswith("import "):
            out.append("")
            continue
        out.append(line)
    if stack:
        raise IdlError("%s: #if not closed" % where)
    return "\n".join(out)


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def _files(idl_dir, files):
    if not os.path.isdir(idl_dir):
        raise IdlError("no IDL directory %s" % idl_dir)
    if files is None:
        files = sorted(f for f in os.listdir(idl_dir) if f.endswith(".idl"))
    return list(files)


def _text(idl_dir, defines, files):
    """-> {file: comment-free preprocessed text}"""
    out = {}
    for f in _files(idl_dir, files):
        p = os.path.join(idl_dir, f)
        if not os.path.exists(p):
            raise IdlError("no IDL file %s" % p)
        raw = open(p, encoding="utf-8", errors="replace").read()
        out[f] = strip_comments(preprocess(raw, defines, f))
    return out


_IFACE_RE = re.compile(r"(?:\[([^\[\]]*)\]\s*)?\binterface\s+(\w+)\s*(?::\s*(\w+)\s*)?\{(.*?)^\}", re.S | re.M)
_UUID_RE = re.compile(r"uuid\(\s*\"?([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\"?\s*\)")
_METHOD_RE = re.compile(r"(?P<ret>[\w\s\*]+?)\s*\b(?P<name>\w+)\s*\((?P<params>.*)\)", re.S)
_ATTR_RE = re.compile(r"\[\s*[A-Za-z_][^\[\]]*\]")

IUNKNOWN_METHODS = [
    ("HRESULT", "QueryInterface", [("REFIID", "riid"), ("void **", "ppvObject")]),
    ("ULONG", "AddRef", []),
    ("ULONG", "Release", []),
]


def _unknown():
    out = []
    for i, (ret, name, ps) in enumerate(IUNKNOWN_METHODS):
        params = [vt.Param("This", "This", 0)] + [vt.Param(t, n, j + 1) for j, (t, n) in enumerate(ps)]
        out.append(vt.Method(name, i, ret, params))
    return out


def _parse_file(f, text):
    """-> {name: (uuid or None, base or None, [Method without slots])} for the interfaces of one file."""
    found = {}
    for m in _IFACE_RE.finditer(text):
        attrs, name, base, body = m.group(1) or "", m.group(2), m.group(3), m.group(4)
        if name in found:
            raise IdlError("%s: interface %s twice" % (f, name))
        u = _UUID_RE.search(attrs)
        body = _ATTR_RE.sub("", body)
        if "[" in body or "]" in body:
            raise IdlError("%s: %s: an array declarator or unknown bracket" % (f, name))
        decls = []
        for stmt in body.split(";"):
            s = " ".join(stmt.split())
            if not s:
                continue
            mm = _METHOD_RE.fullmatch(s)
            if not mm:
                raise IdlError("%s: %s: cannot read %r" % (f, name, s))
            ps = mm.group("params").strip()
            plist = [] if ps in ("", "void") else vt._split_params(ps)
            decls.append((" ".join(mm.group("ret").split()), mm.group("name"),
                          [vt._param(t, i + 1) for i, t in enumerate(plist)]))
        found[name] = (u.group(1).lower() if u else None, base, decls, f)
    return found


def load(idl_dir, defines, files=None):
    """-> {name: Iface} for every interface of `files` (default: every .idl in idl_dir), read with
    the given preprocessor defines.  Iface.header is the file name."""
    texts = _text(idl_dir, defines, files)
    raw = {}
    for f, t in texts.items():
        for name, rec in _parse_file(f, t).items():
            if name in raw:
                raise IdlError("interface %s is in two files" % name)
            raw[name] = rec
    done = {}

    def methods_of(name, trail):
        if name in done:
            return done[name]
        if name in trail:
            raise IdlError("base cycle at %s" % name)
        _, base, decls, _f = raw[name]
        if base is None:
            start = []
        elif base == "IUnknown":
            start = _unknown()
        elif base in raw:
            start = methods_of(base, trail + (name,))
        else:
            raise IdlError("%s: base %s is not in the files read" % (name, base))
        own = []
        for ret, mname, params in decls:
            slot = len(start) + len(own)
            own.append(vt.Method(mname, slot, ret, [vt.Param("This", "This", 0)] + params))
        done[name] = start + own
        return done[name]

    out = {}
    for name, (u, base, decls, f) in raw.items():
        it = vt.Iface(name)
        it.methods = list(methods_of(name, ()))
        it.iid = u
        it.base = base
        it.header = f
        out[name] = it
    return out


_ENUM_RE = re.compile(r"\btypedef\s+enum\s+(\w+)\s*\{[^}]*\}\s*([\w\s,]+?)\s*;")


# Wine's audioclient.idl does `import "audiosessiontypes.h";` and uses AUDCLNT_SHAREMODE, but that
# header is not in third_party/wine-idl, so the reader cannot see the enumeration.  An import line
# of such a header gets its definition here, as the header has it (Microsoft's audiosessiontypes.h;
# the two values are the ones the audio client's constants give: SHARED 0, EXCLUSIVE 1).
IMPORTED_HEADERS = {
    "audiosessiontypes.h":
        "typedef enum AUDCLNT_SHAREMODE { AUDCLNT_SHAREMODE_SHARED = 0, AUDCLNT_SHAREMODE_EXCLUSIVE = 1 } AUDCLNT_SHAREMODE;",
}


def type_text(idl_dir, defines, files=None):
    """The preprocessed text of the files, for gen_desc.resolve.  An enumeration whose typedef
    names differ from its tag (`typedef enum _E {...} E;`, `} A, B;`) gets a line `typedef _E E;`
    after it, which resolve follows as a second name of the enumeration.  The definitions of the
    headers a file imports (IMPORTED_HEADERS) follow the file's text."""
    parts = []
    for f, t in _text(idl_dir, defines, files).items():
        extra = []
        raw = open(os.path.join(idl_dir, f), encoding="utf-8", errors="replace").read()
        for h, definition in IMPORTED_HEADERS.items():
            if re.search(r'^\s*import\s+"%s"' % re.escape(h), raw, re.M):
                extra.append(definition)
        for m in _ENUM_RE.finditer(t):
            tag = m.group(1)
            for name in (n.strip() for n in m.group(2).split(",")):
                if name != tag:
                    extra.append("typedef %s %s;" % (tag, name))
        parts.append(t + "\n" + "\n".join(extra) + "\n")
    return "\n".join(parts)
