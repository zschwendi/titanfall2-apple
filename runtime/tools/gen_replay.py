#!/usr/bin/env python3
"""gen_replay: the structure tables of the Direct3D replay player (runtime/tools/d3dreplay).

    gen_replay.py --out build/gen [HEADER ...]
    gen_replay.py --selftest

Reads the layout headers (default: rt/win/base.h, dxgi.h, d3d11.h, d3d11view.h under
runtime/include) and writes <out>/replay_layouts.h: for every structure they define, its
leaves as the recording names them (`BufferDesc.RefreshRate.Numerator`, `RenderTarget[3].SrcBlend`,
`Texture2D.MipLevels`), each with offsetof, sizeof and whether it is a float, all three
computed by the compiler from the same headers the device is built with.  This file only
finds the NAMES: it reads `struct X { ... };` and `typedef struct { ... } X;` declarations
made of the forms those headers use (fixed-width scalars, pointers, arrays, nested and
anonymous structures and unions, several declarators per line).  A structure it cannot
read is left out and named on standard error; the player then reports a script that needs it.
"""
import sys, re, os

DEFAULT = ['rt/win/base.h', 'rt/win/dxgi.h', 'rt/win/d3d11.h', 'rt/win/d3d11view.h']
ALIAS = {'D3D11_RECT': 'RECT'}          # build/gen/com_types.h: typedef RECT D3D11_RECT
MAX_ARRAY = 4096
TOKEN = re.compile(r'[A-Za-z_]\w*|\d+|[{}\[\];*,()=]')


def strip(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    text = re.sub(r'//[^\n]*', ' ', text)
    text = re.sub(r'^\s*#.*$', ' ', text, flags=re.M)
    return re.sub(r'_Static_assert\s*\((?:[^()]|\([^()]*\)|\((?:[^()]|\([^()]*\))*\))*\)\s*;', ' ', text)


class Parser:
    def __init__(self, text):
        self.t = TOKEN.findall(strip(text))
        self.i = 0
        self.structs = {}               # name -> members

    def body(self):
        """After `{`: members up to the matching `}`."""
        members = []
        while self.t[self.i] != '}':
            members.extend(self.member())
        self.i += 1
        return members

    def member(self):
        t = self.t
        while t[self.i] == 'const':
            self.i += 1
        if t[self.i] in ('struct', 'union'):
            self.i += 1
            name = None
            if t[self.i] != '{':
                name = t[self.i]
                self.i += 1
            if t[self.i] == '{':
                self.i += 1
                typ = ('inline', self.body())
                if name:
                    self.structs[name] = typ[1]
            else:
                typ = ('named', name)
        else:
            typ = ('named', t[self.i])
            self.i += 1
        out = []
        while True:
            ptr = 0
            while t[self.i] in ('*', 'const'):
                ptr += t[self.i] == '*'
                self.i += 1
            name = None
            if t[self.i] not in (';', ','):
                name = t[self.i]
                self.i += 1
            dims = []
            while t[self.i] == '[':
                dims.append(int(t[self.i + 1]))
                self.i += 3
            out.append((name, typ, ptr, dims))
            if t[self.i] == ',':
                self.i += 1
                continue
            if t[self.i] != ';':
                raise ValueError('expected ; near %r' % ' '.join(t[self.i - 4:self.i + 3]))
            self.i += 1
            return out

    def run(self, warn):
        t = self.t
        while self.i < len(t):
            start = self.i
            try:
                if t[self.i] == 'struct' and self.i + 2 < len(t) and t[self.i + 2] == '{':
                    name = t[self.i + 1]
                    self.i += 3
                    self.structs[name] = self.body()
                elif t[self.i] == 'typedef' and t[self.i + 1] == 'struct' and t[self.i + 2] == '{':
                    self.i += 3
                    members = self.body()
                    self.structs[t[self.i]] = members
                else:
                    self.i += 1
            except (ValueError, IndexError) as e:
                warn('gen_replay: cannot read the structure near %r: %s' % (' '.join(t[start:start + 4]), e))
                self.i = start + 1


def flatten(structs, name, prefix, out, depth=0):
    """Leaf paths of structure `name` (a list of member tuples when inline)."""
    members = structs[name] if isinstance(name, str) else name
    if depth > 8:
        raise ValueError('nesting')
    for mname, typ, ptr, dims in members:
        inner = typ[1] if typ[0] == 'inline' else (typ[1] if typ[1] in structs and not ptr else None)
        if mname is None:               # an anonymous structure or union: its members are ours
            if inner is not None:
                flatten(structs, inner, prefix, out, depth + 1)
            continue
        count = 1
        for d in dims:
            count *= d
        if count > MAX_ARRAY or typ[1] in ('WCHAR', 'char', 'CHAR') and dims:
            continue                    # a string or a table too long to name element by element
        idx = [''] if not dims else ['[%d]' % n for n in range(count)] if len(dims) == 1 else None
        if idx is None:
            continue
        for ix in idx:
            if inner is not None:
                flatten(structs, inner, prefix + mname + ix + '.', out, depth + 1)
            else:
                out.append(prefix + mname + ix)


def generate(paths, warn):
    structs = {}
    for p in paths:
        ps = Parser(open(p).read())
        ps.run(warn)
        structs.update(ps.structs)
    for a, b in ALIAS.items():
        if b in structs:
            structs[a] = structs[b]
    lines = ['/* Generated by runtime/tools/gen_replay.py from the layout headers.  Do not edit. */', '']
    rows = []
    for name in sorted(structs):
        if not re.match(r'^(D3D11_|DXGI_|RECT$|D3D_)', name):
            continue
        leaves = []
        try:
            flatten(structs, name, '', leaves)
        except (ValueError, KeyError) as e:
            warn('gen_replay: %s left out: %s' % (name, e))
            continue
        if not leaves:
            continue
        lines.append('static const RpLeaf rp_leaves_%s[] = {' % name)
        for path in leaves:
            lines.append('    RP_LEAF(%s, %s),' % (name, path))
        lines.append('};')
        rows.append('    { "%s", sizeof(%s), rp_leaves_%s, %d },' % (name, name, name, len(leaves)))
    lines += ['', 'static const RpStruct rp_structs[] = {'] + rows + ['};', '']
    return '\n'.join(lines), structs


def selftest():
    text = '''
    /* a comment */
    typedef struct { LONG left, top, right, bottom; } RECT;
    struct DXGI_A { UINT N; UINT D; };
    typedef struct DXGI_A DXGI_A;
    _Static_assert(offsetof(struct DXGI_A, D) == 4, "");
    struct D3D11_B {
        UINT Format;                 /* DXGI_FORMAT */
        struct DXGI_A Rate;
        FLOAT Color[2];
        const void *pMem;
        union { struct DXGI_A One; struct { UINT x; } Two; };
        union { UINT First; UINT Offset; };
        struct { DWORD Low; LONG High; } Luid;
        WCHAR Name[32];
        struct DXGI_A Arr[2];
        RECT r;
    };
    '''
    ps = Parser(text)
    ps.run(lambda m: sys.stderr.write(m + '\n'))
    got = []
    flatten(ps.structs, 'D3D11_B', '', got)
    want = ['Format', 'Rate.N', 'Rate.D', 'Color[0]', 'Color[1]', 'pMem', 'One.N', 'One.D', 'Two.x', 'First', 'Offset',
            'Luid.Low', 'Luid.High', 'Arr[0].N', 'Arr[0].D', 'Arr[1].N', 'Arr[1].D', 'r.left', 'r.top', 'r.right', 'r.bottom']
    if got != want:
        print('gen_replay selftest: FAILED\n  got  %r\n  want %r' % (got, want))
        return 1
    print('gen_replay selftest: ok')
    return 0


def main(argv):
    if '--selftest' in argv:
        return selftest()
    out = argv[argv.index('--out') + 1]
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'include')
    paths = [a for a in argv[1:] if a.endswith('.h')] or [os.path.join(root, p) for p in DEFAULT]
    text, structs = generate(paths, lambda m: sys.stderr.write(m + '\n'))
    os.makedirs(out, exist_ok=True)
    dst = os.path.join(out, 'replay_layouts.h')
    if not os.path.exists(dst) or open(dst).read() != text:
        with open(dst, 'w') as f:
            f.write(text)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
