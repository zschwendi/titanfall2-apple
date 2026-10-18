#!/usr/bin/env python3
"""dxmt_method_cover.py: the Direct3D 11 and DXGI methods the game calls, held against the
source of the DXMT fork: where each is defined and which of DXMT's own markers of an
unfinished path its body carries.  Written for docs/handoff/design-picture.md (section 1).

It reads two things and no game file:
  docs/findings/31-d3d11-usage.json   the (interface, method) pairs finding 31 read from the
                                      code of materialsystem_dx11 (names and counts only)
  third_party/dxmt/src                the fork's source, at whatever commit is checked out

    .venv/bin/python tools/scout/dxmt_method_cover.py            the table (markdown) and the totals
    .venv/bin/python tools/scout/dxmt_method_cover.py --flagged  only the rows with a marker
    .venv/bin/python tools/scout/dxmt_method_cover.py --selftest

HOW IT READS THE SOURCE.  DXMT writes a method as `STDMETHODCALLTYPE` followed by the name and
`(`; the body is taken by matching braces from the first `{` after the parameter list.  An
interface is looked for only in the files that implement it (FILES below); the template in
d3d11_context_impl.cpp serves both context kinds, d3d11_context_def.cpp (deferred contexts,
which the game never creates: finding 31) is left out.  A method defined more than once in
those files (GetDesc of several classes) gives one row per definition.

THE MARKERS (each a text search in the body, nothing more):
  abort     UNIMPLEMENTED(   DXMT logs and calls abort()  (src/d3d11/d3d11_private.h)
  notimpl   E_NOTIMPL
  logs      ERR( / WARN( and their _ONCE forms: the method can report through DXMT's log
  note      TODO / FIXME / "not supported" / "not implemented" / IMPLEMENT_ME
  empty     nothing but an optional lock and `return <one word>;`: the call is taken and dropped
A marker says "read this body", not "this is broken": an abort inside one branch is reached
only by the arguments of that branch.  The plan's table says what each flagged row is.
"""
import argparse
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# Methods the recording added to finding 31's list (docs/findings/40-d3d-call-trace.md, first page).
TRACED = [("ID3D11RenderTargetView", "GetDesc"), ("ID3D11ShaderResourceView", "GetDesc")]

CTX = ["src/d3d11/d3d11_context_impl.cpp", "src/d3d11/d3d11_context_imm.cpp"]
FILES = {
    "ID3D11DeviceContext": CTX,
    "ID3D11Device": ["src/d3d11/d3d11_device.cpp"],
    "IDXGIDevice1": ["src/d3d11/d3d11_device.cpp"],
    "IDXGISwapChain": ["src/d3d11/d3d11_swapchain.cpp"],
    "IDXGIFactory1": ["src/dxgi/dxgi_factory.cpp"],
    "IDXGIAdapter1": ["src/dxgi/dxgi_adapter.cpp"],
    "IDXGIOutput": ["src/dxgi/dxgi_output.cpp"],
}
OURS = {"ID3D11ShaderReflection": "ours: D3DReflect is not in DXMT (runtime/nulldev/reflection.c)",
        "ID3D10Blob": "ours: D3DGetBlobPart is not in DXMT (runtime/nulldev/blob.c)"}
COMMON = ("QueryInterface", "AddRef", "Release")

DEF = re.compile(r"STDMETHODCALLTYPE\s+(\w+)\s*\(")
MARKS = [("abort", re.compile(r"\bUNIMPLEMENTED\s*\(")),
         ("notimpl", re.compile(r"\bE_NOTIMPL\b")),
         ("logs", re.compile(r"\b(ERR|WARN)(_ONCE)?\s*\(")),
         ("note", re.compile(r"TODO|FIXME|IMPLEMENT_ME|not supported|not implemented", re.I))]
EMPTY = re.compile(r"\{\s*(std::lock_guard[^;]*;\s*)?(return(\s+[\w:]+)?\s*;\s*)?\}\Z")


def strip_comments(text):
    """Blank out // and /* */ comments and string contents, keeping every newline and offset."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i)); i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in text[i:j])); i = j
        elif c == '"':
            j = i + 1
            while j < n and text[j] != '"':
                j += 2 if text[j] == "\\" else 1
            out.append('"' + " " * (j - i - 1) + '"'); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)


def definitions(text):
    """{name: [(line, body_text, body_code)]} for every method with a body in `text`."""
    code = strip_comments(text)
    found = {}
    for m in DEF.finditer(code):
        i, depth = m.end(), 1
        while i < len(code) and depth:                      # the parameter list
            depth += {"(": 1, ")": -1}.get(code[i], 0); i += 1
        j = i
        while j < len(code) and code[j] not in "{;":        # `override`, `final`, `= 0`
            j += 1
        if j >= len(code) or code[j] == ";":
            continue                                        # a declaration
        k, depth = j + 1, 1
        while k < len(code) and depth:
            depth += {"{": 1, "}": -1}.get(code[k], 0); k += 1
        line = code.count("\n", 0, m.start(1)) + 1
        found.setdefault(m.group(1), []).append((line, text[j:k], code[j:k]))
    return found


def classify(body_text, body_code):
    marks = [name for name, rx in MARKS[:3] if rx.search(body_code)]
    if MARKS[3][1].search(body_text):
        marks.append("note")
    if EMPTY.match(body_code):
        marks.append("empty")
    return marks


def game_methods(usage, module):
    ifaces = json.load(open(usage))["modules"][module]["interfaces"]
    pairs = [(i, c["method"]) for i in sorted(ifaces) for c in ifaces[i]["called"]]
    return pairs + [p for p in TRACED if p not in pairs]


def cover(pairs, dxmt):
    cache, every = {}, None
    rows = []
    for iface, method in pairs:
        if iface in OURS:
            rows.append((iface, method, "-", [], OURS[iface])); continue
        if method in COMMON:
            rows.append((iface, method, "-", [], "COM plumbing: not looked at")); continue
        files = FILES.get(iface)
        if files is None:
            if every is None:
                every = sorted(os.path.join("src/d3d11", f) for f in os.listdir(os.path.join(dxmt, "src/d3d11"))
                               if f.endswith((".cpp", ".hpp")) and "context_def" not in f)
            files = every
        hits = []
        for f in files:
            if f not in cache:
                cache[f] = definitions(open(os.path.join(dxmt, f), errors="replace").read())
            for line, text, code in cache[f].get(method, []):
                hits.append(("%s:%d" % (f, line), classify(text, code)))
        if not hits:
            rows.append((iface, method, "NOT FOUND", ["missing"], "no definition with a body in the files searched"))
        for where, marks in hits:
            rows.append((iface, method, where, marks, ""))
    return rows


def selftest():
    src = ('struct A {\n  HRESULT\n  STDMETHODCALLTYPE\n  One(int a) override {\n    // UNIMPLEMENTED(x) in a comment\n'
           '    if (a) { UNIMPLEMENTED("one"); }\n    a++; // FIXME later\n    return S_OK;\n  }\n'
           '  void STDMETHODCALLTYPE Two() final { }\n'
           '  HRESULT STDMETHODCALLTYPE Three(int a, void (*f)(int)) = 0;\n'
           '  HRESULT STDMETHODCALLTYPE Four() { WARN("x; TODO {"); a(); b(); c(); return E_NOTIMPL; }\n};\n')
    d = definitions(src)
    assert sorted(d) == ["Four", "One", "Two"], sorted(d)
    assert d["One"][0][0] == 4 and classify(*d["One"][0][1:]) == ["abort", "note"], d["One"]
    assert classify(*d["Two"][0][1:]) == ["empty"]
    assert classify(*d["Four"][0][1:]) == ["notimpl", "logs", "note"], classify(*d["Four"][0][1:])
    print("dxmt_method_cover selftest: ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dxmt", default=os.path.join(ROOT, "third_party/dxmt"))
    ap.add_argument("--usage", default=os.path.join(ROOT, "docs/findings/31-d3d11-usage.json"))
    ap.add_argument("--module", default="materialsystem_dx11")
    ap.add_argument("--flagged", action="store_true", help="only rows with abort, notimpl, note, empty or missing")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not os.path.isdir(os.path.join(a.dxmt, "src/d3d11")):
        sys.exit("dxmt_method_cover: no DXMT source at %s" % a.dxmt)
    pairs = game_methods(a.usage, a.module)
    rows = cover(pairs, a.dxmt)
    loud = {"abort", "notimpl", "note", "empty", "missing"}
    print("| interface | method | defined at | markers | |\n|---|---|---|---|---|")
    for iface, method, where, marks, remark in rows:
        if a.flagged and not (loud & set(marks)):
            continue
        print("| %s | %s | %s | %s | %s |" % (iface, method, where, " ".join(marks), remark))
    looked = [r for r in rows if r[2] != "-"]
    names = {(r[0], r[1]) for r in looked}
    count = lambda mark: len({(r[0], r[1]) for r in looked if mark in r[3]})
    print("\n%d pairs of the game; %d are COM plumbing or ours; %d looked for in DXMT (%d definitions): "
          "missing %d, abort %d, notimpl %d, note %d, empty %d, logs %d"
          % (len(pairs), len(pairs) - len(names), len(names), len(looked), count("missing"), count("abort"),
             count("notimpl"), count("note"), count("empty"), count("logs")))


if __name__ == "__main__":
    main()
