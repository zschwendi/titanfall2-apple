/* Benchmark driver: calls every function described in the tables of bench/functions two ways and
 * compares them, then times them.  See bench/README.md.
 *
 *   native   the corpus source compiled for arm64 (a dylib per corpus file), called as C;
 *   lifted   the corpus source compiled to an x86-64 Windows DLL, lifted to C, compiled for
 *            arm64 with the really-linked macro set of runtime.h.  The DLL's image is mapped
 *            here with relocations applied; arguments go into a Ctx and onto a guest stack
 *            according to the Windows x64 convention.
 *
 * Both sides get the same argument values and the same buffers at the same addresses, one
 * after the other, so that pointers stored into buffers or returned compare equal.
 */
#include "runtime.h"
#include "bench.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sched.h>
#include <setjmp.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* ---- small utilities -------------------------------------------------------------------- */

static void die(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "bench: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(2);
}

typedef struct { uint64_t s; } Rng;
static uint64_t rng_next(Rng *r) {                      /* splitmix64 */
    uint64_t z = (r->s += UINT64_C(0x9E3779B97F4A7C15));
    z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31);
}
static uint64_t rng_below(Rng *r, uint64_t n) { return n ? rng_next(r) % n : 0; }
static uint64_t hash_str(const char *s) {
    uint64_t h = UINT64_C(0xcbf29ce484222325);
    for (; *s; s++) h = (h ^ (uint8_t)*s) * UINT64_C(0x100000001b3);
    return h;
}

static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }

/* ---- performance cores ------------------------------------------------------------------ */
/* macOS has no thread affinity on Apple Silicon.  The thread asks for the highest QoS class,
 * which the scheduler places on performance cores, and every timed run records the CPU
 * number before and after: a run that started or ended on an efficiency core is repeated.
 * CPU numbers below hw.perflevel1.logicalcpu are the efficiency cores on this machine
 * (checked by `driver --cores`, which times a fixed loop per CPU number). */
static int g_ecores;                                    /* CPU numbers [0, g_ecores) are efficiency cores */

static int cpu_now(void) {
    size_t n = 0;
    if (pthread_cpu_number_np(&n) != 0) return -1;
    return (int)n;
}
static int on_pcore(void) { int c = cpu_now(); return c < 0 || c >= g_ecores; }
static void perf_setup(void) {
    int v = 0;
    size_t len = sizeof v;
    if (sysctlbyname("hw.nperflevels", &v, &len, NULL, 0) == 0 && v >= 2) {
        len = sizeof v;
        if (sysctlbyname("hw.perflevel1.logicalcpu", &v, &len, NULL, 0) == 0) g_ecores = v;
    }
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
}

/* ---- x86 memory ordering ----------------------------------------------------------------- */
/* thread_set_x86_64_compat(1) gives the calling thread x86 (total store order) memory
 * ordering.  It needs an entitlement that only a provisioned app bundle carries
 * (experiments/tso/README.md, bench/tso.sh); anywhere else it returns an error, and the
 * benchmark stops rather than report timings for a mode that is not on.  The symbol is
 * looked up at run time so that the driver builds against any SDK. */
static void x86_ordering_on(void) {
    int (*set)(uint32_t) = (int (*)(uint32_t))dlsym(RTLD_DEFAULT, "thread_set_x86_64_compat");
    if (!set) die("--x86-ordering: this system has no thread_set_x86_64_compat");
    int kr = set(1);
    if (kr != 0) die("--x86-ordering: thread_set_x86_64_compat(1) returned %d (the process lacks the entitlement)", kr);
    printf("x86 memory ordering: on for this thread (thread_set_x86_64_compat(1) returned 0)\n");
}

/* ---- PE image --------------------------------------------------------------------------- */

typedef struct Module {
    const ModuleDesc *d;
    uint8_t *base;                  /* where the image is mapped */
    uint32_t size;
    uint64_t preferred;
    uint32_t exp_rva;               /* export directory */
    void *dylib;
    const LiftedEntry *table;
    size_t count;
    LiftedFn *direct;               /* rva -> function, for the direct-mapped dispatch variant */
} Module;

static Module g_mod[32];
static int g_nmod;

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static uint64_t rd64(const uint8_t *p) { uint64_t v; memcpy(&v, p, 8); return v; }

static void load_image(Module *m, int index) {
    const char *path = m->d->dll;
    int fd = open(path, O_RDONLY);
    if (fd < 0) die("cannot open %s (run: sh tests/corpus/build.sh)", path);
    struct stat st;
    fstat(fd, &st);
    size_t fsize = (size_t)st.st_size;
    uint8_t *file = malloc(fsize);
    if (read(fd, file, fsize) != (ssize_t)fsize) die("short read on %s", path);
    close(fd);
    if (fsize < 0x200 || rd16(file) != 0x5A4D) die("%s is not a PE file", path);
    uint32_t pe = rd32(file + 0x3c);
    if (pe + 0x108 > fsize || rd32(file + pe) != 0x4550) die("%s: bad PE header", path);
    uint16_t machine = rd16(file + pe + 4), nsec = rd16(file + pe + 6), optsz = rd16(file + pe + 20);
    const uint8_t *opt = file + pe + 24;
    if (machine != 0x8664 || rd16(opt) != 0x20B) die("%s is not an x86-64 PE32+ image", path);
    m->preferred = rd64(opt + 24);
    m->size = rd32(opt + 56);
    uint32_t hdr = rd32(opt + 60);
    uint32_t exp_rva = rd32(opt + 112), imp_rva = rd32(opt + 120), imp_size = rd32(opt + 124);
    uint32_t rel_rva = rd32(opt + 152), rel_size = rd32(opt + 156);
    if (imp_rva || imp_size) die("%s has imports; this loader has no import binding", path);
    /* A fixed, distinct address per module, far from anything macOS maps by itself, so that
     * the same build gives the same addresses on every run. */
    void *want = (void *)(uintptr_t)(UINT64_C(0x0000300000000000) + (uint64_t)index * UINT64_C(0x100000000));
    size_t map = (m->size + 0x3FFF) & ~(size_t)0x3FFF;
    uint8_t *base = mmap(want, map, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) die("mmap for %s failed", path);
    memcpy(base, file, hdr < fsize ? hdr : fsize);
    const uint8_t *sec = opt + optsz;
    for (int i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsize = rd32(sec + 8), va = rd32(sec + 12), rsize = rd32(sec + 16), rptr = rd32(sec + 20);
        uint32_t n = vsize && vsize < rsize ? vsize : rsize;
        if ((uint64_t)va + n > m->size || (uint64_t)rptr + n > fsize) die("%s: section %d out of range", path, i);
        memcpy(base + va, file + rptr, n);
    }
    /* base relocations */
    uint64_t delta = (uint64_t)(uintptr_t)base - m->preferred;
    uint32_t off = 0;
    while (rel_rva && off + 8 <= rel_size) {
        uint32_t page = rd32(base + rel_rva + off), block = rd32(base + rel_rva + off + 4);
        if (block < 8) break;
        for (uint32_t k = 8; k + 2 <= block; k += 2) {
            uint16_t e = rd16(base + rel_rva + off + k);
            unsigned type = e >> 12;
            if (type == 0) continue;
            if (type != 10) die("%s: relocation type %u is not handled", path, type);
            uint8_t *site = base + page + (e & 0xFFF);
            uint64_t v = rd64(site) + delta;
            memcpy(site, &v, 8);
        }
        off += block;
    }
    free(file);
    m->base = base;
    m->exp_rva = exp_rva;
}

/* RVA of an exported name, or 0. */
static uint32_t export_rva(const Module *m, const char *name) {
    if (!m->exp_rva) return 0;
    const uint8_t *e = m->base + m->exp_rva;
    uint32_t nnames = rd32(e + 24), funcs = rd32(e + 28), names = rd32(e + 32), ords = rd32(e + 36);
    for (uint32_t i = 0; i < nnames; i++) {
        const char *n = (const char *)m->base + rd32(m->base + names + 4 * i);
        if (strcmp(n, name) == 0) return rd32(m->base + funcs + 4 * rd16(m->base + ords + 2 * i));
    }
    return 0;
}

static LiftedFn table_lookup(const Module *m, uint32_t rva) {
    size_t lo = 0, hi = m->count;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (m->table[mid].rva < rva) lo = mid + 1; else hi = mid;
    }
    return lo < m->count && m->table[lo].rva == rva ? m->table[lo].fn : NULL;
}

static int is_skipped(const Module *m, uint32_t rva) {
    for (uint32_t i = 0; i < m->d->nskipped; i++)
        if (m->d->skipped[i] == rva) return 1;
    return 0;
}

static Module *module_of(uint64_t addr) {
    for (int i = 0; i < g_nmod; i++)
        if (addr - (uint64_t)(uintptr_t)g_mod[i].base < g_mod[i].size) return &g_mod[i];
    return NULL;
}

static void modules_setup(void) {
    g_nmod = bench_nmodules;
    if (g_nmod > 32) die("too many modules");
    for (int i = 0; i < g_nmod; i++) {
        Module *m = &g_mod[i];
        m->d = &bench_modules[i];
        load_image(m, i);
        *m->d->g_base = (uint64_t)(uintptr_t)m->base;
        m->table = (const LiftedEntry *)m->d->table;
        m->count = *m->d->table_count;
        m->direct = calloc(m->size, sizeof(LiftedFn));
        for (size_t k = 0; k < m->count; k++) m->direct[m->table[k].rva] = m->table[k].fn;
        m->dylib = dlopen(m->d->dylib, RTLD_NOW | RTLD_LOCAL);
        if (!m->dylib) die("dlopen %s: %s", m->d->dylib, dlerror());
    }
}

/* ---- what the lifted code links against (runtime.h, "really linked") --------------------- */

uint8_t tf_straddle_lock;

enum { ST_OK, ST_TRAP, ST_UNLIFTED, ST_MISS, ST_FAULT, ST_CPU, ST_HANG, ST_RET };
static sigjmp_buf g_jb;
static volatile int g_jb_armed;
static volatile int g_in_lifted;            /* 1 while lifted code runs, 0 while native code runs */
static int g_stop_kind;
static uint64_t g_stop_addr;

#define CB0_GUEST UINT64_C(0x00007ff0cb000010)          /* guest address of bench_cb0: in no module */

int64_t bench_cb0(int32_t v, void *user) {
    uint64_t u = (uint64_t)(uintptr_t)user;
    return (int64_t)v * 3 + (int64_t)(u ^ (u >> 7)) - 11;
}

static _Noreturn void stop_run(int kind, uint64_t addr) {
    g_stop_kind = kind;
    g_stop_addr = addr;
    if (!g_jb_armed) {
        fprintf(stderr, "bench: lifted code stopped outside a guarded call (kind %d, address 0x%llx)\n",
                kind, (unsigned long long)addr);
        exit(2);
    }
    siglongjmp(g_jb, 1);
}

void rt_cpuid(Ctx *ctx) { (void)ctx; stop_run(ST_CPU, 0); }
void rt_rdtsc(Ctx *ctx) { (void)ctx; stop_run(ST_CPU, 1); }

_Noreturn void rt_trap(Ctx *ctx, uint64_t guest_address) {
    (void)ctx;
    stop_run(ST_TRAP, guest_address);
}
/* TRAP without -DTF_EXACT_TRAPS (harness/runtime.h) */
_Noreturn void rt_trap_lazy(uint64_t guest_address) {
    stop_run(ST_TRAP, guest_address);
}

/* The stub of a function the lifter skipped: its row of the table (a call by address) and
 * what a direct call names. */
_Noreturn void rt_trap_skipped(Ctx *ctx, uint64_t guest_address, const char *why, uint64_t why_address) {
    (void)ctx; (void)why; (void)why_address;
    stop_run(ST_UNLIFTED, guest_address);
}

/* An instruction the lifter does not translate, inside a lifted function (harness/runtime.h,
 * UNTRANSLATED): the case needs code that is not lifted, as a call to a skipped function does. */
_Noreturn void rt_trap_untranslated(Ctx *ctx, uint64_t guest_address, const char *what, const char *needs) {
    (void)ctx; (void)what; (void)needs;
    stop_run(ST_UNLIFTED, guest_address);
}

/* harness/runtime.h, RT_CHECK_RSP: a call came back with rsp or its return address changed. */
_Noreturn void rt_rsp_mismatch(Ctx *ctx, uint64_t return_address, uint64_t expected_rsp) {
    (void)ctx; (void)expected_rsp;
    stop_run(ST_RET, return_address);
}

static Module *g_cur;                                   /* module of the function under test */

/* An "import": the callee is host code.  Arguments from rcx and rdx, result in rax, and the
 * return address the guest pushed is popped. */
static void cb0_stub(Ctx *ctx) {
    ctx->r[0] = (uint64_t)bench_cb0((int32_t)ctx->r[1], (void *)(uintptr_t)ctx->r[2]);
    ctx->r[4] += 8;
}

/* harness/runtime.h: the host function for a guest address.  A miss stops the run here. */
LiftedFn rt_dispatch_fn(Ctx *ctx, uint64_t guest_target) {
    (void)ctx;
    Module *m = g_cur;
    uint64_t rva = guest_target - (uint64_t)(uintptr_t)m->base;
    if (rva >= m->size) {
        if (guest_target == CB0_GUEST) return cb0_stub;
        m = module_of(guest_target);
        if (!m) stop_run(ST_MISS, guest_target);
        rva = guest_target - (uint64_t)(uintptr_t)m->base;
    }
#ifdef BENCH_DISPATCH_DIRECT
    LiftedFn fn = m->direct[rva];
#else
    LiftedFn fn = table_lookup(m, (uint32_t)rva);
#endif
    if (!fn) stop_run(ST_MISS, guest_target);       /* a skipped function has a row: its stub, rt_trap_skipped */
    return fn;
}

static void on_fault(int sig, siginfo_t *si, void *uc) {
    (void)uc;
    if (g_jb_armed) {
        g_stop_kind = ST_FAULT;
        g_stop_addr = (uint64_t)(uintptr_t)si->si_addr;
        siglongjmp(g_jb, 2 + (sig == SIGBUS));
    }
    static const char msg[] = "bench: fatal signal outside a guarded call\n";
    write(2, msg, sizeof msg - 1);
    _exit(3);
}

/* Watchdog for the compared calls: a wrong translation can loop for ever.  A timer on the
 * process's own CPU time ticks every quarter second; a guarded call that is still the same
 * call two ticks later is abandoned. */
static volatile uint64_t g_serial, g_wd_seen;
static volatile int g_wd_strikes;

static void on_tick(int sig) {
    (void)sig;
    if (!g_jb_armed || g_serial != g_wd_seen) { g_wd_seen = g_serial; g_wd_strikes = 0; return; }
    if (++g_wd_strikes < 2) return;
    g_wd_strikes = 0;
    g_stop_kind = ST_HANG;
    g_stop_addr = 0;
    siglongjmp(g_jb, 4);
}

static void signals_setup(void) {
    struct sigaction sa;
    /* A wrong translation can recurse until the host stack is gone; the handler then needs
     * a stack of its own. */
    stack_t alt;
    alt.ss_size = 1 << 16;
    alt.ss_sp = malloc(alt.ss_size);
    alt.ss_flags = 0;
    sigaltstack(&alt, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_NODEFER | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGTRAP, &sa, NULL);
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_tick;
    sa.sa_flags = SA_NODEFER;
    sigaction(SIGVTALRM, &sa, NULL);
}

static void watchdog(int on) {
    struct itimerval it;
    memset(&it, 0, sizeof it);
    if (on) it.it_interval.tv_usec = it.it_value.tv_usec = 250000;
    setitimer(ITIMER_VIRTUAL, &it, NULL);
}

/* ---- inputs ----------------------------------------------------------------------------- */

#define GUARD 32

typedef struct Layout {
    uint32_t off[BENCH_MAXARGS], size[BENCH_MAXARGS];
    uint32_t total;
} Layout;

static uint32_t max_off(const ArgDesc *a) {
    uint32_t m = 0;
    for (uint32_t i = 0; i < a->noffs; i++) if (a->offs[i] > m) m = a->offs[i];
    return m;
}

static void layout_of(const FuncDesc *f, Layout *l) {
    uint32_t pos = 64;
    memset(l, 0, sizeof *l);
    for (int i = 0; i < f->nargs; i++) {
        const ArgDesc *a = &f->args[i];
        uint32_t size = 0;
        if (a->kind == A_BUF && a->alias < 0) size = a->size + max_off(a);
        else if (a->kind == A_STRUCT || a->kind == A_U128) size = a->size;
        if (!size) continue;
        l->off[i] = pos;
        l->size[i] = size;
        pos += (size + GUARD + 63) & ~63u;
    }
    l->total = pos;
}

static const uint64_t kEdges[] = {
    0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000,
    0x7fffffff, 0x80000000u, 0xffffffffu, UINT64_C(0x100000000), UINT64_C(0x7fffffffffffffff),
    UINT64_C(0x8000000000000000), UINT64_C(0xffffffffffffffff), UINT64_C(0xfffffffffffffffe),
    UINT64_C(0xffffffff80000000), UINT64_C(0xffffffffffffff80), UINT64_C(0xffffffffffff8000),
};

static uint64_t mixed_value(Rng *r) {
    switch (rng_below(r, 8)) {
    case 0: case 1: case 2: return rng_next(r);
    case 3: return rng_below(r, 17);
    case 4: return (uint64_t)0 - (1 + rng_below(r, 16));
    case 5: return kEdges[rng_below(r, sizeof kEdges / sizeof kEdges[0])];
    case 6: { unsigned bits = 1 + (unsigned)rng_below(r, 64); return rng_next(r) >> (64 - bits); }
    default: return (UINT64_C(1) << rng_below(r, 64)) + rng_below(r, 3) - 1;
    }
}

static uint64_t reduce(const ArgDesc *a, uint64_t v) {      /* to the argument's type, extended to 64 bits */
    if (a->bits == 64) return v;
    uint64_t mask = (UINT64_C(1) << a->bits) - 1;
    v &= mask;
    if (a->is_signed && (v >> (a->bits - 1))) v |= ~mask;
    return v;
}

static uint64_t int_value(const ArgDesc *a, Rng *r) {
    uint64_t v;
    switch (a->dist) {
    case D_LT: v = rng_below(r, a->p0); break;
    case D_MASK: v = mixed_value(r) & a->p0; break;
    case D_EQ: v = a->p0; break;
    case D_PICK: v = rng_below(r, 4) ? a->picks[rng_below(r, a->npicks)] : mixed_value(r); break;
    default: v = mixed_value(r); break;
    }
    return reduce(a, v);
}

static void fill_buffer(const ArgDesc *a, uint8_t *p, uint32_t n, Rng *r) {
    switch (a->fill) {
    case F_ZERO:
        memset(p, 0, n);
        break;
    case F_SMALL:
        for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)rng_below(r, 4);
        break;
    case F_SMALL32: case F_SMALL64: {
        uint32_t w = a->fill == F_SMALL32 ? 4 : 8;
        memset(p, 0, n);
        for (uint32_t i = 0; i < n; i += w) p[i] = (uint8_t)rng_below(r, 4);
        break;
    }
    case F_STR: {
        for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(1 + rng_below(r, 255));
        if (rng_below(r, 4)) for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)(' ' + rng_below(r, 95));
        if (n) { p[rng_below(r, n)] = 0; p[n - 1] = 0; }
        if (n && rng_below(r, 8) == 0) p[0] = 0;
        break;
    }
    case F_HEX: {
        static const char hex[] = "0123456789abcdefABCDEF";
        static const char other[] = "gG xz@`/:\0\n";
        for (uint32_t i = 0; i < n; i++)
            p[i] = rng_below(r, 12) ? (uint8_t)hex[rng_below(r, 22)] : (uint8_t)other[rng_below(r, 11)];
        break;
    }
    case F_I32SORTED: {
        int32_t v = (int32_t)rng_below(r, 17) - 8;
        for (uint32_t i = 0; i + 4 <= n; i += 4) { memcpy(p + i, &v, 4); v += (int32_t)rng_below(r, 16); }
        break;
    }
    case F_LIST16: {
        /* Nodes of 16 bytes: { next (8), val (4), pad (4) }.  Node 0 is the head; a chain of
         * random length through the other nodes in random order, ending in NULL or, one case
         * in eight, looping back to the head. */
        uint32_t nodes = n / 16, order[64];
        if (nodes > 64) nodes = 64;
        for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)rng_next(r);
        for (uint32_t i = 0; i < nodes; i++) order[i] = i;
        for (uint32_t i = nodes; i > 2; i--) {
            uint32_t j = 1 + (uint32_t)rng_below(r, i - 1), t = order[i - 1];
            order[i - 1] = order[j]; order[j] = t;
        }
        uint32_t len = nodes ? 1 + (uint32_t)rng_below(r, nodes) : 0;
        for (uint32_t i = 0; i < len; i++) {
            uint64_t next = i + 1 < len ? (uint64_t)(uintptr_t)(p + 16 * order[i + 1])
                          : (rng_below(r, 8) == 0 ? (uint64_t)(uintptr_t)p : 0);
            memcpy(p + 16 * order[i], &next, 8);
        }
        break;
    }
    default:
        switch (rng_below(r, 5)) {
        case 0: for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)rng_below(r, 4); break;
        case 1: for (uint32_t i = 0; i < n; i++) p[i] = rng_below(r, 2) ? 0xFF : 0x00; break;
        default: for (uint32_t i = 0; i < n; i++) p[i] = (uint8_t)rng_next(r); break;
        }
        break;
    }
}

/* Fills c->chunk and c->v from the generator.  c->chunk and c->pristine must hold l->total bytes. */
static void gen_case(const FuncDesc *f, const Layout *l, Case *c, Rng *r) {
    memset(c->chunk, 0xA5, l->total);
    c->chunk_size = l->total;
    c->garbage = (uint32_t)rng_next(r);
    for (int i = 0; i < f->nargs; i++) {
        const ArgDesc *a = &f->args[i];
        c->buf[i].off = l->off[i];
        c->buf[i].size = l->size[i];
        c->buf[i].nocmp = a->nocmp;
        switch (a->kind) {
        case A_INT:
            c->v[i] = int_value(a, r);
            /* One time in six an unconstrained integer repeats an earlier integer argument,
             * or lands next to it: equal and adjacent operands are where comparisons and
             * carries change their answer, and independent random values almost never meet. */
            if (a->dist == D_MIXED && rng_below(r, 6) == 0) {
                int prev[BENCH_MAXARGS], np = 0;
                for (int j = 0; j < i; j++) if (f->args[j].kind == A_INT) prev[np++] = j;
                if (np) c->v[i] = reduce(a, c->v[prev[rng_below(r, (uint64_t)np)]] + rng_below(r, 3) - 1);
            }
            break;
        case A_CB:
            c->v[i] = (uint64_t)(uintptr_t)&bench_cb0;
            break;
        case A_STRUCT: case A_U128: {
            uint8_t *p = c->chunk + l->off[i];
            for (uint32_t k = 0; k < a->size; k++) p[k] = (uint8_t)rng_next(r);
            switch (rng_below(r, 8)) {                  /* low-entropy, all zero, all ones */
            case 0: case 1: for (uint32_t k = 0; k < a->size; k++) p[k] = (uint8_t)rng_below(r, 3); break;
            case 2: memset(p, 0, a->size); break;
            case 3: memset(p, 0xFF, a->size); break;
            case 4: memset(p, 0xFF, a->size); p[rng_below(r, a->size)] = (uint8_t)rng_next(r); break;
            default: break;
            }
            c->v[i] = (uint64_t)(uintptr_t)p;
            break;
        }
        case A_BUF:
            if (a->alias >= 0) break;                   /* second pass */
            {
                uint8_t *p = c->chunk + l->off[i];
                if (a->copyof >= 0) {
                    const ArgDesc *s = &f->args[a->copyof];
                    uint32_t n = s->size < a->size ? s->size : a->size;
                    fill_buffer(a, p, l->size[i], r);
                    memcpy(p, c->chunk + l->off[a->copyof], n);
                    if (a->mut && n && rng_below(r, 4)) p[rng_below(r, n)] ^= (uint8_t)(1 + rng_below(r, 255));
                } else {
                    fill_buffer(a, p, l->size[i], r);
                }
                uint32_t o = a->noffs ? a->offs[rng_below(r, a->noffs)] : 0;
                c->v[i] = (uint64_t)(uintptr_t)(p + o);
            }
            break;
        }
    }
    for (int i = 0; i < f->nargs; i++) {
        const ArgDesc *a = &f->args[i];
        if (a->kind == A_BUF && a->alias >= 0)
            c->v[i] = (uint64_t)(uintptr_t)(c->chunk + l->off[a->alias] + a->alias_off);
    }
    memcpy(c->pristine, c->chunk, l->total);
}

/* ---- the lifted call ---------------------------------------------------------------------- */

typedef struct LCall {
    uint64_t reg[4];                /* rcx rdx r8 r9 */
    uint64_t stk[BENCH_MAXARGS];    /* fifth argument onwards */
    int nstk;
    uint8_t byref[BENCH_MAXARGS][32] __attribute__((aligned(16)));   /* copies of by-reference arguments */
    uint8_t ret[64] __attribute__((aligned(16)));                     /* struct result through the hidden pointer */
} LCall;

static int struct_in_register(uint32_t size) { return size == 1 || size == 2 || size == 4 || size == 8; }
static int ret_hidden(const FuncDesc *f) {
    return f->ret_kind == R_STRUCT && !struct_in_register(f->ret_size);
}

/* Windows x64: every argument takes one 8-byte slot; the first four slots are rcx, rdx, r8,
 * r9, the rest are on the stack above the 32 bytes of home space.  A struct of 1, 2, 4 or 8
 * bytes travels in the slot; any other struct, and a 128-bit integer, is copied by the caller
 * and the slot holds the address of the copy (16-byte aligned: clang loads a 128-bit
 * argument with movdqa).  A struct result that does not fit the same rule is written through
 * a pointer the caller passes as a hidden first argument; a 128-bit integer result comes
 * back in xmm0 (what clang does for this target; see ab_u128_add in the corpus).  The bits
 * of a slot above the argument's width are undefined; half the cases fill them with junk
 * above bit 31 (below that, the value is extended to 32 bits the way clang's own callers do
 * and clang's callees are allowed to expect). */
static void marshal(const FuncDesc *f, const Case *c, LCall *lc) {
    uint64_t slot[BENCH_MAXARGS + 1];
    int n = 0;
    Rng g = { c->garbage };
    int junk = c->garbage & 1;
    if (ret_hidden(f)) slot[n++] = (uint64_t)(uintptr_t)lc->ret;
    for (int i = 0; i < f->nargs; i++) {
        const ArgDesc *a = &f->args[i];
        uint64_t v = c->v[i];
        switch (a->kind) {
        case A_INT:
            if (a->bits <= 32) {
                v = (uint32_t)v;
                if (junk) v |= rng_next(&g) << 32;
            }
            break;
        case A_CB:
            v = CB0_GUEST;
            break;
        case A_STRUCT: case A_U128:
            if (a->kind == A_STRUCT && struct_in_register(a->size)) {
                uint64_t w = junk ? rng_next(&g) : 0;
                memcpy(&w, (const void *)(uintptr_t)c->v[i], a->size);
                v = w;
            } else {
                memcpy(lc->byref[i], (const void *)(uintptr_t)c->v[i], a->size);
                v = (uint64_t)(uintptr_t)lc->byref[i];
            }
            break;
        default:
            break;
        }
        slot[n++] = v;
    }
    for (int i = 0; i < 4; i++) lc->reg[i] = i < n ? slot[i] : (junk ? rng_next(&g) : 0);
    lc->nstk = n > 4 ? n - 4 : 0;
    for (int i = 4; i < n; i++) lc->stk[i - 4] = slot[i];
}

static uint8_t *g_stack;                                /* guest stack, 1 MiB */
#define STACK_SIZE (1u << 20)
#define SENTINEL UINT64_C(0x0000240000000000)           /* return address of the outermost call */
static uint64_t g_sp;                                   /* rsp at entry: 8 below a 16-byte boundary */

static const int kNonvolatile[] = { 3, 5, 6, 7, 12, 13, 14, 15 };

/* The guest register file.  It lives at a chosen offset inside its own mapping rather than
 * wherever the linker would put a static, because where it sits relative to the guest stack
 * changes the timings (see --ctx-offset and the findings, "What is uncertain"). */
static Ctx *g_ctx;

/* Runs fn once.  Returns ST_OK or why it stopped; *abi collects violations of the calling
 * convention seen after a normal return (bit 0: rsp is not entry + 8, bit 1: a nonvolatile
 * general register changed, bit 2: xmm6-15 changed, bit 3: the direction flag is set). */
static int run_lifted(LiftedFn fn, const LCall *lc, uint64_t seed, uint64_t *rax, unsigned *abi) {
    Ctx *ctx = g_ctx;
    Ctx before;
    Rng g = { seed };
    memset(ctx, 0, sizeof *ctx);
    for (int i = 0; i < 16; i++) ctx->r[i] = rng_next(&g);
    for (int i = 0; i < 16; i++) { ctx->xmm[i].q[0] = rng_next(&g); ctx->xmm[i].q[1] = rng_next(&g); }
    ctx->mxcsr = 0x1F80;
    {   /* the status flags are undefined at entry: random, so that reading a stale one shows */
        uint64_t fl = rng_next(&g);
        ctx->cf = fl & 1; ctx->pf = (fl >> 1) & 1; ctx->af = (fl >> 2) & 1;
        ctx->zf = (fl >> 3) & 1; ctx->sf = (fl >> 4) & 1; ctx->of = (fl >> 5) & 1;
    }
    ctx->r[1] = lc->reg[0]; ctx->r[2] = lc->reg[1]; ctx->r[8] = lc->reg[2]; ctx->r[9] = lc->reg[3];
    ctx->r[4] = g_sp;
    ST64(g_sp, SENTINEL);
    for (int i = 0; i < 4; i++) ST64(g_sp + 8 + 8 * i, rng_next(&g));       /* home space: junk */
    for (int i = 0; i < lc->nstk; i++) ST64(g_sp + 0x28 + 8 * i, lc->stk[i]);
    before = *ctx;
    *abi = 0;
    g_serial++;
    g_jb_armed = 1;
    g_in_lifted = 1;
    int st = ST_OK;
    if (sigsetjmp(g_jb, 1) == 0) fn(ctx);
    else st = g_stop_kind;
    g_in_lifted = 0;
    g_jb_armed = 0;
    *rax = ctx->r[0];
    if (st == ST_OK) {
        if (ctx->r[4] != g_sp + 8) *abi |= 1;
        for (unsigned i = 0; i < sizeof kNonvolatile / sizeof kNonvolatile[0]; i++)
            if (ctx->r[kNonvolatile[i]] != before.r[kNonvolatile[i]]) *abi |= 2;
        if (memcmp(&ctx->xmm[6], &before.xmm[6], 10 * sizeof(Xmm)) != 0) *abi |= 4;
        if (ctx->df) *abi |= 8;
    }
    return st;
}

/* Guards the native call the same way, so that a wrong description shows as a report line
 * rather than a dead process. */
static int run_native(const FuncDesc *f, void *fn, const Case *c, uint64_t *res, void *ret) {
    int st = ST_OK;
    g_serial++;
    g_jb_armed = 1;
    if (sigsetjmp(g_jb, 1) == 0) *res = f->native_call(fn, c->v, ret);
    else st = g_stop_kind;
    g_jb_armed = 0;
    return st;
}

/* ---- comparison --------------------------------------------------------------------------- */

typedef struct Known { char file[32], name[64], cls[32]; int seen; struct Known *next; } Known;
static Known *g_known;

static void load_known(const char *path) {
    FILE *fp = fopen(path, "r");
    char line[512];
    if (!fp) die("cannot open %s", path);
    while (fgets(line, sizeof line, fp)) {
        Known *k = calloc(1, sizeof *k);
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        if (sscanf(line, "%31s %63s %31s", k->file, k->name, k->cls) == 3) { k->next = g_known; g_known = k; }
        else free(k);
    }
    fclose(fp);
}
static Known *known_for(const FuncDesc *f) {
    for (Known *k = g_known; k; k = k->next)
        if (!strcmp(k->file, f->file) && !strcmp(k->name, f->name)) return k;
    return NULL;
}

enum { FS_PASS, FS_MISMATCH, FS_NOT_LIFTED, FS_NEEDS_UNLIFTED, FS_NO_NATIVE, FS_NO_EXPORT };
static const char *const kStatus[] = { "pass", "MISMATCH", "not-lifted", "needs-unlifted", "no-native", "no-export" };

#define MAX_ROUNDS 256

typedef struct Result {
    const FuncDesc *f;
    Module *m;
    void *native;
    LiftedFn lifted;
    uint32_t rva;
    int status;
    long cases, compared, mismatches, unlifted;
    uint64_t unlifted_at;
    char detail[3][600];
    int ndetail;
    Known *known;
    /* timing, ns per call */
    int timed;
    double nat[MAX_ROUNDS], lif[MAX_ROUNDS], base[MAX_ROUNDS];
    uint8_t nat_p[MAX_ROUNDS], lif_p[MAX_ROUNDS];   /* the round ran on a performance core */
    int rounds, confirmed;
    char cores;                     /* 'P': both sides timed on performance cores, 'E': both on efficiency cores, 'M': mixed */
    double nat_min, nat_med, lif_min, lif_med, base_min;
} Result;

static uint64_t ret_mask(const FuncDesc *f) {
    return f->ret_bits >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << f->ret_bits) - 1;
}

static void describe_args(const FuncDesc *f, const Case *c, char *out, size_t n) {
    size_t pos = 0;
    out[0] = 0;
    for (int i = 0; i < f->nargs && pos + 40 < n; i++) {
        const ArgDesc *a = &f->args[i];
        if (a->kind == A_INT) pos += (size_t)snprintf(out + pos, n - pos, "%s0x%llx", i ? ", " : "", (unsigned long long)c->v[i]);
        else if (a->kind == A_BUF) pos += (size_t)snprintf(out + pos, n - pos, "%sbuf%d", i ? ", " : "", i);
        else if (a->kind == A_CB) pos += (size_t)snprintf(out + pos, n - pos, "%scb0", i ? ", " : "");
        else {
            pos += (size_t)snprintf(out + pos, n - pos, "%s{", i ? ", " : "");
            for (uint32_t k = 0; k < a->size && pos + 8 < n; k++)
                pos += (size_t)snprintf(out + pos, n - pos, "%02x", c->pristine[c->buf[i].off + k]);
            pos += (size_t)snprintf(out + pos, n - pos, "}");
        }
    }
}

static void hex_bytes(const uint8_t *p, uint32_t n, char *out, size_t cap) {
    size_t pos = 0;
    out[0] = 0;
    for (uint32_t i = 0; i < n && pos + 3 < cap; i++) pos += (size_t)snprintf(out + pos, cap - pos, "%02x", p[i]);
}

static long g_show;                                     /* --show N: print the first N cases of every function */

static void check_function(Result *res, long ncases, uint64_t seed) {
    const FuncDesc *f = res->f;
    Layout l;
    layout_of(f, &l);
    Case c;
    memset(&c, 0, sizeof c);
    c.chunk = malloc(l.total);
    c.pristine = malloc(l.total);
    uint8_t *after_native = malloc(l.total);
    LCall *lc = aligned_alloc(16, sizeof *lc);
    uint8_t nret[64];
    Rng r = { seed ^ hash_str(f->file) ^ (hash_str(f->name) << 1) };
    g_cur = res->m;
    for (long k = 0; k < ncases; k++) {
        uint64_t nres = 0, rax = 0;
        unsigned abi = 0;
        char why[400];
        why[0] = 0;
        gen_case(f, &l, &c, &r);
        memset(nret, 0, sizeof nret);
        memset(lc->ret, 0, sizeof lc->ret);
        int nst = run_native(f, res->native, &c, &nres, nret);
        if (nst != ST_OK) {
            snprintf(why, sizeof why, "the NATIVE call %s: the description or the corpus function is wrong",
                     nst == ST_HANG ? "did not return" : "faulted");
        }
        memcpy(after_native, c.chunk, l.total);
        memcpy(c.chunk, c.pristine, l.total);
        marshal(f, &c, lc);
        int st = run_lifted(res->lifted, lc, rng_next(&r), &rax, &abi);
        res->cases++;
        if (k < g_show) {
            char args[300];
            describe_args(f, &c, args, sizeof args);
            printf("  %s/%s case %ld (%s): native 0x%llx, lifted rax 0x%llx%s\n", f->file, f->name, k, args,
                   (unsigned long long)nres, (unsigned long long)rax, st == ST_OK ? "" : " (lifted call did not return normally)");
            fflush(stdout);
        }
        if (st == ST_UNLIFTED && nst == ST_OK) {
            res->unlifted++;
            res->unlifted_at = g_stop_addr - (uint64_t)(uintptr_t)module_of(g_stop_addr)->base;
            continue;
        }
        res->compared++;
        if (nst != ST_OK) {
            /* reported below */
        } else if (st == ST_TRAP) {
            Module *m = module_of(g_stop_addr);
            snprintf(why, sizeof why, "lifted code trapped at %s+0x%llx", m ? m->d->file : "?",
                     (unsigned long long)(m ? g_stop_addr - (uint64_t)(uintptr_t)m->base : g_stop_addr));
        } else if (st == ST_MISS) {
            snprintf(why, sizeof why, "lifted code called guest address 0x%llx, which is no lifted function",
                     (unsigned long long)g_stop_addr);
        } else if (st == ST_FAULT) {
            snprintf(why, sizeof why, "lifted code faulted on host address 0x%llx", (unsigned long long)g_stop_addr);
        } else if (st == ST_CPU) {
            snprintf(why, sizeof why, "lifted code executed cpuid or rdtsc");
        } else if (st == ST_RET) {
            Module *m = module_of(g_stop_addr);
            snprintf(why, sizeof why, "a call in lifted code came back with rsp or its return address changed "
                     "(call returning to %s+0x%llx)", m ? m->d->file : "?",
                     (unsigned long long)(m ? g_stop_addr - (uint64_t)(uintptr_t)m->base : g_stop_addr));
        } else if (st == ST_HANG) {
            snprintf(why, sizeof why, "lifted code did not return within half a second of CPU time (no further cases run)");
        } else {
            size_t pos = 0;
            if (f->ret_kind == R_INT && ((nres ^ rax) & ret_mask(f)))
                pos += (size_t)snprintf(why + pos, sizeof why - pos, "result native 0x%llx lifted 0x%llx; ",
                                        (unsigned long long)(nres & ret_mask(f)), (unsigned long long)(rax & ret_mask(f)));
            if (f->ret_kind == R_STRUCT || f->ret_kind == R_U128) {
                uint8_t lret[64];
                memset(lret, 0, sizeof lret);
                if (f->ret_kind == R_U128) {
                    memcpy(lret, &g_ctx->xmm[0], 16);
                } else if (ret_hidden(f)) {
                    memcpy(lret, lc->ret, f->ret_size);
                    if (rax != (uint64_t)(uintptr_t)lc->ret)
                        pos += (size_t)snprintf(why + pos, sizeof why - pos, "rax does not hold the hidden result pointer; ");
                } else {
                    memcpy(lret, &rax, f->ret_size);
                }
                if (memcmp(lret, nret, f->ret_size) != 0) {
                    char a[130], b[130];
                    hex_bytes(nret, f->ret_size, a, sizeof a);
                    hex_bytes(lret, f->ret_size, b, sizeof b);
                    pos += (size_t)snprintf(why + pos, sizeof why - pos, "struct result native %s lifted %s; ", a, b);
                }
            }
            /* every byte of the chunk outside the buffers marked nocmp: buffers and guards */
            for (int i = 0; i < f->nargs; i++)
                if (c.buf[i].size && c.buf[i].nocmp) {
                    memcpy(after_native + c.buf[i].off, c.chunk + c.buf[i].off, c.buf[i].size);
                }
            if (memcmp(after_native, c.chunk, l.total) != 0 && pos + 100 < sizeof why) {
                uint32_t at = 0;
                while (after_native[at] == c.chunk[at]) at++;
                int which = -1;
                for (int i = 0; i < f->nargs; i++)
                    if (c.buf[i].size && at >= c.buf[i].off && at < c.buf[i].off + c.buf[i].size) which = i;
                if (which >= 0)
                    pos += (size_t)snprintf(why + pos, sizeof why - pos,
                                            "buffer of argument %d differs at byte %u: native 0x%02x lifted 0x%02x (was 0x%02x); ",
                                            which, at - c.buf[which].off, after_native[at], c.chunk[at], c.pristine[at]);
                else
                    pos += (size_t)snprintf(why + pos, sizeof why - pos,
                                            "bytes outside every buffer differ at chunk offset %u: native 0x%02x lifted 0x%02x; ",
                                            at, after_native[at], c.chunk[at]);
            }
            if (abi && pos + 120 < sizeof why)
                pos += (size_t)snprintf(why + pos, sizeof why - pos, "lifted code broke the calling convention:%s%s%s%s; ",
                                        abi & 1 ? " rsp is not entry+8" : "", abi & 2 ? " a nonvolatile register changed" : "",
                                        abi & 4 ? " xmm6-15 changed" : "", abi & 8 ? " DF left set" : "");
        }
        if (why[0]) {
            res->mismatches++;
            if (res->ndetail < 3) {
                char args[300];
                describe_args(f, &c, args, sizeof args);
                snprintf(res->detail[res->ndetail++], sizeof res->detail[0], "case %ld (%s): %s", k, args, why);
            }
            if (st == ST_HANG || nst == ST_HANG) break;
        }
    }
    if (res->mismatches) res->status = FS_MISMATCH;
    else if (res->compared == 0) res->status = FS_NEEDS_UNLIFTED;
    else res->status = FS_PASS;
    free(c.chunk); free(c.pristine); free(after_native); free(lc);
}

/* The comparison of one function runs in a child process, so that a wrong translation that
 * scribbles over memory it does not own takes down the child and not the run: the function
 * is reported as a mismatch and the next one starts from a clean copy of the parent.  (It
 * also means every function sees the modules' globals as they were at load.) */
static void check_isolated(Result *res, long ncases, uint64_t seed) {
    int fd[2];
    if (pipe(fd) != 0) die("pipe failed");
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) die("fork failed");
    if (pid == 0) {
        close(fd[0]);
        alarm(300);                                     /* whatever the watchdog does not catch */
        watchdog(1);
        check_function(res, ncases, seed);
        watchdog(0);
        const char *p = (const char *)res;
        size_t left = sizeof *res;
        while (left) {
            ssize_t n = write(fd[1], p, left);
            if (n <= 0) _exit(1);
            p += n; left -= (size_t)n;
        }
        _exit(0);
    }
    close(fd[1]);
    Result *back = malloc(sizeof *back);
    char *p = (char *)back;
    size_t got = 0;
    for (;;) {
        ssize_t n = read(fd[0], p + got, sizeof *back - got);
        if (n <= 0) break;
        got += (size_t)n;
    }
    close(fd[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    if (got == sizeof *back && WIFEXITED(st) && WEXITSTATUS(st) == 0) {
        res->status = back->status;
        res->cases = back->cases; res->compared = back->compared; res->mismatches = back->mismatches;
        res->unlifted = back->unlifted; res->unlifted_at = back->unlifted_at;
        res->ndetail = back->ndetail;
        memcpy(res->detail, back->detail, sizeof res->detail);
    } else {
        res->status = FS_MISMATCH;
        res->cases = res->compared = res->mismatches = 1;
        res->ndetail = 1;
        if (WIFSIGNALED(st))
            snprintf(res->detail[0], sizeof res->detail[0],
                     "the comparison process was killed by signal %d outside a guarded call: one of the two sides damaged memory it does not own",
                     WTERMSIG(st));
        else
            snprintf(res->detail[0], sizeof res->detail[0], "the comparison process ended with status %d before reporting",
                     WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    }
    free(back);
}

/* ---- timing ------------------------------------------------------------------------------- */

/* rounds: the least number of rounds per function; more are run, up to max_rounds, until the
 * best time of each side has been reproduced (see confirmed()). */
typedef struct Timing { int rounds; double target_ns; int ncases; int max_rounds; } Timing;

__attribute__((noinline))
static uint64_t lifted_loop(LiftedFn fn, Ctx *ctx, const LCall *lc, const Case *cs, int n, long reps, int restore, uint64_t sp) {
    uint64_t sum = 0;
    for (long r = 0; r < reps; r++) for (int k = 0; k < n; k++) {
        const LCall *c = &lc[k];
        if (restore) __builtin_memcpy(cs[k].chunk, cs[k].pristine, cs[k].chunk_size);
        ctx->r[1] = c->reg[0]; ctx->r[2] = c->reg[1]; ctx->r[8] = c->reg[2]; ctx->r[9] = c->reg[3];
        ctx->r[4] = sp;
        for (int j = 0; j < c->nstk; j++) ST64(sp + 0x28 + 8 * (uint64_t)j, c->stk[j]);
        fn(ctx);
        sum += ctx->r[0];
    }
    return sum;
}

__attribute__((noinline))
static uint64_t restore_loop(const Case *cs, int n, long reps) {
    uint64_t sum = 0;
    for (long r = 0; r < reps; r++) for (int k = 0; k < n; k++) {
        __builtin_memcpy(cs[k].chunk, cs[k].pristine, cs[k].chunk_size);
        sum += cs[k].chunk[0];
        __asm__ volatile("" : : "r"(cs[k].chunk) : "memory");
    }
    return sum;
}

static volatile uint64_t g_sink;

typedef struct TimeCtx {
    const FuncDesc *f; void *native; LiftedFn lifted;
    Case *cs; LCall *lc; int n;
} TimeCtx;

static double run_once(const TimeCtx *t, int what, long reps) {
    uint64_t t0 = now_ns(), sum;
    if (what == 0) sum = t->f->native_loop(t->native, t->cs, t->n, reps, t->f->restore);
    else if (what == 1) sum = lifted_loop(t->lifted, g_ctx, t->lc, t->cs, t->n, reps, t->f->restore, g_sp);
    else sum = restore_loop(t->cs, t->n, reps);
    uint64_t t1 = now_ns();
    g_sink += sum;
    return (double)(t1 - t0) / ((double)reps * t->n);
}

/* One measurement, on a performance core if one can be had: a run that started or ended on
 * an efficiency core is repeated, up to 6 times.  When 24 measurements in a row have found
 * no performance core (macOS confines a process tree it has demoted to background priority
 * to the efficiency cores, and the benchmark cannot undo that), retrying stops until one is
 * seen again.  *pcore says which kind of core the returned time is from. */
static int g_pcore_misses;

static double measure(const TimeCtx *t, int what, long reps, int *ecore_runs, uint8_t *pcore) {
    double best = 0;
    int attempts = g_pcore_misses >= 24 ? 1 : 6;
    for (int attempt = 0; attempt < attempts; attempt++) {
        int ok = on_pcore();
        double ns = run_once(t, what, reps);
        ok = ok && on_pcore();
        if (ok) { g_pcore_misses = 0; *pcore = 1; return ns; }
        if (attempt == 0 || ns < best) best = ns;
        if (attempt + 1 < attempts) sched_yield();
    }
    g_pcore_misses++;
    (*ecore_runs)++;
    *pcore = 0;
    return best;
}

/* Passes over the cases that take about target_ns.  Best of three trials at each size, so
 * that one preempted trial does not make the loop look slow and the run length tiny. */
static long calibrate(const TimeCtx *t, int what, double target_ns) {
    long reps = 1;
    for (;;) {
        double best = 1e30;
        for (int trial = 0; trial < 3; trial++) {
            double e = run_once(t, what, reps) * (double)reps * t->n;
            if (e < best) best = e;
        }
        if (best > 0 && (best >= target_ns / 8 || reps >= (1L << 26))) {
            double want = target_ns / (best / (double)reps);
            return want < 1 ? 1 : (long)want;
        }
        reps *= 4;
    }
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
/* The rounds a side is judged by, sorted: those on performance cores when there are at least
 * three, otherwise all of them.  Returns how many; *pcore says which. */
static int usable(const double *v, const uint8_t *on_p, int n, double *s, int *pcore) {
    int np = 0, k = 0;
    for (int i = 0; i < n; i++) np += on_p ? on_p[i] : 0;
    *pcore = np >= 3;
    for (int i = 0; i < n; i++) if (!*pcore || on_p[i]) s[k++] = v[i];
    qsort(s, (size_t)k, sizeof(double), cmp_double);
    return k;
}
static int min_med(const double *v, const uint8_t *on_p, int n, double *mn, double *med) {
    double s[MAX_ROUNDS];
    int pcore, k = usable(v, on_p, n, s, &pcore);
    *mn = s[0];
    *med = k & 1 ? s[k / 2] : (s[k / 2 - 1] + s[k / 2]) / 2;
    return pcore;
}
/* The best time counts as reproduced when the third best round is within 2% of it. */
static int confirmed(const double *v, const uint8_t *on_p, int n) {
    double s[MAX_ROUNDS];
    int pcore, k = usable(v, on_p, n, s, &pcore);
    return k >= 3 && s[2] <= s[0] * 1.02;
}

static int g_ecore_runs, g_unconfirmed;

static void time_function(Result *res, const Timing *tm, uint64_t seed) {
    const FuncDesc *f = res->f;
    Layout l;
    layout_of(f, &l);
    int n = tm->ncases;
    Case *cs = calloc((size_t)n, sizeof *cs);
    LCall *lc = aligned_alloc(16, (size_t)n * sizeof *lc);
    uint8_t *arena = aligned_alloc(64, (size_t)n * 2 * l.total);
    Rng r = { seed ^ hash_str(f->file) ^ (hash_str(f->name) << 1) ^ UINT64_C(0x7131) };
    g_cur = res->m;
    for (int k = 0; k < n; k++) {
        cs[k].chunk = arena + (size_t)k * 2 * l.total;
        cs[k].pristine = cs[k].chunk + l.total;
        gen_case(f, &l, &cs[k], &r);
        marshal(f, &cs[k], &lc[k]);
    }
    memset(g_ctx, 0, sizeof *g_ctx);
    g_ctx->mxcsr = 0x1F80;
    ST64(g_sp, SENTINEL);
    TimeCtx t = { f, res->native, res->lifted, cs, lc, n };
    /* Both sides must see the same buffer history, so that data-dependent work is the same:
     * restore the buffers before each side's block of runs. */
    long reps_n, reps_l, reps_b = 0;
    for (int k = 0; k < n; k++) memcpy(cs[k].chunk, cs[k].pristine, l.total);
    reps_n = calibrate(&t, 0, tm->target_ns);
    for (int k = 0; k < n; k++) memcpy(cs[k].chunk, cs[k].pristine, l.total);
    reps_l = calibrate(&t, 1, tm->target_ns);
    if (f->restore) reps_b = calibrate(&t, 2, tm->target_ns);
    int round = 0;
    while (round < tm->max_rounds) {
        int first = round & 1;                          /* alternate which side goes first */
        for (int side = 0; side < 2; side++) {
            int what = side ^ first;
            uint8_t on_p;
            double ns = measure(&t, what, what ? reps_l : reps_n, &g_ecore_runs, &on_p);
            if (what) { res->lif[round] = ns; res->lif_p[round] = on_p; }
            else { res->nat[round] = ns; res->nat_p[round] = on_p; }
        }
        if (f->restore) { uint8_t on_p; res->base[round] = measure(&t, 2, reps_b, &g_ecore_runs, &on_p); }
        round++;
        if (round >= tm->rounds && confirmed(res->nat, res->nat_p, round) && confirmed(res->lif, res->lif_p, round)) {
            res->confirmed = 1;
            break;
        }
    }
    res->rounds = round;
    if (!res->confirmed) g_unconfirmed++;
    double med;
    int np = min_med(res->nat, res->nat_p, round, &res->nat_min, &res->nat_med);
    int lp = min_med(res->lif, res->lif_p, round, &res->lif_min, &res->lif_med);
    min_med(res->base, NULL, round, &res->base_min, &med);
    res->cores = np && lp ? 'P' : (!np && !lp ? 'E' : 'M');
    if (f->restore) {
        /* the buffer restore is part of both loops: take it out of both */
        double b = res->base_min;
        res->nat_min -= b; res->nat_med -= b; res->lif_min -= b; res->lif_med -= b;
        if (res->nat_min < 0.05) res->nat_min = 0.05;
        if (res->lif_min < 0.05) res->lif_min = 0.05;
    }
    res->timed = 1;
    free(cs); free(lc); free(arena);
}

/* ---- reports ------------------------------------------------------------------------------ */

static const char *const kKinds[] = { "arith", "mem", "call", "switch", "virtual", "atomic", "probe" };
#define NKINDS 7

static void json_str(FILE *fp, const char *s) {
    fputc('"', fp);
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') { fputc('\\', fp); fputc(*s, fp); }
        else if ((unsigned char)*s < 0x20) fprintf(fp, "\\u%04x", *s);
        else fputc(*s, fp);
    }
    fputc('"', fp);
}

static void write_json(const char *path, Result *res, int n, const Timing *tm, long ncases, uint64_t seed, const char *label) {
    FILE *fp = fopen(path, "w");
    if (!fp) die("cannot write %s", path);
    fprintf(fp, "{\n \"label\": "); json_str(fp, label);
    fprintf(fp, ",\n \"seed\": %llu, \"cases\": %ld, \"rounds\": %d, \"max_rounds\": %d, \"target_ms\": %.3f, \"timing_cases\": %d,\n"
                " \"efficiency_cores\": %d, \"measurements_not_on_a_performance_core\": %d, \"functions_with_unconfirmed_best\": %d,\n"
                " \"functions\": [\n",
            (unsigned long long)seed, ncases, tm->rounds, tm->max_rounds, tm->target_ns / 1e6, tm->ncases, g_ecores, g_ecore_runs,
            g_unconfirmed);
    for (int i = 0; i < n; i++) {
        Result *r = &res[i];
        fprintf(fp, "  {\"file\": \"%s\", \"name\": \"%s\", \"kind\": \"%s\", \"rva\": \"0x%x\", \"status\": \"%s\", "
                    "\"cases\": %ld, \"compared\": %ld, \"mismatches\": %ld, \"needs_unlifted\": %ld",
                r->f->file, r->f->name, r->f->kind, r->rva, kStatus[r->status], r->cases, r->compared, r->mismatches, r->unlifted);
        if (r->unlifted) fprintf(fp, ", \"unlifted_callee\": \"0x%llx\"", (unsigned long long)r->unlifted_at);
        if (r->known) { fprintf(fp, ", \"explained\": "); json_str(fp, r->known->cls); }
        if (r->ndetail) {
            fprintf(fp, ", \"detail\": [");
            for (int k = 0; k < r->ndetail; k++) { if (k) fputc(',', fp); json_str(fp, r->detail[k]); }
            fputc(']', fp);
        }
        if (r->timed) {
            fprintf(fp, ", \"native_ns\": %.4f, \"native_med\": %.4f, \"lifted_ns\": %.4f, \"lifted_med\": %.4f, \"restore_ns\": %.4f"
                        ", \"rounds\": %d, \"confirmed\": %s, \"cores\": \"%c\"",
                    r->nat_min, r->nat_med, r->lif_min, r->lif_med, r->base_min, r->rounds, r->confirmed ? "true" : "false", r->cores);
            fprintf(fp, ", \"native_runs\": [");
            for (int k = 0; k < r->rounds; k++) fprintf(fp, "%s%.4f", k ? "," : "", r->nat[k]);
            fprintf(fp, "], \"lifted_runs\": [");
            for (int k = 0; k < r->rounds; k++) fprintf(fp, "%s%.4f", k ? "," : "", r->lif[k]);
            fputc(']', fp);
        }
        fprintf(fp, "}%s\n", i + 1 < n ? "," : "");
    }
    fprintf(fp, " ]\n}\n");
    fclose(fp);
}

static void cores_report(void) {
    /* Times a fixed loop for three seconds at the benchmark's QoS class and prints the best
     * time per CPU number it ran on.  The two speeds show which numbers are which cores. */
    double best[64];
    int seen[64] = { 0 };
    for (int i = 0; i < 64; i++) best[i] = 1e30;
    printf("sysctl says CPU numbers 0..%d are efficiency cores\n", g_ecores - 1);
    uint64_t until = now_ns() + UINT64_C(3000000000);
    for (int k = 0; now_ns() < until; k++) {
        int c0 = cpu_now();
        uint64_t t0 = now_ns(), x = 1;
        for (int i = 0; i < 20000; i++) { x = x * UINT64_C(6364136223846793005) + 1; __asm__ volatile("" : "+r"(x)); }
        uint64_t t1 = now_ns();
        g_sink += x;
        if (c0 == cpu_now() && c0 >= 0 && c0 < 64) {
            double ns = (double)(t1 - t0) / 20000;
            seen[c0]++;
            if (ns < best[c0]) best[c0] = ns;
        }
        if ((k & 63) == 63) usleep(300);
    }
    for (int i = 0; i < 64; i++)
        if (seen[i]) printf("  cpu %2d: best %.3f ns per multiply-add step, %4d samples (%s by sysctl)\n", i, best[i], seen[i],
                            i < g_ecores ? "efficiency" : "performance");
}

int main(int argc, char **argv) {
    long ncases = 2000;
    Timing tm = { 0, 5e5, 32, 0 };
    uint64_t seed = 20261008;
    const char *filter = NULL, *json = NULL, *known = NULL, *label = "default", *only_kind = NULL;
    int list = 0, cores = 0, verbose = 0, no_fork = 0, x86_ordering = 0;
    long ctx_offset = 0x200;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--cases") && v) { ncases = atol(v); i++; }
        else if (!strcmp(a, "--rounds") && v) { tm.rounds = atoi(v); i++; }
        else if (!strcmp(a, "--max-rounds") && v) { tm.max_rounds = atoi(v); i++; }
        else if (!strcmp(a, "--target-ms") && v) { tm.target_ns = atof(v) * 1e6; i++; }
        else if (!strcmp(a, "--timing-cases") && v) { tm.ncases = atoi(v); i++; }
        else if (!strcmp(a, "--seed") && v) { seed = strtoull(v, 0, 0); i++; }
        else if (!strcmp(a, "--filter") && v) { filter = v; i++; }
        else if (!strcmp(a, "--kind") && v) { only_kind = v; i++; }
        else if (!strcmp(a, "--json") && v) { json = v; i++; }
        else if (!strcmp(a, "--known") && v) { known = v; i++; }
        else if (!strcmp(a, "--label") && v) { label = v; i++; }
        else if (!strcmp(a, "--ctx-offset") && v) { ctx_offset = strtol(v, 0, 0); i++; }
        else if (!strcmp(a, "--list")) list = 1;
        else if (!strcmp(a, "--cores")) cores = 1;
        else if (!strcmp(a, "--verbose")) verbose = 1;
        else if (!strcmp(a, "--no-fork")) no_fork = 1;
        else if (!strcmp(a, "--show") && v) { g_show = atol(v); i++; }
        else if (!strcmp(a, "--x86-ordering")) x86_ordering = 1;
        else {
            fprintf(stderr, "usage: driver [--cases N] [--rounds R] [--max-rounds M] [--target-ms T] [--timing-cases K] [--seed S]\n"
                            "              [--filter TEXT] [--kind KIND] [--json FILE] [--known FILE] [--label TEXT]\n"
                            "              [--list] [--cores] [--verbose] [--no-fork] [--show N] [--ctx-offset BYTES] [--x86-ordering]\n");
            return 2;
        }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (tm.rounds > MAX_ROUNDS) tm.rounds = MAX_ROUNDS;
    if (tm.max_rounds < tm.rounds) tm.max_rounds = tm.rounds * 8;
    if (tm.max_rounds > MAX_ROUNDS) tm.max_rounds = MAX_ROUNDS;
    if (tm.ncases < 1) tm.ncases = 1;
    perf_setup();
    if (x86_ordering) x86_ordering_on();
    else printf("x86 memory ordering: off\n");
    if (cores) { cores_report(); return 0; }
    if (known) load_known(known);
    modules_setup();
    signals_setup();
    g_stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (g_stack == MAP_FAILED) die("cannot map the guest stack");
    g_sp = (uint64_t)(uintptr_t)(g_stack + STACK_SIZE - 0x1000) - 8;
    {
        uint8_t *m = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (m == MAP_FAILED || ctx_offset < 0 || ctx_offset > 0x8000 || (ctx_offset & 15)) die("cannot place the Ctx");
        g_ctx = (Ctx *)(void *)(m + 0x4000 + ctx_offset);
    }

    Result *res = calloc((size_t)bench_nfuncs, sizeof *res);
    int n = 0;
    for (int i = 0; i < bench_nfuncs; i++) {
        const FuncDesc *f = &bench_funcs[i];
        char full[128];
        snprintf(full, sizeof full, "%s/%s", f->file, f->name);
        if (filter && !strstr(full, filter)) continue;
        if (only_kind && strcmp(only_kind, f->kind)) continue;
        Result *r = &res[n++];
        r->f = f;
        for (int k = 0; k < g_nmod; k++) if (!strcmp(g_mod[k].d->file, f->file)) r->m = &g_mod[k];
        r->known = known_for(f);
        r->rva = export_rva(r->m, f->name);
        r->native = dlsym(r->m->dylib, f->name);
        /* The table has a row for a skipped function too (a stub that stops): not lifted. */
        r->lifted = r->rva && !is_skipped(r->m, r->rva) ? table_lookup(r->m, r->rva) : NULL;
        if (!r->rva) r->status = FS_NO_EXPORT;
        else if (!r->native) r->status = FS_NO_NATIVE;
        else if (!r->lifted) r->status = FS_NOT_LIFTED;
        else if (list) r->status = FS_PASS;
        else if (no_fork) { watchdog(1); check_function(r, ncases, seed); watchdog(0); }
        else check_isolated(r, ncases, seed);
    }
    if (list) {
        for (int i = 0; i < n; i++)
            printf("%-10s %-24s %-8s 0x%05x %s\n", res[i].f->file, res[i].f->name, res[i].f->kind, res[i].rva, kStatus[res[i].status]);
        return 0;
    }

    /* correctness report */
    long counts[6] = { 0 }, explained = 0, unexplained = 0, total_cases = 0;
    for (int i = 0; i < n; i++) {
        Result *r = &res[i];
        counts[r->status]++;
        total_cases += r->compared;
        if (r->status == FS_MISMATCH) {
            if (r->known) { explained++; r->known->seen = 1; } else unexplained++;
        }
        if (r->status == FS_PASS && !verbose) continue;
        if (r->status == FS_NOT_LIFTED && !verbose) continue;
        printf("%-10s %-24s %-14s", r->f->file, r->f->name, kStatus[r->status]);
        if (r->status == FS_MISMATCH)
            printf(" %ld of %ld cases%s%s", r->mismatches, r->compared, r->known ? "  explained: " : "  UNEXPLAINED",
                   r->known ? r->known->cls : "");
        if (r->unlifted)
            printf(" %ld of %ld cases reach unlifted code at %s+0x%llx", r->unlifted, r->cases, r->f->file,
                   (unsigned long long)r->unlifted_at);
        printf("\n");
        for (int k = 0; k < r->ndetail; k++) printf("    %s\n", r->detail[k]);
    }
    for (int i = 0; i < n; i++)
        if (res[i].status == FS_NO_EXPORT || res[i].status == FS_NO_NATIVE)
            unexplained++;                              /* a wrong table row is a failure too */
    for (Known *k = g_known; k; k = k->next)
        if (!k->seen && !filter && !only_kind)
            printf("note: %s %s is listed as an explained mismatch (%s) but did not mismatch in this run\n", k->file, k->name, k->cls);
    printf("correctness: %d functions described; %ld compared and identical, %ld mismatch (%ld explained, %ld unexplained), "
           "%ld not lifted, %ld need unlifted code on every input, %ld bad rows; %ld cases compared\n",
           n, counts[FS_PASS], counts[FS_MISMATCH], explained, unexplained - counts[FS_NO_EXPORT] - counts[FS_NO_NATIVE],
           counts[FS_NOT_LIFTED], counts[FS_NEEDS_UNLIFTED], counts[FS_NO_EXPORT] + counts[FS_NO_NATIVE], total_cases);
    {
        int files = 0;
        for (int k = 0; k < g_nmod; k++) {
            long c = 0;
            for (int i = 0; i < n; i++) if (res[i].m == &g_mod[k] && (res[i].status == FS_PASS || res[i].status == FS_MISMATCH)) c++;
            if (c) files++;
            printf("  %-10s %3ld compared%s%s\n", g_mod[k].d->file, c, g_mod[k].d->native_note ? "   native side: " : "",
                   g_mod[k].d->native_note ? g_mod[k].d->native_note : "");
        }
        printf("  %d corpus files have compared functions\n", files);
    }

    /* timing */
    if (tm.rounds > 0) {
        printf("\ntiming: at least %d rounds of %.2f ms per side (more, up to %d, until the best round is reproduced within 2%%\n"
               "by two others), %d inputs per function, best round reported; spread = (median - best) / best\n",
               tm.rounds, tm.target_ns / 1e6, tm.max_rounds, tm.ncases);
        printf("%-10s %-24s %-8s %10s %7s %10s %7s %7s  %s\n", "file", "function", "kind", "native ns", "spread", "lifted ns", "spread", "ratio",
               "cores");
        double logsum[NKINDS + 1] = { 0 }, logmed[NKINDS + 1] = { 0 }, lo[NKINDS + 1], hi[NKINDS + 1];
        int cnt[NKINDS + 1] = { 0 }, on_p = 0, on_e = 0, mixed = 0;
        for (int k = 0; k <= NKINDS; k++) { lo[k] = 1e30; hi[k] = 0; }
        for (int i = 0; i < n; i++) {
            Result *r = &res[i];
            if (r->status != FS_PASS || r->f->notime || r->unlifted) continue;
            time_function(r, &tm, seed);
            double ratio = r->lif_min / r->nat_min;
            printf("%-10s %-24s %-8s %10.2f %6.0f%% %10.2f %6.0f%% %7.2f  %c\n", r->f->file, r->f->name, r->f->kind,
                   r->nat_min, 100 * (r->nat_med - r->nat_min) / r->nat_min,
                   r->lif_min, 100 * (r->lif_med - r->lif_min) / r->lif_min, ratio, r->cores);
            fflush(stdout);
            if (r->cores == 'P') on_p++; else if (r->cores == 'E') on_e++; else mixed++;
            if (r->cores == 'M') continue;              /* one side on each kind of core: not a ratio */
            int kind = 0;
            while (kind < NKINDS && strcmp(kKinds[kind], r->f->kind)) kind++;
            int probe = !strcmp(r->f->kind, "probe") || r->m->d->native_note;
            for (int pass = 0; pass < 2; pass++) {
                int k = pass ? NKINDS : kind;
                if (pass && probe) continue;            /* the headline leaves out what is not the same source twice */
                logsum[k] += log(ratio);
                logmed[k] += log(r->lif_med / r->nat_med);
                cnt[k]++;
                if (ratio < lo[k]) lo[k] = ratio;
                if (ratio > hi[k]) hi[k] = ratio;
            }
        }
        printf("\ncores: %d functions timed on performance cores (P), %d on efficiency cores (E), %d with one side on each (M, left out below)\n",
               on_p, on_e, mixed);
        if (on_e)
            printf("  NOTE: no performance core was available for the E rows.  macOS confines a process tree it has demoted to\n"
                   "  background priority (for example under an application in App Nap) to the efficiency cores.\n");
        printf("\nlifted / native, geometric mean of the per-function ratios: best round of each side, and (med) median round of each side\n");
        for (int k = 0; k <= NKINDS; k++)
            if (cnt[k])
                printf("  %-28s %3d functions  %6.2fx   range %.2fx to %.2fx   med %.2fx\n",
                       k < NKINDS ? kKinds[k] : "all but probe (same source)", cnt[k], exp(logsum[k] / cnt[k]), lo[k], hi[k],
                       exp(logmed[k] / cnt[k]));
        if (g_ecore_runs)
            printf("  %d measurements were not taken on a performance core\n", g_ecore_runs);
        if (g_unconfirmed)
            printf("  %d functions: the best round of one side was not reproduced within 2%% in %d rounds\n", g_unconfirmed, tm.max_rounds);
    }
    if (json) write_json(json, res, n, &tm, ncases, seed, label);
    return unexplained ? 1 : 0;
}
