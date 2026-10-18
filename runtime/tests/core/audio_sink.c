/* The audio sinks (rt/audio.h; core/audio_sink.c): the silent sink keeps time, the file sink
 * writes a WAV file that is read back here.  The expected values are written here by hand
 * from the brief.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "rt/test.h"
#include "rt/audio.h"
#include "rt/host_audio.h"

static double ms_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void sleep_ms(long ms)
{
    struct timespec req = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&req, NULL);
}

static void set_env(const char *v)
{
    if (v) setenv("RT_HOST_AUDIO", v, 1);
    else unsetenv("RT_HOST_AUDIO");
}

/* RT_HOST_AUDIO_VOLUME: NULL unsets it. */
static void set_volume(const char *v)
{
    if (v) setenv("RT_HOST_AUDIO_VOLUME", v, 1);
    else unsetenv("RT_HOST_AUDIO_VOLUME");
}

static void put_u32(const unsigned char *p, unsigned long *out)
{
    *out = (unsigned long)p[0] | ((unsigned long)p[1] << 8) | ((unsigned long)p[2] << 16) | ((unsigned long)p[3] << 24);
}

static void put_u16(const unsigned char *p, unsigned long *out)
{
    *out = (unsigned long)p[0] | ((unsigned long)p[1] << 8);
}

static void write_ten(RtAudioSink *s, const float *buf)
{
    for (int i = 0; i < 10; i++)
        rt_audio_sink_write(s, buf, 480);
}

TEST(core, audio_sink)
{
    float buf[480 * 2];
    memset(buf, 0, sizeof buf);

    T_CASE("rule 1: unset gives the silent sink; 4800 frames take about 100 ms");
    set_env(NULL);
    {
        RtAudioSink *s = rt_audio_sink_open(48000, 1);
        T_TRUE(s != NULL);
        if (s) {
            T_TRUE(strcmp(rt_audio_sink_kind(s), "none") == 0);
            double t0 = ms_now();
            write_ten(s, buf);
            double el = ms_now() - t0;
            T_TRUE(el >= 80.0 && el <= 400.0);
            T_EQ(rt_audio_sink_frames(s), 4800ull);
            rt_audio_sink_close(s);
        }
    }

    T_CASE("rule 2: file-fast writes a WAV file of 1000 stereo frames, no waiting");
    {
        char path[512];
        snprintf(path, sizeof path, "%s/fast.wav", rt_test_state_dir());
        char env[600];
        snprintf(env, sizeof env, "file-fast:%s", path);
        set_env(env);
        RtAudioSink *s = rt_audio_sink_open(48000, 2);
        T_TRUE(s != NULL);
        if (s) {
            T_TRUE(strcmp(rt_audio_sink_kind(s), "file-fast") == 0);
            float fr[1000 * 2];
            for (int i = 0; i < 1000; i++) {
                fr[2 * i] = i * 0.5f - 100.0f;      /* exact in float; not clamped */
                fr[2 * i + 1] = -(i * 0.25f) - 3.0f;
            }
            double t0 = ms_now();
            rt_audio_sink_write(s, fr, 1000);
            T_TRUE(ms_now() - t0 < 50.0);
            rt_audio_sink_close(s);

            FILE *f = fopen(path, "rb");
            T_TRUE(f != NULL);
            if (f) {
                unsigned char h[44];
                size_t n = fread(h, 1, 44, f);
                T_EQ(n, 44u);
                unsigned long v;
                T_TRUE(memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0);
                put_u32(h + 4, &v);  T_EQ(v, 36ul + 8000ul);
                T_TRUE(memcmp(h + 12, "fmt ", 4) == 0);
                put_u32(h + 16, &v); T_EQ(v, 16ul);
                put_u16(h + 20, &v); T_EQ(v, 3ul);
                put_u16(h + 22, &v); T_EQ(v, 2ul);
                put_u32(h + 24, &v); T_EQ(v, 48000ul);
                put_u32(h + 28, &v); T_EQ(v, 384000ul);
                put_u16(h + 32, &v); T_EQ(v, 8ul);
                put_u16(h + 34, &v); T_EQ(v, 32ul);
                T_TRUE(memcmp(h + 36, "data", 4) == 0);
                put_u32(h + 40, &v); T_EQ(v, 8000ul);

                float back[1000 * 2];
                size_t got = fread(back, sizeof(float), 2000, f);
                T_EQ(got, 2000u);
                int same = 1;
                for (int i = 0; i < 2000; i++)
                    if (back[i] != fr[i]) same = 0;
                T_TRUE(same);
                T_TRUE(fgetc(f) == EOF);
                fclose(f);
            }
        }
    }

    T_CASE("rule 3: file: waits like the silent sink");
    {
        char path[512];
        snprintf(path, sizeof path, "%s/timed.wav", rt_test_state_dir());
        char env[600];
        snprintf(env, sizeof env, "file:%s", path);
        set_env(env);
        RtAudioSink *s = rt_audio_sink_open(48000, 1);
        T_TRUE(s != NULL);
        if (s) {
            T_TRUE(strcmp(rt_audio_sink_kind(s), "file") == 0);
            double t0 = ms_now();
            write_ten(s, buf);
            double el = ms_now() - t0;
            T_TRUE(el >= 80.0 && el <= 400.0);
            rt_audio_sink_close(s);
        }
    }

    T_CASE("rule 4: a 300 ms pause is not caught up; the next write waits its normal time");
    set_env(NULL);
    {
        RtAudioSink *s = rt_audio_sink_open(48000, 1);
        T_TRUE(s != NULL);
        if (s) {
            rt_audio_sink_write(s, buf, 480);   /* notes the clock; waits 10 ms */
            sleep_ms(300);
            double t0 = ms_now();
            rt_audio_sink_write(s, buf, 480);   /* 300 ms late: must not wait */
            double el = ms_now() - t0;
            T_TRUE(el < 50.0);
            t0 = ms_now();
            rt_audio_sink_write(s, buf, 480);   /* normal time: 10 ms */
            el = ms_now() - t0;
            T_TRUE(el >= 5.0 && el < 60.0);
            rt_audio_sink_close(s);
        }
    }

    T_CASE("rule 5: nonsense, rate 0 and 9 channels give NULL");
    /* The case for the coreaudio value was removed: opening that sink opens the Mac's speakers,
     * and no test does that.  It is covered by the owner's run of make host-audio-tone. */
    set_env("nonsense");
    T_TRUE(rt_audio_sink_open(48000, 2) == NULL);
    set_env(NULL);
    T_TRUE(rt_audio_sink_open(0, 2) == NULL);
    T_TRUE(rt_audio_sink_open(48000, 9) == NULL);
    set_env(NULL);

    T_CASE("the coreaudio ring self-test passes; it opens no device");
    T_TRUE(rt_host_audio_ring_selftest());

    T_CASE("RT_HOST_AUDIO_VOLUME: unset 1, 0.25 0.25, 2 and x 1 (the fallback)");
    set_volume(NULL);
    T_TRUE(rt_audio_sink_volume_from_env() == 1.0f);
    set_volume("0.25");
    T_TRUE(rt_audio_sink_volume_from_env() == 0.25f);
    set_volume("2");
    T_TRUE(rt_audio_sink_volume_from_env() == 1.0f);
    set_volume("x");
    T_TRUE(rt_audio_sink_volume_from_env() == 1.0f);
    set_volume(NULL);
}
