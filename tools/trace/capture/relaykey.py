#!/usr/bin/env python3
"""Set, or put back, the relay filter values in the game prefix's user.reg.

  relaykey.py set <from-list-file> [<exclude-list-file>] [<Name>=<list-file> ...]
  relaykey.py restore
  relaykey.py show

`set` writes RelayFromInclude (and RelayExclude when a file is given, and any further value
named as SnoopInclude=<file>) into the section [Software\\Wine\\Debug], after copying
user.reg to ~/tf2-trace/reg-backup/ and recording the section's original Relay* and Snoop*
lines in ~/tf2-trace/relaykey.state.json.  `restore` removes every Relay* and Snoop* line
from the section and puts the recorded ones back; the rest of the file is left as the run
left it.  Run both only while nothing of the prefix is running: wineserver
reads the file when it starts and may rewrite it when it exits.
"""
import json, os, re, shutil, sys, time

REG = '/mnt/1tb_nvme/SteamLibrary/steamapps/compatdata/1237970/pfx/user.reg'
T = os.path.expanduser('~/tf2-trace')
STATE = T + '/relaykey.state.json'
HEAD = b'[Software\\\\Wine\\\\Debug]'


def die(msg):
    print('relaykey: ' + msg)
    sys.exit(1)


def read():
    with open(REG, 'rb') as f:
        return f.read().split(b'\n')


def section(lines):
    heads = [i for i, l in enumerate(lines) if l.startswith(HEAD)]
    if len(heads) != 1:
        die('expected one [Software\\\\Wine\\\\Debug] section, found %d' % len(heads))
    i = heads[0]
    j = i + 1
    while j < len(lines) and lines[j].strip() != b'':
        j += 1
    return i, j


def write(lines):
    tmp = REG + '.tf2trace.tmp'
    with open(tmp, 'wb') as f:
        f.write(b'\n'.join(lines))
    shutil.copymode(REG, tmp)
    os.replace(tmp, REG)


def names(path):
    out = []
    for l in open(path):
        l = l.split('#')[0].strip()
        if l:
            if not re.match(r'^[A-Za-z0-9_.*-]+$', l):
                die('unexpected character in list entry %r' % l)
            out.append(l)
    return ';'.join(out)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'show'
    lines = read()
    i, j = section(lines)
    ours = (b'"Relay', b'"Snoop')
    relay = [l for l in lines[i + 1:j] if l.startswith(ours)]
    if cmd == 'show':
        for l in relay:
            print(l.decode('ascii', 'replace')[:400])
        print('state file present: %s' % os.path.exists(STATE))
    elif cmd == 'set':
        if os.path.exists(STATE):
            die('a previous set was never restored (%s exists)' % STATE)
        frm = names(sys.argv[2])
        rest = sys.argv[3:]
        exc = names(rest.pop(0)) if rest and '=' not in rest[0] and rest[0] else None
        extra = []
        for kv in rest:
            if not kv:
                continue
            k, _, path = kv.partition('=')
            if k not in ('RelayInclude', 'SnoopInclude', 'SnoopExclude', 'SnoopFromInclude'):
                die('unexpected value name %r' % k)
            extra.append((k.encode('ascii'), names(path)))
        os.makedirs(T + '/reg-backup', exist_ok=True)
        backup = T + '/reg-backup/user.reg.' + time.strftime('%Y%m%d-%H%M%S')
        shutil.copy2(REG, backup)
        with open(STATE, 'w') as f:
            json.dump({'orig': [l.decode('latin-1') for l in relay], 'backup': backup}, f)
        # RelayFromExclude must go: where it exists Wine ignores RelayFromInclude (measured, a3-test1)
        drop = [b'"RelayFromInclude"=', b'"RelayFromExclude"='] + ([b'"RelayExclude"='] if exc is not None else [])
        drop += [b'"' + k + b'"=' for k, _ in extra]
        body = [l for l in lines[i + 1:j] if not any(l.startswith(d) for d in drop)]
        body.append(b'"RelayFromInclude"="' + frm.encode('ascii') + b'"')
        if exc is not None:
            body.append(b'"RelayExclude"="' + exc.encode('ascii') + b'"')
        for k, v in extra:
            body.append(b'"' + k + b'"="' + v.encode('ascii') + b'"')
        write(lines[:i + 1] + body + lines[j:])
        print('relaykey: set; backup %s' % backup)
    elif cmd == 'restore':
        if not os.path.exists(STATE):
            die('nothing to restore (no state file)')
        orig = [l.encode('latin-1') for l in json.load(open(STATE))['orig']]
        body = [l for l in lines[i + 1:j] if not l.startswith(ours)] + orig
        write(lines[:i + 1] + body + lines[j:])
        os.remove(STATE)
        print('relaykey: restored %d original line(s)' % len(orig))
    else:
        die('unknown command ' + cmd)


main()
