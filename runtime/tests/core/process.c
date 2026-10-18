/* The process: its priority class and its affinity masks (core/process.c; the specification is
 * rt/thread.h, "the process").  Typed from the numbered plan of docs/handoff/rt-contract-thread.md;
 * the cases 1, 2 and 9 to 14 belong to other units, the numbers of the rest are kept.  Each case
 * is one T_PLAN block.  The cases of one TEST run in one process, in order.
 */
#include "rt/test.h"
#include "../../core/thread_priv.h"

#define SENTINEL 0x5EA1u

/* ---- the priority class ------------------------------------------------------------------- */

TEST(core, process)
{
    Ctx *ctx = T_CTX();

    T_PLAN(3, "the class starts as NORMAL and each of the six classes is kept") {
        const DWORD classes[6] = { 0x40, 0x4000, 0x20, 0x8000, 0x80, 0x100 };
        T_EQ(rt_process_get_priority_class(RT_PSEUDO_PROCESS), 0x20);
        for (unsigned i = 0; i < 6; i++) {
            rt_set_last_error(ctx, SENTINEL);
            T_TRUE(rt_process_set_priority_class(RT_PSEUDO_PROCESS, classes[i]));
            T_EQ(rt_process_get_priority_class(RT_PSEUDO_PROCESS), classes[i]);
            T_EQ(T_LAST_ERROR(), SENTINEL);
        }
    }

    T_PLAN(4, "a bad class is refused; an event handle is not a process") {
        const DWORD bad[3] = { 0, 0x21, 0x10 };
        T_TRUE(rt_process_set_priority_class(RT_PSEUDO_PROCESS, 0x80));    /* the class is 0x80 */
        for (unsigned i = 0; i < 3; i++) {
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_process_set_priority_class(RT_PSEUDO_PROCESS, bad[i]), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        }
        T_EQ(rt_process_get_priority_class(RT_PSEUDO_PROCESS), 0x80);

        HANDLE ev = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(ev != NULL);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_process_set_priority_class(ev, 0x80), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_process_get_priority_class(ev), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_PLAN(5, "the two process-mode background classes are fatal") {
        T_TRAPS(rt_process_set_priority_class(RT_PSEUDO_PROCESS, 0x00100000), "PROCESS_MODE_BACKGROUND");
        T_TRAPS(rt_process_set_priority_class(RT_PSEUDO_PROCESS, 0x00200000), "PROCESS_MODE_BACKGROUND");
    }
}

/* ---- the affinity masks: case 7 continues the state case 6 leaves --------------------------- */

TEST(core, process_affinity)
{
    Ctx *ctx = T_CTX();
    uint64_t sys = 0;
    DWORD_PTR pm = 0, sm = 0;

    T_PLAN(6, "the mask starts as the system mask; a mask of 1 is recorded") {
        sys = rt_system_affinity();
        T_TRUE(rt_process_get_affinity(RT_PSEUDO_PROCESS, &pm, &sm));
        T_EQ(pm, sys);
        T_EQ(sm, sys);

        rt_set_last_error(ctx, SENTINEL);
        T_TRUE(rt_process_set_affinity(RT_PSEUDO_PROCESS, 1));
        T_EQ(T_LAST_ERROR(), SENTINEL);
        T_TRUE(rt_process_get_affinity(RT_PSEUDO_PROCESS, &pm, &sm));
        T_EQ(pm, 1);
        T_EQ(sm, sys);
        T_EQ(rt_process_object()->affinity_changes, 1);
    }

    T_PLAN(7, "a mask of 0 or outside the system mask is refused; an event handle is not a process") {
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_process_set_affinity(RT_PSEUDO_PROCESS, 0), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        if (sys != ~(uint64_t)0) {
            rt_set_last_error(ctx, SENTINEL);
            T_EQ(rt_process_set_affinity(RT_PSEUDO_PROCESS, sys + 1), FALSE);
            T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        }
        T_TRUE(rt_process_get_affinity(RT_PSEUDO_PROCESS, &pm, &sm));
        T_EQ(pm, 1);
        T_EQ(rt_process_object()->affinity_changes, 1);

        HANDLE ev = rt_event_create(ctx, TRUE, FALSE, NULL);
        T_REQUIRE(ev != NULL);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_process_get_affinity(ev, &pm, &sm), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
        rt_set_last_error(ctx, SENTINEL);
        T_EQ(rt_process_set_affinity(ev, 1), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    }

    T_PLAN(8, "a NULL mask pointer is fatal") {
        T_TRAPS(rt_process_get_affinity(RT_PSEUDO_PROCESS, NULL, &sm), "a mask pointer is NULL");
        T_TRAPS(rt_process_get_affinity(RT_PSEUDO_PROCESS, &pm, NULL), "a mask pointer is NULL");
    }
}
