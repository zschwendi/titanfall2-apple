#!/usr/bin/env python3
"""dxmt_abi_scan.py -- what DXMT's native headers change for a Windows x64 caller (finding 35).

Compiles one translation unit that includes the Direct3D 11 / DXGI / d3dcommon / d3d11shader /
d3dcompiler headers exactly as DXMT's native build sees them (third_party/dxmt/include/native/windows
for the base types, third_party/dxmt/include/native/directx for the mingw-w64 headers), with Apple
clang, for two targets, and compares:

  layout   struct sizes, alignments and field offsets: arm64-apple-macos (what a native DXMT
           library uses) against x86_64-pc-windows-msvc (what the recompiled game's own ABI
           expects).  Only the compiler front end is used; nothing is linked or run.
  types    sizes of the base types the interfaces use (BOOL, LONG, WCHAR, HANDLE, LARGE_INTEGER, ...), both targets.
  methods  every virtual method of every COM interface in those headers, classified by how its
           arguments and return value travel on Windows x64 (the guest side of a generated
           thunk): floating point arguments, structs passed by value, structs returned by value.

Usage:  .venv/bin/python tools/scout/dxmt_abi_scan.py [--dxmt third_party/dxmt] [types|layout|methods|all]
Needs only /usr/bin/clang (Xcode) and the DXMT clone.  Writes nothing outside a temporary directory.
Windows-target headers: the compiler is given tiny stand-ins for <cstdint> and <cstring> so that no
macOS C++ library headers are involved (-nostdinc -ffreestanding).
"""

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

PROBE = """\
#include <windows.h>
#include <d3dcommon.h>
#include <d3d11_4.h>
#include <d3d11shader.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
"""

SHIM_CSTDINT = "#pragma once\n#include <stdint.h>\n"
SHIM_CSTRING = """#pragma once
#include <stddef.h>
extern "C" int memcmp(const void *, const void *, size_t);
extern "C" void *memset(void *, int, size_t);
extern "C" void *memcpy(void *, const void *, size_t);
namespace std { using ::memcmp; using ::memset; using ::memcpy; }
"""

TARGETS = {
    "mac": ["-target", "arm64-apple-macos14.0"],
    "win": ["-target", "x86_64-pc-windows-msvc"],
}


def clang_cmd(dxmt, tmp, target, extra):
    inc = ["-I" + os.path.join(dxmt, "include/native/windows"), "-I" + os.path.join(dxmt, "include/native/directx")]
    cmd = ["clang++", "-w", "-std=c++20", "-fsyntax-only"] + TARGETS[target]
    if target == "win":
        res = subprocess.check_output(["clang", "-print-resource-dir"], text=True).strip()
        cmd += ["-nostdinc", "-ffreestanding", "-isystem", os.path.join(tmp, "shim"), "-isystem", res + "/include"]
    return cmd + inc + extra + [os.path.join(tmp, "probe.cpp")]


def layouts(dxmt, tmp, target):
    out = subprocess.run(clang_cmd(dxmt, tmp, target, ["-Xclang", "-fdump-record-layouts-complete"]),
                         capture_output=True, text=True, check=True).stdout
    recs = {}
    cur = None
    for line in out.splitlines():
        m = re.match(r"\s+0 \| (struct|union|class) ([A-Za-z_]\w*)$", line)
        if m:
            cur = {"kind": m.group(1), "fields": [], "size": None, "align": None}
            recs[m.group(2)] = cur
            continue
        if cur is None:
            continue
        m = re.match(r"\s+(\d+(?::\d+-\d+)?) \| {3}(\S.*)$", line)         # one level of nesting = a direct field
        if m:
            cur["fields"].append((m.group(1), m.group(2).split()[-1]))
            continue
        m = re.search(r"\[sizeof=(\d+), (?:dsize=\d+, )?align=(\d+)", line)
        if m:
            cur["size"], cur["align"] = int(m.group(1)), int(m.group(2))
            cur = None
    return recs


TYPE_EXPRS = ["bool", "long", "wchar_t", "BOOL", "LONG", "ULONG", "HRESULT", "WCHAR", "DWORD", "LONGLONG", "HANDLE", "HWND",
              "HMONITOR", "SIZE_T", "LONG_PTR", "UINT_PTR", "GUID", "LUID", "RECT", "POINT", "LARGE_INTEGER", "DXGI_FORMAT",
              "D3D_FEATURE_LEVEL", "D3D11_USAGE"]


def cmd_types(dxmt, tmp):
    """sizeof/alignof of the base types on each target, read from clang's failed static_assert messages."""
    src = PROBE + "".join('static_assert(sizeof(%s) == 0, "S %s");\nstatic_assert(alignof(%s) == 0, "A %s");\n' % (t, t, t, t)
                          for t in TYPE_EXPRS)
    path = os.path.join(tmp, "probe.cpp")
    with open(path, "w") as f:
        f.write(src)
    res = {}
    for tg in ("mac", "win"):
        out = subprocess.run(clang_cmd(dxmt, tmp, tg, ["-ferror-limit=0"]), capture_output=True, text=True).stderr
        # clang prints the canonical type in the requirement text, so key on the message we put there: "S name" / "A name"
        vals = [(n, k, v) for k, n, v in re.findall(r": ([SA]) (\w+)\n.*?evaluates to '(\d+) == 0'", out, re.S)]
        res[tg] = {(n, k): int(v) for n, k, v in vals}
    with open(path, "w") as f:
        f.write(PROBE)
    print("%-20s %-12s %-12s" % ("type", "macOS arm64", "Windows x64") + "   (size/align in bytes)")
    for t in TYPE_EXPRS:
        m = "%s/%s" % (res["mac"].get((t, "S"), "?"), res["mac"].get((t, "A"), "?"))
        w = "%s/%s" % (res["win"].get((t, "S"), "?"), res["win"].get((t, "A"), "?"))
        print("%-20s %-12s %-12s%s" % (t, m, w, "   <-- differs" if m != w else ""))


def cmd_layout(dxmt, tmp):
    mac, win = layouts(dxmt, tmp, "mac"), layouts(dxmt, tmp, "win")
    common = sorted(set(mac) & set(win))
    api = [n for n in common if re.match(r"(D3D|DXGI|ID3D|IDXGI|_?D3D)", n)]
    diff = []
    for n in common:
        a, b = mac[n], win[n]
        if (a["size"], a["align"], a["fields"]) != (b["size"], b["align"], b["fields"]):
            diff.append(n)
    print("records compared: %d (of which Direct3D/DXGI named: %d); macOS-only %d, Windows-only %d"
          % (len(common), len(api), len(set(mac) - set(win)), len(set(win) - set(mac))))
    print("records whose layout differs between arm64-apple-macos and x86_64-pc-windows-msvc: %d" % len(diff))
    for n in diff:
        a, b = mac[n], win[n]
        print("  %-40s macOS sizeof=%-4s align=%-2s   Windows sizeof=%-4s align=%-2s" % (n, a["size"], a["align"], b["size"], b["align"]))
        wf = dict((f, o) for o, f in b["fields"])
        for o, f in a["fields"]:
            if wf.get(f) != o:
                print("      field %-28s offset macOS %-8s Windows %s" % (f, o, wf.get(f)))
    return mac, win, diff


def walk(node, recs, typedefs, enums):
    if isinstance(node, dict):
        k = node.get("kind")
        if k == "CXXRecordDecl" and node.get("completeDefinition") and node.get("name"):
            recs.append(node)
        elif k == "TypedefDecl" and node.get("name"):
            typedefs[node["name"]] = node["type"]["qualType"]
        elif k == "EnumDecl" and node.get("name"):
            enums.add(node["name"])
        for v in node.get("inner", []):
            walk(v, recs, typedefs, enums)


def ret_type(qual):
    depth = 0
    for i, c in enumerate(qual):
        if c == "(" and depth == 0 and i > 0 and qual[i - 1] == " ":
            return qual[:i].strip()
        depth += c == "("
        depth -= c == ")"
    return qual


INT_RE = re.compile(r"^(unsigned |signed )?(char|short|int|long|long long|__int64|_Bool|bool)$")
SKIP_IFACE = re.compile(r"ID3D10Effect|ID3D10StateBlock|ID3D10Debug|ID3D10InfoQueue|ID3D10Switch|ID3D10Counter")


def resolve(q, typedefs, enums, sizes):
    """Resolve typedef chains by hand (clang's JSON only gives the sugared name for return types)."""
    for _ in range(12):
        q = re.sub(r"\b(const|volatile)\b", "", q).strip()
        q = re.sub(r"^(struct|union|class) ", "", q)
        if q.startswith("enum "):
            return "enum"
        if q in enums:
            return "enum"
        if q in sizes or INT_RE.match(q) or q in ("float", "double", "void"):
            return q
        if q in typedefs:
            q = typedefs[q]
            continue
        break
    return q


def classify(qual, typedefs, enums, sizes):
    """Return (class, detail) for one parameter or return type."""
    q = resolve(qual, typedefs, enums, sizes)
    if "*" in q or "&" in q:
        base = resolve(q.rstrip(" *&"), typedefs, enums, sizes)          # what the pointer points to
        if base == "wchar_t":                                              # UTF-16 on Windows, UTF-32 in the native headers
            return "wptr", qual
        return "ptr", qual
    if q in ("float", "double"):
        return "fp", qual
    if q == "void":
        return "void", qual
    if q in ("long", "unsigned long", "signed long"):               # 8 bytes on arm64 macOS, 4 on Windows
        return "long", qual
    if q == "enum" or INT_RE.match(q):
        return "int", qual
    if q in sizes:
        return "struct", "%s (%d bytes)" % (qual, sizes[q])
    return "other", "%s -> %s" % (qual, q)


def cmd_methods(dxmt, tmp, win):
    sizes = {n: r["size"] for n, r in win.items()}
    ast = json.loads(subprocess.run(clang_cmd(dxmt, tmp, "win", ["-Xclang", "-ast-dump=json"]),
                                    capture_output=True, text=True, check=True).stdout)
    recs, typedefs, enums = [], {}, set()
    walk(ast, recs, typedefs, enums)
    ifaces = {}
    for r in recs:
        n = r["name"]
        if not re.match(r"(ID3D11|IDXGI|ID3D10Device|ID3D10Blob|ID3DBlob|ID3DUserDefinedAnnotation|ID3DInclude|ID3D11ShaderReflection)\w*$", n) or SKIP_IFACE.match(n):
            continue
        meths = [m for m in r.get("inner", []) if m.get("kind") == "CXXMethodDecl" and m.get("virtual")]
        ifaces[n] = (r, meths)
    total = sum(len(m) for _, m in ifaces.values())
    print("COM interfaces examined (D3D11, DXGI, ID3D10Device*, blobs, annotation): %d; virtual methods declared by them (own, not inherited): %d" % (len(ifaces), total))
    # vtable slot order: MSVC lays overloads out in reverse order and Itanium adds destructor slots; neither may occur
    overloads = ["%s::%s" % (n, nm) for n, (_, ms) in sorted(ifaces.items())
                 for nm in sorted({m["name"] for m in ms}) if [m["name"] for m in ms].count(nm) > 1]
    vdtors = [n for n, (r, _) in sorted(ifaces.items())
              if any(m.get("kind") == "CXXDestructorDecl" and m.get("virtual") for m in r.get("inner", []))]
    print("overloaded virtual methods inside one interface: %d %s; interfaces with a virtual destructor: %d %s"
          % (len(overloads), overloads, len(vdtors), vdtors))
    flagged, wide, other = [], [], []
    for n, (r, meths) in sorted(ifaces.items()):
        for m in meths:
            params = [p for p in m.get("inner", []) if p.get("kind") == "ParmVarDecl"]
            rt = classify(ret_type(m["type"]["qualType"]), typedefs, enums, sizes)
            pc = [(classify(p["type"]["qualType"], typedefs, enums, sizes), p.get("name", "")) for p in params]
            why = []
            if rt[0] in ("fp", "struct", "long"):
                why.append("returns %s %s" % rt)
            for i, ((c, d), pn) in enumerate(pc):
                if c in ("fp", "struct", "long"):
                    why.append("arg%d %s: %s %s" % (i + 1, pn, c, d))
                if c == "other":
                    other.append("%s::%s arg%d %s" % (n, m["name"], i + 1, d))
            if rt[0] == "other":
                other.append("%s::%s returns %s" % (n, m["name"], rt[1]))
            if why:
                flagged.append("%s::%s  [%s]" % (n, m["name"], "; ".join(why)))
            wp = ["arg%d %s" % (i + 1, pn) for i, ((c, _), pn) in enumerate(pc) if c == "wptr"]
            if wp:
                wide.append("%s::%s  [%s]" % (n, m["name"], "; ".join(wp)))
    print("methods with a floating-point, struct-by-value or C 'long' argument or return value (need per-type thunk code): %d" % len(flagged))
    for f in flagged:
        print("  " + f)
    print("methods with a wide-string pointer argument (wchar_t is 2 bytes on Windows, 4 in the native headers): %d" % len(wide))
    for w in wide:
        print("  " + w)
    print("unclassified types (inspect by hand): %d" % len(other))
    for o in other:
        print("  " + o)
    print("interfaces (own virtual method counts): " + ", ".join("%s(%d)" % (n, len(m)) for n, (_, m) in sorted(ifaces.items())))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("what", nargs="?", default="all", choices=["types", "layout", "methods", "all"])
    ap.add_argument("--dxmt", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "third_party", "dxmt"))
    a = ap.parse_args()
    dxmt = os.path.abspath(a.dxmt)
    if not os.path.isfile(os.path.join(dxmt, "include/native/directx/d3d11.h")):
        sys.exit("need the DXMT clone with its include/native/directx submodule at %s" % dxmt)
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "shim"))
        for name, text in (("cstdint", SHIM_CSTDINT), ("cstring", SHIM_CSTRING)):
            with open(os.path.join(tmp, "shim", name), "w") as f:
                f.write(text)
        with open(os.path.join(tmp, "probe.cpp"), "w") as f:
            f.write(PROBE)
        if a.what in ("types", "all"):
            cmd_types(dxmt, tmp)
            print()
        mac, win, _ = cmd_layout(dxmt, tmp) if a.what in ("layout", "all") else (None, layouts(dxmt, tmp, "win"), None)
        if a.what in ("methods", "all"):
            print()
            cmd_methods(dxmt, tmp, win)


if __name__ == "__main__":
    main()
