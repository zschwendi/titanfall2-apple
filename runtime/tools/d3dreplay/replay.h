/* The Direct3D replay player's engine: plays a script of recorded calls (tools/trace/d3d/d3dscript.py
 * says what a script line is) through the interface slots of the do-nothing device, as translated
 * game code would call them, and holds every result and out-value against the recorded one.
 *
 * Included whole (everything is static) by the player, runtime/tools/d3dreplay/main.c, and by the
 * regression test, runtime/tests/core/d3d_replay.c.  The runtime must be started (rt_init) and the
 * caller must be the main guest thread.
 *
 * STOPS.  A stop inside the device ends the process (rt_report_abort), in a replay as in a real
 * run: nothing in the runtime is changed for the player.  The player learns of a stop from a
 * SCOUT: before it plays a chunk of lines it forks; the child plays the chunk with its standard
 * error on a pipe and exits; if the child died instead, the parent knows at which line (a shared
 * cell) and has the fatal report (the pipe).  The parent then plays the lines before that one
 * itself, records the stop, treats the objects that call would have returned as never made, and
 * sends a new scout from the line after it.  So every call is made twice, once by a process that
 * may die of it, and the process that holds the table never meets a stop.  The fork follows the
 * rule of rt/test.h for T_TRAPS (the forking thread is the only one that matters; the two locks
 * of rt/test_fork.h are put back in the child).
 *
 * WHAT IS COMPARED.  The result (HRESULT, count or level) and every out-value the script holds:
 * integers and floats by value, a structure leaf by leaf, an object pointer as NULL against
 * non-NULL and, when the recording returned an object it had returned before, as the same pointer
 * of ours.  Pointers into memory (pData) are compared as NULL against non-NULL only.  A difference
 * that is ours by design is a row of rp_expected below, with its reason and document, and is
 * counted apart.
 */
#ifndef RP_REPLAY_H
#define RP_REPLAY_H

#include <errno.h>
#include <math.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rt/init.h"
#include "rt/shim.h"
#include "rt/com.h"
#include "rt/module.h"
#include "rt/nulldev.h"
#include "rt/test_fork.h"
#include "rt/crash.h"
#include "rt/log.h"

#define RP_API static __attribute__((unused))

#include "data.h"                   /* the recorded data (--data): the types */

typedef struct { const char *name; uint32_t offset, size; char kind; } RpLeaf;
typedef struct { const char *name; uint32_t size; const RpLeaf *leaves; unsigned n; } RpStruct;
#define RP_LEAF(T, path) { #path, (uint32_t)offsetof(T, path), (uint32_t)sizeof(((T *)0)->path), \
                           _Generic(((T *)0)->path, float: 'f', double: 'f', void *: 'p', const void *: 'p', default: 'i') }
#include "replay_layouts.h"

/* ---- differences that are ours by design ---------------------------------------------------------
 * method: "Interface::Method", or "*::Method" for every interface.  what: the beginning of the
 * difference as the table words it ("result", "result is 0 where the recording has 0x1", or a
 * parameter's name as the descriptions have it, with a leaf: "pDesc.Format"; the DXGI
 * descriptions use MinGW's names, "desc", "mode_count"); "" is every out-value and not the result.
 * A result is named with both values wherever only that pair is ours by design.  Each row: the reason, then the document. */
typedef struct { const char *method, *what, *reason; } RpExpected;
static const RpExpected rp_expected[] = {
    { "*::Release", "result", "our objects have one reference count each and hold what Windows documents; the recorded "
      "counts are DXVK's, which counts private references apart (d3d-interface-layer.md, section 3)" },
    { "*::AddRef", "result", "as Release (d3d-interface-layer.md, section 3)" },
    { "IDXGIAdapter1::GetDesc", "", "the adapter's identity is ours: the recording's is the owner's graphics card "
      "(d3d-contract.md, section 7, the method's row; finding 40: 'the identity and capability answers are that machine's')" },
    { "IDXGIOutput::GetDesc", "", "the output is our one display, not the recording machine's 5120x2880 desktop "
      "(d3d-contract.md, section 7, the method's row)" },
    { "IDXGIOutput::GetDisplayModeList", "", "the display modes are ours (d3d-contract.md, decision D4; the recording "
      "has that machine's 46)" },
    { "IDXGISwapChain::GetFullscreenState", "fullscreen", "our runs are windowed: FALSE where the recorded fullscreen "
      "session wrote TRUE (finding 40, row E; d3d-contract.md, decision D1)" },
    { "ID3D11DeviceContext::GetData", "result is 0 where the recording has 0x1", "our queries are never 'not ready': S_OK where the real device said "
      "S_FALSE (d3d-resources.md, section 5; finding 40, row T)" },
    { "ID3D11DeviceContext::GetData", "pData", "query results are ours (d3d-resources.md, section 5)" },
    { "IDXGISwapChain::Present", "result is 0 where the recording has 0x87a0001", "our Present always answers S_OK; the recording's window lost the screen "
      "(DXGI_STATUS_OCCLUDED from call 2362939: finding 40, row AC)" },
    { "IDXGIAdapter1::CheckInterfaceSupport", "", "the driver version is ours (d3d-contract.md, section 7, row d3d-adapter-interface-support)" },
    { "IDXGIOutput::GetGammaControlCapabilities", "", "the gamma ramp's shape is the display's (finding 40, section 2, D2; the method is a stop today)" },
};

/* ---- the table ----------------------------------------------------------------------------------- */

typedef struct {
    char    *method, *cause, *detail;       /* detail: the first occurrence */
    const char *expected;                   /* the reason, when ours by design */
    uint64_t first, count, last;
} RpRow;

#define RP_SKIP_MAX 16              /* --skip-method may be given this many times */

typedef struct {
    RpRow   *rows;
    unsigned n, cap;
    uint64_t lines, game_calls, played, foreign, writes, writes_done, undescribed, not_played, stops;
    uint64_t clean, with_expected_only, with_differences;       /* of the calls played */
    uint64_t scouts, stops_not_tried, peak_rss, peak_footprint, zero_region;
    uint64_t scouts_by_signal;          /* scouts that a signal ended: 0 unless one hung (SIGALRM) or crashed outside a report */
    double   seconds;
    RpDataCounts data;                  /* with RpOptions.data */
    const char *data_dir;
    uint64_t skipped[RP_SKIP_MAX], skip_first[RP_SKIP_MAX];   /* per RpOptions.skip: lines not played, the first one's call number */
} RpTable;

typedef struct {
    bool     keep_going;            /* run on past disagreements and stops (the table mode) */
    bool     scout;                 /* learn of stops from a forked scout (off: a stop is the runtime's own) */
    bool     play_foreign;          /* also play the calls marked as not the game's */
    bool     progress;              /* a line on standard error every 20 chunks */
    unsigned chunk;                 /* lines per scout; 0 = 5000 */
    FILE    *out;                   /* where the first disagreement is reported; NULL = stderr */
    uint64_t until;                 /* stop after this many lines; 0 = all */
    const char *data;               /* a directory of recorded data (data.h); NULL: every block is zeros */
    const char *skip[RP_SKIP_MAX];  /* --skip-method: Interface::Method names whose lines are not played */
    unsigned    nskip;
} RpOptions;

/* state: 0 none, 1 bound, 2 never made.  owed: references the recording received from calls that we
 * did not make (a stop, or a call not played) or that returned nothing: that many of the script's
 * later Release calls on the object are not played, or our count would fall below the game's. */
typedef struct { uint64_t ptr; int64_t held; uint64_t born; int32_t origin, owed, owed_origin; uint8_t state; } RpObj;
typedef struct { uint32_t obj, sub; uint64_t ptr; int32_t origin; } RpMap;     /* ptr 0: a Map that was not made, and why */
typedef struct { const RtComMethod *m; unsigned arg; uint64_t rec, ours; } RpInOut;

enum { RP_O_OBJ = 1, RP_O_OBJS, RP_O_STRUCT, RP_O_STRUCTS, RP_O_INTS, RP_O_FLOATS };
typedef struct { int kind; unsigned arg, width, count; uint64_t ptr; const char *text; const RpStruct *def; bool inout; } RpOut;

typedef struct {
    Ctx      *ctx;
    RpOptions opt;
    RpTable  *tab;
    bool      record;               /* false in a scout */
    RpObj    *objs;   size_t nobjs;
    RpMap    *maps;   unsigned nmaps, capmaps;
    RpInOut   inout[16]; unsigned ninout;
    uint8_t  *arena;  size_t arena_used, arena_cap;
    void     *big[4096]; unsigned nbig;
    uint8_t  *zero;   size_t zero_len;
    RpData   *data;                 /* NULL without RpOptions.data */
    /* the call being played */
    uint64_t  no;
    const char *line, *method;
    int       differences, expected;
    int32_t   origin;               /* row that explains why this call's objects do not exist */
    uint64_t  cap_hint;
    volatile uint32_t *at;          /* shared with the scout: index of the line it is playing */
    int32_t   nobody[2048];         /* per method (index into rp_methods): 1 + the row of its "no body" stop, once
                                       a scout has died of it.  The layer's table says such a slot stops at every
                                       call (RT_COM_BODY_STOP), so the later calls are counted without a scout */
} RpRun;

#define RP_POISON UINT64_C(0xD3D0D3D0D3D0D3D0)

static inline uint64_t rp_rd64(uint64_t a) { uint64_t v; memcpy(&v, (const void *)(uintptr_t)a, 8); return v; }

static _Noreturn void rp_die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "d3d-replay: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

/* ---- memory for arguments -------------------------------------------------------------------------- */

static void *rp_alloc(RpRun *r, size_t n) {
    n = (n + 15) & ~(size_t)15;
    if (!r->arena) { r->arena_cap = 1 << 20; r->arena = calloc(1, r->arena_cap); }
    if (r->arena_used + n > r->arena_cap) {
        if (r->nbig == sizeof r->big / sizeof r->big[0]) rp_die("call %llu needs too many blocks", (unsigned long long)r->no);
        return r->big[r->nbig++] = calloc(1, n ? n : 16);
    }
    void *p = r->arena + r->arena_used;
    r->arena_used += n;
    return p;
}

static void rp_reset(RpRun *r) {
    memset(r->arena, 0, r->arena_used);
    r->arena_used = 0;
    while (r->nbig) free(r->big[--r->nbig]);
    rp_data_release(r->data);
}

/* n zero bytes the device may read and not write, ending at a page that cannot be touched. */
static uint64_t rp_zero_block(RpRun *r, size_t n) {
    size_t page = (size_t)getpagesize();
    if (n > r->zero_len) {
        size_t len = r->zero_len ? r->zero_len : (size_t)1 << 20;
        while (len < n) len <<= 1;
        if (r->zero) munmap(r->zero, r->zero_len + page);
        r->zero = mmap(NULL, len + page, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (r->zero == MAP_FAILED || mprotect(r->zero + len, page, PROT_NONE)) rp_die("no memory for %zu zero bytes", n);
        r->zero_len = len;
        if (r->tab && r->record) r->tab->zero_region = len;
    }
    return (uint64_t)(uintptr_t)(r->zero + r->zero_len - n);
}

/* ---- the table -------------------------------------------------------------------------------------- */

static const char *rp_expected_reason(const char *method, const char *key, const char *cause) {
    for (unsigned i = 0; i < sizeof rp_expected / sizeof rp_expected[0]; i++) {
        const RpExpected *e = &rp_expected[i];
        const char *m = method;
        const char *em = e->method;
        if (em[0] == '*') { em++; m = strstr(method, "::"); if (!m) continue; }
        if (!strcmp(m, em) && (*e->what ? !strncmp(cause, e->what, strlen(e->what)) : strcmp(key, "result") != 0)) return e->reason;
    }
    return NULL;
}

static int32_t rp_row(RpRun *r, const char *method, const char *cause, const char *expected, const char *detail) {
    RpTable *t = r->tab;
    for (unsigned i = 0; i < t->n; i++)
        if (!strcmp(t->rows[i].cause, cause) && !strcmp(t->rows[i].method, method)) { t->rows[i].count++; t->rows[i].last = r->no; return (int32_t)i; }
    if (t->n == t->cap) { t->cap = t->cap ? t->cap * 2 : 64; t->rows = realloc(t->rows, t->cap * sizeof *t->rows); }
    t->rows[t->n] = (RpRow){ strdup(method), strdup(cause), strdup(detail ? detail : ""), expected, r->no, 1, r->no };
    return (int32_t)t->n++;
}

/* A difference between ours and the recording in the call being played.  key: "result",
 * "pDesc.BindFlags", ...; what: the cause's wording after the key. */
static int32_t rp_differ(RpRun *r, const char *key, const char *what, const char *fmt, ...) {
    char cause[512], detail[1024];
    if (!r->record) return -1;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(detail, sizeof detail, fmt, ap);
    va_end(ap);
    snprintf(cause, sizeof cause, "%s%s%s", key, *what ? " " : "", what);
    const char *exp = rp_expected_reason(r->method, key, cause);
    if (exp) {                                      /* one row per parameter, not per leaf */
        r->expected++;
        if (strcmp(key, "result")) snprintf(cause, sizeof cause, "%.*s differs", (int)strcspn(key, "."), key);
        for (unsigned i = 0; i < r->tab->n; i++)    /* and one count per call */
            if (r->tab->rows[i].expected && r->tab->rows[i].last == r->no && !strcmp(r->tab->rows[i].cause, cause) && !strcmp(r->tab->rows[i].method, r->method)) return (int32_t)i;
    } else r->differences++;
    return rp_row(r, r->method, cause, exp, detail);
}

#define RP_DATA_BODY
#include "data.h"                   /* the recorded data (--data): the functions */

/* ---- objects and mappings -------------------------------------------------------------------------- */

static RpObj *rp_obj(RpRun *r, uint64_t n) {
    if (n >= r->nobjs) {
        size_t cap = r->nobjs ? r->nobjs : 4096;
        while (cap <= n) cap *= 2;
        r->objs = realloc(r->objs, cap * sizeof *r->objs);
        memset(r->objs + r->nobjs, 0, (cap - r->nobjs) * sizeof *r->objs);
        r->nobjs = cap;
    }
    return &r->objs[n];
}

static void rp_never_made(RpRun *r, uint64_t n) {
    RpObj *o = rp_obj(r, n);
    o->state = 2; o->ptr = 0; o->held = 0; o->origin = r->origin; o->born = r->no;
}

static RpMap *rp_map_find(RpRun *r, uint32_t obj, uint32_t sub) {
    for (unsigned i = 0; i < r->nmaps; i++) if (r->maps[i].obj == obj && r->maps[i].sub == sub) return &r->maps[i];
    return NULL;
}

/* ---- reading a script line ------------------------------------------------------------------------- */

static const RpStruct *rp_struct(const char *name, size_t len) {
    for (unsigned i = 0; i < sizeof rp_structs / sizeof rp_structs[0]; i++)
        if (!strncmp(rp_structs[i].name, name, len) && !rp_structs[i].name[len]) return &rp_structs[i];
    return NULL;
}

static const RpLeaf *rp_leaf(const RpStruct *d, const char *path, size_t len, unsigned *hint) {
    for (unsigned k = 0; k < d->n; k++) {
        unsigned i = (*hint + k) % d->n;
        if (!strncmp(d->leaves[i].name, path, len) && !d->leaves[i].name[len]) { *hint = i + 1; return &d->leaves[i]; }
    }
    return NULL;
}

static uint64_t rp_string(RpRun *r, const char *s, const char **end) {      /* s after the opening quote */
    const char *q = strchr(s, '"');
    if (!q) rp_die("call %llu: open string", (unsigned long long)r->no);
    char *out = rp_alloc(r, (size_t)(q - s) + 1), *o = out;
    for (const char *p = s; p < q; p++) {
        if (*p == '%' && p + 2 < q + 1) { char h[3] = { p[1], p[2], 0 }; *o++ = (char)strtol(h, NULL, 16); p += 2; }
        else *o++ = *p;
    }
    *end = q + 1;
    return (uint64_t)(uintptr_t)out;
}

/* Fill (base != NULL, compare false) or compare one structure written as {path=v,...}; returns the
 * position after the closing brace.  key: the parameter's name, for the differences. */
static const char *rp_struct_body(RpRun *r, const RpStruct *d, uint8_t *base, const char *s, bool compare, const char *key) {
    unsigned hint = 0;
    if (*s != '{') rp_die("call %llu: expected { in %s", (unsigned long long)r->no, s);
    s++;
    while (*s != '}') {
        const char *eq = strchr(s, '=');
        if (!eq) rp_die("call %llu: no = in a structure", (unsigned long long)r->no);
        const RpLeaf *lf = rp_leaf(d, s, (size_t)(eq - s), &hint);
        const char *v = eq + 1, *end;
        uint64_t bits = 0;
        double f = 0;
        bool is_ptr = false;
        if (*v == 'b') { size_t n = strtoull(v + 1, (char **)&end, 10); bits = compare ? 1 : rp_in_block(r, n); is_ptr = true; }
        else if (*v == '"') { bits = compare ? 1 : rp_string(r, v + 1, &end); if (compare) end = strchr(v + 1, '"') + 1; is_ptr = true; }
        else {
            f = strtod(v, (char **)&end);
            const char *e2;
            long long ll = strtoll(v, (char **)&e2, 10);
            if (end == v) rp_die("call %llu: no value in %.40s", (unsigned long long)r->no, s);
            bits = e2 == end ? (uint64_t)ll : (f > -9e18 && f < 9e18) ? (uint64_t)(long long)f : 0;
            if (e2 == end && *v != '-') bits = strtoull(v, NULL, 10);
        }
        if (!lf) {
            if (!compare) rp_die("call %llu: structure %s has no leaf %.*s (runtime/tools/gen_replay.py reads the layout headers)",
                                 (unsigned long long)r->no, d->name, (int)(eq - s), s);
        } else if (!compare) {
            if (lf->kind == 'f') { float ff = (float)f; memcpy(base + lf->offset, &ff, 4); }
            else memcpy(base + lf->offset, &bits, lf->size);
        } else {
            char k[160];
            snprintf(k, sizeof k, "%s.%s", key, lf->name);
            uint64_t ours = 0;
            memcpy(&ours, base + lf->offset, lf->size);
            if (lf->kind == 'f') {
                float of; memcpy(&of, base + lf->offset, 4);
                if (!(fabs((double)of - f) <= 1e-6 * fmax(fabs(f), 1.0)) && !(isnan(of) && isnan(f)))
                    rp_differ(r, k, "differs", "ours %g, recorded %g", (double)of, f);
            } else if (lf->kind == 'p' || is_ptr) {
                if (!ours != !bits) rp_differ(r, k, ours ? "is a pointer where the recording has NULL" : "is NULL where the recording has a pointer", "ours %#llx", (unsigned long long)ours);
            } else {
                uint64_t mask = lf->size >= 8 ? ~UINT64_C(0) : (UINT64_C(1) << (8 * lf->size)) - 1;
                if ((ours & mask) != (bits & mask))
                    rp_differ(r, k, "differs", "ours %llu (%#llx), recorded %llu (%#llx)", (unsigned long long)ours,
                              (unsigned long long)ours, (unsigned long long)(bits & mask), (unsigned long long)(bits & mask));
            }
        }
        s = end;
        if (*s == ',') s++;
    }
    return s + 1;
}

static unsigned rp_count_structs(const char *s) {       /* s at '[' */
    unsigned n = 0;
    for (s++; *s == '{'; n++) { s = strchr(s, '}'); if (!s) break; s++; }
    return n;
}

static unsigned rp_count_list(const char *s) {          /* s at '[' */
    if (s[1] == ']') return 0;
    unsigned n = 1;
    for (s++; *s && *s != ']'; s++) n += *s == ',';
    return n;
}

/* One argument token -> the 64 bits (or float) to pass.  Returns false when the call cannot be
 * played because an object it needs was never made (r->origin says why). */
static bool rp_arg(RpRun *r, const RtComMethod *m, unsigned i, const char *t, RtArg *a, RpOut *outs, unsigned *nouts) {
    const char *argname = m ? m->arg_names[i] : "arg";
    *a = RT_ARG_U(0);
    switch (*t) {
    case 'i': *a = RT_ARG_U(*(t + 1) == '-' ? (uint64_t)strtoll(t + 1, NULL, 10) : strtoull(t + 1, NULL, 10)); return true;
    case 'f': *a = RT_ARG_F(strtof(t + 1, NULL)); return true;
    case 'n': return true;
    case 'g': {
        uint8_t *g = rp_alloc(r, 16);
        for (int k = 0; k < 16; k++) { char h[3] = { t[1 + 2 * k], t[2 + 2 * k], 0 }; g[k] = (uint8_t)strtoul(h, NULL, 16); }
        *a = RT_ARG_U((uintptr_t)g);
        return true;
    }
    case 'o': {
        RpObj *o = rp_obj(r, strtoull(t + 1, NULL, 10));
        if (o->state != 1) { r->origin = o->state == 2 ? o->origin : -2; return false; }
        *a = RT_ARG_U(o->ptr);
        return true;
    }
    case 'O': {
        unsigned n = rp_count_list(t + 1);
        uint64_t *v = rp_alloc(r, 8 * (n ? n : 1));
        const char *p = t + 2;
        for (unsigned k = 0; k < n; k++) {
            uint64_t num = strtoull(p, (char **)&p, 10);
            p++;
            if (!num) continue;
            RpObj *o = rp_obj(r, num);
            if (o->state != 1) { r->origin = o->state == 2 ? o->origin : -2; return false; }
            v[k] = o->ptr;
        }
        *a = RT_ARG_U((uintptr_t)v);
        return true;
    }
    case 's': case 'S': {
        const char *b = strpbrk(t + 1, "{[");
        const RpStruct *d = b ? rp_struct(t + 1, (size_t)(b - (t + 1))) : NULL;
        if (!d) rp_die("call %llu: no layout for the structure in %.60s", (unsigned long long)r->no, t);
        unsigned n = *t == 'S' ? rp_count_structs(b) : 1;
        uint8_t *v = rp_alloc(r, (size_t)d->size * (n ? n : 1));
        const char *p = *t == 'S' ? b + 1 : b;
        for (unsigned k = 0; k < n; k++) p = rp_struct_body(r, d, v + (size_t)k * d->size, p, false, argname);
        *a = RT_ARG_U((uintptr_t)v);
        return true;
    }
    case 'I': case 'F': {
        unsigned w = *t == 'F' ? 4 : (unsigned)(t[1] - '0');
        const char *b = strchr(t, '[');
        unsigned n = rp_count_list(b);
        uint8_t *v = rp_alloc(r, (size_t)w * (n ? n : 1));
        const char *p = b + 1;
        for (unsigned k = 0; k < n; k++) {
            if (*t == 'F') { float f = strtof(p, (char **)&p); memcpy(v + 4 * k, &f, 4); }
            else { uint64_t x = *p == '-' ? (uint64_t)strtoll(p, (char **)&p, 10) : strtoull(p, (char **)&p, 10); memcpy(v + (size_t)w * k, &x, w); }
            p++;
        }
        *a = RT_ARG_U((uintptr_t)v);
        return true;
    }
    case 'b': *a = RT_ARG_U(rp_in_block(r, strtoull(t + 1, NULL, 10))); return true;
    case 'B': {
        size_t n = strtoull(t + 1, NULL, 10);
        uint64_t recorded = r->data ? rp_data_block(r, n, true) : 0;
        if (recorded) { *a = RT_ARG_U(recorded); return true; }
        uint8_t *v = rp_alloc(r, n < 4 ? 4 : n);
        memcpy(v, "DXBC", 4);
        *a = RT_ARG_U((uintptr_t)v);
        return true;
    }
    case '>': break;
    default: rp_die("call %llu: argument %s", (unsigned long long)r->no, t);
    }
    RpOut *o = &outs[(*nouts)++];
    *o = (RpOut){ .arg = i, .text = t };
    switch (t[1]) {
    case 'o': o->kind = RP_O_OBJ; o->count = 1; break;
    case 'O': o->kind = RP_O_OBJS; o->count = rp_count_list(t + 2); break;
    case 'd': {
        size_t n = strtoull(t + 2, NULL, 10);
        *a = RT_ARG_U((uintptr_t)rp_alloc(r, n ? n : 1));
        (*nouts)--;
        return true;
    }
    case 's': case 'S': {
        const char *b = strpbrk(t + 2, "{[?");
        o->def = b ? rp_struct(t + 2, (size_t)(b - (t + 2))) : NULL;
        if (!o->def) rp_die("call %llu: no layout for the structure in %.60s", (unsigned long long)r->no, t);
        o->kind = t[1] == 's' ? RP_O_STRUCT : RP_O_STRUCTS;
        o->count = t[1] == 's' ? 1 : rp_count_structs(b);
        o->text = b;
        size_t n = o->count > r->cap_hint ? o->count : (size_t)r->cap_hint;
        uint8_t *v = rp_alloc(r, o->def->size * (n ? n : 1));
        memset(v, 0xCD, o->def->size * (n ? n : 1));
        o->ptr = (uint64_t)(uintptr_t)v;
        *a = RT_ARG_U(o->ptr);
        return true;
    }
    case 'I': case 'F': {
        o->kind = t[1] == 'I' ? RP_O_INTS : RP_O_FLOATS;
        o->width = t[1] == 'F' ? 4 : (unsigned)(t[2] - '0');
        const char *b = strpbrk(t + 2, "[?");
        o->text = b;
        o->count = *b == '[' ? rp_count_list(b) : 1;
        uint8_t *v = rp_alloc(r, (size_t)o->width * (o->count ? o->count : 1));
        memset(v, 0xCD, (size_t)o->width * (o->count ? o->count : 1));
        o->ptr = (uint64_t)(uintptr_t)v;
        *a = RT_ARG_U(o->ptr);
        /* a count the caller fills in before the call (pNumModes, pNumViewports): the recorded value,
         * or ours when the recording passes on what the last call of this method wrote */
        if (o->kind == RP_O_INTS && o->count == 1 && *b == '[' && (!strncmp(argname, "pNum", 4) || strstr(argname, "count"))) {
            uint64_t rec = strtoull(b + 1, NULL, 10), in = rec;
            for (unsigned k = 0; k < r->ninout; k++)
                if (r->inout[k].m == m && r->inout[k].arg == i && r->inout[k].rec == rec) in = r->inout[k].ours;
            memcpy(v, &in, o->width);
            o->inout = true;
            if (in > r->cap_hint) r->cap_hint = in;
        }
        return true;
    }
    default: rp_die("call %llu: argument %s", (unsigned long long)r->no, t);
    }
    uint64_t *slot = rp_alloc(r, 8 * (o->count ? o->count : 1));
    for (unsigned k = 0; k < (o->count ? o->count : 1); k++) slot[k] = RP_POISON;
    o->ptr = (uint64_t)(uintptr_t)slot;
    *a = RT_ARG_U(o->ptr);
    return true;
}

/* After a call that was made (made) or was not (a stop, or an object missing): hold what the device
 * wrote against the recording and bind the objects it returned. */
static void rp_outs(RpRun *r, const RtComMethod *m, const RpOut *outs, unsigned nouts, bool made) {
    for (unsigned n = 0; n < nouts; n++) {
        const RpOut *o = &outs[n];
        const char *key = m ? m->arg_names[o->arg] : (o->arg == 7 ? "ppDevice" : o->arg == 9 ? "ppImmediateContext" : "pFeatureLevel");
        if (o->kind == RP_O_OBJ || o->kind == RP_O_OBJS) {
            const char *p = o->text + (o->kind == RP_O_OBJ ? 2 : 3);
            for (unsigned k = 0; k < o->count; k++) {
                uint64_t ours = made ? rp_rd64(o->ptr + 8 * k) : 0;
                if (*p == '?') { if (made && ours && ours != RP_POISON) rp_differ(r, key, "is written where the recording wrote nothing", "ours %#llx", (unsigned long long)ours); break; }
                uint64_t num = strtoull(p, (char **)&p, 10);
                p++;
                if (ours == RP_POISON) ours = 0;
                if (!num) { if (ours) rp_differ(r, key, "is an object where the recording has NULL", "ours %#llx", (unsigned long long)ours); continue; }
                RpObj *ob = rp_obj(r, num);
                if (!ours) {
                    if (made && r->origin < 0) r->origin = rp_differ(r, key, "is NULL where the recording has an object", "object %llu of the script", (unsigned long long)num);
                    if (ob->state != 1) rp_never_made(r, num);
                    else { ob->owed++; ob->owed_origin = r->origin; }
                    continue;
                }
                if (ob->state == 1 && ob->ptr != ours)
                    rp_differ(r, key, "is not the object the device returned before", "ours %#llx; object %llu of the script was %#llx (call %llu)",
                              (unsigned long long)ours, (unsigned long long)num, (unsigned long long)ob->ptr, (unsigned long long)ob->born);
                if (ob->state != 1 || ob->ptr != ours) { ob->state = 1; ob->ptr = ours; ob->born = r->no; ob->held = 0; }
                ob->held++;
            }
        } else if (!made || *o->text == '?') {
            continue;
        } else if (o->kind == RP_O_STRUCT || o->kind == RP_O_STRUCTS) {
            const char *p = o->kind == RP_O_STRUCT ? o->text : o->text + 1;
            for (unsigned k = 0; k < o->count; k++) p = rp_struct_body(r, o->def, (uint8_t *)(uintptr_t)o->ptr + (size_t)k * o->def->size, p, true, key);
        } else {
            const char *p = o->text + 1;
            for (unsigned k = 0; k < o->count; k++) {
                uint64_t ours = 0;
                memcpy(&ours, (const void *)(uintptr_t)(o->ptr + (uint64_t)o->width * k), o->width);
                if (o->kind == RP_O_FLOATS) {
                    float of; memcpy(&of, &ours, 4);
                    double rec = strtod(p, (char **)&p);
                    if (!(fabs((double)of - rec) <= 1e-6 * fmax(fabs(rec), 1.0))) rp_differ(r, key, "differs", "ours %g, recorded %g", (double)of, rec);
                } else {
                    uint64_t rec = *p == '-' ? (uint64_t)strtoll(p, (char **)&p, 10) : strtoull(p, (char **)&p, 10);
                    uint64_t mask = o->width >= 8 ? ~UINT64_C(0) : (UINT64_C(1) << (8 * o->width)) - 1;
                    if (o->inout && o->count == 1) {
                        unsigned q = 0;
                        while (q < r->ninout && !(r->inout[q].m == m && r->inout[q].arg == o->arg)) q++;
                        if (q == r->ninout && r->ninout < sizeof r->inout / sizeof r->inout[0]) r->ninout++;
                        if (q < r->ninout) r->inout[q] = (RpInOut){ m, o->arg, rec, ours };
                    }
                    if ((ours & mask) != (rec & mask))
                        rp_differ(r, key, "differs", "ours %llu (%#llx), recorded %llu (%#llx)", (unsigned long long)ours, (unsigned long long)ours,
                                  (unsigned long long)(rec & mask), (unsigned long long)(rec & mask));
                }
                p++;
            }
        }
    }
}

/* ---- methods by name ------------------------------------------------------------------------------- */

typedef struct { const char *name; const RtComIface *iface; const RtComMethod *m; } RpMethod;
static RpMethod rp_methods[2048];

static unsigned rp_hash(const char *s, size_t n) {
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
    return h;
}

static void rp_methods_init(void) {
    if (rp_methods[0].name || rp_methods[1].name) return;
    for (unsigned i = 0; i < rt_com_table.nifaces; i++) {
        const RtComIface *f = rt_com_table.ifaces[i];
        for (unsigned k = 0; k < f->nmethods; k++) {
            const char *nm = f->methods[k].entry.name;
            unsigned h = rp_hash(nm, strlen(nm)) % 2048;
            while (rp_methods[h].name) h = (h + 1) % 2048;
            rp_methods[h] = (RpMethod){ nm, f, &f->methods[k] };
        }
    }
}

static const RpMethod *rp_method(const char *name, size_t n) {
    for (unsigned h = rp_hash(name, n) % 2048; rp_methods[h].name; h = (h + 1) % 2048)
        if (!strncmp(rp_methods[h].name, name, n) && !rp_methods[h].name[n]) return &rp_methods[h];
    return NULL;
}

static const RtComIface *rp_iface_of(uint64_t obj) {
    uint64_t vt = rp_rd64(obj);
    for (unsigned i = 0; i < rt_com_table.nifaces; i++) if (rt_com_table.ifaces[i]->vtable == vt) return rt_com_table.ifaces[i];
    return NULL;
}

/* ---- one line -------------------------------------------------------------------------------------- */

/* stopped: the scout died in this line; `report` is what it wrote.  Returns the number of
 * differences that are not ours by design (a stop counts as one). */
static int rp_play_line(RpRun *r, const char *line, bool stopped, const char *report) {
    char buf[256], *copy = NULL, *tok[24];
    size_t len = strlen(line);
    char *s = len < sizeof buf ? memcpy(buf, line, len + 1) : (copy = strdup(line));
    unsigned nt = 0;
    for (char *p = strtok(s, " "); p && nt < 24; p = strtok(NULL, " ")) tok[nt++] = p;
    if (nt < 3) { free(copy); return 0; }
    RpTable *t = r->tab;
    r->no = strtoull(tok[0], NULL, 10);
    r->line = line; r->method = tok[2]; r->differences = r->expected = 0; r->origin = -1; r->cap_hint = 0;
    if (r->record) t->lines++;
    char mark = tok[1][0];

    if (mark == 'W') {                              /* <no> W <object> <subresource> <offset> <bytes> */
        if (nt < 6) rp_die("line %s", line);
        r->method = "write into mapped memory";
        uint32_t obj = (uint32_t)strtoul(tok[2], NULL, 10), sub = (uint32_t)strtoul(tok[3], NULL, 10);
        uint64_t off = strtoull(tok[4], NULL, 10), n = strtoull(tok[5], NULL, 10);
        RpMap *mp = obj ? rp_map_find(r, obj, sub) : NULL;
        if (r->record) t->writes++;
        if (stopped) {
            const RtComIface *f = mp ? rp_iface_of(rp_obj(r, obj)->ptr) : NULL;
            char d[400];
            snprintf(d, sizeof d, "%llu bytes at offset %llu of the mapping of a %s: %.200s", (unsigned long long)n,
                     (unsigned long long)off, f ? f->name : "resource", report);
            rp_row(r, r->method, "STOP: the write ends outside the memory Map handed out (or the process died in it)", NULL, d);
            t->stops++;
            free(copy);
            return 1;
        }
        if (!mp || !mp->ptr) { if (r->record) rp_row(r, r->method, obj ? "not played: its Map was not made" : "not played: the converter found no open mapping at that address", NULL, line); }
        else {
            if (!r->data || !rp_data_write(r, (void *)(uintptr_t)(mp->ptr + off), (size_t)n)) memset((void *)(uintptr_t)(mp->ptr + off), 0, (size_t)n);
            if (r->record) t->writes_done++;
        }
        free(copy);
        return 0;
    }
    if (mark == 'U') { if (r->record) { t->undescribed++; rp_row(r, r->method, "not played: the interface descriptions do not have this method", NULL, line); } free(copy); return 0; }
    if (mark == 'F' && !r->opt.play_foreign) { if (r->record) t->foreign++; free(copy); return 0; }
    for (unsigned k = 0; k < r->opt.nskip; k++) if (!strcmp(r->opt.skip[k], tok[2])) {   /* --skip-method: nothing of the device is called */
        if (r->record) {
            for (unsigned j = 3; j < nt; j++)
                if (tok[j][0] == '>' && tok[j][1] == 'o')
                    rp_die("call %llu (line %s): %s returns an object, and a call that returns an object cannot be skipped (--skip-method)",
                           (unsigned long long)r->no, line, tok[2]);
            if (!t->skipped[k]++) t->skip_first[k] = r->no;
        }
        free(copy);
        return 0;
    }
    if (r->record) t->game_calls++;

    const RpMethod *rm = strstr(tok[2], "::") ? rp_method(tok[2], strlen(tok[2])) : NULL;
    const RtComMethod *m = rm ? rm->m : NULL;
    bool is_create = !strcmp(tok[2], "D3D11CreateDevice");
    bool is_map = m && !strcmp(m->name, "Map") && !strcmp(m->iface->name, "ID3D11DeviceContext");
    if (!m && !is_create) { if (r->record) { t->undescribed++; rp_row(r, r->method, "not played: the runtime's descriptions do not have this method", NULL, line); } free(copy); return 0; }
    unsigned nargs = nt - 4;
    if (m && nargs != m->entry.nargs) rp_die("call %llu: %s takes %u arguments, the script has %u", (unsigned long long)r->no, tok[2], m->entry.nargs, nargs);
    if (nargs > RT_GUEST_CALL_MAX_ARGS) rp_die("call %llu: too many arguments", (unsigned long long)r->no);

    RtArg a[RT_GUEST_CALL_MAX_ARGS];
    RpOut outs[RT_GUEST_CALL_MAX_ARGS];
    unsigned nouts = 0;
    bool can = true;
    for (unsigned i = 0; i < nargs; i++)
        if (!rp_arg(r, m, i, tok[4 + i], &a[i], outs, &nouts)) { can = false; }

    uint64_t fn = 0;
    if (can && m) {
        const RtComIface *f = rp_iface_of(a[0].v.u);
        if (!f || m->slot >= f->nmethods || strcmp(f->methods[m->slot].name, m->name)) {
            if (r->record) { r->origin = rp_row(r, r->method, "not played: our object is not presented as this interface", NULL, f ? f->name : "an object of no described interface"); t->not_played++; }
            can = false;
            stopped = false;
            goto after;
        }
        fn = rp_rd64(rp_rd64(a[0].v.u) + 8 * (uint64_t)m->slot);       /* mov rax,[rcx]; call [rax+8*slot] */
    } else if (can) {
        RtModule *mod = rt_module_find("d3d11.dll");
        fn = mod ? rt_module_export(mod, "D3D11CreateDevice", 0) : 0;
        if (!fn) rp_die("d3d11.dll!D3D11CreateDevice is not there");
    }
    if (can && m && !strcmp(m->name, "Release")) {
        RpObj *ob = rp_obj(r, strtoull(tok[4] + 1, NULL, 10));
        if (ob->owed > 0) {                         /* the reference this Release gives back was never given to us */
            ob->owed--;
            if (r->record) {
                char cause[600];
                if (ob->owed_origin >= 0) snprintf(cause, sizeof cause, "not played: gives back a reference our device never gave (%s: %s)", t->rows[ob->owed_origin].method, t->rows[ob->owed_origin].cause);
                else snprintf(cause, sizeof cause, "not played: gives back a reference of a call that was not played");
                rp_row(r, r->method, cause, NULL, line);
                t->not_played++;
            }
            can = false;
            stopped = false;
            r->origin = ob->owed_origin;
            goto after;
        }
    }
    if (can && m && !strcmp(m->name, "Unmap") && !strcmp(m->iface->name, "ID3D11DeviceContext")) {
        RpMap *mp = rp_map_find(r, (uint32_t)strtoul(tok[5] + 1, NULL, 10), (uint32_t)strtoul(tok[6] + 1, NULL, 10));
        if (mp && !mp->ptr) {                       /* the Map this Unmap ends was not made */
            r->origin = mp->origin;
            *mp = r->maps[--r->nmaps];
            if (r->record) {
                char cause[600];
                if (r->origin >= 0) snprintf(cause, sizeof cause, "not played: its Map was not made (%s: %s)", t->rows[r->origin].method, t->rows[r->origin].cause);
                else snprintf(cause, sizeof cause, "not played: its Map was not made");
                int32_t keep = r->origin;
                rp_row(r, r->method, cause, NULL, line);
                r->origin = keep;
                t->not_played++;
            }
            can = false;
            stopped = false;
            goto after;
        }
    }
    if (!can) {
        if (r->record) {
            char cause[600];
            if (r->origin >= 0) snprintf(cause, sizeof cause, "not played: needs an object our device never returned (%s: %s)", t->rows[r->origin].method, t->rows[r->origin].cause);
            else snprintf(cause, sizeof cause, "not played: uses a pointer that no recorded call returned");
            int32_t keep = r->origin;
            rp_row(r, r->method, cause, NULL, line);
            r->origin = keep;
            t->not_played++;
        }
        stopped = false;
        goto after;
    }
    if (!stopped && m && r->nobody[rm - rp_methods]) {          /* a slot with no body: it stopped before and stops again */
        if (r->record) { r->origin = rp_row(r, r->method, t->rows[r->nobody[rm - rp_methods] - 1].cause, NULL, ""); t->stops++; t->stops_not_tried++; }
        can = false;
        goto after;
    }
    if (stopped) {
        const char *f = strstr(report, "FATAL");
        char cause[400], *c = cause + snprintf(cause, 8, "STOP: ");
        if (f) {                                    /* the report's first line, every number (not part of a name) as # */
            for (f += 5; *f == ' '; f++) { }
            for (; *f && *f != '\n' && c < cause + sizeof cause - 2; ) {
                bool word = (c[-1] >= 'A' && c[-1] <= 'Z') || (c[-1] >= 'a' && c[-1] <= 'z') || (c[-1] >= '0' && c[-1] <= '9') || c[-1] == '_' || c[-1] == '#';
                if (*f >= '0' && *f <= '9' && !word) {
                    if (f[0] == '0' && f[1] == 'x') f += 2;
                    while ((*f >= '0' && *f <= '9') || (*f >= 'a' && *f <= 'f') || (*f >= 'A' && *f <= 'F')) f++;
                    *c++ = '#';
                } else *c++ = *f++;
            }
            *c = 0;
        }
        else snprintf(c, 300, "the process died with no fatal report");
        const char *rest = f && *f ? f + 1 : report;
        r->origin = rp_row(r, r->method, cause, NULL, rest);
        if (m && m->body == RT_COM_BODY_STOP && strstr(report, "unimplemented interface method")) r->nobody[rm - rp_methods] = r->origin + 1;
        t->stops++;
        can = false;
        goto after;
    }

    RtGuestRet ret = rt_guest_call_ex(r->ctx, fn, a, nargs);
    if (r->record) t->played++;

    const char *rtype = m ? m->ret_type : "HRESULT";
    if (tok[3][0] == '=' && strcmp(rtype, "void")) {
        uint32_t rec = (uint32_t)strtoul(tok[3] + 1, NULL, 10), ours = (uint32_t)ret.rax;
        if (rec != ours) {
            if (!strcmp(rtype, "ULONG")) rp_differ(r, "result", "(a count) differs", "ours %u, recorded %u", ours, rec);
            else {
                char w[64];
                snprintf(w, sizeof w, "is %#x where the recording has %#x", ours, rec);
                int32_t row = rp_differ(r, "result", w, "%s", "");
                if ((int32_t)ours < 0 && (int32_t)rec >= 0) r->origin = row;
            }
        }
    }
    if (is_map && (uint32_t)ret.rax != 0) can = false;           /* a Map that failed: recorded below as not made */
    if (is_map && can && nouts) {
        uint32_t obj = (uint32_t)strtoul(tok[5] + 1, NULL, 10), sub = (uint32_t)strtoul(tok[6] + 1, NULL, 10);
        RpMap *mp = rp_map_find(r, obj, sub);
        if (!mp) {
            if (r->nmaps == r->capmaps) { r->capmaps = r->capmaps ? r->capmaps * 2 : 256; r->maps = realloc(r->maps, r->capmaps * sizeof *r->maps); }
            mp = &r->maps[r->nmaps++];
        }
        *mp = (RpMap){ obj, sub, rp_rd64(outs[nouts - 1].ptr + offsetof(D3D11_MAPPED_SUBRESOURCE, pData)), -1 };
    }
    if (m && !strcmp(m->name, "Unmap") && !strcmp(m->iface->name, "ID3D11DeviceContext")) {
        RpMap *mp = rp_map_find(r, (uint32_t)strtoul(tok[5] + 1, NULL, 10), (uint32_t)strtoul(tok[6] + 1, NULL, 10));
        if (mp) *mp = r->maps[--r->nmaps];
    }
    if (m && !strcmp(m->name, "AddRef")) rp_obj(r, strtoull(tok[4] + 1, NULL, 10))->held++;
    if (m && !strcmp(m->name, "Release")) rp_obj(r, strtoull(tok[4] + 1, NULL, 10))->held--;

after:
    if (is_map && !can && nt > 6 && tok[5][0] == 'o' && tok[3][0] == '=' && tok[3][1] == '0' && !tok[3][2]) {
        uint32_t obj = (uint32_t)strtoul(tok[5] + 1, NULL, 10), sub = (uint32_t)strtoul(tok[6] + 1, NULL, 10);
        RpMap *mp = rp_map_find(r, obj, sub);       /* the recording mapped it: its writes and its Unmap follow */
        if (!mp) {
            if (r->nmaps == r->capmaps) { r->capmaps = r->capmaps ? r->capmaps * 2 : 256; r->maps = realloc(r->maps, r->capmaps * sizeof *r->maps); }
            mp = &r->maps[r->nmaps++];
        }
        *mp = (RpMap){ obj, sub, 0, r->origin };
    }
    rp_outs(r, m, outs, nouts, can);
    if (r->record && can) {
        if (r->differences) t->with_differences++;
        else if (r->expected) t->with_expected_only++;
        else t->clean++;
    }
    int bad = r->differences + (stopped ? 1 : 0);
    rp_reset(r);
    free(copy);
    return bad;
}

/* ---- the scout ------------------------------------------------------------------------------------- */

/* In the scout only: the runtime's stop ends in abort(), and a process that dies of SIGABRT is
 * written up by the system's crash reporter, which costs a large part of a second each time.  The
 * scout has printed its report by then, so it leaves through _exit instead.  FOR A REPORT THAT IS
 * THE RUNTIME'S OWN QUIET END (rt_report_quiet(RT_QUIET_ALL) in the child, rt/crash.h): the report
 * sets SIGABRT back to its default before it aborts, so a handler installed here never ran for
 * one, and the scout died of the signal after all (25 crash reports named d3d-replay were the
 * evidence).  With the quiet end the scout exits with status 134.  The handler below is for
 * what is left: an abort() outside a report, and a fault the runtime does not take (a write
 * past the memory Map handed out). */
#define RP_EXIT_STOP 86
static void rp_scout_signal(int sig) {
    static const char msg[] = "\nFATAL the scout died of a signal (a fault outside the runtime's own handling)\n";
    if (sig != SIGABRT) (void)!write(2, msg, sizeof msg - 1);
    _exit(RP_EXIT_STOP);
}

/* Plays lines[from..n) in a forked child.  Returns n when the child lived, or the index of the line
 * it died in, with what it wrote on standard error in `report`. */
static unsigned rp_scout(RpRun *r, char **lines, unsigned from, unsigned n, char *report, size_t cap) {
    int fd[2];
    report[0] = 0;
    if (pipe(fd)) rp_die("pipe: %s", strerror(errno));
    fflush(stdout);
    fflush(stderr);
    *r->at = from;
    pid_t pid = fork();
    if (pid < 0) rp_die("fork: %s", strerror(errno));
    if (pid == 0) {
        rt_log_forked_child();
        rt_dispatcher_forked_child();
        rt_log_set_sink(NULL, NULL);                /* what the child logs goes to the pipe with the report */
        close(fd[0]);
        dup2(fd[1], 2);
        close(fd[1]);
        alarm(300);
        rt_report_quiet(RT_QUIET_ALL);              /* a stop is what the scout is for: exit status 134, no crash report */
        signal(SIGABRT, rp_scout_signal);
        for (int sg = SIGBUS; sg <= SIGSEGV; sg++) {       /* SIGBUS 10, SIGSEGV 11: only where the runtime has no handler */
            struct sigaction old;
            if (!sigaction(sg, NULL, &old) && old.sa_handler == SIG_DFL && !(old.sa_flags & SA_SIGINFO)) signal(sg, rp_scout_signal);
        }
        r->record = false;
        for (unsigned i = from; i < n; i++) { *r->at = i; rp_play_line(r, lines[i], false, NULL); }
        _exit(0);
    }
    close(fd[1]);
    size_t used = 0;
    char junk[4096];
    for (;;) {                                      /* keep the head of what it wrote, and the fatal report */
        ssize_t k = read(fd[0], used < cap - 1 ? report + used : junk, used < cap - 1 ? cap - 1 - used : sizeof junk);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) break;
        if (used < cap - 1) {
            used += (size_t)k;
            report[used] = 0;
            char *f = strstr(report, "FATAL");
            if (!f && used > cap / 2) { memmove(report, report + used - 256, 256); used = 256; report[used] = 0; }
        }
    }
    close(fd[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    r->tab->scouts++;
    if (WIFSIGNALED(status)) r->tab->scouts_by_signal++;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return n;
    char *f = strstr(report, "FATAL");
    if (f && f != report) memmove(report, f, strlen(f) + 1);
    char *tbl = strstr(report, "\ncom: calls through interface slots");      /* the layer's count table follows every report */
    if (tbl) *tbl = 0;
    if (!f) snprintf(report + strlen(report), cap - strlen(report), " [wait status %#x%s]", status, WIFSIGNALED(status) && WTERMSIG(status) == SIGALRM ? ": no answer for 300 s" : "");
    return *r->at;
}

/* ---- a script ------------------------------------------------------------------------------------- */

static void rp_first(RpRun *r, const char *line, const char *report, unsigned rows_before) {
    FILE *o = r->opt.out ? r->opt.out : stderr;
    RpTable *t = r->tab;
    fprintf(o, "d3d-replay: FIRST DISAGREEMENT at call %llu, script line %llu, after %llu calls played\n  %s\n",
            (unsigned long long)r->no, (unsigned long long)t->lines, (unsigned long long)t->played, line);
    for (unsigned i = rows_before; i < t->n; i++)
        if (!t->rows[i].expected) fprintf(o, "  %s: %s\n    %s\n", t->rows[i].method, t->rows[i].cause, t->rows[i].detail);
    if (report && *report) fprintf(o, "  the scout's report:\n%s\n", report);
    fprintf(o, "  our state: the objects this line names\n");
    for (const char *p = line; (p = strpbrk(p, "oO")) != NULL; p++) {
        if (p == line || (p[-1] != ' ' && p[-1] != '>')) continue;
        const char *q = p + 1 + (*p == 'O');
        while (*q >= '0' && *q <= '9') {
            uint64_t num = strtoull(q, (char **)&q, 10);
            if (num && num < r->nobjs) {
                const RpObj *ob = &r->objs[num];
                const RtComIface *f = ob->state == 1 ? rp_iface_of(ob->ptr) : NULL;
                fprintf(o, "    object %llu: %s%s %#llx, returned at call %llu, %lld references held by the script\n", (unsigned long long)num,
                        ob->state == 1 ? "" : ob->state == 2 ? "never made " : "not bound ", f ? f->name : "", (unsigned long long)ob->ptr,
                        (unsigned long long)ob->born, (long long)ob->held);
            }
            if (*q != ',') break;
            q++;
        }
    }
    fprintf(o, "  %u mappings open; reproduce: build/runtime/d3d-replay --first --until %llu <the same script files>\n", r->nmaps, (unsigned long long)t->lines);
}

/* Plays a whole script.  Returns 0 when nothing differs beyond what is ours by design, 1 otherwise. */
RP_API int rp_run(Ctx *ctx, FILE *script, const RpOptions *opt, RpTable *tab) {
    RpRun r = { .ctx = ctx, .opt = *opt, .tab = tab, .record = true };
    unsigned chunk = opt->chunk ? opt->chunk : 5000, n;
    char **lines = calloc(chunk, sizeof *lines);
    char *report = malloc(65536), *buf = NULL;
    size_t bufcap = 0;
    struct timespec t0, t1;
    int bad = 0;
    bool end = false;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rp_methods_init();
    if (opt->data) { r.data = rp_data_open(opt->data); tab->data_dir = opt->data; }
    r.at = mmap(NULL, (size_t)getpagesize(), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (r.at == MAP_FAILED) rp_die("mmap: %s", strerror(errno));
    rp_alloc(&r, 16);
    rp_reset(&r);
    uint64_t total = 0;
    while (!end && !(bad && !opt->keep_going)) {
        if (opt->progress && total && total % (20 * (uint64_t)chunk) == 0) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            fprintf(stderr, "d3d-replay: %llu lines, %llu scouts, %llu stops, %u rows, %.0f s\n", (unsigned long long)total,
                    (unsigned long long)tab->scouts, (unsigned long long)tab->stops, tab->n, (double)(t1.tv_sec - t0.tv_sec));
        }
        for (n = 0; n < chunk; n++) {
            ssize_t k = getline(&buf, &bufcap, script);
            if (k <= 0 || (opt->until && total >= opt->until)) { end = true; break; }
            if (buf[k - 1] == '\n') buf[k - 1] = 0;
            lines[n] = strdup(buf);
            total++;
        }
        for (unsigned from = 0; from < n && !(bad && !opt->keep_going); ) {
            unsigned stop = opt->scout ? rp_scout(&r, lines, from, n, report, 65536) : n;
            for (unsigned i = from; i < stop && !(bad && !opt->keep_going); i++) {
                unsigned rows = tab->n;
                if (rp_play_line(&r, lines[i], false, NULL)) { bad = 1; if (!opt->keep_going) rp_first(&r, lines[i], NULL, rows); }
            }
            if (stop < n && !(bad && !opt->keep_going)) {
                unsigned rows = tab->n;
                if (rp_play_line(&r, lines[stop], true, report)) { bad = 1; if (!opt->keep_going) rp_first(&r, lines[stop], report, rows); }
            }
            from = stop + 1;
        }
        for (unsigned i = 0; i < n; i++) free(lines[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    tab->peak_rss = (uint64_t)ru.ru_maxrss;
    /* The peak of the process's memory footprint (what it has dirtied, compressed or not): unlike
     * the resident size it does not depend on how hard the rest of the machine presses on memory. */
    task_vm_info_data_t vi;
    mach_msg_type_number_t vc = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vi, &vc) == KERN_SUCCESS)
        tab->peak_footprint = vc >= TASK_VM_INFO_REV3_COUNT ? (uint64_t)vi.ledger_phys_footprint_peak : (uint64_t)vi.phys_footprint;
    tab->seconds += (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    munmap((void *)r.at, (size_t)getpagesize());
    free(lines); free(report); free(buf); free(r.objs); free(r.maps); free(r.arena);
    if (r.zero) munmap(r.zero, r.zero_len + (size_t)getpagesize());
    rp_data_close(r.data);
    return bad;
}

/* ---- printing ------------------------------------------------------------------------------------- */

static int rp_row_cmp(const void *a, const void *b) {
    const RpRow *x = a, *y = b;
    return x->count < y->count ? 1 : x->count > y->count ? -1 : x->first < y->first ? -1 : x->first > y->first;
}

static void rp_cell(FILE *o, const char *s, size_t max) {
    for (size_t i = 0; s[i] && i < max; i++) fputc(s[i] == '|' ? '/' : s[i] == '\n' ? ' ' : s[i], o);
    if (strlen(s) > max) fputs("...", o);
}

/* The table as Markdown: first what differs or stops, most frequent first, then what is ours by design. */
RP_API void rp_table_print(RpTable *t, FILE *o) {
    qsort(t->rows, t->n, sizeof *t->rows, rp_row_cmp);
    fprintf(o, "%llu script lines: %llu calls of the game (%llu played, %llu not played, %llu stops), %llu calls that are not the game's, "
            "%llu writes into mapped memory (%llu made), %llu calls of methods without a description.\n\n",
            (unsigned long long)t->lines, (unsigned long long)t->game_calls, (unsigned long long)t->played, (unsigned long long)t->not_played,
            (unsigned long long)t->stops, (unsigned long long)t->foreign, (unsigned long long)t->writes, (unsigned long long)t->writes_done,
            (unsigned long long)t->undescribed);
    fprintf(o, "Of the calls played: %llu agree in everything compared, %llu differ only in what is ours by design, %llu differ otherwise.\n\n",
            (unsigned long long)t->clean, (unsigned long long)t->with_expected_only, (unsigned long long)t->with_differences);
    fprintf(o, "%.1f s, %llu scouts (%llu stops of methods with no body were counted without one), peak memory footprint %.1f MB, peak resident %.1f MB "
            "(of it up to %.1f MB the player's block of zero bytes).\n\n", t->seconds, (unsigned long long)t->scouts, (unsigned long long)t->stops_not_tried,
            (double)t->peak_footprint / 1048576.0, (double)t->peak_rss / 1048576.0, (double)t->zero_region / 1048576.0);
    if (t->data_dir) rp_data_print(&t->data, t->data_dir, o);
    for (int pass = 0; pass < 2; pass++) {
        fprintf(o, pass ? "\nOurs by design:\n\n| method | difference | first call | calls | the first one | reason, document |\n|---|---|---|---|---|---|\n"
                        : "| method | stop or disagreement | first call | calls | the first one |\n|---|---|---|---|---|\n");
        for (unsigned i = 0; i < t->n; i++) {
            const RpRow *w = &t->rows[i];
            if (!w->expected != !pass) continue;
            fprintf(o, "| `%s` | ", w->method);
            rp_cell(o, w->cause, 400);
            fprintf(o, " | %llu | %llu | ", (unsigned long long)w->first, (unsigned long long)w->count);
            rp_cell(o, w->detail, 260);
            if (pass) { fprintf(o, " | "); rp_cell(o, w->expected, 400); }
            fprintf(o, " |\n");
        }
    }
}

/* The rows that are not ours by design, one per line: method, cause, first call, calls (tabs). */
RP_API void rp_table_tsv(const RpTable *t, FILE *o) {
    for (unsigned i = 0; i < t->n; i++)
        if (!t->rows[i].expected)
            fprintf(o, "%s\t%s\t%llu\t%llu\n", t->rows[i].method, t->rows[i].cause, (unsigned long long)t->rows[i].first, (unsigned long long)t->rows[i].count);
}

/* Holds the table against an earlier one written by rp_table_tsv.  Prints every row that is new,
 * affects more calls than before, or is gone; returns the number that are new or grew. */
RP_API int rp_table_against(const RpTable *t, FILE *baseline, FILE *o) {
    char *line = NULL;
    size_t cap = 0;
    int worse = 0;
    bool *seen = calloc(t->n + 1, 1);
    while (getline(&line, &cap, baseline) > 0) {
        char *m = strtok(line, "\t"), *c = strtok(NULL, "\t"), *f = strtok(NULL, "\t"), *n = strtok(NULL, "\t\n");
        if (!m || !c || !f || !n) continue;
        unsigned i = 0;
        while (i < t->n && (t->rows[i].expected || strcmp(t->rows[i].method, m) || strcmp(t->rows[i].cause, c))) i++;
        if (i == t->n) { fprintf(o, "GONE  %s: %s (was %s calls)\n", m, c, n); continue; }
        seen[i] = true;
        uint64_t was = strtoull(n, NULL, 10);
        if (t->rows[i].count > was) { worse++; fprintf(o, "GREW  %s: %s: %llu calls, was %llu\n", m, c, (unsigned long long)t->rows[i].count, (unsigned long long)was); }
        else if (t->rows[i].count < was) fprintf(o, "FEWER %s: %s: %llu calls, was %llu\n", m, c, (unsigned long long)t->rows[i].count, (unsigned long long)was);
    }
    for (unsigned i = 0; i < t->n; i++)
        if (!t->rows[i].expected && !seen[i]) { worse++; fprintf(o, "NEW   %s: %s: %llu calls, first at call %llu\n", t->rows[i].method, t->rows[i].cause, (unsigned long long)t->rows[i].count, (unsigned long long)t->rows[i].first); }
    free(seen);
    free(line);
    return worse;
}

RP_API void rp_table_free(RpTable *t) {
    for (unsigned i = 0; i < t->n; i++) { free(t->rows[i].method); free(t->rows[i].cause); free(t->rows[i].detail); }
    free(t->rows);
    memset(t, 0, sizeof *t);
}

#endif /* RP_REPLAY_H */
