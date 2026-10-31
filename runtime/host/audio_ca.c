/* The speakers' sink (rt/host_audio.h): AudioToolbox's AudioQueue on the default output device.
 * The writer puts frames in a ring; the queue's callback takes 10 ms at a time from it and
 * hands them to the device.  Nothing in the ring is an AudioToolbox type.  Logging goes to
 * RT_CH_FILE. */
#include <AudioToolbox/AudioToolbox.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt/host_audio.h"
#include "rt/log.h"

#define QUEUE_BUFFERS 3
#define TARGET_MS     60u     /* the writer waits while the ring holds more */
#define CAPACITY_MS   200u    /* the ring's size */
#define STALL_NS      (2LL * 1000000000LL)

/* ---- the ring: frames of `ch` floats, no AudioToolbox types ------------------------------ */

typedef struct {
    float *buf;       /* cap * ch floats */
    unsigned cap;     /* frames */
    unsigned ch;
    unsigned rd;      /* index of the oldest frame */
    unsigned n;       /* frames held */
} Ring;

static unsigned ring_count(const Ring *r)
{
    return r->n;
}

/* Puts all `frames` frames or none: false if they do not fit. */
static bool ring_put(Ring *r, const float *src, unsigned frames)
{
    if (frames > r->cap - r->n)
        return false;
    for (unsigned i = 0; i < frames; i++) {
        unsigned w = (r->rd + r->n + i) % r->cap;
        memcpy(r->buf + (size_t)w * r->ch, src + (size_t)i * r->ch, sizeof(float) * r->ch);
    }
    r->n += frames;
    return true;
}

/* Takes up to `frames` frames into dst; returns how many were taken (fewer if the ring holds
 * fewer). */
static unsigned ring_take(Ring *r, float *dst, unsigned frames)
{
    unsigned k = frames < r->n ? frames : r->n;
    for (unsigned i = 0; i < k; i++) {
        unsigned rd = (r->rd + i) % r->cap;
        memcpy(dst + (size_t)i * r->ch, r->buf + (size_t)rd * r->ch, sizeof(float) * r->ch);
    }
    r->rd = (r->rd + k) % r->cap;
    r->n -= k;
    return k;
}

bool rt_host_audio_ring_selftest(void)
{
    enum { CAP = 8, CH = 2 };
    float store[CAP * CH];
    Ring r = { store, CAP, CH, 0, 0 };
    float in[12 * CH], out[12 * CH];
    bool ok = true;
    for (int i = 0; i < 12; i++) {
        in[2 * i] = (float)i;
        in[2 * i + 1] = -(float)i;
    }

    ok = ok && ring_count(&r) == 0;
    ok = ok && ring_take(&r, out, 4) == 0;                 /* empty: takes nothing */
    ok = ok && ring_put(&r, in, 5);                        /* frames 0..4 */
    ok = ok && ring_count(&r) == 5;
    ok = ok && ring_take(&r, out, 3) == 3;                 /* frames 0..2 */
    ok = ok && out[0] == 0.0f && out[4] == 2.0f && out[5] == -2.0f;
    ok = ok && ring_put(&r, in + 5 * CH, 6);               /* frames 5..10: wraps round the end */
    ok = ok && ring_count(&r) == CAP;
    ok = ok && !ring_put(&r, in + 11 * CH, 1);             /* full: refused, nothing changes */
    ok = ok && ring_count(&r) == CAP;
    ok = ok && ring_take(&r, out, 20) == CAP;              /* asks for more than there is */
    for (int k = 0; k < CAP && ok; k++)                    /* frames 3..10, in order */
        ok = out[2 * k] == (float)(3 + k) && out[2 * k + 1] == -(float)(3 + k);
    ok = ok && ring_count(&r) == 0;
    ok = ok && ring_take(&r, out, 1) == 0;
    ok = ok && !ring_put(&r, in, CAP + 1);                 /* more than fits: refused whole */
    ok = ok && ring_count(&r) == 0;
    return ok;
}

/* ---- the sink ---------------------------------------------------------------------------- */

struct RtHostAudio {
    AudioQueueRef q;
    AudioQueueBufferRef bufs[QUEUE_BUFFERS];
    unsigned rate, ch;
    unsigned buf_frames;      /* 10 ms */
    unsigned buf_bytes;
    unsigned target;          /* frames */
    unsigned cap;             /* frames */
    Ring ring;
    float *scratch;           /* cap * ch floats: the clamped copy of what is being written */
    pthread_mutex_t mu;
    pthread_cond_t cv;
    unsigned long long takes;      /* device takes, under mu */
    unsigned long long underrun;   /* frames of silence the device was given, under mu */
    unsigned long long stalls;     /* writes that gave up after 2 s, under mu */
    /* Evidence that sound reaches the device, logged about every 10 s by the writer: frames
     * written and their peak since the last line, against the device's takes. */
    unsigned long long written, logged_takes, logged_underrun;
    float peak;
};

/* Fills one buffer from the ring, silence for what is missing, and enqueues it again. */
static void fill_buffer(RtHostAudio *a, AudioQueueBufferRef b)
{
    float *dst = (float *)b->mAudioData;
    pthread_mutex_lock(&a->mu);
    unsigned got = ring_take(&a->ring, dst, a->buf_frames);
    for (unsigned i = got; i < a->buf_frames; i++)
        memset(dst + (size_t)i * a->ch, 0, sizeof(float) * a->ch);
    a->underrun += a->buf_frames - got;
    a->takes++;
    pthread_cond_broadcast(&a->cv);
    pthread_mutex_unlock(&a->mu);
    b->mAudioDataByteSize = a->buf_bytes;
    AudioQueueEnqueueBuffer(a->q, b, 0, NULL);
}

static void on_buffer(void *user, AudioQueueRef q, AudioQueueBufferRef b)
{
    (void)q;
    fill_buffer((RtHostAudio *)user, b);
}

/* Called with mu held.  Returns 0 when the ring is at or under the target, -1 when the device
 * has taken nothing for 2 s. */
static int wait_for_room(RtHostAudio *a)
{
    unsigned long long seen = a->takes;
    struct timespec dl;
    clock_gettime(CLOCK_REALTIME, &dl);
    dl.tv_sec += 2;
    while (ring_count(&a->ring) > a->target) {
        int rc = pthread_cond_timedwait(&a->cv, &a->mu, &dl);
        if (a->takes != seen) {
            seen = a->takes;
            clock_gettime(CLOCK_REALTIME, &dl);
            dl.tv_sec += 2;
        } else if (rc == ETIMEDOUT) {
            return -1;
        }
    }
    return 0;
}

static float clamp_copy(float *dst, const float *src, size_t n)
{
    float peak = 0.0f;
    for (size_t i = 0; i < n; i++) {
        float v = src[i];
        if (v != v) v = 0.0f;          /* NaN */
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        dst[i] = v;
        if (v > peak) peak = v;
        if (-v > peak) peak = -v;
    }
    return peak;
}

/* About every 10 s of device takes: one line of what was written and what the device took. */
static void log_progress(RtHostAudio *a)
{
    pthread_mutex_lock(&a->mu);
    unsigned long long takes = a->takes - a->logged_takes, under = a->underrun - a->logged_underrun;
    if (takes < 1000) { pthread_mutex_unlock(&a->mu); return; }
    unsigned long long written = a->written;
    float peak = a->peak;
    a->logged_takes = a->takes; a->logged_underrun = a->underrun; a->written = 0; a->peak = 0.0f;
    unsigned long long stalls = a->stalls;
    pthread_mutex_unlock(&a->mu);
    rt_log(RT_CH_FILE, RT_LL_INFO, "audio: coreaudio output: %llu frames written (peak %.3f); the device took %llu buffers, "
           "%llu frames of them silence for want of data; %llu stalls so far", written, (double)peak, takes, under, stalls);
}

RtHostAudio *rt_host_audio_open(unsigned rate, unsigned channels, float volume)
{
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 8) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "audio: coreaudio cannot take %u Hz, %u channels", rate, channels);
        return NULL;
    }
    if (!(volume >= 0.0f)) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;

    RtHostAudio *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    pthread_mutex_init(&a->mu, NULL);
    pthread_cond_init(&a->cv, NULL);
    a->rate = rate;
    a->ch = channels;
    a->buf_frames = rate / 100u;
    a->buf_bytes = a->buf_frames * channels * (unsigned)sizeof(float);
    a->target = rate / 1000u * TARGET_MS;
    a->cap = rate / 1000u * CAPACITY_MS;
    a->ring.buf = calloc((size_t)a->cap * channels, sizeof(float));
    a->ring.cap = a->cap;
    a->ring.ch = channels;
    a->scratch = calloc((size_t)a->cap * channels, sizeof(float));
    if (!a->ring.buf || !a->scratch) {
        rt_host_audio_close(a);
        return NULL;
    }

    AudioStreamBasicDescription fmt;
    memset(&fmt, 0, sizeof fmt);
    fmt.mSampleRate = rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    fmt.mChannelsPerFrame = channels;
    fmt.mBitsPerChannel = 32;
    fmt.mBytesPerFrame = channels * 4u;
    fmt.mBytesPerPacket = channels * 4u;
    fmt.mFramesPerPacket = 1;

    OSStatus st = AudioQueueNewOutput(&fmt, on_buffer, a, NULL, NULL, 0, &a->q);
    if (st != noErr) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "audio: AudioQueueNewOutput failed (%d); no sink opened", (int)st);
        a->q = NULL;
        rt_host_audio_close(a);
        return NULL;
    }
    for (int i = 0; i < QUEUE_BUFFERS; i++) {
        st = AudioQueueAllocateBuffer(a->q, a->buf_bytes, &a->bufs[i]);
        if (st != noErr) {
            rt_log(RT_CH_FILE, RT_LL_WARN, "audio: AudioQueueAllocateBuffer failed (%d); no sink opened", (int)st);
            rt_host_audio_close(a);
            return NULL;
        }
        fill_buffer(a, a->bufs[i]);          /* silence first; enqueues it */
    }
    a->underrun = 0;                         /* the priming silence is not an underrun */
    st = AudioQueueSetParameter(a->q, kAudioQueueParam_Volume, volume);
    if (st != noErr)
        rt_log(RT_CH_FILE, RT_LL_WARN, "audio: cannot set the volume (%d); playing at the device's volume", (int)st);
    st = AudioQueueStart(a->q, NULL);
    if (st != noErr) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "audio: AudioQueueStart failed (%d); no sink opened", (int)st);
        rt_host_audio_close(a);
        return NULL;
    }
    return a;
}

void rt_host_audio_write(RtHostAudio *a, const float *interleaved, unsigned frames)
{
    if (!a || !interleaved) return;
    unsigned done = 0;
    while (done < frames) {
        unsigned k = frames - done;
        if (k > a->cap) k = a->cap;
        float peak = clamp_copy(a->scratch, interleaved + (size_t)done * a->ch, (size_t)k * a->ch);
        unsigned off = 0;
        while (off < k) {
            pthread_mutex_lock(&a->mu);
            if (wait_for_room(a) < 0) {
                a->stalls++;
                pthread_mutex_unlock(&a->mu);
                /* The device stopped taking (an iOS audio-session interruption stops the
                 * queue); starting it again resumes output once the session is back. */
                OSStatus st = AudioQueueStart(a->q, NULL);
                rt_log(RT_CH_FILE, RT_LL_WARN, "audio: coreaudio took nothing for 2 s; restarting the queue (%d)", (int)st);
                return;
            }
            unsigned room = a->cap - ring_count(&a->ring);
            unsigned put = k - off < room ? k - off : room;
            ring_put(&a->ring, a->scratch + (size_t)off * a->ch, put);   /* put <= room: fits */
            a->written += put;
            if (peak > a->peak) a->peak = peak;
            pthread_mutex_unlock(&a->mu);
            off += put;
        }
        done += k;
    }
    log_progress(a);
}

void rt_host_audio_close(RtHostAudio *a)
{
    if (!a) return;
    if (a->q) {
        AudioQueueStop(a->q, true);
        AudioQueueDispose(a->q, true);     /* synchronous: no callback runs after this */
    }
    pthread_mutex_lock(&a->mu);
    rt_log(RT_CH_FILE, RT_LL_INFO, "audio: coreaudio closed: %llu underrun frames, %llu stalls",
           a->underrun, a->stalls);
    pthread_mutex_unlock(&a->mu);
    pthread_cond_destroy(&a->cv);
    pthread_mutex_destroy(&a->mu);
    free(a->ring.buf);
    free(a->scratch);
    free(a);
}
