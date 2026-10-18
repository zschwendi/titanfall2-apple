/* The audio client (runtime/include/rt/wasapi.h): its objects, the device thread that takes the
 * game's frames to the sink, and the mix format of the made-up endpoint.  The method bodies are
 * in runtime/xaudio/<Interface>/<Method>.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/handle.h"
#include "rt/heap.h"
#include "rt/log.h"
#include "rt/thread.h"          /* before rt/sync.h, which names RtThread */
#include "rt/sync.h"
#include "rt/wasapi.h"

static RtComIface *const ac_faces[] = { &rt_iface_IAudioClient };
const RtComClass rt_audio_client_class = { "AudioClient", ac_faces, 1, sizeof(AcClient), ac_device_destroy, NULL };

static RtComIface *const render_faces[] = { &rt_iface_IAudioRenderClient };
static void bound_destroy(void *state) {
    AcBound *b = state;
    rt_com_unhold(b->client);
}
const RtComClass rt_audio_render_class = { "AudioRenderClient", render_faces, 1, sizeof(AcBound), bound_destroy, NULL };

static RtComIface *const clock_faces[] = { &rt_iface_IAudioClock };
const RtComClass rt_audio_clock_class = { "AudioClock", clock_faces, 1, sizeof(AcBound), bound_destroy, NULL };

void *rt_audio_client_new(void) {
    void *obj = rt_com_new(&rt_audio_client_class);
    AcClient *s = rt_com_state_of(obj, &rt_audio_client_class);
    pthread_mutex_init(&s->mu, NULL);
    return obj;
}

static void *bound_new(const RtComClass *cls, void *client_face) {
    void *obj = rt_com_new(cls);
    AcBound *b = rt_com_state_of(obj, cls);
    b->client = client_face;
    b->ac = rt_com_state_of(client_face, &rt_audio_client_class);
    rt_com_hold(client_face);              /* the service keeps the client alive */
    return obj;
}

void *rt_audio_render_new(void *client_face) { return bound_new(&rt_audio_render_class, client_face); }
void *rt_audio_clock_new(void *client_face) { return bound_new(&rt_audio_clock_class, client_face); }

/* ---- the device thread --------------------------------------------------------------------- */

/* One period: take up to AC_PERIOD_FRAMES from the ring (silence for the rest), move the ring's
 * head, count the period as taken by the device, write it to the sink with the lock released
 * (the sink keeps real time), then signal the event under the lock.  It runs no guest code. */
static void *device_main(void *arg) {
    AcClient *s = arg;
    float block[AC_PERIOD_FRAMES * AC_MIX_CHANNELS];
    for (;;) {
        pthread_mutex_lock(&s->mu);
        if (!s->started) {
            pthread_mutex_unlock(&s->mu);
            return NULL;
        }
        uint32_t take = s->padding < AC_PERIOD_FRAMES ? s->padding : AC_PERIOD_FRAMES;
        for (uint32_t i = 0; i < take; i++) {
            uint32_t idx = (s->head + i) % s->buffer_frames;
            memcpy(block + 2 * i, s->ring + 2 * idx, 2 * sizeof(float));
        }
        memset(block + 2 * take, 0, (AC_PERIOD_FRAMES - take) * AC_MIX_CHANNELS * sizeof(float));
        if (take < AC_PERIOD_FRAMES && s->written) s->underruns++;
        s->head = (s->head + take) % s->buffer_frames;
        s->padding -= take;
        s->position += AC_PERIOD_FRAMES;
        pthread_mutex_unlock(&s->mu);

        rt_audio_sink_write(s->sink, block, AC_PERIOD_FRAMES);

        pthread_mutex_lock(&s->mu);
        if (s->event) rt_event_set_object(s->event);
        pthread_mutex_unlock(&s->mu);
    }
}

HRESULT ac_device_start(AcClient *s) {
    pthread_mutex_lock(&s->mu);
    if (!s->initialized) { pthread_mutex_unlock(&s->mu); return AUDCLNT_E_NOT_INITIALIZED; }
    if (s->started) { pthread_mutex_unlock(&s->mu); return AUDCLNT_E_NOT_STOPPED; }
    if ((s->flags & AUDCLNT_STREAMFLAGS_EVENTCALLBACK) && !s->event) {
        pthread_mutex_unlock(&s->mu);
        return AUDCLNT_E_EVENTHANDLE_NOT_SET;
    }
    if (!s->sink) {
        s->sink = rt_audio_sink_open(AC_MIX_RATE, AC_MIX_CHANNELS);
        if (!s->sink) {
            pthread_mutex_unlock(&s->mu);
            rt_log(RT_CH_COM, RT_LL_ERROR, "IAudioClient::Start: the audio sink could not be opened; E_FAIL");
            return E_FAIL;
        }
    }
    s->started = true;
    if (pthread_create(&s->thread, NULL, device_main, s) != 0) {
        s->started = false;
        pthread_mutex_unlock(&s->mu);
        rt_log(RT_CH_COM, RT_LL_ERROR, "IAudioClient::Start: the device thread could not be started; E_FAIL");
        return E_FAIL;
    }
    s->thread_on = true;
    pthread_mutex_unlock(&s->mu);
    return S_OK;
}

void ac_device_stop_join(AcClient *s) {
    pthread_mutex_lock(&s->mu);
    s->started = false;
    bool joinable = s->thread_on;
    s->thread_on = false;
    pthread_mutex_unlock(&s->mu);
    if (joinable) pthread_join(s->thread, NULL);
}

/* The client's destroy function: the last reference is gone, so nothing else can reach the state. */
void ac_device_destroy(void *state) {
    AcClient *s = state;
    ac_device_stop_join(s);
    if (s->sink) rt_audio_sink_close(s->sink);
    if (s->event) rt_object_unref(s->event);
    if (s->ring) rt_gfree(s->ring);
    if (s->write_buf) rt_gfree(s->write_buf);
    pthread_mutex_destroy(&s->mu);
}

/* ---- logging ------------------------------------------------------------------------------- */

void ac_log_no_interface(const char *where, const GUID *iid) {
    char text[40];
    if (!iid) snprintf(text, sizeof text, "(NULL)");
    else snprintf(text, sizeof text, "{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}", (unsigned)iid->Data1,
                  (unsigned)iid->Data2, (unsigned)iid->Data3, iid->Data4[0], iid->Data4[1], iid->Data4[2],
                  iid->Data4[3], iid->Data4[4], iid->Data4[5], iid->Data4[6], iid->Data4[7]);
    rt_log(RT_CH_COM, RT_LL_INFO, "%s: interface %s is not one the audio client has (IAudioRenderClient, IAudioClock); E_NOINTERFACE", where, text);
}

/* ---- the mix format ------------------------------------------------------------------------ */

/* WAVEFORMATEXTENSIBLE, little endian, written byte by byte (the guest's buffer may be unaligned):
 *   0  wFormatTag 0xFFFE          2  nChannels 2              4  nSamplesPerSec 48000
 *   8  nAvgBytesPerSec 384000     12 nBlockAlign 8            14 wBitsPerSample 32
 *   16 cbSize 22                  18 wValidBitsPerSample 32   20 dwChannelMask 0x3
 *   24 SubFormat {00000003-0000-0010-8000-00AA00389B71}, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT */
static void put_le(unsigned char *p, uint32_t v, unsigned bytes) {
    for (unsigned i = 0; i < bytes; i++) p[i] = (unsigned char)(v >> (8 * i));
}

void ac_fill_mix_format(void *buf) {
    unsigned char *p = buf;
    static const unsigned char float_guid[16] = {
        0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71 };
    memset(p, 0, AC_MIX_FORMAT_BYTES);
    put_le(p + 0,  0xFFFE, 2);
    put_le(p + 2,  AC_MIX_CHANNELS, 2);
    put_le(p + 4,  AC_MIX_RATE, 4);
    put_le(p + 8,  AC_MIX_RATE * 8, 4);            /* frames a second times the block align */
    put_le(p + 12, 8, 2);                          /* two 32-bit float samples */
    put_le(p + 14, AC_MIX_BITS, 2);
    put_le(p + 16, 22, 2);                         /* cbSize: the extension's 22 bytes */
    put_le(p + 18, AC_MIX_BITS, 2);                /* wValidBitsPerSample */
    put_le(p + 20, 0x3, 4);                        /* dwChannelMask: front left and right */
    memcpy(p + 24, float_guid, 16);
}

static uint32_t get_le(const unsigned char *p, unsigned bytes) {
    uint32_t v = 0;
    for (unsigned i = 0; i < bytes; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

bool ac_format_is_mix(const void *fmt) {
    const unsigned char *p = fmt;
    uint32_t tag = get_le(p + 0, 2);
    if (get_le(p + 2, 2) != AC_MIX_CHANNELS || get_le(p + 4, 4) != AC_MIX_RATE || get_le(p + 14, 2) != AC_MIX_BITS)
        return false;
    if (tag == 3) return true;                        /* WAVE_FORMAT_IEEE_FLOAT, plain */
    if (tag != 0xFFFE || get_le(p + 16, 2) < 22) return false;
    unsigned char mix[AC_MIX_FORMAT_BYTES];
    ac_fill_mix_format(mix);
    return memcmp(p + 24, mix + 24, 16) == 0;         /* the sub-format: IEEE float */
}
