/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-mixergetlinecontrolsa
 * RT-CASES:  returns MMSYSERR_BADDEVICEID for identifier 0; returns MMSYSERR_BADDEVICEID for identifier 5; returns MMSYSERR_INVALHANDLE for a handle that was never given out; writes nothing through pmxlc; leaves last error alone; logs that there is no mixer device
 * RT-TRAPS:  a query value the function does not have; an object that is not a mixer
 */
#include "rt/shim.h"
#include "rt/win/mm.h"

SHIM(winmm, MMRESULT, mixerGetLineControlsA, HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls)
{
    DWORD query = fdwControls & MIXER_GETLINECONTROLSF_QUERYMASK;
    DWORD kind = fdwControls & MIXER_OBJECTF_TYPEMASK;

    /* This runtime has no audio device (RUNTIME.md 11.1): there are zero mixer devices and no
     * open mixer handle, so every identifier is out of range. pmxlc is not written. */
    (void)hmxobj;
    (void)pmxlc;
    /* The query first: Windows answers MMSYSERR_INVALFLAG for one it does not have on an OPEN
     * mixer (Wine winmm/tests/mixer.c); whether it looks at the object before the flags when
     * there is none is not settled, so this is a stop.  Then the object's kind: only a mixer
     * identifier or a mixer handle is answered (docs/reviews/census-rows.md 1.4). */
    if (query > 2u || (fdwControls & ~(MIXER_GETLINECONTROLSF_QUERYMASK | MIXER_OBJECTF_TYPEMASK)) != 0u)
        RT_UNIMPLEMENTED(ctx, "mixerGetLineControlsA flags 0x%08X: a query value the function does not have", (unsigned int)fdwControls);
    if (kind != MIXER_OBJECTF_MIXER && kind != MIXER_OBJECTF_HMIXER)
        RT_UNIMPLEMENTED(ctx, "mixerGetLineControlsA flags 0x%08X: an object that is not a mixer", (unsigned int)fdwControls);
    rt_log_once(RT_CH_IMPORT, RT_LL_INFO, "winmm.dll!mixerGetLineControlsA", "mixerGetLineControlsA: there is no mixer device");
    return kind == MIXER_OBJECTF_HMIXER ? MMSYSERR_INVALHANDLE : MMSYSERR_BADDEVICEID;
}
