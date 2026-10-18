/* Candidate side of the differential test (docs/DESIGN.md section 8).
 *
 *   driver <cases.bin> <results.bin>
 *
 * Linked with the lifted C of one module, built with -DHARNESS_ISOLATED -DMODULE=<module>.
 * Maps the relocated image, the scratch arena and the guest stack at the fixed addresses
 * the case file names, then runs every case in a forked child, so a wild write or a hang
 * is one failed case and not a dead harness.  The case file names one or more image files
 * (variants of the same module that differ only in data) and the memory files (arena
 * followed by stack), each of which says which image it goes with.  The child reports registers, flags, xmm, the
 * call record and every 4 KiB page of the three regions that differs from its initial
 * contents.  All layout numbers come from the case file; none are compiled in.
 *
 * A case may carry "pokes": byte strings written into the image, arena or stack before the
 * run.  They are part of the case's initial state (the oracle applies the same ones), so the
 * child also applies them to its private copy of the pristine memory and a poked page is
 * reported only if the lifted code changed it afterwards.
 *
 * A case may also name "demand pages": 4 KiB pages outside the three regions that the oracle
 * mapped when the function touched them (harness/difftest.py, demand_page).  Their contents are
 * a function of the case's seed and the page's address, so only the addresses travel.  The
 * child maps each before the lifted function runs and reports those that changed.  The host's
 * pages are 16 KiB: the three neighbours inside a host page get their own demand contents too,
 * which is what the oracle would have mapped had the function gone there, and are compared.
 */
#include "runtime.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CAT_(a, b) a##b
#define CAT(a, b) CAT_(a, b)
#define TABLE_(m) CAT(m, _table)
#define COUNT_(m) CAT(m, _table_count)
extern const LiftedEntry TABLE_(MODULE)[];
extern const size_t COUNT_(MODULE);
extern uint64_t CAT(g_base_, MODULE);

#define PAGE 4096u
#define HOST_PAGE 0x4000u
#define MAX_CALLS 8192
#define MAX_DEMAND 256          /* DEMAND_MAX in difftest.py */
#define TIMEOUT_SECONDS 10

enum { ST_OK = 0, ST_TRAP = 1, ST_SIGNAL = 2, ST_TIMEOUT = 3, ST_NOFUNC = 4, ST_OVERFLOW = 5,
       ST_BADRET = 6, ST_LOST = 7, ST_FPMODE = 8, ST_NOMAP = 9, ST_CUT = 10 };

typedef struct {
    uint32_t magic, n_images, n_mems, n_cases;
    uint64_t image_base, image_size, arena_base, arena_size, stack_base, stack_size, sentinel;
    uint64_t n_probes;           /* addresses of the stack probe, after the memory entries */
} CasesHdr;

#define CASES_MAGIC 0x33434654
#define CASE_PROBE 1u            /* CaseIn.flags: a call to a stack probe only returns (difftest.py, probe_targets) */

typedef struct {
    char     path[256];
    uint32_t image_id, pad;
} MemEntry;

typedef struct {
    uint32_t case_id, mem_id, func_rva, eflags;
    uint64_t seed;
    uint64_t r[16];
    uint64_t gs_base;
    uint32_t mxcsr, poke_bytes;  /* poke_bytes: size of the poke list that follows this record */
    uint32_t fpcw, n_demand;     /* x87 control word at the start; demand pages after the poke list */
    uint32_t cut_at, flags;      /* the case ends when the event of this index arrives (0xFFFFFFFF: never) */
    uint8_t  xmm[256];
} CaseIn;

/* The poke list of a case: repeated { uint64_t addr, len; uint8_t data[len padded to 8]; }.
 * The demand pages: n_demand addresses, each a multiple of 4096. */
typedef struct {
    CaseIn    c;
    uint8_t  *pokes;
    uint64_t *demand;
} CaseSlot;

/* One event of the call record.  Everything the callee could read is here: the stub
 * overwrites rax, rcx, rdx, r8-r11 and xmm0-xmm5 afterwards, so a wrong value in any of them
 * that is not recorded would be invisible (review finding 3).  `ret` is the 8 bytes at rsp,
 * i.e. the return address a call pushed (0 for cpuid/rdtsc events, which are not calls). */
typedef struct {
    uint32_t kind, pad;
    uint64_t target, rsp, rcx, rdx, r8, r9, rax, r10, r11, ret;
    uint8_t  xmm[96];           /* xmm0-xmm5 */
} CallRec;

typedef struct {
    uint32_t case_id, status, aux, eflags;
    uint64_t r[16];
    uint8_t  xmm[256];
    uint64_t final_ra;
    uint32_t ncalls, npages;
    uint32_t mxcsr, fpcw;
} ResHdr;

typedef struct {
    uint64_t seed;
    uint32_t ncalls;
    int      overflow;
    uint64_t final_ra;
    CallRec  calls[MAX_CALLS];
} Harness;

static CasesHdr g_hdr;
static Harness g_h;
static const CaseIn *g_case;
static int g_out = -1;
static uint64_t *g_probes;
static uint8_t *g_pristine_image, *g_pristine_mem;   /* mem = arena followed by stack */
static uint64_t g_demand_host[MAX_DEMAND];            /* the host pages mapped for this case's demand pages */
static uint32_t g_n_demand_host;
uint64_t h_sentinel;
uint8_t tf_straddle_lock;       /* runtime.h: the lock for locked operands that straddle 16 bytes */

static void die(const char *what) {
    fprintf(stderr, "driver: %s: %s\n", what, strerror(errno));
    exit(2);
}

/* ---- float mode (fpmode.h, docs/DESIGN.md section 8) -------------------------------------
 *
 * Lifted code runs in the guest float mode, host code in the host mode.  The driver switches
 * at every crossing: run_case around the lifted function, and every function lifted code can
 * call (h_stub, h_ret, h_trap, rt_cpuid, rt_rdtsc) on entry and before it returns.  Both rules
 * are checked, not assumed: the host code under those entry points (record, emit_result)
 * looks at FPCR and reports ST_FPMODE if it is not the host's, and run_case requires the lifted
 * function to come back in the guest mode of its final MXCSR.
 *
 * The TF_MUTANT_* macros exist for tests/run_selftests.py only: each removes one of the switches,
 * and the self-tests require the harness to notice. */
static int g_fp_violation;      /* 1: lifted code was not in the guest mode; 2: host code was not in the host mode */

static uint64_t guest_fpcr(const Ctx *ctx) {
#if defined(TF_MUTANT_NO_GUEST_MODE)
    (void)ctx;
    return TF_FPCR_HOST;
#elif defined(TF_MUTANT_ROUND_NEAREST)
    return tf_fpcr_for_mxcsr(ctx->mxcsr & ~(3u << TF_MXCSR_RC_SHIFT));
#else
    return tf_fpcr_for_mxcsr(ctx->mxcsr);
#endif
}
static void guest_enter(const Ctx *ctx) { tf_fpcr_write(guest_fpcr(ctx)); }
static void host_enter(void) {
#ifndef TF_MUTANT_NO_HOST_MODE
    tf_fp_enter_host();
#endif
}
static void host_check(void) {
    if (tf_fpcr_read() != TF_FPCR_HOST) { g_fp_violation = 2; tf_fp_enter_host(); }
}

static void write_all(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    while (n) {
        ssize_t k = write(fd, p, n);
        if (k < 0) { if (errno == EINTR) continue; _exit(3); }
        p += k; n -= (size_t)k;
    }
}

/* ---- the stub effect: must match harness/difftest.py exactly --------------------------- */

static uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= UINT64_C(0xBF58476D1CE4E5B9);
    x ^= x >> 27; x *= UINT64_C(0x94D049BB133111EB);
    x ^= x >> 31;
    return x;
}
static uint64_t hv(uint64_t seed, uint32_t idx, uint32_t k) {
    return mix64(seed + UINT64_C(0x9E3779B97F4A7C15) * (uint64_t)(idx + 1)
                      + UINT64_C(0xD1B54A32D192ED03) * (uint64_t)(k + 1));
}
/* The stub's return value.  Three seeds in four always return an arena pointer, so that
 * callers which dereference the result can run to the end; the fourth mixes in 0, 1 and
 * small numbers to reach the other branches. */
static uint64_t stub_rax(uint64_t seed, uint64_t v) {
    unsigned sel = (unsigned)(v & 7);
    uint64_t w = v >> 8;
    if ((seed & 3) != 0 || sel <= 4) return g_hdr.arena_base + 0x40000 + (w % 0x8000) * 16;
    if (sel == 5) return 0;
    if (sel == 6) return 1;
    return w & 0xFF;
}

static uint32_t pack_flags(const Ctx *c) {
    return (uint32_t)c->cf | ((uint32_t)c->pf << 2) | ((uint32_t)c->af << 4) | ((uint32_t)c->zf << 6)
         | ((uint32_t)c->sf << 7) | ((uint32_t)c->df << 10) | ((uint32_t)c->of << 11);
}

static void emit_result(Ctx *ctx, uint32_t status, uint32_t aux);

static int record(Ctx *ctx, int kind, uint64_t target) {
    Harness *h = (Harness *)ctx->host;
    host_check();
    if (h->ncalls == g_case->cut_at) {
        /* The oracle met a limit and stopped at its event of this index (difftest.py, Oracle._run).
         * So does the candidate: the state as the callee would find it, and which event it was. */
        h->final_ra = target;
        emit_result(ctx, ST_CUT, (uint32_t)kind);
        _exit(0);
    }
    if (h->ncalls >= MAX_CALLS) { h->overflow = 1; return -1; }
    CallRec *r = &h->calls[h->ncalls];
    r->kind = (uint32_t)kind; r->pad = 0; r->target = target;
    r->rsp = ctx->r[4]; r->rcx = ctx->r[1]; r->rdx = ctx->r[2]; r->r8 = ctx->r[8]; r->r9 = ctx->r[9];
    r->rax = ctx->r[0]; r->r10 = ctx->r[10]; r->r11 = ctx->r[11];
    r->ret = (kind == TF_CPUID || kind == TF_RDTSC) ? 0 : LD64(ctx->r[4]);
    memcpy(r->xmm, ctx->xmm, sizeof r->xmm);
    return (int)h->ncalls++;
}

static void emit_result(Ctx *ctx, uint32_t status, uint32_t aux);
static volatile sig_atomic_t g_emitting;     /* a result is being written: the signal handler must not add one */

/* ---- pokes ------------------------------------------------------------------------------ */

/* Live and pristine pointers for [addr, addr+len) if it lies inside one of the three regions. */
static int locate(uint64_t addr, uint64_t len, uint8_t **live, uint8_t **pristine) {
    const struct { uint64_t base, size; uint8_t *pristine; } reg[3] = {
        { g_hdr.image_base, g_hdr.image_size, g_pristine_image },
        { g_hdr.arena_base, g_hdr.arena_size, g_pristine_mem },
        { g_hdr.stack_base, g_hdr.stack_size, g_pristine_mem ? g_pristine_mem + g_hdr.arena_size : 0 },
    };
    for (int i = 0; i < 3; i++) {
        if (addr >= reg[i].base && len <= reg[i].size && addr - reg[i].base <= reg[i].size - len) {
            *live = (uint8_t *)(uintptr_t)addr;
            *pristine = reg[i].pristine + (addr - reg[i].base);
            return 1;
        }
    }
    return 0;
}

/* Walk a poke list.  apply = 0 only validates (parent, before forking). */
static int walk_pokes(const CaseSlot *s, int apply) {
    uint32_t pos = 0, n = s->c.poke_bytes;
    while (pos < n) {
        uint64_t addr, len;
        if (n - pos < 16) return 0;
        memcpy(&addr, s->pokes + pos, 8);
        memcpy(&len, s->pokes + pos + 8, 8);
        pos += 16;
        uint64_t padded = (len + 7) & ~UINT64_C(7);
        uint8_t *live, *pristine;
        if (len > n - pos || padded > n - pos || !locate(addr, len, &live, &pristine)) return 0;
        if (apply) {
            memcpy(live, s->pokes + pos, len);
            memcpy(pristine, s->pokes + pos, len);     /* the child's own copy-on-write copy */
        }
        pos += (uint32_t)padded;
    }
    return 1;
}

/* ---- demand pages: must match harness/difftest.py exactly -------------------------------- */

/* Initial contents of the 4 KiB demand page at addr: 512 pointers into the arena. */
static void demand_fill(uint8_t *dst, uint64_t seed, uint64_t addr) {
    uint64_t k = mix64(seed ^ mix64((addr >> 12) + UINT64_C(0x44454D414E44)));
    for (uint64_t i = 1; i <= PAGE / 8; i++) {
        uint64_t v = g_hdr.arena_base + 0x40000 + (mix64(k + UINT64_C(0x9E3779B97F4A7C15) * i) % 0x8000) * 16;
#ifdef TF_MUTANT_DEMAND_CONTENT     /* self-test: the two sides do not start from the same memory */
        v += 16;
#endif
        memcpy(dst + (i - 1) * 8, &v, 8);
    }
}

/* Map the case's demand pages.  Returns 0, with the host page that could not be had in *bad,
 * if the kernel put a page elsewhere: something of this process is already there. */
static int map_demand(const CaseSlot *s, uint64_t *bad) {
    g_n_demand_host = 0;
    for (uint32_t i = 0; i < s->c.n_demand; i++) {
        uint64_t host = s->demand[i] & ~(uint64_t)(HOST_PAGE - 1);
        uint32_t k = 0;
        while (k < g_n_demand_host && g_demand_host[k] != host) k++;
        if (k < g_n_demand_host) continue;
        void *p = mmap((void *)(uintptr_t)host, HOST_PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p != (void *)(uintptr_t)host) { *bad = host; return 0; }
        for (uint64_t off = 0; off < HOST_PAGE; off += PAGE) demand_fill((uint8_t *)p + off, s->c.seed, host + off);
        g_demand_host[g_n_demand_host++] = host;
    }
    return 1;
}

void h_stub(Ctx *ctx, int kind, uint64_t target) {
    Harness *h = (Harness *)ctx->host;
    host_enter();
    int idx = record(ctx, kind, target);
    if (idx < 0) { emit_result(ctx, ST_OVERFLOW, 0); _exit(0); }
    if ((g_case->flags & CASE_PROBE) && (kind == TF_CALL_DIRECT || kind == TF_CALL_INDIRECT)) {
        for (uint64_t i = 0; i < g_hdr.n_probes; i++) {
            if (g_probes[i] == target) {    /* the stack probe's contract: it only returns */
                ctx->r[4] += 8;
                guest_enter(ctx);
                return;
            }
        }
    }
    uint64_t s = h->seed;
    ctx->r[0]  = stub_rax(s, hv(s, (uint32_t)idx, 0));
    ctx->r[1]  = hv(s, (uint32_t)idx, 1);
    ctx->r[2]  = hv(s, (uint32_t)idx, 2);
    ctx->r[8]  = hv(s, (uint32_t)idx, 3);
    ctx->r[9]  = hv(s, (uint32_t)idx, 4);
    ctx->r[10] = hv(s, (uint32_t)idx, 5);
    ctx->r[11] = hv(s, (uint32_t)idx, 6);
    for (int i = 0; i < 6; i++) {
        ctx->xmm[i].q[0] = hv(s, (uint32_t)idx, 16 + 2 * (uint32_t)i);
        ctx->xmm[i].q[1] = hv(s, (uint32_t)idx, 17 + 2 * (uint32_t)i);
    }
    /* the stub returns: pop the return address */
    uint64_t ra = LD64(ctx->r[4]);
    ctx->r[4] += 8;
    if (kind == TF_TAIL_DIRECT || kind == TF_TAIL_INDIRECT) h->final_ra = ra;
    guest_enter(ctx);
}

void h_ret(Ctx *ctx, uint64_t return_address) {
    host_enter();
    ((Harness *)ctx->host)->final_ra = return_address;
    guest_enter(ctx);
}

_Noreturn void h_trap(Ctx *ctx, uint32_t rva) {
    host_enter();
    emit_result(ctx, ST_TRAP, rva);
    _exit(0);
}

/* cpuid and rdtsc are stub events too: the oracle cannot agree with a real shim. */
void rt_cpuid(Ctx *ctx) {
    Harness *h = (Harness *)ctx->host;
    host_enter();
    int idx = record(ctx, TF_CPUID, 0);
    if (idx < 0) { emit_result(ctx, ST_OVERFLOW, 0); _exit(0); }
    ctx->r[0] = (uint32_t)hv(h->seed, (uint32_t)idx, 0);
    ctx->r[3] = (uint32_t)hv(h->seed, (uint32_t)idx, 1);
    ctx->r[1] = (uint32_t)hv(h->seed, (uint32_t)idx, 2);
    ctx->r[2] = (uint32_t)hv(h->seed, (uint32_t)idx, 3);
    guest_enter(ctx);
}
void rt_rdtsc(Ctx *ctx) {
    Harness *h = (Harness *)ctx->host;
    host_enter();
    int idx = record(ctx, TF_RDTSC, 0);
    if (idx < 0) { emit_result(ctx, ST_OVERFLOW, 0); _exit(0); }
    ctx->r[0] = (uint32_t)hv(h->seed, (uint32_t)idx, 0);
    ctx->r[2] = (uint32_t)hv(h->seed, (uint32_t)idx, 1);
    guest_enter(ctx);
}

/* ---- result ---------------------------------------------------------------------------- */

static uint32_t count_or_emit_pages(const uint8_t *live, const uint8_t *pristine, uint64_t base,
                                    uint64_t size, int emit) {
    uint32_t n = 0;
    for (uint64_t off = 0; off < size; off += PAGE) {
        uint64_t len = size - off < PAGE ? size - off : PAGE;
        if (memcmp(live + off, pristine + off, len) != 0) {
            n++;
            if (emit) {
                uint8_t page[PAGE];
                uint64_t addr = base + off;
                memset(page, 0, sizeof page);
                memcpy(page, live + off, len);
                write_all(g_out, &addr, 8);
                write_all(g_out, page, PAGE);
            }
        }
    }
    return n;
}

/* ---- the image: mapped from its file, written pages known ---------------------------------
 *
 * The image of a large module is tens to hundreds of megabytes (client 64, engine 334: mostly
 * zero-initialised data).  Copying it in for every image variant and comparing all of it after
 * every case was most of the candidate's time there.  So:
 *   - the image and its pristine copy are two private mappings of the image file (map_image):
 *     nothing is copied, and the cases' children share the pages they do not write;
 *   - the live mapping is read-only.  The first write to a host page of it faults; the handler
 *     makes that page writable, notes it and lets the write happen (image_write_fault).  A
 *     page that was never noted was never written, so only the noted pages are compared with
 *     the pristine copy afterwards.  What is reported is what comparing everything reported.
 * A fault in the image on a page that is already writable is not this mechanism's: it goes on
 * to the handler's ordinary path. */
static uint8_t *g_img_rw;           /* per host page of the image: made writable in this process */
static uint32_t *g_img_written;     /* those pages, in the order of their first write */
static uint32_t g_n_img_written;

static int image_write_fault(uint64_t addr) {
    if (!g_img_rw || addr < g_hdr.image_base || addr - g_hdr.image_base >= g_hdr.image_size) return 0;
    uint64_t k = (addr - g_hdr.image_base) / HOST_PAGE;
    if (g_img_rw[k]) return 0;
    if (mprotect((void *)(uintptr_t)(g_hdr.image_base + k * HOST_PAGE), HOST_PAGE, PROT_READ | PROT_WRITE) != 0) return 0;
    g_img_rw[k] = 1;
    g_img_written[g_n_img_written++] = (uint32_t)k;
    return 1;
}

static void map_image(const char *path) {
    uint64_t size = (g_hdr.image_size + HOST_PAGE - 1) & ~(uint64_t)(HOST_PAGE - 1);
    int fd = open(path, O_RDONLY);
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) die(path);
    if ((uint64_t)st.st_size != g_hdr.image_size) { fprintf(stderr, "driver: %s is not of the image's size\n", path); exit(2); }
    void *live = mmap((void *)(uintptr_t)g_hdr.image_base, size, PROT_READ, MAP_PRIVATE | MAP_FIXED, fd, 0);
    if (live == MAP_FAILED || (uint64_t)(uintptr_t)live != g_hdr.image_base) die("mmap (image)");
    if (g_pristine_image) munmap(g_pristine_image, size);
    /* writable: a case's child writes its pokes into its own copy-on-write copy (walk_pokes) */
    g_pristine_image = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    if (g_pristine_image == MAP_FAILED) die("mmap (pristine image)");
    close(fd);
}

/* The changed pages of the image, arena and stack are looked for once and remembered for the
 * output.  The list is allocated by main() before any case runs, one entry per 4 KiB page of
 * the three regions. */
typedef struct { uint64_t addr; const uint8_t *live; uint64_t len; } ChangedPage;
static ChangedPage *g_changed;
static uint32_t g_n_changed;

static void find_changed(const uint8_t *live, const uint8_t *pristine, uint64_t base, uint64_t size) {
    for (uint64_t off = 0; off < size; off += PAGE) {
        uint64_t len = size - off < PAGE ? size - off : PAGE;
        if (memcmp(live + off, pristine + off, len) != 0)
            g_changed[g_n_changed++] = (ChangedPage){ base + off, live + off, len };
    }
}

static void emit_result(Ctx *ctx, uint32_t status, uint32_t aux) {
    Harness *h = (Harness *)ctx->host;
    ResHdr res;
    host_check();
    g_emitting = 1;
    if (g_fp_violation && (status == ST_OK || status == ST_TRAP || status == ST_CUT)) { status = ST_FPMODE; aux = (uint32_t)g_fp_violation; }
    memset(&res, 0, sizeof res);
    res.case_id = g_case->case_id;
    res.status = status;
    res.aux = aux;
    res.mxcsr = ctx->mxcsr;
    res.fpcw = ctx->fpcw;
    res.eflags = pack_flags(ctx);
    memcpy(res.r, ctx->r, sizeof res.r);
    memcpy(res.xmm, ctx->xmm, sizeof res.xmm);
    res.final_ra = h->final_ra;
    res.ncalls = h->ncalls;
    const uint8_t *img = (const uint8_t *)(uintptr_t)g_hdr.image_base;
    const uint8_t *arena = (const uint8_t *)(uintptr_t)g_hdr.arena_base;
    const uint8_t *stack = (const uint8_t *)(uintptr_t)g_hdr.stack_base;
    g_n_changed = 0;
    for (uint32_t a = 1; a < g_n_img_written; a++) {            /* by address: few, and nearly in order */
        uint32_t k = g_img_written[a], b = a;
        for (; b > 0 && g_img_written[b - 1] > k; b--) g_img_written[b] = g_img_written[b - 1];
        g_img_written[b] = k;
    }
    for (uint32_t a = 0; a < g_n_img_written; a++) {
        uint64_t off = (uint64_t)g_img_written[a] * HOST_PAGE;
        uint64_t len = g_hdr.image_size - off < HOST_PAGE ? g_hdr.image_size - off : HOST_PAGE;
        find_changed(img + off, g_pristine_image + off, g_hdr.image_base + off, len);
    }
    find_changed(arena, g_pristine_mem, g_hdr.arena_base, g_hdr.arena_size);
    find_changed(stack, g_pristine_mem + g_hdr.arena_size, g_hdr.stack_base, g_hdr.stack_size);
    for (int emit = 0; emit < 2; emit++) {
        uint32_t n = g_n_changed;
        if (emit) {
            write_all(g_out, &res, sizeof res);
            write_all(g_out, h->calls, sizeof(CallRec) * h->ncalls);
            for (uint32_t k = 0; k < g_n_changed; k++) {
                uint8_t page[PAGE];
                memset(page, 0, sizeof page);
                memcpy(page, g_changed[k].live, g_changed[k].len);
                write_all(g_out, &g_changed[k].addr, 8);
                write_all(g_out, page, PAGE);
            }
        }
        for (uint32_t k = 0; k < g_n_demand_host; k++) {
            uint8_t was[HOST_PAGE];
            for (uint64_t off = 0; off < HOST_PAGE; off += PAGE) demand_fill(was + off, g_case->seed, g_demand_host[k] + off);
            n += count_or_emit_pages((const uint8_t *)(uintptr_t)g_demand_host[k], was, g_demand_host[k], HOST_PAGE, emit);
        }
        res.npages = n;
    }
}

static void on_signal(int sig, siginfo_t *info, void *uc) {
    (void)uc;
    /* the first write to a page of the image: let it happen (the float mode and everything else
     * of the interrupted code come back with the return from the handler) */
    if ((sig == SIGSEGV || sig == SIGBUS) && image_write_fault((uint64_t)(uintptr_t)info->si_addr)) return;
    tf_fp_enter_host();     /* a handler starts with the interrupted code's FPCR */
    if (!g_emitting && g_case && (sig == SIGSEGV || sig == SIGBUS)) {
        /* Diagnostics only: the address the lifted code could not reach, in final_ra. */
        ResHdr res;
        memset(&res, 0, sizeof res);
        res.case_id = g_case->case_id;
        res.status = ST_SIGNAL;
        res.aux = (uint32_t)sig;
        res.final_ra = (uint64_t)(uintptr_t)info->si_addr;
        g_emitting = 1;
        write_all(g_out, &res, sizeof res);
        _exit(0);
    }
    _exit(128 + sig);       /* not a crash as far as the OS is concerned: no crash report */
}

static LiftedFn lookup(uint32_t rva) {
    size_t lo = 0, hi = COUNT_(MODULE);
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (TABLE_(MODULE)[mid].rva < rva) lo = mid + 1; else hi = mid;
    }
    if (lo < COUNT_(MODULE) && TABLE_(MODULE)[lo].rva == rva) return TABLE_(MODULE)[lo].fn;
    return 0;
}

static void run_case(const CaseSlot *slot) {
    static Ctx ctx;
    const CaseIn *c = &slot->c;
    int sigs[] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGTRAP, SIGALRM, SIGABRT };
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_signal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++) sigaction(sigs[i], &sa, 0);
    alarm(TIMEOUT_SECONDS);

    g_case = c;
    if (!walk_pokes(slot, 1)) _exit(4);
    memset(&ctx, 0, sizeof ctx);
    memset(&g_h, 0, offsetof(Harness, calls));
    g_h.seed = c->seed;
    ctx.host = &g_h;
    uint64_t bad = 0;
    if (!map_demand(slot, &bad)) {      /* no run: the initial state cannot be the oracle's */
        g_n_demand_host = 0;
        g_h.final_ra = bad;
        emit_result(&ctx, ST_NOMAP, 0);
        return;
    }
    memcpy(ctx.r, c->r, sizeof ctx.r);
    ctx.cf = c->eflags & 1; ctx.pf = (c->eflags >> 2) & 1; ctx.af = (c->eflags >> 4) & 1;
    ctx.zf = (c->eflags >> 6) & 1; ctx.sf = (c->eflags >> 7) & 1; ctx.df = (c->eflags >> 10) & 1;
    ctx.of = (c->eflags >> 11) & 1;
    ctx.mxcsr = c->mxcsr;
    ctx.fpcw = (uint16_t)c->fpcw;
    memcpy(ctx.xmm, c->xmm, sizeof ctx.xmm);
    ctx.gs_base = c->gs_base;
    ctx.host = &g_h;

    LiftedFn fn = lookup(c->func_rva);
    if (!fn) { emit_result(&ctx, ST_NOFUNC, 0); return; }
    guest_enter(&ctx);
    fn(&ctx);
    /* Whatever the lifted code did (calls out, ldmxcsr), it must come back in the guest mode
     * of its final MXCSR. */
    if (tf_fpcr_read() != guest_fpcr(&ctx) && !g_fp_violation) g_fp_violation = 1;
    host_enter();
    uint32_t st = ST_OK;
    if (g_h.overflow) st = ST_OVERFLOW;
    else if (g_h.final_ra != g_hdr.sentinel) st = ST_BADRET;
    emit_result(&ctx, st, 0);
}

/* ---- parent ------------------------------------------------------------------------------ */

static void *map_fixed(uint64_t addr, uint64_t size) {
    uint64_t rounded = (size + 0x3FFF) & ~UINT64_C(0x3FFF);
    void *p = mmap((void *)(uintptr_t)addr, rounded, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) die("mmap");
    if ((uint64_t)(uintptr_t)p != addr) {
        fprintf(stderr, "driver: the kernel would not map 0x%llx (got %p)\n", (unsigned long long)addr, p);
        exit(2);
    }
    return p;
}

static uint8_t *read_file(const char *path, uint64_t expect) {
    FILE *f = fopen(path, "rb");
    if (!f) die(path);
    uint8_t *buf = malloc(expect ? expect : 1);
    if (!buf) die("malloc");
    if (fread(buf, 1, expect, f) != expect) { fprintf(stderr, "driver: %s is short\n", path); exit(2); }
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: driver cases.bin results.bin\n"); return 2; }
    FILE *cf = fopen(argv[1], "rb");
    if (!cf) die(argv[1]);
    if (fread(&g_hdr, sizeof g_hdr, 1, cf) != 1 || g_hdr.magic != CASES_MAGIC) {
        fprintf(stderr, "driver: bad case file\n"); return 2;
    }
    char (*image_paths)[256] = calloc(g_hdr.n_images ? g_hdr.n_images : 1, 256);
    MemEntry *mems = calloc(g_hdr.n_mems ? g_hdr.n_mems : 1, sizeof(MemEntry));
    CaseSlot *cases = calloc(g_hdr.n_cases ? g_hdr.n_cases : 1, sizeof(CaseSlot));
    if (!image_paths || !mems || !cases) die("calloc");
    if (fread(image_paths, 256, g_hdr.n_images, cf) != g_hdr.n_images) die("case file (images)");
    if (fread(mems, sizeof(MemEntry), g_hdr.n_mems, cf) != g_hdr.n_mems) die("case file (mems)");
    g_probes = calloc(g_hdr.n_probes ? g_hdr.n_probes : 1, 8);
    if (!g_probes || fread(g_probes, 8, g_hdr.n_probes, cf) != g_hdr.n_probes) die("case file (probes)");
    for (uint32_t i = 0; i < g_hdr.n_cases; i++) {
        if (fread(&cases[i].c, sizeof(CaseIn), 1, cf) != 1) die("case file (cases)");
        uint32_t n = cases[i].c.poke_bytes;
        if (n) {
            cases[i].pokes = malloc(n);
            if (!cases[i].pokes || fread(cases[i].pokes, 1, n, cf) != n) die("case file (pokes)");
        }
        n = cases[i].c.n_demand;
        if (n > MAX_DEMAND) { fprintf(stderr, "driver: case %u names %u demand pages\n", cases[i].c.case_id, n); return 2; }
        if (n) {
            cases[i].demand = malloc(8 * (size_t)n);
            if (!cases[i].demand || fread(cases[i].demand, 8, n, cf) != n) die("case file (demand pages)");
        }
    }
    fclose(cf);

    h_sentinel = g_hdr.sentinel;
    CAT(g_base_, MODULE) = g_hdr.image_base;
    uint8_t *img = map_fixed(g_hdr.image_base, g_hdr.image_size);
    uint8_t *arena = map_fixed(g_hdr.arena_base, g_hdr.arena_size);
    uint8_t *stack = map_fixed(g_hdr.stack_base, g_hdr.stack_size);
    (void)img;                                  /* reserved here; map_image puts the image file over it */
    g_changed = malloc(((g_hdr.image_size + g_hdr.arena_size + g_hdr.stack_size) / PAGE + 3) * sizeof *g_changed);
    g_img_rw = calloc(g_hdr.image_size / HOST_PAGE + 1, 1);
    g_img_written = malloc((g_hdr.image_size / HOST_PAGE + 1) * sizeof *g_img_written);
    if (!g_changed || !g_img_rw || !g_img_written) die("malloc");

    int out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) die(argv[2]);
    size_t cap = 1 << 20;
    uint8_t *buf = malloc(cap);
    uint32_t cur_mem = UINT32_MAX, cur_image = UINT32_MAX;

    for (uint32_t i = 0; i < g_hdr.n_cases; i++) {
        const CaseSlot *slot = &cases[i];
        const CaseIn *c = &slot->c;
        if (c->mem_id != cur_mem) {
            if (c->mem_id >= g_hdr.n_mems || mems[c->mem_id].image_id >= g_hdr.n_images) {
                fprintf(stderr, "driver: bad mem or image id\n"); return 2;
            }
            if (mems[c->mem_id].image_id != cur_image) {
                cur_image = mems[c->mem_id].image_id;
                map_image(image_paths[cur_image]);
            }
            free(g_pristine_mem);
            g_pristine_mem = read_file(mems[c->mem_id].path, g_hdr.arena_size + g_hdr.stack_size);
            memcpy(arena, g_pristine_mem, g_hdr.arena_size);
            memcpy(stack, g_pristine_mem + g_hdr.arena_size, g_hdr.stack_size);
            cur_mem = c->mem_id;
        }
        if (!walk_pokes(slot, 0)) {
            fprintf(stderr, "driver: case %u has a poke outside the image, arena and stack\n", c->case_id);
            return 2;
        }
        int fds[2];
        if (pipe(fds) != 0) die("pipe");
        pid_t pid = fork();
        if (pid < 0) die("fork");
        if (pid == 0) {
            close(fds[0]);
            g_out = fds[1];
            run_case(slot);
            _exit(0);
        }
        close(fds[1]);
        size_t len = 0;
        for (;;) {
            if (len == cap) { cap *= 2; buf = realloc(buf, cap); if (!buf) die("realloc"); }
            ssize_t k = read(fds[0], buf + len, cap - len);
            if (k < 0) { if (errno == EINTR) continue; die("read"); }
            if (k == 0) break;
            len += (size_t)k;
        }
        close(fds[0]);
        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }

        int clean = WIFEXITED(status) && WEXITSTATUS(status) == 0 && len >= sizeof(ResHdr);
        if (!clean) {
            ResHdr res;
            memset(&res, 0, sizeof res);
            res.case_id = c->case_id;
            int sig = WIFSIGNALED(status) ? WTERMSIG(status)
                    : (WIFEXITED(status) && WEXITSTATUS(status) > 128) ? WEXITSTATUS(status) - 128 : 0;
            res.status = sig == SIGALRM ? ST_TIMEOUT : sig ? ST_SIGNAL : ST_LOST;
            res.aux = (uint32_t)sig;
            res.final_ra = UINT64_MAX;      /* the child could not say where (on_signal) */
            memcpy(buf, &res, sizeof res);
            len = sizeof res;
        }
        uint32_t l32 = (uint32_t)len;
        write_all(out, &l32, 4);
        write_all(out, buf, len);
    }
    close(out);
    return 0;
}
