/* The sound classes: what CoCreateInstance makes for the game (runtime/xaudio/classes.c has the
 * class table).  The two objects exist; no method body is written yet, so a method the game
 * calls stops with its name (rt_com_stop).  Their interfaces are described in runtime/com/desc
 * (IXAudio2 from xaudio2_7.dll, IMMDeviceEnumerator from mmdevapi.dll). */
#ifndef RT_XAUDIO_H
#define RT_XAUDIO_H

#include "rt/com.h"

typedef struct XaEngine     { int nothing_yet; } XaEngine;      /* behind IXAudio2 (version 2.7) */
typedef struct MmEnumerator { int nothing_yet; } MmEnumerator;  /* behind IMMDeviceEnumerator */
extern const RtComClass rt_xaudio_engine_class, rt_mm_enumerator_class;

/* The class table of CoCreateInstance.  Makes an object of class `clsid` and writes its
 * face for `iid` through *ppv: S_OK.  A class the table does not have:
 * REGDB_E_CLASSNOTREG, nothing made, *ppv untouched.  A class it has, asked for an
 * interface that is not a face of it (IUnknown always is): E_NOINTERFACE, *ppv NULL,
 * nothing left alive. */
HRESULT rt_com_class_create(Ctx *ctx, const GUID *clsid, const GUID *iid, void **ppv);

/* The class the table has for `clsid`, or NULL.  CoCreateInstance asks it before it makes
 * anything (a non-NULL pUnkOuter is refused for a class the table has, with nothing made). */
const RtComClass *rt_com_class_by_clsid(const GUID *clsid);

/* Whether the class table offers the sound classes at all.  True unless a program clears it
 * before any guest code runs.  `gate3 run` clears it unless GATE3_SOUND=on: the game's audio
 * library asks for the MMDevice enumerator even with -nosound, and while the sound objects are
 * not complete a run would stop at their first method without a body.  Cleared, the classes are
 * "not registered", which is what every run before 12 October 2026 was told. */
extern bool rt_sound_classes;


/* The two interfaces the game implements and the runtime calls (no description, no thunks: the
 * runtime calls the guest's vtable through these numbers).  Read from Wine's xaudio2.idl with
 * XAUDIO2_VER=7.  Neither has a base in the IDL, so the slots start at 0 and there are no
 * IUnknown slots.  The prototype is on each line. */

/* IXAudio2EngineCallback: 3 methods */
enum {
    XA_ENGINE_CB_OnProcessingPassStart = 0,     /* void OnProcessingPassStart(void) */
    XA_ENGINE_CB_OnProcessingPassEnd   = 1,     /* void OnProcessingPassEnd(void) */
    XA_ENGINE_CB_OnCriticalError       = 2,     /* void OnCriticalError(HRESULT Error) */
    XA_ENGINE_CB_COUNT                 = 3
};

/* IXAudio2VoiceCallback: 7 methods */
enum {
    XA_VOICE_CB_OnVoiceProcessingPassStart = 0, /* void OnVoiceProcessingPassStart(UINT32 BytesRequired) */
    XA_VOICE_CB_OnVoiceProcessingPassEnd   = 1, /* void OnVoiceProcessingPassEnd(void) */
    XA_VOICE_CB_OnStreamEnd                = 2, /* void OnStreamEnd(void) */
    XA_VOICE_CB_OnBufferStart              = 3, /* void OnBufferStart(void *pBufferContext) */
    XA_VOICE_CB_OnBufferEnd                = 4, /* void OnBufferEnd(void *pBufferContext) */
    XA_VOICE_CB_OnLoopEnd                  = 5, /* void OnLoopEnd(void *pBufferContext) */
    XA_VOICE_CB_OnVoiceError               = 6, /* void OnVoiceError(void *pBufferContext, HRESULT Error) */
    XA_VOICE_CB_COUNT                      = 7
};

#endif
