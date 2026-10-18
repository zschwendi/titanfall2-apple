#!/usr/bin/env python3
"""Interface layouts for the Direct3D 11 and DXGI scout (issue #31): method order, parameters, IIDs.

The vtable order is read mechanically from the MinGW-w64 public headers that the DXMT clone
carries (third_party/dxmt/include/native/directx/*.h).  Those headers are MIDL output, so each
`typedef struct IFooVtbl` lists every method of the interface, inherited ones first, in slot
order.  Nothing here comes from the game.  Where a header gives no C vtable (d3d11shader.h uses
DECLARE_INTERFACE_) the STDMETHOD list is read instead.

    .venv/bin/python tools/scout/d3d11_vtables.py                 # summary of every interface
    .venv/bin/python tools/scout/d3d11_vtables.py ID3D11Device    # one interface, slot by slot
    .venv/bin/python tools/scout/d3d11_vtables.py --iid 0x...     # not supported; see d3d11_iids use

Library use:  `load()` -> {name: Iface}.  Iface has .methods (list of Method in slot order),
.iid (str, 8-4-4-4-12 lower case) or None, .base (name or None).  Method has .name, .slot,
.params (list of Param: .type, .name, .index, .array) and .ret.  Slot s is at byte offset 8*s.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HDR_DIR = os.environ.get(
    "D3D_HEADERS",
    os.path.join(HERE, "..", "..", "third_party", "dxmt", "include", "native", "directx"))

HEADERS = ["d3dcommon.h", "d3d11.h", "d3d11_1.h", "d3d11_2.h", "d3d11_3.h", "d3d11_4.h",
           "d3d11sdklayers.h", "d3d11shader.h",
           "dxgi.h", "dxgi1_2.h", "dxgi1_3.h", "dxgi1_4.h", "dxgi1_5.h", "dxgi1_6.h",
           "dxgidebug.h"]

# IUnknown is not in the files above as a Vtbl struct of its own.
IUNKNOWN = ["QueryInterface", "AddRef", "Release"]
NOT_IUNKNOWN = {"ID3DInclude", "ID3D11ShaderReflectionType", "ID3D11ShaderReflectionVariable",
                "ID3D11ShaderReflectionConstantBuffer"}


class Param:
    def __init__(self, type_, name, index, array=None):
        # array: the text between the brackets of an array declarator ("4" for `FLOAT c[4]`), or
        # None.  Such a parameter is a pointer in the ABI; .type is the element type as written.
        self.type, self.name, self.index, self.array = type_, name, index, array

    def __repr__(self):
        return "%s %s" % (self.type, self.name)


class Method:
    def __init__(self, name, slot, ret, params):
        self.name, self.slot, self.ret, self.params = name, slot, ret, params

    @property
    def offset(self):
        return 8 * self.slot

    def __repr__(self):
        return "%d:%s" % (self.slot, self.name)


class Iface:
    def __init__(self, name):
        self.name = name
        self.methods = []
        self.iid = None
        self.base = None
        self.header = None

    def by_offset(self, off):
        if off % 8 or off < 0:
            return None
        s = off // 8
        return self.methods[s] if s < len(self.methods) else None

    def by_name(self, name):
        for m in self.methods:
            if m.name == name:
                return m
        return None


def _split_params(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def _param(text, index):
    text = " ".join(text.split())
    m = re.match(r"^(.*?)(\w+)(\s*\[\s*\w*\s*\])?$", text)
    if not m or text in ("void",):
        return Param(text, "", index)
    return Param(m.group(1).strip(), m.group(2), index, m.group(3).strip(" []") if m.group(3) else None)


_VTBL_RE = re.compile(r"typedef struct (\w+)Vtbl \{(.*?)\n\} \1Vtbl;", re.S)
_FN_RE = re.compile(r"([\w \*]+?)\(STDMETHODCALLTYPE \*(\w+)\)\((.*?)\);", re.S)
_GUID_RE = re.compile(r"DEFINE_GUID\(\s*IID_(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,"
                      r"\s*(0x[0-9a-fA-F]+)\s*,((?:\s*0x[0-9a-fA-F]+\s*,?){8})\)")
_BASE_RE = re.compile(r"^(\w+)\s*:\s*public\s+(\w+)\s*$", re.M)
_DECL_RE = re.compile(r"DECLARE_INTERFACE(?:_)?\((\w+)(?:\s*,\s*(\w+))?\)\s*\{(.*?)\n\};", re.S)
_STDM_RE = re.compile(r"STDMETHOD(?:_\(\s*([^,]+?)\s*,\s*(\w+)\s*\)|\(\s*(\w+)\s*\))\(\s*(.*?)\)\s*PURE;", re.S)


def guid_str(a, b, c, rest):
    d = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", rest)]
    return "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x" % (
        int(a, 16), int(b, 16), int(c, 16), *d)


def guid_bytes(s):
    """Memory layout (little-endian first three fields) of an 8-4-4-4-12 string."""
    h = s.replace("-", "")
    import struct
    return struct.pack("<IHH", int(h[:8], 16), int(h[8:12], 16), int(h[12:16], 16)) + bytes.fromhex(h[16:])


def bytes_guid(b):
    import struct
    a, bb, c = struct.unpack("<IHH", b[:8])
    return "%08x-%04x-%04x-%s-%s" % (a, bb, c, b[8:10].hex(), b[10:16].hex())


_CACHE = {}


def load(hdr_dir=None):
    """Return {interface name: Iface} for every interface found in the headers."""
    hdr_dir = hdr_dir or HDR_DIR
    if hdr_dir in _CACHE:
        return _CACHE[hdr_dir]
    ifaces = {}
    guids = {}
    for h in HEADERS:
        p = os.path.join(hdr_dir, h)
        if not os.path.exists(p):
            continue
        text = open(p, encoding="utf-8", errors="replace").read()
        for m in _GUID_RE.finditer(text):
            guids[m.group(1)] = guid_str(m.group(2), m.group(3), m.group(4), m.group(5))
        bases = {a: b for a, b in _BASE_RE.findall(text)}
        for m in _VTBL_RE.finditer(text):
            name, body = m.group(1), m.group(2)
            body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
            it = Iface(name)
            it.header = h
            for slot, fm in enumerate(_FN_RE.finditer(body)):
                ret = " ".join(fm.group(1).split())
                params = [_param(t, i) for i, t in enumerate(_split_params(fm.group(3)))]
                it.methods.append(Method(fm.group(2), slot, ret, params))
            it.base = bases.get(name)
            ifaces[name] = it
        for m in _DECL_RE.finditer(text):
            name, base, body = m.group(1), m.group(2), m.group(3)
            if name in ifaces:
                continue
            body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
            it = Iface(name)
            it.header = h
            it.base = base
            for slot, fm in enumerate(_STDM_RE.finditer(body)):
                ret = (fm.group(1) or "HRESULT").strip()
                nm = fm.group(2) or fm.group(3)
                plist = fm.group(4).replace("THIS_", "").replace("THIS", "")
                params = [Param("This", "This", 0)] + [_param(t, i + 1) for i, t in enumerate(_split_params(plist))]
                it.methods.append(Method(nm, slot, ret, params))
            ifaces[name] = it
    for name, it in ifaces.items():
        it.iid = guids.get(name)
    # sanity: every interface starts with IUnknown's three methods
    for name, it in ifaces.items():
        got = [m.name for m in it.methods[:3]]
        if got != IUNKNOWN:
            if name in NOT_IUNKNOWN:
                continue
            raise SystemExit("unexpected vtable start for %s: %s" % (name, got))
    _CACHE[hdr_dir] = ifaces
    return ifaces


def iid_table(ifaces=None):
    """{iid string: interface name}.  Several interfaces never share an IID."""
    ifaces = ifaces or load()
    return {it.iid: n for n, it in ifaces.items() if it.iid}


def descendants_of(ifaces, base):
    """Interface names whose method list starts with all of `base`'s methods and that name
    `base` somewhere in their chain of declared bases."""
    out = set()
    for n, it in ifaces.items():
        b = it.base
        seen = 0
        while b and seen < 10:
            if b == base:
                out.add(n)
                break
            b = ifaces[b].base if b in ifaces else None
            seen += 1
    return out


def main():
    ifs = load()
    args = sys.argv[1:]
    if args and args[0] in ifs:
        it = ifs[args[0]]
        print("%s  base=%s  iid=%s  methods=%d  (%s)" % (it.name, it.base, it.iid, len(it.methods), it.header))
        for m in it.methods:
            print("  slot %3d  +0x%03x  %s(%s)" % (m.slot, m.offset, m.name, ", ".join(p.type + " " + p.name for p in m.params[1:])))
        return
    print("%d interfaces" % len(ifs))
    for n in sorted(ifs):
        it = ifs[n]
        print("%-40s %3d methods  base=%-28s %s" % (n, len(it.methods), it.base, it.iid or "-"))


if __name__ == "__main__":
    main()
