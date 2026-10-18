"""Shared helpers for the trace_*.py scripts (unit trace-analysis, finding 30).

Written for the remote machine's system python3 (standard library only).  The scripts read a
Wine debug log of one traced run and print summaries.  Everything sensitive is removed HERE, on
the machine that holds the log, before anything is printed or written:

  * home directories and the user name become `~`; the host name becomes `<host>`;
  * command lines are reduced to switch names (values become `<redacted>`);
  * registry values are never printed, only key and value names (and then only with the user's
    SID and any long digit or hex run replaced);
  * debug strings go through scrub_text(), which drops anything that looks like an identifier.

The user name and host name are read from the environment at run time, so this file carries
neither.  Log line format (Wine 'WINEDEBUG=+timestamp,+pid,+tid'):

    <seconds>.<millis>:<pid hex 4>:<tid hex 4>:<class>:<channel>:<function> <message>
"""
import getpass
import os
import re
import socket
import sys

# <seconds>.<millis> : pid : tid : class : channel : function <rest>
PFX = re.compile(rb'^(\d+)\.(\d+):([0-9a-f]{4}):([0-9a-f]{4}):(\w+):(\w+):([^\s]*) ?(.*)$', re.S)

USER = getpass.getuser()
HOST = socket.gethostname().split('.')[0]
_SUBS = []
for _name, _rep in ((HOST, '<host>'), (USER, '<user>')):
    if _name and len(_name) >= 3:
        _SUBS.append((re.compile(re.escape(_name), re.I), _rep))

SID_RE = re.compile(r'S-1-\d+(?:-\d+)+')
LONGNUM_RE = re.compile(r'\b\d{6,}\b')
HEXRUN_RE = re.compile(r'\b[0-9A-Fa-f]{16,}\b')
EMAIL_RE = re.compile(r'[\w.+-]+@[\w-]+(?:\.[\w-]+)+')
GUID_RE = re.compile(r'\{?[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}\}?')
JWT_RE = re.compile(r'\b[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}\b')
BIGTOKEN_RE = re.compile(r'\b[A-Za-z0-9+/_=-]{32,}\b')
# user directories, in unix and windows spelling
HOME_RE = re.compile(r'(?i)(/home/|/users/|\\users\\|\\home\\)([^/\\"\s]+)')


_ESC_RE = re.compile(r'\\(.)', re.S)


def unescape_path(s):
    """Wine prints strings C-style: backslash and quote are escaped, so \\\\ is one backslash and
    \\" is a quote.  Newlines and tabs become spaces."""
    return _ESC_RE.sub(lambda m: ' ' if m.group(1) in 'nrt' else m.group(1), s)


def scrub_names(s):
    for rx, rep in _SUBS:
        s = rx.sub(rep, s)
    return s


def redact_path(p):
    """Make a path printable: undouble backslashes, hide the user directory."""
    p = unescape_path(p)

    def home(m):
        who = m.group(2)
        # the wine prefix's own account is the same on every Proton machine
        if who.lower() in ('steamuser', 'public', 'default', 'all users'):
            return m.group(0)
        return m.group(1) + '~'
    p = HOME_RE.sub(home, p)
    p = scrub_names(p)
    p = SID_RE.sub('S-1-<sid>', p)
    return p


def scrub_raw(s, maxlen=160):
    """scrub_text() of a string as the log prints it (still C-escaped)."""
    return scrub_text(unescape_path(s), maxlen)


def scrub_text(s, maxlen=160):
    """Free text, already unescaped (debug strings, key names): remove anything that looks like an
    identifier."""
    s = scrub_names(s)
    s = EMAIL_RE.sub('<email>', s)
    s = SID_RE.sub('S-1-<sid>', s)
    s = GUID_RE.sub('<guid>', s)
    s = JWT_RE.sub('<token>', s)
    s = HEXRUN_RE.sub('<hex>', s)
    s = BIGTOKEN_RE.sub('<token>', s)
    s = LONGNUM_RE.sub('<num>', s)
    s = HOME_RE.sub(lambda m: m.group(1) + '~', s)
    return s[:maxlen]


def scrub_light(s, maxlen=200):
    """For a driver's own log (DXVK): hide names, user directories, e-mail, SIDs, GUIDs and very long
    hex or digit runs, but keep extension names and sizes readable."""
    s = scrub_names(s)
    s = EMAIL_RE.sub('<email>', s)
    s = SID_RE.sub('S-1-<sid>', s)
    s = GUID_RE.sub('<guid>', s)
    s = HOME_RE.sub(lambda m: m.group(1) + '~', s)
    s = re.sub(r'\b[0-9A-Fa-f]{20,}\b', '<hex>', s)
    s = re.sub(r'\b\d{13,}\b', '<num>', s)
    return s[:maxlen]


def cmdline_names(cmd):
    """Reduce a command line (still escaped as the log prints it) to switch names.  The program
    keeps only its file name.  A switch keeps its name and loses any value (`-x=v`, `--x=v`,
    `/x:v` give `<name>=<redacted>`).  Every other token becomes <arg>, <empty> or <uri>."""
    cmd = unescape_path(cmd).strip()
    out = []
    if cmd.startswith('"'):
        end = cmd.find('"', 1)
        prog, rest = cmd[1:end if end > 0 else len(cmd)], cmd[end + 1:] if end > 0 else ''
    else:
        m = re.search(r'(?i)\.exe\b', cmd)
        if m:
            prog, rest = cmd[:m.end()], cmd[m.end():]
        else:
            sp = cmd.split(None, 1)
            prog, rest = (sp[0] if sp else ''), (sp[1] if len(sp) > 1 else '')
    out.append(os.path.basename(prog.replace('\\', '/')))
    for t in re.findall(r'"[^"]*"|\S+', rest):
        t0 = t.strip('"')
        if t0 == '':
            out.append('<empty>')
        elif '://' in t0 or t0.lower().startswith(('origin2:', 'link2ea:', 'eadm:')):
            out.append('<uri>')
        elif t0[:1] in '-+/' and len(t0) > 1 and not re.match(r'^[-+/][0-9.]+$', t0):
            m = re.match(r'^([-+/]{1,2}[A-Za-z0-9_.@?-]+)([=:](.*))?$', t0)
            if m:
                out.append(m.group(1) + ('=<redacted>' if m.group(2) else ''))
            else:
                out.append('<switch?>')
        else:
            out.append('<arg>')
    return ' '.join(out)


def open_log(path):
    return open(path, 'rb', buffering=1 << 20)


def records(path, limit_bytes=None):
    """Yield (lineno, ts_seconds, pid, tid, cls, ch, func, msg_bytes) for every log line that
    matches; (lineno, None, ...) is not produced: unmatched lines (continuations) are yielded as
    (lineno, None, None, None, None, None, None, raw_bytes)."""
    n = 0
    read = 0
    with open_log(path) as f:
        for raw in f:
            n += 1
            read += len(raw)
            if limit_bytes is not None and read > limit_bytes:
                break
            m = PFX.match(raw.rstrip(b'\r\n'))
            if not m:
                yield n, None, None, None, None, None, None, raw
                continue
            ts = int(m.group(1)) + int(m.group(2)) / 1000.0
            yield (n, ts, int(m.group(3), 16), int(m.group(4), 16),
                   m.group(5).decode(), m.group(6).decode(), m.group(7).decode(), m.group(8))


def fmt_ts(t, t0):
    return '%.3f' % (t - t0)


def die(msg):
    sys.stderr.write(msg + '\n')
    sys.exit(2)
