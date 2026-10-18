#include "rt/test.h"
#include "rt/handle.h"
#include "rt/machine.h"
#include "rt/thread.h"

/* The system mask: bits 0 to cpu_count - 1 (rt/machine.h).  In this test the process mask is the
 * system mask, and the test changes only its own thread's mask.  It stores the system mask again
 * at the end. */
static DWORD_PTR system_mask(void)
{
    uint32_t n = rt_machine()->cpu_count;
    return n >= 64 ? ~(DWORD_PTR)0 : (((DWORD_PTR)1 << n) - 1);
}

TEST(kernel32, SetThreadAffinityMask)
{
    DWORD_PTR sys = system_mask();
    uint32_t n = rt_machine()->cpu_count;
    HANDLE h, ev;

    h = rt_thread_open(CALL(kernel32, GetCurrentThreadId));
    T_REQUIRE(h != NULL);

    T_CASE("returns the process mask until the thread sets its own, then the mask that was stored");
    T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1), sys);  /* the process mask */
    T_EQ(CALL(kernel32, SetThreadAffinityMask, h, sys), (DWORD_PTR)1);  /* what the first call stored */

    /* Windows (Vista and later) reads all bits set, or only the low 32 bits all set, as all
     * processors (Wine kernel32/tests/thread.c, test_thread_processor): the call succeeds, returns
     * the previous mask, stores the process mask and leaves last error alone. */
    T_CASE("all bits set, or the low 32 bits all set, is all processors: returns the previous mask and stores the process mask");
    T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1), sys);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(kernel32, SetThreadAffinityMask, h, ~(DWORD_PTR)0), (DWORD_PTR)1);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1), sys);  /* the stored mask is the process mask */
    if (sizeof(DWORD_PTR) == 8) {
        T_EQ(CALL(kernel32, SetThreadAffinityMask, h, ~(DWORD_PTR)0 >> 3), (DWORD_PTR)1);
        T_EQ(T_LAST_ERROR(), 0x1234);
        T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1), sys);
    }

    T_CASE("mask 0, and a mask with a bit the process does not have unless its low 32 bits are all set, fail with ERROR_INVALID_PARAMETER");
    T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    if (n < 64) {
        T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1 << 63), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    }

    T_CASE("NULL and an event handle fail with ERROR_INVALID_HANDLE");
    T_EQ(CALL(kernel32, SetThreadAffinityMask, NULL, (DWORD_PTR)1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    ev = CALL(kernel32, CreateEventA, NULL, TRUE, FALSE, NULL);
    T_REQUIRE(ev != NULL);
    T_EQ(CALL(kernel32, SetThreadAffinityMask, ev, (DWORD_PTR)1), 0);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(CALL(kernel32, CloseHandle, ev), TRUE);

    T_CASE("does not change last error on success");
    CALL(kernel32, SetThreadAffinityMask, h, sys);                      /* the stored mask is the system mask */
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1) != 0);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    T_TRUE(CALL(kernel32, SetThreadAffinityMask, h, sys) != 0);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Windows gives every thread the new process mask, one that set its own too (Wine
     * kernel32/tests/thread.c, test_thread_processor, the part that runs where SetThreadGroupAffinity
     * exists, i.e. Windows 7 and later).  The block's sequence: process mask 0xFF; thread A sets
     * 0x0F; process mask 0x03; A sets 0x01 and reads 0x03; process mask 0xFF; A sets 0x01 and reads
     * 0xFF.  Here the full mask is the system mask (0xFF on an eight-processor host), and 0x0F and
     * 0x03 are cut to the processors there are, so the same sequence runs on any host. */
    T_CASE("after SetProcessAffinityMask every thread has the new process mask, one that set its own too");
    {
        DWORD_PTR all = sys, a = sys & 0x0F, m = sys & 0x03;
        CALL(kernel32, SetLastError, 0x1234);
        T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, all), TRUE);   /* the process mask: 0xFF */
        T_EQ(CALL(kernel32, SetThreadAffinityMask, h, a), all);                       /* A sets 0x0F: the process mask it had */
        T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, m), TRUE);    /* the process mask: 0x03 */
        T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1), m);              /* A reads 0x03, then sets 0x01 */
        T_EQ(CALL(kernel32, SetProcessAffinityMask, RT_PSEUDO_PROCESS, all), TRUE);  /* the process mask: 0xFF again */
        T_EQ(CALL(kernel32, SetThreadAffinityMask, h, (DWORD_PTR)1), all);            /* A reads 0xFF */
        T_EQ(T_LAST_ERROR(), 0x1234);
    }

    T_TRUE(CALL(kernel32, SetThreadAffinityMask, h, sys) != 0);        /* store the system mask again */
    T_TRUE(rt_handle_close(h));
}
