/* The VM map against a model, over random operations.
 *
 * The model is the specification written the dull way: an array with one protection per
 * guest page (0 = reserved) and the byte each committed page is filled with.  After every
 * operation the whole region is compared with it: what VirtualQuery says about every page,
 * the protection the kernel has for every host page (the union of section 5.2), and the
 * contents of every page that can be read (data where committed, zeros where not).
 * The generator is seeded, so a failure repeats; the message names the operation count.
 * Helpers: vm_util.c.
 */
#include <stdio.h>

#include "vm_util.c"

enum { MAXP = 80 };

typedef struct {
    uint64_t base;
    unsigned npages;
    RtVmType type;
    bool     live;                  /* the region exists */
    DWORD    alloc_protect;
    DWORD    prot[MAXP];            /* 0: reserved; else the committed page's protection */
    uint8_t  tag[MAXP];             /* what every byte of a committed page holds */
    bool     dirty[MAXP];           /* an uncommitted page the test wrote to through the union */
} Model;

static uint64_t rng;
static unsigned op_no;
static char     op_text[200];

static unsigned rnd(unsigned n) { return (unsigned)((vu_next(&rng) >> 11) % n); }

#define MISMATCH(...)  do { char why_[300]; snprintf(why_, sizeof why_, __VA_ARGS__); \
                        rt_test_fail(__FILE__, __LINE__, "after operation %u (%s): %s", op_no, op_text, why_); \
                        return false; } while (0)

static DWORD type_flag(RtVmType t) { return t == RT_VM_IMAGE ? MEM_IMAGE : t == RT_VM_MAPPED ? MEM_MAPPED : MEM_PRIVATE; }

static DWORD pick_protect(RtVmType type) {
    static const DWORD cow[] = { PAGE_WRITECOPY, PAGE_EXECUTE_WRITECOPY };
    if (type != RT_VM_PRIVATE && rnd(5) == 0) return cow[rnd(2)];
    switch (rnd(8)) {               /* mostly the common ones, so that data stays reachable */
    case 0: case 1: case 2: return PAGE_READWRITE;
    case 3:                 return PAGE_READONLY;
    case 4:                 return PAGE_NOACCESS;
    default:                return vu_private_protects[rnd(VU_N_PRIVATE_PROTECTS)];
    }
}

/* The access the host page that holds guest page g must have. */
static int model_union(const Model *m, unsigned g) {
    unsigned per = (unsigned)(rt_vm_host_page() / PG), g0 = g - g % per;
    int u = PROT_NONE;
    for (unsigned i = g0; i < g0 + per && i < m->npages; i++)
        if (m->prot[i]) u |= vu_access(m->prot[i]);
    return u;
}

static bool verify(const Model *m) {
    const unsigned per = (unsigned)(rt_vm_host_page() / PG);
    MEMORY_BASIC_INFORMATION q;
    if (!m->live) {
        if (rt_vm_query(m->base, &q) != sizeof q || q.State != MEM_FREE) MISMATCH("the released region is not free");
        for (unsigned g = 0; g < m->npages; g += per)
            if (vu_host_prot(m->base + g * PG) != PROT_NONE) MISMATCH("host page of released page %u is accessible", g);
        return true;
    }
    for (unsigned g = 0; g < m->npages; g++) {
        uint64_t page = m->base + g * PG;
        unsigned run = 1;
        while (g + run < m->npages && m->prot[g + run] == m->prot[g]) run++;

        memset(&q, 0xA5, sizeof q);
        if (rt_vm_query(page + rnd(PG), &q) != sizeof q) MISMATCH("query of page %u failed", g);
        if ((uint64_t)(uintptr_t)q.BaseAddress != page) MISMATCH("page %u: BaseAddress %p", g, q.BaseAddress);
        if ((uint64_t)(uintptr_t)q.AllocationBase != m->base) MISMATCH("page %u: AllocationBase %p", g, q.AllocationBase);
        if (q.AllocationProtect != m->alloc_protect) MISMATCH("page %u: AllocationProtect %#x, want %#x", g, q.AllocationProtect, m->alloc_protect);
        if (q.RegionSize != run * PG) MISMATCH("page %u: RegionSize %#llx, want %#llx", g, (unsigned long long)q.RegionSize, (unsigned long long)(run * PG));
        if (q.State != (m->prot[g] ? MEM_COMMIT : MEM_RESERVE)) MISMATCH("page %u: State %#x, protection in the model %#x", g, q.State, m->prot[g]);
        if (q.Protect != m->prot[g]) MISMATCH("page %u: Protect %#x, want %#x", g, q.Protect, m->prot[g]);
        if (q.Type != type_flag(m->type)) MISMATCH("page %u: Type %#x", g, q.Type);
        if (q.PartitionId || q.Reserved_ || q.Reserved2_) MISMATCH("page %u: a reserved field is not zero", g);

        int u = model_union(m, g);
        if (g % per == 0 && vu_host_prot(page) != u) MISMATCH("host page of page %u: protection %d, want %d", g, vu_host_prot(page), u);
        if (u & PROT_READ) {
            if (m->prot[g]) { if (!vu_all(page, PG, m->tag[g])) MISMATCH("committed page %u does not hold %#x", g, m->tag[g]); }
            else if (!m->dirty[g]) { if (!vu_all_zero(page, PG)) MISMATCH("uncommitted page %u is not zero", g); }
        }
    }
    /* The page after the last one is not this region's. */
    if (rt_vm_query(m->base + m->npages * PG, &q) != sizeof q) MISMATCH("query past the end failed");
    if ((uint64_t)(uintptr_t)q.AllocationBase == m->base) MISMATCH("the region is longer than %u pages", m->npages);
    return true;
}

/* A byte range that touches exactly pages [a, b): it starts somewhere in page a and its
 * last byte is somewhere in page b - 1. */
static void byte_range(const Model *m, unsigned a, unsigned b, uint64_t *addr, uint64_t *size) {
    uint64_t lo = m->base + a * PG + rnd(PG), hi = m->base + b * PG - rnd(PG);
    if (hi <= lo) hi = lo + 1;
    *addr = lo;
    *size = hi - lo;
}

static void page_range(const Model *m, unsigned *a, unsigned *b) {
    *a = rnd(m->npages);
    unsigned len = 1 + rnd(rnd(4) ? 6 : m->npages);
    *b = *a + len > m->npages ? m->npages : *a + len;
}

static bool create(Model *m) {
    /* Any address in the first 64 KB of the range rounds down to the base; the size is
     * chosen so that the last byte lands in the last page. */
    uint64_t off = rnd(m->npages * PG < 0x10000 ? (unsigned)(m->npages * PG) : 0x10000);
    uint64_t size = m->npages * PG - off - rnd(PG);
    if (size == 0 || size > m->npages * PG) size = 1 + (m->npages * PG - off - 1);
    bool commit = rnd(2) == 0;
    DWORD p = pick_protect(m->type);
    snprintf(op_text, sizeof op_text, "%s at +%#llx size %#llx protect %#x", commit ? "reserve and commit" : "reserve",
             (unsigned long long)off, (unsigned long long)size, p);
    uint64_t got = rt_vm_alloc(m->base + off, size, commit ? MEM_RESERVE | MEM_COMMIT : MEM_RESERVE, p, m->type);
    if (got != m->base) MISMATCH("got %#llx", (unsigned long long)got);
    m->live = true;
    m->alloc_protect = p;
    for (unsigned g = 0; g < m->npages; g++) { m->prot[g] = commit ? p : 0; m->tag[g] = 0; m->dirty[g] = false; }
    return true;
}

static bool step(Model *m) {
    unsigned a, b;
    uint64_t addr, size;
    op_no++;
    if (!m->live) return create(m);
    page_range(m, &a, &b);
    byte_range(m, a, b, &addr, &size);
    unsigned what = rnd(32);

    if (what < 10) {                                            /* commit */
        DWORD p = pick_protect(m->type);
        snprintf(op_text, sizeof op_text, "commit pages %u-%u protect %#x", a, b - 1, p);
        uint64_t got = rt_vm_alloc(addr, size, rnd(8) ? MEM_COMMIT : MEM_COMMIT | MEM_TOP_DOWN, p, m->type);
        if (got != m->base + a * PG) MISMATCH("got %#llx", (unsigned long long)got);
        for (unsigned g = a; g < b; g++) {
            if (!m->prot[g]) { m->tag[g] = 0; m->dirty[g] = false; }       /* newly committed: zeros */
            m->prot[g] = p;
        }
    } else if (what < 16) {                                     /* decommit */
        bool whole = rnd(12) == 0;
        if (whole) { a = 0; b = m->npages; addr = m->base + rnd(PG); size = 0; }
        snprintf(op_text, sizeof op_text, whole ? "decommit everything" : "decommit pages %u-%u", a, b - 1);
        rt_set_last_error(T_CTX(), VU_MARK);
        BOOL ok = rt_vm_free(addr, size, MEM_DECOMMIT);
        if (m->type != RT_VM_PRIVATE) {
            if (ok || T_LAST_ERROR() != ERROR_INVALID_PARAMETER) MISMATCH("decommit in a view or image: %d, error %u", ok, T_LAST_ERROR());
        } else {
            if (!ok) MISMATCH("failed with error %u", T_LAST_ERROR());
            /* A page that was committed is zero now if it can still be seen; one that was
             * not is left as it was. */
            for (unsigned g = a; g < b; g++) if (m->prot[g]) { m->prot[g] = 0; m->dirty[g] = false; }
        }
    } else if (what < 23) {                                     /* protect */
        DWORD p = pick_protect(m->type), old = 0x5A5A5A5A;
        bool committed = true;
        for (unsigned g = a; g < b; g++) if (!m->prot[g]) committed = false;
        snprintf(op_text, sizeof op_text, "protect pages %u-%u %#x", a, b - 1, p);
        rt_set_last_error(T_CTX(), VU_MARK);
        BOOL ok = rt_vm_protect(addr, size, p, &old);
        if (m->type != RT_VM_PRIVATE && (p & (PAGE_NOCACHE | PAGE_WRITECOMBINE))) {
            /* Not a protection an image or a view can be given, whatever state its pages are in. */
            if (ok || T_LAST_ERROR() != ERROR_INVALID_PARAMETER) MISMATCH("no-cache or write-combine on a view or image: %d, error %u", ok, T_LAST_ERROR());
            if (old != 0x5A5A5A5A) MISMATCH("a failed call wrote the old protection");
        } else if (committed) {
            if (!ok) MISMATCH("failed with error %u", T_LAST_ERROR());
            if (old != m->prot[a]) MISMATCH("old protection %#x, want %#x", old, m->prot[a]);
            for (unsigned g = a; g < b; g++) m->prot[g] = p;
        } else {
            if (ok || T_LAST_ERROR() != ERROR_INVALID_ADDRESS) MISMATCH("a range with an uncommitted page: %d, error %u", ok, T_LAST_ERROR());
            if (old != 0x5A5A5A5A) MISMATCH("a failed call wrote the old protection");
        }
    } else if (what < 29) {                                     /* the guest writes to pages it may write */
        snprintf(op_text, sizeof op_text, "write pages %u-%u", a, b - 1);
        for (unsigned g = a; g < b; g++) {
            if (!m->prot[g] || !(vu_access(m->prot[g]) & PROT_WRITE)) continue;
            m->tag[g] = (uint8_t)(1 + rnd(255));
            vu_fill(m->base + g * PG, PG, m->tag[g]);
        }
    } else if (what < 31) {                                     /* ...and to pages it may not, but can */
        snprintf(op_text, sizeof op_text, "scribble on uncommitted pages in %u-%u", a, b - 1);
        for (unsigned g = a; g < b; g++) {
            if (m->prot[g] || !(model_union(m, g) & PROT_WRITE)) continue;
            vu_fill(m->base + g * PG, PG, (uint8_t)(1 + rnd(255)));
            m->dirty[g] = true;
        }
    } else if (rnd(4) == 0) {                                   /* release; the next step makes it again */
        snprintf(op_text, sizeof op_text, "release");
        /* A private region goes back through VirtualFree, at any address of its first page;
         * an image or a view only through rt_vm_release, at its base, and VirtualFree
         * refuses it and changes nothing (rt/vm.h). */
        uint64_t off = rnd(PG);
        if (m->type == RT_VM_PRIVATE) {
            if (!rt_vm_free(m->base + off, 0, MEM_RELEASE)) MISMATCH("failed with error %u", T_LAST_ERROR());
        } else {
            rt_set_last_error(T_CTX(), VU_MARK);
            if (rt_vm_free(m->base + off, 0, MEM_RELEASE) || T_LAST_ERROR() != ERROR_INVALID_PARAMETER)
                MISMATCH("VirtualFree(MEM_RELEASE) was not refused with ERROR_INVALID_PARAMETER (last error %u)",
                         T_LAST_ERROR());
            if (!verify(m)) return false;
            if (!rt_vm_release(m->base, m->type)) MISMATCH("rt_vm_release failed with error %u", T_LAST_ERROR());
        }
        m->live = false;
    } else {                                                    /* MEM_RESET changes nothing */
        snprintf(op_text, sizeof op_text, "reset pages %u-%u", a, b - 1);
        if (m->type == RT_VM_PRIVATE && rt_vm_alloc(addr, size, MEM_RESET, PAGE_NOACCESS, m->type) != m->base + a * PG)
            MISMATCH("failed with error %u", T_LAST_ERROR());
    }
    return true;
}

static void run(uint64_t seed, uint64_t base, unsigned npages, RtVmType type, unsigned ops) {
    static Model m;
    memset(&m, 0, sizeof m);
    m.base = base;
    m.npages = npages;
    m.type = type;
    rng = seed;
    op_no = 0;
    for (unsigned i = 0; i < ops; i++) {
        if (!step(&m) || !verify(&m)) return;
    }
    T_TRUE(op_no == ops);
    if (m.live) T_TRUE(rt_vm_release(m.base, m.type));
}

TEST(core, vm_model_private)
{
    /* 37 pages: a region that ends in the middle of a host page. */
    run(0x9E3779B97F4A7C15, PLAY(0), 37, RT_VM_PRIVATE, 20000);
}

TEST(core, vm_model_private_whole_granules)
{
    run(0xD1B54A32D192ED03, PLAY(0), 64, RT_VM_PRIVATE, 20000);
}

TEST(core, vm_model_private_small)
{
    /* One page, two pages, five pages: every range is at an edge. */
    run(0x2545F4914F6CDD1D, PLAY(0), 1, RT_VM_PRIVATE, 3000);
    run(0x2545F4914F6CDD1E, PLAY(1), 2, RT_VM_PRIVATE, 3000);
    run(0x2545F4914F6CDD1F, PLAY(2), 5, RT_VM_PRIVATE, 6000);
}

TEST(core, vm_model_image_and_view)
{
    /* Copy-on-write protections are in play; decommit is refused. */
    run(0xA0761D6478BD642F, RT_IMAGE_ARENA_BASE + (UINT64_C(3) << 30), 23, RT_VM_IMAGE, 10000);
    run(0xE7037ED1A0B428DB, PLAY(0), 23, RT_VM_MAPPED, 10000);
}

TEST(core, vm_model_neighbours)
{
    /* Two regions back to back, the first exactly 64 KB.  Random operations on either; a
     * range that reaches from one into the other must fail and change neither. */
    static Model m[2];
    memset(m, 0, sizeof m);
    m[0].base = PLAY(0);          m[0].npages = 16; m[0].type = RT_VM_PRIVATE;
    m[1].base = PLAY(0) + 0x10000; m[1].npages = 21; m[1].type = RT_VM_PRIVATE;
    rng = 0x8EBC6AF09C88C6E3;
    op_no = 0;
    for (unsigned i = 0; i < 20000; i++) {
        if (rnd(8) == 0) {
            /* From the last pages of the first into the first pages of the second. */
            op_no++;
            uint64_t addr = m[1].base - 1 - rnd(3 * PG), size = (m[1].base - addr) + 1 + rnd(3 * PG);
            DWORD old = 0x5A5A5A5A;
            unsigned which = rnd(4);
            snprintf(op_text, sizeof op_text, "call %u across the boundary: %#llx size %#llx", which,
                     (unsigned long long)addr, (unsigned long long)size);
            rt_set_last_error(T_CTX(), VU_MARK);
            bool ok;
            DWORD want;
            switch (which) {
            case 0:  ok = rt_vm_alloc(addr, size, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) != 0; want = ERROR_INVALID_ADDRESS; break;
            case 1:  ok = rt_vm_protect(addr, size, PAGE_READWRITE, &old) != FALSE; want = ERROR_INVALID_ADDRESS; break;
            case 2:  ok = rt_vm_alloc(addr, size, MEM_RESET, PAGE_NOACCESS, RT_VM_PRIVATE) != 0; want = ERROR_INVALID_ADDRESS; break;
            default: ok = rt_vm_free(addr, size, MEM_DECOMMIT) != FALSE;
                     /* It starts in free space when the first region is released. */
                     want = m[0].live ? ERROR_INVALID_PARAMETER : ERROR_INVALID_ADDRESS; break;
            }
            if (ok || T_LAST_ERROR() != want) {
                rt_test_fail(__FILE__, __LINE__, "operation %u (%s): result %d, error %u, want error %u", op_no, op_text,
                             ok, T_LAST_ERROR(), want);
                return;
            }
        } else if (!step(&m[rnd(2)])) {
            return;
        }
        if (!verify(&m[0]) || !verify(&m[1])) return;
    }
    T_TRUE(op_no == 20000);
}
