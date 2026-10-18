#!/usr/bin/env python3
"""d3dtrace: read an `apitrace dump` of the Direct3D call capture and count what the game asked.

Input is always standard input: the text `apitrace dump --color=never [--calls=A-B] <trace>`
prints, one call per line as `number Interface::Method(name = value, ...) = result`.

    d3dtrace.py selftest                 the parser and the counters on made-up lines (make test)
    d3dtrace.py clean [--cut N] [--only REGEX] [--full]
                                         the call lines again, one per line, with every multi-line
                                         string replaced by its size (see below) and long arrays cut
                                         (--full: lines over 2,000 characters are not rewritten, so
                                         every array is whole; the strings are replaced all the same.
                                         This is what d3dscript.py converts)
    d3dtrace.py summary                  one pass over a dump, everything counted, as JSON
    d3dtrace.py start                    call 0 to the first Present, runs of the same calls folded
    d3dtrace.py frame [--head N]         the calls of one frame (a dump of that range), folded (the
                                         first N lines of it), its census, and what is called
                                         between one draw and the next
    d3dtrace.py render FILE SECTION      a table from a summary's JSON (FILE), as Markdown:
                                         census, phases, creators, states, views, shaders, map,
                                         updates, queries, sets, draws, formats, dxgi, misc

THE TRACE HOLDS THE GAME'S SHADERS AS TEXT. The recorder stored each shader as its disassembly,
and `apitrace dump` prints it, hundreds of lines per shader. Nothing of that may leave the
machine the trace is on, so every reading of a dump goes through `call_lines()` THERE:
tools/trace/d3d/remote.sh runs this file on that machine at the end of the dump's pipe, and
only this file's output crosses. `call_lines()` keeps of a multi-line string its first word
when that is a shader model (`vs_5_0`) and the number of lines, nothing else.

No file is written and nothing is imported beyond the standard library, so the whole file can
be handed to `python3 -c`.
"""
import sys, re, json

HEAD = re.compile(r'^(\d+) ([A-Za-z_0-9]+(?:::[A-Za-z_0-9]+)?)\(')
QSTR = re.compile(r'"(?:[^"\\]|\\.)*"')
NAME_EQ = re.compile(r'([A-Za-z_][A-Za-z_0-9]*) = ')
SCALAR = re.compile(r'(?:[^,(){}"]|\([^()]*\))+')
MODEL = re.compile(r'^[a-z]s_\d_\d$')
AFTER = re.compile(r', [A-Za-z_][A-Za-z_0-9]* = |\)|\}')    # what follows a string that has ended

# The newest interface the real device implements -> the one the game uses (finding 31).
BASE = {
    'ID3D11Device5': 'ID3D11Device', 'ID3D11DeviceContext4': 'ID3D11DeviceContext',
    'IDXGIDevice4': 'IDXGIDevice1', 'IDXGIAdapter4': 'IDXGIAdapter1',
    'IDXGIFactory7': 'IDXGIFactory1', 'IDXGIOutput6': 'IDXGIOutput',
    'IDXGISwapChain4': 'IDXGISwapChain', 'ID3D11Texture2D1': 'ID3D11Texture2D',
    'ID3D11Texture3D1': 'ID3D11Texture3D', 'ID3D11RenderTargetView1': 'ID3D11RenderTargetView',
    'ID3D11ShaderResourceView1': 'ID3D11ShaderResourceView',
    'ID3D11UnorderedAccessView1': 'ID3D11UnorderedAccessView',
    'ID3D11BlendState1': 'ID3D11BlendState', 'ID3D11RasterizerState2': 'ID3D11RasterizerState',
    'ID3D11RasterizerState1': 'ID3D11RasterizerState', 'ID3D11Query1': 'ID3D11Query',
}

PREFIX = re.compile(r'\b(?:D3D11_(?:USAGE|BIND|CPU_ACCESS|RESOURCE_MISC|MAP_FLAG|MAP|QUERY_MISC|QUERY|'
                    r'FILTER|TEXTURE_ADDRESS|COMPARISON|STENCIL_OP|DEPTH_WRITE_MASK|FILL|CULL|BLEND_OP|'
                    r'BLEND|SRV_DIMENSION|RTV_DIMENSION|DSV_DIMENSION|UAV_DIMENSION|PRIMITIVE_TOPOLOGY|'
                    r'INPUT|CLEAR|ASYNC_GETDATA|BUFFEREX_SRV_FLAG|BUFFER_UAV_FLAG|DSV)_|DXGI_FORMAT_|'
                    r'DXGI_MODE_SCANLINE_ORDER_|DXGI_MODE_SCALING_|DXGI_SWAP_EFFECT_|DXGI_USAGE_|'
                    r'DXGI_SWAP_CHAIN_FLAG_|D3D_FEATURE_LEVEL_|D3D_DRIVER_TYPE_)')


def short(v):
    """A constant or a set of flags without its family prefix."""
    return PREFIX.sub('', v) if isinstance(v, str) else v


def base_name(func):
    if '::' not in func:
        return func
    i, m = func.split('::')
    return BASE.get(i, i) + '::' + m


# ---------------------------------------------------------------------------- reading lines

def open_quote(line, start=0):
    """Index of a quote that opens a string the line does not close, or -1."""
    i = start
    while True:
        j = line.find('"', i)
        if j < 0:
            return -1
        m = QSTR.match(line, j)
        if not m:
            return j
        i = m.end()


def call_lines(stream):
    """Complete call lines of a dump. A string that runs over several lines never comes out:
    it is replaced by shader(<model>, N lines) or text(N lines)."""
    cur = None
    first = ''
    n = 0
    for raw in stream:
        line = raw.rstrip('\r\n')
        if cur is None:
            if '"' not in line:
                if line[:1].isdigit():
                    yield line
                continue
            if not HEAD.match(line):
                continue
            j = open_quote(line)
            if j < 0:
                yield line
                continue
            cur, first, n = line[:j], line[j + 1:], 0
            continue
        # inside a multi-line string: look for the quote that closes it
        if HEAD.match(line) and line.endswith((')', 'S_OK')) and '"' not in line:
            cur = None          # the string never closed: drop that call, never print its text
            yield line
            continue
        # (a quote that is not escaped and is followed by what follows a value)
        k = -1
        i = 0
        while True:
            k = line.find('"', i)
            if k < 0:
                break
            b = k
            while b > 0 and line[b - 1] == '\\':
                b -= 1
            if (k - b) % 2 == 0 and AFTER.match(line, k + 1):
                break
            i = k + 1
        if k < 0:
            n += 1
            continue
        mark = ('shader(%s, %d lines)' % (first, n)) if MODEL.match(first) else 'text(%d lines)' % (n + 1)
        line = cur + mark + line[k + 1:]
        j = open_quote(line, len(cur) + len(mark))
        if j < 0:
            cur = None
            yield line
        else:
            cur, first, n = line[:j], line[j + 1:], 0


# ---------------------------------------------------------------------------- the parser

def parse_value(s, i):
    """A value at s[i:]: returns (value, index after it). A structure is a dict, an array a
    list, everything else the text as written; `&x` (a pointer to x) is x."""
    c = s[i]
    while c == '&':
        i += 1
        c = s[i]
    if c == '{':
        i += 1
        items = []
        named = False
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
    return m.group(0), m.end()


def parse_call(line):
    """(number, function, {argument: value}, result or None) of one call line."""
    m = HEAD.match(line)
    if not m:
        raise ValueError('not a call line')
    no, func, i = int(m.group(1)), m.group(2), m.end()
    args = {}
    if line[i] != ')':
        while True:
            m = NAME_EQ.match(line, i)
            if not m:
                raise ValueError('no argument name at %d' % i)
            v, i = parse_value(line, m.end())
            args[m.group(1)] = v
            if line[i] == ',':
                i += 2
                continue
            if line[i] != ')':
                raise ValueError('expected ) at %d' % i)
            break
    rest = line[i + 1:]
    ret = None
    if rest.startswith(' = '):
        ret = rest[3:].split(' // ')[0]
    return no, func, args, ret


def show(v, maxlist=8):
    """A parsed value as text again, constants shortened, long arrays cut to their ends."""
    if isinstance(v, dict):
        return '{' + ', '.join('%s = %s' % (k, show(x, maxlist)) for k, x in v.items()) + '}'
    if isinstance(v, list):
        if len(v) > maxlist:
            return '{%s, %s, ...(%d in all)..., %s}' % (show(v[0], maxlist), show(v[1], maxlist), len(v),
                                                        show(v[-1], maxlist))
        return '{' + ', '.join(show(x, maxlist) for x in v) + '}'
    return short(v)


def show_call(no, func, args, ret, maxlist=8):
    s = '%d %s(%s)' % (no, base_name(func), ', '.join('%s = %s' % (k, show(v, maxlist)) for k, v in args.items()))
    return s + (' = ' + ret if ret is not None else '')


def num(v):
    try:
        return int(v, 0)
    except (TypeError, ValueError):
        try:
            return float(v)
        except (TypeError, ValueError):
            return None


# ---------------------------------------------------------------------------- counting

class Agg(dict):
    """key -> {'n': count, 'first': call number, <name>: [min, max, sum], <name>: {value: count}}"""

    def hit(self, key, no, nums=None, sets=None):
        e = self.get(key)
        if e is None:
            e = self[key] = {'n': 0, 'first': no}
        e['n'] += 1
        if nums:
            for k, v in nums.items():
                if v is None:
                    continue
                r = e.get(k)
                if r is None:
                    e[k] = [v, v, v]
                else:
                    if v < r[0]:
                        r[0] = v
                    if v > r[1]:
                        r[1] = v
                    r[2] += v
        if sets:
            for k, v in sets.items():
                d = e.setdefault(k, {})
                if v in d:
                    d[v] += 1
                elif len(d) < 60:
                    d[v] = 1
                else:
                    d['(other)'] = d.get('(other)', 0) + 1
        return e


CREATORS = ('CreateBuffer', 'CreateTexture1D', 'CreateTexture2D', 'CreateTexture3D',
            'CreateShaderResourceView', 'CreateRenderTargetView', 'CreateDepthStencilView',
            'CreateUnorderedAccessView', 'CreateSamplerState', 'CreateBlendState',
            'CreateDepthStencilState', 'CreateRasterizerState', 'CreateVertexShader',
            'CreatePixelShader', 'CreateGeometryShader', 'CreateComputeShader', 'CreateHullShader',
            'CreateDomainShader', 'CreateInputLayout', 'CreateQuery', 'CreatePredicate',
            'CreateSwapChain', 'CreateDeferredContext', 'CreateClassLinkage', 'CreateCounter')
DRAWS = ('Draw', 'DrawIndexed', 'DrawInstanced', 'DrawIndexedInstanced', 'DrawAuto',
         'DrawIndexedInstancedIndirect', 'DrawInstancedIndirect')
SLOT = re.compile(r'StartSlot = (\d+), Num\w+ = (\d+), pp\w+ = (NULL|&|\{)')
FMT = re.compile(r'(\w+) = (DXGI_FORMAT_\w+)')
PTR = re.compile(r'= (0x[0-9a-f]+|NULL)')


def res_label(r):
    """A resource's kind as one phrase, sizes left out."""
    if r is None:
        return '(made before this dump)'
    if r['k'] == 'buffer':
        return 'buffer %s bind=%s cpu=%s misc=%s' % (r['usage'], r['bind'], r['cpu'], r['misc'])
    return '%s %s %s bind=%s cpu=%s misc=%s' % (r['k'], r['fmt'], r['usage'], r['bind'], r['cpu'], r['misc'])


def blend_effective(d):
    """A blend description with the fields the device does not read left out: the game leaves
    them unset, so two descriptions that mean the same differ in memory."""
    rts = d.get('RenderTarget') or []
    if d.get('IndependentBlendEnable') != 'TRUE':
        rts = rts[:1]
    out = []
    for rt in rts:
        if isinstance(rt, dict) and rt.get('BlendEnable') != 'TRUE':
            rt = {'BlendEnable': 'FALSE', 'RenderTargetWriteMask': rt.get('RenderTargetWriteMask')}
        out.append(rt)
    return {'AlphaToCoverageEnable': d.get('AlphaToCoverageEnable'),
            'IndependentBlendEnable': d.get('IndependentBlendEnable'), 'RenderTarget': out}


class Summary:
    """Everything `summary` counts, in one pass. Feed it complete call lines in dump order."""

    def __init__(self):
        self.census = {}            # function -> [first call, frame of first use, count]
        self.results = Agg()        # 'function = result'
        self.frame = 0
        self.per_frame = {'first_call': [None], 'calls': [0], 'draws': [0], 'creates': [0], 'maps': [0]}
        self.objs = {}              # pointer -> what was created there last
        self.creators = {}          # creator -> Agg of descriptions
        self.state_ptrs = {}        # state creator -> {'calls','raw':set,'eff':set,'ptrs':set,...}
        self.maps = Agg()
        self.open_maps = {}         # (resource, subresource) -> [base, key, memcpys]
        self.memcpy_free = Agg()
        self.updates = Agg()
        self.copies = Agg()
        self.queries = Agg()
        self.sets = {}              # method -> Agg
        self.draws = Agg()
        self.formats = Agg()
        self.dxgi = []              # every DXGI call but Present, compact
        self.misc = Agg()           # QueryInterface, Release, AddRef, GetDesc ... by interface
        self.swap_fmt = None
        self.bad = []               # lines the parser refused (number and reason only)
        self.lines = 0

    # -- helpers
    def creator(self, name):
        a = self.creators.get(name)
        if a is None:
            a = self.creators[name] = Agg()
        return a

    def setm(self, name):
        a = self.sets.get(name)
        if a is None:
            a = self.sets[name] = Agg()
        return a

    def bump(self, what):
        self.per_frame[what][-1] += 1

    # -- one line
    def feed(self, line):
        m = HEAD.match(line)
        if not m:
            return
        self.lines += 1
        no = int(m.group(1))
        func = base_name(m.group(2))
        meth = func.split('::')[-1]
        c = self.census.get(func)
        if c is None:
            self.census[func] = [no, self.frame, 1]
        else:
            c[2] += 1
        pf = self.per_frame
        if pf['first_call'][-1] is None:
            pf['first_call'][-1] = no
        pf['calls'][-1] += 1
        if 'DXGI_FORMAT_' in line:
            for field, fmt in FMT.findall(line):
                self.formats.hit('%s %s ; %s' % (meth, field, fmt[12:]), no)
        try:
            self.dispatch(no, func, meth, line)
        except Exception as e:      # a line the tool does not understand is counted, never fatal
            if len(self.bad) < 40:
                self.bad.append([no, func, '%s: %s' % (type(e).__name__, e)])
        if meth == 'Present':
            self.memcpy_free.hit('Present (the number is how many maps were open at it)', no,
                                 nums={'open': len(self.open_maps)})
            self.frame += 1
            for k in pf:
                pf[k].append(None if k == 'first_call' else 0)

    def dispatch(self, no, func, meth, line):
        if meth in ('Map', 'Unmap', 'memcpy'):
            return self.do_map(no, meth, line)
        m = SLOT.search(line)
        if m and meth not in ('IASetVertexBuffers', 'CSSetUnorderedAccessViews',
                              'OMSetRenderTargetsAndUnorderedAccessViews'):
            key = '%s+%s' % (m.group(1), m.group(2))
            if m.group(3) == 'NULL':
                key += ' array NULL'
            elif m.group(3) != '&':
                nulls = line.count('NULL')
                if nulls:
                    key += ' all NULL' if nulls >= int(m.group(2)) else ' some NULL'
            self.setm(meth).hit(key, no)
            return
        if meth in DRAWS or meth == 'Dispatch':
            _, _, a, _ = parse_call(line)
            del a['this']
            self.draws.hit(meth, no, nums=dict((k, num(v)) for k, v in a.items()))
            if meth != 'Dispatch':
                self.bump('draws')
            return
        h = getattr(self, 'do_' + meth, None)
        if h is not None:
            return h(no, func, line)
        if func.startswith('IDXGI') or func == 'D3D11CreateDevice':
            return self.do_dxgi(no, func, line)
        if meth.startswith(('IASet', 'OMSet', 'RSSet', 'Clear')) or meth.endswith('SetShader'):
            return self.do_set(no, meth, line)
        _, _, a, ret = parse_call(line)
        self.misc.hit('%s%s' % (func, ' = ' + ret if ret and not ret.isdigit() else ''), no)

    # -- Map, Unmap and the recorder's memcpy lines
    def do_map(self, no, meth, line):
        _, _, a, ret = parse_call(line)
        if meth == 'memcpy':
            dest, n = num(a.get('dest')), num(a.get('n'))
            best = None
            for om in self.open_maps.values():
                if om[0] is not None and om[0] <= dest and (best is None or om[0] > best[0]):
                    best = om
            if best is None:
                self.memcpy_free.hit('memcpy outside any open Map', no, nums={'bytes': n})
            else:
                best[2] += 1
                self.maps[best[1]]['memcpy'] = self.maps[best[1]].get('memcpy', 0) + 1
                self.maps.hit(best[1], no, nums={'copied': n})['n'] -= 1
            return
        res = a.get('pResource')
        rk = (res, a.get('Subresource'))
        if meth == 'Unmap':
            om = self.open_maps.pop(rk, None)
            if om is None:
                self.memcpy_free.hit('Unmap with no Map seen', no)
            elif om[2] == 0:
                e = self.maps[om[1]]
                e['no_copy'] = e.get('no_copy', 0) + 1
            return
        self.bump('maps')
        r = self.objs.get(res)
        key = '%s ; flags=%s ; %s ; %s ; = %s' % (short(a.get('MapType')), short(a.get('MapFlags')),
                                                   res_label(r),
                                                   'sub 0' if a.get('Subresource') == '0' else 'sub >0', ret)
        mr = a.get('pMappedResource')
        nums = {}
        base = None
        if isinstance(mr, dict):
            base = num(mr.get('pData'))
            nums['RowPitch'] = num(mr.get('RowPitch'))
            nums['DepthPitch'] = num(mr.get('DepthPitch'))
        if r is not None:
            if r['k'] == 'buffer':
                nums['ByteWidth'] = r['size']
            else:
                nums['Width'] = r['w']
                nums['Height'] = r['h']
        self.maps.hit(key, no, nums=nums)
        if rk in self.open_maps:
            self.memcpy_free.hit('Map of a subresource whose earlier Map is still open', no)
        if ret == 'S_OK':
            self.open_maps[rk] = [base, key, 0]

    # -- creators
    def made(self, a, out, rec):
        p = a.get(out)
        if isinstance(p, str) and p.startswith('0x'):
            self.objs[p] = rec
        return p

    def do_CreateBuffer(self, no, func, line):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        d = a['pDesc']
        init = a.get('pInitialData')
        rec = {'k': 'buffer', 'usage': short(d['Usage']), 'bind': short(d['BindFlags']),
               'cpu': short(d['CPUAccessFlags']), 'misc': short(d['MiscFlags']),
               'size': num(d['ByteWidth']), 'stride': num(d['StructureByteStride'])}
        self.made(a, 'ppBuffer', rec)
        key = '%s ; bind=%s ; cpu=%s ; misc=%s ; stride=%s ; init=%s ; = %s' % (
            rec['usage'], rec['bind'], rec['cpu'], rec['misc'],
            'yes' if rec['stride'] else '0', 'no' if init == 'NULL' else 'yes', ret)
        nums = {'ByteWidth': rec['size']}
        if rec['stride']:
            nums['StructureByteStride'] = rec['stride']
        if isinstance(init, dict):
            nums['init_bytes'] = num(str(init.get('pSysMem', ''))[5:-1])
        self.creator('CreateBuffer').hit(key, no, nums=nums)

    def tex(self, no, line, meth, kind, out):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        d = a['pDesc']
        init = a.get('pInitialData')
        sd = d.get('SampleDesc') or {}
        rec = {'k': kind, 'fmt': short(d['Format']), 'w': num(d['Width']), 'h': num(d.get('Height', '1')),
               'mips': num(d['MipLevels']), 'array': num(d.get('ArraySize', '1')),
               'depth': num(d.get('Depth', '1')), 'usage': short(d['Usage']), 'bind': short(d['BindFlags']),
               'cpu': short(d['CPUAccessFlags']), 'misc': short(d['MiscFlags'])}
        self.made(a, out, rec)
        ninit = 0 if init == 'NULL' else (len(init) if isinstance(init, list) else 1)
        key = '%s ; %s ; bind=%s ; cpu=%s ; misc=%s ; samples=%s/%s ; init=%s ; = %s' % (
            rec['fmt'], rec['usage'], rec['bind'], rec['cpu'], rec['misc'], sd.get('Count', '-'),
            sd.get('Quality', '-'), 'yes' if ninit else 'no', ret)
        size = '%dx%d' % (rec['w'], rec['h']) + ('x%d' % rec['depth'] if kind == 'tex3d' else '')
        self.creator(meth).hit(key, no, nums={'Width': rec['w'], 'Height': rec['h'], 'init_entries': ninit},
                               sets={'MipLevels': rec['mips'], 'ArraySize': rec['array'],
                                     'size': '%s m%d a%d' % (size, rec['mips'], rec['array'])})

    def do_CreateTexture1D(self, no, func, line):
        self.tex(no, line, 'CreateTexture1D', 'tex1d', 'ppTexture1D')

    def do_CreateTexture2D(self, no, func, line):
        self.tex(no, line, 'CreateTexture2D', 'tex2d', 'ppTexture2D')

    def do_CreateTexture3D(self, no, func, line):
        self.tex(no, line, 'CreateTexture3D', 'tex3d', 'ppTexture3D')

    def view(self, no, line, meth, out):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        r = self.objs.get(a.get('pResource'))
        d = a.get('pDesc')
        on = '(made before this dump)' if r is None else (
            'buffer bind=%s misc=%s' % (r['bind'], r['misc']) if r['k'] == 'buffer' else '%s %s' % (r['k'], r['fmt']))
        nums = {}
        if isinstance(d, dict):
            key = 'on %s ; view %s ; %s' % (on, short(d.get('Format')), short(d.get('ViewDimension')))
            if 'Flags' in d:
                key += ' ; flags=%s' % short(d['Flags'])
            for k, v in d.items():
                if isinstance(v, dict):
                    for kk, vv in v.items():
                        x = num(vv)
                        if x is not None:
                            nums[kk] = x
                        else:
                            key += ' ; %s=%s' % (kk, short(vv))
        else:
            key = 'on %s ; no description (NULL)' % on
        key += ' ; = %s' % ret
        self.creator(meth).hit(key, no, nums=nums)
        self.made(a, out, {'k': 'view', 'of': a.get('pResource'), 'on': on})

    def do_CreateShaderResourceView(self, no, func, line):
        self.view(no, line, 'CreateShaderResourceView', 'ppSRView')

    def do_CreateRenderTargetView(self, no, func, line):
        self.view(no, line, 'CreateRenderTargetView', 'ppRTView')

    def do_CreateDepthStencilView(self, no, func, line):
        self.view(no, line, 'CreateDepthStencilView', 'ppDepthStencilView')

    def do_CreateUnorderedAccessView(self, no, func, line):
        self.view(no, line, 'CreateUnorderedAccessView', 'ppUAView')

    def state(self, no, line, meth, descarg, out):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        d = a[descarg]
        raw = show(d, 99)
        eff = show(blend_effective(d), 99) if meth == 'CreateBlendState' else raw
        p = a.get(out)
        st = self.state_ptrs.setdefault(meth, {'calls': 0, 'raw': {}, 'eff': {}, 'ptrs': {}, 'same_desc_same_ptr': 0,
                                               'same_desc_other_ptr': 0, 'other_desc_same_ptr': 0})
        st['calls'] += 1
        st['raw'][raw] = st['raw'].get(raw, 0) + 1
        if eff in st['eff']:
            if st['eff'][eff] == p:
                st['same_desc_same_ptr'] += 1
            else:
                st['same_desc_other_ptr'] += 1
        elif p in st['ptrs']:
            st['other_desc_same_ptr'] += 1
        st['eff'][eff] = p
        st['ptrs'][p] = st['ptrs'].get(p, 0) + 1
        self.creator(meth).hit('%s ; = %s' % (eff, ret), no)

    def do_CreateSamplerState(self, no, func, line):
        self.state(no, line, 'CreateSamplerState', 'pSamplerDesc', 'ppSamplerState')

    def do_CreateBlendState(self, no, func, line):
        self.state(no, line, 'CreateBlendState', 'pBlendStateDesc', 'ppBlendState')

    def do_CreateDepthStencilState(self, no, func, line):
        self.state(no, line, 'CreateDepthStencilState', 'pDepthStencilDesc', 'ppDepthStencilState')

    def do_CreateRasterizerState(self, no, func, line):
        self.state(no, line, 'CreateRasterizerState', 'pRasterizerDesc', 'ppRasterizerState')

    def shader(self, no, func, line):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        meth = func.split('::')[-1]
        code = a.get('pShaderBytecode', '')
        m = re.match(r'shader\((\w+), (\d+) lines\)', code)
        out = [k for k in a if k.startswith('pp')]
        key = '%s ; linkage=%s ; out=%s ; = %s' % (m.group(1) if m else code.split('(')[0], a.get('pClassLinkage'),
                                                   'NULL' if out and a[out[0]] == 'NULL' else 'object', ret)
        self.creator(meth).hit(key, no, nums={'BytecodeLength': num(a.get('BytecodeLength')),
                                              'lines': int(m.group(2)) if m else None})

    do_CreateVertexShader = do_CreatePixelShader = do_CreateGeometryShader = shader
    do_CreateComputeShader = do_CreateHullShader = do_CreateDomainShader = shader

    def do_CreateInputLayout(self, no, func, line):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        els = a.get('pInputElementDescs') or []
        if isinstance(els, dict):
            els = [els]
        parts = []
        for e in els:
            parts.append('%s%s:%s@%s+%s%s' % (e['SemanticName'].strip('"'), e['SemanticIndex'], short(e['Format']),
                                              e['InputSlot'], e['AlignedByteOffset'],
                                              '' if e['InputSlotClass'].endswith('PER_VERTEX_DATA')
                                              else ' per instance/%s' % e['InstanceDataStepRate']))
        self.creator('CreateInputLayout').hit('%s elements: %s ; = %s' % (a.get('NumElements'), ', '.join(parts), ret),
                                              no, nums={'BytecodeLength': num(a.get('BytecodeLength'))})

    def do_CreateQuery(self, no, func, line):
        _, _, a, ret = parse_call(line)
        self.bump('creates')
        d = a['pQueryDesc']
        kind = short(d['Query'])
        self.made(a, 'ppQuery', {'k': 'query', 'kind': kind})
        self.creator('CreateQuery').hit('%s ; misc=%s ; out=%s ; = %s' % (
            kind, short(d['MiscFlags']), 'NULL' if a.get('ppQuery') == 'NULL' else 'object', ret), no)

    def qkind(self, a):
        r = self.objs.get(a.get('pAsync'))
        return r['kind'] if r and r.get('k') == 'query' else '(not a query made in this dump)'

    def do_Begin(self, no, func, line):
        _, _, a, _ = parse_call(line)
        self.queries.hit('Begin ; %s' % self.qkind(a), no)

    def do_End(self, no, func, line):
        _, _, a, _ = parse_call(line)
        self.queries.hit('End ; %s' % self.qkind(a), no)

    def do_GetData(self, no, func, line):
        _, _, a, ret = parse_call(line)
        d = a.get('pData')
        x = num(d)
        if d == 'NULL':
            wrote = 'no buffer (NULL)'
        elif ret != 'S_OK':
            wrote = 'buffer, not written'
        elif x is not None:
            wrote = 'wrote 0' if x == 0 else 'wrote a number > 0'
        else:
            wrote = 'wrote %s' % (d if len(str(d)) < 24 else str(d).split('(')[0])
        self.queries.hit('GetData ; %s ; size=%s ; flags=%s ; %s ; = %s' % (
            self.qkind(a), a.get('DataSize'), short(a.get('GetDataFlags')), wrote, ret), no,
            nums={'value': x} if (x is not None and ret == 'S_OK' and d != 'NULL') else None)

    # -- writes and copies
    def do_UpdateSubresource(self, no, func, line):
        _, _, a, _ = parse_call(line)
        r = self.objs.get(a.get('pDstResource'))
        src = str(a.get('pSrcData'))
        self.updates.hit('%s ; %s ; box=%s' % (res_label(r), 'sub 0' if a.get('DstSubresource') == '0' else 'sub >0',
                                               'NULL' if a.get('pDstBox') == 'NULL' else 'given'), no,
                         nums={'bytes': num(src[5:-1]) if src.startswith('blob(') else None,
                               'SrcRowPitch': num(a.get('SrcRowPitch'))})

    def do_CopySubresourceRegion(self, no, func, line):
        _, _, a, _ = parse_call(line)
        d, s = self.objs.get(a.get('pDstResource')), self.objs.get(a.get('pSrcResource'))
        b = a.get('pSrcBox')
        self.copies.hit('CopySubresourceRegion ; to %s ; from %s ; box=%s ; at=%s' % (
            res_label(d), res_label(s), 'NULL' if b == 'NULL' else 'given',
            'origin' if (a.get('DstX'), a.get('DstY'), a.get('DstZ')) == ('0', '0', '0') else 'offset'), no,
            nums={'DstSubresource': num(a.get('DstSubresource')), 'SrcSubresource': num(a.get('SrcSubresource'))})

    def do_CopyResource(self, no, func, line):
        _, _, a, _ = parse_call(line)
        d, s = self.objs.get(a.get('pDstResource')), self.objs.get(a.get('pSrcResource'))
        self.copies.hit('CopyResource ; to %s ; from %s' % (res_label(d), res_label(s)), no)

    def do_ResolveSubresource(self, no, func, line):
        _, _, a, _ = parse_call(line)
        d, s = self.objs.get(a.get('pDstResource')), self.objs.get(a.get('pSrcResource'))
        self.copies.hit('ResolveSubresource ; to %s ; from %s ; %s' % (res_label(d), res_label(s),
                                                                       short(a.get('Format'))), no)

    # -- the Set methods that are not slot ranges, and the clears
    def do_set(self, no, meth, line):
        _, _, a, _ = parse_call(line)
        del a['this']
        nums = None
        if meth == 'OMSetRenderTargets':
            v = a.get('ppRenderTargetViews')
            key = 'NumViews=%s ; views %s ; depth view %s' % (
                a.get('NumViews'), 'array NULL' if v == 'NULL' else ('some NULL' if 'NULL' in v else 'given'),
                'NULL' if a.get('pDepthStencilView') == 'NULL' else 'given')
        elif meth == 'RSSetViewports':
            v = a.get('pViewports')
            n = a.get('NumViewports')
            if isinstance(v, dict):
                key = 'n=%s ; at %s,%s ; depth %s..%s' % (n, v.get('TopLeftX'), v.get('TopLeftY'), v.get('MinDepth'), v.get('MaxDepth'))
                self.setm(meth).hit(key, no, sets={'size': '%sx%s' % (v.get('Width'), v.get('Height'))})
                return
            key = 'n=%s' % n
        elif meth == 'IASetIndexBuffer':
            key = '%s ; buffer %s ; offset %s' % (short(a.get('Format')), 'NULL' if a.get('pIndexBuffer') == 'NULL' else 'given',
                                                  '0' if a.get('Offset') == '0' else '>0')
        elif meth.endswith('SetShader'):
            p = [v for k, v in a.items() if k.startswith('p') and not k.startswith('pp')]
            key = 'shader %s ; class instances %s/%s' % ('NULL' if p and p[0] == 'NULL' else 'given',
                                                         a.get('ppClassInstances'), a.get('NumClassInstances'))
        elif meth == 'OMSetBlendState':
            bf = a.get('BlendFactor')
            key = 'state %s ; factor %s ; mask %s' % ('NULL' if a.get('pBlendState') == 'NULL' else 'given',
                                                      'NULL' if bf == 'NULL' else show(bf), a.get('SampleMask'))
        elif meth == 'OMSetDepthStencilState':
            key = 'state %s ; ref %s' % ('NULL' if a.get('pDepthStencilState') == 'NULL' else 'given', a.get('StencilRef'))
        elif meth in ('RSSetState', 'IASetInputLayout'):
            key = 'NULL' if list(a.values())[0] == 'NULL' else 'given'
        elif meth == 'ClearRenderTargetView':
            key = 'colour %s' % show(a.get('ColorRGBA'))
        elif meth == 'ClearDepthStencilView':
            key = 'flags %s ; depth %s ; stencil %s' % (short(a.get('ClearFlags')), a.get('Depth'), a.get('Stencil'))
        elif meth == 'IASetVertexBuffers':
            st = a.get('pStrides')
            off = a.get('pOffsets')
            offs = off if isinstance(off, list) else [off]
            key = '%s+%s ; offsets %s' % (a.get('StartSlot'), a.get('NumBuffers'),
                                          'all 0' if all(o == '0' for o in offs) else 'some >0')
            self.setm(meth).hit(key, no, sets={'strides': show(st)})
            return
        elif meth == 'CSSetUnorderedAccessViews':
            key = '%s+%s ; initial counts %s' % (a.get('StartSlot'), a.get('NumUAVs'), show(a.get('pUAVInitialCounts')))
        elif meth == 'OMSetRenderTargetsAndUnorderedAccessViews':
            key = ' ; '.join('%s=%s' % (k, 'given' if str(v).startswith('0x') else show(v)) for k, v in a.items())
        else:
            key = ' ; '.join('%s=%s' % (k, 'ptr' if str(v).startswith('0x') else show(v)) for k, v in a.items())
        self.setm(meth).hit(key, no, nums=nums)

    do_IASetVertexBuffers = lambda self, no, func, line: self.do_set(no, 'IASetVertexBuffers', line)
    do_CSSetUnorderedAccessViews = lambda self, no, func, line: self.do_set(no, 'CSSetUnorderedAccessViews', line)

    # -- DXGI: every call kept, long arrays described
    def do_Present(self, no, func, line):
        _, _, a, ret = parse_call(line)
        self.misc.hit('IDXGISwapChain::Present ; SyncInterval=%s ; Flags=%s ; = %s' % (a.get('SyncInterval'), a.get('Flags'), ret), no)

    def do_dxgi(self, no, func, line):
        _, _, a, ret = parse_call(line)
        meth = func.split('::')[-1]
        note = None
        if meth == 'GetDisplayModeList' and isinstance(a.get('pDesc'), list):
            modes = a['pDesc']
            note = {'modes': ['%sx%s@%s/%s %s %s %s' % (m['Width'], m['Height'], m['RefreshRate']['Numerator'],
                                                         m['RefreshRate']['Denominator'], short(m['Format']),
                                                         short(m['ScanlineOrdering']), short(m['Scaling']))
                              for m in modes]}
            a['pDesc'] = '(%d modes, listed apart)' % len(modes)
        elif meth == 'GetGammaControlCapabilities' and isinstance(a.get('pGammaCaps'), dict):
            pos = [num(x) for x in a['pGammaCaps'].get('ControlPointPositions', [])]
            npt = num(a['pGammaCaps'].get('NumGammaControlPoints')) or 0
            used = pos[:npt]
            dev = max([abs(p - i / float(max(npt - 1, 1))) for i, p in enumerate(used)] or [0])
            note = {'positions_in_array': len(pos), 'first': pos[:3], 'last_used': used[-2:], 'after_used': pos[npt:npt + 2],
                    'max_distance_from_even_spacing': dev}
            a['pGammaCaps']['ControlPointPositions'] = '(%d numbers, described apart)' % len(pos)
        elif meth == 'SetGammaControl' and isinstance(a.get('pArray'), dict):
            curve = a['pArray'].get('GammaCurve', [])
            rgb = [(num(c['Red']), num(c['Green']), num(c['Blue'])) for c in curve]
            note = {'curve_entries': len(rgb), 'grey': all(r == g == b for r, g, b in rgb),
                    'rising': all(rgb[i][0] <= rgb[i + 1][0] for i in range(len(rgb) - 1)),
                    'samples': dict((str(i), rgb[i][0]) for i in (0, 1, 2, 128, 256, 512, 768, 1023, 1024) if i < len(rgb)),
                    'max_value': max([c[0] for c in rgb] or [0]),
                    'index_of_max': max(range(len(rgb)), key=lambda i: rgb[i][0]) if rgb else None}
            a['pArray']['GammaCurve'] = '(%d entries, described apart)' % len(rgb)
        if meth == 'CreateSwapChain':
            self.bump('creates')
            d = a.get('pDesc') or {}
            self.swap_fmt = short((d.get('BufferDesc') or {}).get('Format'))
            self.creator('CreateSwapChain').hit(show(d, 99) + ' ; = %s' % ret, no)
        if meth == 'ResizeBuffers' and a.get('NewFormat') not in (None, 'DXGI_FORMAT_UNKNOWN'):
            self.swap_fmt = short(a.get('NewFormat'))
        if meth == 'GetBuffer':
            self.made(a, 'ppSurface', {'k': 'tex2d', 'fmt': self.swap_fmt, 'w': 0, 'h': 0, 'mips': 1, 'array': 1,
                                       'depth': 1, 'usage': '(the swap chain\'s buffer)', 'bind': '-', 'cpu': '-', 'misc': '-'})
        e = {'no': no, 'frame': self.frame, 'call': show_call(no, func, a, ret, 99)}
        if note:
            e['note'] = note
        if len(self.dxgi) < 400:
            self.dxgi.append(e)
        self.misc.hit('%s%s' % (func, ' = ' + ret if ret and not ret.isdigit() else ''), no)

    def do_QueryInterface(self, no, func, line):
        _, _, a, ret = parse_call(line)
        e = {'no': no, 'frame': self.frame, 'call': show_call(no, func, a, ret)}
        self.misc.hit('%s ; %s ; = %s' % (func, a.get('riid'), ret), no)
        if func.startswith('IDXGI') and len(self.dxgi) < 400:
            self.dxgi.append(e)

    def do_Release(self, no, func, line):
        ret = line.rsplit(' = ', 1)[-1]
        self.misc.hit('%s ; %s' % (func, 'to 0' if ret == '0' else 'to >0'), no)
        if func.startswith('IDXGI') and len(self.dxgi) < 400:
            self.dxgi.append({'no': no, 'frame': self.frame, 'call': '%d %s = %s' % (no, func, ret)})

    def do_AddRef(self, no, func, line):
        self.misc.hit(func, no)

    def do_GetDesc(self, no, func, line):
        if func.startswith('IDXGI'):
            return self.do_dxgi(no, func, line)
        _, _, a, _ = parse_call(line)
        d = a.get('pDesc')
        key = func
        if isinstance(d, dict) and 'ViewDimension' in d:
            key += ' ; %s %s' % (short(d.get('Format')), short(d['ViewDimension']))
        elif isinstance(d, dict) and 'Usage' in d:
            key += ' ; %s bind=%s' % (short(d['Usage']), short(d['BindFlags']))
        self.misc.hit(key, no)

    def do_RSGetViewports(self, no, func, line):
        _, _, a, _ = parse_call(line)
        v = a.get('pViewports')
        self.misc.hit('%s ; pNumViewports=%s ; pViewports %s' % (
            func, a.get('pNumViewports'), 'NULL' if v == 'NULL' else ('one' if isinstance(v, dict) else 'array')), no)

    def do_GetFeatureLevel(self, no, func, line):
        self.misc.hit('%s = %s' % (func, short(line.rsplit(' = ', 1)[-1])), no)

    def do_GetImmediateContext(self, no, func, line):
        self.misc.hit(func, no)
        self.dxgi.append({'no': no, 'frame': self.frame, 'call': line[:300]})

    def out(self):
        for st in self.state_ptrs.values():
            st['distinct_raw_descriptions'] = len(st.pop('raw'))
            st['distinct_descriptions'] = len(st.pop('eff'))
            st['distinct_pointers'] = len(st.pop('ptrs'))
        while self.per_frame['calls'] and self.per_frame['calls'][-1] == 0:
            for k in self.per_frame:
                self.per_frame[k].pop()
        return {'lines': self.lines, 'frames': self.frame, 'census': self.census, 'results': self.results,
                'per_frame': self.per_frame, 'creators': self.creators, 'states': self.state_ptrs,
                'maps': self.maps, 'map_notes': self.memcpy_free, 'maps_left_open': len(self.open_maps),
                'updates': self.updates, 'copies': self.copies, 'queries': self.queries, 'sets': self.sets,
                'draws': self.draws, 'formats': self.formats, 'dxgi': self.dxgi, 'misc': self.misc,
                'not_understood': self.bad, 'python': sys.version.split()[0]}


# ---------------------------------------------------------------------------- folding a listing

def fold(tokens, maxp=16):
    """[(start index, period, repeats)]: the token list as runs of a repeated group."""
    out = []
    i = 0
    n = len(tokens)
    while i < n:
        bp, br = 1, 1
        for p in range(1, maxp + 1):
            if i + 2 * p > n:
                break
            grp = tokens[i:i + p]
            r = 1
            while tokens[i + r * p:i + (r + 1) * p] == grp:
                r += 1
            if r >= 2 and p * r > bp * br:
                bp, br = p, r
        out.append((i, bp, br))
        i += bp * br
    return out


DETAIL = re.compile(r'Create|Resize|Fullscreen|Gamma|GetBuffer|GetContainingOutput|MakeWindow|Enum|GetParent|'
                    r'QueryInterface|GetDesc|GetDisplay|D3D11CreateDevice|Present|Flush|GetFeatureLevel|'
                    r'GetImmediateContext|OMSetRenderTargets|RSSetViewports|Clear|GetData|Begin|End|Dispatch|Copy')


STATE = re.compile(r'^(?:IA|VS|PS|GS|CS|HS|DS|OM|RS)Set')


def listing(lines, out, detail_width=170, until_present=False):
    """Print calls folded twice. First runs of a repeated group of methods become one item and
    neighbouring state setters one item; then runs of a repeated group of items are folded again.
    A call that stands alone and is not a plain state setter is printed with its arguments."""
    calls = []
    for line in lines:
        m = HEAD.match(line)
        if not m:
            continue
        calls.append((int(m.group(1)), base_name(m.group(2)), line))
        if until_present and m.group(2).endswith('::Present'):
            break
    names = [c[1].split('::')[-1] for c in calls]
    items = []                                  # [first call, last call, shape, text]
    for i, p, r in fold(names):
        a, b = calls[i][0], calls[i + p * r - 1][0]
        grp = names[i:i + p]
        if all(STATE.match(g) for g in grp):
            if items and items[-1][2] == 'state':
                items[-1][1] = b
                items[-1][4].extend(grp * r)
            else:
                items.append([a, b, 'state', '', grp * r])
            continue
        if r > 1:
            shape = grp[0] if p == 1 else '[' + ', '.join(grp) + ']'
            items.append([a, b, 'x ' + shape, '%d x %s' % (r, shape), None])
            continue
        no, func, line = calls[i]
        text = func
        if DETAIL.search(func):
            try:
                _, f, args, ret = parse_call(line)
                args.pop('this', None)
                text = show_call(no, f, args, ret, 4)[len(str(no)) + 1:]
            except Exception:
                text = func + ' (not parsed)'
            if len(text) > detail_width:
                text = text[:detail_width] + ' ...'
        items.append([a, b, names[i], text, None])
    for it in items:
        if it[2] == 'state':
            cnt = {}
            for g in it[4]:
                cnt[g] = cnt.get(g, 0) + 1
            it[3] = 'state: ' + ', '.join(g if n == 1 else '%s x%d' % (g, n) for g, n in cnt.items())
    for i, p, r in fold([it[2] for it in items], 12):
        a, b = items[i][0], items[i + p * r - 1][1]
        span = '%9d' % a if a == b else '%9d-%d' % (a, b)
        if r == 1:
            out.write('%-20s %s\n' % (span, items[i][3]))
        else:
            out.write('%-20s %d times { %s }\n' % (span, r, ' ; '.join(
                it[3] if it[2] == 'state' or it[2].startswith('x ') else it[2] for it in items[i:i + p])))
    return calls


def frame_report(lines, out, head=0):
    import io
    buf = io.StringIO()
    calls = listing(lines, buf)
    text = buf.getvalue().split('\n')
    if head and len(text) > head:
        text = text[:head] + ['          ... (%d more lines of the folded listing left out)' % (len(text) - head - 1), '']
    out.write('\n'.join(text))
    counts = {}
    sigs = {}
    order = []
    cur = []
    for no, func, line in calls:
        meth = func.split('::')[-1]
        counts[meth] = counts.get(meth, 0) + 1
        if meth in DRAWS or meth == 'Dispatch':
            sig = ' '.join(cur) + ' -> ' + meth
            if sig not in sigs:
                sigs[sig] = [0, no]
                order.append(sig)
            sigs[sig][0] += 1
            cur = []
        elif meth not in ('memcpy',):
            if cur and cur[-1].split('*')[0] == meth:
                k = cur[-1].split('*')
                cur[-1] = '%s*%d' % (meth, int(k[1]) + 1 if len(k) > 1 else 2)
            else:
                cur.append(meth)
    out.write('\ncensus of this range: %d calls\n' % len(calls))
    for k in sorted(counts, key=lambda k: -counts[k]):
        out.write('%8d  %s\n' % (counts[k], k))
    out.write('\nwhat is called between one draw and the next, by how often (%d different sequences)\n' % len(sigs))
    for sig in sorted(order, key=lambda s: -sigs[s][0])[:40]:
        out.write('%6d x  (first at %d)  %s\n' % (sigs[sig][0], sigs[sig][1], sig))


# ---------------------------------------------------------------------------- rendering tables

def rng(r, unit=''):
    if r is None:
        return ''
    return ('%s%s' % (r[0], unit)) if r[0] == r[1] else '%s to %s%s' % (r[0], r[1], unit)


def vals(d, limit=8):
    if not d:
        return ''
    ks = sorted(d, key=lambda k: -d[k])
    s = ', '.join('%s (%d)' % (k, d[k]) for k in ks[:limit])
    return s + (', and %d more' % (len(ks) - limit) if len(ks) > limit else '')


def table(out, head, rows):
    out.write('| ' + ' | '.join(head) + ' |\n|' + '|'.join('---' for _ in head) + '|\n')
    for r in rows:
        out.write('| ' + ' | '.join(str(x).replace('|', '\\|') for x in r) + ' |\n')
    out.write('\n')


def by_count(agg):
    return sorted(agg.items(), key=lambda kv: (-kv[1]['n'], kv[1]['first']))


def render(S, section, out):
    if section == 'census':
        rows = sorted(S['census'].items(), key=lambda kv: kv[1][0])
        table(out, ['first call', 'frame', 'count', 'function'], [(v[0], v[1], v[2], '`%s`' % k) for k, v in rows])
    elif section == 'phases':
        pf = S['per_frame']
        n = len(pf['calls'])
        step = 50
        rows = []
        for a in range(0, n, step):
            b = min(a + step, n)
            rows.append(('%d to %d' % (a, b - 1), pf['first_call'][a], sum(pf['calls'][a:b]), sum(pf['creates'][a:b]),
                         sum(pf['draws'][a:b]), sum(pf['maps'][a:b]), max(pf['calls'][a:b])))
        table(out, ['frames', 'first call', 'calls', 'creators', 'draws', 'maps', 'most calls in one frame'], rows)
    elif section in ('creators', 'views', 'shaders', 'states'):
        want = {'creators': ('CreateBuffer', 'CreateTexture1D', 'CreateTexture2D', 'CreateTexture3D', 'CreateQuery',
                             'CreateInputLayout', 'CreateSwapChain'),
                'views': ('CreateShaderResourceView', 'CreateRenderTargetView', 'CreateDepthStencilView',
                          'CreateUnorderedAccessView'),
                'shaders': ('CreateVertexShader', 'CreatePixelShader', 'CreateGeometryShader', 'CreateComputeShader',
                            'CreateHullShader', 'CreateDomainShader'),
                'states': ('CreateSamplerState', 'CreateBlendState', 'CreateDepthStencilState',
                           'CreateRasterizerState')}[section]
        for name in want:
            agg = S['creators'].get(name)
            if not agg:
                continue
            total = sum(e['n'] for e in agg.values())
            out.write('### `%s`: %d calls, %d distinct rows\n\n' % (name, total, len(agg)))
            if section == 'states':
                st = S['states'].get(name, {})
                out.write('%d calls; %d distinct descriptions as passed, %d once the fields the device does not read '
                          'are left out; %d distinct pointers returned. A description seen before came back with the '
                          'same pointer %d times and with another %d times; a new description came back with a '
                          'pointer already handed out %d times.\n\n' % (
                              st.get('calls', 0), st.get('distinct_raw_descriptions', 0), st.get('distinct_descriptions', 0),
                              st.get('distinct_pointers', 0), st.get('same_desc_same_ptr', 0),
                              st.get('same_desc_other_ptr', 0), st.get('other_desc_same_ptr', 0)))
            rows = []
            for k, e in by_count(agg):
                extra = []
                for f in ('ByteWidth', 'StructureByteStride', 'init_bytes', 'BytecodeLength', 'lines', 'Width', 'Height',
                          'FirstElement', 'NumElements', 'MostDetailedMip', 'MipLevels', 'MipSlice', 'FirstArraySlice',
                          'ArraySize', 'First2DArrayFace', 'NumCubes', 'FirstWSlice', 'WSize', 'Flags'):
                    if f in e and isinstance(e[f], list):
                        extra.append('%s %s' % (f, rng(e[f])))
                    elif f in e and isinstance(e[f], dict):
                        extra.append('%s: %s' % (f, vals(e[f], 6)))
                if 'size' in e:
                    extra.append('sizes: %s' % vals(e['size'], 5))
                rows.append((e['n'], e['first'], k.replace(' ; ', '; '), '; '.join(extra)))
            table(out, ['count', 'first call', 'description', 'ranges'], rows)
    elif section == 'map':
        rows = []
        for k, e in by_count(S['maps']):
            rows.append((e['n'], e['first'], k.replace(' ; ', '; '), rng(e.get('ByteWidth')) or
                         ('%s x %s' % (rng(e.get('Width')), rng(e.get('Height'))) if e.get('Width') else ''),
                         rng(e.get('RowPitch')), e.get('memcpy', 0), rng(e.get('copied')), e.get('no_copy', 0)))
        table(out, ['count', 'first call', 'map type; flags; resource; subresource; result', 'resource size',
                    'RowPitch written', 'memcpy lines', 'bytes per memcpy', 'unmapped with no memcpy'], rows)
        for k, e in S['map_notes'].items():
            out.write('- %s: %d (first at call %d)%s\n' % (k, e['n'], e['first'],
                                                           '; open: %s' % rng(e['open']) if 'open' in e else ''))
        out.write('- maps still open at the end of the dump: %d\n\n' % S['maps_left_open'])
    elif section == 'updates':
        table(out, ['count', 'first call', 'UpdateSubresource into; subresource; box', 'bytes', 'SrcRowPitch'],
              [(e['n'], e['first'], k.replace(' ; ', '; '), rng(e.get('bytes')), rng(e.get('SrcRowPitch')))
               for k, e in by_count(S['updates'])])
        table(out, ['count', 'first call', 'copy', 'destination subresource', 'source subresource'],
              [(e['n'], e['first'], k.replace(' ; ', '; '), rng(e.get('DstSubresource')), rng(e.get('SrcSubresource')))
               for k, e in by_count(S['copies'])])
    elif section == 'queries':
        table(out, ['count', 'first call', 'call; kind; arguments; what came back', 'values written'],
              [(e['n'], e['first'], k.replace(' ; ', '; '), rng(e.get('value'))) for k, e in
               sorted(S['queries'].items(), key=lambda kv: (kv[0].split(' ; ')[0] != 'Begin', kv[0].split(' ; ')[0], -kv[1]['n']))])
    elif section == 'sets':
        for name in sorted(S['sets'], key=lambda n: S['census'].get('ID3D11DeviceContext::' + n, [0])[0]):
            agg = S['sets'][name]
            total = sum(e['n'] for e in agg.values())
            out.write('**`%s`**: %d calls. ' % (name, total))
            parts = []
            for k, e in by_count(agg)[:14]:
                s = '%s (%d)' % (k.replace(' ; ', ', '), e['n'])
                for f in ('size', 'strides'):
                    if f in e:
                        s += ' [%s: %s]' % (f, vals(e[f], 6))
                parts.append(s)
            out.write('; '.join(parts))
            if len(agg) > 14:
                out.write('; and %d more forms' % (len(agg) - 14))
            out.write('\n\n')
    elif section == 'draws':
        rows = []
        for k, e in by_count(S['draws']):
            rows.append((e['n'], e['first'], '`%s`' % k, '; '.join('%s %s' % (f, rng(v)) for f, v in e.items()
                                                                  if isinstance(v, list))))
        table(out, ['count', 'first call', 'method', 'argument ranges'], rows)
    elif section == 'formats':
        byfmt = {}
        for k, e in S['formats'].items():
            where, fmt = k.split(' ; ')
            byfmt.setdefault(fmt, []).append((where, e['n'], e['first']))
        rows = []
        for fmt in sorted(byfmt, key=lambda f: min(x[2] for x in byfmt[f])):
            ws = sorted(byfmt[fmt], key=lambda x: -x[1])
            rows.append(('`%s`' % fmt, min(x[2] for x in ws), sum(x[1] for x in ws),
                         ', '.join('%s (%d)' % (w, n) for w, n, _ in ws)))
        table(out, ['format', 'first call', 'times', 'where (method and field, count)'], rows)
    elif section == 'dxgi':
        for e in S['dxgi']:
            out.write('    f%-4d %s\n' % (e['frame'], e['call']))
            if 'note' in e:
                out.write('          %s\n' % json.dumps(e['note'], sort_keys=True))
    elif section == 'misc':
        table(out, ['count', 'first call', 'call'], [(e['n'], e['first'], k.replace(' ; ', '; '))
                                                     for k, e in sorted(S['misc'].items(), key=lambda kv: kv[1]['first'])])
        if S.get('not_understood'):
            out.write('lines the tool did not understand: %s\n' % json.dumps(S['not_understood']))
    else:
        raise SystemExit('no such section: ' + section)


# ---------------------------------------------------------------------------- self-test

SELFTEST = r'''// process.name = "x"
0 D3D11CreateDevice(pAdapter = NULL, Flags = 0x0, ppDevice = &0x10, pFeatureLevel = &D3D_FEATURE_LEVEL_11_0) = S_OK
1 ID3D11Device5::CreateBuffer(this = 0x10, pDesc = &{ByteWidth = 112, Usage = D3D11_USAGE_DYNAMIC, BindFlags = D3D11_BIND_CONSTANT_BUFFER, CPUAccessFlags = D3D11_CPU_ACCESS_WRITE, MiscFlags = 0x0, StructureByteStride = 0}, pInitialData = NULL, ppBuffer = &0x20) = S_OK
2 ID3D11Device5::CreateVertexShader(this = 0x10, pShaderBytecode = "vs_5_0
dcl_made_up v0.x
a \"quoted\" word, and "this", is not the end
ret
", BytecodeLength = 464, pClassLinkage = NULL, ppVertexShader = &0x30) = S_OK
3 ID3D11DeviceContext4::Map(this = 0x11, pResource = 0x20, Subresource = 0, MapType = D3D11_MAP_WRITE_DISCARD, MapFlags = 0x0, pMappedResource = &{pData = 0x7000, RowPitch = 112, DepthPitch = 112}) = S_OK
4 memcpy(dest = 0x7010, src = blob(48), n = 48) // fake
5 ID3D11DeviceContext4::Unmap(this = 0x11, pResource = 0x20, Subresource = 0)
6 ID3D11Device5::CreateTexture2D(this = 0x10, pDesc = &{Width = 256, Height = 16, MipLevels = 2, ArraySize = 1, Format = DXGI_FORMAT_R8G8B8A8_TYPELESS, SampleDesc = {Count = 1, Quality = 0}, Usage = D3D11_USAGE_IMMUTABLE, BindFlags = D3D11_BIND_SHADER_RESOURCE, CPUAccessFlags = 0x0, MiscFlags = 0x0}, pInitialData = {{pSysMem = blob(16384), SysMemPitch = 1024, SysMemSlicePitch = 16384}, {pSysMem = blob(4096), SysMemPitch = 512, SysMemSlicePitch = 4096}}, ppTexture2D = &0x40) = S_OK
7 ID3D11Device5::CreateShaderResourceView(this = 0x10, pResource = 0x40, pDesc = &{Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D, Texture2D = {MostDetailedMip = 0, MipLevels = 2}}, ppSRView = &0x50) = S_OK
8 ID3D11DeviceContext4::PSSetShaderResources(this = 0x11, StartSlot = 3, NumViews = 1, ppShaderResourceViews = &0x50)
9 ID3D11DeviceContext4::PSSetSamplers(this = 0x11, StartSlot = 0, NumSamplers = 2, ppSamplers = {NULL, NULL})
10 ID3D11Device5::CreateQuery(this = 0x10, pQueryDesc = &{Query = D3D11_QUERY_EVENT, MiscFlags = 0x0}, ppQuery = &0x60) = S_OK
11 ID3D11DeviceContext4::End(this = 0x11, pAsync = 0x60)
12 ID3D11DeviceContext4::DrawIndexed(this = 0x11, IndexCount = 6, StartIndexLocation = 0, BaseVertexLocation = 0)
13 ID3D11DeviceContext4::DrawIndexed(this = 0x11, IndexCount = 36, StartIndexLocation = 6, BaseVertexLocation = 0)
14 IDXGISwapChain4::Present(this = 0x70, SyncInterval = 1, Flags = 0x0) = S_OK
15 ID3D11DeviceContext4::GetData(this = 0x11, pAsync = 0x60, pData = &TRUE, DataSize = 4, GetDataFlags = D3D11_ASYNC_GETDATA_DONOTFLUSH) = S_OK
16 IDXGIOutput6::GetDesc(this = 0x80, pDesc = &{DeviceName = L"\\\\.\\DISPLAY1, with a comma", DesktopCoordinates = {left = 0, top = 0, right = 5120, bottom = 2880}, AttachedToDesktop = TRUE}) = S_OK
17 ID3D11Device5::CreateBlendState(this = 0x10, pBlendStateDesc = &{AlphaToCoverageEnable = FALSE, IndependentBlendEnable = FALSE, RenderTarget = {{BlendEnable = FALSE, SrcBlend = 0, DestBlend = 633273376, BlendOp = 0, SrcBlendAlpha = 0, DestBlendAlpha = 0, BlendOpAlpha = 0, RenderTargetWriteMask = 15}, {BlendEnable = FALSE, SrcBlend = 7, DestBlend = 0, BlendOp = 0, SrcBlendAlpha = 0, DestBlendAlpha = 0, BlendOpAlpha = 0, RenderTargetWriteMask = 15}}}, ppBlendState = &0x90) = S_OK
18 ID3D11Device5::CreateBlendState(this = 0x10, pBlendStateDesc = &{AlphaToCoverageEnable = FALSE, IndependentBlendEnable = FALSE, RenderTarget = {{BlendEnable = FALSE, SrcBlend = 0, DestBlend = 5, BlendOp = 0, SrcBlendAlpha = 0, DestBlendAlpha = 0, BlendOpAlpha = 0, RenderTargetWriteMask = 15}, {BlendEnable = FALSE, SrcBlend = 9, DestBlend = 0, BlendOp = 0, SrcBlendAlpha = 0, DestBlendAlpha = 0, BlendOpAlpha = 0, RenderTargetWriteMask = 15}}}, ppBlendState = &0x90) = S_OK
19 ID3D11DeviceContext4::IASetVertexBuffers(this = 0x11, StartSlot = 0, NumBuffers = 1, ppVertexBuffers = &0x20, pStrides = &60, pOffsets = &0)
'''


def selftest():
    import io
    lines = list(call_lines(io.StringIO(SELFTEST)))
    fails = []

    def check(what, got, want):
        if got != want:
            fails.append('%s: got %r, want %r' % (what, got, want))

    check('number of calls', len(lines), 20)
    check('no shader text survives', [l for l in lines if 'dcl_' in l or 'quoted' in l or 'ret' == l], [])
    sh = [l for l in lines if l.startswith('2 ')][0]
    check('shader line', sh, '2 ID3D11Device5::CreateVertexShader(this = 0x10, pShaderBytecode = shader(vs_5_0, 3 lines), '
                             'BytecodeLength = 464, pClassLinkage = NULL, ppVertexShader = &0x30) = S_OK')
    no, func, a, ret = parse_call(lines[1])
    check('buffer call', (no, func, ret), (1, 'ID3D11Device5::CreateBuffer', 'S_OK'))
    check('buffer desc', a['pDesc']['BindFlags'], 'D3D11_BIND_CONSTANT_BUFFER')
    check('out pointer', a['ppBuffer'], '0x20')
    no, func, a, ret = parse_call(lines[4])
    check('memcpy', (func, a['src'], a['n'], ret), ('memcpy', 'blob(48)', '48', None))
    no, func, a, ret = parse_call(lines[6])
    check('initial data array', len(a['pInitialData']), 2)
    check('nested struct', a['pDesc']['SampleDesc'], {'Count': '1', 'Quality': '0'})
    no, func, a, ret = parse_call(lines[16])
    check('wide string with a comma', a['pDesc']['DeviceName'], 'L"\\\\\\\\.\\\\DISPLAY1, with a comma"')
    check('rect', a['pDesc']['DesktopCoordinates']['right'], '5120')
    no, func, a, ret = parse_call(lines[9])
    check('array of NULL', a['ppSamplers'], ['NULL', 'NULL'])
    check('no result', ret, None)
    check('base name', base_name('IDXGIOutput6::GetDesc'), 'IDXGIOutput::GetDesc')
    check('short', short('D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET'), 'SHADER_RESOURCE | RENDER_TARGET')
    check('fold', fold(list('abababcdd')), [(0, 2, 3), (6, 1, 1), (7, 1, 2)])
    S = Summary()
    for l in lines:
        S.feed(l)
    o = json.loads(json.dumps(S.out()))
    check('nothing refused', o['not_understood'], [])
    check('frames', o['frames'], 1)
    check('census first use', o['census']['ID3D11DeviceContext::Map'], [3, 0, 1])
    check('frame 1 starts', o['per_frame']['first_call'], [0, 15])
    check('draws in frame 0', o['per_frame']['draws'][0], 2)
    mk = [k for k in o['maps']][0]
    check('map key', mk, 'WRITE_DISCARD ; flags=0x0 ; buffer DYNAMIC bind=CONSTANT_BUFFER cpu=WRITE misc=0x0 ; sub 0 ; = S_OK')
    check('map counted once, memcpy attributed', (o['maps'][mk]['n'], o['maps'][mk]['memcpy'], o['maps'][mk]['copied']),
          (1, 1, [48, 48, 48]))
    check('texture row', list(o['creators']['CreateTexture2D'])[0],
          'R8G8B8A8_TYPELESS ; IMMUTABLE ; bind=SHADER_RESOURCE ; cpu=0x0 ; misc=0x0 ; samples=1/0 ; init=yes ; = S_OK')
    check('view row', list(o['creators']['CreateShaderResourceView'])[0],
          'on tex2d R8G8B8A8_TYPELESS ; view R8G8B8A8_UNORM_SRGB ; TEXTURE2D ; = S_OK')
    check('slots', sorted((k, v['n']) for k, v in o['sets']['PSSetShaderResources'].items()), [('3+1', 1)])
    check('null slots', list(o['sets']['PSSetSamplers']), ['0+2 all NULL'])
    check('vertex buffers', list(o['sets']['IASetVertexBuffers']), ['0+1 ; offsets all 0'])
    check('query end', o['queries']['End ; EVENT']['n'], 1)
    check('getdata', list(k for k in o['queries'] if k.startswith('GetData')),
          ['GetData ; EVENT ; size=4 ; flags=DONOTFLUSH ; wrote TRUE ; = S_OK'])
    check('draw ranges', o['draws']['DrawIndexed']['IndexCount'], [6, 36, 42])
    st = o['states']['CreateBlendState']
    check('blend: two raw descriptions, one that the device reads, one pointer',
          (st['distinct_raw_descriptions'], st['distinct_descriptions'], st['distinct_pointers'], st['same_desc_same_ptr']),
          (2, 1, 1, 1))
    check('format seen', o['formats']['CreateShaderResourceView Format ; R8G8B8A8_UNORM_SRGB']['n'], 1)
    buf = io.StringIO()
    for sec in ('census', 'phases', 'creators', 'states', 'views', 'shaders', 'map', 'updates', 'queries', 'sets',
                'draws', 'formats', 'dxgi', 'misc'):
        render(o, sec, buf)
    check('render mentions the shader model', 'vs_5_0' in buf.getvalue(), True)
    buf = io.StringIO()
    frame_report(lines, buf)
    check('listing folds the two draws', '2 x DrawIndexed' in buf.getvalue(), True)
    check('listing merges state setters', 'state: PSSetShaderResources, PSSetSamplers' in buf.getvalue(), True)
    for f in fails:
        sys.stderr.write('FAIL ' + f + '\n')
    print('d3dtrace selftest: %d checks failed' % len(fails) if fails else 'd3dtrace selftest: ok')
    return 1 if fails else 0


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ''
    if cmd == 'selftest':
        return selftest()
    if cmd == 'render':
        with open(argv[2]) as f:
            S = json.load(f)
        for sec in argv[3:]:
            render(S, sec, sys.stdout)
        return 0
    stdin = open(0, encoding='utf-8', errors='replace', closefd=False)
    if cmd == 'clean':
        cut = int(argv[argv.index('--cut') + 1]) if '--cut' in argv else 0
        only = re.compile(argv[argv.index('--only') + 1]) if '--only' in argv else None
        full = '--full' in argv         # call_lines() has replaced the strings either way
        for line in call_lines(stdin):
            if only:
                m = HEAD.match(line)
                if not m or not only.search(m.group(2)):
                    continue
            if len(line) > 2000 and not full:
                try:
                    line = show_call(*parse_call(line), maxlist=6)
                except Exception:
                    pass
            sys.stdout.write((line[:cut] if cut else line) + '\n')
        return 0
    if cmd == 'summary':
        S = Summary()
        for line in call_lines(stdin):
            S.feed(line)
        json.dump(S.out(), sys.stdout)
        sys.stdout.write('\n')
        return 0
    if cmd == 'start':
        listing(call_lines(stdin), sys.stdout, until_present=True)
        return 0
    if cmd == 'frame':
        frame_report(list(call_lines(stdin)), sys.stdout,
                     int(argv[argv.index('--head') + 1]) if '--head' in argv else 0)
        return 0
    sys.stderr.write(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv))
