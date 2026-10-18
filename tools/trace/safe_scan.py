#!/usr/bin/env python3
"""Run relay.py's --commit-safe rules over text files that are about to be committed.

    tools/trace/safe_scan.py docs/trace/*.md docs/findings/39-relay-trace.md   exit 1 on a catch

A slice is prose around tool output, so before the rules are applied to a line these are taken
out of it, because they are names and addresses of code, not data of the run:
  - call sites and addresses written module+0xrva;
  - the function names of the committed census (docs/trace/census-mission1.tsv), the tokens of
    tools/trace/safe_ok.txt (looked at by hand, each with its reason) and any name written as
    Interface::Method;
  - runs made only of - = _ | : (table rules);
  - the slashes of a path: each segment is checked on its own.
What is left is checked for: an e-mail address; 24 or more characters of the base64/hex alphabet
in a row unless they are letters and underscores only; 10 or more digits in a row inside quotes;
the words auth, token, ticket, persona, passw outside a Windows function's name; the name of this
machine's user.  A catch prints file, line number and rule, never the text.  --words adds words
(comma-separated) that must not appear, for the other machine's host and user names.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import relay  # noqa: E402

SITE = re.compile(r'[\w.()]+\+0x[0-9a-fA-F]+')
METHOD = re.compile(r'\b\w+::~?\w+')
RULES = re.compile(r'[-=_|: ]{6,}')
NAME_OK = re.compile(r'\b(OpenProcessToken|GetTokenInformation|AdjustTokenPrivileges|OpenThreadToken|'
                     r'DuplicateToken\w*|LookupPrivilegeValue\w*|Authenticode|authenticat\w+|author\w*|'
                     r'tokeni[sz]\w+|\w*_token_\w*)\b', re.I)


def census_names():
    names = set()
    path = os.path.join(os.path.dirname(os.path.dirname(HERE)), 'docs', 'trace', 'census-mission1.tsv')
    if os.path.exists(path):
        with open(path) as f:
            for line in f:
                p = line.split('\t')
                if len(p) > 2 and not line.startswith('#'):
                    names.add(p[2].split('.', 1)[-1])
    with open(os.path.join(HERE, 'safe_ok.txt')) as f:
        names.update(line.strip() for line in f if line.strip() and not line.startswith('#'))
    return sorted((n for n in names if len(n) >= 20), key=len, reverse=True)


def main(argv):
    words = []
    files = []
    it = iter(argv)
    for x in it:
        if x == '--words':
            words = next(it).split(',')
        else:
            files.append(x)
    safe = relay.Safe(True, False, words)
    names = census_names()
    caught = 0
    for path in files:
        with open(path, errors='replace') as f:
            for n, line in enumerate(f, 1):
                text = SITE.sub(' ', line).replace('/', ' ')      # a path is checked segment by segment
                text = METHOD.sub(' ', text)
                text = RULES.sub(' ', text)
                for name in names:
                    if name in text:
                        text = text.replace(name, ' ')
                text = NAME_OK.sub(' ', text)
                reason = safe._check('', text)
                if reason:
                    caught += 1
                    print('%s:%d: %s' % (path, n, reason))
    print('safe_scan: %d files, %d lines caught' % (len(files), caught))
    return 1 if caught else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
