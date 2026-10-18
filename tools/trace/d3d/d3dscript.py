#!/usr/bin/env python3
"""d3dscript: turn the recording's call lines into a replay script for runtime/tools/d3dreplay.

    d3dscript.py selftest
    d3dscript.py convert [--state FILE] [-o OUT[.gz]] [--foreign 12,13,14] < lines
    d3dscript.py names < lines            every symbolic constant the lines use and its value

Input: the output of `d3dtrace.py clean --full` (tools/trace/d3d/remote.sh ... clean --full), so
no shader text is ever here: a shader arrives as `shader(vs_5_0, 12 lines)` and leaves as a
size.  Output: one line per call,

    <number> <mark> <Interface::Method> <result> <this> <argument> ...

mark   `.` the game's call; `F` a call that is not the game's (finding 40, section 7): kept,
       and skipped by the player unless asked; `U` a method the interface descriptions under
       runtime/com/desc do not have (the player counts it); `W` a write into mapped memory
       (the recorder's memcpy line): `<number> W <object> <subresource> <offset> <bytes>`.
result `-` (none) or `=<number>`.
argument, by the parameter's C type in the description and the shape of the recorded value:
    i<int>  f<float>         by value (enumerators and flags by value)
    n                        a NULL pointer
    g<32 hex digits>         REFIID: pointer to that GUID (bytes in memory order)
    o<N>                     object N (numbers are the converter's: N is given when a call first
                             returns a pointer, and again when it returns one after the
                             recorded count of that pointer fell to 0)
    O[N,0,...]               pointer to an array of object pointers, 0 for NULL
    s<Type>{path=v,...}      pointer to one structure, leaf by leaf (`A.B[2].C`); a leaf value is
                             a number, b<size> (pointer to data) or "text" (%-escaped string)
    S<Type>[{...}{...}]      pointer to an array of structures
    I<width>[v,...] F[v,...] pointer to integers of <width> bytes, to floats
    b<size>  B<size>         pointer to <size> bytes of data (B: shader bytecode)
  what the device writes (`>`), with the recorded value for the player to hold ours against:
    >o<N>  >o0  >o?          an object pointer: object N, NULL, or not written
    >O[N,...]                an array of object pointers
    >s<Type>{...} >s<Type>?  one structure (? = not written)      >S<Type>[{...}...]  an array
    >I<width>[v,...]  >I<width>?   >F[v,...]                       integers or floats
    >d<size>                 <size> bytes of data (GetData)

Parameter types come from runtime/com/desc/*.idef, constants from the `#define`s of
runtime/include/rt/win/*.h and, for names those do not have, from the MinGW-w64 headers the
descriptions were made from (third_party/dxmt/include/native, when present) and the short
table EXTRA below, each with its source.  A name with no value stops the conversion.
"""
import sys, re, os, json, gzip, time, glob

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
sys.path.insert(0, HERE)
from d3dtrace import HEAD, QSTR, NAME_EQ, SCALAR, BASE     # noqa: E402

# Values our headers and MinGW's give no single `NAME = number` line for.  Microsoft's
# documentation of each (winerror.h and dxgi.h values; d3d11.h for the sample mask).
EXTRA = {
    'S_OK': 0, 'S_FALSE': 1, 'E_FAIL': 0x80004005, 'E_INVALIDARG': 0x80070057, 'E_OUTOFMEMORY': 0x8007000E,
    'E_NOINTERFACE': 0x80004002, 'E_NOTIMPL': 0x80004001, 'TRUE': 1, 'FALSE': 0,
    'DXGI_STATUS_OCCLUDED': 0x087A0001, 'DXGI_ERROR_INVALID_CALL': 0x887A0001, 'DXGI_ERROR_NOT_FOUND': 0x887A0002,
    'DXGI_ERROR_MORE_DATA': 0x887A0003, 'DXGI_ERROR_UNSUPPORTED': 0x887A0004,
    'DXGI_ERROR_DEVICE_REMOVED': 0x887A0005, 'DXGI_ERROR_WAS_STILL_DRAWING': 0x887A000A,
    'DXGI_ERROR_NOT_CURRENTLY_AVAILABLE': 0x887A0022,
    'D3D11_DEFAULT_SAMPLE_MASK': 0xFFFFFFFF, 'D3D11_APPEND_ALIGNED_ELEMENT': 0xFFFFFFFF,
    'DXGI_MWA_NO_WINDOW_CHANGES': 1, 'DXGI_MWA_NO_ALT_ENTER': 2, 'DXGI_MWA_NO_PRINT_SCREEN': 4,
}

# Calls of the recording that are not the game's (finding 40, section 7), by trace.
FOREIGN = {'mission1': {12: 'GetImmediateContext', 13: 'QueryInterface', 14: 'Release'}}

D3D11CREATEDEVICE = ('HRESULT', [
    ('IDXGIAdapter *', 'pAdapter'), ('D3D_DRIVER_TYPE', 'DriverType'), ('HMODULE', 'Software'), ('UINT', 'Flags'),
    ('const D3D_FEATURE_LEVEL *', 'pFeatureLevels'), ('UINT', 'FeatureLevels'), ('UINT', 'SDKVersion'),
    ('ID3D11Device **', 'ppDevice'), ('D3D_FEATURE_LEVEL *', 'pFeatureLevel'),
    ('ID3D11DeviceContext **', 'ppImmediateContext')])

WIDTH = {'UINT8': 1, 'BYTE': 1, 'UINT16': 2, 'WORD': 2, 'USHORT': 2, 'UINT64': 8, 'SIZE_T': 8, 'LARGE_INTEGER': 8,
         'ULONG_PTR': 8, 'UINT_PTR': 8, 'LONG_PTR': 8, 'u8': 1, 'u16': 2, 'u64': 8, 'i64': 8}


class Ref:
    """`&x` in a dump: a pointer to x."""
    __slots__ = ('v',)

    def __init__(self, v):
        self.v = v


def parse_value(s, i):
    """As d3dtrace.parse_value, but `&x` stays a Ref and a structure is a list of (name, value)."""
    c = s[i]
    if c == '&':
        v, i = parse_value(s, i + 1)
        return Ref(v), i
    if c == '{':
        i += 1
        items, named = [], False
        if s[i] == '}':
            return [], i + 1
        while True:
            m = NAME_EQ.match(s, i)
            if m:
                named = True
                v, i = parse_value(s, m.end())
                items.append((m.group(1), v))
            else:
                v, i = parse_value(s, i)
                items.append(v)
            if s[i] == ',':
                i += 2
                continue
            if s[i] != '}':
                raise ValueError('expected } at %d' % i)
            return (dict(items) if named else items), i + 1
    if c == '"' or (c == 'L' and s[i + 1:i + 2] == '"'):
        m = QSTR.match(s, i + (c == 'L'))
        if not m:
            raise ValueError('open string at %d' % i)
        return s[i:m.end()], m.end()
    m = SCALAR.match(s, i)
    if not m:
        raise ValueError('no value at %d' % i)
    return m.group(0).strip(), m.end()


def parse_call(line):
    m = HEAD.match(line)
    if not m:
        raise ValueError('not a call line')
    no, func, i = int(m.group(1)), m.group(2), m.end()
    args = []
    if line[i] != ')':
        while True:
            m = NAME_EQ.match(line, i)
            if not m:
                raise ValueError('no argument name at %d' % i)
            v, i = parse_value(line, m.end())
            args.append((m.group(1), v))
            if line[i] == ',':
                i += 2
                continue
            if line[i] != ')':
                raise ValueError('expected ) at %d' % i)
            break
    rest = line[i + 1:]
    ret = rest[3:].split(' // ')[0].strip() if rest.startswith(' = ') else None
    return no, func, args, ret


# ------------------------------------------------------------------ descriptions and constants

PROTO = re.compile(r'^\s*(\d+)\s+(\w+)\s+(.+?)\b(\w+)\((.*)\)\s*(?:#.*)?$')


def load_descriptions(root=ROOT):
    """{Interface: {Method: (return type, [(type, name), ...])}} and the IIDs by interface name."""
    methods, iids = {}, {}
    for path in sorted(glob.glob(os.path.join(root, 'runtime', 'com', 'desc', '*.idef'))):
        name = None
        for line in open(path):
            w = line.split()
            if not w or w[0].startswith('#'):
                continue
            if w[0] == 'interface':
                name = w[1]
                methods[name] = {}
            elif w[0] == 'iid':
                iids[name] = w[1]
            elif w[0] == 'inherits':
                iids.setdefault(w[1], w[2])
            elif w[0].isdigit():
                m = PROTO.match(line)
                if not m:
                    raise ValueError('%s: cannot read %r' % (path, line))
                params = []
                for p in (m.group(5).split(',') if m.group(5).strip() not in ('', 'void') else []):
                    pm = re.match(r'^\s*(.*?)(\w+)\s*$', p)
                    params.append((pm.group(1).strip(), pm.group(2)))
                methods[name][m.group(4)] = (m.group(3).strip(), params)
    return methods, iids


def load_type_classes(root=ROOT):
    """Type name -> class word of the two types.def files (u32, u64, void, struct, ...)."""
    cls = {}
    for rel in ('runtime/tools/types.def', 'runtime/com/types.def'):
        for line in open(os.path.join(root, rel)):
            w = line.split('#')[0].split()
            if len(w) >= 2 and not w[0].startswith('@'):
                cls[w[0]] = w[1]
            if '#= struct' in line or '#= RECT' in line:
                cls[line.split()[0]] = 'struct'
            if '#= interface' in line:
                cls[line.split()[0]] = 'interface'
    return cls


DEFINE = re.compile(r'^\s*#\s*define\s+([A-Z][A-Za-z0-9_]*)\s+\(?\(?(?:\w+\))?\s*(-?0x[0-9a-fA-F]+|-?\d+)[uUlL]*\)?\s*(?:/[/*].*)?$')
ENUMERATOR = re.compile(r'^\s*([A-Z][A-Za-z0-9_]*)\s*=\s*(-?0x[0-9a-fA-F]+|-?\d+)[uUlL]*\s*,?\s*(?:/[/*].*)?$')


def load_constants(root=ROOT):
    """(values, sources): ours first, then MinGW's headers for what ours lack, then EXTRA."""
    val, src = {}, {}
    for path in sorted(glob.glob(os.path.join(root, 'runtime', 'include', 'rt', 'win', '*.h'))):
        for line in open(path, errors='replace'):
            m = DEFINE.match(line)
            if m and m.group(1) not in val:
                val[m.group(1)] = int(m.group(2), 0)
                src[m.group(1)] = 'rt/win/' + os.path.basename(path)
    for path in sorted(glob.glob(os.path.join(root, 'third_party', 'dxmt', 'include', 'native', 'directx', '*.h'))):
        if os.path.basename(path) not in ('d3d11.h', 'dxgi.h', 'd3dcommon.h', 'dxgiformat.h', 'dxgitype.h', 'dxgicommon.h'):
            continue
        for line in open(path, errors='replace'):
            m = ENUMERATOR.match(line) or DEFINE.match(line)
            if m and m.group(1) not in val:
                val[m.group(1)] = int(m.group(2), 0)
                src[m.group(1)] = 'MinGW ' + os.path.basename(path)
    for k, v in EXTRA.items():
        if k not in val:
            val[k] = v
            src[k] = 'EXTRA'
    return val, src


class Unknown(Exception):
    pass


class Converter:
    def __init__(self, root=ROOT, foreign=None):
        self.methods, self.iids = load_descriptions(root)
        self.cls = load_type_classes(root)
        self.const, self.const_src = load_constants(root)
        self.foreign = foreign or {}
        self.objs = {}              # recorded address -> [number, alive]
        self.next_obj = 1
        self.maps = {}              # (object number, subresource) -> recorded pData
        self.used = {}              # constant -> uses
        self.stats = {'calls': 0, 'writes': 0, 'unplaced_writes': 0, 'undescribed': 0, 'foreign': 0, 'objects': 0}
        self.undescribed = {}

    # -- state across ranges
    def save(self, path):
        with open(path, 'w') as f:
            json.dump({'objs': self.objs, 'next_obj': self.next_obj, 'stats': self.stats,
                       'maps': [[k[0], k[1], v] for k, v in self.maps.items()], 'undescribed': self.undescribed}, f)

    def load(self, path):
        with open(path) as f:
            d = json.load(f)
        self.objs, self.next_obj, self.stats = d['objs'], d['next_obj'], d['stats']
        self.maps = {(a, b): v for a, b, v in d['maps']}
        self.undescribed = d['undescribed']

    # -- values
    def number(self, text, want_float=False):
        """An integer (or a float's text) for a scalar as the dump writes it."""
        t = text.strip()
        if t == 'NULL':
            return 0
        try:
            return int(t, 0)
        except ValueError:
            pass
        if re.match(r'^-?(?:\d+\.?\d*(?:e[-+]?\d+)?|\.\d+|inf|nan)$', t, re.I):
            return t if want_float else int(float(t))
        v = 0
        for part in t.split('|'):
            p = part.strip()
            if p in self.const:
                self.used[p] = self.used.get(p, 0) + 1
                v |= self.const[p]
            else:
                try:
                    v |= int(p, 0)
                except ValueError:
                    raise Unknown(p)
        return v

    def obj_in(self, text):
        if text == 'NULL':
            return 0
        e = self.objs.get(text)
        if e is None:               # never returned by any call: a number nothing is bound to
            e = self.objs[text] = [self.next_obj, False]
            self.next_obj += 1
        return e[0]

    def obj_out(self, text):
        if text == 'NULL':
            return 0
        e = self.objs.get(text)
        if e is None or not e[1]:
            e = self.objs[text] = [self.next_obj, True]
            self.next_obj += 1
            self.stats['objects'] += 1
        return e[0]

    def classify(self, ctype):
        """(base type, pointer depth, is the pointed-to thing const)."""
        t = ctype.replace('*', ' * ').split()
        depth = t.count('*')
        base = [w for w in t if w not in ('const', '*', 'struct')][0]
        if depth == 0:
            return base, 0, False
        return base, depth, 'const' in t      # the descriptions write `I const **p` for `I *const *p`

    def is_interface(self, base):
        return self.cls.get(base) == 'interface' or base in self.methods or re.match(r'^I(D3D|DXGI|Unknown)', base)

    def is_struct(self, base):
        return self.cls.get(base, '').startswith('struct') or base.endswith('_DESC') or base in ('RECT', 'D3D11_RECT')

    def width(self, base):
        return WIDTH.get(base, WIDTH.get(self.cls.get(base, ''), 4))

    def leaves(self, v, prefix, out):
        """Flatten a recorded structure into path=value leaves."""
        if isinstance(v, dict):
            for k, x in v.items():
                self.leaves(x, prefix + '.' + k if prefix else k, out)
        elif isinstance(v, list):
            for n, x in enumerate(v):
                self.leaves(x, '%s[%d]' % (prefix, n), out)
        elif isinstance(v, Ref):
            self.leaves(v.v, prefix, out)
        elif v.startswith('blob('):
            out.append('%s=b%s' % (prefix, v[5:-1]))
        elif v.startswith('L"'):
            pass                                        # a wide string the device wrote: never compared
        elif v.startswith('"'):
            out.append('%s="%s"' % (prefix, re.sub(r'[^A-Za-z0-9_.-]', lambda m: '%%%02x' % ord(m.group(0)),
                                                    v[1:-1])))
        elif v == '?':
            pass
        else:
            n = self.number(v, want_float=True)
            out.append('%s=%s' % (prefix, n))

    def struct(self, base, v):
        out = []
        self.leaves(v, '', out)
        return '{' + ','.join(out) + '}'

    def arg(self, ctype, v, args, method='', pname=''):
        base, depth, const = self.classify(ctype)
        # MinGW's dxgi.h leaves `const` off the description a creator reads (CreateSwapChain's desc)
        if depth == 1 and not const and method.startswith(('Create', 'Set')) and self.is_struct(base):
            const = True
        if depth == 0:
            if base in ('REFIID', 'REFGUID'):
                return 'g' + self.guid(v)
            if base in ('FLOAT', 'float'):
                return 'f%s' % self.number(v, want_float=True)
            return 'i%d' % self.number(v)
        if v == 'NULL':
            return 'n'
        inner = v.v if isinstance(v, Ref) else v
        if depth == 2:
            if const:
                items = inner if isinstance(inner, list) else [inner]
                return 'O[' + ','.join(str(self.obj_in(x)) for x in items) + ']'
            if inner == '?':
                return '>o?'
            if isinstance(inner, list):
                return '>O[' + ','.join(str(self.obj_out(x)) for x in inner) + ']'
            return '>o%d' % self.obj_out(inner)
        if self.is_interface(base):
            return 'o%d' % self.obj_in(inner)
        if base == 'void':
            if const:
                if inner.startswith('blob('):
                    return ('B' if 'Bytecode' in pname else 'b') + inner[5:-1]
                if inner.startswith(('shader(', 'text(')):
                    return 'B%d' % self.number(args.get('BytecodeLength', '4'))
                raise ValueError('data argument %r' % (inner,))
            return '>d%d' % self.number(args.get('DataSize', '0'))
        if self.is_struct(base):
            if isinstance(inner, list) and (not inner or isinstance(inner[0], (dict, Ref))):
                body = '[' + ''.join(self.struct(base, x) for x in inner) + ']'
                return ('S' if const else '>S') + base + body
            if inner == '?':
                return '>s%s?' % base
            return ('s' if const else '>s') + base + self.struct(base, inner)
        # scalars behind a pointer
        items = inner if isinstance(inner, list) else [inner]
        if base in ('FLOAT', 'float'):
            return ('F[' if const else '>F[') + ','.join(str(self.number(x, True)) for x in items) + ']'
        if items == ['?']:
            return '>I%d?' % self.width(base)
        return ('I%d[' if const else '>I%d[') % self.width(base) + ','.join(str(self.number(x)) for x in items) + ']'

    def guid(self, v):
        m = re.match(r'^uuid\(([0-9a-fA-F-]{36})\)$', v)
        text = m.group(1) if m else self.iids.get(v[4:] if v.startswith('IID_') else v)
        if not text:
            raise Unknown(v)
        h = text.replace('-', '')
        # memory order: the first three fields are little-endian
        return ''.join((h[6:8], h[4:6], h[2:4], h[0:2], h[10:12], h[8:10], h[14:16], h[12:14], h[16:])).lower()

    def result(self, ret, rtype):
        if ret is None or rtype == 'void':
            return '-'
        return '=%d' % (self.number(ret) & 0xFFFFFFFF)

    # -- one line
    def convert(self, line):
        no, func, args, ret = parse_call(line)
        self.stats['calls'] += 1
        if func == 'memcpy':
            return self.write(no, dict(args))
        if '::' in func:
            iface, method = func.split('::')
            iface = BASE.get(iface, iface)
            desc = self.methods.get(iface, {}).get(method)
            name = iface + '::' + method
        else:
            iface, method, name = None, func, func
            desc = D3D11CREATEDEVICE if func == 'D3D11CreateDevice' else None
        mark = '.'
        if no in self.foreign:
            if self.foreign[no] != method:
                raise ValueError('call %d is %s, not the %s that finding 40 names as not the game\'s' %
                                 (no, method, self.foreign[no]))
            mark = 'F'
            self.stats['foreign'] += 1
        named = dict(args)
        this = None
        if iface:
            this = named.get('this')
            args = [a for a in args if a[0] != 'this']
        if desc is None or len(desc[1]) != len(args):
            self.stats['undescribed'] += 1
            self.undescribed[name] = self.undescribed.get(name, 0) + 1
            if this:
                self.obj_in(this)
            if method == 'Release' and ret == '0' and this in self.objs:
                self.objs[this][1] = False
            return '%d U %s -' % (no, name)
        out = ['%d' % no, mark, name, self.result(ret, desc[0])]
        if iface:
            out.append('o%d' % self.obj_in(this))
        for (ctype, pname), (_, v) in zip(desc[1], args):
            out.append(self.arg(ctype, v, named, method, pname))
        if method == 'Release' and ret == '0' and this in self.objs:
            self.objs[this][1] = False          # the next call that returns this address returns a new object
        if method == 'Map' and iface == 'ID3D11DeviceContext' and ret == 'S_OK':
            m = named['pMappedResource']
            m = m.v if isinstance(m, Ref) else m
            self.maps[(self.obj_in(named['pResource']), self.number(named['Subresource']))] = int(m['pData'], 0)
        if method == 'Unmap' and iface == 'ID3D11DeviceContext':
            self.maps.pop((self.obj_in(named['pResource']), self.number(named['Subresource'])), None)
        return ' '.join(out)

    def write(self, no, a):
        """The recorder's memcpy: a write of n bytes at dest, inside the open mapping that begins
        nearest below it."""
        dest, n = int(a['dest'], 0), self.number(a['n'])
        best = None
        for key, base in self.maps.items():
            if base <= dest and (best is None or base > best[1]):
                best = (key, base)
        self.stats['writes'] += 1
        if best is None:
            self.stats['unplaced_writes'] += 1
            return '%d W 0 0 0 %d' % (no, n)
        return '%d W %d %d %d %d' % (no, best[0][0], best[0][1], dest - best[1], n)


# ------------------------------------------------------------------------------- self-test

SELFTEST = [
    ('0 D3D11CreateDevice(pAdapter = NULL, DriverType = D3D_DRIVER_TYPE_HARDWARE, Software = NULL, Flags = 0x0, '
     'pFeatureLevels = NULL, FeatureLevels = 0, SDKVersion = 7, ppDevice = &0x10, pFeatureLevel = &D3D_FEATURE_LEVEL_11_0, '
     'ppImmediateContext = &0x11) = S_OK',
     '0 . D3D11CreateDevice =0 n i1 i0 i0 n i0 i7 >o1 >I4[45056] >o2'),
    ('1 ID3D11Device5::QueryInterface(this = 0x10, riid = IID_IDXGIDevice1, ppvObj = &0x18) = S_OK',
     '1 . ID3D11Device::QueryInterface =0 o1 g0f97db777662ba48ba28070143b4392c >o3'),
    ('2 ID3D11Device5::GetImmediateContext(this = 0x10, ppImmediateContext = &0x11)',
     '2 F ID3D11Device::GetImmediateContext - o1 >o2'),
    ('3 ID3D11Device5::CreateBuffer(this = 0x10, pDesc = &{ByteWidth = 112, Usage = D3D11_USAGE_DYNAMIC, BindFlags = '
     'D3D11_BIND_CONSTANT_BUFFER, CPUAccessFlags = D3D11_CPU_ACCESS_WRITE, MiscFlags = 0x0, StructureByteStride = 0}, '
     'pInitialData = NULL, ppBuffer = &0x20) = S_OK',
     '3 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=112,Usage=2,BindFlags=4,CPUAccessFlags=65536,'
     'MiscFlags=0,StructureByteStride=0} n >o4'),
    ('4 ID3D11DeviceContext4::Map(this = 0x11, pResource = 0x20, Subresource = 0, MapType = D3D11_MAP_WRITE_DISCARD, '
     'MapFlags = 0x0, pMappedResource = &{pData = 0x7000, RowPitch = 112, DepthPitch = 112}) = S_OK',
     '4 . ID3D11DeviceContext::Map =0 o2 o4 i0 i4 i0 >sD3D11_MAPPED_SUBRESOURCE{pData=28672,RowPitch=112,DepthPitch=112}'),
    ('5 memcpy(dest = 0x7010, src = blob(48), n = 48) // fake', '5 W 4 0 16 48'),
    ('6 ID3D11DeviceContext4::Unmap(this = 0x11, pResource = 0x20, Subresource = 0)',
     '6 . ID3D11DeviceContext::Unmap - o2 o4 i0'),
    ('7 memcpy(dest = 0x7010, src = blob(8), n = 8) // fake', '7 W 0 0 0 8'),
    ('8 ID3D11Device5::CreateTexture2D(this = 0x10, pDesc = &{Width = 4, Height = 4, MipLevels = 2, ArraySize = 1, '
     'Format = DXGI_FORMAT_R8G8B8A8_UNORM, SampleDesc = {Count = 1, Quality = 0}, Usage = D3D11_USAGE_IMMUTABLE, '
     'BindFlags = D3D11_BIND_SHADER_RESOURCE, CPUAccessFlags = 0x0, MiscFlags = 0x0}, pInitialData = {{pSysMem = blob(64), '
     'SysMemPitch = 16, SysMemSlicePitch = 64}, {pSysMem = blob(16), SysMemPitch = 8, SysMemSlicePitch = 16}}, '
     'ppTexture2D = &0x40) = S_OK',
     '8 . ID3D11Device::CreateTexture2D =0 o1 sD3D11_TEXTURE2D_DESC{Width=4,Height=4,MipLevels=2,ArraySize=1,Format=28,'
     'SampleDesc.Count=1,SampleDesc.Quality=0,Usage=1,BindFlags=8,CPUAccessFlags=0,MiscFlags=0} '
     'SD3D11_SUBRESOURCE_DATA[{pSysMem=b64,SysMemPitch=16,SysMemSlicePitch=64}{pSysMem=b16,SysMemPitch=8,'
     'SysMemSlicePitch=16}] >o5'),
    ('9 ID3D11Device5::CreateVertexShader(this = 0x10, pShaderBytecode = shader(vs_5_0, 12 lines), BytecodeLength = 464, '
     'pClassLinkage = NULL, ppVertexShader = &0x30) = S_OK',
     '9 . ID3D11Device::CreateVertexShader =0 o1 B464 i464 n >o6'),
    ('10 ID3D11DeviceContext4::PSSetSamplers(this = 0x11, StartSlot = 0, NumSamplers = 2, ppSamplers = {NULL, 0x50})',
     '10 . ID3D11DeviceContext::PSSetSamplers - o2 i0 i2 O[0,7]'),
    ('11 ID3D11DeviceContext4::IASetVertexBuffers(this = 0x11, StartSlot = 0, NumBuffers = 1, ppVertexBuffers = &0x20, '
     'pStrides = &60, pOffsets = &0)',
     '11 . ID3D11DeviceContext::IASetVertexBuffers - o2 i0 i1 O[4] I4[60] I4[0]'),
    ('12 ID3D11DeviceContext4::ClearRenderTargetView(this = 0x11, pRenderTargetView = 0x60, ColorRGBA = {0, 0, 0.5, 1})',
     '12 . ID3D11DeviceContext::ClearRenderTargetView - o2 o8 F[0,0,0.5,1]'),
    ('13 ID3D11Buffer::Release(this = 0x20) = 0', '13 . ID3D11Buffer::Release =0 o4'),
    ('14 ID3D11Device5::CreateBuffer(this = 0x10, pDesc = &{ByteWidth = 16, Usage = D3D11_USAGE_DEFAULT, BindFlags = '
     'D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER, CPUAccessFlags = 0x0, MiscFlags = 0x0, StructureByteStride = 0}, '
     'pInitialData = &{pSysMem = blob(16), SysMemPitch = 0, SysMemSlicePitch = 0}, ppBuffer = &0x20) = S_OK',
     '14 . ID3D11Device::CreateBuffer =0 o1 sD3D11_BUFFER_DESC{ByteWidth=16,Usage=0,BindFlags=3,CPUAccessFlags=0,'
     'MiscFlags=0,StructureByteStride=0} sD3D11_SUBRESOURCE_DATA{pSysMem=b16,SysMemPitch=0,SysMemSlicePitch=0} >o9'),
    ('15 ID3D11DeviceContext4::GetData(this = 0x11, pAsync = 0x70, pData = &TRUE, DataSize = 4, GetDataFlags = '
     'D3D11_ASYNC_GETDATA_DONOTFLUSH) = S_FALSE',
     '15 . ID3D11DeviceContext::GetData =1 o2 o10 >d4 i4 i1'),
    ('16 IDXGIAdapter4::EnumOutputs(this = 0x80, Output = 1, ppOutput = ?) = DXGI_ERROR_NOT_FOUND',
     '16 . IDXGIAdapter1::EnumOutputs =2289696770 o11 i1 >o?'),
    ('17 ID3D11DeviceContext4::RSGetViewports(this = 0x11, pNumViewports = &1, pViewports = &{TopLeftX = 0, TopLeftY = 0, '
     'Width = 512, Height = 512, MinDepth = 0, MaxDepth = 1})',
     '17 . ID3D11DeviceContext::RSGetViewports - o2 >I4[1] >sD3D11_VIEWPORT{TopLeftX=0,TopLeftY=0,Width=512,Height=512,'
     'MinDepth=0,MaxDepth=1}'),
    ('18 ID3D11Device5::CreateInputLayout(this = 0x10, pInputElementDescs = {{SemanticName = "TEX COORD", SemanticIndex = 0, '
     'Format = DXGI_FORMAT_R32G32_FLOAT, InputSlot = 0, AlignedByteOffset = 28, InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA, '
     'InstanceDataStepRate = 0}}, NumElements = 1, pShaderBytecodeWithInputSignature = blob(464), BytecodeLength = 464, '
     'ppInputLayout = &0x90) = S_OK',
     '18 . ID3D11Device::CreateInputLayout =0 o1 SD3D11_INPUT_ELEMENT_DESC[{SemanticName="TEX%20COORD",SemanticIndex=0,'
     'Format=16,InputSlot=0,AlignedByteOffset=28,InputSlotClass=0,InstanceDataStepRate=0}] i1 B464 i464 >o12'),
    ('19 ID3D11Device5::NoSuchMethod(this = 0x10, x = 1)', '19 U ID3D11Device::NoSuchMethod -'),
    ('22 IDXGIFactory7::CreateSwapChain(this = 0xb0, pDevice = 0x18, pDesc = &{BufferDesc = {Width = 64, Height = 32, RefreshRate = '
     '{Numerator = 0, Denominator = 0}, Format = DXGI_FORMAT_R8G8B8A8_UNORM, ScanlineOrdering = '
     'DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED, Scaling = DXGI_MODE_SCALING_UNSPECIFIED}, SampleDesc = {Count = 1, Quality = 0}, '
     'BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, BufferCount = 2, OutputWindow = 0x30156, Windowed = TRUE, SwapEffect = '
     'DXGI_SWAP_EFFECT_DISCARD, Flags = 0x0}, ppSwapChain = &0xc0) = S_OK',
     '22 . IDXGIFactory1::CreateSwapChain =0 o13 o3 sDXGI_SWAP_CHAIN_DESC{BufferDesc.Width=64,BufferDesc.Height=32,'
     'BufferDesc.RefreshRate.Numerator=0,BufferDesc.RefreshRate.Denominator=0,BufferDesc.Format=28,'
     'BufferDesc.ScanlineOrdering=0,BufferDesc.Scaling=0,SampleDesc.Count=1,SampleDesc.Quality=0,BufferUsage=32,'
     'BufferCount=2,OutputWindow=196950,Windowed=1,SwapEffect=0,Flags=0} >o14'),
    ('20 IDXGISwapChain4::GetFullscreenState(this = 0xc0, pFullscreen = &TRUE, ppTarget = NULL) = S_OK',
     '20 . IDXGISwapChain::GetFullscreenState =0 o14 >I4[1] n'),
]


def selftest():
    c = Converter(foreign={2: 'GetImmediateContext'})
    fails = 0
    for line, want in SELFTEST:
        try:
            got = c.convert(line)
        except Exception as e:      # noqa: BLE001
            got = 'EXCEPTION %s: %s' % (type(e).__name__, e)
        if got != want:
            fails += 1
            sys.stderr.write('FAIL\n  line %s\n  got  %s\n  want %s\n' % (line[:100], got, want))
    # the state survives a save and a load (ranges are converted one process each)
    import tempfile
    with tempfile.TemporaryDirectory() as d:
        c.save(os.path.join(d, 's.json'))
        c2 = Converter()
        c2.load(os.path.join(d, 's.json'))
        got = c2.convert('21 ID3D11Buffer::Release(this = 0x20) = 0')
        if got != '21 . ID3D11Buffer::Release =0 o9' or c2.stats['calls'] != len(SELFTEST) + 1:
            fails += 1
            sys.stderr.write('FAIL state: %s, %r\n' % (got, c2.stats))
    try:
        Converter().convert('0 ID3D11DeviceContext4::IASetPrimitiveTopology(this = 0x11, Topology = D3D11_NO_SUCH_NAME)')
        fails += 1
        sys.stderr.write('FAIL: an unknown constant did not stop the conversion\n')
    except Unknown:
        pass
    print('d3dscript selftest: %d checks failed' % fails if fails else 'd3dscript selftest: ok')
    return 1 if fails else 0


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ''
    if cmd == 'selftest':
        return selftest()
    if cmd not in ('convert', 'names'):
        sys.stderr.write(__doc__)
        return 2
    opt = dict(zip(argv[2::2], argv[3::2]))
    foreign = FOREIGN['mission1']
    if '--foreign' in opt:
        foreign = {int(x.split(':')[0]): x.split(':')[1] for x in opt['--foreign'].split(',') if x}
    c = Converter(foreign=foreign)
    if opt.get('--state') and os.path.exists(opt['--state']):
        c.load(opt['--state'])
    out = sys.stdout
    if opt.get('-o'):
        out = gzip.open(opt['-o'], 'wt', compresslevel=6) if opt['-o'].endswith('.gz') else open(opt['-o'], 'w')
    t0, n, size = time.time(), 0, 0
    for line in sys.stdin:
        line = line.rstrip('\n')
        if not line:
            continue
        try:
            s = c.convert(line)
        except Unknown as e:
            sys.stderr.write('d3dscript: no value for the name %s (line %s)\n' % (e, line[:160]))
            return 1
        except Exception as e:      # noqa: BLE001
            sys.stderr.write('d3dscript: %s: %s\n  %s\n' % (type(e).__name__, e, line[:300]))
            return 1
        n += 1
        if cmd == 'convert':
            out.write(s + '\n')
            size += len(s) + 1
    if out is not sys.stdout:
        out.close()
    if cmd == 'names':
        for k in sorted(c.used):
            print('%-48s %#12x  %-20s %d' % (k, c.const[k], c.const_src[k], c.used[k]))
    if opt.get('--state'):
        c.save(opt['--state'])
    sys.stderr.write('d3dscript: %d lines in %.1f s, %d bytes of script; in all so far %s; not described: %s\n' %
                     (n, time.time() - t0, size, json.dumps(c.stats), json.dumps(c.undescribed)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
