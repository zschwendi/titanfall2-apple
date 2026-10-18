/* unsnap: the bytes of an apitrace trace file with its compression taken off, on standard output.
 *
 *     cc -O2 -o build/tools/unsnap tools/trace/d3d/unsnap.c
 *     build/tools/unsnap FILE | ...          (tools/trace/d3d/tracefile.py builds and runs it)
 *
 * The container (written from memory of apitrace's trace_file_snappy.cpp and of Google's
 * format_description.txt for Snappy, both public; the proof that the memory is right is
 * tracefile.py's `check` on the real recording and its self-test, which holds this program
 * against a second decoder written in Python):
 *
 *     'a' 't'                                  two bytes
 *     chunk*                                   each: a 32-bit little-endian length, then that many
 *                                              bytes of one raw Snappy block
 *
 * A raw Snappy block: the length of the data it holds as a varint (7 bits a byte, low bits
 * first, the top bit says another byte follows), then elements.  The low two bits of an
 * element's first byte say what it is:
 *     0  a literal: the upper six bits are its length less one, or 60 to 63 for a length less
 *        one that follows in 1 to 4 bytes (little-endian); then the bytes themselves
 *     1  a copy: length 4 + bits 2 to 4, offset = bits 5 to 7 * 256 + the next byte
 *     2  a copy: length 1 + the upper six bits, offset = the next two bytes
 *     3  a copy: length 1 + the upper six bits, offset = the next four bytes
 * A copy takes bytes that lie `offset` back in what has been written and may run into the
 * bytes it writes itself (offset < length repeats a pattern), so it is done byte by byte then.
 *
 * Every length and offset is checked before it is used; a file that fails a check ends the
 * program with exit status 1 and a line that names the chunk and the check.  Nothing of the
 * file's contents is printed.
 *
 * A FILE THAT ENDS INSIDE ITS LAST CHUNK (a recorder that was killed: the real recording is
 * 615 bytes short of its last chunk's declared length) is not an error unless --strict is
 * given: the elements of that block that are whole are decoded and written, a line on standard
 * error says how many bytes that gave, and the exit status is 0.  A reader of the trace then
 * sees the stream end inside a call, as apitrace's own reader does.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(unsigned long long chunk, const char *what) {
    fprintf(stderr, "unsnap: chunk %llu: %s\n", chunk, what);
    exit(1);
}

int main(int argc, char **argv) {
    int strict = argc == 3 && !strcmp(argv[1], "--strict");
    if (argc != 2 + strict) { fprintf(stderr, "usage: unsnap [--strict] FILE\n"); return 2; }
    argv += strict;
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    unsigned char head[4];
    if (fread(head, 1, 2, f) != 2 || head[0] != 'a' || head[1] != 't') { fprintf(stderr, "unsnap: %s does not begin with \"at\"\n", argv[1]); return 1; }

    size_t incap = 1 << 21, outcap = 1 << 21;
    unsigned char *in = malloc(incap), *out = malloc(outcap);
    if (!in || !out) { fprintf(stderr, "unsnap: no memory\n"); return 2; }
    setvbuf(stdout, NULL, _IOFBF, 1 << 20);

    for (unsigned long long chunk = 0;; chunk++) {
        size_t got = fread(head, 1, 4, f);
        if (got == 0) break;
        if (got != 4) die(chunk, "the file ends inside a chunk's length");
        size_t n = (size_t)head[0] | (size_t)head[1] << 8 | (size_t)head[2] << 16 | (size_t)head[3] << 24;
        size_t declared = n;
        if (n > incap) { incap = n; in = realloc(in, incap); if (!in) die(chunk, "no memory"); }
        size_t have = fread(in, 1, n, f);
        int cut = have != n;                /* the file ends inside this chunk: see the head */
        if (cut && strict) die(chunk, "the file ends inside a chunk");
        if (cut) n = have;

        size_t i = 0, want = 0;
        for (unsigned shift = 0;; shift += 7) {
            if (i >= n || shift > 28) { if (cut) return 0; die(chunk, "the block's length does not end"); }
            unsigned char b = in[i++];
            want |= (size_t)(b & 0x7f) << shift;
            if (!(b & 0x80)) break;
        }
        if (want > outcap) { outcap = want; out = realloc(out, outcap); if (!out) die(chunk, "no memory"); }

        size_t o = 0;
        while (i < n) {
            unsigned tag = in[i++];
            size_t len, off;
            if (cut) {                      /* an element that is not whole ends the output */
                size_t need = (tag & 3) == 0 ? ((tag >> 2) >= 60 ? (tag >> 2) - 59 : 0) : (tag & 3) == 1 ? 1 : (tag & 3) == 2 ? 2 : 4;
                if (need > n - i) break;
                if ((tag & 3) == 0) {
                    size_t l = tag >> 2;
                    if (l >= 60) { l = 0; for (size_t j = 0; j < need; j++) l |= (size_t)in[i + j] << (8 * j); }
                    if (l + 1 > n - i - need) break;
                }
            }
            switch (tag & 3) {
            case 0:
                len = tag >> 2;
                if (len >= 60) {
                    unsigned k = (unsigned)len - 59;
                    if (i + k > n) die(chunk, "a literal's length runs past the block");
                    len = 0;
                    for (unsigned j = 0; j < k; j++) len |= (size_t)in[i + j] << (8 * j);
                    i += k;
                }
                len++;
                if (len > n - i) die(chunk, "a literal runs past the block");
                if (len > want - o) die(chunk, "a literal runs past the declared length");
                memcpy(out + o, in + i, len);
                i += len;
                o += len;
                continue;
            case 1:
                if (i + 1 > n) die(chunk, "a copy's offset runs past the block");
                len = 4 + ((tag >> 2) & 7);
                off = (size_t)(tag >> 5) << 8 | in[i];
                i += 1;
                break;
            case 2:
                if (i + 2 > n) die(chunk, "a copy's offset runs past the block");
                len = 1 + (tag >> 2);
                off = (size_t)in[i] | (size_t)in[i + 1] << 8;
                i += 2;
                break;
            default:
                if (i + 4 > n) die(chunk, "a copy's offset runs past the block");
                len = 1 + (tag >> 2);
                off = (size_t)in[i] | (size_t)in[i + 1] << 8 | (size_t)in[i + 2] << 16 | (size_t)in[i + 3] << 24;
                i += 4;
                break;
            }
            if (off == 0 || off > o) die(chunk, "a copy reaches before the block's first byte");
            if (len > want - o) die(chunk, "a copy runs past the declared length");
            if (off >= len) memcpy(out + o, out + o - off, len);
            else for (size_t j = 0; j < len; j++) out[o + j] = out[o + j - off];
            o += len;
        }
        if (o != want && !cut) die(chunk, "the block holds fewer bytes than it declares");
        if (fwrite(out, 1, o, stdout) != o) return 3;       /* the reader went away */
        if (cut) {
            fprintf(stderr, "unsnap: chunk %llu: the file ends inside it: %zu of %zu bytes of the block gave %zu of %zu bytes of data\n",
                    chunk, have, declared, o, want);
            break;
        }
    }
    if (fflush(stdout) != 0) return 3;
    return 0;
}
