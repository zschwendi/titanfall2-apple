/* `make ref-libc-test`: ref_libc.c, compiled natively with its names prefixed ref_, against
 * this Mac's C library.  The Windows reference program computes with ref_libc.c what the
 * Gate 2 driver computes with the system library, so the two must agree: every format the
 * call tables give to snprintf, with their values and with random ones; gmtime_r, timegm and
 * asctime_r over the C runtime's whole range; powf on the random stream's domain within one
 * unit in the last place (the tolerance rank 60 has); the string functions on random text.
 * Exit 0 when nothing differs.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int    ref_vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int    ref_snprintf(char *buf, size_t cap, const char *fmt, ...);
int    ref_sscanf(const char *s, const char *fmt, ...);
float  ref_powf(float x, float y);
double ref_sqrt(double x), ref_fabs(double x), ref_ceil(double x), ref_floor(double x);
struct tm *ref_gmtime_r(const time_t *t, struct tm *out);
time_t ref_timegm(struct tm *tm);
char  *ref_asctime_r(const struct tm *tm, char *buf);
int    ref_atoi(const char *s);
size_t ref_strlen(const char *s), ref_strnlen(const char *s, size_t n), ref_strspn(const char *s, const char *set), ref_strcspn(const char *s, const char *set);
int    ref_strcmp(const char *a, const char *b), ref_strncmp(const char *a, const char *b, size_t n), ref_memcmp(const void *a, const void *b, size_t n);
char  *ref_strstr(const char *h, const char *n), *ref_strchr(const char *s, int c);
void  *ref_memmove(void *d, const void *s, size_t n);

static unsigned long nfail, ncase, nquirk;
static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

static void same(const char *fmt, ...) {
    char a[1400], b[1400];
    va_list ap;
    va_start(ap, fmt); int na = vsnprintf(a, sizeof a, fmt, ap); va_end(ap);
    va_start(ap, fmt); int nb = ref_vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    ncase++;
    if (na == nb && strcmp(a, b) == 0) return;
    /* One known difference, and it is the system's: for %g without #, when the value is an exact
     * tie at the last kept digit and rounds to a digit followed by a zero, this Mac's library
     * keeps that zero ("%.2g" of 905 gives "9.0e+02"; the C standard removes trailing zeros, and
     * the same library gives "9e+02" for 904 and 906).  The call tables avoid ties (rank 52's
     * comment).  Counted apart when removing the zeros from the system's text gives ours. */
    char last = fmt[strlen(fmt) - 1];
    if ((last == 'g' || last == 'G') && !strchr(fmt, '#')) {
        char t[1400];
        size_t e = strcspn(a, "eE"), end = e, k;
        if (memchr(a, '.', e)) { while (end > 0 && a[end - 1] == '0') end--; if (end > 0 && a[end - 1] == '.') end--; }
        memcpy(t, a, end);
        strcpy(t + end, a + e);
        for (k = 0; t[k] == ' '; k++) { }                                             /* the padding changes with the length */
        const char *o = b;
        while (*o == ' ') o++;
        size_t lt = strlen(t), lo = strlen(o);
        while (lt > k && t[lt - 1] == ' ') lt--;
        while (lo > 0 && o[lo - 1] == ' ') lo--;
        if (lt - k == lo && memcmp(t + k, o, lo) == 0) { nquirk++; return; }
    }
    if (nfail++ < 25) printf("DIFFERENT \"%s\": system \"%s\" (%d), ours \"%s\" (%d)\n", fmt, a, na, b, nb);
}

static double random_double(void) {
    switch (rnd() % 6) {
    case 0: { union { uint64_t u; double d; } v = { rnd() }; return v.d != v.d || v.d - v.d != 0 ? 1.5 : v.d; }  /* any finite bit pattern */
    case 1: return (double)(int64_t)(rnd() % 2000001) / 1000.0 - 1000.0;                 /* three decimals */
    case 2: return (double)(rnd() % 100000) / 8.0;                                        /* ties for %.2f and below */
    case 3: return ldexp((double)(rnd() >> 11), (int)(rnd() % 200) - 150);
    case 4: return (double)(int64_t)(rnd() % 2001 - 1000) * pow(10.0, (double)(rnd() % 40) - 20.0);
    default: return (double)(rnd() % 1000) + 0.5;                                         /* ties for %.0f */
    }
}

int main(void) {
    /* The formats of gate2/calls_wide.c ranks 51, 52, 84 and of the tables' own messages. */
    static const struct { const char *fmt; long long a, b, d; } ints[] = {
        { "%d|%5d|%-5d|%05d\n", 42, 42, -42 }, { "%+d|% d|%.3d\n", 7, 7, 7 }, { "%x|%X|%#x\n", 0xbeef, 0xBEEF, 255 },
        { "%08x|%o|%i\n", 0x1234, 8, -2147483647 - 1 }, { "%u|%u|%c\n", -1, 0, 'Z' }, { "%3c|%-3c|%%\n", 'a', 'b', 0 },
        { "%hd|%hu|%d\n", 70000, 70000, -3 }, { "%-+6d|%+06d|%#o\n", 42, 42, 8 }, { "%#X|%x|%.0d\n", 0xabc, 0, 0 },
        { "%#o|%#x|%5.3d\n", 0, 0, 7 }, { "%-8d|%08d|% 05d\n", -7, -7, 7 }, { "%hhd|%hhu|%#.5x\n", 300, 300, 0xab } };
    for (unsigned i = 0; i < sizeof ints / sizeof ints[0]; i++) same(ints[i].fmt, (int)ints[i].a, (int)ints[i].b, (int)ints[i].d);
    same("%lld|%llu\n", -9000000000000000000LL, -1LL);
    same("%llx|%20lld\n", 0x123456789abcdef0LL, 1234567890123LL);
    same("%ld|%lu|%zu|%#lx\n", -3L, 4000000000UL, (size_t)123456789012ull, 255UL);
    same("%*d|%-*d|\n", 6, 12, 4, 12);
    same("%s|%8s|%-8s|%.2s|%.*s\n", "abc", "abc", "abc", "abc", 1, "xyz");
    same("%-+6d|%+06d|%#o|%#X|%x|%-8.3s|%8.3s|\n", 42, 42, 8, 0xabc, 0u, "abcdef", "abcdef");
    same("%s %.3f %lld %c %5.1f%%\n", "w", 1.0627, 123456789012345LL, 'q', 99.5);
    same("%d %.3f %s %d %.2f %.1f\n", 7, 0.125, "mid", -8, 99.5, -0.26);
    same("%s: got %#llx (%lld), expected %#llx (%lld)", "what", 0xdeadbeefcafeull, -5LL, 0ull, 0LL);
    same("%p|%p|%20p", (void *)0x1234abcd, (void *)0, (void *)0x10);
    static const struct { const char *fmt; double a, b, d; } dbl[] = {
        { "%f|%f|%f\n", 3.14159265, -0.5, 0.0 }, { "%.2f|%.1f|%.0f\n", 3.14159265, 1234.5678, 2.7 },
        { "%10.3f|%-10.3f|%+.3f\n", 2.71828, 2.71828, 2.71828 }, { "%e|%E|%.3e\n", 1234.5678, 0.00012345, -6.02214e23 },
        { "%g|%g|%g\n", 100000.0, 1000000.0, 0.0001 }, { "%g|%G|%.3g\n", 0.00001234, 1.5e-10, 1234567.0 },
        { "%f|%.3f|%g\n", 123456789.125, 1e15, 1e15 }, { "%.10f|%.6e|%g\n", 0.1, 0.1, 0.1 }, { "%f|%e|%g\n", 1e-7, 1e-7, 1e-7 },
        { "%08.2f|%#.0f|% f\n", -3.14159, 5.2, 1.25 }, { "%f|%g|%.4f\n", 65536.0, 65536.0, 1.0 / 3.0 }, { "%g|%g|%g\n", 2.5, -0.0, 1e100 },
        { "%.17g|%.17g|%.17g\n", 0.1, 1.0 / 66.0, 5e-324 }, { "%f|%e|%g\n", INFINITY, -INFINITY, NAN }, { "%F|%E|%G\n", INFINITY, -INFINITY, NAN },
        { "%.9g|%+g|%.6f\n", 0.123456789f, 0.0, 1e300 }, { "%.0f|%.0f|%.0f\n", 0.5, 1.5, 2.5 }, { "%.0e|%#.0e|%#g\n", 12345.0, 12345.0, 1.5 },
        { "%10.4g|%-12e|%012.3e\n", 3.14159e-7, -2.5, 1.0e10 }, { "%.1f|%.3f|%.2f\n", -0.25, 1.0625, 0.125 }, { "%.4f|%.1f ms|%+g\n", 2.99999, 19.96, -1e-9 } };
    for (unsigned i = 0; i < sizeof dbl / sizeof dbl[0]; i++) same(dbl[i].fmt, dbl[i].a, dbl[i].b, dbl[i].d);
    /* Random values through every floating conversion with random flags, width and precision. */
    for (int i = 0; i < 400000; i++) {
        char fmt[40];
        static const char conv[] = "feEgGfge";
        int w = (int)(rnd() % 4) ? 0 : (int)(rnd() % 30), p = (int)(rnd() % 5) ? (int)(rnd() % 20) : -1;
        int n = snprintf(fmt, sizeof fmt, "%%%s%s%s%s%s", rnd() % 5 ? "" : "-", rnd() % 5 ? "" : "+", rnd() % 7 ? "" : " ", rnd() % 6 ? "" : "#", rnd() % 5 ? "" : "0");
        if (w) n += snprintf(fmt + n, sizeof fmt - (size_t)n, "%d", w);
        if (p >= 0) n += snprintf(fmt + n, sizeof fmt - (size_t)n, ".%d", p);
        snprintf(fmt + n, sizeof fmt - (size_t)n, "%c", conv[rnd() % 8]);
        same(fmt, random_double());
    }
    for (int i = 0; i < 200000; i++) {                                                   /* and the integer ones */
        char fmt[40];
        static const char *const conv[] = { "d", "u", "x", "X", "o", "lld", "llx", "llu", "hd", "hhu", "i" };
        unsigned k = (unsigned)(rnd() % 11);
        int w = (int)(rnd() % 3) ? 0 : (int)(rnd() % 24), p = (int)(rnd() % 4) ? -1 : (int)(rnd() % 14);
        int n = snprintf(fmt, sizeof fmt, "[%%%s%s%s%s%s", rnd() % 4 ? "" : "-", rnd() % 4 ? "" : "+", rnd() % 6 ? "" : " ", rnd() % 4 ? "" : "#", rnd() % 4 ? "" : "0");
        if (w) n += snprintf(fmt + n, sizeof fmt - (size_t)n, "%d", w);
        if (p >= 0) n += snprintf(fmt + n, sizeof fmt - (size_t)n, ".%d", p);
        snprintf(fmt + n, sizeof fmt - (size_t)n, "%s]", conv[k]);
        uint64_t v = rnd() >> (rnd() % 64);
        if (rnd() % 8 == 0) v = 0;
        if (rnd() % 3 == 0) v = 0 - v;
        if (k >= 5 && k <= 7) same(fmt, (unsigned long long)v); else same(fmt, (unsigned)v);
    }
    /* Truncation and the return value. */
    for (size_t cap = 0; cap < 12; cap++) {
        char a[16], b[16];
        memset(a, 'x', sizeof a); memset(b, 'x', sizeof b);
        int na = snprintf(a, cap, "%s=%d", "abcd", 12345), nb = ref_snprintf(b, cap, "%s=%d", "abcd", 12345);
        ncase++;
        if (na != nb || memcmp(a, b, sizeof a) != 0) { nfail++; printf("DIFFERENT truncation at capacity %zu\n", cap); }
    }
    /* The calendar: every 1,597th second... far too many; steps that hit every month of 1,031 years. */
    for (int64_t t = -2000000000; t <= 32535215999 + 75600; t += 86400 * 17 + 3607) {
        time_t tt = (time_t)t;
        struct tm a, b;
        char ta[32], tb[32];
        gmtime_r(&tt, &a); ref_gmtime_r(&tt, &b);
        ncase++;
        if (a.tm_sec != b.tm_sec || a.tm_min != b.tm_min || a.tm_hour != b.tm_hour || a.tm_mday != b.tm_mday || a.tm_mon != b.tm_mon ||
            a.tm_year != b.tm_year || a.tm_wday != b.tm_wday || a.tm_yday != b.tm_yday || b.tm_isdst != 0) { if (nfail++ < 25) printf("DIFFERENT gmtime_r(%lld)\n", (long long)t); continue; }
        if (ref_timegm(&b) != tt) { if (nfail++ < 25) printf("DIFFERENT timegm for %lld: %lld\n", (long long)t, (long long)ref_timegm(&b)); }
        if (a.tm_year < 8100 && strcmp(asctime_r(&a, ta), ref_asctime_r(&b, tb)) != 0) { if (nfail++ < 25) printf("DIFFERENT asctime_r(%lld): \"%s\" \"%s\"\n", (long long)t, ta, tb); }
    }
    /* powf where rank 60 uses it: a unit draw in (0, 1) to the exponents 2, 0.5, 3.7, 0.25; and wider. */
    unsigned long off = 0, far = 0, n_pow = 0;
    static const float exps[] = { 2.0f, 0.5f, 3.7f, 0.25f, 1.0f, -1.5f, 7.0f, 0.001f };
    for (int i = 0; i < 400000; i++) {
        float x = (float)((rnd() >> 40) + 1) * (1.0f / 16777217.0f), y = exps[rnd() % 8];
        if (rnd() % 10 == 0) x = (float)ldexp((double)(rnd() >> 40) + 1.0, (int)(rnd() % 60) - 40);
        float a = powf(x, y), b = ref_powf(x, y);
        uint32_t ua, ub;
        memcpy(&ua, &a, 4); memcpy(&ub, &b, 4);
        long d = (long)ua - (long)ub;
        n_pow++; ncase++;
        if (d) off++;
        if (d < -1 || d > 1) { far++; if (nfail++ < 25) printf("DIFFERENT powf(%.9g, %g): system %.9g, ours %.9g\n", x, y, a, b); }
    }
    for (int i = 0; i < 100000; i++) {
        double x = random_double();
        ncase++;
        if (ref_fabs(x) != fabs(x) || ref_ceil(x) != ceil(x) || ref_floor(x) != floor(x) || (x >= 0 && ref_sqrt(x) != sqrt(x))) { if (nfail++ < 25) printf("DIFFERENT fabs, ceil, floor or sqrt of %.17g\n", x); }
    }
    /* sscanf as rank 83 uses it, and the string functions on random text over a small alphabet. */
    for (int i = 0; i < 2000; i++) {
        char line[80], w1[16], w2[16];
        int t1, n1, t2, n2;
        double v1, v2;
        snprintf(line, sizeof line, "thread %d message %d of %s %f\n", 1 + i % 2, i, "gate2", i * 0.25);
        int r1 = sscanf(line, "thread %d message %d of %15s %lf", &t1, &n1, w1, &v1), r2 = ref_sscanf(line, "thread %d message %d of %15s %lf", &t2, &n2, w2, &v2);
        ncase++;
        if (r1 != 4 || r2 != 4 || t1 != t2 || n1 != n2 || strcmp(w1, w2) != 0 || v1 != v2) { if (nfail++ < 25) printf("DIFFERENT sscanf of \"%s\"\n", line); }
    }
    for (int i = 0; i < 200000; i++) {
        char a[24], b[24], set[5];
        for (int k = 0; k < 23; k++) { a[k] = (char)(rnd() % 12 ? 'a' + rnd() % 3 : 0); b[k] = (char)(rnd() % 12 ? 'a' + rnd() % 3 : 0); }
        a[23] = b[23] = 0;
        for (int k = 0; k < 4; k++) set[k] = (char)('a' + rnd() % 4);
        set[rnd() % 5] = 0; set[4] = 0;
        b[rnd() % 4] = 0;
        ncase++;
        int s1 = strcmp(a, b), s2 = ref_strcmp(a, b), m1 = strncmp(a, b, 5), m2 = ref_strncmp(a, b, 5), c1 = memcmp(a, b, 24), c2 = ref_memcmp(a, b, 24);
        if (strlen(a) != ref_strlen(a) || strnlen(a, 7) != ref_strnlen(a, 7) || (s1 > 0) != (s2 > 0) || (s1 < 0) != (s2 < 0) || (m1 > 0) != (m2 > 0) ||
            (m1 < 0) != (m2 < 0) || (c1 > 0) != (c2 > 0) || (c1 < 0) != (c2 < 0) || strstr(a, b) != ref_strstr(a, b) || strchr(a, 'b') != ref_strchr(a, 'b') ||
            strspn(a, set) != ref_strspn(a, set) || strcspn(a, set) != ref_strcspn(a, set)) { if (nfail++ < 25) printf("DIFFERENT string function on \"%s\", \"%s\", \"%s\"\n", a, b, set); }
        char num[24];
        snprintf(num, sizeof num, " %s%u", rnd() % 2 ? "-" : "", (unsigned)(rnd() % 2000000));
        if (atoi(num) != ref_atoi(num)) { if (nfail++ < 25) printf("DIFFERENT atoi(\"%s\")\n", num); }
        char m[24], r[24];
        memcpy(m, a, 24); memcpy(r, a, 24);
        size_t from = rnd() % 12, to = rnd() % 12, len = rnd() % 12;
        memmove(m + to, m + from, len); ref_memmove(r + to, r + from, len);
        if (memcmp(m, r, 24) != 0) { if (nfail++ < 25) printf("DIFFERENT memmove\n"); }
    }
    printf("ref-libc-test: %lu cases, %lu differ from the system library (and %lu more by the system's own %%g tie zero); "
           "powf: %lu of %lu differ by one unit in the last place, %lu by more\n", ncase, nfail, nquirk, off, n_pow, far);
    return nfail ? 1 : 0;
}
