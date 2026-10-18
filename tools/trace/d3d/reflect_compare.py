#!/usr/bin/env python3
"""Hold what a run of the recompiled game answered about vertex-shader input signatures against
the input layouts the real game was recorded creating (unit d3d-reflect).

    .venv/bin/python tools/trace/d3d/reflect_compare.py [build/gate3/run.log] [docs/trace/d3d-creators.md]

Our side, from the run's log (channel com; runtime/nulldev/reflection.c and blob.c write the lines):
    reflection #N: ...                  one per D3DReflect: the container's size and chunks
    reflection #N: input i: ...         each input parameter GetInputParameterDesc then answers
    blob #N: ...                        one per D3DGetBlobPart: the size of the signature part
    input layout #N: ...                one per CreateInputLayout: its elements and the signature passed
The recorded side: the table "CreateInputLayout" of docs/trace/d3d-creators.md (51 calls, 39
distinct rows; each row has the layout's elements and BytecodeLength, the size of the
signature part the game passed).  Nothing is read from the recording itself or from the game.

Printed: counts of our side; whether every recorded length is 44 bytes more than an ISGN chunk
of exactly the row's semantic names (8 + 24 per element + each distinct name once with its NUL,
rounded up to 4); for each recorded row the signatures of our run with the same set of
(semantic name, index), the part's length and each element's component type and count (a row
with no such signature is not a fault: the recording goes on to shaders a short run never
creates); and, when the run created input layouts, each one against the signature passed with
it and against the recorded rows in order.  Exit 1 on a fault: a part that does not fit its
reflection, or a layout whose elements are not its signature's.
Only semantic names, numbers and sizes are printed.
"""
import re
import sys

FLOATISH = ("FLOAT", "UNORM", "SNORM")


def part_len(names):
    seen, n = [], 0
    for name in names:
        if name not in seen:
            seen.append(name)
            n += len(name) + 1
    data = 8 + 24 * len(names) + n
    return 44 + data + (-data % 4)


def fmt_type(fmt):
    if fmt.endswith("_UINT"): return 1
    if fmt.endswith("_SINT"): return 2
    if fmt.endswith(FLOATISH): return 3
    return 0


def fmt_count(fmt):
    return len(re.findall(r"[RGBAX]\d+", fmt.split("_")[0]))


def recorded(path):
    rows, on = [], False
    for line in open(path):
        if line.startswith("### `CreateInputLayout`"):
            on = True
            continue
        if on and line.startswith("### "):
            break
        m = on and re.match(r"\| (\d+) \| (\d+) \| (\d+) elements: (.*); = S_OK \| BytecodeLength (\d+) \|", line)
        if m:
            els = []
            for e in m.group(4).split(", "):
                x = re.match(r"([A-Z_]+?)(\d+):([A-Z0-9_]+)@(\d+)\+(\d+)( per instance/(\d+))?$", e)
                els.append((x.group(1), int(x.group(2)), x.group(3), int(x.group(4)), int(x.group(5)), int(x.group(7) or 0)))
            assert len(els) == int(m.group(3))
            rows.append({"count": int(m.group(1)), "first": int(m.group(2)), "els": els, "len": int(m.group(5))})
    return sorted(rows, key=lambda r: r["first"])


def ours(path):
    refl, blobs, layouts, last = {}, [], {}, None
    for line in open(path, errors="replace"):
        if not line.startswith("com: "):
            continue
        m = re.match(r"com: reflection #(\d+): a container of (\d+) bytes, checksum right, chunks(.*); (ISGN|ISG1) with (\d+) input", line)
        if m:
            last = int(m.group(1))
            chunks = dict((c.split(":")[0], int(c.split(":")[1])) for c in m.group(3).split())
            refl[last] = {"size": int(m.group(2)), "chunks": chunks, "tag": m.group(4), "n": int(m.group(5)), "els": []}
            continue
        m = re.match(r"com: reflection #(\d+): input \d+: (\S+?)(\d+) register (\d+) system value (\d+) component type (\d+) mask (\S+) read (\S+)", line)
        if m:
            refl[int(m.group(1))]["els"].append((m.group(2), int(m.group(3)), int(m.group(6)), int(m.group(7), 16), int(m.group(8), 16),
                                               int(m.group(4)), int(m.group(5))))
            continue
        m = re.match(r"com: blob #(\d+): the input-signature part of a container of (\d+) bytes: (\d+) bytes \(an ISGN chunk of (\d+)\)", line)
        if m:
            blobs.append({"id": int(m.group(1)), "src": int(m.group(2)), "len": int(m.group(3)), "isgn": int(m.group(4)), "after": last})
            continue
        m = re.match(r"com: input layout #(\d+): (\d+) elements, with (\d+) bytes", line)
        if m:
            layouts[int(m.group(1))] = {"n": int(m.group(2)), "len": int(m.group(3)), "els": [], "sig": []}
            continue
        m = re.match(r"com: input layout #(\d+): element \d+: (\S+?)(\d+) format (\d+) slot (\d+) offset (\d+) class (\d+) rate (\d+)", line)
        if m:
            layouts[int(m.group(1))]["els"].append((m.group(2),) + tuple(int(m.group(i)) for i in range(3, 9)))
            continue
        m = re.match(r"com: input layout #(\d+): signature \d+: (\S+?)(\d+) register (\d+) component type (\d+)", line)
        if m:
            layouts[int(m.group(1))]["sig"].append((m.group(2), int(m.group(3)), int(m.group(5))))
    return refl, blobs, layouts


def main():
    log = sys.argv[1] if len(sys.argv) > 1 else "build/gate3/run.log"
    doc = sys.argv[2] if len(sys.argv) > 2 else "docs/trace/d3d-creators.md"
    rows = recorded(doc)
    refl, blobs, layouts = ours(log)
    bad = 0

    print("our run: %d shaders reflected, %d input parameters answered, %d signature parts made, %d input layouts created"
          % (len(refl), sum(len(r["els"]) for r in refl.values()), len(blobs), len(layouts)))
    for r in refl.values():
        if len(r["els"]) != r["n"]:
            bad += 1
            print("  FAULT: a reflection logged %d of its %d parameters" % (len(r["els"]), r["n"]))
    sigs = {}
    for k in sorted(refl):
        sigs.setdefault(tuple(refl[k]["els"]), []).append(k)
    print("  distinct signatures: %d; parameters per shader: %d to %d; all chunks named ISGN: %s"
          % (len(sigs), min(r["n"] for r in refl.values()), max(r["n"] for r in refl.values()),
             all(r["tag"] == "ISGN" for r in refl.values())) if refl else "  no reflection in the log")
    # Each part against the reflection before it (the game reflects, then asks for the part of the same bytes),
    # and against the length an ISGN chunk of that signature's names has.
    ok = 0
    for b in blobs:
        r = refl.get(b["after"])
        good = (r is not None and r["size"] == b["src"] and r["chunks"].get("ISGN") == b["isgn"] and b["len"] == 44 + b["isgn"]
                and b["len"] == part_len([e[0] for e in r["els"]]))
        ok += good
        if not good:
            bad += 1
            print("  FAULT: part #%d (%d bytes, chunk %d, source %d) does not fit the reflection before it" % (b["id"], b["len"], b["isgn"], b["src"]))
    print("  parts that are 44 bytes more than their shader's ISGN chunk and as long as that signature's names make them: %d of %d" % (ok, len(blobs)))

    print("the recording: %d CreateInputLayout calls in %d distinct rows" % (sum(r["count"] for r in rows), len(rows)))
    fit = [r for r in rows if part_len([e[0] for e in r["els"]]) == r["len"]]
    print("  rows whose BytecodeLength is that of a part with exactly the row's semantic names: %d of %d" % (len(fit), len(rows)))
    for r in rows:
        if r not in fit:
            print("    call %d: recorded %d, computed %d" % (r["first"], r["len"], part_len([e[0] for e in r["els"]])))

    print("each recorded row against the signatures of our run (a signature agrees when it has the row's semantic names and")
    print("indexes, makes a part of the row's length, and each element's component type and count are its format's):")
    found = same_names = 0
    for r in rows:
        want = sorted((e[0], e[1]) for e in r["els"])
        hits = [(s, ids) for s, ids in sigs.items() if sorted((e[0], e[1]) for e in s) == want]
        agree = []
        for s, ids in hits:
            mine = dict(((e[0], e[1]), (e[2], bin(e[3]).count("1"))) for e in s)
            if part_len([e[0] for e in s]) == r["len"] and all(mine[(e[0], e[1])] == (fmt_type(e[2]), fmt_count(e[2])) for e in r["els"]):
                agree.append((s, ids))
        found += bool(agree)
        same_names += bool(hits) and not agree
        print("  call %-7d %d elements, %3d bytes: %s" % (r["first"], len(r["els"]), r["len"],
              "%d signature (%d shaders) agrees" % (len(agree), sum(len(i) for _, i in agree)) if agree else
              "%d signatures have the names with other types" % len(hits) if hits else "no shader of our run has these names"))
    print("  recorded rows with an agreeing signature in our run: %d of %d; with the names only: %d; with neither: %d"
          % (found, len(rows), same_names, len(rows) - found - same_names))

    if layouts:
        print("the input layouts our run created, in order, against the recorded calls in order:")
        calls = sorted(rows, key=lambda r: r["first"])
        for i, k in enumerate(sorted(layouts)):
            l = layouts[k]
            own = sorted((e[0], e[1]) for e in l["els"]) == sorted((e[0], e[1]) for e in l["sig"])
            rec = calls[i] if i < len(calls) else None
            same = rec is not None and [(e[0], e[1], e[3], e[4]) for e in rec["els"]] == [(e[0], e[1], e[3], e[4]) for e in l["els"]] \
                and rec["len"] == l["len"]
            if not own:
                bad += 1
            print("  layout %d: %d elements, %d bytes; its elements %s the signature passed with them; recorded row %s"
                  % (i + 1, l["n"], l["len"], "are" if own else "ARE NOT (FAULT)",
                     ("call %d: %s" % (rec["first"], "same names, slots, offsets and length" if same else "differs")) if rec else "none left"))
    else:
        print("our run created no input layout: nothing to hold against the recorded calls in order")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
