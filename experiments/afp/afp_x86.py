"""x86 side of the AFP experiment: run one real SSE instruction over many input records in Unicorn.

The instruction under test is always the register-register form (xmm0 <- xmm1, or eax/rax for the
integer forms).  Around it sits a tiny loop (load operands, execute, store xmm0 and rax) so one
emu_start covers a whole input set.  The loop is harness scaffolding; only the effect of the
instruction on xmm0 / rax / RFLAGS is read.

Record in : a[16] b[16]        xmm0 = a, xmm1 = b, rax = a.q[0]   (g2x forms: xmm0 = b)
Record out: xmm0[16] rax[8]    (flag forms: rax = RFLAGS & 0x8d5)
"""
import struct
from unicorn import Uc, UC_ARCH_X86, UC_MODE_64
from unicorn.x86_const import UC_X86_REG_RSI, UC_X86_REG_RDI, UC_X86_REG_RDX, UC_X86_REG_RCX, UC_X86_REG_RSP

CODE, DATA = 0x1000000, 0x4000000
MAX_RECORDS = 400_000
MEM_SIZE = (MAX_RECORDS * 56 + 0x20000 + 0xfff) & ~0xfff

PFX = {"ss": b"\xf3", "sd": b"\xf2", "ps": b"", "pd": b"\x66"}
# opcode byte for the arithmetic family
ARITH_OPC = {"add": 0x58, "mul": 0x59, "sub": 0x5c, "min": 0x5d, "div": 0x5e, "max": 0x5f,
             "sqrt": 0x51, "rsqrt": 0x52, "rcp": 0x53}
CMP_PRED = ["eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"]
REX_W = b"\x48"
MODRM_X0_X1 = b"\xc1"        # xmm0, xmm1
MODRM_X0_G0 = b"\xc0"        # xmm0, eax/rax
MODRM_G0_X1 = b"\xc1"        # eax/rax, xmm1


def encode(op):
    """Return (bytes, kind, expected_mnemonic_prefix) for an op name like addss, cmpltps, cvttss2si32."""
    for fam, opc in ARITH_OPC.items():
        for sfx in ("ss", "sd", "ps", "pd"):
            if op == fam + sfx:
                if fam in ("rsqrt", "rcp") and sfx in ("sd", "pd"):
                    continue
                return PFX[sfx] + b"\x0f" + bytes([opc]) + MODRM_X0_X1, "xx", op
    m = {"comiss": b"\x0f\x2f", "ucomiss": b"\x0f\x2e", "comisd": b"\x66\x0f\x2f", "ucomisd": b"\x66\x0f\x2e"}
    if op in m:
        return m[op] + MODRM_X0_X1, "flags", op
    if op.startswith("cmp") and op[-2:] in PFX and op[3:-2] in CMP_PRED:
        p = CMP_PRED.index(op[3:-2])
        return PFX[op[-2:]] + b"\x0f\xc2" + MODRM_X0_X1 + bytes([p]), "xx", op
    ci = {"cvtsi2ss": (b"\xf3", 0x2a), "cvtsi2sd": (b"\xf2", 0x2a)}
    for base, (pfx, opc) in ci.items():
        for w in (32, 64):
            if op == f"{base}{w}":
                return pfx + (REX_W if w == 64 else b"") + b"\x0f" + bytes([opc]) + MODRM_X0_G0, "g2x", base
    co = {"cvttss2si": (b"\xf3", 0x2c), "cvttsd2si": (b"\xf2", 0x2c), "cvtss2si": (b"\xf3", 0x2d), "cvtsd2si": (b"\xf2", 0x2d)}
    for base, (pfx, opc) in co.items():
        for w in (32, 64):
            if op == f"{base}{w}":
                return pfx + (REX_W if w == 64 else b"") + b"\x0f" + bytes([opc]) + MODRM_G0_X1, "xx", base
    cv = {"cvtss2sd": b"\xf3\x0f\x5a", "cvtsd2ss": b"\xf2\x0f\x5a", "cvtps2pd": b"\x0f\x5a", "cvtpd2ps": b"\x66\x0f\x5a",
          "cvtdq2ps": b"\x0f\x5b", "cvtps2dq": b"\x66\x0f\x5b", "cvttps2dq": b"\xf3\x0f\x5b",
          "cvtdq2pd": b"\xf3\x0f\xe6", "cvtpd2dq": b"\xf2\x0f\xe6", "cvttpd2dq": b"\x66\x0f\xe6"}
    if op in cv:
        return cv[op] + MODRM_X0_X1, "xx", op
    raise KeyError(op)


def loop_code(op_bytes, kind):
    body = b"\x0f\x10\x06"                 # movups xmm0,[rsi]
    body += b"\x0f\x10\x4e\x10"            # movups xmm1,[rsi+16]
    body += b"\x48\x8b\x06"                # mov rax,[rsi]
    if kind == "g2x":
        body += b"\x0f\x28\xc1"            # movaps xmm0,xmm1    (destination keeps b's upper lanes)
    body += op_bytes
    if kind == "flags":
        body += b"\x9c\x58\x48\x25\xd5\x08\x00\x00"    # pushfq; pop rax; and rax,0x8d5
    body += b"\x0f\x11\x07"                # movups [rdi],xmm0
    body += b"\x48\x89\x47\x10"            # mov [rdi+16],rax
    body += b"\x48\x83\xc6\x20"            # add rsi,32
    body += b"\x48\x83\xc7\x18"            # add rdi,24
    body += b"\x48\xff\xca"                # dec rdx
    jnz = b"\x75" + struct.pack("b", -(len(body) + 2))
    assert len(body) + 2 < 128
    return b"\x0f\xae\x11" + body + jnz + b"\xf4"    # ldmxcsr [rcx] first


class X86:
    def __init__(self):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_64)
        self.uc.mem_map(CODE, 0x1000)
        self.uc.mem_map(DATA, MEM_SIZE)

    def run(self, op, records, mxcsr):
        """records: bytes, N x 32.  Returns bytes, N x 24."""
        enc, kind, _ = encode(op)
        n = len(records) // 32
        assert n <= MAX_RECORDS
        code = loop_code(enc, kind)
        uc = self.uc
        uc.mem_write(CODE, code)
        uc.mem_write(DATA, struct.pack("<I", mxcsr))
        in_base = DATA + 0x100
        out_base = in_base + len(records)
        uc.mem_write(in_base, records)
        uc.reg_write(UC_X86_REG_RCX, DATA)
        uc.reg_write(UC_X86_REG_RSI, in_base)
        uc.reg_write(UC_X86_REG_RDI, out_base)
        uc.reg_write(UC_X86_REG_RDX, n)
        uc.reg_write(UC_X86_REG_RSP, DATA + MEM_SIZE - 0x100)    # pushfq in the flag forms
        uc.emu_start(CODE, CODE + len(code) - 1)
        return bytes(uc.mem_read(out_base, 24 * n))


def disasm_check():
    """Disassemble every encoding with Capstone and return {op: text}."""
    from capstone import Cs, CS_ARCH_X86, CS_MODE_64
    cs = Cs(CS_ARCH_X86, CS_MODE_64)
    out = {}
    for op in all_op_names():
        enc, kind, _ = encode(op)
        ins = list(cs.disasm(enc, 0))
        assert len(ins) == 1 and ins[0].size == len(enc), (op, ins)
        out[op] = f"{ins[0].mnemonic} {ins[0].op_str}"
    return out


def all_op_names():
    names = []
    for fam in ("add", "sub", "mul", "div", "sqrt", "min", "max"):
        names += [fam + s for s in ("ss", "sd", "ps", "pd")]
    names += ["rcpss", "rcpps", "rsqrtss", "rsqrtps"]
    names += ["comiss", "ucomiss", "comisd", "ucomisd"]
    names += [f"cmp{p}{s}" for p in CMP_PRED for s in ("ss", "sd", "ps", "pd")]
    names += [f"{b}{w}" for b in ("cvtsi2ss", "cvtsi2sd", "cvttss2si", "cvttsd2si", "cvtss2si", "cvtsd2si") for w in (32, 64)]
    names += ["cvtss2sd", "cvtsd2ss", "cvtps2pd", "cvtpd2ps", "cvtdq2ps", "cvtps2dq", "cvttps2dq",
              "cvtdq2pd", "cvtpd2dq", "cvttpd2dq"]
    return names


if __name__ == "__main__":
    for k, v in disasm_check().items():
        print(f"{k:16s} {v}")
