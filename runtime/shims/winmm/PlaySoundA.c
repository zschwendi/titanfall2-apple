/* RT-STATUS: absent
 * RT-DOC:    https://learn.microsoft.com/en-us/previous-versions//dd743680(v=vs.85)
 * RT-CASES:  returns FALSE for a file name with flags 0x20001; returns FALSE for NULL with flags 0; leaves last error alone; logs that there is no audio output device
 * RT-TRAPS:  none
 */
#include "rt/shim.h"

SHIM(winmm, BOOL, PlaySoundA, LPCSTR pszSound, HMODULE hmod, DWORD fdwSound)
{
    /* This runtime has no audio output device (audio is not built before Gate 5), so nothing
     * can be played. A call that only stops a sound (pszSound NULL) has nothing to stop: FALSE
     * too is the planner's choice. pszSound is not read: with SND_MEMORY it is not a string.
     * PlaySound never touches last error. */
    (void)pszSound;
    (void)hmod;
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!PlaySoundA",
                "PlaySoundA: there is no audio output device (flags 0x%X)", fdwSound);
    return FALSE;
}
