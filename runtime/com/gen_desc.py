#!/usr/bin/env python3
"""Write the interface descriptions of runtime/com from the MinGW-w64 Direct3D headers.

    .venv/bin/python runtime/com/gen_desc.py            rewrite desc/*.idef and types.def
    .venv/bin/python runtime/com/gen_desc.py --check    exit 1 if they differ from what is checked in
                                                        (exit 0 with a note where the headers are absent)

WHERE THE FACTS COME FROM.  The headers are the ones tools/scout/d3d11_vtables.py parses, and
that script does the parsing: third_party/dxmt/include/native/directx/*.h (d3d11.h, dxgi.h,
d3d11shader.h, d3dcommon.h; dxgiformat.h, dxgitype.h and dxgicommon.h for what a type name
is) and ../windows/unknwn.h for IUnknown's IID.  They are the
MinGW-w64 project's own headers (mingw-w64-headers; the DirectX ones are the output of Wine's
IDL compiler on Wine's IDL files), carried by the DXMT clone in the main checkout, which is not
part of this repository.  Licence: LGPL 2.1 or later, with the MinGW-w64 statement that data
structure layouts and numerical parameters taken from them do not make the result a derived
work (third_party/dxmt/include/native/directx/README.md).  Nothing here is read from
Microsoft's SDK, and nothing from the game.

What is taken from a header: an interface's name, its IID, its base, and for each vtable slot
the method's name, return type and parameters (types and MinGW's parameter names).  Each
`typedef struct IFooVtbl` lists every method of the interface in slot order, inherited ones
first; d3d11shader.h has no C vtable and its STDMETHOD list is read instead.

WHAT IS WRITTEN (both checked in, so that a build needs no header):

  desc/<Interface>.idef   one per `interface` line of interfaces.def:
        interface <name>
        iid       <8-4-4-4-12, lower case>
        owner     <native module whose slot holds the vtable>
        header    <the header it was read from>
        inherits  <base> <iid> <slots>          nearest base first, down to IUnknown
        <slot> <declared by> <return type> <Method>(<parameters>)      in vtable order
    The prototype is in the runtime's type names (runtime/tools/types.def and types.def here)
    and leaves out `This`, which every method takes first.  An array parameter is written as
    the pointer it is in the ABI, with its length after `#`.

  types.def   every type name the descriptions use that runtime/tools/types.def does not
    have, in that file's format, with the C definition after `#=`:
        <name> u32        #= enum                 an enumeration, 32 bits by value
        <name> <class>    #= <known type>         a typedef of a type the runtime has
        <name> void       #= struct | interface   used only behind a pointer; the layout is
                                                  not described here (the name is declared as
                                                  an incomplete struct, which a header under
                                                  runtime/include/rt/win/ may complete)

An `interface` line of interfaces.def may name an IDL source instead of the headers:
    interface <name> <owner> idl <file> [NAME=VALUE ...]
read by runtime/com/idl_source.py from third_party/wine-idl (Wine's IDL files; the preprocessor
defines are given on the line).  Such a description is written the same way; its header line
names the file.  Without that directory, --check skips those descriptions and types.def with a
note; a write fails.

Standard library only; single process.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools", "scout"))
sys.path.insert(0, os.path.join(ROOT, "runtime", "tools"))
import d3d11_vtables as vt          # noqa: E402
import idl_source                   # noqa: E402
import shimdef                      # noqa: E402

IUNKNOWN = [("HRESULT", "QueryInterface", [("REFIID", "riid"), ("void **", "ppvObject")]),
            ("ULONG", "AddRef", []), ("ULONG", "Release", [])]
RENAME = {"WINBOOL": "BOOL"}        # MinGW's spelling of BOOL


class DescError(Exception):
    pass


def norm_type(t):
    """A type as the header writes it -> as the runtime writes it: no struct/enum keyword,
    MinGW's WINBOOL as BOOL, one space before the stars."""
    words = [w for w in re.findall(r"\w+|\*", t) if w not in ("struct", "enum", "interface")]
    words = [RENAME.get(w, w) for w in words]
    stars = words.count("*")
    base = " ".join(w for w in words if w != "*")
    return base + (" " + "*" * stars if stars else "")


def base_name(t):
    return " ".join(w for w in re.findall(r"\w+", t) if w not in ("const", "volatile"))


# Read for what a type name is (an enumeration, a typedef, a struct), beside the scout's list.
TYPE_HEADERS = ["dxgiformat.h", "dxgitype.h", "dxgicommon.h"]


def load_headers(hdr_dir):
    text = {}
    for h in vt.HEADERS + TYPE_HEADERS:
        p = os.path.join(hdr_dir, h)
        if os.path.exists(p):
            text[h] = open(p, encoding="utf-8", errors="replace").read()
    return text


def iunknown_iid(hdr_dir):
    p = os.path.join(hdr_dir, "..", "windows", "unknwn.h")
    m = vt._GUID_RE.search(open(p, encoding="utf-8", errors="replace").read())
    if not m or m.group(1) != "IUnknown":
        raise DescError("%s: no DEFINE_GUID(IID_IUnknown, ...)" % p)
    return vt.guid_str(m.group(2), m.group(3), m.group(4), m.group(5))


def chain_of(ifaces, name):
    """Bases of `name`, nearest first, ending with IUnknown; or, for a root interface (no base,
    no IUnknown), the bases down to the root, which is the last of them (none for the root itself)."""
    out, b = [], ifaces[name].base
    while b and b != "IUnknown":
        if b not in ifaces:
            raise DescError("%s: base %s is not in the headers" % (name, b))
        out.append(b)
        b = ifaces[b].base
    if b == "IUnknown":
        return out + ["IUnknown"]
    return out


def describe(ifaces, name, owner, unk_iid, used):
    it = ifaces[name]
    chain = chain_of(ifaces, name)
    root = not chain or chain[-1] != "IUnknown"     # a root interface, or derived from one: no IUnknown
    if not it.iid and not root:
        raise DescError("%s has no IID in the headers" % name)
    slots_of = {b: (3 if b == "IUnknown" else len(ifaces[b].methods)) for b in chain}
    iid_of = {b: (unk_iid if b == "IUnknown" else ifaces[b].iid) for b in chain}
    for b in chain:
        if not iid_of[b] and not root:
            raise DescError("%s: base %s has no IID in the headers" % (name, b))
        if b != "IUnknown" and [m.name for m in ifaces[b].methods] != [m.name for m in it.methods[:slots_of[b]]]:
            raise DescError("%s does not begin with the methods of its base %s" % (name, b))
        used.add(b)
    used.add(name)
    source = "the Wine IDL file" if it.header.endswith(".idl") else "the MinGW-w64 header"
    lines = ["# Generated by runtime/com/gen_desc.py from %s %s.  Do not edit: change" % (source, it.header),
             "# interfaces.def or the generator and run `make com-desc`.  Format: runtime/com/gen_desc.py.",
             "interface %s" % name, "iid       %s" % (it.iid or "none"), "owner     %s" % owner, "header    %s" % it.header]
    if not chain:                               # a root interface: no base at all
        lines.append("root")
    for b in chain:
        lines.append("inherits  %s %s %d" % (b, iid_of[b] or "none", slots_of[b]))
    width = max(len(b) for b in chain + [name])
    for m in it.methods:
        declared = name
        for b in chain:                         # the farthest base that already has this slot
            if m.slot < slots_of[b]:
                declared = b
        if m.slot < 3 and not root:             # IUnknown's three, in the runtime's own words
            ret, mname, params = IUNKNOWN[m.slot]
            if m.name != mname:
                raise DescError("%s slot %d is %s, not %s" % (name, m.slot, m.name, mname))
            notes = []
        else:
            ret, mname, params, notes = norm_type(m.ret), m.name, [], []
            for p in m.params[1:]:
                t = norm_type(p.type)
                if p.array is not None:
                    t = t + (" *" if "*" not in t else "*")
                    notes.append("%s[%s]" % (p.name, p.array))
                if not p.name:
                    raise DescError("%s::%s: a parameter without a name (%r)" % (name, mname, p.type))
                params.append((t, p.name))
            used.add(("ret", base_name(ret), "*" in ret, "%s::%s" % (name, mname)))
            for t, pn in params:
                used.add(("par", base_name(t), "*" in t, "%s::%s %s" % (name, mname, pn)))
        proto = "%s%s%s(%s)" % (ret, "" if ret.endswith("*") else " ", mname, ", ".join(
            "%s%s%s" % (t, "" if t.endswith("*") else " ", pn) for t, pn in params) or "void")
        lines.append("%3d  %-*s  %s%s" % (m.slot, width, declared, proto, ("   # " + ", ".join(notes)) if notes else ""))
    return "\n".join(lines) + "\n"


def resolve(name, known, alltext, seen=()):
    """-> (class, size or None, C definition) for a type name the runtime's table lacks."""
    if name in known:
        t = known[name]
        return t.cls, t.size, name
    if name in seen:
        raise DescError("typedef cycle at %s" % name)
    if re.search(r"\btypedef\s+enum\s+%s\s*\{" % re.escape(name), alltext) or \
       re.search(r"\benum\s+%s\s*\{" % re.escape(name), alltext):
        return "u32", 4, "enum"
    m = re.search(r"\btypedef\s+(\w+)\s+%s\s*;" % re.escape(name), alltext)
    if m and m.group(1) not in ("struct", "enum", "interface", "union"):
        cls, size, c = resolve(m.group(1), known, alltext, seen + (name,))
        if c == "enum":
            return cls, size, "enum"            # a second name of an enumeration
        return cls, size, m.group(1) if m.group(1) in known else c
    if re.search(r"\b(?:typedef\s+)?struct\s+%s\s*\{" % re.escape(name), alltext):
        return "void", None, "struct"
    return None, None, None


def build(hdr_dir, idl_dir=None):
    """-> ({relative path under runtime/com: text}, [names of IDL interfaces not built because
    idl_dir is None]).  With idl_dir None the IDL interfaces and types.def are not built."""
    ifaces = vt.load(hdr_dir)
    alltext = "\n".join(load_headers(hdr_dir).values())
    unk_iid = iunknown_iid(hdr_dir)
    known, _ = shimdef.load_types([os.path.join(ROOT, "runtime", "tools", "types.def")])
    out, used, wanted, extra, skipped = {}, set(), [], [], []
    for n, raw in enumerate(open(os.path.join(HERE, "interfaces.def")), 1):
        f = raw.split("#", 1)[0].split()
        if not f:
            continue
        if f[0] == "interface" and len(f) == 3:
            wanted.append((f[1], f[2], None))
        elif f[0] == "interface" and len(f) >= 5 and f[3] == "idl" and f[4].endswith(".idl"):
            defs = f[5:]
            if not all(re.fullmatch(r"\w+=\w+", d) for d in defs):
                raise DescError("interfaces.def:%d: defines are NAME=VALUE" % n)
            wanted.append((f[1], f[2], (f[4], tuple(sorted(tuple(d.split("=", 1)) for d in defs)))))
        elif f[0] == "type" and len(f) == 2:
            extra.append(f[1])
        else:
            raise DescError("interfaces.def:%d: expected `interface <name> <module> [idl <file> [NAME=VALUE ...]]` or `type <name>`" % n)
    # Each IDL source is read once, with its defines.  Its interfaces join the headers' for
    # `describe` and the type rules; its type text joins the headers' for `resolve`.
    idl_loaded = set()
    for name, owner, src in wanted:
        if src is None or idl_dir is None or src in idl_loaded:
            continue
        loaded = idl_source.load(idl_dir, dict(src[1]), [src[0]])
        for k in loaded:
            if k in ifaces:
                raise DescError("%s: interface %s is also in the headers" % (src[0], k))
        idl_loaded.add(src)
        ifaces.update(loaded)
        alltext += "\n" + idl_source.type_text(idl_dir, dict(src[1]), [src[0]])
    for name, owner, src in wanted:
        if src is not None and idl_dir is None:
            skipped.append(name)
            continue
        if name not in ifaces:
            raise DescError("interfaces.def: %s is not in %s" % (name, "the headers" if src is None else src[0]))
        if src is not None and ifaces[name].header != src[0]:
            raise DescError("interfaces.def: %s is in %s, not %s" % (name, ifaces[name].header, src[0]))
        out["desc/%s.idef" % name] = describe(ifaces, name, owner, unk_iid, used)
    if skipped:
        return out, skipped

    # ---- types.def ----
    rows = {}
    for u in sorted(x for x in used if isinstance(x, str)):
        rows[u] = ("void", None, "interface")                       # every interface and base, by name
    for kind, base, pointer, where in sorted(x for x in used if not isinstance(x, str)):
        if base in known or base in rows:
            continue
        cls, size, c = resolve(base, known, alltext)
        if cls is None:
            if not pointer:
                raise DescError("%s: type %s is passed by value and the headers do not say what it is" % (where, base))
            is_iface = base in ifaces or re.search(r"\bDECLARE_INTERFACE_?\(\s*%s\b" % re.escape(base), alltext)
            cls, size, c = "void", None, "interface" if is_iface else "struct"
        if cls == "void" and not pointer:
            raise DescError("%s: %s is a struct passed or returned by value; the generator has no rule for that" % (where, base))
        rows[base] = (cls, size, c)
    for base in extra:
        if base not in known and base not in rows:
            cls, size, c = resolve(base, known, alltext)
            if cls is None:
                raise DescError("interfaces.def: type %s is not in the headers" % base)
            rows[base] = (cls, size, c)
    lines = ["# Generated by runtime/com/gen_desc.py from the MinGW-w64 headers and the Wine IDL files.  Do not edit: change",
             "# interfaces.def or the generator and run `make com-desc`.",
             "#",
             "# The type names the interface descriptions (desc/*.idef) use that runtime/tools/types.def",
             "# does not have, in that file's format; it is read after that file.  After `#=`: what the C",
             "# type is.  `enum`: 32 bits by value (uint32_t).  A type name: a typedef of it.  `struct`",
             "# and `interface` (class void): used only behind a pointer, declared as an incomplete",
             "# struct of the same name; a header under runtime/include/rt/win/ may complete a struct.",
             "@include com_types.h", ""]
    width = max(len(n) for n in rows)
    for name in sorted(rows, key=lambda n: ({"enum": 0, "struct": 2, "interface": 3}.get(rows[n][2], 1), n)):
        cls, size, c = rows[name]
        col = cls + (" %d" % size if cls in ("struct", "struct8") else "")
        lines.append("%-*s  %-10s #= %s" % (width, name, col, c))
    out["types.def"] = "\n".join(lines) + "\n"
    return out, []


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--headers", default=vt.HDR_DIR, help="directory of the MinGW-w64 DirectX headers")
    ap.add_argument("--check", action="store_true", help="compare with the checked-in files instead of writing")
    args = ap.parse_args()
    if not os.path.exists(os.path.join(args.headers, "d3d11.h")):
        if args.check:
            print("gen_desc: SKIPPED: no d3d11.h in %s (the DXMT clone of the main checkout is not here)" % args.headers)
            return 0
        sys.exit("gen_desc: no d3d11.h in %s" % args.headers)
    have_idl = os.path.isdir(idl_source.IDL_DIR)
    if not have_idl and not args.check:
        sys.exit("gen_desc: no IDL directory %s (third_party/wine-idl: the main checkout's link)" % idl_source.IDL_DIR)
    try:
        files, skipped = build(args.headers, idl_source.IDL_DIR if have_idl else None)
    except (DescError, shimdef.ShimError, idl_source.IdlError, OSError) as e:
        sys.exit("gen_desc: %s" % e)
    have = set()
    if os.path.isdir(os.path.join(HERE, "desc")):
        have = {"desc/" + f for f in os.listdir(os.path.join(HERE, "desc")) if f.endswith(".idef")}
    skip_files = {"desc/%s.idef" % n for n in skipped}
    stale = sorted(have - set(files) - skip_files)
    if args.check:
        bad = list(stale)
        for rel, text in sorted(files.items()):
            try:
                if open(os.path.join(HERE, rel)).read() != text:
                    bad.append(rel)
            except OSError:
                bad.append(rel)
        if bad:
            print("gen_desc: runtime/com differs from what the headers give: %s" % ", ".join(bad))
            return 1
        if skipped:
            print("gen_desc: SKIPPED for %s and types.def: no %s (the Wine IDL files are not here); "
                  "the other descriptions agree with the headers" % (", ".join(sorted(skipped)), idl_source.IDL_DIR))
            return 0
        print("gen_desc: %d descriptions and types.def agree with the headers" % (len(files) - 1))
        return 0
    os.makedirs(os.path.join(HERE, "desc"), exist_ok=True)
    for rel in stale:
        os.remove(os.path.join(HERE, rel))
    for rel, text in sorted(files.items()):
        with open(os.path.join(HERE, rel), "w") as f:
            f.write(text)
    n = sum(len(re.findall(r"^\s*\d+  ", t, re.M)) for r, t in files.items() if r.startswith("desc/"))
    print("gen_desc: %d interfaces, %d vtable slots, %d type names" % (
        len(files) - 1, n, len(re.findall(r"#=", files["types.def"])) - 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
