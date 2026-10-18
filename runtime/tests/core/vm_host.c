/* The host pages under the guest pages (docs/RUNTIME.md section 5.2): a host page is
 * accessible with the union of the committed guest pages in it; commit yields zeros;
 * decommit of part of a host page zeroes at once, of a whole host page drops it;
 * PAGE_GUARD and PAGE_NOACCESS are exact on whole host pages and otherwise recorded and
 * logged once.
 *
 * The host page size is asked, not assumed.  Tests that need several guest pages in one
 * host page do nothing on a host with 4 KB pages, where no guest page has a neighbour.
 * Helpers and the PLAY addresses: vm_util.c.
 */
#include <stdio.h>

#include "vm_util.c"

#define H   rt_vm_host_page()
#define K   (H / PG)                    /* guest pages per host page: 4 on Apple silicon */

#define RW  (PROT_READ | PROT_WRITE)

/* True if this host has several guest pages per host page; otherwise makes the one check a
 * test must make and lets it end. */
static bool shared_host_pages(void) {
    if (K >= 2) return true;
    T_EQ(K, 1);
    return false;
}

TEST(core, vm_host_page_size)
{
    /* The map works for any host page that is a power of two between the guest page and
     * the allocation granularity; a region base is always host page aligned. */
    T_TRUE(H >= PG && (H & (H - 1)) == 0 && 0x10000 % H == 0);
    T_EQ(H, (uint64_t)sysconf(_SC_PAGESIZE));
    uint64_t r = rt_vm_alloc(0, 1, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    T_EQ(r % H, 0);
}

TEST(core, vm_host_union)
{
    if (!shared_host_pages()) return;
    const uint64_t r = PLAY(0);
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, 4 * H, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);

    /* Nothing committed: no access, no memory. */
    for (unsigned i = 0; i < 4; i++) T_EQ(vu_host_prot(r + i * H), PROT_NONE);
    T_TRUE(!vu_reads(r) && !vu_resident(r));

    /* One guest page committed read-write makes its host page read-write: the reserved
     * neighbours in it read as zero and do not fault.  The next host page is untouched.
     * Committing costs no memory until a page is touched. */
    T_REQUIRE(rt_vm_alloc(r + PG, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r + PG);
    T_EQ(vu_host_prot(r), RW);
    T_TRUE(!vu_resident(r));
    T_EQ(vu_host_prot(r + H), PROT_NONE);
    T_TRUE(vu_reads(r) && vu_writes(r + H - 1) && !vu_reads(r + H));
    T_TRUE(vu_all_zero(r, H));
    Q_RESERVED(r, r, PAGE_NOACCESS, PG);                        /* the map still knows what is what */
    Q_COMMITTED(r + PG, r, PAGE_NOACCESS, PG, PAGE_READWRITE);

    /* Read-only alone: the host page is read-only and a write faults. */
    const uint64_t b = r + H;
    T_REQUIRE(rt_vm_alloc(b, PG, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE) == b);
    T_EQ(vu_host_prot(b), PROT_READ);
    T_TRUE(vu_reads(b) && !vu_writes(b));
    /* A read-write neighbour: the union is read-write, so a write to the read-only page
     * no longer faults.  That is the cost section 5.2 accepts; the record stays exact. */
    T_REQUIRE(rt_vm_alloc(b + PG, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == b + PG);
    T_EQ(vu_host_prot(b), RW);
    T_TRUE(vu_writes(b));
    Q_COMMITTED(b, r, PAGE_NOACCESS, PG, PAGE_READONLY);
    /* The neighbour goes read-only too: the union shrinks back. */
    T_REQUIRE(rt_vm_protect(b + PG, PG, PAGE_READONLY, &old));
    T_EQ(vu_host_prot(b), PROT_READ);
    T_TRUE(!vu_writes(b) && !vu_writes(b + PG));
    /* No-access next to read-only: the union is read. */
    T_REQUIRE(rt_vm_protect(b + PG, PG, PAGE_NOACCESS, &old));
    T_EQ(vu_host_prot(b), PROT_READ);
    /* Every committed page of the host page no-access: nothing, exactly as on Windows. */
    T_REQUIRE(rt_vm_protect(b, PG, PAGE_NOACCESS, &old));
    T_EQ(vu_host_prot(b), PROT_NONE);
    T_TRUE(!vu_reads(b) && !vu_reads(b + PG));
    /* Execute is read; execute-read-write is read-write. */
    T_REQUIRE(rt_vm_protect(b, PG, PAGE_EXECUTE, &old));
    T_EQ(vu_host_prot(b), PROT_READ);
    T_REQUIRE(rt_vm_protect(b, PG, PAGE_EXECUTE_READ, &old));
    T_EQ(vu_host_prot(b), PROT_READ);
    T_REQUIRE(rt_vm_protect(b + PG, PG, PAGE_EXECUTE_READWRITE, &old));
    T_EQ(vu_host_prot(b), RW);
    /* Decommit takes a page out of the union. */
    T_REQUIRE(rt_vm_free(b + PG, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(b), PROT_READ);
    T_REQUIRE(rt_vm_free(b, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(b), PROT_NONE);

    /* The union is per host page: the last guest page of one host page and the first of
     * the next do not open each other's neighbours' host pages. */
    const uint64_t c = r + 2 * H;
    T_REQUIRE(rt_vm_alloc(c + H - PG, 2 * PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == c + H - PG);
    T_EQ(vu_host_prot(c), RW);
    T_EQ(vu_host_prot(c + H), RW);
    T_EQ(vu_host_prot(c - H), PROT_NONE);
    T_REQUIRE(rt_vm_protect(c + H - PG, PG, PAGE_READONLY, &old));
    T_EQ(vu_host_prot(c), PROT_READ);
    T_EQ(vu_host_prot(c + H), RW);
    T_REQUIRE(rt_vm_free(c + H, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(c), PROT_READ);
    T_EQ(vu_host_prot(c + H), PROT_NONE);
}

TEST(core, vm_host_whole_pages_are_exact)
{
    /* On whole host pages every protection is exact: the host faults where Windows would. */
    const uint64_t r = PLAY(0);
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, 4 * H, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    vu_fill(r, 4 * H, 0xB6);

    T_REQUIRE(rt_vm_protect(r, H, PAGE_READONLY, &old));
    T_TRUE(vu_reads(r) && !vu_writes(r) && vu_reads(r + H - 1) && !vu_writes(r + H - 1));
    T_TRUE(vu_writes(r + H));                                   /* the next host page is not affected */
    T_REQUIRE(rt_vm_protect(r + H, H, PAGE_NOACCESS, &old));
    T_TRUE(!vu_reads(r + H) && !vu_reads(r + 2 * H - 1) && vu_writes(r + 2 * H));
    T_REQUIRE(rt_vm_protect(r + 2 * H, H, PAGE_READWRITE | PAGE_GUARD, &old));
    T_TRUE(!vu_reads(r + 2 * H) && !vu_reads(r + 3 * H - 1) && vu_writes(r + 3 * H));
    T_REQUIRE(rt_vm_protect(r + 3 * H, H, PAGE_EXECUTE_READ, &old));
    T_TRUE(vu_reads(r + 3 * H) && !vu_writes(r + 3 * H));
    /* None of that is the inexact case, so nothing was logged about it. */
    T_TRUE(!T_LOGGED(RT_CH_VM, "covers only part"));

    /* The contents are still there when the protection is lifted. */
    T_REQUIRE(rt_vm_protect(r, 4 * H, PAGE_READWRITE, &old));
    T_EQ(old, PAGE_READONLY);
    T_TRUE(vu_all(r, 4 * H, 0xB6));

    /* A guest stack (section 5.3) relies on this: a 64 KB guard below the stack that is
     * never accessible. */
    const uint64_t s = PLAY(1);
    T_REQUIRE(rt_vm_alloc(s, 0x10000 + 0x100000, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == s);
    T_REQUIRE(rt_vm_alloc(s + 0x10000, 0x100000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == s + 0x10000);
    T_TRUE(!vu_reads(s) && !vu_reads(s + 0xFFFF) && vu_writes(s + 0x10000) && vu_writes(s + 0x10FFFF));
    T_TRUE(!vu_reads(s + 0x110000));                            /* and free space above it */
}

/* The line the map logs for an inexact protection, for the host page at `at`. */
static const char *inexact_line(const char *what, uint64_t at) {
    static char buf[160];
    snprintf(buf, sizeof buf, "%s covers only part of the %llu KB host page at %#llx,", what,
             (unsigned long long)(H >> 10), (unsigned long long)at);
    return buf;
}

TEST(core, vm_host_inexact_is_logged_once)
{
    if (!shared_host_pages()) return;
    const uint64_t r = PLAY(0);
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, 8 * H, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    T_TRUE(!T_LOGGED(RT_CH_VM, "covers only part"));

    /* PAGE_NOACCESS on one guest page of a host page whose other pages stay read-write:
     * recorded, reported by VirtualQuery, not enforced, and said so in the log. */
    T_REQUIRE(rt_vm_protect(r + PG, PG, PAGE_NOACCESS, &old));
    Q_COMMITTED(r + PG, r, PAGE_READWRITE, PG, PAGE_NOACCESS);
    T_TRUE(vu_writes(r + PG));
    T_TRUE(T_LOGGED(RT_CH_VM, inexact_line("PAGE_NOACCESS", r)));
    T_TRUE(!T_LOGGED(RT_CH_VM, "PAGE_GUARD covers only part"));
    /* Once: the second and third time, somewhere else, by another route, nothing more. */
    T_REQUIRE(rt_vm_protect(r + H, PG, PAGE_NOACCESS, &old));
    T_REQUIRE(rt_vm_alloc(r + 2 * H, PG, MEM_COMMIT, PAGE_NOACCESS, RT_VM_PRIVATE));
    T_TRUE(!T_LOGGED(RT_CH_VM, inexact_line("PAGE_NOACCESS", r + H)));
    T_TRUE(!T_LOGGED(RT_CH_VM, inexact_line("PAGE_NOACCESS", r + 2 * H)));

    /* PAGE_GUARD the same, with its own line. */
    T_REQUIRE(rt_vm_protect(r + 3 * H + PG, PG, PAGE_READWRITE | PAGE_GUARD, &old));
    Q_COMMITTED(r + 3 * H + PG, r, PAGE_READWRITE, PG, PAGE_READWRITE | PAGE_GUARD);
    T_TRUE(vu_writes(r + 3 * H + PG));
    T_TRUE(T_LOGGED(RT_CH_VM, inexact_line("PAGE_GUARD", r + 3 * H)));
    T_REQUIRE(rt_vm_protect(r + 4 * H, PG, PAGE_READONLY | PAGE_GUARD, &old));
    T_TRUE(!T_LOGGED(RT_CH_VM, inexact_line("PAGE_GUARD", r + 4 * H)));
    /* The guard is still recorded after an access went through: no handler clears it. */
    Q_COMMITTED(r + 3 * H + PG, r, PAGE_READWRITE, PG, PAGE_READWRITE | PAGE_GUARD);
}

TEST(core, vm_host_inexact_by_commit)
{
    if (!shared_host_pages()) return;
    /* The inexact case can arise without VirtualProtect: a no-access page is committed
     * first (exact so far), then a neighbour is committed read-write. */
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 2 * H, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    T_REQUIRE(rt_vm_alloc(r, PG, MEM_COMMIT, PAGE_READWRITE | PAGE_GUARD, RT_VM_PRIVATE) == r);
    T_REQUIRE(rt_vm_alloc(r + H, PG, MEM_COMMIT, PAGE_NOACCESS, RT_VM_PRIVATE) == r + H);
    T_TRUE(!vu_reads(r) && !vu_reads(r + H));
    T_TRUE(!T_LOGGED(RT_CH_VM, "covers only part"));
    T_REQUIRE(rt_vm_alloc(r + PG, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r + PG);
    T_TRUE(T_LOGGED(RT_CH_VM, inexact_line("PAGE_GUARD", r)));
    T_TRUE(!T_LOGGED(RT_CH_VM, "PAGE_NOACCESS covers only part"));
    T_REQUIRE(rt_vm_alloc(r + H + PG, PG, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE) == r + H + PG);
    T_TRUE(T_LOGGED(RT_CH_VM, inexact_line("PAGE_NOACCESS", r + H)));
    T_TRUE(vu_reads(r) && vu_reads(r + H));
}

TEST(core, vm_host_decommit_zeroes_or_drops)
{
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 8 * H, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    vu_fill(r, 8 * H, 0xD7);
    for (unsigned i = 0; i < 8; i++) T_TRUE(vu_resident(r + i * H));

    /* A whole host page: dropped.  No access, no memory, zeros when committed again. */
    T_REQUIRE(rt_vm_free(r + H, H, MEM_DECOMMIT));
    T_EQ(vu_host_prot(r + H), PROT_NONE);
    T_TRUE(!vu_resident(r + H) && !vu_reads(r + H));
    T_TRUE(vu_resident(r) && vu_resident(r + 2 * H));
    T_TRUE(vu_all(r, H, 0xD7) && vu_all(r + 2 * H, 6 * H, 0xD7));
    T_REQUIRE(rt_vm_alloc(r + H, H, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE));
    T_TRUE(vu_all_zero(r + H, H));

    if (!shared_host_pages()) return;

    /* Part of a host page: those 4 KB are zero AT ONCE (they can still be read through
     * the union), the rest of the host page keeps its data and its memory. */
    T_REQUIRE(rt_vm_free(r + PG, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(r), RW);
    T_TRUE(vu_resident(r));
    T_TRUE(vu_all(r, PG, 0xD7) && vu_all_zero(r + PG, PG) && vu_all(r + 2 * PG, H - 2 * PG, 0xD7));
    /* The other guest pages one by one: the host page is dropped when the last one goes. */
    for (uint64_t g = 0; g < K; g++) {
        if (g == 1) continue;
        T_TRUE(vu_resident(r));
        T_REQUIRE(rt_vm_free(r + g * PG, PG, MEM_DECOMMIT));
    }
    T_EQ(vu_host_prot(r), PROT_NONE);
    T_TRUE(!vu_resident(r));

    /* One decommit with a partial host page at each end and whole ones between. */
    const uint64_t lo = r + 2 * H + PG, hi = r + 6 * H - PG;
    T_REQUIRE(rt_vm_free(lo, hi - lo, MEM_DECOMMIT));
    T_TRUE(vu_all(r + 2 * H, PG, 0xD7) && vu_all_zero(r + 2 * H + PG, H - PG));     /* head: first page stays */
    T_EQ(vu_host_prot(r + 2 * H), RW);
    for (unsigned i = 3; i < 5; i++) {                                              /* middle: dropped */
        T_EQ(vu_host_prot(r + i * H), PROT_NONE);
        T_TRUE(!vu_resident(r + i * H));
    }
    T_TRUE(vu_all_zero(r + 5 * H, H - PG) && vu_all(r + 6 * H - PG, PG, 0xD7));     /* tail: last page stays */
    T_EQ(vu_host_prot(r + 5 * H), RW);
    T_TRUE(vu_all(r + 6 * H, 2 * H, 0xD7));
    Q_COMMITTED(r + 2 * H, r, PAGE_READWRITE, PG, PAGE_READWRITE);
    Q_RESERVED(lo, r, PAGE_READWRITE, hi - lo);
    Q_COMMITTED(hi, r, PAGE_READWRITE, PG + 2 * H, PAGE_READWRITE);
    /* All of it committed again: zeros where it was decommitted, data where it was not. */
    T_REQUIRE(rt_vm_alloc(lo, hi - lo, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == lo);
    T_TRUE(vu_all(r + 2 * H, PG, 0xD7) && vu_all_zero(lo, hi - lo) && vu_all(hi, PG + 2 * H, 0xD7));
}

TEST(core, vm_host_decommit_under_protection)
{
    if (!shared_host_pages()) return;
    DWORD old;

    /* A read-only host page: the decommitted page is still zeroed, and the host page is
     * read-only again afterwards. */
    const uint64_t r = PLAY(0);
    T_REQUIRE(rt_vm_alloc(r, 2 * H, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    vu_fill(r, 2 * H, 0xE1);
    T_REQUIRE(rt_vm_protect(r, 2 * H, PAGE_READONLY, &old));
    T_REQUIRE(rt_vm_free(r + PG, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(r), PROT_READ);
    T_TRUE(!vu_writes(r));
    T_TRUE(vu_all(r, PG, 0xE1) && vu_all_zero(r + PG, PG) && vu_all(r + 2 * PG, 2 * H - 2 * PG, 0xE1));

    /* A host page whose pages are all no-access: after one is decommitted the rest still
     * has no access and still has its data; the decommitted one comes back as zeros. */
    const uint64_t n = PLAY(1);
    T_REQUIRE(rt_vm_alloc(n, H, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == n);
    vu_fill(n, H, 0xE2);
    T_REQUIRE(rt_vm_protect(n, H, PAGE_NOACCESS, &old));
    T_REQUIRE(rt_vm_free(n, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(n), PROT_NONE);
    T_TRUE(vu_resident(n));                                     /* not dropped: three pages are still committed */
    T_REQUIRE(rt_vm_alloc(n, PG, MEM_COMMIT, PAGE_NOACCESS, RT_VM_PRIVATE) == n);
    T_EQ(vu_host_prot(n), PROT_NONE);
    T_REQUIRE(rt_vm_protect(n, H, PAGE_READWRITE, &old));
    T_EQ(old, PAGE_NOACCESS);
    T_TRUE(vu_all_zero(n, PG) && vu_all(n + PG, H - PG, 0xE2));

    /* Guard pages with a read-write neighbour: decommit of the neighbour leaves a host
     * page with no access that keeps the guard pages' data. */
    const uint64_t g = PLAY(2);
    T_REQUIRE(rt_vm_alloc(g, H, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == g);
    vu_fill(g, H, 0xE3);
    T_REQUIRE(rt_vm_protect(g + PG, H - PG, PAGE_READWRITE | PAGE_GUARD, &old));
    T_EQ(vu_host_prot(g), RW);
    T_REQUIRE(rt_vm_free(g, PG, MEM_DECOMMIT));
    T_EQ(vu_host_prot(g), PROT_NONE);
    T_TRUE(!vu_reads(g) && !vu_reads(g + PG));
    T_REQUIRE(rt_vm_protect(g + PG, H - PG, PAGE_READWRITE, &old));
    T_EQ(old, PAGE_READWRITE | PAGE_GUARD);
    T_TRUE(vu_all_zero(g, PG) && vu_all(g + PG, H - PG, 0xE3));
}

TEST(core, vm_host_commit_yields_zeros)
{
    if (!shared_host_pages()) return;
    /* The guest can write into a reserved page through the union (on Windows that write
     * would fault).  A commit of that page still yields zeros. */
    const uint64_t r = PLAY(0);
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, 2 * H, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    T_REQUIRE(rt_vm_alloc(r, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    vu_fill(r, H, 0x66);                                        /* page 0 is ours; the rest is scribbling */
    T_REQUIRE(rt_vm_alloc(r + PG, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r + PG);
    T_TRUE(vu_all(r, PG, 0x66) && vu_all_zero(r + PG, PG));
    /* The rest of the host page and the whole of the next, in one commit, read-only. */
    T_REQUIRE(rt_vm_alloc(r + 2 * PG, 2 * H - 2 * PG, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE) == r + 2 * PG);
    T_TRUE(vu_all_zero(r + PG, 2 * H - PG));
    T_TRUE(vu_all(r, PG, 0x66));
    T_EQ(vu_host_prot(r), RW);
    T_EQ(vu_host_prot(r + H), PROT_READ);

    /* The same when the page that keeps the host page open is read-only (the scribbling
     * went in before it was): the commit has to write zeros into a read-only host page. */
    const uint64_t s = PLAY(1);
    T_REQUIRE(rt_vm_alloc(s, H, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == s);
    T_REQUIRE(rt_vm_alloc(s, PG, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == s);
    vu_fill(s, H, 0x67);
    T_REQUIRE(rt_vm_protect(s, PG, PAGE_READONLY, &old));
    T_EQ(vu_host_prot(s), PROT_READ);
    T_TRUE(vu_all(s + PG, PG, 0x67));                           /* the scribble is visible until the commit */
    T_REQUIRE(rt_vm_alloc(s + PG, PG, MEM_COMMIT, PAGE_READONLY, RT_VM_PRIVATE) == s + PG);
    T_EQ(vu_host_prot(s), PROT_READ);
    T_TRUE(vu_all(s, PG, 0x67) && vu_all_zero(s + PG, PG));
    /* Committed no-access next to it: zero as well, once it can be looked at. */
    T_REQUIRE(rt_vm_alloc(s + 2 * PG, PG, MEM_COMMIT, PAGE_NOACCESS, RT_VM_PRIVATE) == s + 2 * PG);
    T_REQUIRE(rt_vm_protect(s + 2 * PG, PG, PAGE_READONLY, &old));
    T_EQ(old, PAGE_NOACCESS);
    T_TRUE(vu_all_zero(s + 2 * PG, PG));
}

TEST(core, vm_host_many_pages)
{
    /* Calls that cover many host pages with different needs: neighbours that must end up
     * with different host protections, runs to drop next to pages to keep. */
    enum { N = 64 };
    const uint64_t r = PLAY(0);
    DWORD old;
    T_REQUIRE(rt_vm_alloc(r, N * H, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE) == r);
    for (unsigned i = 0; i < N; i++) {
        DWORD p = i % 3 == 0 ? PAGE_READWRITE : i % 3 == 1 ? PAGE_READONLY : PAGE_NOACCESS;
        T_REQUIRE(rt_vm_alloc(r + i * H, H, MEM_COMMIT, p, RT_VM_PRIVATE) == r + i * H);
    }
    for (unsigned i = 0; i < N; i++) T_EQ(vu_host_prot(r + i * H), i % 3 == 0 ? RW : i % 3 == 1 ? PROT_READ : PROT_NONE);
    T_REQUIRE(rt_vm_protect(r, N * H, PAGE_READWRITE, &old));
    T_EQ(old, PAGE_READWRITE);
    for (unsigned i = 0; i < N; i++) T_EQ(vu_host_prot(r + i * H), RW);
    T_TRUE(vu_all_zero(r, N * H));
    for (unsigned i = 0; i < N; i++) vu_fill(r + i * H, H, (uint8_t)(i + 1));

    if (!shared_host_pages()) return;

    /* Decommit every other host page except its first guest page: those host pages stay,
     * zeroed apart from that page. */
    for (unsigned i = 0; i < N; i += 2) T_REQUIRE(rt_vm_free(r + i * H + PG, H - PG, MEM_DECOMMIT));
    for (unsigned i = 0; i < N; i++) {
        T_EQ(vu_host_prot(r + i * H), RW);
        if (i % 2 == 0) T_TRUE(vu_all(r + i * H, PG, (uint8_t)(i + 1)) && vu_all_zero(r + i * H + PG, H - PG));
        else T_TRUE(vu_all(r + i * H, H, (uint8_t)(i + 1)));
    }
    /* Then everything from the middle of host page 1 to the middle of the last one. */
    T_REQUIRE(rt_vm_free(r + H + H / 2, (N - 2) * H, MEM_DECOMMIT));
    T_EQ(vu_host_prot(r), RW);
    T_TRUE(vu_all(r, PG, 1) && vu_all_zero(r + PG, H - PG));
    T_EQ(vu_host_prot(r + H), RW);
    T_TRUE(vu_all(r + H, H / 2, 2) && vu_all_zero(r + H + H / 2, H / 2));
    for (unsigned i = 2; i < N - 1; i++) {
        T_EQ(vu_host_prot(r + i * H), PROT_NONE);
        T_TRUE(!vu_resident(r + i * H));
    }
    T_EQ(vu_host_prot(r + (N - 1) * H), RW);
    T_TRUE(vu_all_zero(r + (N - 1) * H, H / 2));
    T_TRUE(vu_all(r + (N - 1) * H + H / 2, H / 2, N));

    /* One commit over the lot: zeros everywhere it had been decommitted. */
    T_REQUIRE(rt_vm_alloc(r, N * H, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == r);
    for (unsigned i = 0; i < N; i++) T_EQ(vu_host_prot(r + i * H), RW);
    T_TRUE(vu_all(r, PG, 1) && vu_all(r + H, H / 2, 2));
    T_TRUE(vu_all_zero(r + H + H / 2, (N - 2) * H));
    T_TRUE(vu_all(r + (N - 1) * H + H / 2, H / 2, N));
    T_REQUIRE(rt_vm_free(r, 0, MEM_RELEASE));
    for (unsigned i = 0; i < N; i++) T_TRUE(vu_host_prot(r + i * H) == PROT_NONE && !vu_resident(r + i * H));
}
