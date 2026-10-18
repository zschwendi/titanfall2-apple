/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize
 * RT-CASES:  already initialized: _ALREADY_INITIALIZED; NULL pFormat E_POINTER; exclusive: _EXCLUSIVE_MODE_NOT_ALLOWED;
 *            a format that is not the mix format: _UNSUPPORTED_FORMAT; the mix format in shared mode: S_OK, the ring and
 *            the write buffer made (a duration of 0 gives two periods)
 * RT-TRAPS:  a StreamFlags bit outside EVENTCALLBACK, NOPERSIST, AUTOCONVERTPCM, SRC_DEFAULT_QUALITY
 */
#include "rt/heap.h"
#include "rt/log.h"
#include "rt/wasapi.h"

/* RT-DOC, read 2026-10-12: "AUDCLNT_E_ALREADY_INITIALIZED: The audio stream has already been initialized."
 * "E_POINTER: Parameter pFormat is NULL."  The buffer is the frames the duration covers (duration in 100 ns
 * units times 48000, rounded up), at least two periods (960 frames) and at most 96000: the brief's rule.
 * A flag outside the four named in the brief is a stop (RT_UNIMPLEMENTED), so the planner sees what the game
 * asks.  hnsPeriodicity is not used (the device period is fixed) and AudioSessionGuid is not used (no session). */
COM_METHOD(IAudioClient, HRESULT, Initialize, AUDCLNT_SHAREMODE ShareMode, DWORD StreamFlags, REFERENCE_TIME hnsBufferDuration, REFERENCE_TIME hnsPeriodicity, const WAVEFORMATEX *pFormat, LPCGUID AudioSessionGuid)
{
    AcClient *s = rt_com_state(ctx, This, &rt_audio_client_class);
    (void)hnsPeriodicity;
    (void)AudioSessionGuid;
    pthread_mutex_lock(&s->mu);
    bool already = s->initialized;
    pthread_mutex_unlock(&s->mu);
    if (already) return AUDCLNT_E_ALREADY_INITIALIZED;
    if (!pFormat) return E_POINTER;
    if (ShareMode == AUDCLNT_SHAREMODE_EXCLUSIVE) return AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED;
    if (ShareMode != AUDCLNT_SHAREMODE_SHARED) return E_INVALIDARG;
    const DWORD known = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_NOPERSIST |
                        AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
    if (StreamFlags & ~known)
        RT_UNIMPLEMENTED(ctx, "IAudioClient::Initialize with StreamFlags 0x%08lx: the bits 0x%08lx are not built",
                         (unsigned long)StreamFlags, (unsigned long)(StreamFlags & ~known));
    if (!ac_format_is_mix(pFormat)) return AUDCLNT_E_UNSUPPORTED_FORMAT;

    /* The duration in frames, rounded up, clamped before the multiply so that no value overflows: a
     * duration above 2 s is the largest buffer. */
    uint32_t frames;
    if (hnsBufferDuration <= 0) frames = 0;
    else if (hnsBufferDuration > 2000000) frames = AC_MAX_BUFFER_FRAMES;
    else frames = (uint32_t)(((uint64_t)hnsBufferDuration * AC_MIX_RATE + 9999999) / 10000000);
    if (frames < AC_MIN_BUFFER_FRAMES) frames = AC_MIN_BUFFER_FRAMES;
    if (frames > AC_MAX_BUFFER_FRAMES) frames = AC_MAX_BUFFER_FRAMES;

    float *ring = rt_galloc((size_t)frames * AC_MIX_CHANNELS * sizeof(float));
    void *wbuf = rt_galloc((size_t)frames * AC_MIX_CHANNELS * sizeof(float));
    pthread_mutex_lock(&s->mu);
    s->flags = StreamFlags;
    s->buffer_frames = frames;
    s->ring = ring;
    s->write_buf = wbuf;
    s->padding = s->head = s->granted = 0;
    s->position = s->underruns = 0;
    s->open = s->written = false;
    s->initialized = true;
    pthread_mutex_unlock(&s->mu);
    rt_log(RT_CH_COM, RT_LL_INFO, "IAudioClient::Initialize: shared mode, StreamFlags 0x%08lx, hnsBufferDuration %lld asked, %u frames given",
           (unsigned long)StreamFlags, (long long)hnsBufferDuration, (unsigned)frames);
    return S_OK;
}
