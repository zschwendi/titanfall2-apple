#!/usr/bin/env python3
"""make_dxbc.py -- hand-assemble the few Direct3D shader-model-5.0 (DXBC) programs the DXMT test programs need.

There is no HLSL compiler on this Mac and no compiler-produced bytecode was taken from anywhere, so the
programs are written token by token, from the public layout of the tokenized program format (DXMT's own
copy of Microsoft's header documents it: third_party/dxmt/libs/DXBCParser/d3d12tokenizedprogramformat.hpp).
The result is a standard DXBC container (chunks ISGN, OSGN, SHEX; the container checksum is computed) that
DXMT's SM50Initialize reads exactly as it reads compiler output.  It is NOT fxc output: no RDEF or STAT chunk,
no compiler version string.

Programs (what the HLSL would be):

  vs_5_0   float4 main(float4 pos : POSITION, float4 col : COLOR, out float4 ocol : COLOR) : SV_Position
           { ocol = col; return pos; }
  ps_5_0   float4 main(float4 pos : SV_Position, float4 col : COLOR) : SV_Target { return col; }
  ps_5_0   float4 main(float4 pos : SV_Position, float4 col : COLOR) : SV_Target { return col + float4(0, 0, 0, SALT); }
           (SALT is a placeholder DWORD, 0xDEADBEEF, that offscreen_triangle --salt patches at run time so that every
           run can make a pixel shader Metal's pipeline cache has never seen; used only for timing)
  cs_5_0   RWByteAddressBuffer u0;  [numthreads(1,1,1)] void main() { u0.Store(0, 0x1234abcd); }
  cs_5_0   [numthreads(1,1,1)] void main() { }

Two more, for asking DXMT's converter what it does with a shader it cannot take (docs/handoff/pic-fork.md):

  ps_5_0   the first pixel shader with one `abort` instruction (opcode 207, no operands) in front of the `mov`.
           `abort` is a real Shader Model 5 opcode that fxc never writes into a shipped shader and that DXMT's
           converter has no case for: "an instruction it does not know".           -> kPsUnknownDxbc
  (none)   a container with the two signature chunks and no SHEX chunk: "a container it refuses".  -> kNoCodeDxbc

Usage:  .venv/bin/python tools/dxmt/make_dxbc.py tools/dxmt/dxbc_blobs.h        (rewrites the header)
        .venv/bin/python tools/dxmt/make_dxbc.py --check tools/dxmt/dxbc_blobs.h  (exit 1 if it differs)
        .venv/bin/python tools/dxmt/make_dxbc.py --dump <dir>                     (one <name>.dxbc file per program)
"""
import struct
import sys

# ---- token constants (values printed from DXMT's header with a small C++ program, see finding) -------------
OP_ADD, OP_MOV, OP_RET = 0, 54, 62
OP_DCL_CONSTANT_BUFFER = 89
OP_DCL_INPUT, OP_DCL_INPUT_PS = 95, 98
OP_DCL_OUTPUT, OP_DCL_OUTPUT_SIV = 101, 103
OP_DCL_GLOBAL_FLAGS = 106
OP_DCL_THREAD_GROUP, OP_DCL_UAV_RAW, OP_STORE_RAW = 155, 157, 166
OP_ABORT = 207  # D3D11_SB_OPCODE_ABORT; same header, counted by tools of unit pic-fork

PS, VS, CS = 0, 1, 5  # program types
T_TEMP, T_INPUT, T_OUTPUT, T_IMM32, T_UAV = 0, 1, 2, 4, 30  # operand types
NAME_POSITION, NAME_TARGET = 1, 64  # D3D_NAME_*
INTERP_LINEAR = 2


def version(prog_type, major=5, minor=0):
    return (prog_type << 16) | (major << 4) | minor


def op(opcode, length, extra=0):
    return opcode | (extra << 11) | (length << 24)


def operand_4c_mask(otype, mask=0xF, dims=1):
    # 4 components (2), mask selection mode (0), mask at bit 4, type at bit 12, index dimension at bit 20
    return 2 | (mask << 4) | (otype << 12) | (dims << 20)


def operand_4c_swizzle(otype, swz=0xE4, dims=1):
    return 2 | (1 << 2) | (swz << 4) | (otype << 12) | (dims << 20)


def operand_0c(otype, dims=1):  # declaration operands of resources (t#, u#, s#): no component selection
    return (otype << 12) | (dims << 20)


def program(prog_type, body):
    toks = [version(prog_type), 0] + body
    toks[1] = len(toks)
    return struct.pack("<%dI" % len(toks), *toks)


# ---- shaders ----------------------------------------------------------------------------------------------
GLOBAL_FLAGS_REFACTORING = op(OP_DCL_GLOBAL_FLAGS, 1, 1)


def vs_passthrough():
    return program(VS, [
        GLOBAL_FLAGS_REFACTORING,
        op(OP_DCL_INPUT, 3), operand_4c_mask(T_INPUT), 0,             # dcl_input v0.xyzw
        op(OP_DCL_INPUT, 3), operand_4c_mask(T_INPUT), 1,             # dcl_input v1.xyzw
        op(OP_DCL_OUTPUT_SIV, 4), operand_4c_mask(T_OUTPUT), 0, NAME_POSITION,  # dcl_output_siv o0.xyzw, position
        op(OP_DCL_OUTPUT, 3), operand_4c_mask(T_OUTPUT), 1,           # dcl_output o1.xyzw
        op(OP_MOV, 5), operand_4c_mask(T_OUTPUT), 0, operand_4c_swizzle(T_INPUT), 0,  # mov o0.xyzw, v0.xyzw
        op(OP_MOV, 5), operand_4c_mask(T_OUTPUT), 1, operand_4c_swizzle(T_INPUT), 1,  # mov o1.xyzw, v1.xyzw
        op(OP_RET, 1),
    ])


def ps_color():
    return program(PS, [
        GLOBAL_FLAGS_REFACTORING,
        op(OP_DCL_INPUT_PS, 3, INTERP_LINEAR), operand_4c_mask(T_INPUT), 1,  # dcl_input_ps linear v1.xyzw
        op(OP_DCL_OUTPUT, 3), operand_4c_mask(T_OUTPUT), 0,                   # dcl_output o0.xyzw
        op(OP_MOV, 5), operand_4c_mask(T_OUTPUT), 0, operand_4c_swizzle(T_INPUT), 1,  # mov o0.xyzw, v1.xyzw
        op(OP_RET, 1),
    ])


def ps_color_unknown():
    return program(PS, [
        GLOBAL_FLAGS_REFACTORING,
        op(OP_DCL_INPUT_PS, 3, INTERP_LINEAR), operand_4c_mask(T_INPUT), 1,
        op(OP_DCL_OUTPUT, 3), operand_4c_mask(T_OUTPUT), 0,
        op(OP_ABORT, 1),                                                      # abort   (no operands)
        op(OP_MOV, 5), operand_4c_mask(T_OUTPUT), 0, operand_4c_swizzle(T_INPUT), 1,
        op(OP_RET, 1),
    ])


SALT_PLACEHOLDER = 0xDEADBEEF


def ps_color_salted():
    imm4 = 2 | (T_IMM32 << 12)  # four-component immediate operand token, followed by four DWORDs
    return program(PS, [
        GLOBAL_FLAGS_REFACTORING,
        op(OP_DCL_INPUT_PS, 3, INTERP_LINEAR), operand_4c_mask(T_INPUT), 1,
        op(OP_DCL_OUTPUT, 3), operand_4c_mask(T_OUTPUT), 0,
        op(OP_ADD, 10), operand_4c_mask(T_OUTPUT), 0, operand_4c_swizzle(T_INPUT), 1,  # add o0.xyzw, v1.xyzw,
        imm4, 0, 0, 0, SALT_PLACEHOLDER,                                              #     l(0, 0, 0, SALT)
        op(OP_RET, 1),
    ])


def cs_store():
    imm1 = 1 | (T_IMM32 << 12)  # one-component immediate operand token, followed by one DWORD
    return program(CS, [
        GLOBAL_FLAGS_REFACTORING,
        op(OP_DCL_UAV_RAW, 3), operand_0c(T_UAV), 0,                  # dcl_uav_raw u0
        op(OP_DCL_THREAD_GROUP, 4), 1, 1, 1,                          # dcl_thread_group 1, 1, 1
        op(OP_STORE_RAW, 7), operand_4c_mask(T_UAV, 0x1), 0,          # store_raw u0.x,
        imm1, 0,                                                      #           l(0),
        imm1, 0x1234ABCD,                                             #           l(0x1234abcd)
        op(OP_RET, 1),
    ])


def cs_empty():
    return program(CS, [
        op(OP_DCL_THREAD_GROUP, 4), 1, 1, 1,
        op(OP_RET, 1),
    ])


# ---- signatures -------------------------------------------------------------------------------------------
FLOAT = 3  # D3D_REGISTER_COMPONENT_FLOAT32


def signature(elements):
    """elements: (semantic, index, sysval, comp_type, register, mask, rwmask)."""
    n = len(elements)
    head = struct.pack("<II", n, 8)
    names = b""
    name_off = {}
    base = 8 + 24 * n
    for e in elements:
        if e[0] not in name_off:
            name_off[e[0]] = base + len(names)
            names += e[0].encode() + b"\0"
    body = b""
    for sem, idx, sysval, ctype, reg, mask, rw in elements:
        body += struct.pack("<IIIII", name_off[sem], idx, sysval, ctype, reg) + struct.pack("<BBBB", mask, rw, 0, 0)
    data = head + body + names
    return data + b"\0" * (-len(data) % 4)


# ---- container --------------------------------------------------------------------------------------------
def md5_transform(state, block):
    """The MD5 compression function on one 64-byte block (state: four 32-bit ints)."""
    import math
    K = [int(abs(math.sin(i + 1)) * 2**32) & 0xFFFFFFFF for i in range(64)]
    S = [7, 12, 17, 22] * 4 + [5, 9, 14, 20] * 4 + [4, 11, 16, 23] * 4 + [6, 10, 15, 21] * 4
    M = struct.unpack("<16I", block)
    a, b, c, d = state
    for i in range(64):
        if i < 16:
            f, g = (b & c) | (~b & d), i
        elif i < 32:
            f, g = (d & b) | (~d & c), (5 * i + 1) % 16
        elif i < 48:
            f, g = b ^ c ^ d, (3 * i + 5) % 16
        else:
            f, g = c ^ (b | (~d & 0xFFFFFFFF)), (7 * i) % 16
        f = (f + a + K[i] + M[g]) & 0xFFFFFFFF
        a, d, c = d, c, b
        b = (b + (((f << S[i]) | (f >> (32 - S[i]))) & 0xFFFFFFFF)) & 0xFFFFFFFF
    return [(x + y) & 0xFFFFFFFF for x, y in zip(state, (a, b, c, d))]


def dxbc_checksum(container):
    """The container checksum the Direct3D runtime and vkd3d compute: MD5 with a non-standard final block, over
    everything after the first 20 bytes (magic and checksum).  Written from memory of vkd3d-shader's checksum.c.
    DXMT does not check it.  It reproduces the stored checksum of the two fxc-produced containers (604 and 596 bytes,
    vs_4_0 and ps_4_0, from FNA3D.dll in CrossOver's wine-mono) that were read for this comparison only; their bytes
    are not used anywhere.  Both have a tail of fewer than 56 bytes, so the "56 or more" branch is not covered by
    that comparison (kPsSaltedDxbc is the only blob here that takes it, and its checksum is stale after patching)."""
    data = container[20:]
    size = len(data)
    state = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    full = size & ~0x3F
    for off in range(0, full, 64):
        state = md5_transform(state, data[off:off + 64])
    left = data[full:]
    num_bits, num_bits_2 = (size * 8) & 0xFFFFFFFF, ((size * 2) | 1) & 0xFFFFFFFF
    if len(left) >= 56:
        blk = left + b"\x80" + b"\0" * (63 - len(left))
        state = md5_transform(state, blk)
        blk = struct.pack("<I", num_bits) + b"\0" * 56 + struct.pack("<I", num_bits_2)
    else:
        blk = struct.pack("<I", num_bits) + left + b"\x80"
        blk += b"\0" * (60 - len(blk)) + struct.pack("<I", num_bits_2)
    state = md5_transform(state, blk)
    return struct.pack("<4I", *state)


def container(chunks):
    n = len(chunks)
    header_len = 32 + 4 * n
    offsets, body, off = [], b"", header_len
    for tag, data in chunks:
        offsets.append(off)
        c = tag + struct.pack("<I", len(data)) + data
        body += c
        off += len(c)
    total = header_len + len(body)
    out = b"DXBC" + b"\0" * 16 + struct.pack("<III", 1, total, n) + struct.pack("<%dI" % n, *offsets) + body
    return out[:4] + dxbc_checksum(out) + out[20:]


def make_all():
    f4 = lambda sem, idx, sysval, reg, mask, rw: (sem, idx, sysval, FLOAT, reg, mask, rw)
    blobs = {}
    blobs["kVsDxbc"] = container([
        (b"ISGN", signature([f4("POSITION", 0, 0, 0, 0xF, 0xF), f4("COLOR", 0, 0, 1, 0xF, 0xF)])),
        (b"OSGN", signature([f4("SV_Position", 0, NAME_POSITION, 0, 0xF, 0), f4("COLOR", 0, 0, 1, 0xF, 0)])),
        (b"SHEX", vs_passthrough()),
    ])
    blobs["kPsDxbc"] = container([
        (b"ISGN", signature([f4("SV_Position", 0, NAME_POSITION, 0, 0xF, 0), f4("COLOR", 0, 0, 1, 0xF, 0xF)])),
        (b"OSGN", signature([f4("SV_Target", 0, NAME_TARGET, 0, 0xF, 0)])),
        (b"SHEX", ps_color()),
    ])
    blobs["kPsSaltedDxbc"] = container([
        (b"ISGN", signature([f4("SV_Position", 0, NAME_POSITION, 0, 0xF, 0), f4("COLOR", 0, 0, 1, 0xF, 0xF)])),
        (b"OSGN", signature([f4("SV_Target", 0, NAME_TARGET, 0, 0xF, 0)])),
        (b"SHEX", ps_color_salted()),
    ])
    blobs["kCsStoreDxbc"] = container([(b"ISGN", signature([])), (b"OSGN", signature([])), (b"SHEX", cs_store())])
    blobs["kCsEmptyDxbc"] = container([(b"ISGN", signature([])), (b"OSGN", signature([])), (b"SHEX", cs_empty())])
    ps_in = signature([f4("SV_Position", 0, NAME_POSITION, 0, 0xF, 0), f4("COLOR", 0, 0, 1, 0xF, 0xF)])
    ps_out = signature([f4("SV_Target", 0, NAME_TARGET, 0, 0xF, 0)])
    blobs["kPsUnknownDxbc"] = container([(b"ISGN", ps_in), (b"OSGN", ps_out), (b"SHEX", ps_color_unknown())])
    blobs["kNoCodeDxbc"] = container([(b"ISGN", ps_in), (b"OSGN", ps_out)])
    return blobs


def render(blobs):
    out = ["// Generated by tools/dxmt/make_dxbc.py -- do not edit.  Hand-assembled DXBC (SM 5.0), not compiler output.\n",
           "#pragma once\n"]
    for name, b in blobs.items():
        out.append("static const unsigned char %s[%d] = {\n" % (name, len(b)))
        for i in range(0, len(b), 16):
            out.append("  " + ", ".join("0x%02x" % x for x in b[i:i + 16]) + ",\n")
        out.append("};\n")
    return "".join(out)


if __name__ == "__main__":
    args = sys.argv[1:]
    if args and args[0] == "--dump" and len(args) == 2:
        import os
        os.makedirs(args[1], exist_ok=True)
        for n, b in make_all().items():
            open(os.path.join(args[1], n[1:] + ".dxbc"), "wb").write(b)
        sys.exit(0)
    check = args and args[0] == "--check"
    if check:
        args = args[1:]
    if len(args) != 1:
        sys.exit(__doc__)
    text = render(make_all())
    if check:
        sys.exit(0 if open(args[0]).read() == text else 1)
    open(args[0], "w").write(text)
    for n, b in make_all().items():
        print("%-14s %4d bytes" % (n, len(b)))
