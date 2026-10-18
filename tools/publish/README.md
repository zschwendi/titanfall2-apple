# tools/publish

- `export.py --out DIR [--allowlist tools/publish/allowlist.txt]`: copies the tracked files
  matched by the allowlist (fnmatch globs; `#` comments; `!` excludes; last match wins) into a
  new DIR, then runs `check.py DIR` and exits with its status.
- `check.py DIR [--game DIR2] [--deny FILE]`: fails on binaries, files over 2,000,000 bytes,
  executable/VPK/Mach-O magic, files whose SHA-256 matches a file under the game directory,
  e-mail addresses, user home paths, secrets, Tailscale hosts and denied words. Prints
  `path:line: rule` only; never the content.
- `check.py --selftest`: checks that every rule fires on its own test file.

The deny file `tools/publish/deny.local` is untracked and must be written by hand; it is
ignored by git.
