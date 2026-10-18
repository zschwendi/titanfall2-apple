#!/usr/bin/env python3
"""Header, section and unwind-table facts for ui(11).dll (supports questions 1 to 3).

Prints the COFF characteristics (including IMAGE_FILE_RELOCS_STRIPPED), DllCharacteristics
(ASLR / high-entropy / NX), the preferred image base, the entry point, the section table, the
unwind-table breakdown (primary entries vs chained continuation entries, and the number of
distinct function starts), and whether relocations and imports are absent.

    .venv/bin/python tools/scout/ui11_header.py "game/r2/paks/Win64/ui(11).dll"
"""

import os
import struct
import sys
from collections import Counter

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "inventory"))
from pe_inventory import PE, UNW_FLAG_CHAININFO  # noqa: E402

path = sys.argv[1]
pe = PE(path)
d = pe.data
coff = pe.e_lfanew + 4
(_, _, _, _, _, _, characteristics) = struct.unpack_from("<HHIIIHH", d, coff)

print("file size %d bytes, machine 0x%x, pe32plus %s" % (len(d), pe.machine, pe.pe32plus))
print("COFF characteristics 0x%04x: relocs-stripped=%s executable=%s dll=%s large-address=%s"
      % (characteristics, bool(characteristics & 0x1), bool(characteristics & 0x2),
         bool(characteristics & 0x2000), bool(characteristics & 0x20)))
print("DllCharacteristics 0x%04x: dynamic-base(ASLR)=%s high-entropy=%s nx=%s no-seh=%s guard-cf=%s"
      % (pe.dll_characteristics, bool(pe.dll_characteristics & 0x40), bool(pe.dll_characteristics & 0x20),
         bool(pe.dll_characteristics & 0x100), bool(pe.dll_characteristics & 0x400),
         bool(pe.dll_characteristics & 0x4000)))
print("preferred image base 0x%x, SizeOfImage 0x%x, entry RVA 0x%x" % (pe.image_base, pe.size_of_image, pe.entry_rva))
print("data directories (rva, size):")
names = ["export", "import", "resource", "exception", "security", "reloc", "debug", "arch", "globalptr",
         "tls", "load_config", "bound_import", "iat", "delay_import", "clr", "reserved"]
for i, (rva, size) in enumerate(pe.dirs):
    if rva or size:
        print("  %-13s rva 0x%06x size 0x%x" % (names[i], rva, size))

print("sections:")
for s in pe.sections:
    print("  %-8s va 0x%06x vsize 0x%06x rawsize 0x%06x flags 0x%08x exec=%s write=%s"
          % (s["name"], s["va"], s["vsize"], s["rsize"], s["flags"], pe.is_exec(s), bool(s["flags"] & 0x80000000)))

rva, size = pe.dirs[3]
off = pe.rva_to_off(rva)
entries = [struct.unpack_from("<III", d, off + 12 * i) for i in range(size // 12)]
primary, chained, ehandler, uhandler, codes = 0, 0, 0, 0, Counter()
starts = set()
for begin, end, unw in entries:
    uoff = pe.rva_to_off(unw & ~1)
    ver_flags, prolog, ncodes, frame = struct.unpack_from("<BBBB", d, uoff)
    flags = ver_flags >> 3
    if flags & UNW_FLAG_CHAININFO:
        chained += 1
    else:
        primary += 1
        starts.add(begin)
        ehandler += bool(flags & 0x1)
        uhandler += bool(flags & 0x2)
        codes[ncodes] += 1
print("exception directory: %d RUNTIME_FUNCTION entries, %d primary, %d chained (continuation), "
      "%d distinct primary begin RVAs" % (len(entries), primary, chained, len(starts)))
print("primary entries with exception handler %d, with unwind handler %d" % (ehandler, uhandler))
print("chained entries whose primary ranges cover the same function: see ui11_shape.py")
print("unwind-code count distribution (top 8 by count): %s"
      % ", ".join("%d codes:%d" % kv for kv in codes.most_common(8)))
print("relocation directory present: %s, size %d; imports directory present: %s; delay imports: %s"
      % (pe.dirs[5][0] != 0, pe.dirs[5][1], pe.dirs[1][0] != 0, pe.dirs[13][0] != 0))
print("TLS directory present: %s; load config present: %s"
      % (pe.dirs[9][0] != 0, pe.dirs[10][0] != 0))
print("rich header: %s; pdb path: %s" % (pe.rich_header() is not None, pe.pdb_path()))
