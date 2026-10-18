/* The loader: static TLS and the loader's per-thread record (runtime/core/loader_tls.c;
 * specification: loader.h under "static TLS and threads", rt_loader_in_entry in rt/module.h).
 * Plan: docs/handoff/rt-contract-loader.md, task 4.
 *
 * Two tests.  loader_tls is single-threaded: cases 1 to 9 and 12, then what is not in the
 * plan (13 to 15: the save and restore of the entry mark, the line in the fatal report, the
 * misuse stops).  loader_tls_threads runs cases 11 and 10, in that order, for real: the plan
 * kept them under `#if 0` for want of a second guest thread, and rt_thread_attach and
 * rt_thread_create exist now.  Case 11 comes first because it needs a thread that is
 * recorded BEFORE a module's TLS is loaded.  Every wait on another thread is bounded
 * (EVENTUALLY: 30 s). */
#include <pthread.h>

#include "wait_util.c"
#include "rt/test_pe.h"
#include "rt/heap.h"
#include "rt/win/teb.h"
#include "../../core/loader.h"
#include "../../core/report.h"

#define F(i) RT_TEST_LIFT_RVA(i)
#define CORPUS_REL "bin/x64_retail/corpus_O1.dll"

/* Slot a: a 16-byte template 1..16, 32 bytes of zero fill, two callbacks.  Slot b: an 8-byte
 * template, no zero fill, no callbacks.  Slot c: no TLS directory. */
static void write_slots(void) {
    static const uint8_t tpl_a[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static const uint8_t tpl_b[8] = { 0xB1, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6, 0xB7, 0xB8 };
    RtTestPe *a = rt_test_pe_new();
    rt_test_pe_put(a, 0x3100, tpl_a, 16);
    rt_test_pe_put_va(a, 0x3200, F(4));
    rt_test_pe_put_va(a, 0x3208, F(5));
    rt_test_pe_tls(a, 0x3100, 16, 32, 0x2080, 0x3200);
    rt_test_pe_write(a, "t_lift_a.dll");
    RtTestPe *b = rt_test_pe_new();
    rt_test_pe_put(b, 0x3100, tpl_b, 8);
    rt_test_pe_tls(b, 0x3100, 8, 0, 0x2080, 0);
    rt_test_pe_write(b, "t_lift_b.dll");
    rt_test_pe_write(rt_test_pe_new(), "t_lift_c.dll");
    rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL);
}

/* Slot `index` of a thread's static TLS array, read as guest code reads it: [gs:0x58][index]. */
static uint64_t tls_slot(const Ctx *ctx, unsigned index) {
    return LD64(LD64(ctx->gs_base + RT_TEB_TLS_POINTER) + 8 * index);
}

static bool is_template_a(uint64_t block) {
    for (unsigned i = 0; i < 16; i++) if (LD8(block + i) != i + 1) return false;
    for (unsigned i = 16; i < 48; i++) if (LD8(block + i) != 0) return false;
    return true;
}

static bool is_template_b(uint64_t block) {
    for (unsigned i = 0; i < 8; i++) if (LD8(block + i) != 0xB1 + i) return false;
    return true;
}

/* ---- the callbacks of slot a ------------------------------------------------------------------ */

static struct { unsigned n; unsigned which[4]; uint64_t rcx[4], rdx[4], r8[4]; bool inside[4]; const RtModule *m[4]; DWORD reason[4]; } cb;
static char report_line[512];

static void callback(Ctx *ctx, unsigned which) {
    unsigned i = cb.n++;
    if (i >= 4) return;
    cb.which[i] = which;
    cb.rcx[i] = ctx->r[1];
    cb.rdx[i] = ctx->r[2];
    cb.r8[i] = ctx->r[8];
    cb.inside[i] = rt_loader_in_entry(RT_THREAD(ctx), &cb.m[i], &cb.reason[i]);
    if (which == 4) rt_report_entry(report_line, sizeof report_line, 0, RT_THREAD(ctx));
}
static void cb4(Ctx *ctx) { callback(ctx, 4); GUEST_RET(ctx); }
static void cb5(Ctx *ctx) { callback(ctx, 5); GUEST_RET(ctx); }
static void cb_fatal(Ctx *ctx) { (void)ctx; rt_fatal("boom in a TLS callback"); }

TEST_SETUP(core, loader_tls) { write_slots(); }
TEST(core, loader_tls)
{
    Ctx *ctx = T_CTX();
    RtThread *self = RT_THREAD(ctx);
    RtModule *A = rt_module_find("t_lift_a.dll"), *B = rt_module_find("t_lift_b.dll"), *C = rt_module_find("t_lift_c.dll");
    T_REQUIRE(A != NULL && B != NULL && C != NULL);
    RtLdrModule *LA = rt_ldr_module(A), *LB = rt_ldr_module(B), *LC = rt_ldr_module(C);
    const RtModule *m = NULL;
    DWORD reason = 99;
    uint64_t block_a = 0, block_b = 0;
    rt_test_lift_set('a', 4, cb4);
    rt_test_lift_set('a', 5, cb5);

    T_PLAN(1, "start-up gave the indices in registry order") {
        T_EQ(LA->tls_index, 0);
        T_EQ(LB->tls_index, 1);
        T_EQ(LC->tls_index, -1);
        T_EQ(rt_ldr_module(rt_module_find("corpus_O1.dll"))->tls_index, -1);
        T_EQ(rt_ldr_module(rt_module_find("corpus_O1.dll"))->state, RT_LDR_UNLOADED);
    }
    T_PLAN(2, "recording a thread is idempotent; it is in no entry point") {
        T_TRUE(rt_ldr_thread_record(self) == NULL);
        T_TRUE(!rt_loader_in_entry(self, &m, &reason));
        rt_ldr_tls_thread(ctx);
        RtLdrThread *r = rt_ldr_thread_record(self);
        rt_ldr_tls_thread(ctx);
        T_TRUE(r != NULL && rt_ldr_thread_record(self) == r && r->thread == self && !r->attached);
        T_EQ(rt_ldr_thread_count(), 1);
        T_TRUE(!rt_loader_in_entry(self, &m, &reason));
        T_TRUE(!rt_loader_in_entry(NULL, &m, &reason));
        T_EQ(tls_slot(ctx, 0), 0);                                  /* no module is loaded: no block */
    }
    T_PLAN(3, "load a: the index is stored, the thread has a block") {
        rt_ldr_tls_thread(ctx);
        rt_ldr_image_map(LA);
        ST32(A->base + 0x2080, 0xFFFFFFFF);
        rt_ldr_tls_load(LA);
        T_EQ(LD32(A->base + 0x2080), 0);
        block_a = tls_slot(ctx, 0);
        T_TRUE(block_a != 0 && block_a % 16 == 0);
        T_TRUE(rt_module_from_address(block_a) == NULL && !rt_vm_in_image_arena(block_a));       /* not the image */
        T_TRUE(rt_heap_size(rt_heap_process(), 0, (void *)(uintptr_t)block_a) >= 48);             /* a process-heap block */
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_a.dll: static TLS index 0"));
    }
    T_PLAN(4, "the block is the template, then the zero fill") {
        T_TRUE(is_template_a(block_a));
    }
    T_PLAN(5, "the block is a copy: writing it does not change the template") {
        ST8(block_a, 0xEE);
        T_EQ(LD8(A->base + 0x3100), 1);
        T_EQ(LD8(block_a), 0xEE);
    }
    T_PLAN(6, "load b: its own index and block; a's block is not touched") {
        rt_ldr_image_map(LB);
        rt_ldr_tls_load(LB);
        T_EQ(LD32(B->base + 0x2080), 1);
        block_b = tls_slot(ctx, 1);
        T_TRUE(block_b != 0 && block_b != block_a && block_b % 16 == 0);
        T_TRUE(is_template_b(block_b));
        T_EQ(tls_slot(ctx, 0), block_a);                            /* not reallocated */
        T_EQ(LD8(block_a), 0xEE);                                   /* and not refilled */
        T_EQ(LD32(A->base + 0x2080), 0);
    }
    T_PLAN(7, "no other slot is filled") {
        T_EQ(tls_slot(ctx, 2), 0);
        T_EQ(tls_slot(ctx, RT_STATIC_TLS_SLOTS - 1), 0);
    }
    T_PLAN(8, "the callbacks of a, in order, marked while they run") {
        memset(&cb, 0, sizeof cb);
        rt_ldr_tls_callbacks(ctx, LA, DLL_PROCESS_ATTACH);
        T_EQ(cb.n, 2);
        T_EQ(cb.which[0], 4);
        T_EQ(cb.which[1], 5);
        for (unsigned i = 0; i < 2; i++) {
            T_EQ(cb.rcx[i], A->base);
            T_EQ((uint32_t)cb.rdx[i], DLL_PROCESS_ATTACH);
            T_EQ(cb.r8[i], 0);
            T_TRUE(cb.inside[i] && cb.m[i] == A);
            T_EQ(cb.reason[i], DLL_PROCESS_ATTACH);
        }
        m = NULL;
        T_TRUE(!rt_loader_in_entry(self, &m, &reason));              /* not stale after the return */
        T_TRUE(rt_ldr_thread_record(self) != NULL && rt_ldr_thread_record(self)->entry == 0);
    }
    T_PLAN(9, "a module without callbacks, a module without a directory") {
        memset(&cb, 0, sizeof cb);
        rt_ldr_tls_callbacks(ctx, LB, DLL_THREAD_ATTACH);
        rt_ldr_tls_callbacks(ctx, LC, DLL_PROCESS_ATTACH);          /* not even mapped: nothing to do */
        T_EQ(cb.n, 0);
        T_TRUE(!rt_loader_in_entry(self, &m, &reason));
    }
    T_PLAN(12, "no TLS directory, and a directory that points outside the image") {
        T_TRAPS(rt_ldr_tls_load(LC), "TLS");
        T_TRAPS(rt_ldr_tls_load(LA), "twice");
        IMAGE_TLS_DIRECTORY64 *d = rt_ldr_rva(&LA->img, LA->img.dir[IMAGE_DIRECTORY_ENTRY_TLS].VirtualAddress, sizeof *d);
        T_REQUIRE(d != NULL);
        T_EQ(d->AddressOfIndex, A->base + 0x2080);                  /* relocated */
        uint64_t saved = d->AddressOfIndex;
        d->AddressOfIndex = A->base + 0x10000 - 2;                  /* its 4 bytes cross the end */
        T_TRAPS(rt_ldr_tls_load(LA), "outside the image");
        T_TRAPS(rt_ldr_tls_callbacks(ctx, LA, DLL_PROCESS_ATTACH), "outside the image");
        d->AddressOfIndex = saved;
        /* An alignment the loader does not give is said, not granted in silence. */
        T_TRUE(!T_LOGGED(RT_CH_LOADER, "alignment"));
        d->Characteristics = 0x00700000;                            /* IMAGE_SCN_ALIGN_64BYTES */
        memset(&cb, 0, sizeof cb);
        rt_ldr_tls_callbacks(ctx, LA, DLL_PROCESS_ATTACH);
        T_EQ(cb.n, 2);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_a.dll: its TLS directory asks for 64-byte alignment"));
        d->Characteristics = 0;
    }
    T_PLAN(13, "a nested call restores the outer mark (not in the plan)") {
        memset(&cb, 0, sizeof cb);
        uint64_t outer = rt_ldr_entry_enter(ctx, LB, DLL_THREAD_ATTACH);
        T_EQ(outer, 0);
        T_TRUE(rt_loader_in_entry(self, &m, &reason) && m == B);
        T_EQ(reason, DLL_THREAD_ATTACH);
        rt_ldr_tls_callbacks(ctx, LA, DLL_THREAD_DETACH);            /* as a DllMain of b that makes the loader call a */
        T_EQ(cb.n, 2);
        T_TRUE(cb.inside[0] && cb.m[0] == A && cb.inside[1] && cb.m[1] == A);
        T_EQ(cb.reason[0], DLL_THREAD_DETACH);
        T_EQ((uint32_t)cb.rdx[1], DLL_THREAD_DETACH);
        m = NULL;
        T_TRUE(rt_loader_in_entry(self, &m, &reason) && m == B);     /* b again, not "none" and not a */
        T_EQ(reason, DLL_THREAD_ATTACH);
        rt_ldr_entry_leave(ctx, outer);
        T_TRUE(!rt_loader_in_entry(self, &m, &reason));
        /* DLL_PROCESS_DETACH is reason 0: still "inside". */
        outer = rt_ldr_entry_enter(ctx, LA, DLL_PROCESS_DETACH);
        reason = 99;
        T_TRUE(rt_loader_in_entry(self, &m, &reason) && m == A);
        T_EQ(reason, DLL_PROCESS_DETACH);
        rt_ldr_entry_leave(ctx, outer);
    }
    T_PLAN(14, "the fatal report names the module (not in the plan)") {
        memset(&cb, 0, sizeof cb);
        report_line[0] = 0;
        rt_ldr_tls_callbacks(ctx, LA, DLL_PROCESS_ATTACH);
        T_TRUE(!strcmp(report_line, "  inside the entry point of t_lift_a.dll (reason 1)\n"));
        char big[4096];
        rt_report_guest(big, sizeof big, 0, self);                  /* outside: the line is not there */
        T_TRUE(strstr(big, "inside the entry point") == NULL);
        rt_test_lift_set('a', 5, cb_fatal);
        T_TRAPS(rt_ldr_tls_callbacks(ctx, LA, DLL_PROCESS_ATTACH), "boom in a TLS callback");
        T_TRAPS(rt_ldr_tls_callbacks(ctx, LA, DLL_PROCESS_ATTACH), "inside the entry point of t_lift_a.dll (reason 1)");
        rt_test_lift_set('a', 5, cb5);
    }
    T_PLAN(15, "misuse, and the thread unit's two hooks on guest thread 1 (not in the plan)") {
        T_TRAPS(rt_ldr_entry_enter(ctx, LA, 4), "not a DllMain reason");
        T_TRAPS(rt_ldr_tls_thread(NULL), "not the calling guest thread's");
        T_TRAPS(rt_loader_thread_detach(ctx), "never attached");     /* recorded on the way, not attached */
        T_TRAPS((rt_loader_thread_attach(ctx), rt_loader_thread_attach(ctx)), "called twice");
        /* End of a thread: its blocks are freed, its array is zero, the loader forgets it. */
        rt_ldr_tls_thread_end(ctx);
        T_EQ(tls_slot(ctx, 0), 0);
        T_EQ(tls_slot(ctx, 1), 0);
        T_TRUE(rt_ldr_thread_record(self) == NULL);
        T_EQ(rt_ldr_thread_count(), 0);
        T_TRAPS(rt_ldr_tls_thread_end(ctx), "not recorded");
        rt_ldr_tls_thread(ctx);                                     /* recorded again: fresh blocks of both */
        T_TRUE(is_template_a(tls_slot(ctx, 0)) && is_template_b(tls_slot(ctx, 1)));
    }
}

/* ---- two threads ------------------------------------------------------------------------------ */

/* The second thread of case 11: a host thread made a guest thread with rt_thread_attach.
 * `stage` is the main thread's word, `at` this thread's. */
typedef struct {
    int      stage, at;
    int      timed_out;
    uint64_t slot0_start, slot1_start;          /* before any module is loaded */
    uint64_t slot0_a;                           /* after the main thread loaded a */
    bool     tpl_a_ok;
    uint64_t slot0_b, slot1_b;                  /* after the main thread loaded b */
    bool     tpl_b_ok, kept;
    unsigned recorded;
} Second;

static bool reached(const int *p, int v) { return EVENTUALLY(wu_load(p) >= v); }

static void *second_host(void *arg) {
    Second *s = arg;
    RtThread *t = rt_thread_attach("tls-second");          /* rt_loader_thread_attach records it */
    Ctx *ctx = &t->ctx;
    rt_loader_lock();
    rt_ldr_tls_thread(ctx);                                 /* the plan's call: already recorded, no change */
    s->recorded = rt_ldr_thread_count();
    rt_loader_unlock();
    s->slot0_start = tls_slot(ctx, 0);
    s->slot1_start = tls_slot(ctx, 1);
    wu_store(&s->at, 1);
    if (!reached(&s->stage, 1)) { wu_store(&s->timed_out, 1); goto out; }
    s->slot0_a = tls_slot(ctx, 0);                          /* given by rt_ldr_tls_load on the main thread */
    s->tpl_a_ok = s->slot0_a && is_template_a(s->slot0_a);
    if (s->slot0_a) ST8(s->slot0_a + 1, 0x77);              /* this thread's own data */
    wu_store(&s->at, 2);
    if (!reached(&s->stage, 2)) { wu_store(&s->timed_out, 1); goto out; }
    s->slot0_b = tls_slot(ctx, 0);
    s->slot1_b = tls_slot(ctx, 1);
    s->tpl_b_ok = s->slot1_b && is_template_b(s->slot1_b);
    s->kept = s->slot0_b && LD8(s->slot0_b + 1) == 0x77;
    wu_store(&s->at, 3);
    if (!reached(&s->stage, 3)) wu_store(&s->timed_out, 1);
out:
    rt_thread_detach(t);                                    /* rt_loader_thread_detach frees the blocks */
    wu_store(&s->at, 4);
    return NULL;
}

/* The second thread of case 10: a guest thread made by rt_thread_create.  Its start routine
 * reads its array the way lifted code does. */
static struct { int ran; uint64_t slot0, slot1, slot2; bool tpl_a_ok, tpl_b_ok, same_after; unsigned recorded; bool attached; } created;

GUEST_FN(tls_reader) {
    created.slot0 = tls_slot(ctx, 0);
    created.slot1 = tls_slot(ctx, 1);
    created.slot2 = tls_slot(ctx, 2);
    created.tpl_a_ok = created.slot0 && is_template_a(created.slot0);
    created.tpl_b_ok = created.slot1 && is_template_b(created.slot1);
    rt_loader_lock();
    rt_ldr_tls_thread(ctx);                                 /* the plan's call: idempotent */
    created.recorded = rt_ldr_thread_count();
    created.attached = rt_ldr_thread_record(RT_THREAD(ctx)) && rt_ldr_thread_record(RT_THREAD(ctx))->attached;
    rt_loader_unlock();
    created.same_after = tls_slot(ctx, 0) == created.slot0 && tls_slot(ctx, 1) == created.slot1;
    wu_store(&created.ran, 1);
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

TEST_SETUP(core, loader_tls_threads) { write_slots(); }
TEST(core, loader_tls_threads)
{
    Ctx *ctx = T_CTX();
    RtModule *A = rt_module_find("t_lift_a.dll"), *B = rt_module_find("t_lift_b.dll");
    T_REQUIRE(A != NULL && B != NULL);
    RtLdrModule *LA = rt_ldr_module(A), *LB = rt_ldr_module(B);
    static Second s;
    pthread_t th;
    uint64_t main_a = 0, main_b = 0;

    T_PLAN(11, "a thread recorded before a module loads gets its block from the load on another thread") {
        rt_ldr_tls_thread(ctx);
        T_EQ(pthread_create(&th, NULL, second_host, &s), 0);
        T_REQUIRE(reached(&s.at, 1));
        T_EQ(s.recorded, 2);
        T_EQ(s.slot0_start, 0);
        T_EQ(s.slot1_start, 0);

        rt_loader_lock();
        rt_ldr_image_map(LA);
        rt_ldr_tls_load(LA);                                /* writes the other thread's array while it runs */
        rt_loader_unlock();
        main_a = tls_slot(ctx, 0);
        ST8(main_a, 0xEE);
        wu_store(&s.stage, 1);
        T_REQUIRE(reached(&s.at, 2));
        T_TRUE(s.slot0_a != 0);                             /* the load reached the recorded thread */
        T_TRUE(s.tpl_a_ok);
        T_TRUE(main_a != 0 && s.slot0_a != main_a);         /* one block per thread */

        rt_loader_lock();
        rt_ldr_image_map(LB);
        rt_ldr_tls_load(LB);
        rt_loader_unlock();
        main_b = tls_slot(ctx, 1);
        wu_store(&s.stage, 2);
        T_REQUIRE(reached(&s.at, 3));
        T_EQ(s.slot0_b, s.slot0_a);                         /* the other thread's block of a did not move */
        T_TRUE(s.kept);                                     /* nor lose what that thread wrote */
        T_TRUE(s.slot1_b != 0 && s.slot1_b != main_b && s.slot1_b != s.slot0_b);
        T_TRUE(s.tpl_b_ok);
        T_EQ(tls_slot(ctx, 0), main_a);
        T_EQ(LD8(main_a), 0xEE);

        wu_store(&s.stage, 3);
        T_REQUIRE(reached(&s.at, 4));
        T_EQ(pthread_join(th, NULL), 0);
        T_EQ(wu_load(&s.timed_out), 0);
        T_EQ(rt_ldr_thread_count(), 1);                     /* the ended thread is forgotten */
    }
    T_PLAN(10, "a thread that starts after the loads has fresh blocks of its own") {
        T_REQUIRE(main_a != 0 && main_b != 0);
        DWORD id = 0;
        HANDLE h = rt_thread_create(ctx, 0, T_GUEST_ADDR(tls_reader), 0, 0, &id);
        T_REQUIRE(h != NULL);
        T_EQ(rt_wait_handle(ctx, h, 30000, FALSE), WAIT_OBJECT_0);
        T_EQ(wu_load(&created.ran), 1);
        T_TRUE(created.slot0 != 0 && created.slot0 != main_a);
        T_TRUE(created.slot1 != 0 && created.slot1 != main_b && created.slot1 != created.slot0);
        T_TRUE(created.tpl_a_ok);                           /* the template, not thread 1's block (0xEE) */
        T_TRUE(created.tpl_b_ok);
        T_EQ(created.slot2, 0);
        T_TRUE(created.same_after && created.attached);
        T_EQ(created.recorded, 2);
        T_EQ(LD8(main_a), 0xEE);
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_EQ(rt_ldr_thread_count(), 1);
        T_EQ(rt_handle_close(h), TRUE);
    }
}
