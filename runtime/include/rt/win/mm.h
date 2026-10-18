/* winmm and avrt: the handle types and constants the shims of the wave, mixer and PlaySound
 * functions name (mmsystem.h, mmeapi.h).  The machine this runtime describes has no audio
 * device before Gate 5 (docs/RUNTIME.md section 11.1), so no structure is filled and none is
 * declared: WAVEFORMATEX, WAVEHDR and the mixer structures cross the ABI as LPVOID.  The
 * MMSYSERR_ values are in rt/win/error.h. */
#ifndef RT_WIN_MM_H
#define RT_WIN_MM_H

#include "rt/wintypes.h"

typedef void *HWAVEOUT, *HMIXER, *HMIXEROBJ;
typedef HWAVEOUT *LPHWAVEOUT;
typedef HMIXER *LPHMIXER;

/* uDeviceID of waveOutOpen: "the function selects a waveform-audio output device capable of
 * playing the given format" (checked against Wine's include/mmsystem.h: -1). */
#define WAVE_MAPPER               0xFFFFFFFFu

/* The object-type bits of fdwOpen (mixerOpen) and fdwInfo, fdwControls, fdwDetails (the other
 * mixer functions): the top bit says the first argument is a handle, 0 says it is a mixer
 * device identifier (checked against Wine's include/mmsystem.h). */
#define MIXER_OBJECTF_HANDLE      0x80000000u
#define MIXER_OBJECTF_MIXER       0x00000000u
#define MIXER_OBJECTF_HMIXER      (MIXER_OBJECTF_HANDLE | MIXER_OBJECTF_MIXER)
#define MIXER_OBJECTF_TYPEMASK    0xF0000000u

/* avrt: what AvSetMmThreadCharacteristics returns.  The call is accepted and has no effect
 * (docs/RUNTIME.md section 6.3), so one constant serves every caller; it is not a kernel
 * handle (proposal of unit rt-shim-rows-2, held for the orchestrator). */
#define RT_AVRT_TASK              ((HANDLE)(uintptr_t)0x00A72700u)


/* ---- added by unit rt-census-rows (docs/reviews/census-rows.md, 1.4 and 1.8) -------------- */
/* mixerGetLineInfoA, mixerGetLineControlsA: the structures are never read or written here (no
 * mixer device exists), so only the pointer types are declared.  The query values in the low
 * bits of the flags (mmsystem.h; values as Wine's public include/mmsystem.h has them). */
typedef struct tagMIXERLINEA *LPMIXERLINEA;
typedef struct tagMIXERLINECONTROLSA *LPMIXERLINECONTROLSA;
#define MIXER_GETLINEINFOF_DESTINATION      0x00000000u
#define MIXER_GETLINEINFOF_SOURCE           0x00000001u
#define MIXER_GETLINEINFOF_LINEID           0x00000002u
#define MIXER_GETLINEINFOF_COMPONENTTYPE    0x00000003u
#define MIXER_GETLINEINFOF_TARGETTYPE       0x00000004u
#define MIXER_GETLINEINFOF_QUERYMASK        0x0000000Fu
#define MIXER_GETLINECONTROLSF_ALL          0x00000000u
#define MIXER_GETLINECONTROLSF_ONEBYID      0x00000001u
#define MIXER_GETLINECONTROLSF_ONEBYTYPE    0x00000002u
#define MIXER_GETLINECONTROLSF_QUERYMASK    0x0000000Fu
/* dsound: what DirectSoundCaptureCreate answers on a machine with no capture device (Wine
 * dsound/tests/capture.c accepts it; the value as Wine's public include/dsound.h has it). */
#define DSERR_NODRIVER                      ((HRESULT)0x88780078)
#define DSERR_INVALIDPARAM                  ((HRESULT)0x80070057)

#endif /* RT_WIN_MM_H */
