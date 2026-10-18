#!/usr/bin/env python3
"""Facts the runtime loader design (docs/RUNTIME.md) depends on, read from the game DLLs.

1. Per module: image size, entry point, section alignment, TLS directory (template size,
   zero fill, alignment, number of TLS callbacks).
2. DLL names that appear as strings (ASCII or UTF-16) but are not in the module's import
   table: candidates for LoadLibrary / GetModuleHandle by name at run time.
3. Function names that appear as strings but are neither imported nor exported by that
   module: candidates for GetProcAddress at run time.  The dictionary is every import name in inventory.json
   plus the names the Visual Studio 2012 static C runtime looks up by name.

    .venv/bin/python tools/scout/loader_facts.py game inventory/inventory.json

Single process, read only.  Prints Markdown to stdout.
"""

import json
import os
import re
import struct
import sys
from collections import defaultdict

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE  # noqa: E402

# Names the VS2012 static CRT (winapisupp.c) resolves with GetProcAddress on kernel32.
CRT_PROBES = """FlsAlloc FlsFree FlsGetValue FlsSetValue InitializeCriticalSectionEx CreateEventExW
CreateSemaphoreExW SetThreadStackGuarantee CreateThreadpoolTimer SetThreadpoolTimer
WaitForThreadpoolTimerCallbacks CloseThreadpoolTimer CreateThreadpoolWait SetThreadpoolWait
CloseThreadpoolWait FlushProcessWriteBuffers FreeLibraryWhenCallbackReturns
GetCurrentProcessorNumber GetLogicalProcessorInformation CreateSymbolicLinkW
SetDefaultDllDirectories EnumSystemLocalesEx CompareStringEx GetDateFormatEx GetLocaleInfoEx
GetTimeFormatEx GetUserDefaultLocaleName IsValidLocaleName LCMapStringEx GetCurrentPackageId
GetTickCount64 GetFileInformationByHandleExW SetFileInformationByHandleW CorExitProcess
MessageBoxW GetActiveWindow GetLastActivePopup GetUserObjectInformationW
GetProcessWindowStation""".split()

ASCII = re.compile(rb"[\x20-\x7e]{4,}")
UTF16 = re.compile(rb"(?:[\x20-\x7e]\x00){4,}")
DLLNAME = re.compile(r"^[A-Za-z0-9_.\-()+]{2,40}\.dll$", re.I)


def game_modules(root):
    out = []
    for sub in ("", "bin/x64_retail"):
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for n in sorted(os.listdir(d)):
            if n.lower().endswith(".dll"):
                out.append(os.path.join(d, n))
    return out


def tls_info(pe):
    rva, size = pe.dirs[9]
    off = pe.rva_to_off(rva) if rva else None
    if off is None:
        return None
    start, end, index, callbacks, zero_fill, chars = struct.unpack_from("<QQQQII", pe.data, off)
    ncb = 0
    if callbacks:
        coff = pe.rva_to_off(callbacks - pe.image_base)
        while coff is not None and coff + 8 <= len(pe.data):
            (v,) = struct.unpack_from("<Q", pe.data, coff)
            if v == 0:
                break
            ncb += 1
            coff += 8
    align_field = (chars >> 20) & 0xF
    return {"raw": end - start, "zero": zero_fill, "index_rva": index - pe.image_base,
            "callbacks": ncb, "align": (1 << (align_field - 1)) if align_field else 0}


def strings(data):
    seen = set()
    for m in ASCII.finditer(data):
        seen.add(m.group().decode("latin-1"))
    for m in UTF16.finditer(data):
        seen.add(m.group().decode("utf-16-le"))
    return seen


def main():
    root, inv_path = sys.argv[1], sys.argv[2]
    inv = json.load(open(inv_path))
    dictionary = set(CRT_PROBES)
    for f in inv["files"]:
        for kind in ("imports", "delay_imports"):
            for fns in f[kind].values():
                dictionary.update(n for n in fns if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]{5,}", n))

    mods = game_modules(root)
    names = {os.path.basename(p).lower() for p in mods}

    print("## Images\n")
    print("| Module | SizeOfImage | Entry RVA | Section align | TLS raw | TLS zero fill | "
          "TLS align | TLS callbacks |")
    print("|---|---|---|---|---|---|---|---|")
    total = 0
    dll_refs = defaultdict(set)
    proc_refs = defaultdict(set)
    for p in mods:
        pe = PE(p)
        base = os.path.basename(p)
        total += pe.size_of_image
        opt = pe.e_lfanew + 24
        (sec_align,) = struct.unpack_from("<I", pe.data, opt + 32)
        t = tls_info(pe)
        print("| %s | 0x%x | 0x%x | 0x%x | %s | %s | %s | %s |" % (
            base, pe.size_of_image, pe.entry_rva, sec_align,
            t["raw"] if t else "-", t["zero"] if t else "-",
            t["align"] if t else "-", t["callbacks"] if t else "-"))
        imported_dlls = set(pe.imports()) | set(pe.delay_imports())
        imported_fns = set(pe.exports()["names"])        # its own export names are not look-ups
        for table in (pe.imports(), pe.delay_imports()):
            for fns in table.values():
                imported_fns.update(fns)
        for s in strings(pe.data):
            s = s.strip()
            tail = s.replace("/", "\\").rsplit("\\", 1)[-1]
            if DLLNAME.match(tail) and tail.lower() not in imported_dlls and tail.lower() != base.lower():
                dll_refs[tail.lower()].add(base)
            if s in dictionary and s not in imported_fns:
                proc_refs[s].add(base)
    print("\nTotal SizeOfImage: %.1f MB in %d modules\n" % (total / 1e6, len(mods)))

    print("## DLL names present as strings but not imported by that module\n")
    print("| Name | In the game's own set | Modules holding the string |")
    print("|---|---|---|")
    for n in sorted(dll_refs):
        print("| %s | %s | %s |" % (n, "yes" if n in names else "", ", ".join(sorted(dll_refs[n]))))

    print("\n## Function names present as strings, neither imported nor exported by that module\n")
    print("| Name | CRT probe | Modules | Which |")
    print("|---|---|---|---|")
    for n in sorted(proc_refs, key=lambda k: (-len(proc_refs[k]), k)):
        ms = sorted(proc_refs[n])
        which = ", ".join(ms) if len(ms) <= 4 else ""
        print("| %s | %s | %d | %s |" % (n, "yes" if n in CRT_PROBES else "", len(ms), which))


if __name__ == "__main__":
    main()
