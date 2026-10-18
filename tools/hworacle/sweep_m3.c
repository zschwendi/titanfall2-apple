/* sweep_m3.c - the arm64 side of the rcp/rsqrt sweep (tools/hworacle/sweep.py).
 *
 * Computes FRECPE / FRSQRTE on this CPU over ranges of binary32 bit patterns, with FPCR set the
 * way lifted code runs (harness/fpmode.h, tf_fp_enter_guest) or left at the host default, and
 * prints either a hash per chunk or the raw results.  The hash is the one the x86 guest program
 * in sweep.py computes over RCPSS / RSQRTSS:  h = (h ^ result) * K, starting from SEED.
 *
 *   sweep_m3 hash  <rcp|rsqrt> <mxcsr hex> <guest|host> <scalar|vector> <first chunk> <n chunks>
 *   sweep_m3 dump  <rcp|rsqrt> <mxcsr hex> <guest|host> <scalar|vector> <first input hex> <count>
 *   sweep_m3 check <rcp|rsqrt> <mxcsr hex> <guest|host> <scalar|vector> <first input hex> <count> <A|B> <file>
 *
 * `check` compares this CPU's results over a range with the x86 results, given as the run-length
 * records the guest program wrote (pairs of little-endian u32: input, result).  Kind A: the x86
 * result is constant from each record's input up to the next record's.  Kind B: result XOR input
 * is constant (NaN ranges).  Prints the number of differing inputs, a count per kind of input, and
 * the first 48 differences as "input arm x86".
 *
 * A chunk is the 2^23 inputs that share sign and exponent: chunk c covers c << 23 .. (c+1) << 23.
 * Build: clang -O2 -fno-vectorize -fno-slp-vectorize -I harness -o build/hworacle/sweep_m3 tools/hworacle/sweep_m3.c
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fpmode.h"

#define SWEEP_K    UINT64_C(0x9E3779B97F4A7C15)
#define SWEEP_SEED UINT64_C(0x0123456789ABCDEF)

/* The instructions themselves, so that nothing is folded or replaced by the compiler. */
static inline uint32_t rcp_scalar(uint32_t x)
{
    uint32_t r;
    __asm__("fmov s1, %w1\n\tfrecpe s0, s1\n\tfmov %w0, s0" : "=r"(r) : "r"(x) : "v0", "v1");
    return r;
}

static inline uint32_t rsqrt_scalar(uint32_t x)
{
    uint32_t r;
    __asm__("fmov s1, %w1\n\tfrsqrte s0, s1\n\tfmov %w0, s0" : "=r"(r) : "r"(x) : "v0", "v1");
    return r;
}

static inline void rcp_vector(const uint32_t in[4], uint32_t out[4])
{
    __asm__("ldr q1, [%1]\n\tfrecpe v0.4s, v1.4s\n\tstr q0, [%0]" : : "r"(out), "r"(in) : "v0", "v1", "memory");
}

static inline void rsqrt_vector(const uint32_t in[4], uint32_t out[4])
{
    __asm__("ldr q1, [%1]\n\tfrsqrte v0.4s, v1.4s\n\tstr q0, [%0]" : : "r"(out), "r"(in) : "v0", "v1", "memory");
}

/* results for inputs first .. first + n - 1 (n a multiple of 4), FPCR already set by the caller */
static void __attribute__((noinline)) compute(int rsqrt, int vector, uint32_t first, uint32_t n, uint32_t *out)
{
    if (vector) {
        for (uint32_t i = 0; i < n; i += 4) {
            uint32_t in[4] = { first + i, first + i + 1, first + i + 2, first + i + 3 };
            if (rsqrt) rsqrt_vector(in, out + i); else rcp_vector(in, out + i);
        }
    } else {
        for (uint32_t i = 0; i < n; i++) out[i] = rsqrt ? rsqrt_scalar(first + i) : rcp_scalar(first + i);
    }
}

static const char *kind_of(uint32_t x)
{
    uint32_t e = (x >> 23) & 0xFF, m = x & 0x7FFFFF;
    return e == 0xFF ? (m ? "nan" : "inf") : e == 0 ? (m ? "denormal" : "zero") : "normal";
}

static int check(int rsqrt, uint32_t mxcsr, int guest, int vector, uint32_t first, uint64_t count, int kind_b, const char *path)
{
    static uint32_t buf[1 << 16];
    static const char *names[5] = { "zero", "denormal", "normal", "inf", "nan" };
    uint64_t by_kind[5] = { 0 }, diffs = 0, shown = 0;
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n_rec = (size_t)size / 8, k = 0;
    uint32_t *rec = malloc(size > 0 ? (size_t)size : 8);
    if (!rec || fread(rec, 8, n_rec, f) != n_rec || n_rec == 0 || rec[0] != first) {
        fprintf(stderr, "sweep_m3 check: bad record file (the first record must be for the first input)\n");
        return 2;
    }
    fclose(f);
    for (uint64_t done = 0; done < count;) {
        uint32_t n = count - done > (1 << 16) ? (1 << 16) : (uint32_t)(count - done);
        if (guest) tf_fp_enter_guest(mxcsr);
        compute(rsqrt, vector, first + (uint32_t)done, (n + 3) & ~3u, buf);
        tf_fp_enter_host();
        for (uint32_t i = 0; i < n; i++) {
            uint32_t x = first + (uint32_t)done + i;
            while (k + 1 < n_rec && rec[2 * (k + 1)] <= x && rec[2 * (k + 1)] > rec[2 * k]) k++;
            uint32_t want = kind_b ? x ^ rec[2 * k] ^ rec[2 * k + 1] : rec[2 * k + 1];
            if (buf[i] != want) {
                const char *kd = kind_of(x);
                for (int j = 0; j < 5; j++) if (!strcmp(kd, names[j])) by_kind[j]++;
                if (shown++ < 48) printf("diff %08x %08x %08x\n", x, buf[i], want);
                diffs++;
            }
        }
        done += n;
    }
    printf("total %llu", (unsigned long long)diffs);
    for (int j = 0; j < 5; j++) printf(" %s %llu", names[j], (unsigned long long)by_kind[j]);
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 10 && !strcmp(argv[1], "check"))
        return check(!strcmp(argv[2], "rsqrt"), (uint32_t)strtoul(argv[3], NULL, 16), !strcmp(argv[4], "guest"),
                     !strcmp(argv[5], "vector"), (uint32_t)strtoul(argv[6], NULL, 16), strtoull(argv[7], NULL, 0),
                     !strcmp(argv[8], "B"), argv[9]);
    if (argc != 8) {
        fprintf(stderr, "usage: sweep_m3 hash|dump|check rcp|rsqrt <mxcsr hex> guest|host scalar|vector <first> <count> [A|B file]\n");
        return 2;
    }
    int dump = !strcmp(argv[1], "dump"), rsqrt = !strcmp(argv[2], "rsqrt");
    uint32_t mxcsr = (uint32_t)strtoul(argv[3], NULL, 16);
    int guest = !strcmp(argv[4], "guest"), vector = !strcmp(argv[5], "vector");
    static uint32_t buf[1 << 16];
    if (dump) {
        uint32_t first = (uint32_t)strtoul(argv[6], NULL, 16);
        uint64_t count = strtoull(argv[7], NULL, 0);
        for (uint64_t done = 0; done < count;) {
            uint32_t n = count - done > (1 << 16) ? (1 << 16) : (uint32_t)(count - done);
            uint32_t n4 = (n + 3) & ~3u;
            if (guest) tf_fp_enter_guest(mxcsr);
            compute(rsqrt, vector, first + (uint32_t)done, n4, buf);
            tf_fp_enter_host();
            for (uint32_t i = 0; i < n; i++) printf("%08x\n", buf[i]);
            done += n;
        }
        return 0;
    }
    unsigned long c0 = strtoul(argv[6], NULL, 0), nc = strtoul(argv[7], NULL, 0);
    for (unsigned long c = c0; c < c0 + nc && c < 512; c++) {
        uint64_t h = SWEEP_SEED;
        for (uint32_t off = 0; off < (1u << 23); off += 1 << 16) {
            if (guest) tf_fp_enter_guest(mxcsr);
            compute(rsqrt, vector, (uint32_t)(c << 23) + off, 1 << 16, buf);
            tf_fp_enter_host();
            for (uint32_t i = 0; i < (1 << 16); i++) h = (h ^ buf[i]) * SWEEP_K;
        }
        printf("%lu %016llx\n", c, (unsigned long long)h);
    }
    return 0;
}
