/* Where mixed sound goes (rt/audio.h).  The waiting sinks use CLOCK_MONOTONIC and nanosleep.
 * Logging goes to RT_CH_FILE (no audio channel exists). */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt/audio.h"
#include "rt/host_audio.h"
#include "rt/log.h"

#define NS_PER_S 1000000000LL
#define LATE_NS  (100LL * 1000000LL)

typedef enum { SINK_NONE, SINK_FILE, SINK_FILE_FAST, SINK_COREAUDIO } SinkKind;

struct RtAudioSink {
    SinkKind kind;
    unsigned rate, channels;
    unsigned long long frames;   /* frames taken so far */
    long long start_ns;          /* the schedule's zero (waiting sinks) */
    int started;
    FILE *f;                     /* WAV file, or NULL */
    RtHostAudio *host;           /* the speakers (coreaudio), or NULL; the host paces it */
};

static long long now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * NS_PER_S + ts.tv_nsec;
}

static void sleep_until_ns(long long t)
{
    for (;;) {
        long long d = t - now_ns();
        if (d <= 0) return;
        struct timespec req = { (time_t)(d / NS_PER_S), (long)(d % NS_PER_S) };
        if (nanosleep(&req, NULL) != 0 && errno != EINTR) return;
    }
}

static void put_u32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void put_u16(unsigned char *p, uint16_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
}

/* The 44-byte header; the two lengths are zero until close. */
static void write_header(FILE *f, unsigned rate, unsigned channels)
{
    unsigned char h[44];
    uint32_t block = channels * 4u;
    memcpy(h + 0, "RIFF", 4);  put_u32(h + 4, 0);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4); put_u32(h + 16, 16);
    put_u16(h + 20, 3);                       /* IEEE float */
    put_u16(h + 22, (uint16_t)channels);
    put_u32(h + 24, rate);
    put_u32(h + 28, rate * block);
    put_u16(h + 32, (uint16_t)block);
    put_u16(h + 34, 32);
    memcpy(h + 36, "data", 4); put_u32(h + 40, 0);
    fwrite(h, 1, sizeof h, f);
}

float rt_audio_sink_volume_from_env(void)
{
    const char *v = getenv("RT_HOST_AUDIO_VOLUME");
    if (v == NULL)
        return 1.0f;
    char *end = NULL;
    double d = strtod(v, &end);
    if (end == v || *end != '\0' || !(d >= 0.0 && d <= 1.0)) {
        rt_log(RT_CH_FILE, RT_LL_WARN, "audio: RT_HOST_AUDIO_VOLUME=%s is not a number from 0 to 1; using 1", v);
        return 1.0f;
    }
    return (float)d;
}

RtAudioSink *rt_audio_sink_open(unsigned rate, unsigned channels)
{
    if (rate < 8000 || rate > 192000 || channels < 1 || channels > 8)
        return NULL;

    const char *v = getenv("RT_HOST_AUDIO");
    SinkKind kind;
    const char *path = NULL;
    if (v == NULL || v[0] == '\0' || strcmp(v, "none") == 0) {
        kind = SINK_NONE;
    } else if (strcmp(v, "coreaudio") == 0) {
        kind = SINK_COREAUDIO;
    } else if (strncmp(v, "file-fast:", 10) == 0) {
        kind = SINK_FILE_FAST; path = v + 10;
    } else if (strncmp(v, "file:", 5) == 0) {
        kind = SINK_FILE; path = v + 5;
    } else {
        rt_log(RT_CH_FILE, RT_LL_WARN, "audio: RT_HOST_AUDIO=%s is not a known sink; no sink opened", v);
        return NULL;
    }

    RtHostAudio *host = NULL;
    if (kind == SINK_COREAUDIO) {
        float volume = rt_audio_sink_volume_from_env();
        host = rt_host_audio_open(rate, channels, volume);
        if (!host)
            return NULL;                 /* rt_host_audio_open has logged why */
        rt_log(RT_CH_FILE, RT_LL_INFO, "audio: coreaudio: %u Hz, %u channels, volume %g", rate, channels, (double)volume);
    }

    FILE *f = NULL;
    if (path) {
        f = fopen(path, "wb");
        if (!f) {
            rt_log(RT_CH_FILE, RT_LL_WARN, "audio: cannot create %s; no sink opened", path);
            return NULL;
        }
        write_header(f, rate, channels);
    }

    RtAudioSink *s = calloc(1, sizeof *s);
    if (!s) {
        if (f) fclose(f);
        if (host) rt_host_audio_close(host);
        return NULL;
    }
    s->kind = kind;
    s->host = host;
    s->rate = rate;
    s->channels = channels;
    s->f = f;
    return s;
}

void rt_audio_sink_write(RtAudioSink *s, const float *interleaved, unsigned frames)
{
    if (!s) return;

    if (s->kind == SINK_COREAUDIO) {
        s->frames += frames;
        rt_host_audio_write(s->host, interleaved, frames);   /* the host waits; no clock here */
        return;
    }

    if (s->f && frames)
        fwrite(interleaved, sizeof(float) * s->channels, frames, s->f);
    s->frames += frames;

    if (s->kind == SINK_FILE_FAST)
        return;

    long long now = now_ns();
    if (!s->started) {
        s->started = 1;
        s->start_ns = now;
    }
    long long target = s->start_ns + (long long)(s->frames * (unsigned long long)NS_PER_S / s->rate);
    if (now - target > LATE_NS) {
        s->start_ns += now - target;   /* fell behind: move the start, do not catch up */
        return;
    }
    sleep_until_ns(target);
}

unsigned long long rt_audio_sink_frames(const RtAudioSink *s)
{
    return s ? s->frames : 0;
}

const char *rt_audio_sink_kind(const RtAudioSink *s)
{
    if (!s) return "none";
    switch (s->kind) {
    case SINK_FILE: return "file";
    case SINK_FILE_FAST: return "file-fast";
    case SINK_COREAUDIO: return "coreaudio";
    default: return "none";
    }
}

void rt_audio_sink_close(RtAudioSink *s)
{
    if (!s) return;
    if (s->host) rt_host_audio_close(s->host);
    if (s->f) {
        unsigned long long data = s->frames * s->channels * 4ull;
        unsigned char b[4];
        put_u32(b, (uint32_t)(36ull + data));
        fseek(s->f, 4, SEEK_SET);
        fwrite(b, 1, 4, s->f);
        put_u32(b, (uint32_t)data);
        fseek(s->f, 40, SEEK_SET);
        fwrite(b, 1, 4, s->f);
        fclose(s->f);
    }
    free(s);
}
