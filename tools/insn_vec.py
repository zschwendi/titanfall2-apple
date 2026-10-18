#!/usr/bin/env python3
"""Test-vector lines for an instruction family, so that nobody assembles by hand.

    .venv/bin/python tools/insn_vec.py --enc "66 0F 70 /r ib" --imm 1b,4e
    .venv/bin/python tools/insn_vec.py --enc "66 0F 73 /3 ib" --imm 4 --at 0x1100
    .venv/bin/python tools/insn_vec.py 660f70c11b f30f7eca

The first form takes an encoding as the Intel manual writes it and prints one line per operand
form: register with register (low numbers, xmm8 and above, the same register twice) and
register with memory ([rbx], [rbx + 0x10], base + index*4 + displacement, RIP-relative into
the vector image's data at 0x8000).  The second form takes instruction bytes you wrote.

Every line is ready for VECTORS in tests/vectors/<family>.py: (slot, hex, text), the text being
exactly what the decoder prints, followed by a comment with the lifter's name for the
instruction, its operand shape, and the arguments of enc() that match these bytes.  Keep the
lines whose name is your mnemonic, drop the others (the same bytes with a memory operand, or
without REX.W, can be another instruction), and give every line you keep its own slot.

Encoding notation: optional mandatory prefix (66, F2 or F3), optional REX.W, the opcode bytes,
then /r (ModRM with two operands) or /0 to /7 (the digit goes in ModRM's reg field), then ib if
an 8-bit immediate follows.  --imm gives the immediate values in hex (default 1b): each operand
form gets one of them in turn, and the rest are shown once with the plain register form.
--forms rr,mem limits the operand forms (rr hi same mem disp sib rip; hi uses registers 8 and
above); with --forms, every listed form is printed with every immediate.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import capstone                                      # noqa: E402

from lifter import translate as T                    # noqa: E402

DATA = 0x8040            # inside the data of every vector image (tests/vectors/__init__.py), 16-byte aligned
FORMS = ("rr", "hi", "same", "mem", "disp", "sib", "rip")


def parse_enc(text):
    """(prefix bytes, REX.W, opcode bytes, /digit or None, has an imm8) of an encoding in Intel notation."""
    toks = text.replace("+", " ").split()
    prefix, opcode, digit, imm, w, modrm = [], [], None, False, False, False
    for t in toks:
        u = t.upper()
        if u in ("REX.W", "REX.W+"):
            w = True
        elif u == "/R":
            modrm = True
        elif len(u) == 2 and u[0] == "/" and u[1] in "01234567":
            modrm, digit = True, int(u[1])
        elif u == "IB":
            imm = True
        else:
            try:
                b = int(u, 16)
            except ValueError:
                sys.exit("insn_vec: cannot read %r in the encoding %r" % (t, text))
            if not 0 <= b <= 0xFF:
                sys.exit("insn_vec: %r is not a byte" % t)
            if not opcode and b in (0x66, 0xF2, 0xF3):
                prefix.append(b)
            else:
                opcode.append(b)
    if not opcode or not modrm:
        sys.exit("insn_vec: the encoding needs opcode bytes and /r or /digit: %r" % text)
    return prefix, w, opcode, digit, imm


def encode(prefix, w, opcode, reg, form, imm, slot):
    """Bytes of one operand form.  `reg` is the ModRM reg field (0..15); the rm side is the form's."""
    rex_r = reg >> 3
    reg &= 7
    rex_x = rex_b = 0
    tail = b""
    if form == "rr":
        rm = 2
        modrm = bytes([0xC0 | reg << 3 | rm])
    elif form == "hi":
        rex_b, rm = 1, 4                              # xmm12 / r12
        modrm = bytes([0xC0 | reg << 3 | rm])
    elif form == "same":
        rex_b = rex_r
        modrm = bytes([0xC0 | reg << 3 | reg])
    elif form == "mem":
        modrm = bytes([0x00 | reg << 3 | 3])          # [rbx]
    elif form == "disp":
        modrm = bytes([0x40 | reg << 3 | 3, 0x10])    # [rbx + 0x10]
    elif form == "sib":
        modrm = bytes([0x40 | reg << 3 | 4, 0x88, 0x20])      # [rax + rcx*4 + 0x20]
    elif form == "rip":
        modrm = bytes([0x00 | reg << 3 | 5])
        tail = b"rip"
    else:
        raise ValueError(form)
    rex = 0x40 | w << 3 | rex_r << 2 | rex_x << 1 | rex_b
    head = bytes(prefix) + (bytes([rex]) if rex != 0x40 else b"") + bytes(opcode) + modrm
    immb = bytes([imm]) if imm is not None else b""
    if tail:
        disp = DATA - (slot + len(head) + 4 + len(immb))
        head += (disp & 0xFFFFFFFF).to_bytes(4, "little")
    return head + immb


def describe(md, raw, slot):
    """(vector line, lifter name) for instruction bytes at `slot`, or (reason, None)."""
    ins = next(md.disasm(raw, slot, 1), None)
    if ins is None:
        return "does not decode", None
    if ins.size != len(raw):
        return "decodes as %d bytes (%s %s), not %d" % (ins.size, ins.mnemonic, ins.op_str, len(raw)), None
    text = (ins.mnemonic + " " + ins.op_str).strip()
    name = T.mnemonic_of(ins)
    try:
        e = T.Encoding(ins)
        key = "0x%s" % ("%X" % e.key).zfill(2 if e.key < 0x100 else 4 if e.key < 0x10000 else 6)
        how = "enc(%s%s, mand=%s)" % (key, "" if not _digit_matters(ins) else ", reg=%d" % e.reg,
                                      "0x%02X" % e.mandatory if e.mandatory else "0")
    except T.Unsupported as u:
        how = "refused by the core: %s" % u
    fam = T.FAMILY_OF.get(name)
    line = '    (0x%04x, "%s", "%s"),' % (slot, raw.hex(), text)
    note = "%s | %s | %s%s" % (name, T.shape(ins).split(" ", 1)[-1] if " " in T.shape(ins) else "-", how,
                               "" if fam is None else " | family %s" % fam)
    return "%-64s # %s" % (line, note), name


def _digit_matters(ins):
    """True for the opcode groups where ModRM's reg field selects the instruction (0F 71/72/73, 0F AE, ...)."""
    op = list(ins.opcode)
    return op[0] == 0x0F and op[1] in (0x71, 0x72, 0x73, 0xAE, 0xC7, 0x01, 0x00, 0x18, 0xBA)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("hex", nargs="*", help="instruction bytes as hex, one instruction each")
    ap.add_argument("--enc", help='an encoding in Intel notation, e.g. "66 0F 70 /r ib"')
    ap.add_argument("--imm", default="1b", help="immediate values in hex, comma-separated (with ib)")
    ap.add_argument("--at", default="0x1000", help="slot of the first line (default 0x1000); 16 bytes per line")
    ap.add_argument("--forms", help="operand forms to print, each with every immediate: " + " ".join(FORMS))
    ap.add_argument("--name", help="print only the lines that decode to this mnemonic")
    args = ap.parse_args()
    if bool(args.enc) == bool(args.hex):
        ap.error("give either --enc or instruction bytes")
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True
    slot = int(args.at, 16)
    raws = []
    if args.hex:
        for h in args.hex:
            try:
                raws.append(bytes.fromhex(h))
            except ValueError:
                sys.exit("insn_vec: %r is not hex" % h)
    else:
        prefix, w, opcode, digit, has_imm = parse_enc(args.enc)
        imms = [int(x, 16) for x in args.imm.split(",")] if has_imm else [None]
        forms = [f for f in (args.forms or ",".join(FORMS)).split(",") if f]
        if any(f not in FORMS for f in forms):
            ap.error("--forms takes: " + " ".join(FORMS))
        regs = {"rr": 1, "hi": 9, "same": 3, "mem": 1, "disp": 10, "sib": 2, "rip": 1}
        k = 0
        for f in forms:
            if digit is not None and f == "same":
                continue
            for imm in (imms if args.forms else [imms[k % len(imms)]]):
                raws.append((f, digit if digit is not None else regs[f], imm))
            k += 1
        if not args.forms:
            for imm in imms[1:]:                      # every further immediate once, register form
                raws.append(("rr", digit if digit is not None else 1, imm))
    shown, absent = 0, []
    for item in raws:
        if isinstance(item, tuple):
            f, reg, imm = item
            raw = encode(prefix, w, opcode, reg, f, imm, slot)
        else:
            raw = item
        line, name = describe(md, raw, slot)
        if name is None:
            if isinstance(item, tuple):
                absent.append(item[0])               # a form this instruction does not have
            else:
                print("    # %s: %s" % (raw.hex(), line))
            continue
        if args.name and name != args.name:
            continue
        print(line)
        shown += 1
        slot += 0x10
    if absent and not args.name:
        print("    # forms that do not decode (the instruction has none such): " + " ".join(absent))
    if not shown:
        print("    # nothing to show%s" % (" under the name %s" % args.name if args.name else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
