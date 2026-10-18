"""Guest patches: the places where the lifted code differs from the game's code ON PURPOSE.

Everything else the lifter writes is a translation: the lifted function does what the game's
instructions do, and where the lifter cannot say exactly what they do it writes nothing.  A
guest patch is the one exception.  It changes the behaviour of the game at one instruction,
because the owner of the project decided that the game's own behaviour there is a defect
that this port does not keep.  It must never be mistaken for the lifter's translation, so:

  * every patch is a row of one tracked, reviewed file, lifter/guest_patches.toml, which says
    for each what it does, why, and where that was found and decided;
  * a patch names its module, the SHA-256 of the DLL file it was written for, the rva of the
    instruction and the exact bytes expected there.  It is applied only when all of them
    match.  A patch listed for the module being lifted that does not match (another file,
    other bytes, an rva that is no instruction of a lifted function, a shape its kind does not
    allow) STOPS THE LIFT (GuestPatchError): a patch that silently stopped applying would
    bring the defect back without anybody noticing;
  * the lifted C says so at the instruction (the line after the instruction's own comment
    begins with the words GUEST PATCH), and the module's report lists every patch applied,
    in the function's record and in the summary;
  * `lifter.lift --no-patches` (GUEST_PATCHES=off for the make targets) lifts without them,
    so that the game's own behaviour can always be measured.  The patches change nothing
    else: the C of an unpatched lift is the C this lifter writes with an empty table.

There is one kind of patch, and no language for writing others:

  reuse-load   The instruction at `at` is a load, `mov r64, qword ptr [base + disp]`.  It
               does not read memory any more: its destination register takes the value that
               the earlier load at `reuse` put in ITS destination register.  The row's author
               asserts that the two loads read the same location and that the routine means
               one value (the table says on what that rests); the lifter checks the rest,
               from the decoded function: both are plain 64-bit loads of that form, in one
               lifted function; the code between them is a straight line that every
               execution of `at` has come down (no branch into it, no call, jump or return
               in it); the instructions between them are other loads of that form and
               nothing else, so nothing there writes memory; none of them writes the
               register that holds the first value.  Run by one thread, a patched function
               therefore does what the game's does whenever the author's assertion holds.

The table and this file are part of what identifies a lift (lift.lifter_stamp) and of the key
of the shared cache of lifted modules (tools/liftcache/liftcache.py).
"""

import hashlib
import os
import tomllib

from capstone import x86_const as X

from .translate import GPR, Encoding, Unsupported, legacy_prefixes, mnemonic_of, reg_rd, reg_wr

TABLE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "guest_patches.toml")
MARK = "GUEST PATCH"                # what a patched instruction's C begins with
KINDS = ("reuse-load",)
UNPATCHED_DIR = os.path.join("build", "lifted-unpatched")      # where `lifter.lift --no-patches` writes by default

_FIX_KEYS = {"kind", "what", "why", "ref", "decided"}
_PATCH_KEYS = {"fix", "module", "sha256", "at", "bytes", "reuse", "reuse_bytes"}


class GuestPatchError(Exception):
    """The table is malformed, or a patch listed for the module being lifted does not match
    it.  Nothing catches it inside the lifter: the lift stops and writes nothing."""


class Patch:
    def __init__(self, fix, kind, module, sha256, at, code, reuse, reuse_code, what="", why="", ref=()):
        self.fix, self.kind, self.module, self.sha256 = fix, kind, module, sha256
        self.at, self.code, self.reuse, self.reuse_code = at, bytes(code), reuse, bytes(reuse_code)
        self.what, self.why, self.ref = what, why, tuple(ref)

    def name(self):
        return "%s at %s+0x%x" % (self.fix, self.module, self.at)


def _hex(text, where):
    try:
        b = bytes.fromhex(text)
    except (ValueError, TypeError):
        b = b""
    if not b:
        raise GuestPatchError("%s: %r is not a string of hex bytes" % (where, text))
    return b


def load_table(path=None):
    """The patches of the table file, checked for form: [Patch].  Every key must be one this
    file knows, every field must be there: a misspelt field is an error, not a default."""
    path = path or TABLE
    try:
        with open(path, "rb") as f:
            doc = tomllib.load(f)
    except (OSError, tomllib.TOMLDecodeError) as e:
        raise GuestPatchError("%s: cannot be read as a patch table: %s" % (path, e))
    if set(doc) - {"fix", "patch"}:
        raise GuestPatchError("%s: unknown top-level entries: %s" % (path, ", ".join(sorted(set(doc) - {"fix", "patch"}))))
    fixes = doc.get("fix", {})
    for name, fx in fixes.items():
        where = "%s: [fix.%s]" % (path, name)
        if not isinstance(fx, dict) or set(fx) - _FIX_KEYS or not {"kind", "what", "why", "ref"} <= set(fx):
            raise GuestPatchError("%s must have kind, what, why and ref (and may have decided), nothing else" % where)
        if fx["kind"] not in KINDS:
            raise GuestPatchError("%s: kind %r; the kinds are: %s" % (where, fx["kind"], ", ".join(KINDS)))
        if not all(isinstance(fx[k], str) and fx[k].strip() for k in ("what", "why")):
            raise GuestPatchError("%s: what and why must say something" % where)
        if not (isinstance(fx["ref"], list) and fx["ref"] and all(isinstance(r, str) and r.strip() for r in fx["ref"])):
            raise GuestPatchError("%s: ref must be a list of references" % where)
    out, seen = [], set()
    for i, row in enumerate(doc.get("patch", [])):
        where = "%s: [[patch]] number %d" % (path, i + 1)
        if not isinstance(row, dict) or set(row) != _PATCH_KEYS:
            raise GuestPatchError("%s must have exactly: %s" % (where, ", ".join(sorted(_PATCH_KEYS))))
        if row["fix"] not in fixes:
            raise GuestPatchError("%s: there is no [fix.%s]" % (where, row["fix"]))
        if not (isinstance(row["module"], str) and row["module"]):
            raise GuestPatchError("%s: module must be the lifter's name of a module" % where)
        sha = row["sha256"]
        if not (isinstance(sha, str) and len(sha) == 64 and all(c in "0123456789abcdef" for c in sha)):
            raise GuestPatchError("%s: sha256 must be 64 lower-case hex digits" % where)
        for k in ("at", "reuse"):
            if not (isinstance(row[k], int) and not isinstance(row[k], bool) and row[k] > 0):
                raise GuestPatchError("%s: %s must be an rva" % (where, k))
        fx = fixes[row["fix"]]
        p = Patch(row["fix"], fx["kind"], row["module"], sha, row["at"], _hex(row["bytes"], where + ", bytes"),
                  row["reuse"], _hex(row["reuse_bytes"], where + ", reuse_bytes"), fx["what"], fx["why"], fx["ref"])
        if (p.module, p.at) in seen:
            raise GuestPatchError("%s: a second patch at %s+0x%x" % (where, p.module, p.at))
        seen.add((p.module, p.at))
        out.append(p)
    return out


def file_sha256(image):
    """SHA-256 of the file the image was read from (Image.from_pe); of the mapped bytes for an
    image that came from no file (Image.synthetic, the self-tests), unless the image carries one."""
    return getattr(image, "file_sha256", None) or hashlib.sha256(bytes(image.data)).hexdigest()


def plan(image, patches=True):
    """What lift_module asks for before it lifts `image`: the Plan of the patches listed for
    this module.  `patches`: True, the table file; False, no patches at all (--no-patches: the
    table is not even read); or a list of Patch, for the self-tests.

    A patch listed for a module of this name but for another file stops the lift here: the
    game's DLL is not the one the patch was reviewed against, and whether the patch is still
    right, still needed, or now wrong is a question for a person."""
    if patches is False:
        return Plan(image, [], False, None)
    table = load_table() if patches is True else list(patches)
    mine = [p for p in table if p.module == image.name]
    have = file_sha256(image) if mine else ""
    for p in mine:
        if p.sha256 != have:
            raise GuestPatchError(
                "guest patch %s was written for a %s whose file has SHA-256 %s; the file being lifted has %s. "
                "A patch is applied only to the file it was reviewed against: review it for this file and "
                "add a row (%s), or lift without patches (--no-patches, GUEST_PATCHES=off)"
                % (p.name(), p.module, p.sha256, have, os.path.relpath(TABLE)))
    return Plan(image, mine, True, TABLE if patches is True else None)


class Plan:
    """The patches of one module during one lift.  lift_function hands every lifted function
    to apply(); lift_module asks finish() for the record once all functions are through."""

    def __init__(self, image, patches, enabled, table):
        self.image = image
        self.enabled = enabled
        self.table = table                  # the table file the patches came from; None: given by the caller, or none
        self.by_at = {p.at: p for p in patches}
        self.applied = []

    def apply(self, fn, cfg, stmts):
        """Apply the patches whose instruction is in this lifted function: replace the C
        statements of that instruction in `stmts` (rva -> [statement]).  Returns the records
        for the function's entry in the report."""
        if not self.by_at:
            return []
        recs = []
        for at in sorted(a for a in self.by_at if a in cfg.insns):
            p = self.by_at[at]
            stmts[at] = _APPLY[p.kind](self.image, p, cfg, stmts)
            recs.append({"fix": p.fix, "kind": p.kind, "at": "0x%x" % p.at, "reuse": "0x%x" % p.reuse,
                         "function": "0x%x" % fn.entry})
        self.applied += recs
        return recs

    def finish(self):
        """The summary's entry.  A patch of this module that no lifted function took stops the lift."""
        done = {int(r["at"], 16) for r in self.applied}
        for at, p in sorted(self.by_at.items()):
            if at not in done:
                raise GuestPatchError("guest patch %s was not applied: 0x%x is not an instruction of any function the lifter "
                                      "lifted (a wrong rva, or its function was skipped)" % (p.name(), at))
        if not self.enabled:
            return {"enabled": False, "applied": [],
                    "note": "lifted with --no-patches: the game's own behaviour everywhere, whatever the table lists"}
        entry = {"enabled": True, "table": "(given by the caller)", "applied": sorted(self.applied, key=lambda r: int(r["at"], 16))}
        if self.table:
            with open(self.table, "rb") as f:
                entry["table_sha256"] = hashlib.sha256(f.read()).hexdigest()
            entry["table"] = os.path.relpath(self.table, os.path.dirname(os.path.dirname(TABLE))).replace(os.sep, "/")
        return entry


def describe(entry):
    """Lines for the lifter's printed summary; nothing for a module without patches."""
    if not entry["enabled"]:
        return ["guest patches: NOT APPLIED (--no-patches): this is the game's own behaviour, for measuring it"]
    if not entry["applied"]:
        return []
    return ["guest patches, deliberate changes of the game's behaviour (%s): %d applied: %s" % (
        entry["table"], len(entry["applied"]), ", ".join("%s at %s" % (r["fix"], r["at"]) for r in entry["applied"]))]


# ---- reuse-load -----------------------------------------------------------------------------------

def _plain_load(ins):
    """The destination register of `ins` if it is `mov r64, qword ptr [base + disp]` with no
    prefix, no index and no segment (opcode 8B with REX.W), else None."""
    if mnemonic_of(ins) != "mov" or legacy_prefixes(bytes(ins.bytes)) or len(ins.operands) != 2:
        return None
    try:
        if Encoding(ins).key != 0x8B:
            return None
    except Unsupported:
        return None
    dst, src = ins.operands
    if dst.type != X.X86_OP_REG or dst.reg not in GPR or GPR[dst.reg][0] != 64:
        return None
    if src.type != X.X86_OP_MEM or src.size != 8:
        return None
    m = src.mem
    if m.segment != 0 or m.index != 0 or m.base not in GPR or GPR[m.base][0] != 64:
        return None
    return dst.reg


def _reuse_load(image, p, cfg, stmts):
    def no(why):
        return GuestPatchError("guest patch %s does not match the code: %s" % (p.name(), why))

    loads = []
    for what, rva, code in (("reuse", p.reuse, p.reuse_code), ("at", p.at, p.code)):
        have = bytes(image.data[rva:rva + len(code)])
        if have != code:
            raise no("the bytes at 0x%x (%s) are %s, the table expects %s" % (rva, what, have.hex(" "), code.hex(" ")))
        ins = cfg.insns.get(rva)
        if ins is None:
            raise no("0x%x (%s) is not an instruction of the function that holds 0x%x" % (rva, what, p.at))
        if ins.size != len(code):
            raise no("the instruction at 0x%x (%s) is %d bytes long, the table gives %d" % (rva, what, ins.size, len(code)))
        reg = _plain_load(ins)
        if reg is None:
            raise no("the instruction at 0x%x (%s) is `%s %s`, not a plain 64-bit load `mov r64, qword ptr [base + disp]`"
                     % (rva, what, ins.mnemonic, ins.op_str))
        if not stmts.get(rva) or MARK in stmts[rva][0]:
            raise no("the instruction at 0x%x (%s) has no translation of its own to replace or to rely on" % (rva, what))
        loads.append(reg)
    first, second = loads
    if p.reuse >= p.at:
        raise no("the load whose value is reused (0x%x) must come before the patched one (0x%x)" % (p.reuse, p.at))
    a = p.reuse
    while a != p.at:
        ins = cfg.insns[a]
        nxt = a + ins.size
        fall = cfg.fall.get(a)
        if cfg.flow.get(a) is not None or fall is None or fall[0] != "next" or fall[1] != nxt:
            raise no("0x%x, between the two loads, is a call, a jump, a branch or an end (`%s %s`): the code from 0x%x to "
                     "0x%x is not a straight line" % (a, ins.mnemonic, ins.op_str, p.reuse, p.at))
        if nxt > p.at or nxt not in cfg.insns:
            raise no("the instructions after 0x%x do not lead to 0x%x" % (p.reuse, p.at))
        if nxt in cfg.labels:
            raise no("a branch lands at 0x%x, between the two loads: 0x%x can be reached without passing 0x%x"
                     % (nxt, p.at, p.reuse))
        if a != p.reuse:
            if any(r in GPR and GPR[r][1] == GPR[first][1] for r in ins.regs_access()[1]):
                raise no("`%s %s` at 0x%x, between the two loads, writes the register that holds the first load's value"
                         % (ins.mnemonic, ins.op_str, a))
            if _plain_load(ins) is None:
                raise no("`%s %s` at 0x%x stands between the two loads: only other plain 64-bit loads may (an instruction "
                         "that can write memory could make the two loads differ without any other thread)"
                         % (ins.mnemonic, ins.op_str, a))
        a = nxt
    return ["/* %s %s (lifter/guest_patches.toml): takes the value loaded at 0x%x; the game's instruction reads the "
            "memory again */" % (MARK, p.fix, p.reuse), reg_wr(second, reg_rd(first))]


_APPLY = {"reuse-load": _reuse_load}
