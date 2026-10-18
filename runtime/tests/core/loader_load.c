/* The loader: loading a lifted module, its entry point, the thread attach and detach calls
 * (runtime/core/loader_load.c; specification: rt_module_load and its neighbours in
 * rt/module.h, rt_ldr_call_entry and rt_ldr_order_* in core/loader.h).
 * Plan: docs/handoff/rt-contract-loader.md, task 5, cases 1 to 21; 22 and up are not in it.
 *
 * Nine tests, because a module can be loaded once per process and each test is a process:
 *   loader_load          cases 1 to 11, 18 to 20: the chain a -> b -> c loaded through a
 *   loader_load_threads  the thread unit's two hooks on real threads from rt_thread_create;
 *                        static TLS and TLS callbacks inside a load; DisableThreadLibraryCalls
 *   loader_load_race     case 21, for real: two guest threads load one module at once
 *   loader_load_fail     case 12   an entry point returns FALSE
 *   loader_load_bool     case 13   the BOOL is the low half of rax
 *   loader_load_reentry  case 14   loads from inside entry points
 *   loader_load_cycle    case 15, and two modules that import from each other
 *   loader_load_absent   case 16   an import whose DLL file is not there
 *   loader_load_traps    case 17, and a thread that ends inside an entry point
 * Every wait on another thread is bounded (EVENTUALLY and the waits on a thread handle: 30 s).
 *
 * The three slots (rt/test_pe.h): a imports BFn from b and GetLastError from kernel32; b
 * imports CVal from c and exports BFn; c exports CVal.  Function 0 of each slot is its entry
 * point: it appends to `elog` and returns what ret_val[] says.
 */
#include <pthread.h>
#include <unistd.h>

#include "wait_util.c"
#include "rt/test_pe.h"
#include "rt/win/teb.h"
#include "../../core/loader.h"

#define F(i)        RT_TEST_LIFT_RVA(i)
#define CORPUS_REL  "bin/x64_retail/corpus_O1.dll"
#define UNBOUND     UINT64_C(0xBAD0BAD0BAD0BAD0)      /* what the file leaves in an import slot */
#define A_SLOT_B    0x2020u                           /* a's slot for b's BFn */
#define A_SLOT_K    0x2040u                           /* a's slot for kernel32's GetLastError */
#define B_SLOT      0x2020u                           /* b's slot for c's CVal (or a's AFn) */
#define C_TLS_INDEX 0x2080u

enum { W_C_FILE = 1, W_C_ENTRY = 2, W_C_TLS = 4, W_CYCLE = 8, W_CHAIN = W_C_FILE | W_C_ENTRY };

static void write_slots(unsigned what) {
    if (what & W_C_FILE) {
        RtTestPe *c = rt_test_pe_new();
        if (what & W_C_ENTRY) rt_test_pe_entry(c, F(0));
        rt_test_pe_export(c, "CVal", 1, 0x2200);
        if (what & W_C_TLS) {
            static const uint8_t tpl[8] = { 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8 };
            rt_test_pe_put(c, 0x3100, tpl, 8);
            rt_test_pe_put_va(c, 0x3200, F(4));
            rt_test_pe_tls(c, 0x3100, 8, 8, C_TLS_INDEX, 0x3200);
        }
        rt_test_pe_write(c, "t_lift_c.dll");
    }
    RtTestPe *b = rt_test_pe_new();
    rt_test_pe_entry(b, F(0));
    if (what & W_CYCLE) rt_test_pe_import(b, "t_lift_a.dll", "AFn", 0, B_SLOT);
    else                rt_test_pe_import(b, "t_lift_c.dll", "CVal", 0, B_SLOT);
    rt_test_pe_export(b, "BFn", 1, F(1));
    rt_test_pe_write(b, "t_lift_b.dll");
    RtTestPe *a = rt_test_pe_new();
    rt_test_pe_entry(a, F(0));
    rt_test_pe_import(a, "t_lift_b.dll", "BFn", 0, A_SLOT_B);
    rt_test_pe_import(a, "kernel32.dll", "GetLastError", 0, A_SLOT_K);
    if (what & W_CYCLE) rt_test_pe_export(a, "AFn", 1, F(1));
    rt_test_pe_write(a, "t_lift_a.dll");
    rt_test_install_copy(RT_TEST_CORPUS_DLL, CORPUS_REL);
}

/* ---- the log of calls the loader made --------------------------------------------------------- */

typedef struct {
    char     kind;              /* 'e' entry point, 't' TLS callback, 's' a thread's start routine */
    char     slot;
    uint64_t rcx, rdx, r8;
    uint32_t thread;
    bool     in;                /* rt_loader_in_entry inside the call, and what it named */
    const RtModule *in_m;
    DWORD    in_reason;
    uint64_t tls_block;         /* 't' only: the thread's own block of the module */
} Call;

#define LOG_MAX 128
static struct { unsigned n; Call c[LOG_MAX]; } elog;
static uint64_t ret_val[3] = { 1, 1, 1 };                   /* what an entry point returns for DLL_PROCESS_ATTACH */
static void (*inside[3])(Ctx *ctx, DWORD reason);           /* more work for a slot's entry point */

static Call *note(Ctx *ctx, char kind, int s) {
    static Call spare;
    unsigned i = __atomic_fetch_add(&elog.n, 1, __ATOMIC_SEQ_CST);
    Call *c = i < LOG_MAX ? &elog.c[i] : &spare;
    c->kind = kind;
    c->slot = (char)('a' + s);
    c->rcx = ctx->r[1];
    c->rdx = ctx->r[2];
    c->r8 = ctx->r[8];
    c->thread = RT_THREAD(ctx)->id;
    c->in = rt_loader_in_entry(RT_THREAD(ctx), &c->in_m, &c->in_reason);
    return c;
}

/* The thread calls return FALSE, which the loader must ignore. */
static void entry(Ctx *ctx, int s) {
    const DWORD reason = (DWORD)ctx->r[2];
    note(ctx, 'e', s);
    if (inside[s]) inside[s](ctx, reason);
    ctx->r[0] = reason == DLL_PROCESS_ATTACH ? ret_val[s] : 0;
    GUEST_RET(ctx);
}
static void entry_a(Ctx *ctx) { entry(ctx, 0); }
static void entry_b(Ctx *ctx) { entry(ctx, 1); }
static void entry_c(Ctx *ctx) { entry(ctx, 2); }

static void tls_c(Ctx *ctx) {
    RtModule *C = rt_module_find("t_lift_c.dll");
    Call *c = note(ctx, 't', 2);
    c->tls_block = LD64(LD64(ctx->gs_base + RT_TEB_TLS_POINTER) + 8 * (uint64_t)LD32(C->base + C_TLS_INDEX));
    GUEST_RET(ctx);
}

static void set_hooks(void) {
    rt_test_lift_set('a', 0, entry_a);
    rt_test_lift_set('b', 0, entry_b);
    rt_test_lift_set('c', 0, entry_c);
    rt_test_lift_set('c', 4, tls_c);
}

/* Call i of the log is a call of `kind` into slot's module `m` as fn(base, reason, 0) on
 * `thread`, and the thread was marked as inside exactly that call. */
static bool is_call(unsigned i, char kind, char slot, const RtModule *m, DWORD reason, uint32_t thread) {
    const Call *c = &elog.c[i];
    return i < elog.n && i < LOG_MAX && c->kind == kind && c->slot == slot && c->rcx == m->base && c->rdx == reason
        && c->r8 == 0 && c->thread == thread && c->in && c->in_m == m && c->in_reason == reason;
}

/* How often slot's entry point was called with `reason`. */
static unsigned entries(char slot, DWORD reason) {
    unsigned n = 0;
    for (unsigned i = 0; i < elog.n && i < LOG_MAX; i++)
        n += elog.c[i].kind == 'e' && elog.c[i].slot == slot && elog.c[i].rdx == reason;
    return n;
}

static uint32_t protection(uint64_t addr) {
    MEMORY_BASIC_INFORMATION mbi;
    return rt_vm_query(addr, &mbi) == sizeof mbi ? mbi.Protect : 0;
}

#define MODULES \
    Ctx *ctx = T_CTX(); \
    const uint32_t me = RT_THREAD(ctx)->id; \
    RtModule *A = rt_module_find("t_lift_a.dll"), *B = rt_module_find("t_lift_b.dll"), *C = rt_module_find("t_lift_c.dll"); \
    T_REQUIRE(A != NULL && B != NULL && C != NULL); \
    RtLdrModule *LA = rt_ldr_module(A), *LB = rt_ldr_module(B), *LC = rt_ldr_module(C); \
    (void)me; (void)LA; (void)LB; (void)LC; \
    set_hooks()

/* ---- loader_load: the chain ------------------------------------------------------------------- */

static struct {
    uint64_t a_slot_b, a_slot_k, b_slot_c;
    bool     a_loaded, b_loaded_in_c, a_finds_itself;
    int      a_state, a_state_in_c, b_state_in_c, c_state_in_c;
    unsigned order_in_a, order_in_c;
    RtLdrModule *last_in_a;
    uint32_t rdata_in_a;
} seen;

static void chain_in_a(Ctx *ctx, DWORD reason) {
    (void)ctx;
    if (reason != DLL_PROCESS_ATTACH) return;
    RtModule *A = rt_module_find("t_lift_a.dll");
    seen.a_slot_b = LD64(A->base + A_SLOT_B);
    seen.a_slot_k = LD64(A->base + A_SLOT_K);
    seen.a_loaded = A->loaded;
    seen.a_finds_itself = rt_module_handle_a("t_lift_a.dll") == A && rt_module_from_handle((HMODULE)(uintptr_t)A->base) == A
                       && rt_module_from_pc(A->base + F(0)) == A;
    seen.a_state = (int)rt_ldr_module(A)->state;
    seen.order_in_a = rt_ldr_order_count();
    seen.last_in_a = seen.order_in_a ? rt_ldr_order_at(seen.order_in_a - 1) : NULL;
    seen.rdata_in_a = protection(A->base + 0x3000);
}

static void chain_in_b(Ctx *ctx, DWORD reason) {
    (void)ctx;
    if (reason == DLL_PROCESS_ATTACH) seen.b_slot_c = LD64(rt_module_find("t_lift_b.dll")->base + B_SLOT);
}

static void chain_in_c(Ctx *ctx, DWORD reason) {
    (void)ctx;
    if (reason != DLL_PROCESS_ATTACH) return;
    seen.a_state_in_c = (int)rt_ldr_module(rt_module_find("t_lift_a.dll"))->state;
    seen.b_state_in_c = (int)rt_ldr_module(rt_module_find("t_lift_b.dll"))->state;
    seen.c_state_in_c = (int)rt_ldr_module(rt_module_find("t_lift_c.dll"))->state;
    seen.b_loaded_in_c = rt_module_find("t_lift_b.dll")->loaded;
    seen.order_in_c = rt_ldr_order_count();
}

TEST_SETUP(core, loader_load) { write_slots(W_CHAIN); }
TEST(core, loader_load)
{
    MODULES;
    RtModule *K = rt_module_find("kernel32.dll");
    T_REQUIRE(K != NULL);
    const RtModule *m = NULL;
    DWORD reason = 99;
    inside[0] = chain_in_a;
    inside[1] = chain_in_b;
    inside[2] = chain_in_c;

    T_PLAN(20, "the attach hook on guest thread 1 before any load calls nothing") {
        T_EQ(LA->state, RT_LDR_UNLOADED);
        T_TRUE(!A->loaded && A->load_count == 0);
        T_EQ(rt_ldr_order_count(), 0);
        T_TRAPS(rt_ldr_order_at(0), "the load order has 0 modules");
        rt_loader_thread_attach(ctx);
        T_EQ(elog.n, 0);
        T_TRUE(rt_ldr_thread_record(RT_THREAD(ctx)) != NULL && rt_ldr_thread_record(RT_THREAD(ctx))->attached);
        T_TRAPS(rt_loader_thread_attach(ctx), "twice");
    }
    T_PLAN(1, "loading a runs the entry points of c, b and a, in that order, once each") {
        rt_set_last_error(ctx, 0x1234);
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == A);
        T_EQ(elog.n, 3);
        T_TRUE(is_call(0, 'e', 'c', C, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(1, 'e', 'b', B, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(2, 'e', 'a', A, DLL_PROCESS_ATTACH, me));
        T_TRUE(!rt_loader_in_entry(RT_THREAD(ctx), &m, &reason));    /* the mark is taken back */
    }
    T_PLAN(10, "a load that succeeds leaves last error alone") {
        T_EQ(T_LAST_ERROR(), 0x1234);
    }
    T_PLAN(2, "all three are loaded, counted once and in the load order") {
        T_TRUE(A->loaded && B->loaded && C->loaded);
        T_EQ(A->load_count, 1);
        T_EQ(B->load_count, 1);
        T_EQ(C->load_count, 1);
        T_EQ(rt_ldr_order_count(), 3);
        T_TRUE(rt_ldr_order_at(0) == LC && rt_ldr_order_at(1) == LB && rt_ldr_order_at(2) == LA);
        T_TRUE(LC->order == 0 && LB->order == 1 && LA->order == 2);
        T_EQ(LA->state, RT_LDR_LOADED);
        T_EQ(LB->state, RT_LDR_LOADED);
        T_EQ(LC->state, RT_LDR_LOADED);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_a.dll: loaded at"));
        T_TRAPS(rt_ldr_order_at(3), "the load order has 3 modules");
    }
    T_PLAN(3, "the import slots were filled before the entry point ran") {
        T_EQ(seen.a_slot_b, B->base + F(1));
        T_EQ(seen.a_slot_k, rt_native_import("kernel32.dll", "GetLastError", 0));
        T_EQ(seen.b_slot_c, C->base + 0x2200);
        T_EQ(LD64(A->base + A_SLOT_B), B->base + F(1));
    }
    T_PLAN(4, "inside the entry point the module is loaded, LOADING and in the order") {
        T_TRUE(seen.a_loaded);
        T_TRUE(seen.a_finds_itself);                                 /* GetModuleHandle on itself, as a C runtime does */
        T_EQ(seen.a_state, RT_LDR_LOADING);
        T_EQ(seen.order_in_a, 3);
        T_TRUE(seen.last_in_a == LA);
        /* c's entry point ran while b and a were waiting for their imports. */
        T_EQ(seen.c_state_in_c, RT_LDR_LOADING);
        T_EQ(seen.b_state_in_c, RT_LDR_LOADING);
        T_EQ(seen.a_state_in_c, RT_LDR_LOADING);
        T_TRUE(seen.b_loaded_in_c);
        T_EQ(seen.order_in_c, 1);
    }
    T_PLAN(18, "the pages have their final protection, and had it when the entry point ran") {
        T_EQ(protection(A->base + 0x3000), PAGE_READONLY);
        T_EQ(protection(A->base + 0x2000), PAGE_READWRITE);
        T_EQ(protection(A->base + 0x1000), PAGE_EXECUTE_READ);
        T_EQ(seen.rdata_in_a, PAGE_READONLY);
    }
    T_PLAN(5, "a second load counts and runs nothing") {
        T_TRUE(rt_module_load(ctx, u"T_LIFT_A", 0) == A);
        T_EQ(A->load_count, 2);
        T_EQ(elog.n, 3);
        T_EQ(B->load_count, 1);
        T_EQ(C->load_count, 1);
        T_EQ(rt_ldr_order_count(), 3);
    }
    T_PLAN(6, "a native module is counted; the search flags are ignored") {
        const int32_t k0 = K->load_count;
        rt_set_last_error(ctx, 0x1234);
        T_TRUE(rt_module_load(ctx, u"kernel32.dll", 0) == K);
        T_EQ(K->load_count, k0 + 1);
        T_TRUE(rt_module_load_a(ctx, "C:\\x\\KERNEL32", LOAD_WITH_ALTERED_SEARCH_PATH) == K);
        T_TRUE(rt_module_load_a(ctx, "kernel32", LOAD_LIBRARY_SEARCH_SYSTEM32) == K);
        T_TRUE(rt_module_load(ctx, u"c:/x/Kernel32.DLL", LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS
                              | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR) == K);
        T_EQ(K->load_count, k0 + 4);
        /* Only the last path component names the module: a directory may hold any character. */
        T_TRUE(rt_module_load(ctx, u"C:\\caf\u00e9\\kernel32.dll", 0) == K);
        T_TRUE(rt_module_load_a(ctx, "C:\\caf\xE9\\kernel32.dll", 0) == K);
        T_EQ(K->load_count, k0 + 6);
        T_EQ(T_LAST_ERROR(), 0x1234);
        K->load_count = k0;
    }
    T_PLAN(7, "a name no module has") {
        T_TRUE(rt_module_load(ctx, u"nosuch.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "LoadLibrary(\"nosuch.dll\"): the module registry has no such module"));
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load_a(ctx, "caf\xE9.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "LoadLibrary(\"caf?.dll\")"));
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"kernel32\u00e9", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    }
    T_PLAN(8, "no name, an empty name, a flag bit nobody defines") {
        const int32_t k0 = K->load_count;
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load_a(ctx, NULL, 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load_a(ctx, "", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"kernel32", 0x4000) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load_a(ctx, "kernel32", 0x80000000u | LOAD_WITH_ALTERED_SEARCH_PATH) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        /* A NULL name fails whatever the flags; it does not reach the stop of case 9. */
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, NULL, LOAD_LIBRARY_AS_DATAFILE) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(K->load_count, k0);
    }
    T_PLAN(9, "the flags that are not built stop, before the name is looked up") {
        T_TRAPS(rt_module_load(ctx, u"kernel32", LOAD_LIBRARY_AS_DATAFILE), "LOAD_LIBRARY_AS_DATAFILE");
        T_TRAPS(rt_module_load_a(ctx, "nosuch", DONT_RESOLVE_DLL_REFERENCES), "DONT_RESOLVE_DLL_REFERENCES");
        T_TRAPS(rt_module_load(ctx, u"nosuch", LOAD_LIBRARY_AS_IMAGE_RESOURCE | LOAD_WITH_ALTERED_SEARCH_PATH),
                "LoadLibraryEx(\"nosuch\") with LOAD_LIBRARY_AS_IMAGE_RESOURCE (flags 0x28)");
        T_TRAPS(rt_module_load(ctx, u"kernel32", LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE), "LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE");
        T_TRAPS(rt_module_load(ctx, u"kernel32", LOAD_IGNORE_CODE_AUTHZ_LEVEL), "LOAD_IGNORE_CODE_AUTHZ_LEVEL");
        T_TRAPS(rt_module_load(NULL, u"kernel32", 0), "rt_module_load: the ctx is not the calling guest thread's");
        Ctx stranger;                                                /* belongs to no thread */
        memset(&stranger, 0, sizeof stranger);
        T_TRAPS(rt_module_load_a(&stranger, "kernel32", 0), "rt_module_load_a: the ctx is not the calling guest thread's");
    }
    T_PLAN(11, "FreeLibrary counts down to 0, unloads nothing and calls nothing") {
        rt_set_last_error(ctx, 0x1234);
        T_EQ(rt_module_free(A), TRUE);
        T_EQ(A->load_count, 1);
        T_TRUE(!T_LOGGED(RT_CH_LOADER, "FreeLibrary(t_lift_a.dll)"));
        T_EQ(rt_module_free(A), TRUE);
        T_EQ(A->load_count, 0);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "FreeLibrary(t_lift_a.dll): its load count is 0"));
        T_TRUE(!T_LOGGED(RT_CH_LOADER, "freed more often than loaded"));
        T_EQ(rt_module_free(A), TRUE);
        T_EQ(A->load_count, 0);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "FreeLibrary(t_lift_a.dll): freed more often than loaded"));
        T_TRUE(A->loaded);
        T_EQ(LA->state, RT_LDR_LOADED);
        T_EQ(rt_ldr_order_count(), 3);
        T_EQ(elog.n, 3);
        T_EQ(B->load_count, 1);
        T_EQ(T_LAST_ERROR(), 0x1234);                               /* always TRUE: no last error */
        T_TRUE(rt_module_load(ctx, u"t_lift_a", 0) == A);           /* loaded again: counted, not run */
        T_EQ(A->load_count, 1);
        T_EQ(elog.n, 3);
        T_TRAPS(rt_module_free(NULL), "null");
    }
    T_PLAN(20, "the detach hook calls the entry points last to first; attach, first to last") {
        rt_loader_thread_detach(ctx);
        T_EQ(elog.n, 6);
        T_TRUE(is_call(3, 'e', 'a', A, DLL_THREAD_DETACH, me));
        T_TRUE(is_call(4, 'e', 'b', B, DLL_THREAD_DETACH, me));
        T_TRUE(is_call(5, 'e', 'c', C, DLL_THREAD_DETACH, me));
        T_TRUE(rt_ldr_thread_record(RT_THREAD(ctx)) == NULL);        /* forgotten */
        T_EQ(rt_ldr_thread_count(), 0);
        T_TRAPS(rt_loader_thread_detach(ctx), "never attached");
        T_TRAPS(rt_loader_thread_detach(NULL), "never attached");
        rt_loader_thread_attach(ctx);                                /* a thread the loader does not know: attach it */
        T_EQ(elog.n, 9);
        T_TRUE(is_call(6, 'e', 'c', C, DLL_THREAD_ATTACH, me));
        T_TRUE(is_call(7, 'e', 'b', B, DLL_THREAD_ATTACH, me));
        T_TRUE(is_call(8, 'e', 'a', A, DLL_THREAD_ATTACH, me));
        T_TRUE(!rt_loader_in_entry(RT_THREAD(ctx), &m, &reason));
        /* The thread calls returned FALSE: nothing failed. */
        T_EQ(LA->state, RT_LDR_LOADED);
        T_EQ(rt_ldr_order_count(), 3);
    }
    T_PLAN(19, "the corpus through the public door") {
        RtModule *corpus = rt_module_load(ctx, u"corpus_O1.dll", 0);
        T_REQUIRE(corpus != NULL && corpus == rt_module_find("corpus_O1.dll"));
        T_EQ(rt_ldr_module(corpus)->state, RT_LDR_LOADED);
        T_EQ(corpus->load_count, 1);
        T_EQ(rt_ldr_order_count(), 4);
        T_TRUE(rt_ldr_order_at(3) == rt_ldr_module(corpus));
        T_TRUE(T_LOGGED(RT_CH_LOADER, "corpus_o1.dll: loaded at"));
        uint64_t fib = rt_module_export(corpus, "fib", 0);
        T_REQUIRE(fib != 0);
        const char *detail = NULL;
        RtTestArg arg = { RT_AC_U32, 4, { .u = 13 } };
        RtTestRet r = rt_test_invoke(fib, 1, &arg, &detail);
        T_TRUE(r.violations == 0);
        T_EQ((uint32_t)r.rax, 233);
        T_EQ(elog.n, 9);                                             /* no entry point: no call */
    }
}

/* ---- loader_load_threads: the two hooks on real threads --------------------------------------- */

static struct { int ran; uint32_t id; uint64_t param; } started;

GUEST_FN(thread_start) {
    Call *c = note(ctx, 's', 0);
    started.id = c->thread;
    started.param = c->rcx;
    wu_store(&started.ran, 1);
    ctx->r[0] = 5;
    GUEST_RET(ctx);
}

/* Start a thread, wait for it to end and for its host thread to be gone.  Returns its id. */
static uint32_t run_thread(Ctx *ctx, uint64_t start, DWORD *exit_code) {
    DWORD id = 0;
    HANDLE h = rt_thread_create(ctx, 0, start, 0x77, 0, &id);
    if (!h) return 0;
    if (rt_wait_handle(ctx, h, 30000, FALSE) != WAIT_OBJECT_0) return 0;
    if (exit_code && !rt_thread_get_exit_code(h, exit_code)) return 0;
    rt_handle_close(h);
    return EVENTUALLY(rt_thread_host_threads() == 0) ? id : 0;
}

TEST_SETUP(core, loader_load_threads) { write_slots(W_CHAIN | W_C_TLS); }
TEST(core, loader_load_threads)
{
    MODULES;
    uint64_t my_block = 0;

    T_PLAN(22, "static TLS and the TLS callbacks inside a load: index, block, callbacks, then the entry point") {
        T_EQ(LC->tls_index, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == A);
        T_EQ(elog.n, 4);
        T_TRUE(is_call(0, 't', 'c', C, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(1, 'e', 'c', C, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(2, 'e', 'b', B, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(3, 'e', 'a', A, DLL_PROCESS_ATTACH, me));
        T_EQ(LD32(C->base + C_TLS_INDEX), 0);
        my_block = elog.c[0].tls_block;                              /* the callback already had the block */
        T_TRUE(my_block != 0 && LD8(my_block) == 0xC1 && LD8(my_block + 8) == 0);
        T_EQ(rt_ldr_thread_count(), 1);                              /* guest thread 1, recorded by the load */
        T_TRUE(!rt_ldr_thread_record(RT_THREAD(ctx))->attached);
    }
    T_PLAN(23, "a thread made after the load: attach calls first to last, detach calls last to first, on that thread") {
        DWORD code = 0;
        uint32_t id = run_thread(ctx, T_GUEST_ADDR(thread_start), &code);
        T_REQUIRE(id != 0 && id != me);
        T_EQ(wu_load(&started.ran), 1);
        T_EQ(started.id, id);
        T_EQ(started.param, 0x77);
        T_EQ(code, 5);
        T_EQ(elog.n, 13);
        T_TRUE(is_call(4, 't', 'c', C, DLL_THREAD_ATTACH, id));
        T_TRUE(is_call(5, 'e', 'c', C, DLL_THREAD_ATTACH, id));
        T_TRUE(is_call(6, 'e', 'b', B, DLL_THREAD_ATTACH, id));
        T_TRUE(is_call(7, 'e', 'a', A, DLL_THREAD_ATTACH, id));
        T_TRUE(elog.c[8].kind == 's' && elog.c[8].thread == id && !elog.c[8].in);
        T_TRUE(is_call(9, 'e', 'a', A, DLL_THREAD_DETACH, id));
        T_TRUE(is_call(10, 'e', 'b', B, DLL_THREAD_DETACH, id));
        T_TRUE(is_call(11, 'e', 'c', C, DLL_THREAD_DETACH, id));
        T_TRUE(is_call(12, 't', 'c', C, DLL_THREAD_DETACH, id));
        /* The thread had its own block of c from before its first callback to after its last. */
        T_TRUE(elog.c[4].tls_block != 0 && elog.c[4].tls_block != my_block);
        T_EQ(elog.c[12].tls_block, elog.c[4].tls_block);
        T_EQ(rt_ldr_thread_count(), 1);
    }
    T_PLAN(24, "DisableThreadLibraryCalls: no more thread calls to that entry point; refused for static TLS") {
        rt_set_last_error(ctx, 0x1234);
        T_EQ(rt_module_disable_thread_calls(B), TRUE);
        T_EQ(rt_module_disable_thread_calls(rt_module_find("kernel32.dll")), TRUE);
        T_EQ(T_LAST_ERROR(), 0x1234);
        T_EQ(rt_module_disable_thread_calls(C), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "DisableThreadLibraryCalls(t_lift_c.dll): refused"));
        T_TRAPS(rt_module_disable_thread_calls(NULL), "null");

        uint32_t id = run_thread(ctx, T_GUEST_ADDR(thread_start), NULL);
        T_REQUIRE(id != 0);
        T_EQ(elog.n, 20);
        T_TRUE(is_call(13, 't', 'c', C, DLL_THREAD_ATTACH, id));
        T_TRUE(is_call(14, 'e', 'c', C, DLL_THREAD_ATTACH, id));
        T_TRUE(is_call(15, 'e', 'a', A, DLL_THREAD_ATTACH, id));
        T_TRUE(elog.c[16].kind == 's' && elog.c[16].thread == id);
        T_TRUE(is_call(17, 'e', 'a', A, DLL_THREAD_DETACH, id));
        T_TRUE(is_call(18, 'e', 'c', C, DLL_THREAD_DETACH, id));
        T_TRUE(is_call(19, 't', 'c', C, DLL_THREAD_DETACH, id));
        T_EQ(entries('b', DLL_THREAD_ATTACH) + entries('b', DLL_THREAD_DETACH), 2);     /* the first thread's */
    }
}

/* ---- loader_load_race: case 21 ---------------------------------------------------------------- */

static struct {
    int       ready[2], go, calling[2], done[2];
    int       a_entry_done;                     /* a's entry point is about to return */
    RtModule *got[2];
    int       done_at_return[2];                /* a_entry_done when rt_module_load came back */
    int       state_at_return[2];
} race;

GUEST_FN(race_start) {
    const unsigned i = (unsigned)ctx->r[1] & 1;
    wu_store(&race.ready[i], 1);                /* attached: it will not need the loader lock before its load */
    (void)EVENTUALLY(wu_load(&race.go));
    wu_store(&race.calling[i], 1);
    RtModule *m = rt_module_load(ctx, u"t_lift_a.dll", 0);
    race.done_at_return[i] = wu_load(&race.a_entry_done);
    race.state_at_return[i] = m ? (int)rt_ldr_module(m)->state : -1;
    race.got[i] = m;
    wu_store(&race.done[i], 1);
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* a's entry point stays inside until the other thread is at the door too, and then for the
 * 50 ms of the plan, so that a door left open is walked through. */
static void race_in_a(Ctx *ctx, DWORD reason) {
    (void)ctx;
    if (reason != DLL_PROCESS_ATTACH) return;
    (void)EVENTUALLY(wu_load(&race.calling[0]) && wu_load(&race.calling[1]));
    usleep(50000);
    wu_store(&race.a_entry_done, 1);
}

TEST_SETUP(core, loader_load_race) { write_slots(W_CHAIN); }
TEST(core, loader_load_race)
{
    MODULES;
    inside[0] = race_in_a;

    T_PLAN(21, "two guest threads load a at once: one runs the entry points, the other waits and finds it loaded") {
        HANDLE h[2];
        for (unsigned i = 0; i < 2; i++) {
            h[i] = rt_thread_create(ctx, 0, T_GUEST_ADDR(race_start), i, 0, NULL);
            T_REQUIRE(h[i] != NULL);
        }
        T_REQUIRE(EVENTUALLY(wu_load(&race.ready[0]) && wu_load(&race.ready[1])));
        T_EQ(elog.n, 0);
        wu_store(&race.go, 1);
        T_REQUIRE(EVENTUALLY(wu_load(&race.done[0]) && wu_load(&race.done[1])));
        for (unsigned i = 0; i < 2; i++) {
            T_EQ(rt_wait_handle(ctx, h[i], 30000, FALSE), WAIT_OBJECT_0);
            T_EQ(rt_handle_close(h[i]), TRUE);
            T_TRUE(race.got[i] == A);
            T_EQ(race.done_at_return[i], 1);                         /* nobody got a module whose entry point was still running */
            T_EQ(race.state_at_return[i], RT_LDR_LOADED);
        }
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_EQ(entries('a', DLL_PROCESS_ATTACH), 1);
        T_EQ(entries('b', DLL_PROCESS_ATTACH), 1);
        T_EQ(entries('c', DLL_PROCESS_ATTACH), 1);
        T_EQ(A->load_count, 2);
        T_EQ(B->load_count, 1);
        T_EQ(C->load_count, 1);
        T_EQ(LA->state, RT_LDR_LOADED);
        T_EQ(rt_ldr_order_count(), 3);
        /* Both threads were made before the load (no attach call) and ended after it. */
        T_EQ(entries('a', DLL_THREAD_ATTACH), 0);
        T_EQ(entries('a', DLL_THREAD_DETACH), 2);
        T_EQ(entries('c', DLL_THREAD_DETACH), 2);
        T_EQ(rt_ldr_thread_count(), 0);                              /* guest thread 1 loaded nothing */
    }
}

/* ---- loader_load_fail: case 12 ---------------------------------------------------------------- */

TEST_SETUP(core, loader_load_fail) { write_slots(W_CHAIN); }
TEST(core, loader_load_fail)
{
    MODULES;
    ret_val[1] = 0;                                                  /* b's entry point returns FALSE */

    T_PLAN(12, "an entry point returns FALSE: failed for good, out of the order, never run again") {
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_DLL_INIT_FAILED);
        T_EQ(elog.n, 2);
        T_TRUE(is_call(0, 'e', 'c', C, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(1, 'e', 'b', B, DLL_PROCESS_ATTACH, me));
        T_EQ(LB->state, RT_LDR_FAILED);
        T_EQ(LA->state, RT_LDR_FAILED);
        T_EQ(LC->state, RT_LDR_LOADED);
        T_EQ(rt_ldr_order_count(), 1);
        T_TRUE(rt_ldr_order_at(0) == LC);
        T_TRUE(B->loaded && A->loaded);
        T_EQ(A->load_count, 0);
        T_EQ(B->load_count, 0);
        T_EQ(C->load_count, 1);
        T_EQ(LD64(A->base + A_SLOT_B), UNBOUND);                     /* a never got as far as its slots */
        T_TRUE(rt_module_handle_a("t_lift_b.dll") == B);             /* GetModuleHandle keeps finding it */
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_b.dll: its entry point returned FALSE"));
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_a.dll: not loaded: t_lift_b.dll, which it imports from, could not be loaded"));

        ret_val[1] = 1;                                              /* it would succeed now: it is not asked */
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_b.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_DLL_INIT_FAILED);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load_a(ctx, "t_lift_a", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_DLL_INIT_FAILED);
        T_EQ(elog.n, 2);
        T_EQ(LB->state, RT_LDR_FAILED);
        T_EQ(rt_ldr_order_count(), 1);
        T_EQ(B->load_count, 0);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "an earlier load of t_lift_b.dll failed"));
    }
    T_PLAN(25, "a failed module gets no thread calls") {
        rt_loader_thread_attach(ctx);
        T_EQ(elog.n, 3);
        T_TRUE(is_call(2, 'e', 'c', C, DLL_THREAD_ATTACH, me));
        rt_loader_thread_detach(ctx);
        T_EQ(elog.n, 4);
        T_TRUE(is_call(3, 'e', 'c', C, DLL_THREAD_DETACH, me));
    }
}

/* ---- loader_load_bool: case 13 ---------------------------------------------------------------- */

TEST_SETUP(core, loader_load_bool) { write_slots(W_CHAIN); }
TEST(core, loader_load_bool)
{
    MODULES;
    ret_val[2] = UINT64_C(0xFFFFFFFF00000001);                       /* c: TRUE under garbage */
    ret_val[1] = UINT64_C(0x100000000);                              /* b: FALSE under garbage */

    T_PLAN(13, "the BOOL is the low 32 bits of rax") {
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_b.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_DLL_INIT_FAILED);
        T_EQ(elog.n, 2);
        T_EQ(LC->state, RT_LDR_LOADED);
        T_EQ(C->load_count, 1);
        T_EQ(LB->state, RT_LDR_FAILED);
        T_EQ(rt_ldr_order_count(), 1);
        T_TRUE(rt_ldr_order_at(0) == LC);
        /* The same through rt_ldr_call_entry, whose result the thread calls ignore. */
        rt_loader_lock();
        ret_val[2] = UINT64_C(0x100000000);
        T_EQ(rt_ldr_call_entry(ctx, LC, DLL_PROCESS_ATTACH), FALSE);
        ret_val[2] = UINT64_C(0xABCD000000000002);
        T_EQ(rt_ldr_call_entry(ctx, LC, DLL_PROCESS_ATTACH), TRUE);
        rt_loader_unlock();
        T_TRAPS((rt_loader_lock(), rt_ldr_call_entry(ctx, LA, DLL_PROCESS_ATTACH)), "the image is not built");
    }
}

/* ---- loader_load_reentry: case 14 ------------------------------------------------------------- */

static struct {
    RtModule *c_from_c, *a_from_b, *corpus_from_a;
    int32_t   c_count_in_c, a_count_in_b;
    bool      mark_back_in_c, mark_back_in_a;
    BOOL      b_disabled;
    unsigned  log_in_b, order_in_a;
} re;

static bool marked(Ctx *ctx, const char *module, DWORD reason) {
    const RtModule *m = NULL;
    DWORD r = 99;
    return rt_loader_in_entry(RT_THREAD(ctx), &m, &r) && m == rt_module_find(module) && r == reason;
}

static void re_in_c(Ctx *ctx, DWORD reason) {
    if (reason != DLL_PROCESS_ATTACH) return;
    re.c_from_c = rt_module_load(ctx, u"t_lift_c.dll", 0);           /* its own module */
    re.c_count_in_c = rt_module_find("t_lift_c.dll")->load_count;
    re.mark_back_in_c = marked(ctx, "t_lift_c.dll", DLL_PROCESS_ATTACH);
}

static void re_in_b(Ctx *ctx, DWORD reason) {
    if (reason != DLL_PROCESS_ATTACH) return;
    re.a_from_b = rt_module_load_a(ctx, "t_lift_a.dll", 0);          /* the module whose load is loading b */
    re.a_count_in_b = rt_module_find("t_lift_a.dll")->load_count;
    /* As the shim will: the handle, then the module it names (found although it is still loading). */
    RtModule *self = rt_module_from_handle((HMODULE)(uintptr_t)rt_module_find("t_lift_b.dll")->base);
    re.b_disabled = self ? rt_module_disable_thread_calls(self) : 99;
    re.log_in_b = elog.n;
}

static void re_in_a(Ctx *ctx, DWORD reason) {
    if (reason != DLL_PROCESS_ATTACH) return;
    re.corpus_from_a = rt_module_load(ctx, u"corpus_O1", 0);         /* a module nobody has loaded */
    re.mark_back_in_a = marked(ctx, "t_lift_a.dll", DLL_PROCESS_ATTACH);
    re.order_in_a = rt_ldr_order_count();
}

TEST_SETUP(core, loader_load_reentry) { write_slots(W_CHAIN); }
TEST(core, loader_load_reentry)
{
    MODULES;
    RtModule *corpus = rt_module_find("corpus_O1.dll");
    T_REQUIRE(corpus != NULL);
    inside[0] = re_in_a;
    inside[1] = re_in_b;
    inside[2] = re_in_c;

    T_PLAN(14, "loads from inside entry points: no deadlock, no second entry call") {
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == A);
        T_EQ(elog.n, 3);
        T_TRUE(is_call(0, 'e', 'c', C, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(1, 'e', 'b', B, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(2, 'e', 'a', A, DLL_PROCESS_ATTACH, me));
        T_TRUE(re.c_from_c == C);
        T_EQ(re.c_count_in_c, 1);
        T_EQ(C->load_count, 2);
        T_TRUE(re.mark_back_in_c);
        T_TRUE(re.a_from_b == A);                                    /* still LOADING: returned, not run */
        T_EQ(re.a_count_in_b, 1);
        T_EQ(re.log_in_b, 2);
        T_EQ(A->load_count, 2);
        T_EQ(B->load_count, 1);
        T_EQ(LA->state, RT_LDR_LOADED);
    }
    T_PLAN(26, "a module an entry point loads comes after its loader in the order") {
        T_TRUE(re.corpus_from_a == corpus);
        T_TRUE(re.mark_back_in_a);
        T_EQ(re.order_in_a, 4);
        T_EQ(rt_ldr_order_count(), 4);
        T_TRUE(rt_ldr_order_at(0) == LC && rt_ldr_order_at(1) == LB && rt_ldr_order_at(2) == LA);
        T_TRUE(rt_ldr_order_at(3) == rt_ldr_module(corpus));
        T_EQ(corpus->load_count, 1);
    }
    T_PLAN(30, "DisableThreadLibraryCalls from the module's own DLL_PROCESS_ATTACH, where modules call it") {
        T_EQ(re.b_disabled, TRUE);
        rt_loader_thread_attach(ctx);
        rt_loader_thread_detach(ctx);
        T_EQ(entries('b', DLL_THREAD_ATTACH) + entries('b', DLL_THREAD_DETACH), 0);
        T_EQ(entries('c', DLL_THREAD_ATTACH) + entries('a', DLL_THREAD_ATTACH), 2);
        T_EQ(entries('c', DLL_THREAD_DETACH) + entries('a', DLL_THREAD_DETACH), 2);
    }
}

/* ---- loader_load_cycle: case 15, and an import cycle ------------------------------------------ */

static struct { uint64_t b_slot_a, a_slot_b_in_b, a_slot_b_in_a; int a_state_in_b; } cyc;

static void cyc_in_b(Ctx *ctx, DWORD reason) {
    (void)ctx;
    if (reason != DLL_PROCESS_ATTACH) return;
    RtModule *A = rt_module_find("t_lift_a.dll");
    cyc.b_slot_a = LD64(rt_module_find("t_lift_b.dll")->base + B_SLOT);
    cyc.a_slot_b_in_b = LD64(A->base + A_SLOT_B);
    cyc.a_state_in_b = (int)rt_ldr_module(A)->state;
}

static void cyc_in_a(Ctx *ctx, DWORD reason) {
    (void)ctx;
    if (reason == DLL_PROCESS_ATTACH) cyc.a_slot_b_in_a = LD64(rt_module_find("t_lift_a.dll")->base + A_SLOT_B);
}

TEST_SETUP(core, loader_load_cycle) { write_slots(W_C_FILE | W_CYCLE); }
TEST(core, loader_load_cycle)
{
    MODULES;
    inside[0] = cyc_in_a;
    inside[1] = cyc_in_b;

    T_PLAN(15, "a module without an entry point is loaded without a call") {
        T_EQ(LC->img.mapped, false);
        T_TRUE(rt_module_load(ctx, u"t_lift_c.dll", 0) == C);
        T_EQ(elog.n, 0);
        T_EQ(LC->state, RT_LDR_LOADED);
        T_EQ(C->load_count, 1);
        T_EQ(rt_ldr_order_count(), 1);
        T_TRUE(rt_ldr_order_at(0) == LC);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_c.dll: loaded at"));
        T_TRUE(T_LOGGED(RT_CH_LOADER, "(it has no entry point)"));
        rt_loader_thread_attach(ctx);                                /* nor a thread call */
        rt_loader_thread_detach(ctx);
        T_EQ(elog.n, 0);
    }
    T_PLAN(27, "a imports from b and b from a: both load, each entry point once, b's first") {
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == A);
        T_EQ(elog.n, 2);
        T_TRUE(is_call(0, 'e', 'b', B, DLL_PROCESS_ATTACH, me));
        T_TRUE(is_call(1, 'e', 'a', A, DLL_PROCESS_ATTACH, me));
        T_EQ(cyc.b_slot_a, A->base + F(1));                          /* bound to a module that was still loading */
        T_EQ(cyc.a_state_in_b, RT_LDR_LOADING);
        T_EQ(cyc.a_slot_b_in_b, UNBOUND);                            /* a's own slots waited for b */
        T_EQ(cyc.a_slot_b_in_a, B->base + F(1));
        T_EQ(A->load_count, 2);                                      /* b's import of a, and the load */
        T_EQ(B->load_count, 1);
        T_EQ(LA->state, RT_LDR_LOADED);
        T_EQ(LB->state, RT_LDR_LOADED);
        T_EQ(rt_ldr_order_count(), 3);
        T_TRUE(rt_ldr_order_at(1) == LB && rt_ldr_order_at(2) == LA);
    }
}

/* ---- loader_load_absent: case 16 -------------------------------------------------------------- */

TEST_SETUP(core, loader_load_absent) { write_slots(0); }
TEST(core, loader_load_absent)
{
    MODULES;

    T_PLAN(16, "an import whose DLL file is not there") {
        T_EQ(LC->state, RT_LDR_ABSENT);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_c.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "LoadLibrary(\"t_lift_c.dll\"): t_lift_c.dll has no DLL file in this run"));
        T_EQ(LC->state, RT_LDR_ABSENT);
        T_TRUE(!C->loaded);

        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_b.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        T_EQ(LB->state, RT_LDR_FAILED);
        T_TRUE(B->loaded);
        T_EQ(LD64(B->base + B_SLOT), UNBOUND);
        T_TRUE(T_LOGGED(RT_CH_LOADER, "import of t_lift_c.dll: t_lift_c.dll has no DLL file in this run"));
        T_TRUE(T_LOGGED(RT_CH_LOADER, "t_lift_b.dll: not loaded: t_lift_c.dll, which it imports from, could not be loaded"));
        T_EQ(elog.n, 0);
        T_EQ(rt_ldr_order_count(), 0);

        /* From here on b is a module whose load failed, whatever the reason was. */
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_b.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_DLL_INIT_FAILED);
        rt_set_last_error(ctx, 0);
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_DLL_INIT_FAILED);
        T_EQ(LA->state, RT_LDR_FAILED);
        T_EQ(elog.n, 0);
    }
}

/* ---- loader_load_traps: case 17, and a thread that ends inside an entry point ----------------- */

enum { M_NONE, M_BOOM, M_RSP, M_EXIT_THREAD, M_DETACH };
static int trap_mode;

static void trap_in_b(Ctx *ctx, DWORD reason) {
    (void)reason;
    if (trap_mode == M_BOOM) rt_fatal("boom in an entry point");
    if (trap_mode == M_RSP) ctx->r[4] -= 8;                          /* returns with rsp 8 too low */
}

static void trap_in_c(Ctx *ctx, DWORD reason) {
    if (reason != DLL_THREAD_ATTACH) return;
    if (trap_mode == M_EXIT_THREAD) rt_thread_exit(ctx, 7);
    if (trap_mode == M_DETACH) rt_thread_detach(RT_THREAD(ctx));
}

GUEST_FN(exiting_start) {
    note(ctx, 's', 0);
    rt_thread_exit(ctx, 9);                                          /* ExitThread from the start routine: allowed */
}

static void *attaching_host(void *arg) {
    (void)arg;
    RtThread *t = rt_thread_attach("load-traps");                    /* DLL_THREAD_ATTACH to c, whose entry point detaches */
    rt_thread_detach(t);
    return NULL;
}

static void run_attached(void) {
    pthread_t th;
    if (pthread_create(&th, NULL, attaching_host, NULL) == 0) pthread_join(th, NULL);
}

TEST_SETUP(core, loader_load_traps) { write_slots(W_CHAIN); }
TEST(core, loader_load_traps)
{
    MODULES;
    inside[1] = trap_in_b;
    inside[2] = trap_in_c;
    T_REQUIRE(rt_module_load(ctx, u"t_lift_c.dll", 0) == C);

    T_PLAN(17, "a fatal report inside an entry point names the module; an entry point that moves rsp is one") {
        T_TRAPS((trap_mode = M_BOOM, rt_module_load(ctx, u"t_lift_b.dll", 0)), "boom in an entry point");
        T_TRAPS((trap_mode = M_BOOM, rt_module_load(ctx, u"t_lift_b.dll", 0)), "inside the entry point of t_lift_b.dll (reason 1)");
        T_TRAPS((trap_mode = M_RSP, rt_module_load(ctx, u"t_lift_b.dll", 0)), "rsp");
        T_EQ(LB->state, RT_LDR_UNLOADED);                            /* all of that was in forked children */
    }
    T_PLAN(28, "ExitThread outside an entry point ends the thread, with its detach calls") {
        DWORD code = 0;
        uint32_t id = run_thread(ctx, T_GUEST_ADDR(exiting_start), &code);
        T_REQUIRE(id != 0);
        T_EQ(code, 9);
        T_EQ(elog.n, 4);
        T_TRUE(is_call(1, 'e', 'c', C, DLL_THREAD_ATTACH, id));
        T_TRUE(elog.c[2].kind == 's' && elog.c[2].thread == id);
        T_TRUE(is_call(3, 'e', 'c', C, DLL_THREAD_DETACH, id));
        T_EQ(rt_ldr_thread_count(), 1);
    }
    T_PLAN(29, "a thread that ends inside an entry point is a stop that names the module and the reason") {
        T_TRUE(EVENTUALLY(rt_thread_host_threads() == 0));
        T_TRAPS((trap_mode = M_EXIT_THREAD, run_thread(ctx, T_GUEST_ADDR(thread_start), NULL)), "ExitThread on thread 0x");
        T_TRAPS((trap_mode = M_EXIT_THREAD, run_thread(ctx, T_GUEST_ADDR(thread_start), NULL)),
                "inside the entry point or a TLS callback of t_lift_c.dll (reason 2): that is not supported");
        T_TRAPS((trap_mode = M_DETACH, run_attached()),
                "ends inside the entry point or a TLS callback of t_lift_c.dll (reason 2): that is not supported");
        /* The loader lock is free and the loader works: nothing above happened in this process. */
        T_TRUE(rt_module_load(ctx, u"t_lift_a.dll", 0) == A);
        T_EQ(rt_ldr_order_count(), 3);
    }
}
