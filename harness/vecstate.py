"""Vector operands of the instruction test: the values at which a packed integer instruction
can be wrong (review docs/reviews/insn-wave-review.md, finding 1; docs/DESIGN.md section 8).

insn_state fills the xmm registers with random bytes.  Random bytes never give a packed shift
a count below its lane width, a packed compare two equal lanes, or a pack a lane inside its
range, so a translation wrong only there passed every state.  `structure` rewrites the vector
operands (xmm registers and 16-byte memory operands that are read) of the EVEN states of an
instruction; the odd states are exactly what they were (random registers, boundary values
poked into memory operands).  It draws from a random stream of its own, seeded with the state's
random xmm bytes, so the registers, flags and seed of every state are what they were too, and
the same seed gives the same states.

What an even state holds is decided by the class of the mnemonic (CLASS_TABLE) and the
position of the state among the instruction's structured states (`slot_of`: states spent on the
#GP of a misaligned operand are not counted, so that the memory form sees every recipe with
its result compared).  The recipes, lane by lane at the lane width of the class:

  count        the count operand's low quadword from COUNTS (0, 1, width-1, width, width+1, 2^32,
               2^32+1, then two counts strictly inside the width); its high quadword and the
               data stay random
  equal        the destination stays random; of the source lanes some are copies of the
               destination lane, some the copy plus or minus one, some stay as they were
  edges        both operands from the pool of the class (`edges`: the ends of the lane's signed
               and unsigned ranges and their neighbours; for a pack also those of the narrower
               type), walking through the pairs of the pool so that every pool value is met
  edge-equal   the destination from the pool, every source lane its copy or the copy plus or
               minus one
  mixed        every second lane of the destination and the other lanes of the source from the
               pool, the rest as they were (a limit against a random value)

A mnemonic nobody classified gets the general mix: equal, edges, mixed in turn, the lane width
going through 8, 16, 32 and 64.  An instruction with one vector operand (an immediate shift,
pshufd's source, a store) gets the same recipes on that operand; `equal` then leaves it random.
"""

import os
import random
import re
import sys

from capstone import x86_const as X

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
if REPO not in sys.path:
    sys.path.insert(0, REPO)

from lifter.translate import XMM, mnemonic_of     # noqa: E402
import oraclefix as OF                            # noqa: E402

ENABLED = True          # False: the states of before (the self-tests show what those missed)

LANE = {"b": 8, "w": 16, "d": 32, "q": 64}

# The one table: a pattern for the whole mnemonic, whose group is the letter of the lane the
# instruction reads (packsswb reads words, pmuludq doublewords), and the class.
CLASS_TABLE = (
    (r"ps(?:ll|rl|ra)([wdq])",                                                         "count"),
    (r"pcmp(?:eq|gt)([bwdq])",                                                         "equal"),
    (r"pack[su]s([wd])[bw]",                                                           "pack"),
    (r"p(?:add|sub)u?s([bw])",                                                         "limits"),
    (r"p(?:avg|min|max)[su]?([bwd])",                                                  "limits"),
    (r"pmul[lh]u?([wd])|pmulhrs([w])|pmulu?([d])q|pmadd([w])d|pmaddu([b])sw|psad([b])w", "limits"),
)
# The recipe of each structured state in turn.
RECIPES = {
    "count":   ("count",),
    "equal":   ("equal", "edge-equal", "equal", "edges"),
    "pack":    ("edges", "edges", "mixed", "edge-equal"),
    "limits":  ("edges", "edge-equal", "edges", "mixed"),
    "general": ("equal", "edges", "mixed"),
}
GENERAL_LANES = (8, 16, 32, 64)
FROM_POOL = ("edges", "edge-equal", "mixed")


def classify(name):
    """(class, lane bits) of a mnemonic; ('general', None) for one the table does not have."""
    for pattern, cls in CLASS_TABLE:
        m = re.fullmatch(pattern, name)
        if m:
            return cls, LANE[next(g for g in m.groups() if g)]
    return "general", None


def edges(n, lane):
    """Where an n-bit type ends, as `lane`-bit patterns: 0, the sign boundary on both sides and
    2^n, each with the two values below it and the one above.  For n = lane these are -2, -1, 0,
    1 and the largest and smallest signed values with one neighbour each."""
    out = []
    for b in (0, 1 << (n - 1), -(1 << (n - 1)), 1 << n):
        for d in (-2, -1, 0, 1):
            v = (b + d) & ((1 << lane) - 1)
            if v not in out:
                out.append(v)
    return out


def pool_of(cls, lane):
    """The values the `edges`, `edge-equal` and `mixed` recipes draw from."""
    narrow = cls in ("pack", "general") and lane > 8
    return edges(lane, lane) + ([v for v in edges(lane // 2, lane) if v not in edges(lane, lane)] if narrow else [])


def counts(width, rng):
    """The low quadword of a packed shift's count, state after state."""
    return [0, 1, width - 1, width, width + 1, 1 << 32, (1 << 32) + 1,
            rng.randrange(2, width - 1), rng.randrange(2, width - 1)]


def slot_of(ins, k):
    """The position of the even state k among the instruction's structured states, not counting
    those in which an operand that must be aligned is put at a misaligned address (there the
    expected result is the #GP, whatever the values are)."""
    skip = OF.aligned_operand(ins) is not None
    return sum(1 for i in range(0, k, 2) if not (skip and OF.wanted_offset(i)))


def vector_operands(ins, regs, xmm, peek, ea_of):
    """[(place, 16 bytes)] for the vector operands whose value the instruction reads, in operand
    order: place is an xmm number or ('m', address).  A 16-byte memory operand in first place of
    a two-operand instruction is the destination of a store and is left out."""
    out = []
    for i, op in enumerate(ins.operands):
        if op.type == X.X86_OP_REG and op.reg in XMM:
            n = XMM[op.reg]
            out.append((n, bytes(xmm[16 * n:16 * n + 16])))
        elif op.type == X.X86_OP_MEM and op.size == 16 and peek is not None and not (i == 0 and len(ins.operands) > 1):
            a = ea_of(ins, op, regs)
            v = peek(a, 16)
            if v is not None:
                out.append((("m", a), v.to_bytes(16, "little")))
    return out


def _split(data, lane):
    return [int.from_bytes(data[i:i + lane // 8], "little") for i in range(0, 16, lane // 8)]


def _join(lanes, lane):
    return b"".join(v.to_bytes(lane // 8, "little") for v in lanes)


def structure(ins, regs, xmm, k, peek, ea_of):
    """(xmm, pokes) of state k: `xmm` (256 random bytes) with the vector operands of `ins`
    rewritten as the module text says, and the pokes for those in memory.  Odd states, states
    without an index (the float cases, which have operands of their own: floatcase.py) and
    instructions without a vector operand come back unchanged."""
    if not ENABLED or k is None or k % 2:
        return xmm, []
    ops = vector_operands(ins, regs, xmm, peek, ea_of)
    if not ops:
        return xmm, []
    rng = random.Random(b"vec" + bytes(xmm))               # a stream of its own: the caller's is not advanced
    s = slot_of(ins, k)
    cls, lane = classify(mnemonic_of(ins))
    two = len(ops) > 1
    pair = two and ops[1][0] != ops[0][0]                  # one register twice is one value
    if cls == "count" and not two:                         # the immediate form: only the data
        cls = "general"
    elif lane is None:
        lane = GENERAL_LANES[s % len(GENERAL_LANES)]
    cycle = RECIPES[cls]
    recipe = cycle[s % len(cycle)]
    new = {}                                               # place -> its 16 bytes
    if recipe == "count":
        q = _split(ops[1][1], 64)
        q[0] = counts(lane, rng)[s % 9]
        new[ops[1][0]] = _join(q, 64)
    else:
        mask, n = (1 << lane) - 1, 128 // lane
        d = _split(ops[0][1], lane)
        b = _split(ops[1][1], lane) if pair else None
        pool = pool_of(cls, lane)
        p = len(pool)
        q0 = n * sum(1 for t in range(s) if cycle[t % len(cycle)] in FROM_POOL)
        pairs = [(pool[q % p], pool[(q + q // p + p // 2) % p]) for q in range(q0, q0 + n)]
        rng.shuffle(pairs)
        if recipe == "equal":
            if pair:
                kinds = [(0, None, 1, 0, None, -1)[(i + s) % 6] for i in range(n)]
                rng.shuffle(kinds)
                b = [v if kind is None else (x + kind) & mask for x, v, kind in zip(d, b, kinds)]
        elif recipe == "edges":
            d = [x for x, _ in pairs]
            if pair:
                b = [y for _, y in pairs]
        elif recipe == "edge-equal":
            d = [x for x, _ in pairs]
            if pair:
                deltas = [(0, 1, 0, -1)[(i + s) % 4] for i in range(n)]
                rng.shuffle(deltas)
                b = [(x + dx) & mask for x, dx in zip(d, deltas)]
        else:                                              # mixed
            d = [pairs[i][0] if i % 2 == 0 else v for i, v in enumerate(d)]
            if pair:
                b = [pairs[i][1] if i % 2 else v for i, v in enumerate(b)]
        new[ops[0][0]] = _join(d, lane)
        if pair:
            new[ops[1][0]] = _join(b, lane)
    out, pokes = bytearray(xmm), []
    for place, old in ops[:2]:
        if place not in new or new[place] == old:
            continue
        if isinstance(place, tuple):
            pokes.append((place[1], new[place]))
        else:
            out[16 * place:16 * place + 16] = new[place]
        new.pop(place)
    return bytes(out), pokes
