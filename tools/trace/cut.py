#!/usr/bin/env python3
"""Run tools/trace/relay.py on the machine that holds the reference trace, read-only.

    tools/trace/cut.py CAPTURE SUBCOMMAND [options]         e.g.
    tools/trace/cut.py mission1 calls --dll user32,gdi32,imm32 --t 2.5:4 --commit-safe
    tools/trace/cut.py mission1 census > build/trace/mission1.census.tsv
    tools/trace/cut.py mission1 multi -- census -- threads -- files -- phases
    tools/trace/cut.py test1 calls --from all --fn 'Heap|Virtual' --t 20:21
    tools/trace/cut.py mission1 index          (re)build the cached index
    tools/trace/cut.py mission1 text FILE      print a small text file beside the log (the DXVK logs)

The script travels on standard input (`ssh HOST nice python3 - ... < relay.py`); nothing is
installed, written or started on that machine, and the log never leaves it: what comes back is
what relay.py prints.  The first use of a capture makes one pass for its index (the game's
process id, its time zero, per-second offsets, the module loads), kept in build/trace/ on this
side (git-ignored); after that a `calls` or `census` with --t starts with a seek.

One command at a time: a lock file in build/trace/ makes a second cut.py wait for the first.
Anything meant for a committed file gets --commit-safe (see relay.py's text).
"""
import fcntl
import json
import os
import shlex
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
CACHE = os.path.join(ROOT, 'build', 'trace')
def ref_setting(key, env):
    """The reference PC's setting: $env, then $TF_REF_<KEY>, then the line KEY=... of .ref-host at the
    checkout's root (git-ignored: each owner names their own machine there).  '' if none."""
    for name in (env, 'TF_REF_' + key.upper()):
        if name and os.environ.get(name):
            return os.environ[name]
    try:
        with open(os.path.join(ROOT, '.ref-host'), encoding='utf-8') as f:
            for line in f:
                k, _, v = line.strip().partition('=')
                if k.strip() == key and v.strip():
                    return v.strip()
    except OSError:
        pass
    return ''


HOST = ref_setting('host', 'TRACE_HOST')
SSH = ['ssh', '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=10', HOST]

# name -> (directory under ~/tf2-trace, what it is)
CAPTURES = {
    'mission1': ('a3-mission1', 'the capture: start, main menu, a campaign mission, play, quit (10 October)'),
    'start2': ('a3-snoop2', 'a second start, 62 s of the game\'s process, same caller and exclude lists'),
    'test1': ('a3-test1', 'the first test: every caller of every process, 75 s, stopped before the title screen'),
}
TEXT_FILES = ('Titanfall2_d3d11.log', 'Titanfall2_dxgi.log', 'from.list', 'exclude.list', 'hist.txt')


def remote_log(capture):
    return 'tf2-trace/%s/steam-1237970.log' % CAPTURES[capture][0]


def run_remote(args, stdin_path=None):
    """Run one command there; relay.py on standard input when stdin_path is given."""
    cmd = SSH + [' '.join(shlex.quote(a) for a in args)]
    with open(stdin_path or os.devnull, 'rb') as f:
        return subprocess.run(cmd, stdin=f, stdout=subprocess.PIPE)


def relay(args):
    return run_remote(['nice', 'python3', '-'] + args, os.path.join(HERE, 'relay.py'))


def index_path(capture):
    return os.path.join(CACHE, capture + '.index.json')


def get_index(capture, rebuild=False):
    path = index_path(capture)
    if not rebuild and os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    sys.stderr.write('cut.py: one pass over %s for its index...\n' % capture)
    p = relay(['index', remote_log(capture)])
    if p.returncode != 0:
        sys.exit('cut.py: the index pass failed (exit %d)' % p.returncode)
    index = json.loads(p.stdout.decode())
    if not index.get('pid'):
        sys.exit('cut.py: no process of that log loaded Titanfall2.exe first')
    with open(path, 'w') as f:
        json.dump(index, f)
    return index


def main(argv):
    if len(argv) < 2 or argv[0] in ('-h', '--help') or argv[0] not in CAPTURES:
        sys.stdout.write(__doc__ + '\ncaptures:\n' + ''.join('  %-9s %s\n' % (k, v[1]) for k, v in CAPTURES.items()))
        return 0 if argv[:1] in ([], ['-h'], ['--help']) else 2
    capture, cmd, rest = argv[0], argv[1], argv[2:]
    os.makedirs(CACHE, exist_ok=True)
    lock = open(os.path.join(CACHE, '.lock'), 'w')
    fcntl.flock(lock, fcntl.LOCK_EX)
    if cmd == 'text':
        if len(rest) != 1 or rest[0] not in TEXT_FILES:
            sys.exit('cut.py text: one of ' + ', '.join(TEXT_FILES))
        p = run_remote(['cat', 'tf2-trace/%s/%s' % (CAPTURES[capture][0], rest[0])])
        sys.stdout.buffer.write(p.stdout)
        return p.returncode
    index = get_index(capture, rebuild=(cmd == 'index'))
    if cmd == 'index':
        small = dict((k, v) for k, v in index.items() if k not in ('offsets', 'mods'))
        small['processes'] = len(index['processes'])
        print(json.dumps(small, indent=1))
        print('game process: pid %s, %.3f s from its first line to its last' % (
            index['pid'], index['t_end'] - index['t0']))
        return 0
    sys.path.insert(0, HERE)
    import relay as relay_mod
    args = [cmd, remote_log(capture), '--pid', index['pid']]
    if cmd != 'multi':
        args += ['--ctx', relay_mod.make_ctx(index)]
    p = relay(args + rest)
    sys.stdout.buffer.write(p.stdout)
    return p.returncode


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
