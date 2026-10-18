#!/usr/bin/env python3
"""Run the game; when it stops on game data that is not on this Mac, fetch it and run again.

    tools/gamedata/run_fetch.sh [--rounds N] [--floor-gb G] [--whole-max-gb G] [--game DIR]
                                [--gate3 BIN] [-- game arguments]

Each round runs `build/gate3/gate3 run` (with the game arguments, if any) and reads the end of
its log (build/gate3/run.log, or $GATE3_RUN_OUT.log).  A stop of runtime/core/file_partial.c
has the line "  fetch: tools/gamedata/fetch.py ...": what it asks for is fetched and the next
round begins.  The loop ends at any other stop or end of the run, at --rounds (40), when a
fetch is refused by the disk floor (--floor-gb, 30) or fails, when the line is not one this
loop obeys, or when a round asks for exactly what the round before fetched.  It prints a
table of what was fetched and why, and then how the last run ended.

WHAT IS OBEYED (finding 10 of docs/reviews/night-2026-10-11-code-review.md).  The log also
holds the game's own text (OutputDebugString, names, patterns), so a line is not a command
because it looks like one:
  - only the LAST fatal report of the log counts, and only if it is the runtime's report for
    game data: a line that begins, at the first column, with "FATAL runtime: game data: ",
    and the lines after it that begin with two spaces.  Text of the game that the log
    carries stands behind a channel name ("guest.debug: ...") and is not such a line;
  - the report must have exactly one line "  fetch: tools/gamedata/fetch.py ...";
  - that line is PARSED, NOT PASSED ON.  It must be one of the five forms the runtime
    prints (PATH; PATH OFF+LEN; --open PATH; --open-matching DIR/PATTERN; --manifest).  PATH
    must be a file of the manifest (exact match after fetch.py's own normalisation: the
    manifest's spelling is what is used), DIR a directory of the manifest, OFF and LEN
    numbers inside the file's size; no other option and no other word that begins with "-"
    is accepted.  fetch.py is then started with arguments this loop built itself, the path
    after "--".
A line that fails any of this is printed with the reason and the loop ends with status 5.

A FILE READ IN A ROW (the planner's answer D to gate3-run-8, built by gate3-run-9).  A reader
that goes through a file from front to back stops once per missing piece, and each stop costs
a whole run of the game.  So when a round asks for a range of the file the round before asked
a range of, further on in it, and the manifest gives the file less than 100 MiB (WHOLE_UNDER),
the whole file is fetched instead of the range, and the table says so.  A larger file is
fetched range by range as before.

--whole-max-gb G is fetch.py's: whole files only, and the loop ends with an entry at the first
file larger than G GiB that the run needs.  --game DIR runs against another install root than
game/ (the driver's GATE3_ROOT; its map is DIR.partial): a private copy, for trying things
that the shared directory must not see.

Exit status: 0 the last run ended on something that is not missing game data (the loop did
its work), 3 the disk floor, 4 the limit of rounds, 5 a fetch failed or did not help, 7 a
file larger than --whole-max-gb.

The table is also appended to build/gate3/run_fetch.tsv.  The game's files stay where they
are: nothing here reads their contents.
"""
import argparse
import os
import re
import shlex
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
FETCH = os.path.join(HERE, 'fetch.py')
REPORT = 'FATAL runtime: game data: '
FETCH_LINE = '  fetch: tools/gamedata/fetch.py '
RANGE = re.compile(r'(0x[0-9a-fA-F]{1,16}|[0-9]{1,20})\+(0x[0-9a-fA-F]{1,16}|[0-9]{1,20})')
TAIL = 1 << 20
WHOLE_UNDER = 100 << 20             # a file read in a row is fetched whole below this size (the first comment)

sys.path.insert(0, HERE)
import fetch as fetch_tool          # noqa: E402  (its manifest, its normalisation, its name rules)


class Refused(Exception):
    """A fetch line that is not obeyed, with the reason."""


def read_stop(log_path):
    """The last fatal report of the log: (its lines, the text after "fetch.py " or None, why
    there is no such text).  The third is '' when the stop is simply not about game data and
    a reason when a report for game data has no fetch line that can be taken."""
    try:
        with open(log_path, 'rb') as fp:
            fp.seek(0, os.SEEK_END)
            cut = fp.tell() > TAIL
            fp.seek(max(0, fp.tell() - TAIL))
            text = fp.read().decode('utf-8', errors='replace')
    except OSError:
        return [], None, ''
    lines = text.split('\n')
    if cut:
        lines = lines[1:]                           # the piece of a line the tail begins in
    start = None
    for i, line in enumerate(lines):
        if line.startswith('FATAL '):               # at the first column: a report, not text behind a channel name
            start = i
    if start is None:
        return [], None, ''
    report = [lines[start]]
    for line in lines[start + 1:start + 80]:
        if not line.startswith('  '):
            break
        report.append(line)
    if not lines[start].startswith(REPORT):
        return report, None, ''
    asks = [line for line in report[1:] if line.lstrip(' ').startswith('fetch:')]
    if len(asks) != 1 or not asks[0].startswith(FETCH_LINE):
        return report, None, 'the report for game data has %d fetch lines, not exactly one "%s..."' % (len(asks), FETCH_LINE)
    return report, asks[0][len(FETCH_LINE):], ''


def checked(text, manifest):
    """The arguments for fetch.py that the fetch line `text` (what stands after "fetch.py ")
    asks for, built here from its parsed and checked parts: (arguments, what it is in words).
    `manifest` is fetch.py's (lower-case path -> kind, size, the install's spelling) or None.
    Raises Refused for anything but the five forms the runtime prints."""
    try:
        words = shlex.split(text)
    except ValueError as e:
        raise Refused('it does not split into words (%s)' % e)
    if words == ['--manifest']:
        return ['--manifest'], 'the manifest'
    form = 'whole'
    if words and words[0] in ('--open', '--open-matching'):
        form, words = words[0][2:], words[1:]
    if not words or len(words) > (2 if form == 'whole' else 1):
        raise Refused('it is not one of the five forms the runtime prints')
    for w in words:
        if w.startswith('-'):
            raise Refused('the word %r begins with "-": no option but --open, --open-matching and --manifest is obeyed' % w[:80])
    if manifest is None:
        raise Refused('there is no manifest to check its path against (tools/gamedata/fetch.py --manifest)')
    name = words[0].replace('\\', '/').strip('/')
    if form == 'open-matching':
        d, _, pat = name.rpartition('/')
        why = fetch_tool.bad_name(pat)
        if why:
            raise Refused('its pattern: %s' % why)
        spelled = ''
        if d:
            e = manifest.get(fetch_tool.fold(d))
            if not e or e[0] != 'd':
                raise Refused('%r is not a directory of the manifest' % d[:200])
            spelled = e[2] + '/'
        return ['--open-matching=' + spelled + pat], 'the entries of %s that match %s' % (spelled or 'the install root', pat)
    e = manifest.get(fetch_tool.fold(name))
    if not e or e[0] != 'f':
        raise Refused('%r is not a file of the manifest' % name[:200])
    _, size, path = e
    if form == 'open':
        return ['--open', '--', path], '%s, opened' % path
    if len(words) == 1:
        return ['--', path], '%s, whole' % path
    m = RANGE.fullmatch(words[1])
    if not m:
        raise Refused('%r is not OFFSET+LENGTH in digits' % words[1][:80])
    lo, n = int(m.group(1), 0), int(m.group(2), 0)
    if n == 0 or lo >= size or lo + n > size:
        raise Refused('the bytes 0x%x+0x%x are not inside %s, which has %d bytes in the manifest' % (lo, n, path, size))
    return ['--', path, '0x%x+0x%x' % (lo, n)], '%s, 0x%x+0x%x' % (path, lo, n)


def ranged(args):
    """(path, offset) when `args` is what checked() builds for a range of a file, else None."""
    if args and len(args) == 3 and args[0] == '--':
        m = RANGE.fullmatch(args[2])
        if m:
            return args[1], int(m.group(1), 0)
    return None


def in_a_row(asked, before, manifest, limit=WHOLE_UNDER):
    """The rule of the first comment, "A FILE READ IN A ROW".  `asked` and `before` are the
    arguments checked() built for this round's fetch line and for the one of the round before
    (None in round 1).  Returns (arguments, what it is in words) for the whole file when this
    round asks for a range further on in the file the round before asked a range of and the
    manifest gives that file fewer than `limit` bytes; None when the rule does not apply."""
    a, b = ranged(asked), ranged(before)
    if not a or not b or a[0] != b[0] or a[1] <= b[1]:
        return None
    e = manifest.get(fetch_tool.fold(a[0])) if manifest else None
    if not e or e[0] != 'f' or e[1] >= limit:
        return None
    return ['--', a[0]], '%s, whole (read in a row: 0x%x after 0x%x; its %d bytes are under the %d MiB of the rule)' % (
        a[0], a[1], b[1], e[1], limit >> 20)


def load_manifest(game):
    """fetch.py's manifest for the game directory `game`, or None if there is none."""
    try:
        store = fetch_tool.Store(game, 0, fetch_tool.MIB, 0)
        return store.manifest if store.load_manifest() else None
    except fetch_tool.Fail as f:
        raise Refused('the manifest cannot be used: %s' % f)


def field(report, name):
    for line in report:
        if line.startswith('  ' + name):
            return line[len(name) + 2:].strip()
    return ''


def main(argv):
    game_args = []
    if '--' in argv:
        i = argv.index('--')
        argv, game_args = argv[:i], argv[i + 1:]
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--rounds', type=int, default=40)
    ap.add_argument('--floor-gb', type=float, default=30.0)
    ap.add_argument('--gate3', default=os.path.join(REPO, 'build', 'gate3', 'gate3'))
    ap.add_argument('--source', default=None, help='passed to fetch.py (default: the owner\'s install over ssh)')
    ap.add_argument('--whole-max-gb', default=None)
    ap.add_argument('--game', default=None)
    ap.add_argument('--small', default=None, help='passed to fetch.py')
    ap.add_argument('--block', default=None, help='passed to fetch.py')
    o = ap.parse_args(argv)
    if not os.access(o.gate3, os.X_OK):
        print('run_fetch: no %s (make gate3-build)' % o.gate3, file=sys.stderr)
        return 2
    out = os.environ.get('GATE3_RUN_OUT') or os.path.join(REPO, 'build', 'gate3', 'run')
    env = dict(os.environ)
    passed = []
    if o.game:
        env['GATE3_ROOT'] = os.path.abspath(o.game)
        passed += ['--game=' + os.path.abspath(o.game)]
    for name in ('source', 'whole_max_gb', 'small', 'block'):
        if getattr(o, name) is not None:
            passed += ['--%s=%s' % (name.replace('_', '-'), getattr(o, name))]
    log_path = out + '.log'
    rows, last_args, last_asked, status, ending = [], None, None, 0, ''
    report = []
    for rnd in range(1, o.rounds + 1):
        t0 = time.time()
        with open(out + '.fetch-round.out', 'wb') as fp:
            run = subprocess.run([o.gate3, 'run'] + (['--'] + game_args if game_args else []), cwd=REPO, env=env, stdout=fp, stderr=subprocess.STDOUT)
        report, text, bad = read_stop(log_path)
        took = time.time() - t0
        if text is None and not bad:
            ending = 'round %d: the run ended on something else (driver status %d, %.1f s)' % (rnd, run.returncode, took)
            break
        try:
            if bad:
                raise Refused(bad)
            manifest = load_manifest(o.game or os.path.join(REPO, 'game'))
            asked, args = checked(text, manifest)
            row = in_a_row(asked, last_asked, manifest)
            last_asked = asked                      # what the run asked for, before the rule
            if row:
                asked, args = row
        except Refused as r:
            ending = 'round %d: THE STOP\'S FETCH LINE WAS NOT OBEYED: %s. The line: %r' % (rnd, r, (text or '')[:300])
            status = 5
            break
        if asked == last_args:
            ending = 'round %d: the run asks again for what round %d fetched: %s' % (rnd, rnd - 1, args)
            status = 5
            break
        last_args = asked
        headline = report[0].replace(REPORT, '')
        why = 'run_fetch round %d: %s; asked %s; caller %s' % (rnd, headline, field(report, 'asked'), field(report, 'caller'))
        cmd = [sys.executable, FETCH, '--why', why, '--floor-gb', str(o.floor_gb)] + passed + asked
        f = subprocess.run(cmd, cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        got = [l.strip() for l in f.stdout.splitlines() if l.strip()]
        rows.append((rnd, '%.1f' % took, headline, field(report, 'file'), field(report, 'asked'), field(report, 'caller'), args, f.returncode,
                     got[-1] if got else ''))
        print('round %d (%.1f s): %s\n    %s\n    fetch.py %s\n    %s' % (rnd, took, headline, field(report, 'file'), args,
                                                                         '\n    '.join(got)), flush=True)
        if f.returncode == 3:
            ending, status = 'round %d: the fetch was refused by the disk floor' % rnd, 3
            break
        if f.returncode == 7:
            ending, status = 'round %d: the run needs a file larger than --whole-max-gb %s: %s' % (rnd, o.whole_max_gb, got[-1] if got else ''), 7
            break
        if f.returncode:
            ending, status = 'round %d: the fetch failed with status %d' % (rnd, f.returncode), 5
            break
    else:
        ending, status = 'the limit of %d rounds' % o.rounds, 4

    print('\nrun_fetch: %d fetches' % len(rows))
    print('round\tseconds\tstop\tfile\tasked\tcaller\tfetch.py arguments\tfetch status')
    for r in rows:
        print('\t'.join(str(x) for x in r[:8]))
    try:
        with open(os.path.join(os.path.dirname(out), 'run_fetch.tsv'), 'a') as fp:
            for r in rows:
                fp.write('\t'.join([time.strftime('%Y-%m-%d %H:%M:%S')] + [str(x) for x in r]) + '\n')
            fp.write('%s\tend\t%s\n' % (time.strftime('%Y-%m-%d %H:%M:%S'), ending))
    except OSError:
        pass
    print('\nrun_fetch: ended at %s' % ending)
    if report:
        print('the last run\'s report (%s):' % log_path)
        for line in report[:14]:
            print('  ' + line[:230])
    else:
        print('the last run left no fatal report in %s: `%s summary %s` says how it ended' % (log_path, o.gate3, log_path))
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
