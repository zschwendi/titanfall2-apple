#!/usr/bin/env python3
"""frame_compare: read two pictures (PNG or .rtshot) and report how much they differ.

Standard library only. Never prints pixel values.
Commands: stats, compare, topng, selftest.
"""
import argparse
import io
import json
import os
import random
import struct
import sys
import tempfile
import time
import zlib
from collections import Counter
from contextlib import redirect_stdout

PNG_SIG = b'\x89PNG\r\n\x1a\n'
RTSHOT_MAGIC = b'RTSHOT1\x00'


class ToolError(Exception):
    """A user-facing error: printed on stderr, exit status 2."""


class _Parser(argparse.ArgumentParser):
    def error(self, message):
        raise ToolError(message)


# ---------------------------------------------------------------- reading

def decode_png(data):
    """Return (width, height, rgb bytes) for an 8-bit RGB or RGBA PNG."""
    if data[:8] != PNG_SIG:
        raise ToolError("not a PNG file (bad signature)")
    n = len(data)
    pos = 8
    ihdr = None
    idat = []
    while True:
        if pos + 8 > n:
            raise ToolError("PNG truncated: chunk header missing")
        length, ctype = struct.unpack('>I4s', data[pos:pos + 8])
        body_start = pos + 8
        body_end = body_start + length
        if body_end + 4 > n:
            raise ToolError("PNG truncated inside chunk %r" % ctype)
        body = data[body_start:body_end]
        (crc,) = struct.unpack('>I', data[body_end:body_end + 4])
        if zlib.crc32(ctype + body) & 0xFFFFFFFF != crc:
            raise ToolError("PNG chunk %r has a bad CRC" % ctype)
        pos = body_end + 4
        if ctype == b'IHDR':
            if length != 13:
                raise ToolError("PNG IHDR has length %d, expected 13" % length)
            ihdr = struct.unpack('>IIBBBBB', body)
            w, h, depth, ct, comp, filt, interlace = ihdr
            if depth != 8:
                raise ToolError("PNG bit depth %d found; only 8 is supported" % depth)
            if ct not in (2, 6):
                raise ToolError("PNG colour type %d found; only 2 (RGB) and 6 (RGBA) are supported" % ct)
            if interlace != 0:
                raise ToolError("PNG is interlaced (interlace method %d); not supported" % interlace)
            if comp != 0 or filt != 0:
                raise ToolError("PNG compression %d / filter method %d found; expected 0 and 0" % (comp, filt))
            if w == 0 or h == 0:
                raise ToolError("PNG has zero width or height")
        elif ctype == b'IDAT':
            if ihdr is None:
                raise ToolError("PNG IDAT before IHDR")
            idat.append(body)
        elif ctype == b'IEND':
            break
    if ihdr is None:
        raise ToolError("PNG has no IHDR chunk")
    if not idat:
        raise ToolError("PNG has no IDAT data")
    w, h, _, ct, _, _, _ = ihdr
    bpp = 3 if ct == 2 else 4
    try:
        raw = zlib.decompress(b''.join(idat))
    except zlib.error as e:
        raise ToolError("PNG image data does not decompress: %s" % e)
    stride = w * bpp
    if len(raw) != h * (stride + 1):
        raise ToolError("PNG image data has %d bytes, expected %d" % (len(raw), h * (stride + 1)))
    px = _unfilter(raw, w, h, bpp)
    if bpp == 3:
        rgb = bytes(px)
    else:
        out = bytearray(w * h * 3)
        out[0::3] = px[0::4]
        out[1::3] = px[1::4]
        out[2::3] = px[2::4]
        rgb = bytes(out)
    return w, h, rgb


def _unfilter(raw, w, h, bpp):
    """Undo the PNG row filters; returns bytearray of h*w*bpp bytes."""
    stride = w * bpp
    out = bytearray(h * stride)
    src = 0
    for y in range(h):
        ft = raw[src]
        cur = bytearray(raw[src + 1:src + 1 + stride])
        src += 1 + stride
        start = y * stride
        up = start - stride
        if ft == 0:
            pass
        elif ft == 1:
            for i in range(bpp, stride):
                cur[i] = (cur[i] + cur[i - bpp]) & 255
        elif ft == 2:
            if y > 0:
                for i in range(stride):
                    cur[i] = (cur[i] + out[up + i]) & 255
        elif ft == 3:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                b = out[up + i] if y > 0 else 0
                cur[i] = (cur[i] + ((a + b) >> 1)) & 255
        elif ft == 4:
            for i in range(stride):
                a = cur[i - bpp] if i >= bpp else 0
                if y > 0:
                    b = out[up + i]
                    c = out[up + i - bpp] if i >= bpp else 0
                else:
                    b = 0
                    c = 0
                p = a + b - c
                pa = p - a if p >= a else a - p
                pb = p - b if p >= b else b - p
                pc = p - c if p >= c else c - p
                if pa <= pb and pa <= pc:
                    pr = a
                elif pb <= pc:
                    pr = b
                else:
                    pr = c
                cur[i] = (cur[i] + pr) & 255
        else:
            raise ToolError("PNG row %d has unknown filter type %d" % (y, ft))
        out[start:start + stride] = cur
    return out


def decode_rtshot(data):
    if len(data) < 16 or data[:8] != RTSHOT_MAGIC:
        raise ToolError("not an .rtshot file (bad magic)")
    w, h = struct.unpack('<II', data[8:16])
    if w == 0 or h == 0:
        raise ToolError(".rtshot has zero width or height")
    if len(data) != 16 + w * h * 4:
        raise ToolError(".rtshot has %d bytes, expected %d for %dx%d"
                        % (len(data), 16 + w * h * 4, w, h))
    px = data[16:]
    out = bytearray(w * h * 3)
    out[0::3] = px[0::4]
    out[1::3] = px[1::4]
    out[2::3] = px[2::4]
    return w, h, bytes(out)


def load_image(path):
    try:
        with open(path, 'rb') as f:
            data = f.read()
    except OSError as e:
        raise ToolError("cannot read %s: %s" % (path, e.strerror or e))
    if path.endswith('.rtshot'):
        return decode_rtshot(data)
    try:
        return decode_png(data)
    except ToolError as e:
        raise ToolError("%s: %s" % (path, e))


# ---------------------------------------------------------------- writing

def _chunk(ctype, body):
    return (struct.pack('>I', len(body)) + ctype + body
            + struct.pack('>I', zlib.crc32(ctype + body) & 0xFFFFFFFF))


def encode_png(w, h, rgb):
    """8-bit RGB PNG, filter 0 on every row, zlib level 6."""
    stride = w * 3
    raw = bytearray()
    for y in range(h):
        raw.append(0)
        raw += rgb[y * stride:(y + 1) * stride]
    return (PNG_SIG
            + _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
            + _chunk(b'IDAT', zlib.compress(bytes(raw), 6))
            + _chunk(b'IEND', b''))


def topng_file(inp, out):
    w, h, rgb = load_image(inp)
    try:
        with open(out, 'wb') as f:
            f.write(encode_png(w, h, rgb))
    except OSError as e:
        raise ToolError("cannot write %s: %s" % (out, e.strerror or e))


# ---------------------------------------------------------------- measures

def stats_report(img, clear=None, tol=0):
    w, h, rgb = img
    n = w * h
    counts = Counter(zip(rgb[0::3], rgb[1::3], rgb[2::3]))
    rep = {
        'width': w,
        'height': h,
        'distinct': len(counts),
        'mean': [sum(rgb[c::3]) / n for c in range(3)],
        'top_share': max(counts.values()) / n,
    }
    if clear is not None:
        cr, cg, cb = clear
        nonclear = 0
        for (r, g, b), cnt in counts.items():
            if abs(r - cr) > tol or abs(g - cg) > tol or abs(b - cb) > tol:
                nonclear += cnt
        rep['nonclear_share'] = nonclear / n
    return rep


def cell_means(img, C, R):
    """Mean colour per cell (list of 3-lists, None for an empty cell)."""
    w, h, rgb = img
    rowlen = w * 3
    xs = [(i * w // C, (i + 1) * w // C) for i in range(C)]
    ys = [(j * h // R, (j + 1) * h // R) for j in range(R)]
    sums = [[0, 0, 0] for _ in range(C * R)]
    for j, (y0, y1) in enumerate(ys):
        for y in range(y0, y1):
            row = rgb[y * rowlen:(y + 1) * rowlen]
            for i, (x0, x1) in enumerate(xs):
                if x1 <= x0:
                    continue
                seg = row[x0 * 3:x1 * 3]
                s = sums[j * C + i]
                s[0] += sum(seg[0::3])
                s[1] += sum(seg[1::3])
                s[2] += sum(seg[2::3])
    means = []
    for j, (y0, y1) in enumerate(ys):
        for i, (x0, x1) in enumerate(xs):
            cnt = (y1 - y0) * (x1 - x0)
            if cnt <= 0:
                means.append(None)
            else:
                s = sums[j * C + i]
                means.append([s[0] / cnt, s[1] / cnt, s[2] / cnt])
    return means


def _reduced_luma(img):
    """Luminance blocks: 8x8 averages, as a list of rows of floats."""
    w, h, rgb = img
    rw, rh = w // 8, h // 8
    cols = rw * 8
    rowlen = w * 3
    red = []
    for by in range(rh):
        acc = [0.0] * rw
        for y in range(by * 8, by * 8 + 8):
            seg = rgb[y * rowlen:y * rowlen + cols * 3]
            lum = [(299 * r + 587 * g + 114 * b) / 1000
                   for r, g, b in zip(seg[0::3], seg[1::3], seg[2::3])]
            for bx in range(rw):
                acc[bx] += sum(lum[bx * 8:bx * 8 + 8])
        red.append([v / 64 for v in acc])
    return red


def ssim_of(a, b):
    ra = _reduced_luma(a)
    rb = _reduced_luma(b)
    if not ra or len(ra) < 8 or len(ra[0]) < 8:
        return None
    C1 = 6.5025
    C2 = 58.5225
    total = 0.0
    count = 0
    for wy in range(len(ra) // 8):
        for wx in range(len(ra[0]) // 8):
            va_list = []
            vb_list = []
            for y in range(wy * 8, wy * 8 + 8):
                va_list.extend(ra[y][wx * 8:wx * 8 + 8])
                vb_list.extend(rb[y][wx * 8:wx * 8 + 8])
            ma = sum(va_list) / 64
            mb = sum(vb_list) / 64
            va = sum((x - ma) ** 2 for x in va_list) / 64
            vb = sum((y - mb) ** 2 for y in vb_list) / 64
            cov = sum((x - ma) * (y - mb) for x, y in zip(va_list, vb_list)) / 64
            total += ((2 * ma * mb + C1) * (2 * cov + C2)) / \
                     ((ma * ma + mb * mb + C1) * (va + vb + C2))
            count += 1
    return total / count


def compare_report(a, b, tol=8, cells=(64, 36)):
    if a[0] != b[0] or a[1] != b[1]:
        raise ToolError("images differ in size: %dx%d and %dx%d" % (a[0], a[1], b[0], b[1]))
    w, h = a[0], a[1]
    n = w * h
    dch = []
    for c in range(3):
        dch.append([abs(x - y) for x, y in zip(a[2][c::3], b[2][c::3])])
    dr, dg, db = dch
    within = sum(1 for x, y, z in zip(dr, dg, db) if x <= tol and y <= tol and z <= tol) / n
    mean_abs = [sum(d) / n for d in dch]
    max_abs = max(max(dr), max(dg), max(db))
    C, R = cells
    ma_ = cell_means(a, C, R)
    mb_ = cell_means(b, C, R)
    diffs = []
    for pa, pb in zip(ma_, mb_):
        if pa is None or pb is None:
            continue
        for c in range(3):
            diffs.append(abs(pa[c] - pb[c]))
    cell_max = max(diffs) if diffs else 0.0
    cell_mean = sum(diffs) / len(diffs) if diffs else 0.0
    return {
        'width': w,
        'height': h,
        'tol': tol,
        'within': within,
        'mean_abs': mean_abs,
        'max_abs': max_abs,
        'cell_max': cell_max,
        'cell_mean': cell_mean,
        'ssim': ssim_of(a, b),
    }


def diff_images(a, b, gain=8, cells=(64, 36), top=10):
    """Difference picture (RGB bytes, gain * |a-b| clipped to 255) and a report."""
    if a[0] != b[0] or a[1] != b[1]:
        raise ToolError("images differ in size: %dx%d and %dx%d" % (a[0], a[1], b[0], b[1]))
    w, h = a[0], a[1]
    n = w * h
    d = bytes(abs(x - y) for x, y in zip(a[2], b[2]))
    out = bytes(min(255, gain * v) for v in d)
    dr, dg, db = d[0::3], d[1::3], d[2::3]
    differing = sum(1 for x, y, z in zip(dr, dg, db) if x or y or z) / n
    C, R = cells
    rowlen = w * 3
    xs = [(i * w // C, (i + 1) * w // C) for i in range(C)]
    ys = [(j * h // R, (j + 1) * h // R) for j in range(R)]
    cand = []
    for j, (y0, y1) in enumerate(ys):
        for i, (x0, x1) in enumerate(xs):
            cnt = (y1 - y0) * (x1 - x0)
            if cnt <= 0:
                continue
            total = 0
            mx = 0
            for y in range(y0, y1):
                seg = d[y * rowlen + x0 * 3:y * rowlen + x1 * 3]
                total += sum(seg)
                mx = max(mx, max(seg))
            cand.append((-(total / (cnt * 3)), j * C + i, {
                'col': i, 'row': j, 'x': x0, 'y': y0,
                'w': x1 - x0, 'h': y1 - y0,
                'mean_abs': total / (cnt * 3), 'max_abs': mx}))
    cand.sort(key=lambda t: (t[0], t[1]))
    rep = {
        'width': w,
        'height': h,
        'gain': gain,
        'differing': differing,
        'top': [t[2] for t in cand[:top]],
    }
    return out, rep


def parse_cells(text):
    try:
        c, r = text.lower().split('x')
        c, r = int(c), int(r)
    except ValueError:
        raise ToolError("--cells expects CxR, found %r" % text)
    if c <= 0 or r <= 0:
        raise ToolError("--cells needs positive numbers, found %r" % text)
    return c, r


def parse_clear(text):
    try:
        parts = [int(p) for p in text.split(',')]
    except ValueError:
        raise ToolError("--clear expects R,G,B, found %r" % text)
    if len(parts) != 3 or any(p < 0 or p > 255 for p in parts):
        raise ToolError("--clear expects three values 0-255, found %r" % text)
    return tuple(parts)


# ---------------------------------------------------------------- commands

def _build_parser():
    p = _Parser(prog='frame_compare.py')
    sub = p.add_subparsers(dest='cmd', required=True, parser_class=_Parser)

    s = sub.add_parser('stats')
    s.add_argument('image')
    s.add_argument('--clear', default=None)
    s.add_argument('--tol', type=int, default=0)

    c = sub.add_parser('compare')
    c.add_argument('a')
    c.add_argument('b')
    c.add_argument('--tol', type=int, default=8)
    c.add_argument('--cells', default='64x36')
    c.add_argument('--min-within', type=float, default=None)
    c.add_argument('--max-cell', type=float, default=None)

    t = sub.add_parser('topng')
    t.add_argument('inp')
    t.add_argument('out')

    df = sub.add_parser('diff')
    df.add_argument('a')
    df.add_argument('b')
    df.add_argument('out')
    df.add_argument('--gain', type=int, default=8)
    df.add_argument('--cells', default='64x36')
    df.add_argument('--top', type=int, default=10)

    sub.add_parser('selftest')
    return p


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    try:
        args = _build_parser().parse_args(argv)
        if args.cmd == 'stats':
            clear = parse_clear(args.clear) if args.clear is not None else None
            img = load_image(args.image)
            rep = stats_report(img, clear, args.tol)
            print(json.dumps(rep))
            return 0
        if args.cmd == 'compare':
            cells = parse_cells(args.cells)
            a = load_image(args.a)
            b = load_image(args.b)
            rep = compare_report(a, b, args.tol, cells)
            print(json.dumps(rep))
            ok = True
            if args.min_within is not None and rep['within'] < args.min_within:
                ok = False
            if args.max_cell is not None and rep['cell_max'] > args.max_cell:
                ok = False
            return 0 if ok else 1
        if args.cmd == 'topng':
            topng_file(args.inp, args.out)
            return 0
        if args.cmd == 'diff':
            if args.gain < 0:
                raise ToolError("--gain needs a value 0 or more, found %d" % args.gain)
            if args.top < 0:
                raise ToolError("--top needs a value 0 or more, found %d" % args.top)
            cells = parse_cells(args.cells)
            a = load_image(args.a)
            b = load_image(args.b)
            out, rep = diff_images(a, b, args.gain, cells, args.top)
            try:
                with open(args.out, 'wb') as f:
                    f.write(encode_png(a[0], a[1], out))
            except OSError as e:
                raise ToolError("cannot write %s: %s" % (args.out, e.strerror or e))
            print(json.dumps(rep))
            return 0
        if args.cmd == 'selftest':
            return selftest()
    except ToolError as e:
        print("frame_compare: error: %s" % e, file=sys.stderr)
        return 2
    except SystemExit as e:
        return e.code if isinstance(e.code, int) else 2
    return 2


# ---------------------------------------------------------------- self-test

class SelfTestFail(Exception):
    pass


class _Checks:
    def __init__(self):
        self.n = 0

    def ok(self, name, cond):
        if not cond:
            raise SelfTestFail(name)
        self.n += 1


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    if pb <= pc:
        return b
    return c


def _enc_raw(w, h, px, bpp, filters):
    """Test's own forward filter: returns the raw (filtered, with filter bytes)."""
    stride = w * bpp
    out = bytearray()
    prev = bytes(stride)
    for y in range(h):
        cur = px[y * stride:(y + 1) * stride]
        f = filters[y]
        out.append(f)
        for i in range(stride):
            x = cur[i]
            a = cur[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 0:
                v = x
            elif f == 1:
                v = x - a
            elif f == 2:
                v = x - b
            elif f == 3:
                v = x - ((a + b) // 2)
            else:
                v = x - _paeth(a, b, c)
            out.append(v & 255)
        prev = cur
    return bytes(out)


def _enc_png(w, h, px, ctype, filters, splits=1, depth=8, interlace=0):
    bpp = 3 if ctype == 2 else 4
    comp = zlib.compress(_enc_raw(w, h, px, bpp, filters), 6)
    parts = []
    step = (len(comp) + splits - 1) // splits
    for k in range(splits):
        parts.append(comp[k * step:(k + 1) * step])
    out = bytearray(PNG_SIG)
    out += _chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, ctype, 0, 0, interlace))
    for p in parts:
        out += _chunk(b'IDAT', p)
    out += _chunk(b'IEND', b'')
    return bytes(out)


def _rgba_to_rgb(px):
    out = bytearray(len(px) // 4 * 3)
    out[0::3] = px[0::4]
    out[1::3] = px[1::4]
    out[2::3] = px[2::4]
    return bytes(out)


def _write(path, data):
    with open(path, 'wb') as f:
        f.write(data)


def _rtshot_bytes(w, h, rgba):
    return RTSHOT_MAGIC + struct.pack('<II', w, h) + rgba


def selftest():
    try:
        ck = _Checks()
        rng = random.Random(1)
        with tempfile.TemporaryDirectory() as td:
            # 1. filters, RGB and RGBA
            W, H = 37, 23
            rgb = bytes(rng.getrandbits(8) for _ in range(W * H * 3))
            rgba = bytearray(W * H * 4)
            rgba[0::4] = rgb[0::3]
            rgba[1::4] = rgb[1::3]
            rgba[2::4] = rgb[2::3]
            rgba[3::4] = bytes(rng.getrandbits(8) for _ in range(W * H))
            rgba = bytes(rgba)
            for f in range(5):
                d = _enc_png(W, H, rgb, 2, [f] * H)
                ck.ok('filter %d RGB round trip' % f, decode_png(d) == (W, H, rgb))
                d = _enc_png(W, H, rgba, 6, [f] * H)
                ck.ok('filter %d RGBA round trip' % f, decode_png(d) == (W, H, rgb))
            mixed = [y % 5 for y in range(H)]
            ck.ok('mixed filters RGB', decode_png(_enc_png(W, H, rgb, 2, mixed)) == (W, H, rgb))
            ck.ok('mixed filters RGBA', decode_png(_enc_png(W, H, rgba, 6, mixed)) == (W, H, rgb))

            # 2. IDAT split over three chunks
            d = _enc_png(W, H, rgb, 2, mixed, splits=3)
            ck.ok('three IDAT chunks', decode_png(d) == (W, H, rgb))

            # 3. rtshot and topng
            p_rt = os.path.join(td, 'a.rtshot')
            _write(p_rt, _rtshot_bytes(W, H, rgba))
            ck.ok('rtshot read', load_image(p_rt) == (W, H, rgb))
            p_png = os.path.join(td, 'from_rt.png')
            topng_file(p_rt, p_png)
            ck.ok('topng of rtshot reads back', load_image(p_png) == (W, H, rgb))

            # 4. self comparison
            img = (64, 64, bytes(rng.getrandbits(8) for _ in range(64 * 64 * 3)))
            rep = compare_report(img, img, 8, (64, 36))
            ck.ok('self within 1', rep['within'] == 1.0)
            ck.ok('self max_abs 0', rep['max_abs'] == 0)
            ck.ok('self cell_max 0', rep['cell_max'] == 0)
            ck.ok('self ssim 1', rep['ssim'] is not None and abs(rep['ssim'] - 1.0) < 1e-9)
            p_img = os.path.join(td, 'img.png')
            _write(p_img, encode_png(64, 64, img[2]))
            with redirect_stdout(io.StringIO()):
                st = main(['compare', p_img, p_img, '--min-within', '1', '--max-cell', '0'])
            ck.ok('self compare exit 0', st == 0)

            # 5. one 8x8 block raised by 40
            base = bytearray(rng.randrange(200) for _ in range(64 * 64 * 3))
            other = bytearray(base)
            for y in range(24, 32):
                for x in range(16, 24):
                    for c in range(3):
                        other[(y * 64 + x) * 3 + c] += 40
            other = bytes(other)
            rep = compare_report((64, 64, bytes(base)), (64, 64, other), 8, (64, 36))
            ck.ok('block within exact', rep['within'] == 1 - 64 / 4096)
            ck.ok('block max_abs 40', rep['max_abs'] == 40)
            p_a = os.path.join(td, 'base.png')
            p_b = os.path.join(td, 'other.png')
            _write(p_a, encode_png(64, 64, bytes(base)))
            _write(p_b, encode_png(64, 64, other))
            with redirect_stdout(io.StringIO()):
                s1 = main(['compare', p_a, p_b, '--min-within', '0.99'])
                s2 = main(['compare', p_a, p_b, '--min-within', '0.98'])
            ck.ok('block min-within 0.99 fails (exit 1)', s1 == 1)
            ck.ok('block min-within 0.98 passes (exit 0)', s2 == 0)

            # 6. different sizes
            p_s1 = os.path.join(td, 's1.png')
            p_s2 = os.path.join(td, 's2.png')
            _write(p_s1, encode_png(10, 10, bytes(300)))
            _write(p_s2, encode_png(12, 10, bytes(360)))
            with redirect_stdout(io.StringIO()), _capture_err():
                ck.ok('different sizes exit 2', main(['compare', p_s1, p_s2]) == 2)

            # 7. stats
            n_w, n_h = 20, 20
            n = n_w * n_h
            first = (10, 20, 30)
            second = (200, 100, 50)
            buf = bytearray()
            for i in range(n):
                buf += bytes(second if i < 10 else first)
            st_img = (n_w, n_h, bytes(buf))
            rep = stats_report(st_img, first, 0)
            ck.ok('stats distinct 2', rep['distinct'] == 2)
            ck.ok('stats top_share', rep['top_share'] == (n - 10) / n)
            ck.ok('stats nonclear_share', rep['nonclear_share'] == 10 / n)
            p_st = os.path.join(td, 'st.png')
            _write(p_st, encode_png(n_w, n_h, st_img[2]))
            with redirect_stdout(io.StringIO()):
                ck.ok('stats command exit 0', main(['stats', p_st, '--clear', '10,20,30']) == 0)

            # 8. 16-bit and interlaced headers
            ok_png = encode_png(8, 8, bytes(8 * 8 * 3))
            p_ok = os.path.join(td, 'ok8.png')
            _write(p_ok, ok_png)
            p16 = os.path.join(td, 'd16.png')
            _write(p16, _enc_png(8, 8, bytes(8 * 8 * 3), 2, [0] * 8, depth=16))
            pil = os.path.join(td, 'interlaced.png')
            _write(pil, _enc_png(8, 8, bytes(8 * 8 * 3), 2, [0] * 8, interlace=1))
            with redirect_stdout(io.StringIO()), _capture_err():
                ck.ok('16-bit header is error', main(['compare', p_ok, p16]) == 2)
                ck.ok('interlaced header is error', main(['compare', p_ok, pil]) == 2)

            # 9. 1920x1080 RGB PNG with filter 4 on every row
            BW, BH = 1920, 1080
            big = rng.randbytes(BW * BH * 3)
            p_big = os.path.join(td, 'big.png')
            _write(p_big, _enc_png(BW, BH, big, 2, [4] * BH))
            t0 = time.time()
            got = load_image(p_big)
            elapsed = time.time() - t0
            ck.ok('1920x1080 round trip', got == (BW, BH, big))
            ck.ok('1920x1080 under 30 s', elapsed < 30)
            print("frame_compare selftest: 1920x1080 PNG (filter 4) read in %.2f s" % elapsed)

            # 10. diff
            p_same = os.path.join(td, 'same.png')
            _write(p_same, encode_png(64, 64, img[2]))
            p_dz = os.path.join(td, 'diff_self.png')
            with redirect_stdout(io.StringIO()) as buf_self:
                ck.ok('diff self exit 0', main(['diff', p_same, p_same, p_dz]) == 0)
            rep_self = json.loads(buf_self.getvalue())
            ck.ok('diff self differing 0', rep_self['differing'] == 0.0)
            ck.ok('diff self all top mean 0',
                  all(t['mean_abs'] == 0 for t in rep_self['top']))
            ck.ok('diff self OUT all black', load_image(p_dz) == (64, 64, bytes(64 * 64 * 3)))

            p_dblk = os.path.join(td, 'diff_block.png')
            with redirect_stdout(io.StringIO()) as buf_blk:
                ck.ok('diff block exit 0', main(['diff', p_a, p_b, p_dblk,
                                                 '--cells', '8x8', '--top', '1',
                                                 '--gain', '2']) == 0)
            rep_blk = json.loads(buf_blk.getvalue())
            ck.ok('diff block differing 64/4096', rep_blk['differing'] == 64 / 4096)
            top0 = rep_blk['top'][0]
            ck.ok('diff block top cell at block',
                  (top0['col'], top0['row'], top0['x'], top0['y'], top0['w'], top0['h'])
                  == (2, 3, 16, 24, 8, 8))
            ck.ok('diff block top mean_abs 40', top0['mean_abs'] == 40.0)
            ck.ok('diff block top max_abs 40', top0['max_abs'] == 40)
            ck.ok('diff block gain 2', rep_blk['gain'] == 2)
            dpx = load_image(p_dblk)
            ok_px = True
            for y in range(64):
                for x in range(64):
                    inside = 16 <= x < 24 and 24 <= y < 32
                    want = 80 if inside else 0
                    base_i = (y * 64 + x) * 3
                    if any(dpx[2][base_i + c] != want for c in range(3)):
                        ok_px = False
            ck.ok('diff block OUT 80 inside, 0 outside', ok_px)

            with redirect_stdout(io.StringIO()), _capture_err():
                ck.ok('diff different sizes exit 2', main(['diff', p_s1, p_s2, p_dz]) == 2)
        print("frame_compare selftest: %d checks passed" % ck.n)
        return 0
    except SelfTestFail as e:
        print("frame_compare selftest: FAILED check: %s" % e)
        return 1
    except Exception as e:
        print("frame_compare selftest: FAILED with error: %r" % (e,))
        return 1


class _capture_err:
    """Swallow stderr while error paths print their one line."""
    def __enter__(self):
        self._old = sys.stderr
        sys.stderr = io.StringIO()
        return self

    def __exit__(self, *exc):
        sys.stderr = self._old
        return False


if __name__ == '__main__':
    sys.exit(main())
