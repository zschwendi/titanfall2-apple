# Method: how the work was organised

The project was built by one planner and many small workers. The planner is a large language model.
It wrote plans and short task briefs. Most of the small tasks were done by a small model, Claude
Haiku, in waves. This document says how that was organised, how the work was checked, and what
rules kept game data and accounts out of the published repository.

## 1. Roles

| Role | Who | Does |
|---|---|---|
| Owner | A person | Sets the goals and decides the open questions, such as the licence and the Origin SDK stand-ins |
| Planner | A large model | Writes plans, and short task briefs for each unit of work |
| Scout | A small model, read-only | Gathers facts from the code and the game's files before a code task is written. Changes nothing |
| Worker | A small model | Does one unit of work in its own git worktree, then runs the unit's check command |

## 2. Waves

`tools/swarm/wave.py` runs a wave of units with headless workers, and no orchestrating session takes
turns in between:

1. Each unit is claimed, and a worker does it in its own git worktree.
2. The unit's check command runs. The unit is merged only when the check passes.
3. If the check fails, one more worker runs with the end of the check log. If that also fails, the
   unit is left claimed for a stronger model.
4. At the end, one suite runs once on the merged tree, and the ledger of results is committed.

The defaults are six workers at a time, 30 minutes per worker run and two attempts per unit.
Each worker's log is kept in `swarm/logs/`, which is git-ignored.

Every unit has a check command, and the unit is merged only when it passes.

## 3. Correctness oracles

| Oracle | Used for |
|---|---|
| Unicorn CPU emulator | Single instructions and whole functions. The emulator runs the original bytes; the lifted code must give the same result (the difference tests, `docs/DESIGN.md` section 8) |
| A real x86-64 PC (`tools/hworacle`) | Instructions where the emulators disagree |
| Traces of the real game under Proton on a Linux PC (`tools/trace`) | Counts and shapes of what the game does. Only counts and shapes were kept, never the game's data |

## 4. Rules that keep game data and accounts out of the repository

The repository must contain no game file, no lifted code, nothing derived from the game and no
account. Three things enforce this.

**Only tracked files are exported.** `tools/publish/export.py --out DIR` copies the tracked files
that match `tools/publish/allowlist.txt` into a new directory. An allowlist is a list of file
patterns, where a later line wins and `!` excludes. The export is a new tree, not a copy of this history.

**Every export is checked.** `tools/publish/check.py DIR` runs after each export and fails when it
finds any of these:

- a binary file, an executable, a `.vpk` or a Mach-O file, or a file over 2,000,000 bytes;
- a file whose SHA-256 matches a file in the game directory;
- an e-mail address, a user's home path, a secret, or a private network host name;
- a word on the deny list.

It prints only `path:line: rule`, never the line itself. `check.py --selftest` checks that every rule
fires on its own test file. The deny list, `tools/publish/deny.local`, is written by hand and is not tracked.

**Identities go in ignored files.** The reference PC and the signing identity are kept in git-ignored
files (`.ref-host`, `app/signing.local.mk`). The local deny list is also git-ignored. Commit messages and documents use neutral
words for the owner, hosts and paths: "the owner", "the development Mac", and no user names or home paths.
