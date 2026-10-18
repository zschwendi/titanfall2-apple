/* The values an entry checks, kept so that two runs of the same entry can be compared: ours
 * (gate2/driver.c, with GATE2_VALUES=1) and the same calls made to the real DLLs on real x86
 * (tools/ref/refcalls.c).  tools/ref/compare.py reads both.
 *
 * Every EXPECT and EXPECT_TRUE records (what, got) here before it compares, and gate2_value
 * records a value that has no exact expectation (a stream of draws checked by a tolerance).
 * Per distinct `what` text the table keeps how often it was checked, the first and the last
 * value, and a digest of all its values that does not depend on their order (two threads may
 * check at once).  Both programs compile this file, so the two outputs have one format:
 *
 *     V <rank> <times> <first> <last> <digest> <kind> <what>
 *     R <rank> <PASS|FAIL|SKIP|...> <checks> <digest of every value> <name> <message>
 *
 * with one tab between fields; tabs and line ends inside a text are written as spaces.  <kind>
 * is E for a value an EXPECT compared with its expectation (when both runs pass and the values
 * differ, the expectation itself came from the machine: a thread id, an address) and M for a
 * value recorded by gate2_value, which nothing compared: there a difference is a difference.
 */
#ifndef GATE2_VALUES_H
#define GATE2_VALUES_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define GATE2_VALUES_MAX 256
typedef struct {
    char     what[112];
    char     kind;                              /* 'E' or 'M' */
    uint64_t hash, count, first, last, digest;
} Gate2Value;
typedef struct {
    volatile int lock;
    unsigned     n, dropped;                    /* dropped: checks whose text found no free row */
    uint64_t     checks, digest;
    Gate2Value   v[GATE2_VALUES_MAX];
} Gate2Values;

static inline uint64_t gate2_mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull;
    x ^= x >> 27; x *= 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}
static inline uint64_t gate2_hash_text(const char *s) {
    uint64_t h = 0xcbf29ce484222325ull;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 0x100000001b3ull; }
    return h;
}
/* An order-dependent digest of a stream of values, for gate2_value: d = gate2_stream(d, x). */
static inline uint64_t gate2_stream(uint64_t d, uint64_t x) { return gate2_mix64(d ^ x) + 0x9e3779b97f4a7c15ull; }

static inline void gate2_values_record(Gate2Values *t, char kind, const char *what, uint64_t got) {
    uint64_t h = gate2_hash_text(what), d = gate2_mix64(h ^ gate2_mix64(got));
    while (__atomic_exchange_n(&t->lock, 1, __ATOMIC_ACQUIRE)) { }
    t->checks++;
    t->digest += d;
    unsigned i = 0;
    while (i < t->n && t->v[i].hash != h) i++;
    if (i == t->n && t->n == GATE2_VALUES_MAX) {
        t->dropped++;
    } else {
        Gate2Value *v = &t->v[i];
        if (i == t->n) {
            size_t k = 0;
            for (; what[k] && k < sizeof v->what - 1; k++) v->what[k] = what[k] == '\t' || what[k] == '\n' || what[k] == '\r' ? ' ' : what[k];
            v->what[k] = 0;
            v->hash = h; v->count = 0; v->digest = 0; v->first = got; v->kind = kind;
            t->n++;
        }
        v->count++;
        v->last = got;
        v->digest += d;
    }
    __atomic_store_n(&t->lock, 0, __ATOMIC_RELEASE);
}

/* Row i as a line without its line end; returns the length (0 when i is past the end). */
static inline int gate2_values_line(const Gate2Values *t, unsigned i, int rank, char *buf, size_t n) {
    if (i >= t->n) return 0;
    const Gate2Value *v = &t->v[i];
    return snprintf(buf, n, "V\t%d\t%llu\t%#llx\t%#llx\t%016llx\t%c\t%s", rank, (unsigned long long)v->count, (unsigned long long)v->first,
                    (unsigned long long)v->last, (unsigned long long)v->digest, v->kind, v->what);
}

/* The result line of a rank, without its line end.  `name` and `msg` lose tabs and line ends. */
static inline int gate2_values_result(const Gate2Values *t, int rank, const char *status, const char *name, const char *msg,
                                      char *buf, size_t n) {
    int at = snprintf(buf, n, "R\t%d\t%s\t%llu\t%016llx\t", rank, status, (unsigned long long)t->checks, (unsigned long long)t->digest);
    if (at < 0 || (size_t)at >= n) return at;
    size_t k = (size_t)at;
    for (int part = 0; part < 2; part++) {
        for (const char *p = part ? msg : name; p && *p && k + 2 < n; p++) buf[k++] = *p == '\t' || *p == '\n' || *p == '\r' ? ' ' : *p;
        if (part == 0 && k + 2 < n) buf[k++] = '\t';
    }
    buf[k] = 0;
    return (int)k;
}

#endif /* GATE2_VALUES_H */
