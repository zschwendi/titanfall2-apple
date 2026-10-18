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

TEST(kernel32, GetProcessAffinityMask)
{
    const DWORD_PTR junk = (DWORD_PTR)0xDEADBEEFCAFEF00DULL;
    const DWORD_PTR sys = system_mask();
    DWORD_PTR pm, sm;
    HANDLE ev;

    T_CASE("both masks are the system mask at first");
    pm = sm = 0;
    T_EQ(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, &sm), TRUE);
    T_EQ(pm, sys);
    T_EQ(sm, sys);

    T_CASE("a mask stored by SetProcessAffinityMask is read back while the system mask stays");
    T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, 1), TRUE);
    pm = sm = 0;
    T_EQ(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, &sm), TRUE);
    T_EQ(pm, 1);
    T_EQ(sm, sys);

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE and write neither mask");
    ev = make_event();
    T_REQUIRE(ev != NULL);
    pm = sm = junk;
    T_EQ(CALL(kernel32, GetProcessAffinityMask, NULL, &pm, &sm), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, GetProcessAffinityMask, ev, &pm, &sm), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(pm, junk);
    T_EQ(sm, junk);
    T_TRUE(rt_handle_close(ev));

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, &sm), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop of the core (core: a NULL mask pointer): it runs after the checks of the last case. */
    T_TRAPS(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, NULL, &sm), "a mask pointer is NULL");
    T_TRAPS(CALL(kernel32, GetProcessAffinityMask, RT_PSEUDO_PROCESS, &pm, NULL), "a mask pointer is NULL");

    /* Store the system mask again: the test set a mask in the case above. */
    T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, sys), TRUE);
}
