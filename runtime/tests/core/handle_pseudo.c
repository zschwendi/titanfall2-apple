/* Pseudo-handles (RUNTIME.md 4.1): (HANDLE)-1 is the current process, (HANDLE)-2 the current
 * thread.  rt_handle_ref resolves them wherever the kind allows, closing one has no effect,
 * duplicating one makes a real handle.  The process and thread objects belong to the thread
 * unit, which fills two hooks in rt_init (rt_thread_attach_main); an empty hook is loud:
 *   process  rt_handle_set_process_object (rt/handle.h)
 *   thread   RtThread.sub[RT_SUB_HANDLE], the slot rt/thread.h reserves for it.
 * These tests put objects of their own into the thread's slot, which is a borrowed pointer. */
#include <pthread.h>
#include <string.h>

#include "rt/test.h"
#include "rt/handle.h"

typedef struct TObj {
    RtObject obj;
    uint32_t canary;
} TObj;

#define ALIVE 0xA11FE000u
#define PSEUDO_PROCESS ((HANDLE)(intptr_t)-1)
#define PSEUDO_THREAD  ((HANDLE)(intptr_t)-2)
#define SENTINEL 0x5EA1u

static unsigned destroyed_count;
static void tobj_destroy(RtObject *o) { ((TObj *)o)->canary = 0; destroyed_count++; }
static const RtObjectOps tops = { .name = "test", .destroy = tobj_destroy };

static TObj *mk(RtKind kind) {
    TObj *t = (TObj *)rt_object_new(kind, sizeof *t, &tops, NULL, NULL);
    T_TRUE(t != NULL);
    if (t) t->canary = ALIVE;
    return t;
}
static uint32_t refs_of(RtObject *o) { return __atomic_load_n(&o->refs, __ATOMIC_SEQ_CST); }
static void plant_error(void) { rt_set_last_error(T_CTX(), SENTINEL); }

static void *ref_thread_pseudo(void *arg) {
    (void)arg;
    rt_handle_ref(PSEUDO_THREAD, RT_K_ANY);
    return NULL;
}
static void ref_thread_pseudo_on_host_thread(void) {
    pthread_t th;
    if (pthread_create(&th, NULL, ref_thread_pseudo, NULL)) return;
    pthread_join(th, NULL);
}
static void set_wrong_kind_in_thread_slot(void) {
    RT_THREAD(T_CTX())->sub[RT_SUB_HANDLE] = &mk(RT_K_EVENT)->obj;
    rt_handle_ref(PSEUDO_THREAD, RT_K_ANY);
}
static void set_garbage_in_thread_slot(void) {
    static uint64_t not_an_object[8];
    RT_THREAD(T_CTX())->sub[RT_SUB_HANDLE] = (RtObject *)&not_an_object[4];
    rt_handle_ref(PSEUDO_THREAD, RT_K_ANY);
}

TEST(core, handle_pseudo_unset)
{
    /* The thread unit has filled both hooks by the time a test runs.  The process object
     * cannot be taken away again (the report for an unregistered one is no longer reachable);
     * the thread's slot can be emptied, and resolving the pseudo-handle is then fatal and
     * says which hook is empty.  Nothing is guessed. */
    RtObject *own = RT_THREAD(T_CTX())->sub[RT_SUB_HANDLE];
    T_TRUE(own != NULL && own->kind == RT_K_THREAD);
    RT_THREAD(T_CTX())->sub[RT_SUB_HANDLE] = NULL;
    T_TRAPS(rt_handle_ref(PSEUDO_THREAD, RT_K_ANY), "RtThread.sub[RT_SUB_HANDLE]");
    T_TRAPS(rt_handle_ref(PSEUDO_THREAD, RT_K_THREAD), "(HANDLE)-2, the current-thread pseudo-handle");
    T_TRAPS(rt_handle_dup(PSEUDO_THREAD), "(HANDLE)-2");
    T_TRAPS(ref_thread_pseudo_on_host_thread(), "not a guest thread");

    /* A kind that cannot match is a plain failure, whether or not the hook is filled. */
    plant_error();
    T_TRUE(rt_handle_ref(PSEUDO_PROCESS, RT_K_EVENT) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    plant_error();
    T_TRUE(rt_handle_ref(PSEUDO_THREAD, RT_K_PROCESS) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* Closing needs no object: it has no effect and succeeds. */
    plant_error();
    T_EQ(rt_handle_close(PSEUDO_PROCESS), TRUE);
    T_EQ(rt_handle_close(PSEUDO_THREAD), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(rt_handle_close_kind(PSEUDO_PROCESS, RT_K_PROCESS), TRUE);
    T_EQ(rt_handle_close_kind(PSEUDO_THREAD, RT_K_THREAD), TRUE);
    plant_error();
    T_EQ(rt_handle_close_kind(PSEUDO_PROCESS, RT_K_THREAD), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    plant_error();
    T_EQ(rt_handle_close_kind(PSEUDO_THREAD, RT_K_FIND), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    /* Only the full 64-bit -1 and -2 are pseudo-handles: the zero-extended 32-bit values are
     * "negative" handles that no slot has. */
    HANDLE not_pseudo[] = { (HANDLE)(uintptr_t)0xFFFFFFFFu, (HANDLE)(uintptr_t)0xFFFFFFFEu,
                            (HANDLE)(intptr_t)-3, (HANDLE)(intptr_t)-4, (HANDLE)(intptr_t)-8 };
    for (unsigned i = 0; i < sizeof not_pseudo / sizeof *not_pseudo; i++) {
        plant_error();
        T_REQUIRE(rt_handle_ref(not_pseudo[i], RT_K_ANY) == NULL);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        plant_error();
        T_REQUIRE(rt_handle_close(not_pseudo[i]) == FALSE);
        T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
    }

    /* Registering the process object. */
    TObj *ev = mk(RT_K_EVENT);
    T_TRAPS(rt_handle_set_process_object(&ev->obj), "is of kind RT_K_EVENT, not RT_K_PROCESS");
    T_TRAPS(rt_handle_set_process_object(NULL), "NULL object");
    T_TRAPS(rt_handle_ref(PSEUDO_PROCESS, (RtKind)RT_K_COUNT), "is not an object kind");
}

TEST(core, handle_pseudo)
{
    /* The process object is the one the thread unit registered in rt_init. */
    RtObject *proc_obj = rt_handle_ref(PSEUDO_PROCESS, RT_K_PROCESS);
    TObj *thr = mk(RT_K_THREAD);
    if (!proc_obj || !thr) return;
    rt_object_unref(proc_obj);
    T_EQ(refs_of(proc_obj), 2);                     /* the thread unit's and the table's */
    RT_THREAD(T_CTX())->sub[RT_SUB_HANDLE] = &thr->obj;     /* borrowed: the thread unit's reference is ours */

    /* The process and the thread resolve where the kind allows. */
    plant_error();
    RtObject *r = rt_handle_ref(PSEUDO_PROCESS, RT_K_ANY);
    T_TRUE(r == proc_obj);
    T_EQ(refs_of(r), 3);
    T_EQ(T_LAST_ERROR(), SENTINEL);                 /* success leaves last error alone */
    rt_object_unref(r);
    r = rt_handle_ref(PSEUDO_PROCESS, RT_K_PROCESS);
    T_TRUE(r == proc_obj);
    rt_object_unref(r);
    r = rt_handle_ref(PSEUDO_THREAD, RT_K_ANY);
    T_TRUE(r == &thr->obj);
    T_EQ(refs_of(r), 2);
    rt_object_unref(r);
    r = rt_handle_ref(PSEUDO_THREAD, RT_K_THREAD);
    T_TRUE(r == &thr->obj);
    rt_object_unref(r);

    /* Every other kind is refused. */
    for (unsigned k = 1; k < RT_K_COUNT; k++) {
        if (k != RT_K_PROCESS) {
            plant_error();
            T_REQUIRE(rt_handle_ref(PSEUDO_PROCESS, (RtKind)k) == NULL);
            T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        }
        if (k != RT_K_THREAD) {
            plant_error();
            T_REQUIRE(rt_handle_ref(PSEUDO_THREAD, (RtKind)k) == NULL);
            T_REQUIRE(T_LAST_ERROR() == ERROR_INVALID_HANDLE);
        }
    }
    T_EQ(refs_of(proc_obj), 2);
    T_EQ(refs_of(&thr->obj), 1);

    /* Closing has no effect on the object. */
    plant_error();
    T_EQ(rt_handle_close(PSEUDO_PROCESS), TRUE);
    T_EQ(rt_handle_close(PSEUDO_THREAD), TRUE);
    T_EQ(rt_handle_close_kind(PSEUDO_PROCESS, RT_K_PROCESS), TRUE);
    T_EQ(rt_handle_close_kind(PSEUDO_THREAD, RT_K_THREAD), TRUE);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    T_EQ(refs_of(proc_obj), 2);
    T_EQ(refs_of(&thr->obj), 1);
    T_EQ(destroyed_count, 0);
    T_EQ(rt_handle_close_kind(PSEUDO_PROCESS, RT_K_THREAD), FALSE);
    T_EQ(rt_handle_close_kind(PSEUDO_THREAD, RT_K_PROCESS), FALSE);

    /* DuplicateHandle of a pseudo-handle makes a real handle to the real object. */
    plant_error();
    HANDLE hp = rt_handle_dup(PSEUDO_PROCESS);
    T_TRUE(hp != NULL && hp != PSEUDO_PROCESS);
    T_EQ(T_LAST_ERROR(), SENTINEL);
    uint64_t v = (uint64_t)(uintptr_t)hp;
    T_TRUE(v != 0 && (v & 3) == 0 && (v >> 31) == 0);
    T_EQ(refs_of(proc_obj), 3);
    r = rt_handle_ref(hp, RT_K_PROCESS);
    T_TRUE(r == proc_obj);
    rt_object_unref(r);
    HANDLE ht = rt_handle_dup(PSEUDO_THREAD);
    T_TRUE(ht != NULL && ht != PSEUDO_THREAD && ht != hp);
    T_EQ(refs_of(&thr->obj), 2);
    r = rt_handle_ref(ht, RT_K_THREAD);
    T_TRUE(r == &thr->obj);
    rt_object_unref(r);

    /* The real handle closes like any other and the object lives on. */
    T_EQ(rt_handle_close(hp), TRUE);
    T_EQ(refs_of(proc_obj), 2);
    T_EQ(rt_handle_close(hp), FALSE);
    T_EQ(destroyed_count, 0);

    /* The duplicate outlives the thread's own reference: clear the slot and drop it. */
    RT_THREAD(T_CTX())->sub[RT_SUB_HANDLE] = NULL;
    rt_object_unref(&thr->obj);
    T_EQ(destroyed_count, 0);
    r = rt_handle_ref(ht, RT_K_THREAD);
    T_TRUE(r == &thr->obj);
    rt_object_unref(r);
    T_EQ(rt_handle_close(ht), TRUE);
    T_EQ(destroyed_count, 1);                       /* the thread object, now that nothing refers to it */

    /* With the slot empty again, the thread pseudo-handle is loud once more. */
    T_TRAPS(rt_handle_ref(PSEUDO_THREAD, RT_K_ANY), "RtThread.sub[RT_SUB_HANDLE]");
    /* The process object can be registered once. */
    TObj *proc2 = mk(RT_K_PROCESS);
    T_TRAPS(rt_handle_set_process_object(&proc2->obj), "already registered");
    /* A slot with something other than a thread object is a bug of the thread unit. */
    T_TRAPS(set_wrong_kind_in_thread_slot(), "holds an object of kind RT_K_EVENT, not RT_K_THREAD");
    T_TRAPS(set_garbage_in_thread_slot(), "is not a live object made by rt_object_new");
}
