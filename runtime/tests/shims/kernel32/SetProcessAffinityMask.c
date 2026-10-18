#include "rt/test.h"
#include "rt/handle.h"
#include "rt/machine.h"

/* The system mask has bits 0 to cpu_count - 1 (rt_machine, rt/machine.h).  cpu_count is at
 * most 64, so the mask fits in a DWORD_PTR. */
static DWORD_PTR system_mask(void)
{
    const uint32_t n = rt_machine()->cpu_count;
    return n >= 64 ? (DWORD_PTR)-1 : (((DWORD_PTR)1 << n) - 1);
}

/* An event is made with the core's own calls, as in tests/shims/kernel32/CloseHandle.c. */
static void noop_destroy(RtObject *o)
{
    (void)o;
}

static const RtObjectOps event_ops = { .name = "test-event", .destroy = noop_destroy };

static HANDLE make_event(void)
{
    RtObject *o = rt_object_new(RT_K_EVENT, sizeof(RtObject), &event_ops, NULL, NULL);
    return o ? rt_handle_new(o) : NULL;
}

TEST(kernel32, SetProcessAffinityMask)
{
    const DWORD_PTR sys = system_mask();
    DWORD_PTR pm, sm;
    HANDLE ev;

    T_CASE("a mask of bit 0 alone is stored and read back while the system mask stays");
    T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, 1), TRUE);
    pm = sm = 0;
    T_EQ(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, &sm), TRUE);
    T_EQ(pm, 1);
    T_EQ(sm, sys);

    T_CASE("a mask of 0 or with a bit outside the system mask fails with ERROR_INVALID_PARAMETER and leaves the stored mask unchanged");
    T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, 0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    if (rt_machine()->cpu_count < 64) {          /* bit 63 is then outside the system mask */
        T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, (DWORD_PTR)1 << 63), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }
    pm = sm = 0;
    T_EQ(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, &sm), TRUE);
    T_EQ(pm, 1);
    T_EQ(sm, sys);

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE");
    ev = make_event();
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, SetProcessAffinityMask, NULL, 1), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, SetProcessAffinityMask, ev, 1), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_TRUE(rt_handle_close(ev));

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, 1), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Store the system mask again: the test left a mask of its own. */
    T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, sys), TRUE);
    pm = sm = 0;
    T_EQ(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, &sm), TRUE);
    T_EQ(pm, sys);
}
