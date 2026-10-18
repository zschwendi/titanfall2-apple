#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"

/* A wide literal is not a WCHAR string here: L"" is a 32-bit wchar_t on the host, so the
 * task names are u"" (UTF-16) literals, cast to LPCWSTR by WSTR. */
#define WSTR(s) ((LPCWSTR)(s))

TEST(avrt, AvSetMmThreadCharacteristicsW)
{
    DWORD idx;
    HANDLE h;
    unsigned n0;

    T_CASE("a task name that is one of the seven, in any case, is accepted");
    idx = 0;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"audio"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    T_EQ(idx, 1);
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"CAPTURE"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"distribution"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"gAmEs"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"PlayBack"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"PRO AUDIO"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"window manager"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    T_EQ(idx, 1);

    T_CASE("index 0 is stored as 1 and gives the task handle");
    idx = 0;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    T_EQ(idx, 1);

    T_CASE("index 1 stays 1 and gives the task handle");
    idx = 1;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    T_EQ(idx, 1);

    T_CASE("any other index fails with ERROR_INVALID_TASK_INDEX and NULL");
    CALL(kernel32, SetLastError, 0);
    idx = 7;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(h, NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_TASK_INDEX);
    T_EQ(idx, 7);
    CALL(kernel32, SetLastError, 0);
    idx = 0xFFFFFFFFu;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(h, NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_TASK_INDEX);
    T_EQ(idx, 0xFFFFFFFFu);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    idx = 0;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    T_EQ(T_LAST_ERROR(), 0x1234);
    idx = 1;
    h = CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(h, RT_AVRT_TASK);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs once that the call is accepted and ignored");
    idx = 1;
    CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_TRUE(T_LOGGED(RT_CH_THREAD, "AvSetMmThreadCharacteristicsW(...): accepted and ignored"));
    n0 = T_LOG_COUNT(RT_CH_THREAD);
    idx = 0;
    CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), &idx);
    T_EQ(T_LOG_COUNT(RT_CH_THREAD), n0);

    /* The NULL cases end in an RT_UNIMPLEMENTED report: each one is proved here, after the
     * checks of the last case. */
    T_TRAPS(CALL(avrt, AvSetMmThreadCharacteristicsW, NULL, &idx), "NULL TaskName");
    T_TRAPS(CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Pro Audio"), NULL), "NULL TaskIndex");
    T_TRAPS(CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"Nonesuch"), &idx), "a task name that is not one of the seven");
    /* U+FF21 (fullwidth A) has a case mapping the core does not carry (rt_wchar_case_known is
     * FALSE for it), so the shim refuses the name itself rather than let the core stop without
     * naming the task. */
    T_TRAPS(CALL(avrt, AvSetMmThreadCharacteristicsW, WSTR(u"\uFF21udio"), &idx), "a task name that is not one of the seven");
}
