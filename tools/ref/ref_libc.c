/* The part of the C library that the Gate 2 call tables (gate2/calls*.c) use on the host side,
 * written out so that the Windows reference program (refcalls.c) needs no C library: this Mac
 * has no Windows SDK and no mingw, and the answers must not depend on which C runtime the other
 * machine's Wine brings.  Plain C, no Windows call.
 *
 * The call tables use these functions to compute what a guest function should return (the
 * host's snprintf for a printf through Msg, gmtime_r for the calendar code, powf for a random
 * stream), so each must give what this Mac's library gives.  `make ref-libc-test` compiles this
 * file natively with every name prefixed ref_ (REF_LIBC_TEST) and compares it with the system
 * library over the formats and values the tables use and many random ones.
 *
 * printf: d i u x X o c s p %% f F e E g G; flags - + space # 0; width and precision, also as
 * `*`; lengths hh h l ll z j t.  Floating point is exact: the binary value is expanded to all
 * its decimal digits and rounded once, half to even, as the C standard asks and as the Mac's
 * library does.  `long` has the width of the target (32 bits on Windows), like everything else
 * the tables are compiled with.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef REF_LIBC_TEST
#include <time.h>
#define N(name) ref_##name
#else
#include "ref_libc.h"
#define N(name) name
#endif

/* ---- memory and strings -------------------------------------------------------------------------- */

void *N(memcpy)(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *N(memset)(void *d, int c, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)c; return d; }
void *N(memmove)(void *d, const void *s, size_t n) {
    uint8_t *a = d; const uint8_t *b = s;
    if (a < b) { while (n--) *a++ = *b++; } else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
int N(memcmp)(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++) if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
size_t N(strlen)(const char *s) { const char *p = s; while (*p) p++; return (size_t)(p - s); }
size_t N(strnlen)(const char *s, size_t max) { size_t n = 0; while (n < max && s[n]) n++; return n; }
int N(strcmp)(const char *a, const char *b) {
    for (;; a++, b++) { uint8_t x = (uint8_t)*a, y = (uint8_t)*b; if (x != y) return (int)x - (int)y; if (!x) return 0; }
}
int N(strncmp)(const char *a, const char *b, size_t n) {
    for (; n; n--, a++, b++) { uint8_t x = (uint8_t)*a, y = (uint8_t)*b; if (x != y) return (int)x - (int)y; if (!x) return 0; }
    return 0;
}
char *N(strcpy)(char *d, const char *s) { char *r = d; while ((*d++ = *s++)) { } return r; }
char *N(strncpy)(char *d, const char *s, size_t n) {
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    for (; i < n; i++) d[i] = 0;
    return d;
}
char *N(strchr)(const char *s, int c) {
    for (;; s++) { if (*s == (char)c) return (char *)(uintptr_t)s; if (!*s) return NULL; }
}
char *N(strstr)(const char *h, const char *n) {
    if (!*n) return (char *)(uintptr_t)h;
    for (; *h; h++) {
        const char *a = h, *b = n;
        while (*b && *a == *b) { a++; b++; }
        if (!*b) return (char *)(uintptr_t)h;
    }
    return NULL;
}
size_t N(strspn)(const char *s, const char *set) {
    size_t n = 0;
    for (; s[n]; n++) { const char *p = set; while (*p && *p != s[n]) p++; if (!*p) break; }
    return n;
}
size_t N(strcspn)(const char *s, const char *set) {
    size_t n = 0;
    for (; s[n]; n++) { const char *p = set; while (*p && *p != s[n]) p++; if (*p) break; }
    return n;
}
int N(atoi)(const char *s) {
    int sign = 1; unsigned v = 0;
    while (*s == ' ' || (*s >= '\t' && *s <= '\r')) s++;
    if (*s == '-') { sign = -1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0');
    return sign < 0 ? (int)(0u - v) : (int)v;
}

/* ---- arithmetic ---------------------------------------------------------------------------------- */

static uint64_t dbl_bits(double d) { union { double d; uint64_t u; } v; v.d = d; return v.u; }
static double bits_dbl(uint64_t u) { union { double d; uint64_t u; } v; v.u = u; return v.d; }

double N(fabs)(double x) { return bits_dbl(dbl_bits(x) & 0x7fffffffffffffffull); }
double N(sqrt)(double x) { return __builtin_sqrt(x); }          /* one instruction on both machines (-fno-math-errno) */
double N(floor)(double x) {
    if (!(x > -4503599627370496.0 && x < 4503599627370496.0)) return x;
    double d = (double)(long long)x;
    if (d > x) d -= 1.0;
    return d;
}
double N(ceil)(double x) {
    if (!(x > -4503599627370496.0 && x < 4503599627370496.0)) return x;
    double d = (double)(long long)x;
    if (d < x) d += 1.0;
    return d == 0.0 && x < 0.0 ? -0.0 : d;
}

/* ln 2 in two parts (the first has its low 21 bits clear, so k * LN2_HI is exact for |k| < 2^21). */
#define LN2_HI 6.93147180369123816490e-01
#define LN2_LO 1.90821492927058770002e-10

static double two_to(int k) {                   /* 2^k, k in [-1022, 1023] */
    return bits_dbl((uint64_t)(k + 1023) << 52);
}
static double ln_pos(double x) {                /* x finite and > 0; error about 1e-16 relative */
    uint64_t u = dbl_bits(x);
    int e = (int)(u >> 52 & 0x7ff);
    if (e == 0) { u = dbl_bits(x * 18014398509481984.0); e = (int)(u >> 52 & 0x7ff) - 54; }
    e -= 1023;
    double m = bits_dbl((u & 0x000fffffffffffffull) | 0x3ff0000000000000ull);     /* [1, 2) */
    if (m > 1.4142135623730951) { m *= 0.5; e++; }
    double s = (m - 1.0) / (m + 1.0), s2 = s * s, sum = 0.0;
    for (int k = 45; k >= 1; k -= 2) sum = sum * s2 + 1.0 / k;                    /* 1 + s^2/3 + s^4/5 + ... */
    return (double)e * LN2_HI + (2.0 * s * sum + (double)e * LN2_LO);
}
static double exp_any(double t) {
    if (t != t) return t;
    if (t > 709.78) return bits_dbl(0x7ff0000000000000ull);
    if (t < -745.2) return 0.0;
    double kf = t * 1.4426950408889634;
    int k = (int)(kf < 0 ? kf - 0.5 : kf + 0.5);
    double r = (t - (double)k * LN2_HI) - (double)k * LN2_LO, p = 1.0;
    for (int n = 24; n >= 1; n--) p = 1.0 + p * r / n;
    if (k > 1023) { p *= two_to(1023); k -= 1023; }
    if (k < -1022) { p *= two_to(-1022); k += 1022; if (k < -1022) { p *= two_to(-1022); k += 1022; } }
    return p * two_to(k < -1022 ? -1022 : k);
}

/* powf through double precision: the result is the correctly rounded one except when
 * x^y lies within about 1e-15 (relative) of the middle between two floats. */
float N(powf)(float x, float y) {
    if (y == 0.0f || x == 1.0f) return 1.0f;
    if (x != x || y != y) return x + y;
    if (x == 0.0f) return y > 0.0f ? 0.0f : 1.0f / 0.0f;
    double ax = x < 0.0f ? -(double)x : (double)x;
    if (ax > 3.5e38) return y > 0.0f ? (float)ax : 0.0f;
    if (y > 3.4e38f || y < -3.4e38f) return (ax < 1.0) == (y < 0.0f) ? 1.0f / 0.0f : 0.0f;
    double r = exp_any((double)y * ln_pos(ax));
    if (x < 0.0f) {
        double f = N(floor)((double)y);
        if (f != (double)y) return 0.0f / 0.0f;                                  /* a negative base with a fraction */
        if (N(floor)(f / 2.0) * 2.0 != f) r = -r;                                /* an odd power */
    }
    return (float)r;
}

/* ---- printf -------------------------------------------------------------------------------------- */

typedef struct { char *buf; size_t cap, len; } Out;
static void put(Out *o, char ch) { if (o->len + 1 < o->cap) o->buf[o->len] = ch; o->len++; }
static void put_n(Out *o, char ch, int n) { while (n-- > 0) put(o, ch); }
static void put_s(Out *o, const char *s, size_t n) { while (n--) put(o, *s++); }

typedef struct { int left, plus, space, alt, zero, width, prec; } Spec;       /* prec < 0: none given */

/* [spaces] sign+prefix [zeros] body [spaces] */
static void put_field(Out *o, const Spec *sp, const char *pre, size_t npre, int zeros, const char *body, size_t nbody, int zero_pad) {
    int len = (int)npre + zeros + (int)nbody, pad = sp->width > len ? sp->width - len : 0;
    if (!sp->left && !zero_pad) put_n(o, ' ', pad);
    put_s(o, pre, npre);
    if (!sp->left && zero_pad) put_n(o, '0', pad);
    put_n(o, '0', zeros);
    put_s(o, body, nbody);
    if (sp->left) put_n(o, ' ', pad);
}

static void fmt_int(Out *o, const Spec *sp, uint64_t v, int negative, int base, int upper, int is_signed, int pointer) {
    char dig[24], pre[3];
    size_t nd = 0, npre = 0;
    const char *set = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    while (v) { dig[sizeof dig - ++nd] = set[v % (unsigned)base]; v /= (unsigned)base; }
    const char *d = dig + sizeof dig - nd;
    int zeros = 0;
    if (sp->prec >= 0) zeros = sp->prec > (int)nd ? sp->prec - (int)nd : 0;
    else if (nd == 0) zeros = 1;                                                 /* the value 0 with no precision: "0" */
    if (is_signed) {
        if (negative) pre[npre++] = '-';
        else if (sp->plus) pre[npre++] = '+';
        else if (sp->space) pre[npre++] = ' ';
    }
    if (base == 16 && (pointer || (sp->alt && nd))) { pre[npre++] = '0'; pre[npre++] = upper ? 'X' : 'x'; }
    if (base == 8 && sp->alt && zeros == 0) zeros = 1;                           /* %#o: a leading 0, also for the value 0 */
    put_field(o, sp, pre, npre, zeros, d, nd, sp->zero && !sp->left && sp->prec < 0);
}

/* The exact decimal expansion of a finite double's magnitude: the digits (no leading zero,
 * none at all for 0) and `pt` such that the value is 0.DIGITS * 10^pt.  At most 767 digits. */
#define DIG_MAX 800
static int exact_digits(double x, char *dig, int *pt) {
    uint64_t u = dbl_bits(x) & 0x7fffffffffffffffull, m = u & 0x000fffffffffffffull;
    int e = (int)(u >> 52);
    if (e == 0) e = 1; else m |= 0x0010000000000000ull;
    e -= 1075;                                                                   /* value = m * 2^e */
    if (m == 0) { *pt = 0; return 0; }
    static const uint32_t BASE = 1000000000u;
    uint32_t limb[96];
    int nl = 0, k = 0;
    for (uint64_t t = m; t; t /= BASE) limb[nl++] = (uint32_t)(t % BASE);
    int twos = e > 0 ? e : 0, fives = e < 0 ? -e : 0;
    if (e < 0) k = -e;                                                           /* value = N / 10^k with N = m * 5^k */
    while (twos || fives) {
        uint32_t f;
        if (twos) { int s = twos > 29 ? 29 : twos; f = (uint32_t)1 << s; twos -= s; }
        else { int s = fives > 13 ? 13 : fives; f = 1; for (int i = 0; i < s; i++) f *= 5; fives -= s; }
        uint64_t carry = 0;
        for (int i = 0; i < nl; i++) { uint64_t t = (uint64_t)limb[i] * f + carry; limb[i] = (uint32_t)(t % BASE); carry = t / BASE; }
        while (carry) { limb[nl++] = (uint32_t)(carry % BASE); carry /= BASE; }
    }
    int nd = 0;
    for (int i = nl - 1; i >= 0; i--) {
        char nine[9];
        uint32_t t = limb[i];
        for (int j = 8; j >= 0; j--) { nine[j] = (char)('0' + t % 10); t /= 10; }
        int j = 0;
        if (i == nl - 1) while (j < 8 && nine[j] == '0') j++;
        for (; j < 9; j++) dig[nd++] = nine[j];
    }
    *pt = nd - k;
    return nd;
}

/* Round the digits to `keep` of them, half to even; returns the new count (<= keep, or 1 when
 * a carry runs out of the top), and moves *pt when it does. */
static int round_digits(char *dig, int nd, int keep, int *pt) {
    if (keep >= nd) return nd;
    if (keep < 0) return 0;
    int up = 0;
    if (dig[keep] > '5') up = 1;
    else if (dig[keep] == '5') {
        for (int i = keep + 1; i < nd && !up; i++) if (dig[i] != '0') up = 1;
        if (!up) up = keep > 0 && ((dig[keep - 1] - '0') & 1);                   /* a tie: to even (nothing kept is 0, even) */
    }
    if (!up) return keep;
    for (int i = keep - 1; i >= 0; i--) {
        if (dig[i] != '9') { dig[i]++; return keep; }
        dig[i] = '0';
    }
    dig[0] = '1';                                                                /* 999 -> 1000, or nothing kept -> 1 */
    (*pt)++;
    return keep > 0 ? keep : 1;
}

static char digit_at(const char *dig, int nd, int i) { return i >= 0 && i < nd ? dig[i] : '0'; }

/* The body of a floating conversion (no sign, no padding) into `out`; returns its length. */
static int fmt_fixed(char *out, const char *dig, int nd, int pt, int frac, int point) {
    int n = 0;
    if (pt <= 0) out[n++] = '0';
    for (int i = 0; i < pt; i++) out[n++] = digit_at(dig, nd, i);
    if (frac > 0 || point) out[n++] = '.';
    for (int i = 0; i < frac; i++) out[n++] = digit_at(dig, nd, pt + i);
    return n;
}
static int fmt_exp(char *out, const char *dig, int nd, int pt, int frac, int point, int upper) {
    int n = 0, x = nd ? pt - 1 : 0;
    out[n++] = digit_at(dig, nd, 0);
    if (frac > 0 || point) out[n++] = '.';
    for (int i = 0; i < frac; i++) out[n++] = digit_at(dig, nd, 1 + i);
    out[n++] = upper ? 'E' : 'e';
    out[n++] = x < 0 ? '-' : '+';
    if (x < 0) x = -x;
    if (x >= 100) out[n++] = (char)('0' + x / 100);
    out[n++] = (char)('0' + x / 10 % 10);
    out[n++] = (char)('0' + x % 10);
    return n;
}
static int strip_zeros(char *out, int n) {                                       /* %g without #: the fraction's trailing zeros go */
    int e = 0;
    while (e < n && out[e] != 'e' && out[e] != 'E') e++;
    int has_point = 0;
    for (int i = 0; i < e; i++) if (out[i] == '.') has_point = 1;
    if (!has_point) return n;
    int end = e;
    while (end > 0 && out[end - 1] == '0') end--;
    if (end > 0 && out[end - 1] == '.') end--;
    for (int i = e; i < n; i++) out[end++] = out[i];
    return end;
}

static __attribute__((noinline)) void fmt_double(Out *o, const Spec *sp, double x, char conv) {
    char pre[1], dig[DIG_MAX], body[DIG_MAX + 420];
    size_t npre = 0;
    int upper = conv >= 'A' && conv <= 'Z', lower = upper ? conv + 32 : conv;
    uint64_t u = dbl_bits(x);
    if (u >> 63) pre[npre++] = '-';
    else if (sp->plus) pre[npre++] = '+';
    else if (sp->space) pre[npre++] = ' ';
    if ((u >> 52 & 0x7ff) == 0x7ff) {
        int nan = (u & 0x000fffffffffffffull) != 0;
        const char *t = nan ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        put_field(o, sp, pre, npre, 0, t, 3, 0);
        return;
    }
    int prec = sp->prec < 0 ? 6 : sp->prec > 400 ? 400 : sp->prec, pt, n;
    int nd = exact_digits(x, dig, &pt);
    if (lower == 'f') {
        nd = round_digits(dig, nd, pt + prec, &pt);
        n = fmt_fixed(body, dig, nd, pt, prec, sp->alt);
    } else if (lower == 'e') {
        nd = round_digits(dig, nd, prec + 1, &pt);
        n = fmt_exp(body, dig, nd, pt, prec, sp->alt, upper);
    } else {
        if (prec == 0) prec = 1;
        nd = round_digits(dig, nd, prec, &pt);
        int x10 = nd ? pt - 1 : 0;
        if (x10 < -4 || x10 >= prec) n = fmt_exp(body, dig, nd, pt, prec - 1, sp->alt, upper);
        else                         n = fmt_fixed(body, dig, nd, pt, prec - 1 - x10, sp->alt);
        if (!sp->alt) n = strip_zeros(body, n);
    }
    put_field(o, sp, pre, npre, 0, body, (size_t)n, sp->zero && !sp->left);
}

int N(vsnprintf)(char *buf, size_t cap, const char *fmt, va_list ap) {
    Out o = { buf, cap, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        Spec sp = { 0, 0, 0, 0, 0, 0, -1 };
        for (fmt++;; fmt++) {
            if (*fmt == '-') sp.left = 1; else if (*fmt == '+') sp.plus = 1; else if (*fmt == ' ') sp.space = 1;
            else if (*fmt == '#') sp.alt = 1; else if (*fmt == '0') sp.zero = 1; else break;
        }
        if (*fmt == '*') { sp.width = va_arg(ap, int); fmt++; if (sp.width < 0) { sp.left = 1; sp.width = -sp.width; } }
        else while (*fmt >= '0' && *fmt <= '9') sp.width = sp.width * 10 + (*fmt++ - '0');
        if (*fmt == '.') {
            fmt++;
            sp.prec = 0;
            if (*fmt == '*') { sp.prec = va_arg(ap, int); fmt++; }                /* a negative one means none */
            else while (*fmt >= '0' && *fmt <= '9') sp.prec = sp.prec * 10 + (*fmt++ - '0');
        }
        int len = 0;                                                             /* -2 hh, -1 h, 1 l, 2 ll, 3 z or t, 4 j */
        if (*fmt == 'h') { len = -1; fmt++; if (*fmt == 'h') { len = -2; fmt++; } }
        else if (*fmt == 'l') { len = 1; fmt++; if (*fmt == 'l') { len = 2; fmt++; } }
        else if (*fmt == 'z' || *fmt == 't') { len = 3; fmt++; }
        else if (*fmt == 'j') { len = 4; fmt++; }
        else if (*fmt == 'L') fmt++;
        char conv = *fmt;
        if (!conv) break;
        switch (conv) {
        case 'd': case 'i': {
            int64_t v = len == 2 || len == 4 ? va_arg(ap, long long) : len == 3 ? (int64_t)va_arg(ap, ptrdiff_t) : len == 1 ? va_arg(ap, long)
                      : va_arg(ap, int);
            if (len == -1) v = (short)v; else if (len == -2) v = (signed char)v;
            fmt_int(&o, &sp, v < 0 ? 0 - (uint64_t)v : (uint64_t)v, v < 0, 10, 0, 1, 0);
            break;
        }
        case 'u': case 'x': case 'X': case 'o': {
            uint64_t v = len == 2 || len == 4 ? va_arg(ap, unsigned long long) : len == 3 ? va_arg(ap, size_t) : len == 1 ? va_arg(ap, unsigned long)
                       : va_arg(ap, unsigned);
            if (len == -1) v = (unsigned short)v; else if (len == -2) v = (unsigned char)v;
            fmt_int(&o, &sp, v, 0, conv == 'u' ? 10 : conv == 'o' ? 8 : 16, conv == 'X', 0, 0);
            break;
        }
        case 'p':
            fmt_int(&o, &sp, (uint64_t)(uintptr_t)va_arg(ap, void *), 0, 16, 0, 0, 1);
            break;
        case 'c': {
            char ch = (char)va_arg(ap, int);
            sp.prec = -1;
            put_field(&o, &sp, "", 0, 0, &ch, 1, 0);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            size_t n = 0;
            while ((sp.prec < 0 || n < (size_t)sp.prec) && s[n]) n++;
            put_field(&o, &sp, "", 0, 0, s, n, 0);
            break;
        }
        case 'f': case 'F': case 'e': case 'E': case 'g': case 'G':
            fmt_double(&o, &sp, va_arg(ap, double), conv);
            break;
        case '%':
            put(&o, '%');
            break;
        default:                                                                 /* not a conversion of ours: shown as written */
            put(&o, '%');
            put(&o, conv);
            break;
        }
    }
    if (cap) buf[o.len < cap ? o.len : cap - 1] = 0;
    return (int)o.len;
}

int N(snprintf)(char *buf, size_t cap, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = N(vsnprintf)(buf, cap, fmt, ap);
    va_end(ap);
    return n;
}

/* ---- sscanf: %d %u %s %<n>s %f %lf, literal text and white space ---------------------------------- */

static int is_space(char ch) { return ch == ' ' || (ch >= '\t' && ch <= '\r'); }

int N(sscanf)(const char *s, const char *fmt, ...) {
    va_list ap;
    int got = 0;
    va_start(ap, fmt);
    for (; *fmt; fmt++) {
        if (is_space(*fmt)) { while (is_space(*s)) s++; continue; }
        if (*fmt != '%') { if (*s != *fmt) break; s++; continue; }
        fmt++;
        int width = 0, is_long = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');
        if (*fmt == 'l') { is_long = 1; fmt++; }
        if (*fmt == '%') { if (*s != '%') break; s++; continue; }
        while (is_space(*s)) s++;
        if (!*s) break;
        if (*fmt == 's') {
            char *out = va_arg(ap, char *);
            int n = 0;
            while (*s && !is_space(*s) && (!width || n < width)) out[n++] = *s++;
            out[n] = 0;
            got++;
        } else if (*fmt == 'd' || *fmt == 'u') {
            int neg = 0;
            unsigned v = 0;
            if (*s == '-' || *s == '+') neg = *s++ == '-';
            if (!(*s >= '0' && *s <= '9')) break;
            while (*s >= '0' && *s <= '9') v = v * 10 + (unsigned)(*s++ - '0');
            *va_arg(ap, unsigned *) = neg ? 0u - v : v;
            got++;
        } else if (*fmt == 'f') {
            /* Decimal digits with an optional fraction and exponent.  Exact whenever the digits
             * make an integer below 2^53 and the power of ten is at most 10^22 (one division or
             * multiplication of exact values); the tables read back n * 0.25 printed with %f. */
            int neg = 0, exp10 = 0, any = 0;
            uint64_t mant = 0;
            if (*s == '-' || *s == '+') neg = *s++ == '-';
            for (; *s >= '0' && *s <= '9'; s++, any = 1) { if (mant < 1000000000000000000ull) mant = mant * 10 + (uint64_t)(*s - '0'); else exp10++; }
            if (*s == '.') for (s++; *s >= '0' && *s <= '9'; s++, any = 1) if (mant < 1000000000000000000ull) { mant = mant * 10 + (uint64_t)(*s - '0'); exp10--; }
            if (!any) break;
            if ((*s == 'e' || *s == 'E') && ((s[1] >= '0' && s[1] <= '9') || ((s[1] == '-' || s[1] == '+') && s[2] >= '0' && s[2] <= '9'))) {
                int eneg = 0, e = 0;
                s++;
                if (*s == '-' || *s == '+') eneg = *s++ == '-';
                while (*s >= '0' && *s <= '9') { if (e < 10000) e = e * 10 + (*s - '0'); s++; }
                exp10 += eneg ? -e : e;
            }
            double v = (double)mant, p = 1.0;
            int e = exp10 < 0 ? -exp10 : exp10;
            for (int i = 0; i < e && i < 400; i++) p *= 10.0;
            v = exp10 < 0 ? v / p : v * p;
            if (neg) v = -v;
            if (is_long) *va_arg(ap, double *) = v; else *va_arg(ap, float *) = (float)v;
            got++;
        } else break;
    }
    va_end(ap);
    return got;
}

/* ---- the calendar ---------------------------------------------------------------------------------- */

static int64_t days_from_civil(int64_t y, int m, int d) {                         /* days since 1970-01-01; m 1 to 12 */
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

struct tm *N(gmtime_r)(const time_t *t, struct tm *out) {
    int64_t s = (int64_t)*t, days = s / 86400, rem = s % 86400;
    if (rem < 0) { rem += 86400; days--; }
    int64_t z = days + 719468, era = (z >= 0 ? z : z - 146096) / 146097, doe = z - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365, y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100), mp = (5 * doy + 2) / 153;
    int d = (int)(doy - (153 * mp + 2) / 5 + 1), m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y += m <= 2;
    int64_t wday = (days + 4) % 7;
    if (wday < 0) wday += 7;
    N(memset)(out, 0, sizeof *out);
    out->tm_sec = (int)(rem % 60);
    out->tm_min = (int)(rem / 60 % 60);
    out->tm_hour = (int)(rem / 3600);
    out->tm_mday = d;
    out->tm_mon = m - 1;
    out->tm_year = (int)(y - 1900);
    out->tm_wday = (int)wday;
    out->tm_yday = (int)(days - days_from_civil(y, 1, 1));
    out->tm_isdst = 0;
    return out;
}

time_t N(timegm)(struct tm *tm) {
    int64_t y = (int64_t)tm->tm_year + 1900, mon = tm->tm_mon;
    y += mon / 12; mon %= 12;
    if (mon < 0) { mon += 12; y--; }
    int64_t days = days_from_civil(y, (int)mon + 1, 1) + (tm->tm_mday - 1);
    return (time_t)(days * 86400 + (int64_t)tm->tm_hour * 3600 + (int64_t)tm->tm_min * 60 + tm->tm_sec);
}

char *N(asctime_r)(const struct tm *tm, char *buf) {                              /* "Tue Nov 14 22:13:20 2023\n", 26 bytes */
    static const char days[] = "SunMonTueWedThuFriSat", months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    N(snprintf)(buf, 26, "%.3s %.3s%3d %.2d:%.2d:%.2d %d\n", days + 3 * (tm->tm_wday % 7 + (tm->tm_wday < 0 ? 7 : 0)) % 21,
                months + 3 * ((tm->tm_mon % 12 + 12) % 12), tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, 1900 + tm->tm_year);
    return buf;
}
