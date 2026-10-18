#!/usr/bin/env python3
"""Interface factories, registered interfaces and exported singletons of tier0.dll and vstdlib.dll
(finding 21, section 6).

Verified from the binaries:
  * vstdlib exports `CreateInterface`; its list head and node layout are read from the code.
  * The registrations are the static constructors that call the list-insert routine; each is read for its
    interface name string, creator function and the object the creator returns.
  * tier0 exports no `CreateInterface` and registers nothing.
  * RTTI gives the class behind each object and the size of its vtable; each vtable slot is measured
    (instruction count, lifter status, direct-call closure) so the slots simple enough to call can be listed.

Slot names are given only where a call site or the code shape and the public Source SDK header order agree;
each name carries its basis.  Nothing else is guessed.

    .venv/bin/python tools/scout/gate2_interfaces.py
"""

import json

import gate2_lib as L
import xref_lib

# slot names: (module, class) -> {slot: (name, basis)}
SLOTS = {
    ("tier0", "CCommandLine"): {
        0: ("CreateCmdLine(int argc, char** argv)", "inferred: joins argv, then calls slot 1 through the vtable"),
        1: ("CreateCmdLine(const char* commandline)", "verified: tokenises quotes and '@' in the string"),
        3: ("GetCmdLine()", "verified: returns the pointer at this+8"),
        4: ("CheckParm(const char* parm, const char** value)", "verified: calls FindParm (slot 11), returns the parm and its value"),
        7: ("ParmValue(const char*, float)", "inferred from slot order; skipped by the lifter"),
        8: ("ParmValue(const char*, int default)", "verified: structure (FindParm, the next parm, a leading + or - means no value); the conversion call at 0x26868 is taken to be atoi [I]"),
        9: ("ParmValue(const char*, const char* default)", "verified: the value string after the parm, else the default"),
        10: ("ParmCount()", "verified: returns the int at this+0x28"),
        11: ("FindParm(const char*)", "verified: index of the first parm that compares equal, 0 if none"),
        12: ("GetParm(int)", "verified: parm string by index, empty string out of range"),
        15: ("scalar deleting destructor", "verified: resets the vptr, frees"),
    },
    ("tier0", "CStdMemAlloc"): {
        0: ("Alloc(size, file, line)", "inferred: twin of slot 1; MSVC lists overloads in reverse declaration order"),
        1: ("Alloc(size)", "verified: operator new calls [vtable+8] with one size argument"),
        2: ("Realloc(ptr, size, file, line)", "inferred: twin of slot 3"),
        3: ("Realloc(ptr, size)", "verified: MemAllocScratch calls [vtable+0x18] with a pointer and a size"),
        4: ("Free(ptr, file, line)", "inferred: twin of slot 5"),
        5: ("Free(ptr)", "verified: operator delete tail-jumps to [vtable+0x28]"),
        6: ("Expand_NoLongerSupported", "inferred: 2-instruction stub"),
        7: ("Expand_NoLongerSupported (file, line)", "inferred: 2-instruction stub"),
        8: ("GetSize(ptr)", "inferred: 72 instructions, no calls, follows the two stubs in the public IMemAlloc order"),
        9: ("PushAllocDbgInfo", "inferred: 1-instruction no-op"),
        10: ("PopAllocDbgInfo", "inferred: 1-instruction no-op"),
    },
    ("vstdlib", "CKeyValuesSystem"): {
        0: ("RegisterSizeofKeyValues(int)", "inferred: 4 instructions, public IKeyValuesSystem order"),
        1: ("AllocKeyValuesMemory(int)", "inferred: allocator call, public order"),
        2: ("FreeKeyValuesMemory(void*)", "inferred: allocator call, public order"),
        3: ("GetSymbolForString(const char*, bool create)", "inferred: 131 instructions, takes a lock, grows a pool with VirtualAlloc"),
        4: ("GetStringForSymbol(symbol)", "inferred: 7 instructions, table index"),
        5: ("AddKeyValuesToMemoryLeakList", "inferred: 1-instruction no-op"),
        6: ("RemoveKeyValuesFromMemoryLeakList", "inferred: 1-instruction no-op"),
    },
    ("vstdlib", "CUniformRandomStream"): {
        0: ("SetSeed(int)", "verified: slot RVA equals the export ?SetSeed@CUniformRandomStream; the exported RandomSeed jumps through [vtable]"),
        1: ("GetSeed()", "verified: slot RVA equals the export ?GetSeed@CUniformRandomStream; GetCurrentRandomSeed jumps through [vtable+8]"),
        2: ("RandomFloat(float min, float max)", "verified: slot RVA equals the export ?RandomFloat@CUniformRandomStream; RandomFloat jumps through [vtable+0x10]"),
        3: ("RandomInt(int min, int max)", "verified: slot RVA equals the export ?RandomInt@CUniformRandomStream; RandomInt jumps through [vtable+0x18]"),
        4: ("RandomFloatExp(float min, float max, float exp)", "verified: slot RVA equals the export ?RandomFloatExp@CUniformRandomStream"),
    },
}

CLASS_VTABLES = [("tier0", "CCommandLine", 0x521a8), ("tier0", "CStdMemAlloc", 0x52fb8),
                 ("vstdlib", "CKeyValuesSystem", 0x5c0d8), ("vstdlib", "CCvar", 0x5b558),
                 ("vstdlib", "CEventSystem", 0x5b918), ("vstdlib", "CProcessUtils", 0x5c538),
                 ("vstdlib", "CUniformRandomStream", 0x5b020), ("vstdlib", "CThreadPool", 0x5bcc8)]

BAD_IMPORTS = {"CreateThread", "CreateFileW", "CreateFileA", "CreateProcessA", "CreatePipe", "ResumeThread"}


def slot_table(M, vt, cls):
    out = []
    n = 0
    while n < 64:
        v = M.m.u64(vt + 8 * n)
        if v is None or not M.m.in_exec(v - M.m.base):
            break
        r = v - M.m.base
        rep = M.rep(r)
        c = M.closure(r)
        imps = sorted({n_ for d, n_ in c["imports"] if d != "tier0.dll"})
        name = SLOTS.get((M.name, cls), {}).get(n)
        simple = (M.status(r) != "skipped" and not c["skipped"] and c["insns"] <= 400 and not (set(imps) & BAD_IMPORTS))
        out.append({"slot": n, "rva": r, "insns": rep["insns"] if rep else None, "lifter": M.status(r),
                    "closure_insns": c["insns"], "closure_skipped": len(c["skipped"]), "indirect": c["indirect"],
                    "imports": imps, "name": name[0] if name else None, "basis": name[1] if name else None,
                    "simple": simple})
        n += 1
    return out


def registrations(M):
    """Static constructors that tail-jump into the list-insert routine; read their three `lea`s."""
    head = None
    for e in M.exports:
        if e["name"] == "CreateInterface":
            ev = [x for x in M.disasm(e["rva"], 3)]
            # the first instruction loads the list head
            for addr, size, mn, tgt, fm in M.t["rip"]:
                if addr == e["rva"]:
                    head = tgt
    if head is None:
        return None, []
    writers = sorted({M.m.unit_of(a) for a, s, mn, tg, fm in M.t["rip"] if tg == head and fm and mn == "mov"})
    insert_fns = [w for w in writers if not any(e["rva"] == w for e in M.exports)]
    out = []
    for ins in insert_fns:
        for site, mn, cf in M.ctx.callers(ins):
            # constructor body: lea r8,[name]; lea rdx,[creator]; lea rcx,[node]; jmp insert
            ctor = M.m.unit_of(site)
            leas = [(a, tg) for a, s, m_, tg, fm in M.t["rip"] if m_ == "lea" and M.m.unit_of(a) == ctor]
            name = creator = node = None
            for a, tg in sorted(leas):
                if M.m.section_name(tg) == ".rdata" and M.m.cstring(tg):
                    name = M.m.cstring(tg)
                elif M.m.in_exec(tg):
                    creator = tg
                elif M.m.section_name(tg) == ".data":
                    node = tg
            ret = None
            if creator:
                for a, s, m_, tg, fm in M.t["rip"]:
                    if m_ == "lea" and a == creator:
                        ret = tg
            out.append({"ctor": ctor, "name": name, "creator": creator, "node": node, "object": ret})
    return head, out


def class_of_object(M, obj_rva):
    """RTTI class of the object at obj_rva.  Static objects of these DLLs are laid out in .data with the
    vptr already in the file (a relocated pointer), so the first qword names the vtable.  Returns a list of
    (class, vtable_rva); empty when the object is constructed at run time."""
    vts = xref_lib.vtables(M.m)
    v = M.m.u64(obj_rva)
    if v:
        r = v - M.m.base
        if r in vts:
            return [(xref_lib.demangle_class(vts[r]), r)]
    return []


def build():
    out = {}
    for mn in L.MODULES:
        M = L.load(mn)
        o = {"create_interface_exports": [e["name"] for e in M.exports if e["name"] == "CreateInterface"]}
        head, regs = registrations(M)
        o["list_head_rva"] = head
        o["registrations"] = regs
        for r in regs:
            if r["object"] is not None:
                r["class"] = class_of_object(M, r["object"])
        out[mn] = o
    classes = []
    for mn, cls, vt in CLASS_VTABLES:
        M = L.load(mn)
        classes.append({"module": mn, "class": cls, "vtable_rva": vt, "slots": slot_table(M, vt, cls)})
    out["classes"] = classes
    return out


def main():
    d = build()
    for mn in L.MODULES:
        o = d[mn]
        print(mn, "CreateInterface exports:", o["create_interface_exports"], "list head:", o["list_head_rva"] and hex(o["list_head_rva"]))
        for r in o["registrations"]:
            print("   %-18s ctor %#x creator %#x node %#x object %#x class %s" % (
                r["name"], r["ctor"], r["creator"] or 0, r["node"] or 0, r["object"] or 0, r.get("class")))
    for c in d["classes"]:
        simple = [s["slot"] for s in c["slots"] if s["simple"]]
        print("%s %s vtable %#x: %d slots, %d simple: %s" % (c["module"], c["class"], c["vtable_rva"], len(c["slots"]), len(simple), simple))
    json.dump(d, open("/dev/null", "w"), default=str)


if __name__ == "__main__":
    main()
