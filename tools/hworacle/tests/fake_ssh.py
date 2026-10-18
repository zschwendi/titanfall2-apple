#!/usr/bin/env python3
"""fake_ssh.py - a stand-in for the `ssh` program, used only by test_client.py.

client.Oracle runs   [client.SSH, "-o", ..., "-C", host, script]   with a batch on stdin.  This
program receives the same argv, ignores the options and the host, takes the last argument as the
remote shell script, works out which of the client's scripts it is by its contents, and does what
the x86 machine would have done - without any other machine, without running the script, and
without touching anything outside the state directory named by $FAKE_SSH_STATE:

    calls.jsonl     one JSON object per invocation (see "log entry" below)
    knobs.json      failure injection, read at the start of every invocation (all keys optional)
    builds/<hash>   a runner build exists (made by a build script, removed by a purge script)
    blobs/<hex>     a stored memory image, with its bytes (the upload script checks the SHA-256 and
                    exits 9 on a mismatch, as the real one does)
    created         the remote directory tf2-hw-oracle exists

Script kinds: build, blob_list, blob_upload, runner (a batch or --selftest), ps, listing, purge,
unknown (exit 127).  A runner invocation for a build that does not exist exits 97 without reading
its input, like the real `test -x ... || exit 97`.

Like the real runner it answers REFUSED / detail 34 (insns 0) for a case with the unchecked flag when
--allow-unchecked is not among the runner's arguments.

The fake CPU answers each case from a hash of its wire record (case.record(0)) and the salt, so an
answer depends on the whole register state and on nothing else:
    outcome DONE, insns 1, rip = case.rip + 1, rax = first 8 bytes of SHA-256(record + salt),
    xmm3 = the next 16 bytes, rflags = digest[24] & 0xCD5, and one 4-byte memory run (digest[25:29])
    at the address of the case's first writable region, if it has one.

Knobs (knobs.json), every one optional:
    unreachable        true: exit 255 with a message on stderr before reading anything
    fail_after_calls   N: after N runner invocations have run (not counting exit 97), every invocation
                       of any kind is unreachable
    truncate_after     N: the runner's stdout is cut after N bytes, exit 0
    drop_end_marker    true: the runner's stdout lacks the final end marker, exit 0
    garbage_header     true: the runner's stdout starts with 400 bytes that are not a result header
    fault_from_call    N (default 0): truncate_after / drop_end_marker / garbage_header apply only to
                       runner invocations that run number N or later (0-based)
    lose_blob          N: for the next N runner invocations that reference a stored blob, the first
                       such blob is deleted from the store before the runner looks, so the runner
                       answers status 2 (missing blobs) + the 32-byte digests + end marker, exit 4
    timeout_cases      [i, ...]: batch positions that get outcome TIMEOUT (detail = time limit)
    killed_cases       [i, ...]: batch positions that get outcome KILLED (detail 9, SIGKILL)
    outcome_cases      {"i": [outcome, detail], ...}: any other outcome for a batch position (a refusal
                       by inspection [2, 17], a bad case [5, 7], a runner error [6, 0], ...); the record
                       has insns 0 and no changed registers
    answer_salt        string mixed into every answer (hardware that answers differently)
    selftest_fails     true: every runner invocation answers status 3 (self-test failed), exit 5
    wrong_build        string: the header's build field (instead of the hash in the runner path)
    wrong_answers_rev  N: the header's answers revision
    build_warns        true: the compiler prints a warning (with -Werror in the flags: exit 1, nothing built)
    build_fails        true: the build script fails (exit 1, nothing built)
    ps_lines           [str, ...]: what the remote-processes script prints

Log entry (one per invocation, written at the end of it):
    kind, script, argv (the ssh options and host), stdin_len (None when unreachable: nothing was
    read), exit, ran (a runner invocation that got past the build check and the unreachable knobs),
    and for a runner invocation: args (what follows the runner path), n_cases, header (the batch
    header: version, flags, time_limit_ms, group_size, batch_limit_s), stored (hex digests of the
    blobs the batch says are stored on the machine), case_keys (hex Case.key() of each case in the
    order received), status (the result header's status), note.
"""

import sys

sys.dont_write_bytecode = True

import hashlib  # noqa: E402
import json  # noqa: E402
import os  # noqa: E402
import re  # noqa: E402
import struct  # noqa: E402
import traceback  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))          # tools/hworacle, where client.py lives

import client  # noqa: E402

D = "tf2-hw-oracle"
CPU_SIGNATURE, MICROCODE, FLAGS, XCR0 = 0x906EA, 0xF0, 3, 0x1F


class State:
    def __init__(self, root):
        self.root = root
        self.log_path = os.path.join(root, "calls.jsonl")
        self.knobs_path = os.path.join(root, "knobs.json")
        self.builds = os.path.join(root, "builds")
        self.blobs = os.path.join(root, "blobs")
        os.makedirs(self.builds, exist_ok=True)
        os.makedirs(self.blobs, exist_ok=True)

    def knobs(self):
        try:
            with open(self.knobs_path) as f:
                return json.load(f)
        except FileNotFoundError:
            return {}

    def save_knobs(self, knobs):
        with open(self.knobs_path, "w") as f:
            json.dump(knobs, f)

    def read_log(self):
        try:
            with open(self.log_path) as f:
                return [json.loads(line) for line in f if line.strip()]
        except FileNotFoundError:
            return []

    def append_log(self, entry):
        with open(self.log_path, "a") as f:
            f.write(json.dumps(entry) + "\n")

    def mark_created(self):
        with open(os.path.join(self.root, "created"), "w"):
            pass

    def created(self):
        return os.path.exists(os.path.join(self.root, "created"))

    def has_build(self, h):
        return os.path.exists(os.path.join(self.builds, h))

    def blob_names(self):
        return sorted(os.listdir(self.blobs))


def classify(script):
    if "gcc " in script:
        return "build"
    if "sha256sum" in script:
        return "blob_upload"
    if "/proc/" in script and "readlink" in script:
        return "ps"
    if "du -sh" in script:
        return "listing"
    if "rm -f bin/runner-*" in script:
        return "purge"
    if "mkdir -p %s/blobs && ls %s/blobs" % (D, D) in script:
        return "blob_list"
    if re.search(r"bin/runner-[0-9a-f]{16}", script) and "exec nice" in script:
        return "runner"
    return "unknown"


def result_header(status, n, build, message, answers_rev=None):
    return client.RESULT_HDR.pack(
        client.MAGIC_RESULT, client.FORMAT_VERSION, status, n, CPU_SIGNATURE, MICROCODE, FLAGS,
        client.ANSWERS_REV if answers_rev is None else answers_rev, XCR0,
        b"Fake CPU", b"Linux 6.0.0-fake", build.encode(), message.encode())


def end_marker(n):
    return struct.pack("<II", client.MAGIC_END_OUT, n)


def fake_cpu(index, case, salt):
    d = hashlib.sha256(case.record(0) + salt).digest()
    gpr = list(case.gpr)
    gpr[0] = int.from_bytes(d[:8], "little")
    xmm = bytearray(case.xmm)
    xmm[48:64] = d[8:24]
    mem = []
    for r in case.regions:
        if r.prot & client.PROT_W:
            mem = [(r.addr, d[25:29])]
            break
    return client.encode_result(index, case, client.DONE, 0, gpr=gpr, xmm=bytes(xmm), rip=case.rip + 1,
                                rflags=d[24] & 0xCD5, insns=1, mem=mem)


def handle_build(script, stdin, st, knobs, entry):
    h = re.search(r"src/runner-([0-9a-f]{16})\.c", script).group(1)
    st.mark_created()
    if knobs.get("build_fails"):
        sys.stderr.write("fake gcc: error: build_fails knob set\n")
        return 1
    # The real build writes the shipped bytes to runner-<h>.c and compiles them: the hash in the name
    # must be the hash of the source and the flags, or the client and the runner disagree about what
    # "this build" is.
    want = hashlib.sha256(stdin + b"\0" + client.GCC_FLAGS.encode()).hexdigest()[:16]
    if want != h or client.GCC_FLAGS not in script:
        sys.stderr.write("fake gcc: the shipped source or the flags do not match the hash %s (computed %s)\n" % (h, want))
        entry["note"] = "build hash mismatch"
        return 1
    if knobs.get("build_warns"):
        sys.stderr.write("runner.c:1:1: warning: fake warning\n")
        if "-Werror" in script:                        # as gcc: the warning is an error, nothing is built
            return 1
    with open(os.path.join(st.builds, h), "wb") as f:
        f.write(hashlib.sha256(stdin).digest())
    return 0


def handle_upload(script, stdin, st, entry):
    h = re.search(r"cd %s/blobs; cat > \.tmp-([0-9a-f]{64})\.\$\$" % D, script).group(1)
    st.mark_created()
    if hashlib.sha256(stdin).hexdigest() != h:
        sys.stderr.write("fake blob upload: the bytes received do not match %s\n" % h)
        entry["note"] = "sha256 mismatch"
        return 9
    with open(os.path.join(st.blobs, h), "wb") as f:
        f.write(stdin)
    return 0


def handle_runner(script, stdin, st, knobs, entry, ran_before):
    h = re.search(r"bin/runner-([0-9a-f]{16})", script).group(1)
    args = script[script.rindex("bin/runner-" + h) + len("bin/runner-" + h):].strip()
    entry["args"] = args
    if not st.has_build(h):
        return 97, b""
    entry["ran"] = True
    build = knobs.get("wrong_build", h)
    rev = knobs.get("wrong_answers_rev")
    if knobs.get("selftest_fails"):
        entry["status"] = 3
        return 5, result_header(3, 0, build, "self-test: results missing", rev) + end_marker(0)
    if "--selftest" in args.split():
        entry["status"] = 0
        return 0, result_header(0, 0, build, "self-test passed", rev) + end_marker(0)

    try:
        hdr, cases = client.unpack_batch(stdin)
        if hdr["version"] != client.FORMAT_VERSION:
            raise ValueError("format version not supported by this runner")
    except Exception as e:                              # noqa: BLE001 - the real runner says "malformed batch"
        entry["status"] = 1
        return 5, result_header(1, 0, build, "malformed batch (%s)" % e, rev) + end_marker(0)
    entry["n_cases"] = len(cases)
    entry["header"] = {k: hdr[k] for k in ("version", "flags", "time_limit_ms", "group_size", "batch_limit_s")}
    entry["stored"] = [d.hex() for d in hdr["stored_blobs"]]
    entry["case_keys"] = [c.key().hex() for c in cases]

    stored = hdr["stored_blobs"]
    lose = int(knobs.get("lose_blob") or 0)
    if lose and stored:
        for d in stored:
            p = os.path.join(st.blobs, d.hex())
            if os.path.exists(p):
                os.unlink(p)
                break
        knobs["lose_blob"] = lose - 1
        st.save_knobs(knobs)
    missing = [d for d in stored if not os.path.exists(os.path.join(st.blobs, d.hex()))]
    if missing:
        entry["status"] = 2
        return 4, (result_header(2, len(missing), build, "memory blobs missing or damaged in the blob directory", rev)
                   + b"".join(missing) + end_marker(len(missing)))

    salt = str(knobs.get("answer_salt", "")).encode()
    timeouts, killed = set(knobs.get("timeout_cases", [])), set(knobs.get("killed_cases", []))
    others = {int(k): v for k, v in knobs.get("outcome_cases", {}).items()}
    recs = []
    for i, c in enumerate(cases):
        if i in timeouts:
            recs.append(client.encode_result(i, c, client.TIMEOUT, detail=hdr["time_limit_ms"]))
        elif i in killed:
            recs.append(client.encode_result(i, c, client.KILLED, detail=9))
        elif i in others:
            recs.append(client.encode_result(i, c, others[i][0], detail=others[i][1]))
        elif c.unchecked and "--allow-unchecked" not in args.split():
            recs.append(client.encode_result(i, c, client.REFUSED, detail=34))
        else:
            recs.append(fake_cpu(i, c, salt))
    entry["status"] = 0
    stream = result_header(0, len(cases), build, "", rev) + b"".join(recs) + end_marker(len(cases))
    if ran_before >= int(knobs.get("fault_from_call", 0)):
        if knobs.get("garbage_header"):
            stream = b"GARBAGE!" * 50 + stream[400:]
        elif knobs.get("truncate_after") is not None:
            stream = stream[:int(knobs["truncate_after"])]
        elif knobs.get("drop_end_marker"):
            stream = stream[:-8]
    return 0, stream


def handle_listing(st):
    if not st.created():
        return "(no %s directory)\n" % D
    out = "8.0K\t.\n"
    for h in sorted(os.listdir(st.builds)):
        out += "%10d  ./bin/runner-%s\n" % (32, h)
    for name in st.blob_names():
        out += "%10d  ./blobs/%s\n" % (os.path.getsize(os.path.join(st.blobs, name)), name)
    return out


def run(argv):
    root = os.environ.get("FAKE_SSH_STATE")
    if not root or not os.path.isdir(root):
        sys.stderr.write("fake_ssh: FAKE_SSH_STATE must name an existing directory\n")
        return 99
    st = State(root)
    script = argv[-1]
    kind = classify(script)
    knobs = st.knobs()
    entry = {"kind": kind, "script": script, "argv": argv[:-1], "stdin_len": None, "ran": False}
    log = st.read_log()
    ran_before = sum(1 for e in log if e["kind"] == "runner" and e.get("ran"))
    code = 0
    try:
        fail_after = knobs.get("fail_after_calls")
        if knobs.get("unreachable") or (fail_after is not None and ran_before >= int(fail_after)):
            sys.stderr.write("ssh: connect to host fake-host port 22: Connection refused\n")
            entry["note"] = "unreachable"
            code = 255
            return code
        stdin = sys.stdin.buffer.read()
        entry["stdin_len"] = len(stdin)
        out = b""
        if kind == "build":
            code = handle_build(script, stdin, st, knobs, entry)
        elif kind == "blob_upload":
            code = handle_upload(script, stdin, st, entry)
        elif kind == "blob_list":
            st.mark_created()
            out = "".join(n + "\n" for n in st.blob_names()).encode()
        elif kind == "runner":
            code, out = handle_runner(script, stdin, st, knobs, entry, ran_before)
        elif kind == "ps":
            out = "".join(line + "\n" for line in knobs.get("ps_lines", [])).encode()
        elif kind == "listing":
            out = handle_listing(st).encode()
        elif kind == "purge":
            for d in (st.builds, st.blobs):
                for n in os.listdir(d):
                    os.unlink(os.path.join(d, n))
        else:
            sys.stderr.write("fake_ssh: not a script this stand-in knows: %r\n" % script[:200])
            code = 127
        sys.stdout.buffer.write(out)
        sys.stdout.buffer.flush()
        return code
    except Exception:                                    # noqa: BLE001 - make a fake bug loud
        traceback.print_exc()
        entry["note"] = "fake_ssh crashed"
        code = 98
        return code
    finally:
        entry["exit"] = code
        st.append_log(entry)


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
