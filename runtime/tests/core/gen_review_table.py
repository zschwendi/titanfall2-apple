#!/usr/bin/env python3
"""Generate runtime/tests/core/nulldev_review_table.c from the reviewer's format table.

Source: docs/reviews/d3d-format-table.md.
  section 1, every DXGI_FORMAT: the element's bytes and block size (or the reason there is none);
  section 3, the worked numbers: row pitch, depth pitch and bytes of every level of each table.
Every number in the generated C is parsed from that markdown; none is typed here.

Run from anywhere:   .venv/bin/python runtime/tests/core/gen_review_table.py
Other paths:         --src <markdown>  --out <C file>

The script stops with an error (and writes nothing) on any table shape it does not know, on a
cell it cannot read, or on a count that disagrees with the document's own count.  Standard
library only.
"""

import argparse
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..', '..'))
DEFAULT_SRC = os.path.join(ROOT, 'docs', 'reviews', 'd3d-format-table.md')
DEFAULT_OUT = os.path.join(HERE, 'nulldev_review_table.c')

S1_HEADER = ['value', 'name', 'element', 'MS', 'DXTex bits', 'Wine', 'planar / video', 'typeless', 'depth', 'status']
NUM_COLUMNS = {'subresource', 'level', 'width', 'height', 'depth', 'row pitch', 'depth pitch', 'bytes'}
# columns that are indices, names or layout offsets: no device call returns them, so they are not compared
SKIPPED_COLUMNS = {'array slice (face)', 'offset, packed', 'offset, each start rounded up to 16'}
DASH = '—'
# the review's own word for the kind of element it gives no plain or block size to
KIND_TEXT = {'bit': 'bit', 'pair': '2x1 pair', 'planar': 'planar', 'unknown': 'unknown',
             'video': 'video or palette (size from DXTex only)'}


class ParseError(Exception):
    pass


def fail(msg):
    raise ParseError(msg)


class Table:
    def __init__(self, heading, heading_line, line, header, rows):
        self.heading = heading              # nearest heading above the table ('' if none)
        self.heading_line = heading_line    # its 1-based line (0 if none)
        self.line = line                    # 1-based line of the header row
        self.header = header                # header cell texts
        self.rows = rows                    # [(1-based line, cell texts)], separator excluded


def cells_of(text, lineno):
    t = text.strip()
    if not (t.startswith('|') and t.endswith('|')):
        fail(f'line {lineno}: not a table row: {text[:80]!r}')
    return [c.strip() for c in t[1:-1].split('|')]


def is_separator(text):
    return re.fullmatch(r'\|(\s*:?-{3,}:?\s*\|)+', text.strip()) is not None


def find_tables(lines, first, last):
    """Every markdown table in lines[first:last].  A line that starts with '|' and is not part
    of a table is an error: it would otherwise be skipped without a trace."""
    tables, used = [], set()
    heading, heading_line = '', 0
    i = first
    while i < last:
        text = lines[i]
        if text.startswith('#'):
            heading, heading_line = text, i + 1
            i += 1
            continue
        if text.startswith('|'):
            if i + 1 >= last or not is_separator(lines[i + 1]):
                fail(f'line {i + 1}: a table row with no separator line under it')
            header = cells_of(text, i + 1)
            used.add(i)
            used.add(i + 1)
            rows = []
            j = i + 2
            while j < last and lines[j].startswith('|'):
                cells = cells_of(lines[j], j + 1)
                if len(cells) != len(header):
                    fail(f'line {j + 1}: {len(cells)} cells; the header on line {i + 1} has {len(header)}')
                rows.append((j + 1, cells))
                used.add(j)
                j += 1
            tables.append(Table(heading, heading_line, i + 1, header, rows))
            i = j
            continue
        i += 1
    for k in range(first, last):
        if lines[k].startswith('|') and k not in used:
            fail(f'line {k + 1}: stray table line')
    return tables


def section(lines, number):
    starts = [i for i, t in enumerate(lines) if re.match(r'## %d\. ' % number, t)]
    if len(starts) != 1:
        fail(f'section {number}: {len(starts)} headings match')
    first = starts[0] + 1
    last = next((i for i in range(first, len(lines)) if lines[i].startswith('## ')), len(lines))
    return first, last


def cell_num(text, where):
    if not re.fullmatch(r'\d+', text):
        fail(f'{where}: expected a whole number, got {text!r}')
    return int(text)


def cell_pair(text, where):
    m = re.fullmatch(r'(\d+) / (\d+)', text)
    if not m:
        fail(f'{where}: expected "row / bytes", got {text!r}')
    return int(m.group(1)), int(m.group(2))


# ---- section 1 ------------------------------------------------------------------------------------

def element_of(text, lineno):
    """The element column -> (kind, bytes, block).  kind 'plain' and 'block' are the first array;
    'bit', 'pair', 'planar' and 'unknown' are the second."""
    m = re.fullmatch(r'(\d+) bit \((\d+) B\)', text)
    if m:
        bits, nbytes = int(m.group(1)), int(m.group(2))
        if bits != 8 * nbytes:
            fail(f'line {lineno}: element {text!r}: {bits} bits is not {nbytes} bytes')
        return 'plain', nbytes, 1
    m = re.fullmatch(r'4x4 / (\d+) B', text)
    if m:
        return 'block', int(m.group(1)), 4
    m = re.fullmatch(r'2x1 / (\d+) B', text)
    if m:
        return 'pair', int(m.group(1)), 0
    if re.fullmatch(r'1 bit \(8x1 / 1 B\)', text):
        return 'bit', 0, 0
    if text.startswith('planar:'):
        return 'planar', 0, 0
    if text == DASH:
        return 'unknown', 0, 0
    fail(f'line {lineno}: element {text!r} is not a shape this script knows')


def parse_section1(lines, first, last, text):
    m = re.search(r'point 1, (\d+) formats', text)
    if not m:
        fail('the document does not state its count of formats in point 1')
    expected = int(m.group(1))
    tables = find_tables(lines, first, last)
    if len(tables) != 1:
        fail(f'section 1: {len(tables)} tables, expected 1')
    t = tables[0]
    if t.header != S1_HEADER:
        fail(f'section 1: header {t.header} is not {S1_HEADER}')
    rows = []
    for lineno, cells in t.rows:
        m = re.fullmatch(r'0x([0-9a-fA-F]+) \((\d+)\)', cells[0])
        if not m or int(m.group(1), 16) != int(m.group(2)):
            fail(f'line {lineno}: value {cells[0]!r}')
        value = int(m.group(2))
        m = re.fullmatch(r'`([A-Za-z0-9_]+)`', cells[1])
        if not m:
            fail(f'line {lineno}: name {cells[1]!r}')
        kind, nbytes, block = element_of(cells[2], lineno)
        # The seven video and palette formats (AYUV, Y410, Y416, AI44, IA44, P8, A8P8): the review's
        # element size for them is DXTex's alone (its status column says so; Microsoft's page states
        # no size) and the device has none on purpose: a format past 99 does not exist at feature
        # level 11_0.  They are compared as "the device has no size", not as a size.
        if kind == 'plain' and cells[6] == 'video' and 'DXTex only' in cells[9]:
            kind = 'video'
        rows.append({'value': value, 'name': m.group(1), 'element': cells[2], 'kind': kind,
                     'text': KIND_TEXT.get(kind, kind), 'bytes': nbytes, 'block': block, 'line': lineno})
    if len(rows) != expected:
        fail(f'section 1: {len(rows)} rows, the document says {expected}')
    if len({r['value'] for r in rows}) != len(rows):
        fail('section 1: a DXGI_FORMAT value occurs twice')
    return rows, t


# ---- section 3 ------------------------------------------------------------------------------------

def heading_size(t):
    m = re.search(r'(\d+) by (\d+)(?: by (\d+))?', t.heading)
    if not m:
        fail(f'line {t.heading_line}: no size in heading {t.heading!r}')
    return int(m.group(1)), int(m.group(2)), int(m.group(3)) if m.group(3) else 1


def heading_format(t):
    m = re.search(r'`([A-Z0-9_]+)` \((\d+)\)', t.heading)
    return (m.group(1), int(m.group(2))) if m else None


def heading_levels(t):
    m = re.search(r'(\d+) levels?\b', t.heading)
    return int(m.group(1)) if m else None


def heading_array(t):
    m = re.search(r'`ArraySize` (\d+)', t.heading)
    return int(m.group(1)) if m else None


def parse_section3(lines, first, last):
    tables = find_tables(lines, first, last)
    if not tables:
        fail('section 3: no tables')
    records, summary, totals = [], [], []
    for ti, t in enumerate(tables):
        if 'level' not in t.header:
            fail(f'line {t.line}: a table without a level column: {t.header}')
        cols = []
        for h in t.header:
            if h in NUM_COLUMNS:
                cols.append(('num', h, None))
            elif h in SKIPPED_COLUMNS:
                cols.append(('skip', h, None))
            else:
                m = re.fullmatch(r'(\S+) \((\d+)\) row pitch / bytes', h)
                if not m:
                    fail(f'line {t.line}: unknown column {h!r}')
                cols.append(('fmt', m.group(1), int(m.group(2))))
        has_fmt_cols = any(c[0] == 'fmt' for c in cols)
        hfmt = heading_format(t)
        if not has_fmt_cols and hfmt is None:
            fail(f'line {t.line}: no format in the columns or the heading')
        w0, h0, d0 = heading_size(t)
        stated_levels = heading_levels(t)
        stated_slices = heading_array(t)
        if 'depth' not in t.header and d0 != 1:
            fail(f'line {t.line}: heading gives depth {d0} but the table has no depth column')

        rows = []
        for lineno, cells in t.rows:
            if re.fullmatch(r'\*\*(total|end)\*\*', cells[0]):
                totals.append(lineno)
                continue
            vals, items = {}, []
            for idx, (kind, name, num) in enumerate(cols):
                where = f'line {lineno}, column {t.header[idx]!r}'
                if kind == 'num':
                    vals[name] = cell_num(cells[idx], where)
                elif kind == 'fmt':
                    rp, nb = cell_pair(cells[idx], where)
                    items.append((num, name, rp, nb))
            for need in ('level', 'width', 'height'):
                if need not in vals:
                    fail(f'line {lineno}: no {need} column')
            if not items:
                if 'row pitch' not in vals or 'bytes' not in vals:
                    fail(f'line {lineno}: no row pitch or bytes column')
                items = [(hfmt[1], hfmt[0], vals['row pitch'], vals['bytes'])]
            rows.append({'line': lineno, 'level': vals['level'], 'sub': vals.get('subresource'),
                         'width': vals['width'], 'height': vals['height'], 'depth': vals.get('depth', 1),
                         'depth_pitch': vals.get('depth pitch', 0), 'items': items})

        groups = []
        for r in rows:
            if r['level'] == 0:
                groups.append([])
            if not groups or r['level'] != len(groups[-1]):
                fail(f'line {r["line"]}: level {r["level"]} where the chain expects level '
                     f'{len(groups[-1]) if groups else 0}')
            groups[-1].append(r)
        for g in groups:
            r0 = g[0]
            if (r0['width'], r0['height'], r0['depth']) != (w0, h0, d0):
                fail(f'line {r0["line"]}: level 0 is {r0["width"]} by {r0["height"]} by {r0["depth"]}; '
                     f'the heading says {w0} by {h0} by {d0}')
            if stated_levels is not None and len(g) != stated_levels:
                fail(f'line {r0["line"]}: {len(g)} levels, the heading says {stated_levels}')
        if len({len(g) for g in groups}) != 1:
            fail(f'line {t.line}: chains of different lengths')
        if stated_slices is not None and len(groups) != stated_slices:
            fail(f'line {t.line}: {len(groups)} slices, the heading says {stated_slices}')
        if any(c[1] == 'subresource' for c in cols):
            for k, r in enumerate(rows):
                if r['sub'] != k:
                    fail(f'line {r["line"]}: subresource {r["sub"]} where {k} is expected')

        count = 0
        names = []
        for r in rows:
            for num, name, rp, nb in r['items']:
                records.append((ti, r['line'], num, name, w0, h0, d0, r['level'], r['width'], r['height'],
                                r['depth'], rp, r['depth_pitch'], nb))
                count += 1
                if name not in names:
                    names.append(name)
        summary.append((t.heading_line, t.heading, len(rows), count, names))
    return records, summary, totals, tables


# ---- C output -------------------------------------------------------------------------------------

def c_str(s, where):
    if not s.isascii():
        fail(f'{where}: non-ASCII text {s!r}')
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


def comment(s):
    return s.replace('*/', '* /').replace('/*', '/ *')


def render(s1_rows, other_rows, records, tables):
    out = []
    w = out.append
    w('/* GENERATED by runtime/tests/core/gen_review_table.py from docs/reviews/d3d-format-table.md:\n')
    w(' * section 1 (the element of every DXGI_FORMAT) and section 3 (the worked numbers).  Do not edit\n')
    w(' * by hand: run  .venv/bin/python runtime/tests/core/gen_review_table.py  and rebuild.\n')
    w(' *\n')
    w(' * The reviewer\'s numbers against the do-nothing device: rt_nulldev_format_bytes, rt_nulldev_level\n')
    w(' * (runtime/nulldev/memory.c).  No check stops at the first difference: each mismatch is printed on\n')
    w(' * one line, "md N" being the line of the markdown table it comes from, and each case ends with one\n')
    w(' * T_EQ on the count.  Where a table has no depth pitch column (every 2D table) depth_pitch is 0. */\n')
    w('#include <stdio.h>\n\n')
    w('#include "rt/test.h"\n')
    w('#include "rt/nulldev.h"\n\n')

    w('/* section 1: the elements a plain pixel or a 4x4 block of whole bytes gives */\n')
    w('struct rv_format {\n')
    w('    unsigned value;             /* DXGI_FORMAT */\n')
    w('    unsigned element_bytes;     /* the review\'s bytes per pixel, or per block */\n')
    w('    unsigned block;             /* 1, or 4 for a 4x4 block */\n')
    w('    const char *name;\n')
    w('    unsigned md_line;\n')
    w('};\n')
    w('static const struct rv_format rv_formats[] = {\n')
    for r in s1_rows:
        if r['kind'] in ('plain', 'block'):
            w(f'    {{ {r["value"]}, {r["bytes"]}, {r["block"]}, {c_str(r["name"], "name")}, {r["line"]} }},\n')
    w('};\n\n')

    w('/* section 1: the values the review gives no plain or block size: kind is the review\'s reason */\n')
    w('struct rv_other {\n')
    w('    unsigned value;\n')
    w('    const char *kind;           /* "bit", "2x1 pair", "planar" or "unknown" */\n')
    w('    const char *name;\n')
    w('    unsigned md_line;\n')
    w('};\n')
    w('static const struct rv_other rv_other[] = {\n')
    for r in other_rows:
        w(f'    {{ {r["value"]}, {c_str(r["text"], "kind")}, {c_str(r["name"], "name")}, {r["line"]} }},\n')
    w('};\n\n')

    w('/* section 3: one cell of a worked table, one level of one format.  The table\'s "bytes" is the\n')
    w(' * whole level (all depth slices); its "depth pitch" is one slice.  Level 0 is width0 by height0\n')
    w(' * by depth0. */\n')
    w('struct rv_level {\n')
    w('    unsigned format;            /* DXGI_FORMAT */\n')
    w('    const char *name;\n')
    w('    unsigned width0, height0, depth0;\n')
    w('    unsigned level;\n')
    w('    unsigned width, height, depth;      /* of this level */\n')
    w('    unsigned row_pitch;         /* the table\'s "row pitch" */\n')
    w('    unsigned depth_pitch;       /* the table\'s "depth pitch": 0 where the table has no such column */\n')
    w('    unsigned long long bytes;   /* the table\'s "bytes" */\n')
    w('    unsigned md_line;\n')
    w('};\n')
    w('static const struct rv_level rv_levels[] = {\n')
    last_table = None
    for (ti, line, num, name, w0, h0, d0, level, width, height, depth, rp, dp, nb) in records:
        if ti != last_table:
            heading, heading_line = tables[ti]
            w(f'    /* md {heading_line}: {comment(heading.lstrip("#").strip())} */\n')
            last_table = ti
        w(f'    {{ {num}, {c_str(name, "name")}, {w0}, {h0}, {d0}, {level}, {width}, {height}, {depth}, '
          f'{rp}, {dp}, {nb}ULL, {line} }},\n')
    w('};\n\n')
    return ''.join(out)


TEST_BODY = r'''
/* prints "N", or "-" where the table has no such column (0) */
static void num_or_dash(char *buf, unsigned long n, unsigned v)
{
    if (v) snprintf(buf, n, "%u", v);
    else snprintf(buf, n, "-");
}

TEST(core, nulldev_review_table)
{
    uint32_t bytes, block;
    unsigned mismatches;

    T_CASE("section 1: element sizes");
    mismatches = 0;
    for (unsigned i = 0; i < sizeof rv_formats / sizeof rv_formats[0]; i++) {
        const struct rv_format *r = &rv_formats[i];
        bytes = block = 0;
        bool ok = rt_nulldev_format_bytes((DXGI_FORMAT)r->value, &bytes, &block);
        if (!ok || bytes != r->element_bytes || block != r->block) {
            mismatches++;
            if (ok)
                printf("section 1 md %u: %s (%u): review %u bytes, block %u; device %u bytes, block %u\n",
                       r->md_line, r->name, r->value, r->element_bytes, r->block, (unsigned)bytes, (unsigned)block);
            else
                printf("section 1 md %u: %s (%u): review %u bytes, block %u; device: no size\n",
                       r->md_line, r->name, r->value, r->element_bytes, r->block);
        }
    }
    T_EQ(mismatches, 0);

    T_CASE("section 1: formats the review gives no plain or block size");
    mismatches = 0;
    for (unsigned i = 0; i < sizeof rv_other / sizeof rv_other[0]; i++) {
        const struct rv_other *r = &rv_other[i];
        bytes = block = 0;
        bool ok = rt_nulldev_format_bytes((DXGI_FORMAT)r->value, &bytes, &block);
        if (ok) {
            mismatches++;
            printf("section 1 no-size md %u: %s (%u) %s: device returns true, %u bytes, block %u\n",
                   r->md_line, r->name, r->value, r->kind, (unsigned)bytes, (unsigned)block);
        } else {
            printf("section 1 no-size md %u: %s (%u) %s: device returns false\n",
                   r->md_line, r->name, r->value, r->kind);
        }
    }
    T_EQ(mismatches, 0);

    T_CASE("section 3: worked numbers");
    mismatches = 0;
    for (unsigned i = 0; i < sizeof rv_levels / sizeof rv_levels[0]; i++) {
        const struct rv_level *r = &rv_levels[i];
        NdLevel l = { 0 };
        bool ok = rt_nulldev_level((DXGI_FORMAT)r->format, r->width0, r->height0, r->depth0, r->level, &l);
        bool same = ok && l.width == r->width && l.height == r->height && l.depth == r->depth &&
                    l.row_bytes == r->row_pitch && l.bytes == r->bytes &&
                    (r->depth_pitch == 0 || l.slice_bytes == r->depth_pitch);
        if (same) continue;
        mismatches++;
        char dp[24];
        num_or_dash(dp, sizeof dp, r->depth_pitch);
        if (!ok)
            printf("section 3 md %u: %s (%u) %ux%ux%u level %u: review %ux%ux%u, row %u, depth pitch %s, bytes %llu; "
                   "device: no size\n", r->md_line, r->name, r->format, r->width0, r->height0, r->depth0, r->level,
                   r->width, r->height, r->depth, r->row_pitch, dp, r->bytes);
        else
            printf("section 3 md %u: %s (%u) %ux%ux%u level %u: review %ux%ux%u, row %u, depth pitch %s, bytes %llu; "
                   "device %ux%ux%u, row %u, depth pitch %llu, bytes %llu\n",
                   r->md_line, r->name, r->format, r->width0, r->height0, r->depth0, r->level,
                   r->width, r->height, r->depth, r->row_pitch, dp, r->bytes,
                   (unsigned)l.width, (unsigned)l.height, (unsigned)l.depth, (unsigned)l.row_bytes,
                   (unsigned long long)l.slice_bytes, (unsigned long long)l.bytes);
    }
    T_EQ(mismatches, 0);
}
'''


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--src', default=DEFAULT_SRC)
    ap.add_argument('--out', default=DEFAULT_OUT)
    args = ap.parse_args(argv)

    with open(args.src, encoding='utf-8') as f:
        text = f.read()
    lines = text.split('\n')

    s1_first, s1_last = section(lines, 1)
    s1_rows, s1_table = parse_section1(lines, s1_first, s1_last, text)
    s3_first, s3_last = section(lines, 3)
    records, summary, totals, s3_tables = parse_section3(lines, s3_first, s3_last)

    s1_plain = [r for r in s1_rows if r['kind'] in ('plain', 'block')]
    s1_other = [r for r in s1_rows if r['kind'] not in ('plain', 'block')]
    kinds = {}
    for r in s1_other:
        kinds[r['text']] = kinds.get(r['text'], 0) + 1
    tables = [(t.heading, t.heading_line) for t in s3_tables]

    # the file is written only when every part parsed
    body = render(s1_rows, s1_other, records, tables) + TEST_BODY
    body = body.replace('\n\n\n', '\n\n')
    if not body.endswith('\n'):
        body += '\n'
    with open(args.out, 'w', encoding='utf-8', newline='\n') as f:
        f.write(body)

    print(f'section 1 (table at line {s1_table.line}): {len(s1_rows)} rows; {len(s1_plain)} with a plain or block '
          f'size; {len(s1_other)} without: ' + ', '.join(f'{k} {v}' for k, v in sorted(kinds.items())))
    print(f'section 3: {len(s3_tables)} tables, {len(records)} level cells (each: row pitch and bytes); '
          f'{len(totals)} totals rows, not compared')
    for ln, heading, nrows, ncells, names in summary:
        print(f'  md {ln}: {heading}  [{nrows} levels, {ncells} cells, {", ".join(names)}]')
    print(f'wrote {os.path.relpath(args.out, ROOT)}')
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except ParseError as e:
        print(f'gen_review_table: {e}', file=sys.stderr)
        sys.exit(1)
