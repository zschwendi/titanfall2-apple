/* The mixing core of the XAudio2 voices (rt/xamix.h; runtime/xaudio/mix.c).  One T_CASE per case of
 * the brief (1 to 13).  The expected values are written here by hand from the brief's rules.
 */
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "rt/test.h"
#include "rt/xamix.h"

#define NEAR(a, b, tol) (fabsf((float)(a) - (float)(b)) <= (tol))
#define TOL 1e-4f

static XmFormat fmt_of(uint32_t rate, uint16_t ch, uint16_t bits, bool fl)
{
    XmFormat f = { rate, ch, bits, fl };
    return f;
}

static unsigned mix1(XmVoice *v, float *out, unsigned frames, unsigned rate, unsigned ch, XmEvent *ev, unsigned max_ev)
{
    return xm_mix(&v, 1, out, frames, rate, ch, ev, max_ev);
}

static bool all_zero(const float *x, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
        if (x[i] != 0.0f) return false;
    return true;
}

static bool ev_is(const XmEvent *e, XmEventKind k, void *ctx)
{
    return e->kind == k && e->context == ctx;
}

/* Expected source frame index of output k for case 7 (play 0..9, loop 6..9, two extra passes). */
static unsigned seq7(unsigned k)
{
    return k < 10 ? k : 6 + (k - 10) % 4;
}

static float sine_src[22050];
static float sine_out[30000];

TEST(core, xamix)
{
    T_CASE("1: 16-bit mono ramp at 48000 into mono: samples/32768, BUFFER_START/END, frames_played, then silence");
    {
        int16_t s[100];
        for (int i = 0; i < 100; i++) s[i] = (int16_t)(i * 300 - 15000);
        XmFormat f = fmt_of(48000, 1, 16, false);
        XmVoice *v = xm_voice_new(&f, (void *)0x11);
        XmBuffer b = { 0 };
        b.data = s; b.bytes = sizeof s; b.context = (void *)0x22;
        T_EQ(xm_voice_submit(v, &b), 0);
        xm_voice_start(v);
        float out[100];
        XmEvent ev[8];
        unsigned n = mix1(v, out, 100, 48000, 1, ev, 8);
        bool ok = true;
        for (int i = 0; i < 100; i++) ok = ok && NEAR(out[i], s[i] / 32768.0f, TOL);
        T_TRUE(ok);
        T_EQ(n, 2u);
        T_TRUE(ev[0].kind == XM_EV_BUFFER_START && ev[0].context == (void *)0x22 && ev[0].tag == (void *)0x11);
        T_TRUE(ev[1].kind == XM_EV_BUFFER_END && ev[1].context == (void *)0x22);
        unsigned q = 99;
        uint64_t fp = 99;
        void *cc = NULL;
        xm_voice_state(v, &q, &fp, &cc);
        T_EQ(q, 0u);
        T_TRUE(fp == 100);
        n = mix1(v, out, 100, 48000, 1, ev, 8);
        T_EQ(n, 0u);
        T_TRUE(all_zero(out, 100));
        xm_voice_free(v);
    }

    T_CASE("2: float stereo, volume 0.5 and channel volumes {1,0}: left halved, right silent; a swap matrix swaps");
    {
        float s[8];
        for (int i = 0; i < 4; i++) { s[2 * i] = 0.25f; s[2 * i + 1] = -0.5f; }
        XmFormat f = fmt_of(48000, 2, 32, true);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.data = s; b.bytes = sizeof s;
        T_EQ(xm_voice_submit(v, &b), 0);
        xm_voice_start(v);
        xm_voice_set_volume(v, 0.5f);
        float cv[2] = { 1.0f, 0.0f };
        xm_voice_set_channel_volumes(v, cv, 2);
        float out[8];
        XmEvent ev[8];
        mix1(v, out, 4, 48000, 2, ev, 8);
        bool ok = true;
        for (int i = 0; i < 4; i++) ok = ok && NEAR(out[2 * i], 0.125f, TOL) && out[2 * i + 1] == 0.0f;
        T_TRUE(ok);
        xm_voice_free(v);

        v = xm_voice_new(&f, NULL);
        T_EQ(xm_voice_submit(v, &b), 0);
        xm_voice_start(v);
        float swap[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
        xm_voice_set_matrix(v, 2, swap);
        mix1(v, out, 4, 48000, 2, ev, 8);
        ok = true;
        for (int i = 0; i < 4; i++) ok = ok && NEAR(out[2 * i], -0.5f, TOL) && NEAR(out[2 * i + 1], 0.25f, TOL);
        T_TRUE(ok);
        xm_voice_free(v);
    }

    T_CASE("3: mono to stereo by default: both outputs carry it; stereo to mono: the mean");
    {
        float m[4] = { 0.6f, 0.6f, 0.6f, 0.6f };
        XmFormat f1 = fmt_of(48000, 1, 32, true);
        XmVoice *v = xm_voice_new(&f1, NULL);
        XmBuffer b = { 0 };
        b.data = m; b.bytes = sizeof m;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[8];
        XmEvent ev[8];
        mix1(v, out, 4, 48000, 2, ev, 8);
        bool ok = true;
        for (int i = 0; i < 8; i++) ok = ok && NEAR(out[i], 0.6f, TOL);
        T_TRUE(ok);
        xm_voice_free(v);

        float st[8] = { 0.2f, 0.4f, 0.2f, 0.4f, 0.2f, 0.4f, 0.2f, 0.4f };
        XmFormat f2 = fmt_of(48000, 2, 32, true);
        v = xm_voice_new(&f2, NULL);
        b.data = st; b.bytes = sizeof st;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        mix1(v, out, 4, 48000, 1, ev, 8);
        ok = true;
        for (int i = 0; i < 4; i++) ok = ok && NEAR(out[i], 0.3f, TOL);
        T_TRUE(ok);
        xm_voice_free(v);
    }

    T_CASE("4: 8-bit and 24-bit (and 32-bit integer) samples come out at the documented scale");
    {
        uint8_t s8[3] = { 200, 128, 0 };
        XmFormat f8 = fmt_of(48000, 1, 8, false);
        XmVoice *v = xm_voice_new(&f8, NULL);
        XmBuffer b = { 0 };
        b.data = s8; b.bytes = sizeof s8;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[3];
        XmEvent ev[8];
        mix1(v, out, 3, 48000, 1, ev, 8);
        T_TRUE(NEAR(out[0], 72.0f / 128.0f, TOL));
        T_TRUE(NEAR(out[1], 0.0f, TOL));
        T_TRUE(NEAR(out[2], -1.0f, TOL));
        xm_voice_free(v);

        uint8_t s24[9] = { 0x00, 0x00, 0x40, 0x00, 0x00, 0xC0, 0x00, 0x00, 0x20 };
        XmFormat f24 = fmt_of(48000, 1, 24, false);
        v = xm_voice_new(&f24, NULL);
        b.data = s24; b.bytes = sizeof s24;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        mix1(v, out, 3, 48000, 1, ev, 8);
        T_TRUE(NEAR(out[0], 0.5f, TOL));
        T_TRUE(NEAR(out[1], -0.5f, TOL));
        T_TRUE(NEAR(out[2], 0.25f, TOL));
        xm_voice_free(v);

        uint8_t s32[4] = { 0x00, 0x00, 0x00, 0x40 };
        XmFormat f32 = fmt_of(48000, 1, 32, false);
        v = xm_voice_new(&f32, NULL);
        b.data = s32; b.bytes = sizeof s32;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        mix1(v, out, 1, 48000, 1, ev, 8);
        T_TRUE(NEAR(out[0], 0.5f, TOL));
        xm_voice_free(v);
    }

    T_CASE("5: two voices mixed: the sum");
    {
        float a[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
        float c[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        XmFormat f = fmt_of(48000, 1, 32, true);
        XmVoice *v1 = xm_voice_new(&f, NULL), *v2 = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.data = a; b.bytes = sizeof a; xm_voice_submit(v1, &b);
        b.data = c; xm_voice_submit(v2, &b);
        xm_voice_start(v1);
        xm_voice_start(v2);
        XmVoice *vs[2] = { v1, v2 };
        float out[4];
        XmEvent ev[16];
        xm_mix(vs, 2, out, 4, 48000, 1, ev, 16);
        bool ok = true;
        for (int i = 0; i < 4; i++) ok = ok && NEAR(out[i], 0.75f, TOL);
        T_TRUE(ok);
        xm_voice_free(v1);
        xm_voice_free(v2);
    }

    T_CASE("6: two buffers queued play back to back across 7-frame mixes; events in order; queued 2, 1, 0");
    {
        float a[10], c[10];
        for (int i = 0; i < 10; i++) { a[i] = 0.01f * (float)(i + 1); c[i] = 0.01f * (float)(i + 11); }
        XmFormat f = fmt_of(48000, 1, 32, true);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.data = a; b.bytes = sizeof a; b.context = (void *)1;
        xm_voice_submit(v, &b);
        b.data = c; b.context = (void *)2;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[7];
        XmEvent all[16];
        unsigned na = 0;
        bool ok = true;
        unsigned qs[3];
        for (int call = 0; call < 3; call++) {
            XmEvent ev[8];
            unsigned n = mix1(v, out, 7, 48000, 1, ev, 8);
            for (unsigned i = 0; i < n; i++) all[na++] = ev[i];
            unsigned q = 0;
            xm_voice_state(v, &q, NULL, NULL);
            qs[call] = q;
            for (int i = 0; i < 7; i++) {
                int g = call * 7 + i;
                float want = g < 20 ? 0.01f * (float)(g + 1) : 0.0f;
                ok = ok && NEAR(out[i], want, TOL);
            }
        }
        T_TRUE(ok);
        T_TRUE(qs[0] == 2 && qs[1] == 1 && qs[2] == 0);
        T_EQ(na, 4u);
        T_TRUE(ev_is(&all[0], XM_EV_BUFFER_START, (void *)1));
        T_TRUE(ev_is(&all[1], XM_EV_BUFFER_END, (void *)1));
        T_TRUE(ev_is(&all[2], XM_EV_BUFFER_START, (void *)2));
        T_TRUE(ev_is(&all[3], XM_EV_BUFFER_END, (void *)2));
        xm_voice_free(v);
    }

    T_CASE("7: loop of 4 frames at the end of a 10-frame play region, loop_count 2: 18 frames, two LOOP_END, BUFFER_END");
    {
        int16_t s[10];
        for (int i = 0; i < 10; i++) s[i] = (int16_t)(i * 1000);
        XmFormat f = fmt_of(48000, 1, 16, false);
        XmBuffer b = { 0 };
        b.data = s; b.bytes = sizeof s; b.context = (void *)7;
        b.loop_begin = 6; b.loop_length = 4; b.loop_count = 2;
        XmVoice *v = xm_voice_new(&f, NULL);
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[32];
        XmEvent ev[8];
        unsigned n = mix1(v, out, 32, 48000, 1, ev, 8);
        bool ok = true;
        for (unsigned k = 0; k < 32; k++) {
            float want = k < 18 ? s[seq7(k)] / 32768.0f : 0.0f;
            ok = ok && NEAR(out[k], want, TOL);
        }
        T_TRUE(ok);
        T_EQ(n, 4u);
        T_TRUE(ev[0].kind == XM_EV_BUFFER_START);
        T_TRUE(ev[1].kind == XM_EV_LOOP_END);
        T_TRUE(ev[2].kind == XM_EV_LOOP_END);
        T_TRUE(ev[3].kind == XM_EV_BUFFER_END && ev[3].context == (void *)7);
        xm_voice_free(v);

        XmVoice *w = xm_voice_new(&f, NULL);
        xm_voice_submit(w, &b);
        xm_voice_start(w);
        mix1(w, out, 3, 48000, 1, ev, 8);
        xm_voice_exit_loop(w);
        n = mix1(w, out, 32, 48000, 1, ev, 8);
        ok = true;
        for (unsigned k = 0; k < 32; k++) {
            float want = k < 7 ? s[k + 3] / 32768.0f : 0.0f;
            ok = ok && NEAR(out[k], want, TOL);
        }
        T_TRUE(ok);
        T_EQ(n, 1u);
        T_TRUE(ev[0].kind == XM_EV_BUFFER_END);
        xm_voice_free(w);

        b.loop_count = 255;
        XmVoice *u = xm_voice_new(&f, NULL);
        xm_voice_submit(u, &b);
        xm_voice_start(u);
        static float long_out[1200];
        static XmEvent many[1000];
        n = mix1(u, long_out, 1200, 48000, 1, many, 1000);
        ok = true;
        for (unsigned k = 0; k < 1200; k++) ok = ok && NEAR(long_out[k], s[seq7(k)] / 32768.0f, TOL);
        T_TRUE(ok);
        T_TRUE(n > 100 && n < 1000);                       /* one LOOP_END per pass of 4 frames */
        T_TRUE(many[0].kind == XM_EV_BUFFER_START);
        T_TRUE(many[1].kind == XM_EV_LOOP_END);
        T_TRUE(many[n - 1].kind == XM_EV_LOOP_END);
        uint64_t fp = 0;
        unsigned q = 0;
        xm_voice_state(u, &q, &fp, NULL);
        T_EQ(q, 1u);
        T_TRUE(fp == 1200);
        xm_voice_free(u);
    }

    T_CASE("8: end_of_stream: STREAM_END after BUFFER_END, and frames_played restarts at 0");
    {
        float a[4] = { 0.1f, 0.1f, 0.1f, 0.1f };
        float c[4] = { 0.2f, 0.2f, 0.2f, 0.2f };
        XmFormat f = fmt_of(48000, 1, 32, true);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.data = a; b.bytes = sizeof a; b.context = (void *)8;
        b.end_of_stream = true;
        xm_voice_submit(v, &b);
        b.data = c; b.bytes = sizeof c; b.context = (void *)9; b.end_of_stream = false;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[8];
        XmEvent ev[8];
        unsigned n = mix1(v, out, 8, 48000, 1, ev, 8);
        T_EQ(n, 5u);
        T_TRUE(ev[0].kind == XM_EV_BUFFER_START && ev[0].context == (void *)8);
        T_TRUE(ev[1].kind == XM_EV_BUFFER_END && ev[1].context == (void *)8);
        T_TRUE(ev[2].kind == XM_EV_STREAM_END && ev[2].context == (void *)8);
        T_TRUE(ev[3].kind == XM_EV_BUFFER_START && ev[3].context == (void *)9);
        T_TRUE(ev[4].kind == XM_EV_BUFFER_END && ev[4].context == (void *)9);
        uint64_t fp = 99;
        xm_voice_state(v, NULL, &fp, NULL);
        T_TRUE(fp == 4);
        xm_voice_free(v);
    }

    T_CASE("9: resampling: 24000 into 48000 doubles the length; ratio 2 halves it; 441 Hz sine 44100 -> 48000");
    {
        float a[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
        XmFormat f = fmt_of(24000, 1, 32, true);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.data = a; b.bytes = sizeof a;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[20];
        XmEvent ev[8];
        mix1(v, out, 20, 48000, 1, ev, 8);
        float want[8] = { 0.1f, 0.15f, 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.4f };
        bool ok = true;
        for (int i = 0; i < 8; i++) ok = ok && NEAR(out[i], want[i], TOL);
        for (int i = 8; i < 20; i++) ok = ok && out[i] == 0.0f;
        T_TRUE(ok);
        xm_voice_free(v);

        float e[8];
        for (int i = 0; i < 8; i++) e[i] = 0.1f * (float)i;
        XmFormat f48 = fmt_of(48000, 1, 32, true);
        v = xm_voice_new(&f48, NULL);
        b.data = e; b.bytes = sizeof e;
        xm_voice_submit(v, &b);
        xm_voice_set_ratio(v, 2.0f);
        xm_voice_start(v);
        mix1(v, out, 20, 48000, 1, ev, 8);
        ok = true;
        for (int i = 0; i < 4; i++) ok = ok && NEAR(out[i], 0.2f * (float)i, TOL);
        for (int i = 4; i < 20; i++) ok = ok && out[i] == 0.0f;
        T_TRUE(ok);
        xm_voice_free(v);

        for (int i = 0; i < 22050; i++)
            sine_src[i] = 2.0f + sinf((float)(2.0 * M_PI * 441.0 * i / 44100.0));
        XmFormat f44 = fmt_of(44100, 1, 32, true);
        v = xm_voice_new(&f44, NULL);
        b.data = sine_src; b.bytes = sizeof sine_src;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        unsigned n = mix1(v, sine_out, 30000, 48000, 1, ev, 64);
        unsigned count = 0;
        while (count < 30000 && sine_out[count] != 0.0f) count++;
        T_TRUE(count >= 23998 && count <= 24002);
        T_TRUE(n >= 2);
        float worst = 0.0f;
        for (unsigned k = 100; k + 100 < count; k++) {
            float d = fabsf(sine_out[k] - 2.0f - sinf((float)(2.0 * M_PI * 441.0 * k / 48000.0)));
            if (d > worst) worst = d;
        }
        T_TRUE(worst < 0.01f);
        xm_voice_free(v);
    }

    T_CASE("10: flush: the playing buffer plays on, the two queued get BUFFER_END at the next mix; stop keeps the place");
    {
        float b1[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
        float b2[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        float b3[4] = { 0.6f, 0.6f, 0.6f, 0.6f };
        XmFormat f = fmt_of(48000, 1, 32, true);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.bytes = sizeof b1;
        b.data = b1; b.context = (void *)1; xm_voice_submit(v, &b);
        b.data = b2; b.context = (void *)2; xm_voice_submit(v, &b);
        b.data = b3; b.context = (void *)3; xm_voice_submit(v, &b);
        xm_voice_start(v);
        float out[4];
        XmEvent ev[8];
        mix1(v, out, 2, 48000, 1, ev, 8);
        xm_voice_flush(v);
        unsigned q = 0;
        xm_voice_state(v, &q, NULL, NULL);
        T_EQ(q, 1u);
        unsigned n = mix1(v, out, 2, 48000, 1, ev, 8);
        T_EQ(n, 3u);
        T_TRUE(ev_is(&ev[0], XM_EV_BUFFER_END, (void *)2));
        T_TRUE(ev_is(&ev[1], XM_EV_BUFFER_END, (void *)3));
        T_TRUE(ev_is(&ev[2], XM_EV_BUFFER_END, (void *)1));
        T_TRUE(NEAR(out[0], 0.3f, TOL) && NEAR(out[1], 0.4f, TOL));
        xm_voice_free(v);

        float s[8];
        for (int i = 0; i < 8; i++) s[i] = 0.1f * (float)(i + 1);
        v = xm_voice_new(&f, NULL);
        b.data = s; b.bytes = sizeof s; b.context = (void *)4;
        xm_voice_submit(v, &b);
        xm_voice_start(v);
        mix1(v, out, 3, 48000, 1, ev, 8);
        xm_voice_stop(v);
        n = mix1(v, out, 4, 48000, 1, ev, 8);
        T_EQ(n, 0u);
        T_TRUE(all_zero(out, 4));
        uint64_t fp = 0;
        xm_voice_state(v, NULL, &fp, NULL);
        T_TRUE(fp == 3);
        xm_voice_start(v);
        float out5[5];
        mix1(v, out5, 5, 48000, 1, ev, 8);
        bool ok = true;
        for (int i = 0; i < 5; i++) ok = ok && NEAR(out5[i], s[i + 3], TOL);
        T_TRUE(ok);
        xm_voice_free(v);
    }

    T_CASE("11: bytes_needed: stopped 0; started and empty 1920 (480 frames, 16-bit stereo); 100 frames queued 1520");
    {
        int16_t s[200];
        memset(s, 0, sizeof s);
        XmFormat f = fmt_of(48000, 2, 16, false);
        XmVoice *v = xm_voice_new(&f, NULL);
        T_EQ(xm_voice_bytes_needed(v, 480, 48000), 0u);
        xm_voice_start(v);
        T_EQ(xm_voice_bytes_needed(v, 480, 48000), 1920u);
        XmBuffer b = { 0 };
        b.data = s; b.bytes = 100 * 4;
        xm_voice_submit(v, &b);
        T_EQ(xm_voice_bytes_needed(v, 480, 48000), 1520u);
        xm_voice_free(v);
    }

    T_CASE("12: submit: each bad buffer gives -2; the 65th gives -1");
    {
        static uint8_t d[40];
        XmFormat f = fmt_of(48000, 2, 16, false);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer bad[9];
        memset(bad, 0, sizeof bad);
        bad[0].data = NULL; bad[0].bytes = 40;
        bad[1].data = d; bad[1].bytes = 0;
        bad[2].data = d; bad[2].bytes = 6;
        bad[3].data = d; bad[3].bytes = 40; bad[3].play_begin = 10;
        bad[4].data = d; bad[4].bytes = 40; bad[4].play_begin = 5; bad[4].play_length = 8;
        bad[5].data = d; bad[5].bytes = 40; bad[5].loop_count = 1; bad[5].loop_begin = 6; bad[5].loop_length = 6;
        bad[6].data = d; bad[6].bytes = 40; bad[6].loop_count = 0; bad[6].loop_begin = 2;
        bad[7].data = d; bad[7].bytes = 40; bad[7].play_begin = 2; bad[7].loop_count = 1; bad[7].loop_begin = 1; bad[7].loop_length = 2;
        bad[8].data = d; bad[8].bytes = 40; bad[8].loop_count = 1; bad[8].loop_begin = 9; bad[8].loop_length = 3;
        bool ok = true;
        for (int i = 0; i < 9; i++) ok = ok && xm_voice_submit(v, &bad[i]) == -2;
        T_TRUE(ok);
        unsigned q = 99;
        xm_voice_state(v, &q, NULL, NULL);
        T_EQ(q, 0u);
        XmBuffer good = { 0 };
        good.data = d; good.bytes = 40;
        ok = true;
        for (int i = 0; i < 64; i++) ok = ok && xm_voice_submit(v, &good) == 0;
        T_TRUE(ok);
        T_EQ(xm_voice_submit(v, &good), -1);
        xm_voice_state(v, &q, NULL, NULL);
        T_EQ(q, 64u);
        xm_voice_free(v);
    }

    T_CASE("13: max_ev 1 with two buffers that both end inside one mix: one event a call, none lost");
    {
        float a[2] = { 0.1f, 0.2f };
        float c[2] = { 0.3f, 0.4f };
        XmFormat f = fmt_of(48000, 1, 32, true);
        XmVoice *v = xm_voice_new(&f, NULL);
        XmBuffer b = { 0 };
        b.data = a; b.bytes = sizeof a; b.context = (void *)1; xm_voice_submit(v, &b);
        b.data = c; b.context = (void *)2; xm_voice_submit(v, &b);
        xm_voice_start(v);
        XmEvent got[16];
        unsigned ng = 0;
        bool one_each = true;
        for (int call = 0; call < 12; call++) {
            XmEvent ev[4];
            float out[4];
            unsigned n = mix1(v, out, 4, 48000, 1, ev, 1);
            if (n > 1) one_each = false;
            for (unsigned i = 0; i < n && ng < 16; i++) got[ng++] = ev[i];
        }
        T_TRUE(one_each);
        T_EQ(ng, 4u);
        T_TRUE(ev_is(&got[0], XM_EV_BUFFER_START, (void *)1));
        T_TRUE(ev_is(&got[1], XM_EV_BUFFER_END, (void *)1));
        T_TRUE(ev_is(&got[2], XM_EV_BUFFER_START, (void *)2));
        T_TRUE(ev_is(&got[3], XM_EV_BUFFER_END, (void *)2));
        xm_voice_free(v);
    }
}
