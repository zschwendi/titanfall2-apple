#!/usr/bin/env python3
"""Guard for a publish tree: refuses binaries, game-derived files, personal or secret text.

Usage: check.py DIR [--game DIR2] [--deny FILE]
       check.py --selftest
Prints one line per finding: path:line: rule. Exit 1 if any rule fires.
Never prints the content of a scanned file.
"""
import hashlib
import os
import re
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
DEFAULT_DENY = HERE / "deny.local"
MAGIC = [b"MZ", b"DXBC", b"bplist", b"\x34\x12\xaa\x55", b"\x7fELF",
         b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xca\xfe\xba\xbe"]
LARGE = 2_000_000
SEED_MAGIC = b"HWORACLE-CACHE-SEGMENT v1\n"
EMAIL_OK = "noreply@anthropic.com"
EMAIL_OK_DOMAINS = ("example.com", "example.org", "example.net", "example.invalid",    # RFC 2606: never a real person
                    "list.winehq.org")                              # public mailing lists, cited in research_notes
EMAIL_RE = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}")
# A home directory: /Users/<name> or /home/<name> at the start of a path.  Not after a word, a dot or
# ">" ("<state dir>/home/x", "%s/home/x": the game's own home inside a state directory).
USER_RE = re.compile(r"(?<![\w.>-])/(?:U(?:sers)|h(?:ome))/[A-Za-z0-9_]")
# Placeholder homes used by tests and docs: "someone" and the one-letter "x".
USER_OK_RE = re.compile(r"/(?:U(?:sers)|h(?:ome))/(?:someone|x)(?![A-Za-z0-9_])")
SECRET_RES = [
    re.compile(r"sk\-ant\-"),
    re.compile(r"ANTHROPIC_API[_]KEY=[\"']?[^\s\"']+"),
    re.compile(r"gh[p]_|gh[o]_|github[_]pat[_]"),
    re.compile(r"-{5}BEGIN.*PRIVATE KEY"),
    re.compile(r"(?i)(?:api[_-]?key|token|secret|password).{0,4}?[=:]\s*[\"']?[^\s\"']{16,}"),
]
HOST_RE = re.compile(r"ts\.net|\b100\.(?:6[4-9]|[7-9]\d|1[01]\d|12[0-7])\.\d{1,3}\.\d{1,3}\b")


def read_deny(path):
    """Return the non-empty lines of the deny file, or None if it is missing."""
    p = Path(path)
    if not p.is_file():
        return None
    return [ln.strip() for ln in p.read_text(encoding="utf-8", errors="replace").splitlines() if ln.strip()]


def game_hashes(game_dir):
    hashes = set()
    if game_dir is None or not Path(game_dir).is_dir():
        return hashes
    for root, _dirs, files in os.walk(game_dir):
        for name in files:
            if name.lower() in ("titanfall2.exe", "originsdk.dll"):
                continue            # never read, not even to hash (the project's rule; tools/gamedata/fetch.py NEVER)
            if os.path.getsize(os.path.join(root, name)) == 0:
                continue            # an empty file is no evidence of anything (a .gitkeep would match it)
            h = hashlib.sha256()
            with open(os.path.join(root, name), "rb") as f:
                for chunk in iter(lambda: f.read(1 << 20), b""):
                    h.update(chunk)
            hashes.add(h.hexdigest())
    return hashes


def check_tree(root, game_dir, deny_file):
    """Return a list of (relpath, line, rule) findings for every file under root."""
    findings = []
    ghashes = game_hashes(game_dir)
    deny = read_deny(deny_file)
    if deny is None:
        print(f"note: deny file missing ({deny_file}); deny and deny-word host checks skipped")
        deny = []
    deny_low = [d.lower() for d in deny]
    deny_host_res = [re.compile(r"(?<![A-Za-z0-9])" + re.escape(d) + r"(?![A-Za-z0-9])", re.I) for d in deny]
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root).replace(os.sep, "/")
            with open(full, "rb") as f:
                data = f.read()
            head = data[:8192]
            try:
                head.decode("utf-8") if len(data) <= 8192 else data.decode("utf-8")
                utf_ok = True
            except UnicodeDecodeError:
                utf_ok = False
            # The one kind of binary file we publish: the x86 oracle's answers to our own test vectors
            # (tools/hworacle/seed.py), under tests/oracle_seed/, recognised by the segment magic.
            seed_ok = rel.startswith("tests/oracle_seed/") and rel.endswith(".hwc") and data.startswith(SEED_MAGIC)
            if (b"\x00" in head or not utf_ok) and not seed_ok:
                findings.append((rel, 0, "binary"))
            if len(data) > LARGE:
                findings.append((rel, 0, "large"))
            if any(data.startswith(m) for m in MAGIC):
                findings.append((rel, 0, "magic"))
            if hashlib.sha256(data).hexdigest() in ghashes:
                findings.append((rel, 0, "game-hash"))
            text = data.decode("utf-8", errors="replace")
            for n, line in enumerate(text.splitlines(), 1):
                for m in (EMAIL_RE.finditer(line) if "@" in line else ()):
                    if m.group(0).lower() != EMAIL_OK and not m.group(0).lower().endswith(tuple("@" + d for d in EMAIL_OK_DOMAINS)):
                        findings.append((rel, n, "email"))
                        break
                if USER_RE.search(USER_OK_RE.sub("", line)):
                    findings.append((rel, n, "user-path"))
                if any(r.search(line) for r in SECRET_RES):
                    findings.append((rel, n, "secret"))
                if HOST_RE.search(line) or any(r.search(line) for r in deny_host_res):
                    findings.append((rel, n, "host"))
                if any(d in line.lower() for d in deny_low):
                    findings.append((rel, n, "deny"))
    return findings


def main(argv):
    if argv and argv[0] == "--selftest":
        return selftest()
    args = list(argv)
    game = str(REPO / "game")
    deny = str(DEFAULT_DENY)
    if "--game" in args:
        i = args.index("--game"); game = args[i + 1]; del args[i:i + 2]
    if "--deny" in args:
        i = args.index("--deny"); deny = args[i + 1]; del args[i:i + 2]
    if len(args) != 1 or not os.path.isdir(args[0]):
        print("usage: check.py DIR [--game DIR2] [--deny FILE] | check.py --selftest", file=sys.stderr)
        return 2
    findings = check_tree(args[0], game, deny)
    for rel, n, rule in findings:
        print(f"{rel}:{n}: {rule}")
    return 1 if findings else 0


def selftest():
    # Trigger strings are assembled from pieces so this file itself does not fire the rules.
    secret_line = "api" + "_key = \"" + "abcdefghijklmnop1234" + "\""
    files = {
        "binary.dat": b"abc\x00def",
        "large.txt": b"a" * (LARGE + 1),
        "magic.bin": b"MZ" + b"x" * 20,
        "gamecopy.dat": b"GAME-DATA-" * 4,
        "email.txt": b"contact someone" + b"@" + b"corp-mail.org\n",
        "userpath.txt": b"see /" + b"Users/" + b"jdoe2/x\n",
        "secret.txt": secret_line.encode() + b"\n",
        "host.txt": b"node " + b"box.tail1234." + b"ts" + b".net\n",
        "deny.txt": b"the codename Zebraquill appears here\n",
        "clean.txt": b"plain text, nothing to see.\n"
                     b"the game's home: %s/h" + b"ome/two.bin, <state dir>/h" + b"ome/m.bin, a.state/h" + b"ome/s.sav\n"
                     b"a made-up address: user" + b"@" + b"example.com\n"
                     b"placeholders: /U" + b"sers/someone/Library and /h" + b"ome/x/bin\n",
    }
    rule_for = {
        "binary.dat": "binary", "large.txt": "large", "magic.bin": "magic",
        "gamecopy.dat": "game-hash", "email.txt": "email", "userpath.txt": "user-path",
        "secret.txt": "secret", "host.txt": "host", "deny.txt": "deny",
    }
    with tempfile.TemporaryDirectory() as tmp:
        tree = os.path.join(tmp, "tree"); game = os.path.join(tmp, "game")
        os.makedirs(tree); os.makedirs(game)
        for name, data in files.items():
            if name == "gamecopy.dat":
                Path(game, "original.bin").write_bytes(data)
            Path(tree, name).write_bytes(data)
        deny_file = os.path.join(tmp, "deny.txt")
        Path(deny_file).write_text("zebraquill\n", encoding="utf-8")
        findings = check_tree(tree, game, deny_file)
    fired = {(rel, rule) for rel, _n, rule in findings}
    ok = 0
    for name, rule in rule_for.items():
        if (name, rule) in fired:
            ok += 1
        else:
            print(f"selftest: rule {rule} did not fire on {name}")
    clean_hits = [f for f in findings if f[0] == "clean.txt"]
    for f in clean_hits:
        print(f"selftest: clean.txt wrongly flagged: {f[2]}")
    if clean_hits or ok != len(rule_for):
        return 1
    print(f"check selftest: {ok} rules ok")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
