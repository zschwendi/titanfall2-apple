#!/usr/bin/env python3
"""Instruction index: every instruction of every DLL, as the lifter's names for it, cached on disk.

For each DLL (the game's under game/, the compiled test corpus under tests/corpus/build/) two
passes, both independent of the translator registry: they record what the decoder and the lifter's
discovery say about the code, never whether an instruction is translated.

  1. Linear sweep over every unwind-table range (as tools/census/insn_census.py does, one byte
     skipped to resynchronise after anything undecodable).  Every decoded instruction is an
     instance, with m = lifter.translate.mnemonic_of(ins) and s = lifter.translate.shape(ins).
  2. Reachability.  Program(image).discover(), then for every function the CFG the lifter
     builds (build_cfg).  An instance is reached if its address is in the union of all those
     CFGs.  Reached instructions the sweep did not decode are instances too: those of leaf
     functions outside every unwind range, and the occasional instruction inside a range that
     the sweep missed because the data in front of it was decoded across its first bytes.  An
     address reached from two functions (a shared tail, an entry into the middle of a function)
     is one instance.

The linear sweep decodes some data that lies inside code ranges (jump tables, padding) as
instructions - `in`, `out`, `iretd`.  Those instances are never reached: that is how decoded
data is told apart from code, "an instruction the lifter's recursive descent never reaches".
The converse does not hold.  Unreached is not always data: code behind an unresolved indirect
jump, and exception-handler funclets (entered only through the scope tables in .xdata), are
real code that the descent does not reach.  Nor is reached always code: the descent can run on
into data (d3dcompiler_46 has pointer tables in its code section, and the instructions decoded
from them are reached).

The result per DLL is one JSON file
inventory/insn_index/<module>.<size>.<16 hex of sha256>.<rules>.json, where <rules> is the first 12
hex digits of a SHA-256 over INDEX_VERSION, SAMPLES, BASELINE, the source of the translate.py
functions mnemonic_of, shape and legacy_prefixes and the tables they read, and the files
lifter/discover.py, lifter/image.py and tools/inventory/pe_inventory.py.  A file is written once and only read
afterwards; files of other sizes, hashes or rules are left alone (the directory is shared).
An entry has exactly these keys:

    module, group ("game", "redist" or "corpus"), path (relative to the repo root), size,
    sha256, rules, samples, instructions, reached, undecoded_bytes, functions,
    mnemonics: {m: {count, reached, functions (that contain m), shapes: {s: {count, reached,
        rvas: up to SAMPLES addresses, evenly spread over the reached instances of s, or over
        all of them if none is reached}}}},
    blocker_sets: [[names, number of functions with exactly that set], ...] - per function the
        reached mnemonics that are not in BASELINE; only the non-empty sets,
    functions_clean: functions whose set is empty.

Other code imports this file:  REPO, INDEX_DIR, SAMPLES, module_name, find_dlls, load, game_count.

    .venv/bin/python tools/census/insn_index.py                           # build or refresh, summary
    .venv/bin/python tools/census/insn_index.py --worklist [--json FILE]   # what is left to translate
    .venv/bin/python tools/census/insn_index.py --mnemonic pshufd          # where one mnemonic occurs
    .venv/bin/python tools/census/insn_index.py --forms                    # refused forms of translated ones

--jobs N sets the number of worker processes.  Always run it as a file (multiprocessing).
"""

import argparse
import contextlib
import glob
import hashlib
import inspect
import json
import os
import sys
import tempfile
import time
import traceback
from array import array
from collections import Counter
from multiprocessing import Pool

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)
sys.path.insert(0, os.path.join(REPO, "tools", "inventory"))
from pe_inventory import PE  # noqa: E402

import capstone  # noqa: E402

from lifter import translate  # noqa: E402
from lifter.discover import Func, Program  # noqa: E402
from lifter.image import Image  # noqa: E402
from lifter.lift import lift_function  # noqa: E402
from lifter.translate import Translator, mnemonic_of, shape  # noqa: E402

INDEX_DIR = os.path.join(REPO, "inventory", "insn_index")
INDEX_VERSION = 1       # bump when this file changes what an entry contains
SAMPLES = 8             # sample addresses kept per shape
REDIST = frozenset({"d3dcompiler_46.dll"})      # Microsoft's shader compiler: indexed, but not game code

# The mnemonics translated on 2026-10-08: sorted(lifter.translate.HANDLERS), 168 names.  Frozen on
# purpose.  The index must not depend on the registry, which grows; blocker_sets are computed
# against this list, and reports subtract whatever the current registry translates.
BASELINE = frozenset((
    "adc add and andnpd andnps andpd andps bsf bsr bswap bt btc btr bts call cbw cdq cdqe clc cld cmc "
    "cmova cmovae cmovb cmovbe cmove cmovg cmovge cmovl cmovle cmovne cmovno cmovnp cmovns cmovo cmovp "
    "cmovs cmp cmpsb cmpsl cmpsq cmpsw cmpxchg cmpxchg16b cpuid cqo cwd cwde dec div hlt idiv imul inc "
    "int int3 ja jae jb jbe je jecxz jg jge jl jle jmp jne jno jnp jns jo jp jrcxz js lea leave lfence "
    "lodsb lodsl lodsq lodsw mfence mov movabs movapd movaps movd movdqa movdqu movq movsb movsd movsl "
    "movsq movss movsw movsx movsxd movupd movups movzx mul neg nop not or orpd orps pand pandn pause pop "
    "por prefetch prefetchnta prefetcht0 prefetcht1 prefetcht2 prefetchw push pxor rdtsc ret rol ror sal "
    "sar sbb scasb scasl scasq scasw seta setae setb setbe sete setg setge setl setle setne setno setnp "
    "setns seto setp sets sfence shl shld shr shrd stc std stosb stosl stosq stosw sub test ud2 xadd xchg "
    "xor xorpd xorps").split())

GROUP_ORDER = ("game", "redist", "corpus")
LAST_LOAD = {"cached": 0, "built": 0, "skipped": [], "failed": []}      # what the last load() did


# ---- files and cache keys ---------------------------------------------------------------------------

def module_name(path):
    """The module name Image.from_pe gives a file by default: the base name without extension,
    lower case, every character that is not a letter or digit replaced by '_'."""
    name = os.path.splitext(os.path.basename(path))[0].lower()
    return "".join(c if c.isalnum() else "_" for c in name)


def find_dlls(repo=REPO):
    """[(group, module, absolute path)] sorted by (group, module): every DLL under game/ (group
    "game", or "redist" for REDIST) and under tests/corpus/build/ (group "corpus")."""
    found = []
    for p in glob.glob(os.path.join(repo, "game", "**", "*.dll"), recursive=True):
        if os.path.basename(p).lower() == "originsdk.dll":
            continue                            # the project's rule: EA's SDK is never disassembled
        group = "redist" if os.path.basename(p).lower() in REDIST else "game"
        found.append((group, module_name(p), p))
    for p in glob.glob(os.path.join(repo, "tests", "corpus", "build", "*.dll")):
        found.append(("corpus", module_name(p), p))
    return sorted(found)


def rules_hash(repo=REPO):
    """First 12 hex digits of the SHA-256 over everything that decides what an index entry says."""
    h = hashlib.sha256()

    def feed(label, data):
        h.update(label.encode() + b"\0" + data + b"\0")

    feed("index-version", str(INDEX_VERSION).encode())
    feed("samples", str(SAMPLES).encode())
    feed("baseline", " ".join(sorted(BASELINE)).encode())
    for fn in (translate.mnemonic_of, translate.shape, translate.legacy_prefixes):
        feed("translate." + fn.__name__, inspect.getsource(fn).encode())
    for name in ("LEGACY_PREFIXES", "STRING_OPCODES", "_STRING_KIND"):      # the tables those functions read
        feed("translate." + name, repr(getattr(translate, name)).encode())
    for rel in ("lifter/discover.py", "lifter/image.py", "tools/inventory/pe_inventory.py"):
        with open(os.path.join(repo, rel), "rb") as f:
            feed(rel, f.read())
    return h.hexdigest()[:12]


def _file_facts(path):
    """(size, sha256 hex, reason it cannot be indexed or None)."""
    with open(path, "rb") as f:
        sha = hashlib.file_digest(f, "sha256").hexdigest()
    size = os.path.getsize(path)
    try:
        pe = PE(path)
    except Exception as e:                                  # not a PE file at all
        return size, sha, "not a PE file (%s)" % e
    if not pe.pe32plus or pe.machine != 0x8664:
        return size, sha, "not an x86-64 PE32+ file"
    return size, sha, None


def _cache_path(module, size, sha, rules):
    return os.path.join(INDEX_DIR, "%s.%d.%s.%s.json" % (module, size, sha[:16], rules))


def _write_entry(entry, path):
    """Write the entry atomically: a temp file in the same directory, then os.replace.  One
    line per mnemonic, so that the file can be grepped."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    fd, tmp = tempfile.mkstemp(dir=os.path.dirname(path), prefix="." + os.path.basename(path) + ".", suffix=".tmp")
    try:
        with os.fdopen(fd, "w") as f:
            keys = list(entry)
            f.write("{\n")
            for i, k in enumerate(keys):
                if k == "mnemonics":
                    f.write(' "mnemonics":{\n')
                    items = sorted(entry[k].items())
                    for j, (m, d) in enumerate(items):
                        f.write("  %s:%s%s\n" % (json.dumps(m), json.dumps(d, separators=(",", ":")),
                                                 "," if j + 1 < len(items) else ""))
                    f.write(" }")
                else:
                    f.write(" %s:%s" % (json.dumps(k), json.dumps(entry[k], separators=(",", ":"))))
                f.write(",\n" if i + 1 < len(keys) else "\n")
            f.write("}\n")
        os.chmod(tmp, 0o644)                    # mkstemp makes it private; other agents read the file
        os.replace(tmp, path)
    except BaseException:
        with contextlib.suppress(OSError):
            os.unlink(tmp)
        raise


def _read_entry(path):
    with open(path) as f:
        return json.load(f)


# ---- building one entry -----------------------------------------------------------------------------

def _spread(rvas, k=SAMPLES):
    """At most k addresses, deterministic and evenly spread over the sorted candidates."""
    lst = sorted(rvas)
    if len(lst) > k:
        lst = [lst[i * len(lst) // k] for i in range(k)]
    return lst


def index_image(im):
    """The two passes over one Image.  Returns (the part of the entry from "instructions" to
    "functions_clean", statistics for the progress line)."""
    n_img = im.size
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    sid_at = array("I", bytes(4 * n_img))   # 1 + id of the (mnemonic, shape) pair of the instance at an RVA, or 0
    keys = []                               # id -> (mnemonic, shape)
    intern = {}                             # (mnemonic, shape) -> id

    def sid_of(ins):
        key = (mnemonic_of(ins), shape(ins))
        sid = intern.get(key)
        if sid is None:
            sid = intern[key] = len(keys)
            keys.append(key)
        return sid

    # Pass 1: linear sweep over the unwind ranges.
    swept = array("I")                      # RVAs of the sweep's instances
    decoded = dups = undecoded = 0
    for begin, end, _ in im.pdata:
        end = min(end, n_img)
        if begin >= end:
            continue
        code = bytes(im.data[begin:end])
        pos = 0
        while pos < len(code):
            last = pos
            for ins in md.disasm(code[pos:], begin + pos):
                rva = ins.address
                last = rva - begin + ins.size
                decoded += 1
                if sid_at[rva]:                 # inside two overlapping unwind ranges: one instance
                    dups += 1
                    continue
                sid_at[rva] = sid_of(ins) + 1
                swept.append(rva)
            if last >= len(code):
                break
            undecoded += 1                      # Capstone stopped: skip one byte and resynchronise
            pos = last + 1

    # Pass 2: what the lifter's recursive descent reaches.
    prog = Program(im)
    prog.discover()
    reached = bytearray(n_img)
    extra = array("I")                          # reached instructions the sweep did not decode
    extra_outside = extra_inside = 0            # ... outside every unwind range / inside one, off the sweep
    in_functions = Counter()                    # mnemonic -> number of functions containing it
    blockers = Counter()                        # sorted tuple of mnemonics not in BASELINE -> functions
    clean = 0
    for entry in sorted(prog.funcs):
        cfg = prog.build_cfg(prog.funcs[entry])
        sids = set()
        for rva, ins in cfg.insns.items():
            s1 = sid_at[rva]
            if not s1:
                s1 = sid_at[rva] = sid_of(ins) + 1
                extra.append(rva)
                if prog.container(rva) is None:
                    extra_outside += 1
                else:
                    extra_inside += 1
            reached[rva] = 1
            sids.add(s1 - 1)
        ms = {keys[sid][0] for sid in sids}
        in_functions.update(ms)
        names = tuple(sorted(ms - BASELINE))
        if names:
            blockers[names] += 1
        else:
            clean += 1
        del cfg                                 # CsInsn objects are heavy: never keep a function's CFG

    # Aggregate per (mnemonic, shape).
    n = len(keys)
    count, n_reached = [0] * n, [0] * n
    r_rvas, u_rvas = [array("I") for _ in range(n)], [array("I") for _ in range(n)]
    for lst in (swept, extra):
        for rva in lst:
            sid = sid_at[rva] - 1
            count[sid] += 1
            if reached[rva]:
                n_reached[sid] += 1
                r_rvas[sid].append(rva)
            else:
                u_rvas[sid].append(rva)
    mnemonics = {}
    for sid in sorted(range(n), key=lambda i: keys[i]):
        m, s = keys[sid]
        d = mnemonics.get(m)
        if d is None:
            d = mnemonics[m] = {"count": 0, "reached": 0, "functions": in_functions.get(m, 0), "shapes": {}}
        d["count"] += count[sid]
        d["reached"] += n_reached[sid]
        d["shapes"][s] = {"count": count[sid], "reached": n_reached[sid],
                          "rvas": _spread(r_rvas[sid] if n_reached[sid] else u_rvas[sid])}
    body = {
        "instructions": len(swept) + len(extra),
        "reached": sum(n_reached),
        "undecoded_bytes": undecoded,
        "functions": len(prog.funcs),
        "mnemonics": mnemonics,
        "blocker_sets": [[list(names), c] for names, c in sorted(blockers.items(), key=lambda kv: (-kv[1], kv[0]))],
        "functions_clean": clean,
    }
    stats = {"sweep": len(swept), "sweep_decoded": decoded, "overlap": dups,
             "extra_outside": extra_outside, "extra_inside": extra_inside}
    return body, stats


def build_job(job):
    """Pool worker: index one DLL and write its cache file.  Takes and returns small picklable
    values; the parent reads the result from the file.  Never raises."""
    group, module, path, rel, size, sha, rules, cache = job
    t0 = time.time()
    res = {"module": module, "seconds": 0.0}
    try:
        try:
            im = Image.from_pe(path)
        except Exception as e:
            res.update(status="skipped", message="Image.from_pe rejects it: %s: %s" % (type(e).__name__, e))
            return res
        body, stats = index_image(im)
        entry = {"module": module, "group": group, "path": rel, "size": size, "sha256": sha, "rules": rules,
                 "samples": SAMPLES}
        entry.update(body)
        _write_entry(entry, cache)
        res.update(status="built", instructions=entry["instructions"], reached=entry["reached"],
                   functions=entry["functions"], **stats)
    except Exception:
        res.update(status="failed", message=traceback.format_exc())
    res["seconds"] = time.time() - t0
    return res


# ---- the index ----------------------------------------------------------------------------------------

def load(repo=REPO, jobs=None, quiet=False):
    """{module: entry} for every DLL: read from inventory/insn_index/ where the file for this
    (module, size, sha256, rules) exists, built (in a Pool, the largest DLLs first) and written
    where it does not.  Each entry also has "abs_path".  A DLL that cannot be indexed is left out
    and reported on stderr; LAST_LOAD says which."""
    rules = rules_hash(repo)
    index, todo, seen = {}, [], {}
    LAST_LOAD.update(cached=0, built=0, skipped=[], failed=[])
    for group, module, path in find_dlls(repo):
        rel = os.path.relpath(path, repo).replace(os.sep, "/")
        if module in seen:
            LAST_LOAD["skipped"].append((module, "same module name as %s" % seen[module]))
            print("insn_index: skipping %s: the module name %r is already taken by %s" % (rel, module, seen[module]),
                  file=sys.stderr)
            continue
        seen[module] = rel
        size, sha, reason = _file_facts(path)
        if reason:
            LAST_LOAD["skipped"].append((module, reason))
            print("insn_index: skipping %s: %s" % (rel, reason), file=sys.stderr)
            continue
        cache = _cache_path(module, size, sha, rules)
        if os.path.exists(cache):
            try:
                index[module] = dict(_read_entry(cache), abs_path=path)
                LAST_LOAD["cached"] += 1
                continue
            except ValueError:
                print("insn_index: %s is not valid JSON, rebuilding" % cache, file=sys.stderr)
        todo.append((group, module, path, rel, size, sha, rules, cache))
    if todo:
        todo.sort(key=lambda j: -j[4])
        n_jobs = jobs if jobs else min(8, (os.cpu_count() or 2) - 2)
        n_jobs = max(1, min(n_jobs, len(todo)))
        if n_jobs == 1:
            results = map(build_job, todo)
        else:
            # One DLL per worker process, so that the memory of a big one goes back to the system.
            pool = Pool(n_jobs, maxtasksperchild=1)
            results = pool.imap_unordered(build_job, todo, chunksize=1)
        try:
            by_module = {j[1]: j for j in todo}
            for res in results:
                module = res["module"]
                if res["status"] == "built":
                    LAST_LOAD["built"] += 1
                    job = by_module[module]
                    index[module] = dict(_read_entry(job[7]), abs_path=job[2])
                    if not quiet:
                        print("%-24s %9d instructions %9d reached %6d functions %7.1f s   (sweep %d, + %d outside "
                              "the unwind ranges, + %d inside off its boundaries)" % (
                                  module, res["instructions"], res["reached"], res["functions"], res["seconds"],
                                  res["sweep"], res["extra_outside"], res["extra_inside"]), flush=True)
                elif res["status"] == "skipped":
                    LAST_LOAD["skipped"].append((module, res["message"]))
                    print("insn_index: skipping %s: %s" % (module, res["message"]), file=sys.stderr, flush=True)
                else:
                    LAST_LOAD["failed"].append((module, res["message"]))
                    print("insn_index: FAILED %s after %.1f s:\n%s" % (module, res["seconds"], res["message"]),
                          file=sys.stderr, flush=True)
        finally:
            if n_jobs > 1:
                pool.terminate()
                pool.join()
    if not quiet:
        print("insn_index: %d modules: %d cached, %d built (%d skipped, %d failed)" % (
            len(index), LAST_LOAD["cached"], LAST_LOAD["built"], len(LAST_LOAD["skipped"]), len(LAST_LOAD["failed"])),
            flush=True)
    return index


def game_count(index, mnemonic):
    """Instances of a mnemonic in the game's own DLLs (group "game": not the corpus, not the redistributable)."""
    return sum(e["mnemonics"].get(mnemonic, {}).get("count", 0) for e in index.values() if e["group"] == "game")


# ---- reports ------------------------------------------------------------------------------------------

def summary(index):
    """Lines: per group, how much is indexed and how much of it the registry translates today."""
    translated = set(translate.HANDLERS)
    lines = []
    for group in GROUP_ORDER:
        es = [e for e in index.values() if e["group"] == group]
        if not es:
            continue
        names = set()
        for e in es:
            names.update(e["mnemonics"])
        lines.append("%-7s %3d modules %10d instructions %10d reached %5d mnemonics (%d not translated) "
                     "%7d functions" % (group, len(es), sum(e["instructions"] for e in es),
                                        sum(e["reached"] for e in es), len(names), len(names - translated),
                                        sum(e["functions"] for e in es)))
    return lines


def worklist(index):
    """What is left to translate, as data: every mnemonic in the index that the registry does not
    translate, most frequent in the game first.

    {"mnemonics": [{"mnemonic", "game" (instances in the game), "reached", "blocked" (game functions that
    contain it), "sole" (game functions where it is the only blocker), "corpus" and "redist" (instances),
    "where", "shapes": [[shape, count], ...] (the 3 most frequent in the game)}],
     "totals": {"code_mnemonics" (untranslated, reached in the game), "code_instances" (their instances in the
    game), "functions_blocked" (game functions with a blocker left), "functions" (game functions)},
     "blocker_sets": [[names, functions], ...]}

    The blocker sets are those of the game's DLLs with the mnemonics the registry translates today taken
    out; a function whose set is then empty is no longer blocked.  "where" is "code" (reached in the game),
    "data-only" (decoded in the game but never reached: decoded data, see the module docstring),
    "corpus-only" or "redist-only" (not in the game at all; "corpus+redist" if in both)."""
    translated = set(translate.HANDLERS)
    count, n_reached = Counter(), Counter()
    shapes = {}
    other = {"corpus": Counter(), "redist": Counter()}
    merged = Counter()
    n_functions = 0
    names = set()
    for e in index.values():
        for m, d in e["mnemonics"].items():
            names.add(m)
            if e["group"] == "game":
                count[m] += d["count"]
                n_reached[m] += d["reached"]
                sh = shapes.setdefault(m, Counter())
                for s, sd in d["shapes"].items():
                    sh[s] += sd["count"]
            elif e["group"] in other:
                other[e["group"]][m] += d["count"]
        if e["group"] == "game":
            n_functions += e["functions"]
            for blockers, c in e["blocker_sets"]:
                left = tuple(b for b in blockers if b not in translated)
                if left:
                    merged[left] += c
    blocked, sole = Counter(), Counter()
    for blockers, c in merged.items():
        for b in blockers:
            blocked[b] += c
        if len(blockers) == 1:
            sole[blockers[0]] += c
    rows = []
    for m in sorted(names - translated, key=lambda m: (-count[m], -(other["corpus"][m] + other["redist"][m]), m)):
        if n_reached[m]:
            where = "code"
        elif count[m]:
            where = "data-only"
        elif other["corpus"][m] and other["redist"][m]:
            where = "corpus+redist"
        else:
            where = "corpus-only" if other["corpus"][m] else "redist-only"
        top = sorted(shapes.get(m, Counter()).items(), key=lambda kv: (-kv[1], kv[0]))[:3]
        rows.append({"mnemonic": m, "game": count[m], "reached": n_reached[m], "blocked": blocked[m],
                     "sole": sole[m], "corpus": other["corpus"][m], "redist": other["redist"][m],
                     "where": where, "shapes": [[s, c] for s, c in top]})
    code = [r for r in rows if r["where"] == "code"]
    return {"mnemonics": rows,
            "totals": {"code_mnemonics": len(code), "code_instances": sum(r["game"] for r in code),
                       "functions_blocked": sum(merged.values()), "functions": n_functions},
            "blocker_sets": [[list(b), c] for b, c in sorted(merged.items(), key=lambda kv: (-kv[1], kv[0]))]}


def format_worklist(data):
    lines = ["mnemonic | game | of which reached | functions blocked | only blocker | corpus | where | top shapes"]
    for r in data["mnemonics"]:
        lines.append(("%-14s | %8d | %8d | %6d | %6d | %6d | %-13s | %s" % (
            r["mnemonic"], r["game"], r["reached"], r["blocked"], r["sole"], r["corpus"], r["where"],
            "; ".join("%s (%d)" % (s, c) for s, c in r["shapes"]))).rstrip())
    t = data["totals"]
    lines += ["",
              "%d untranslated mnemonics occur in reached code in the game, %d instances" % (
                  t["code_mnemonics"], t["code_instances"]),
              "%d of %d game functions have a blocker left after subtracting what the registry translates" % (
                  t["functions_blocked"], t["functions"])]
    return lines


def mnemonic_report(index, names):
    """Lines: per module, the count and reached count of each mnemonic, and its shapes with sample addresses."""
    lines = []
    for name in names:
        total = reached = 0
        for e in sorted(index.values(), key=lambda e: (GROUP_ORDER.index(e["group"]), e["module"])):
            d = e["mnemonics"].get(name)
            if d is None:
                continue
            total += d["count"]
            reached += d["reached"]
            lines.append("%s (%s): %s: %d instances, %d reached, in %d functions" % (
                e["module"], e["group"], name, d["count"], d["reached"], d["functions"]))
            for s, sd in sorted(d["shapes"].items(), key=lambda kv: (-kv[1]["count"], kv[0])):
                lines.append("    %-34s %8d %8d reached   %s" % (
                    s, sd["count"], sd["reached"], " ".join("0x%x" % r for r in sd["rvas"])))
        if total:
            lines.append("%s: %d instances, %d reached, %d in the game%s" % (
                name, total, reached, game_count(index, name),
                "" if name in translate.HANDLERS else " (not translated)"))
        else:
            lines.append("%s: not in the index" % name)
        lines.append("")
    return lines


# ---- refused forms ------------------------------------------------------------------------------------

def forms_job(job):
    """Pool worker: ask the lifter whether it translates the sampled instructions of one DLL.
    job = (module, path, [(mnemonic, shape, [rva, ...])]).  Returns (module, [(mnemonic, shape, tried, refused,
    reasons)])."""
    module, path, items = job
    im = Image.from_pe(path)
    prog = Program(im)
    tr = Translator(im, im.name)
    out = []
    for m, s, rvas in items:
        refused, reasons = 0, set()
        for rva in rvas:
            ins = prog.decode(rva)
            if ins is None:
                refused += 1
                reasons.add("decode: cannot decode")
                continue
            try:
                rec, _, _, _ = lift_function(prog, tr, Func(rva, [(rva, rva + ins.size)], "insn"), "x", single=True)
            except Exception as e:                  # a LifterBug stops a lift; here it is a finding
                refused += 1
                reasons.add("%s: %s" % (type(e).__name__, str(e)[:160]))
                continue
            if rec["status"] == "skipped":
                refused += 1
                reasons.update("%s: %s" % (r["kind"], r["what"]) for r in rec["reasons"])
        out.append((m, s, len(rvas), refused, sorted(reasons)))
    return module, out


def forms(index, jobs=None):
    """Lines: the (family, mnemonic, shape) of translated mnemonics that the lifter refuses on at least one
    of the first two sample addresses per module, with the game count of the shape, the number refused
    of the number tried (and in which modules, as game/redist/corpus), and the distinct reasons."""
    translated = translate.HANDLERS
    game = Counter()
    tasks, groups = [], {}
    for e in index.values():
        items = []
        for m, d in e["mnemonics"].items():
            if m not in translated:
                continue
            for s, sd in d["shapes"].items():
                if e["group"] == "game":
                    game[(m, s)] += sd["count"]
                if sd["reached"] > 0:
                    items.append((m, s, sd["rvas"][:2]))
        if items:
            tasks.append((e["module"], e["abs_path"], items))
            groups[e["module"]] = e["group"]
    tasks.sort(key=lambda t: -len(t[1]))
    n_jobs = max(1, min(jobs if jobs else min(8, (os.cpu_count() or 2) - 2), len(tasks)))
    if n_jobs == 1:
        results = list(map(forms_job, tasks))
    else:
        with Pool(n_jobs, maxtasksperchild=1) as pool:
            results = list(pool.imap_unordered(forms_job, tasks, chunksize=1))
    tried, refused, reasons, where = Counter(), Counter(), {}, {}
    for module, res in results:
        for m, s, t, r, why in res:
            tried[(m, s)] += t
            refused[(m, s)] += r
            reasons.setdefault((m, s), set()).update(why)
            if r:
                where.setdefault((m, s), set()).add(module)
    bad = sorted(k for k in tried if refused[k])
    lines = []
    for m, s in sorted(bad, key=lambda k: (translate.FAMILY_OF[k[0]], k[0], k[1])):
        mods = sorted(where[(m, s)], key=lambda x: (GROUP_ORDER.index(groups[x]), x))
        lines.append("%-14s | %-12s | %-34s | game %7d | refused %d of %d in %s | %s" % (
            translate.FAMILY_OF[m], m, s, game[(m, s)], refused[(m, s)], tried[(m, s)],
            ", ".join(mods[:3]) + (" +%d" % (len(mods) - 3) if len(mods) > 3 else ""),
            " / ".join(sorted(reasons[(m, s)]))))
    in_game = sum(1 for k in bad if any(groups[x] == "game" for x in where[k]))
    lines.append("")
    lines.append("%d of %d (translated mnemonic, shape) pairs have a refused sample, %d of them in a game module" % (
        len(bad), len(tried), in_game))
    return lines


# ---- command line -------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--worklist", action="store_true", help="the untranslated mnemonics, most frequent first")
    ap.add_argument("--json", metavar="FILE", help="with --worklist: also write the data as JSON")
    ap.add_argument("--mnemonic", nargs="+", metavar="NAME", help="per module: counts, shapes, sample addresses")
    ap.add_argument("--forms", action="store_true", help="forms of translated mnemonics that the lifter refuses")
    ap.add_argument("--jobs", type=int, metavar="N", help="worker processes (default: min(8, cpus - 2))")
    args = ap.parse_args()
    if args.json and not args.worklist:
        ap.error("--json goes with --worklist")
    reports = args.worklist or args.mnemonic or args.forms
    if reports:
        with contextlib.redirect_stdout(sys.stderr):        # keep stdout for the report
            index = load(jobs=args.jobs)
    else:
        index = load(jobs=args.jobs)
    status = 1 if LAST_LOAD["failed"] else 0
    if not reports:
        print("\n".join(summary(index)))
    if args.worklist:
        data = worklist(index)
        print("\n".join(format_worklist(data)))
        if args.json:
            with open(args.json, "w") as f:
                json.dump(data, f, indent=1)
    if args.mnemonic:
        print("\n".join(mnemonic_report(index, args.mnemonic)))
    if args.forms:
        print("\n".join(forms(index, args.jobs)))
    return status


if __name__ == "__main__":
    sys.exit(main())
