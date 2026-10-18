#!/usr/bin/env python3
"""Compare runtime/include/rt/win/d3d11.h, d3d11view.h and dxgi.h with the MinGW-w64 headers they were written from.

    .venv/bin/python runtime/tools/check_d3d_layout.py        (make d3d-layout-check)

Our headers give Direct3D's and DXGI's structures as fixed-width C and its enumerators as #defines, each
with `_Static_assert`s on size and offsets (docs/RUNTIME.md 3.3).  Our compiler proves the
asserts against OUR structures; this script proves the same numbers against the headers the
interface descriptions were generated from (third_party/dxmt/include/native/directx: MinGW-w64's
d3d11.h, dxgi.h, d3d11shader.h, d3dcommon.h), compiled for the Windows layout: LLP64 types come
from those headers' own typedefs, and -fshort-wchar makes WCHAR 16 bits (without it the six
structures with a WCHAR array differ: finding 35, docs/findings/35-dxmt-native.md).

It reads from our header:
    _Static_assert(sizeof(struct X) == N, ...)          -> static_assert(sizeof(X) == N)
    _Static_assert(offsetof(struct X, a.b) == N, ...)   -> static_assert(offsetof(X, a.b) == N)
    #define NAME value                                  -> static_assert(NAME == value)
and writes one C++ file of static_asserts that includes only the MinGW headers.  A #define
that is ours and not a name of those headers carries the comment /* ours */ on its line and
is skipped.  It also fails when a structure of our header has no sizeof assert, or a
top-level field of it has no offsetof assert.

Exit 0: everything agrees.  Without the headers (they are not in git): "SKIPPED", exit 0.
"""

import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HEADERS = [os.path.join(ROOT, "runtime", "include", "rt", "win", n) for n in ("dxgi.h", "d3d11.h", "d3d11view.h")]
NATIVE = os.path.join(ROOT, "third_party", "dxmt", "include", "native")


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", lambda m: " " * len(m.group(0)) if "\n" not in m.group(0) else
                  "\n" * m.group(0).count("\n"), text, flags=re.S)


def structs(text):
    """{name: [top-level field names]} for every `struct NAME { ... };` of the header."""
    out = {}
    for m in re.finditer(r"^struct\s+(\w+)\s*\{", text, re.M):
        depth, i, fields, start = 1, m.end(), [], m.end()
        while depth:
            c = text[i]
            if c == "{":
                depth += 1
            elif c == "}":
                depth -= 1
                if depth == 1:
                    start = None                       # the declarator after a nested body follows
            elif c == ";" and depth == 1:
                decl = text[(start if start is not None else text.rfind("}", 0, i) + 1):i]
                name = re.search(r"(\w+)\s*(\[[^\]]*\]\s*)*$", decl.strip())
                if name:
                    fields.append(name.group(1))
                start = i + 1
            i += 1
        out[m.group(1)] = fields
    return out


def main():
    raw = "\n".join(open(h).read() for h in HEADERS if os.path.exists(h))
    text = strip_comments(raw)
    problems, asserts = [], []
    sized, offs = set(), {}
    for m in re.finditer(r"_Static_assert\(\s*sizeof\(struct (\w+)\)\s*==\s*(\w+)", text):
        sized.add(m.group(1))
        asserts.append("static_assert(sizeof(%s) == %s, \"sizeof %s\");" % (m.group(1), m.group(2), m.group(1)))
    for m in re.finditer(r"_Static_assert\(\s*offsetof\(struct (\w+),\s*([\w.\[\]]+)\)\s*==\s*(\w+)", text):
        offs.setdefault(m.group(1), set()).add(m.group(2).split(".")[0].split("[")[0])
        asserts.append("static_assert(offsetof(%s, %s) == %s, \"%s.%s\");" % (
            m.group(1), m.group(2), m.group(3), m.group(1), m.group(2)))
    for name, fields in structs(text).items():
        if name not in sized:
            problems.append("struct %s has no _Static_assert on its size" % name)
        for f in fields:
            if f not in offs.get(name, ()):
                problems.append("struct %s: field %s has no _Static_assert on its offset" % (name, f))
    ndef = 0
    for line in raw.split("\n"):
        m = re.match(r"#define\s+(\w+)\s+(.+?)\s*(/\*.*)?$", line)
        if not m or m.group(1).startswith("RT_") or "/* ours" in line:
            continue
        ndef += 1
        value = re.sub(r"\(\s*HRESULT\s*\)", "(int)", m.group(2))
        asserts.append("static_assert((unsigned long long)(%s) == (unsigned long long)(%s), \"%s\");" % (
            m.group(1), value, m.group(1)))
    if problems:
        print("\n".join(problems))
        sys.exit(1)
    if not os.path.isdir(os.path.join(NATIVE, "directx")):
        print("d3d-layout-check: SKIPPED, %s is not here (the headers are not in git)" % NATIVE)
        return
    src = "#include <cstddef>\n#include <d3d11.h>\n#include <dxgi.h>\n#include <d3d11shader.h>\n#include <d3dcommon.h>\n"
    src += "\n".join(asserts) + "\nint main() { return 0; }\n"
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, "layout.cpp")
        open(path, "w").write(src)
        r = subprocess.run(["c++", "-std=c++17", "-fsyntax-only", "-fshort-wchar", "-w", "-ferror-limit=0",
                            "-I" + os.path.join(NATIVE, "directx"), "-I" + os.path.join(NATIVE, "windows"),
                            "-I" + NATIVE, path], capture_output=True, text=True)
    if r.returncode:
        errs = [l for l in r.stderr.split("\n") if "error" in l]
        print("\n".join(e.split("error: ", 1)[-1] for e in errs[:60]))
        print("d3d-layout-check: %d of %d comparisons with the MinGW headers fail" % (len(errs), len(asserts)))
        sys.exit(1)
    print("d3d-layout-check: %d structures, %d sizes and offsets, %d constants agree with the MinGW headers" % (
        len(sized), len(asserts) - ndef, ndef))


if __name__ == "__main__":
    main()
