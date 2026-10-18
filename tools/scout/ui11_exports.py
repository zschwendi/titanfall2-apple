#!/usr/bin/env python3
"""Question 1 for ui(11).dll: what does its export table contain?

Reads the export directory directly and reports:

  - how many exports, how many named, ordinal range, forwarders
  - whether each export RVA lands in an executable section (code) or not (data)
  - whether export RVAs are distinct (aliases share an address) and whether each one is the
    start of an entry in the x64 unwind table (.pdata)
  - the naming pattern: underscore tokens, the "__" suffix, and the most common tokens
  - 30 representative names, chosen deterministically across the sorted list
  - pairing: how many base names (with the "__hidden" suffix removed) have a sibling
  - whether ordinal order follows name order or address order

    .venv/bin/python tools/scout/ui11_exports.py "game/r2/paks/Win64/ui(11).dll" [--json OUT]

The script prints only counts, token statistics and names. The optional JSON holds names and
RVAs, not binary bytes.
"""

import argparse
import json
import os
import struct
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE  # noqa: E402


def read_exports(pe):
    """Return (directory info, list of export dicts) read straight from the export directory."""
    rva, size = pe.dirs[0]
    base_off = pe.rva_to_off(rva)
    if base_off is None:
        raise SystemExit("no export directory")
    d = pe.data
    (_, timestamp, _, _, name_rva, ordinal_base,
     n_funcs, n_names, funcs_rva, names_rva, ords_rva) = struct.unpack_from("<IIHHIIIIIII", d, base_off)
    fa = pe.rva_to_off(funcs_rva)
    na = pe.rva_to_off(names_rva)
    oa = pe.rva_to_off(ords_rva)
    func_rvas = [struct.unpack_from("<I", d, fa + 4 * i)[0] for i in range(n_funcs)]
    name_for_index = {}
    for i in range(n_names):
        (nrva,) = struct.unpack_from("<I", d, na + 4 * i)
        (idx,) = struct.unpack_from("<H", d, oa + 2 * i)
        name_for_index[idx] = pe.cstr(nrva)
    exports = []
    for idx, frva in enumerate(func_rvas):
        if frva == 0:
            continue
        sec = pe.section_of(frva)
        exports.append({
            "ordinal": ordinal_base + idx,
            "name": name_for_index.get(idx),
            "rva": frva,
            "section": sec["name"] if sec else None,
            "is_code": bool(sec and pe.is_exec(sec)),
            "forwarder": rva <= frva < rva + size,
        })
    info = {"dll_name": pe.cstr(name_rva), "ordinal_base": ordinal_base, "functions": n_funcs,
            "names": n_names, "timestamp": timestamp}
    return info, exports


def unwind_starts(pe):
    """Set of function begin RVAs listed in the x64 exception directory."""
    rva, size = pe.dirs[3]
    off = pe.rva_to_off(rva)
    return {struct.unpack_from("<I", pe.data, off + 12 * i)[0] for i in range(size // 12)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--json", help="write names and RVAs to this JSON file (no binary bytes)")
    args = ap.parse_args()

    pe = PE(args.path)
    info, exports = read_exports(pe)
    named = [e for e in exports if e["name"]]
    fwd = [e for e in exports if e["forwarder"]]
    code = [e for e in exports if e["is_code"]]
    rvas = [e["rva"] for e in exports]
    begins = unwind_starts(pe)

    print("DLL name in export directory: %r, timestamp 0x%x" % (info["dll_name"], info["timestamp"]))
    print("Export directory: NumberOfFunctions=%d NumberOfNames=%d OrdinalBase=%d"
          % (info["functions"], info["names"], info["ordinal_base"]))
    print("Exports with an address: %d   named: %d   forwarders: %d" % (len(exports), len(named), len(fwd)))
    ords = [e["ordinal"] for e in exports]
    print("Ordinal range: %d..%d (contiguous: %s)" % (min(ords), max(ords), sorted(ords) == list(range(min(ords), max(ords) + 1))))
    print("Export RVA in an executable section: %d   outside code: %d" % (len(code), len(exports) - len(code)))
    print("Distinct export RVAs: %d (aliased exports: %d)" % (len(set(rvas)), len(rvas) - len(set(rvas))))
    print("Export RVAs that are unwind-table function starts: %d of %d" % (sum(r in begins for r in rvas), len(rvas)))
    print("Exception directory entries: %d" % len(begins))

    # Ordering: does ordinal order follow the name, and does it follow the address?
    by_ord = sorted(exports, key=lambda e: e["ordinal"])
    names_in_ord = [e["name"] for e in by_ord]
    print("Ordinal order == sorted name order: %s" % (names_in_ord == sorted(names_in_ord)))
    rva_in_ord = [e["rva"] for e in by_ord]
    print("Ordinal order == ascending RVA order: %s" % (rva_in_ord == sorted(rva_in_ord)))
    gaps = sorted(b - a for a, b in zip(sorted(set(rvas)), sorted(set(rvas))[1:]))
    if gaps:
        print("Gap between consecutive export RVAs: min %d, median %d, max %d bytes"
              % (gaps[0], gaps[len(gaps) // 2], gaps[-1]))

    names = sorted(e["name"] for e in named)
    lens = sorted(len(n) for n in names)
    print("Name length: min %d, median %d, max %d" % (lens[0], lens[len(lens) // 2], lens[-1]))
    print("Names with an underscore: %d of %d" % (sum("_" in n for n in names), len(names)))
    print("Names with a double underscore: %d" % sum("__" in n for n in names))
    suffixes = Counter(n.split("__", 1)[1] for n in names if "__" in n)
    print("Double-underscore suffixes: %s" % ", ".join("%s(%d)" % kv for kv in suffixes.most_common()))
    print("Names that are not lowercase identifiers: %d" % sum(not all(c.islower() or c.isdigit() or c == "_" for c in n) for n in names))
    print("Names with a digit: %d" % sum(any(c.isdigit() for c in n) for n in names))
    first = Counter(n.split("_")[0] for n in names)
    print("Most common first tokens: %s" % ", ".join("%s(%d)" % kv for kv in first.most_common(12)))
    last = Counter(n.split("_")[-1] for n in names)
    print("Most common last tokens: %s" % ", ".join("%s(%d)" % kv for kv in last.most_common(12)))
    tok = Counter(t for n in names for t in n.replace("__", "_").split("_") if t)
    print("Most common tokens anywhere: %s" % ", ".join("%s(%d)" % kv for kv in tok.most_common(25)))

    # Pairing: strip the __suffix, then see which base names have more than one variant.
    base_of = defaultdict(list)
    for n in names:
        base_of[n.split("__", 1)[0]].append(n)
    multi = {b: v for b, v in base_of.items() if len(v) > 1}
    print("Distinct base names (suffix removed): %d; base names with two or more variants: %d"
          % (len(base_of), len(multi)))
    both = [b for b, v in multi.items() if b in names and (b + "__hidden") in names]
    print("Base names with both the plain name and the __hidden variant: %d" % len(both))
    hidden = [n for n in names if n.endswith("__hidden")]
    no_plain = [n for n in hidden if n[:-len("__hidden")] not in names]
    print("__hidden exports: %d; of these, without a plain sibling: %d" % (len(hidden), len(no_plain)))
    # Shared-prefix families: first two tokens.
    fam = Counter("_".join(n.split("__", 1)[0].split("_")[:2]) for n in names)
    print("Two-token families: %d; largest: %s" % (len(fam), ", ".join("%s(%d)" % kv for kv in fam.most_common(10))))

    step = max(1, len(names) // 30)
    sample = names[::step][:30]
    print("30 representative names (every %d-th in sorted order):" % step)
    for n in sample:
        print("  " + n)

    if args.json:
        with open(args.json, "w") as f:
            json.dump({"info": info, "counts": {"exports": len(exports), "named": len(named),
                       "forwarders": len(fwd), "code": len(code)}, "sample30": sample,
                       "exports": [{k: v for k, v in e.items() if k != "section"} for e in exports]},
                      f, indent=1)
        print("wrote %s" % args.json)


if __name__ == "__main__":
    main()
