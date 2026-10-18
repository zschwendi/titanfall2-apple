/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/avrt/nf-avrt-avsetmmthreadcharacteristicsw
 * RT-CASES:  a task name that is one of the seven, in any case, is accepted; index 0 is stored as 1 and gives the task handle; index 1 stays 1 and gives the task handle; any other index fails with ERROR_INVALID_TASK_INDEX and NULL; success leaves last error alone; logs once that the call is accepted and ignored
 * RT-TRAPS:  NULL TaskName; NULL TaskIndex; a task name that is not one of the seven
 */
#include "rt/shim.h"
#include "rt/locale.h"              /* rt_compare_string, rt_wchar_case_known, rt_wide_to_mb */
#include "rt/win/mm.h"              /* RT_AVRT_TASK */

/* The seven Windows 7 tasks.  Names are UTF-16 inside the runtime; the literals are u"" strings
 * because L"" is a 32-bit wchar_t on the host. */
#define WTEXT(s) ((const WCHAR *)(s))
static const WCHAR *const TASKS[] = {
    WTEXT(u"Audio"), WTEXT(u"Capture"), WTEXT(u"Distribution"), WTEXT(u"Games"),
    WTEXT(u"Playback"), WTEXT(u"Pro Audio"), WTEXT(u"Window Manager"),
};

/* TRUE if the NUL-terminated `name` is one of the seven, ignoring case by CompareStringW's rule
 * (NORM_IGNORECASE, en-US, from the core).  A name with a code unit whose case the core does not
 * cover is refused (FALSE) before the core is asked: the core stops on such a unit when it
 * reaches one, and that stop does not name the task. */
static BOOL is_win7_task(const WCHAR *name)
{
    int len = 0;
    while (name[len]) {
        if (!rt_wchar_case_known(name[len])) return FALSE;
        len++;
    }
    for (int i = 0; i < (int)(sizeof TASKS / sizeof TASKS[0]); i++)
        if (rt_compare_string(LANG_ENGLISH_US_LCID, NORM_IGNORECASE, name, len, TASKS[i], -1) == CSTR_EQUAL)
            return TRUE;
    return FALSE;
}

SHIM(avrt, HANDLE, AvSetMmThreadCharacteristicsW, LPCWSTR TaskName, LPDWORD TaskIndex)
{
    if (!TaskName) RT_UNIMPLEMENTED(ctx, "AvSetMmThreadCharacteristicsW with NULL TaskName");
    if (!TaskIndex) RT_UNIMPLEMENTED(ctx, "AvSetMmThreadCharacteristicsW with NULL TaskIndex");

    /* A name that is not one of the seven is a stop, not ERROR_INVALID_TASK_NAME: that answer is
     * not given here. The report shows the name in UTF-8 when it fits in the buffer. */
    if (!is_win7_task(TaskName)) {
        char shown[256];
        if (rt_wide_to_mb(CP_UTF8, 0, TaskName, -1, shown, (int)sizeof shown, NULL, NULL))
            RT_UNIMPLEMENTED(ctx, "AvSetMmThreadCharacteristicsW with a task name that is not one of the seven: \"%s\"", shown);
        RT_UNIMPLEMENTED(ctx, "AvSetMmThreadCharacteristicsW with a task name that is not one of the seven");
    }

    /* The index is the documented one: 0 on the first call, and the value given out (1) on the
     * next ones. Any other value fails and is left unchanged. */
    if (*TaskIndex == 0)
        *TaskIndex = 1;
    else if (*TaskIndex != 1)
        return FAIL(ctx, ERROR_INVALID_TASK_INDEX, NULL);

    rt_log_once(RT_CH_THREAD, RT_LL_DEBUG, "AvSetMmThreadCharacteristicsW", "AvSetMmThreadCharacteristicsW(...): accepted and ignored");
    return RT_AVRT_TASK;
}
