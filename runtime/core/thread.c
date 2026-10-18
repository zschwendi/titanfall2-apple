/* Guest threads (docs/RUNTIME.md sections 6.1 to 6.3).  Interface: rt/thread.h; what the
 * thread unit's files share: thread_priv.h.
 *
 * Here: the process block and the process object, guest thread 1, the list of running
 * threads with its lock, the accessors, and the building blocks every way of making or
 * ending a thread uses (rt_thread_new, rt_thread_bind, rt_thread_finish).  CreateThread and
 * ExitThread themselves are in thread_run.c.
 */
#include <mach/mach.h>
#include <mach/mach_traps.h>
#include <os/lock.h>
#include <stdlib.h>
#include <string.h>

#include "thread_priv.h"
#include "rt/heap.h"
#include "rt/machine.h"
#include "rt/time.h"
#include "rt/vm.h"

#define MAIN_STACK_SIZE   (UINT64_C(8) << 20)     /* section 5.3: 8 MiB for the first thread */
#define STACK_GUARD       UINT64_C(0x10000)       /* 64 KB that is never accessible, below the stack */

static _Thread_local RtThread *tl_self;
static RtThread     main_thread;
static RtThreadPriv main_priv;
static uint64_t     process_block;
static RtProcessObject *process_object;

/* The thread lock (rt/thread.h, LOCKS) and what it guards here. */
static os_unfair_lock list_lock = OS_UNFAIR_LOCK_INIT;
static RtThread *all_threads;           /* running guest threads, newest first */
static unsigned  n_threads;
static uint32_t  next_id = RT_FIRST_THREAD_ID + 4;      /* atomic; ids are never reused */

void rt_thread_list_lock(void)   { os_unfair_lock_lock(&list_lock); }
void rt_thread_list_unlock(void) { os_unfair_lock_unlock(&list_lock); }
RtThread *rt_thread_list_head(void) { os_unfair_lock_assert_owner(&list_lock); return all_threads; }
bool rt_thread_list_trylock(void) { return os_unfair_lock_trylock(&list_lock); }
RtThread *rt_thread_list_head_unlocked(void) { return all_threads; }

static void list_add(RtThread *t) {
    rt_thread_list_lock();
    t->next = all_threads;
    all_threads = t;
    n_threads++;
    rt_thread_list_unlock();
}

static void list_remove(RtThread *t) {
    rt_thread_list_lock();
    RtThread **at = &all_threads;
    while (*at && *at != t) at = &(*at)->next;
    if (!*at) rt_fatal("thread 0x%x is not in the list of running threads", t->id);
    *at = t->next;
    t->next = NULL;
    n_threads--;
    rt_thread_list_unlock();
}

void rt_thread_for_each(void (*fn)(RtThread *t, void *user), void *user) {
    if (!fn) rt_fatal("rt_thread_for_each: no function");
    rt_thread_list_lock();
    for (RtThread *t = all_threads; t; t = t->next) fn(t, user);
    rt_thread_list_unlock();
}

unsigned rt_thread_count(void) {
    rt_thread_list_lock();
    unsigned n = n_threads;
    rt_thread_list_unlock();
    return n;
}

/* Hooks other units fill (rt/thread.h).  Weak, doing nothing, until the owner defines them.
 * The two loader hooks, rt_loader_thread_attach and rt_loader_thread_detach, have no weak
 * definition: the loader defines them (core/loader_load.c), and a thread's static TLS
 * blocks are made and freed there, so linking without them must fail. */
__attribute__((weak)) void rt_crash_thread_enter(RtThread *t)   { (void)t; }
__attribute__((weak)) void rt_crash_thread_leave(RtThread *t)   { (void)t; }
__attribute__((weak)) void rt_file_thread_exit(RtThread *t)     { (void)t; }
/* Last error of a host thread that is not a guest thread: start-up before thread 1 exists,
 * and the host threads of tests.  Per host thread, so two such threads do not race on it. */
static _Thread_local DWORD early_last_error;

RtThread *rt_thread_self(void) { return tl_self; }

Ctx *rt_main_ctx(void) {
    if (!main_thread.teb) rt_fatal("rt_main_ctx before rt_init");
    return &main_thread.ctx;
}

void rt_thread_set_last_error(DWORD e) {
    RtThread *t = tl_self;
    if (t) rt_set_last_error(&t->ctx, e);
    else early_last_error = e;
}

DWORD rt_thread_get_last_error(void) {
    RtThread *t = tl_self;
    return t ? rt_get_last_error(&t->ctx) : early_last_error;
}

void rt_thread_set_name(RtThread *t, const char *name) {
    size_t n = strlen(name);
    if (n >= sizeof t->name) n = sizeof t->name - 1;
    memcpy(t->name, name, n);
    t->name[n] = 0;
    if (t == tl_self) pthread_setname_np(t->name);
}

static uint64_t alloc_or_die(uint64_t addr, uint64_t size, DWORD type, DWORD protect, const char *what) {
    uint64_t p = rt_vm_alloc(addr, size, type, protect, RT_VM_PRIVATE);
    if (!p) rt_fatal("cannot allocate %s (%llu bytes): Win32 error %u", what, (unsigned long long)size,
                     rt_thread_get_last_error());
    return p;
}

/* The process block (section 6.2): zero except for the fields the guest is known to read.
 * Created early so that each subsystem writes its own field from its own init function:
 * the module registry the image base, the heap unit the process heap. */
void rt_process_block_init(void) {
    const RtMachine *mc = rt_machine();
    if (process_block) rt_fatal("rt_process_block_init called twice");
    process_block = alloc_or_die(0, RT_PEB_REGION_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, "the process block");
    ST8(process_block + RT_PEB_BEING_DEBUGGED, 0);
    ST32(process_block + RT_PEB_NUMBER_OF_PROCESSORS, mc->cpu_count);
    ST32(process_block + RT_PEB_OS_MAJOR_VERSION, mc->os_major);
    ST32(process_block + RT_PEB_OS_MINOR_VERSION, mc->os_minor);
    ST16(process_block + RT_PEB_OS_BUILD_NUMBER, (uint16_t)mc->os_build);
    ST16(process_block + RT_PEB_OS_CSD_VERSION, (uint16_t)(mc->sp_major << 8 | mc->sp_minor));
    ST32(process_block + RT_PEB_OS_PLATFORM_ID, 2);
}

uint64_t rt_process_block(void) {
    if (!process_block) rt_fatal("rt_process_block before rt_process_block_init");
    return process_block;
}

/* Free the thread block, the guest stack and the TLS expansion array of `t`, as far as they
 * exist.  After it the thread has no guest side: teb and gs_base are 0. */
static void free_guest_side(RtThread *t) {
    if (t->teb) {
        uint64_t expansion = LD64(t->teb + RT_TEB_TLS_EXPANSION);
        if (expansion) rt_gfree((void *)(uintptr_t)expansion);
    }
    if (t->stack_region && !rt_vm_free(t->stack_region, 0, MEM_RELEASE))
        rt_fatal("cannot free the guest stack of thread 0x%x: Win32 error %u", t->id, rt_thread_get_last_error());
    if (t->teb && !rt_vm_free(t->teb, 0, MEM_RELEASE))
        rt_fatal("cannot free the thread block of thread 0x%x: Win32 error %u", t->id, rt_thread_get_last_error());
    t->teb = t->stack_region = t->stack_limit = t->stack_base = 0;
    t->ctx.gs_base = 0;
}

/* Thread block and guest stack for `t` (sections 5.3 and 6.2).  False, with last error set
 * by the VM unit and nothing allocated, if the arena cannot give them. */
static bool make_guest_side(RtThread *t, uint64_t stack_size) {
    t->teb = rt_vm_alloc(0, RT_TEB_REGION_SIZE, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    if (!t->teb) return false;

    /* One region: a guard that is reserved and never committed, then the stack, committed
     * whole.  Nothing is committed on demand; a real overflow walks into the guard. */
    t->stack_region = rt_vm_alloc(0, STACK_GUARD + stack_size, MEM_RESERVE, PAGE_NOACCESS, RT_VM_PRIVATE);
    if (!t->stack_region ||
        !rt_vm_alloc(t->stack_region + STACK_GUARD, stack_size, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE)) {
        DWORD e = rt_thread_get_last_error();
        free_guest_side(t);
        rt_thread_set_last_error(e);
        return false;
    }
    t->stack_limit = t->stack_region + STACK_GUARD;
    t->stack_base = t->stack_limit + stack_size;

    ST64(t->teb + RT_TEB_STACK_BASE, t->stack_base);
    ST64(t->teb + RT_TEB_STACK_LIMIT, t->stack_limit);
    ST64(t->teb + RT_TEB_SELF, t->teb);
    ST64(t->teb + RT_TEB_PROCESS_ID, RT_PROCESS_ID);
    ST64(t->teb + RT_TEB_THREAD_ID, t->id);
    ST64(t->teb + RT_TEB_TLS_POINTER, t->teb + RT_TEB_STATIC_TLS_ARRAY);
    ST64(t->teb + RT_TEB_PEB, process_block);
    ST32(t->teb + RT_TEB_LAST_ERROR, 0);
    ST64(t->teb + RT_TEB_DEALLOC_STACK, t->stack_region);

    memset(&t->ctx, 0, sizeof t->ctx);
    t->ctx.mxcsr = TF_MXCSR_DEFAULT;
    t->ctx.fpcw = TF_FPCW_DEFAULT;
    t->ctx.df = 0;
    t->ctx.gs_base = t->teb;
    t->ctx.host = t;
    /* Leave a small frame's worth above the first return address, 16-byte aligned. */
    t->ctx.r[4] = t->stack_base - 0x40;
    return true;
}

/* The x86 memory-ordering mode is per host thread and not inherited (section 6.1). */
void rt_thread_cpu_modes(RtThread *t) {
    RtTsoMode mode = rt_config()->tso;
    const char *env = getenv("RT_TSO");
    if (env && *env) {
        if (!strcmp(env, "require")) mode = RT_TSO_REQUIRE;
        else if (!strcmp(env, "try")) mode = RT_TSO_TRY;
        else if (!strcmp(env, "off")) mode = RT_TSO_OFF;
        else rt_fatal("RT_TSO must be require, try or off, not '%s'", env);
    }
    tf_fp_enter_host();             /* host code runs with the host FPCR; guest calls switch */
    if (mode == RT_TSO_OFF) return;
    kern_return_t kr = thread_set_x86_64_compat(1);
    if (kr == KERN_SUCCESS) { t->tso = 1; return; }
    if (mode == RT_TSO_REQUIRE)
        rt_fatal("thread_set_x86_64_compat(1) was refused (kern_return %d).  Guest threads need the x86 "
                 "memory-ordering mode, which the kernel grants only to the entitled app bundle; "
                 "RT_TSO=try runs without it (tests only)", kr);
    rt_log(RT_CH_THREAD, RT_LL_INFO, "x86 memory-ordering mode refused for thread 0x%x (kern_return %d); "
           "continuing without it (RT_TSO=try)", t->id, kr);
}

/* ---- the two kernel objects ----------------------------------------------------------------- */

static bool thread_signaled(RtObject *obj, struct RtThread *waiter) {
    (void)waiter;
    return ((RtThreadObject *)obj)->exited != 0;
}

/* The last reference to a thread object is gone: the RtThread goes with it.  The thread has
 * ended or never ran (the unit holds a reference while it runs), so nothing uses the memory. */
static void thread_destroy(RtObject *obj) {
    RtThread *t = ((RtThreadObject *)obj)->thread;
    RtThreadPriv *p = RT_THREAD_PRIV(t);
    if (p->kind == RT_TK_MAIN) rt_fatal("the thread object of guest thread 1 was destroyed");
    pthread_cond_destroy(&p->gate_cond);
    pthread_mutex_destroy(&p->gate_lock);
    free(p->fls);
    free(t);                        /* one block: the RtThread and its RtThreadPriv */
}

static bool process_signaled(RtObject *obj, struct RtThread *waiter) { (void)obj; (void)waiter; return false; }

static const RtObjectOps thread_ops  = { .name = "thread", .destroy = thread_destroy, .is_signaled = thread_signaled };
static const RtObjectOps process_ops = { .name = "process", .is_signaled = process_signaled };

RtProcessObject *rt_process_object(void) {
    if (!process_object) rt_fatal("rt_process_object before rt_init");
    return process_object;
}

uint64_t rt_system_affinity(void) {
    uint32_t n = rt_machine()->cpu_count;
    return n >= 64 ? ~UINT64_C(0) : (UINT64_C(1) << n) - 1;
}

/* The thread object of `t`, with the unit's reference in its RtThreadPriv and the borrowed
 * pointer in sub[RT_SUB_HANDLE] (the rule in rt/thread.h).  False if there is no memory. */
static bool make_thread_object(RtThread *t) {
    RtThreadObject *o = (RtThreadObject *)rt_object_new(RT_K_THREAD, sizeof *o, &thread_ops, NULL, NULL);
    if (!o) return false;
    o->thread = t;
    o->id = t->id;
    o->exit_code = STILL_ACTIVE;
    o->priority = THREAD_PRIORITY_NORMAL;
    RT_THREAD_PRIV(t)->object = o;
    t->sub[RT_SUB_HANDLE] = &o->obj;
    return true;
}

/* ---- building blocks ------------------------------------------------------------------------ */

RtThread *rt_thread_new(uint64_t guest_stack, RtThreadKind kind) {
    struct Block { RtThread t; RtThreadPriv p; } *b = calloc(1, sizeof *b);
    if (!b) { rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
    RtThread *t = &b->t;
    RtThreadPriv *p = &b->p;
    t->sub[RT_SUB_THREAD] = p;
    p->kind = kind;
    pthread_mutex_init(&p->gate_lock, NULL);
    pthread_cond_init(&p->gate_cond, NULL);
    t->id = __atomic_fetch_add(&next_id, 4, __ATOMIC_SEQ_CST);
    if (!make_guest_side(t, guest_stack) || !make_thread_object(t)) {
        free_guest_side(t);
        pthread_cond_destroy(&p->gate_cond);
        pthread_mutex_destroy(&p->gate_lock);
        free(b);
        rt_thread_set_last_error(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    list_add(t);
    return t;
}

void rt_thread_discard(RtThread *t) {
    RtThreadPriv *p = RT_THREAD_PRIV(t);
    RtThreadObject *o = p->object;
    list_remove(t);
    free_guest_side(t);                         /* never attached to the loader: it owns no TLS block */
    t->sub[RT_SUB_HANDLE] = NULL;
    p->object = NULL;
    rt_object_unref(&o->obj);
}

void rt_thread_bind(RtThread *t) {
    if (tl_self) rt_fatal("rt_thread_bind: the host thread already is guest thread 0x%x", tl_self->id);
    tl_self = t;
    if (RT_THREAD_PRIV(t)) RT_THREAD_PRIV(t)->host_port = pthread_mach_thread_np(pthread_self());   /* for the wait report */
    if (t->name[0]) pthread_setname_np(t->name);
    rt_thread_cpu_modes(t);
    rt_crash_thread_enter(t);
}

void rt_thread_finish(RtThread *t, DWORD code) {
    RtThreadPriv *p = t ? RT_THREAD_PRIV(t) : NULL;
    if (!t || t != tl_self) rt_fatal("rt_thread_finish: not the calling thread's own RtThread");
    if (p->kind == RT_TK_MAIN) rt_fatal("rt_thread_finish: guest thread 1 ends only with the process");
    if (p->finishing) rt_fatal("rt_thread_finish: called twice for thread 0x%x", t->id);
    p->finishing = 1;
    const uint32_t id = t->id;

    rt_fls_thread_exit(&t->ctx);                /* 1: FLS callbacks */
    rt_loader_thread_detach(&t->ctx);           /* 2: DLL_THREAD_DETACH, the last guest code; frees the static TLS blocks */
    rt_wait_thread_exit(t);                     /* 3: abandon mutexes, drop APCs */
    rt_file_thread_exit(t);                     /* 4 */
    list_remove(t);                             /* 5 */
    free_guest_side(t);                         /* 6 */
    tl_self = NULL;

    RtThreadObject *o = p->object;
    rt_dispatcher_lock();                       /* 7: signalled, with the exit code */
    o->exited = 1;
    o->exit_code = code;
    rt_signal(&o->obj);
    rt_dispatcher_unlock();

    t->sub[RT_SUB_HANDLE] = NULL;               /* 8: the slot first, then the reference */
    p->object = NULL;
    rt_crash_thread_leave(t);
    rt_object_unref(&o->obj);                   /* may free t */
    rt_log(RT_CH_THREAD, RT_LL_INFO, "guest thread 0x%x ended with exit code %u", id, code);
}

/* ---- guest thread 1 ------------------------------------------------------------------------- */

void rt_thread_attach_main(RtTsoMode tso) {
    RtThread *t = &main_thread;
    (void)tso;                                  /* rt_thread_cpu_modes reads rt_config()->tso, the same value */
    if (t->teb) rt_fatal("rt_thread_attach_main called twice");
    t->id = RT_FIRST_THREAD_ID;
    t->is_main = 1;
    t->sub[RT_SUB_THREAD] = &main_priv;
    main_priv.kind = RT_TK_MAIN;
    pthread_mutex_init(&main_priv.gate_lock, NULL);
    pthread_cond_init(&main_priv.gate_cond, NULL);
    if (!make_guest_side(t, MAIN_STACK_SIZE))
        rt_fatal("cannot allocate the thread block and guest stack of guest thread 1: Win32 error %u",
                 rt_thread_get_last_error());
    DWORD early = early_last_error;
    tl_self = t;
    main_priv.host_port = pthread_mach_thread_np(pthread_self());           /* for the wait report */
    if (early) rt_set_last_error(&t->ctx, early);

    /* The process object behind (HANDLE)-1 and this thread's object behind (HANDLE)-2. */
    RtProcessObject *proc = (RtProcessObject *)rt_object_new(RT_K_PROCESS, sizeof *proc, &process_ops, NULL, NULL);
    if (!proc || !make_thread_object(t)) rt_fatal("no memory for the process and thread objects");
    proc->priority_class = NORMAL_PRIORITY_CLASS;
    proc->affinity = rt_system_affinity();
    proc->creation_time = rt_time_utc_100ns();  /* what GetProcessTimes reports as the creation time */
    process_object = proc;
    rt_handle_set_process_object(&proc->obj);

    list_add(t);
    rt_thread_cpu_modes(t);                     /* no loader hook: the loader records guest thread 1 itself */
    rt_crash_thread_enter(t);
    rt_log(RT_CH_THREAD, RT_LL_INFO, "guest thread 0x%x attached to the main host thread: thread block %#llx, "
           "stack %#llx-%#llx", t->id, (unsigned long long)t->teb, (unsigned long long)t->stack_limit,
           (unsigned long long)t->stack_base);
}
