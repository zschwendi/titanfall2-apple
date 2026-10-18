/* The mixing core of the XAudio2 source voices (rt/xamix.h).  Plain C: no lock, no allocation
 * inside xm_mix, no global state.
 *
 * Playback reads one source frame at a time (fetch) and keeps two frames, f0 and f1, with the
 * fractional position t between them.  A voice with the same rate as the output reads only f0
 * (t stays 0), so its events come at the frame that consumes the buffer's last frame.  Events
 * made by a fetch wait in pend[] and are handed out in order as max_ev allows; a voice with
 * events waiting produces no more frames in that call.
 */
#include <stdlib.h>
#include <string.h>

#include "rt/xamix.h"

#define XM_PEND_MAX 8

typedef struct XmEntry {
    const uint8_t *data;
    uint32_t play_begin, play_end;     /* absolute frames in the buffer, play_end > play_begin */
    uint32_t loop_begin, loop_end;     /* absolute; meaningful when loop_count > 0 */
    uint32_t loop_count;
    void *context;
    bool end_of_stream;
    bool started;                      /* BUFFER_START given: the buffer is the playing one */
} XmEntry;

struct XmVoice {
    XmFormat fmt;
    void *tag;
    uint32_t frame_bytes;
    uint32_t sample_bytes;
    XmEntry queue[XM_MAX_QUEUED];      /* ring: head, count */
    unsigned head, count;
    void *ends[XM_MAX_QUEUED];         /* contexts of flushed buffers: BUFFER_END owed at the next mix */
    unsigned n_ends;
    XmEvent pend[XM_PEND_MAX];         /* events made by fetch, not yet handed out */
    unsigned n_pend;
    bool running, exit_loop, in_loop;
    uint32_t cur, passes_left;         /* playback cursor (absolute frame) and extra passes left */
    uint64_t frames_played;
    float volume;
    float chvol[XM_MAX_CHANNELS];
    bool matrix_set;
    unsigned m_out;
    float matrix[XM_MAX_CHANNELS * XM_MAX_CHANNELS];   /* [out * channels + src] */
    double ratio;
    double t;                          /* fractional position between f0 and f1, 0 <= t */
    float f0[XM_MAX_CHANNELS], f1[XM_MAX_CHANNELS];
    bool have0, have1, ended;          /* ended: the source ran out while f1 was wanted; f1 holds f0 */
};

static bool fmt_ok(const XmFormat *f)
{
    if (f->rate < 1000 || f->rate > 200000) return false;
    if (f->channels < 1 || f->channels > XM_MAX_CHANNELS) return false;
    if (f->is_float) return f->bits == 32;
    return f->bits == 8 || f->bits == 16 || f->bits == 24 || f->bits == 32;
}

XmVoice *xm_voice_new(const XmFormat *fmt, void *tag)
{
    if (!fmt || !fmt_ok(fmt)) return NULL;
    XmVoice *v = calloc(1, sizeof *v);
    if (!v) return NULL;
    v->fmt = *fmt;
    v->tag = tag;
    v->sample_bytes = fmt->is_float ? 4 : fmt->bits / 8;
    v->frame_bytes = v->sample_bytes * fmt->channels;
    v->volume = 1.0f;
    for (unsigned c = 0; c < XM_MAX_CHANNELS; c++) v->chvol[c] = 1.0f;
    v->ratio = 1.0;
    return v;
}

void xm_voice_free(XmVoice *v)
{
    free(v);
}

int xm_voice_submit(XmVoice *v, const XmBuffer *b)
{
    if (!b || !b->data || b->bytes == 0 || b->bytes % v->frame_bytes) return -2;
    uint32_t frames = b->bytes / v->frame_bytes;

    uint32_t pb = b->play_begin, pe;
    if (pb >= frames) return -2;
    if (b->play_length == 0) pe = frames;
    else {
        if (b->play_length > frames - pb) return -2;
        pe = pb + b->play_length;
    }

    uint32_t lb = 0, le = 0;
    if (b->loop_count == 0) {
        if (b->loop_begin || b->loop_length) return -2;
    } else {
        lb = b->loop_begin;
        if (lb < pb || lb >= pe) return -2;
        if (b->loop_length == 0) le = pe;
        else {
            if (b->loop_length > pe - lb) return -2;
            le = lb + b->loop_length;
        }
    }

    if (v->count + v->n_ends >= XM_MAX_QUEUED) return -1;

    XmEntry *e = &v->queue[(v->head + v->count) % XM_MAX_QUEUED];
    e->data = b->data;
    e->play_begin = pb;
    e->play_end = pe;
    e->loop_begin = lb;
    e->loop_end = le;
    e->loop_count = b->loop_count;
    e->context = b->context;
    e->end_of_stream = b->end_of_stream;
    e->started = false;
    v->count++;
    return 0;
}

void xm_voice_flush(XmVoice *v)
{
    unsigned keep = (v->count > 0 && v->queue[v->head].started) ? 1 : 0;
    for (unsigned i = keep; i < v->count; i++)
        v->ends[v->n_ends++] = v->queue[(v->head + i) % XM_MAX_QUEUED].context;
    v->count = keep;
}

void xm_voice_exit_loop(XmVoice *v)
{
    v->exit_loop = true;
}

void xm_voice_start(XmVoice *v)
{
    v->running = true;
}

void xm_voice_stop(XmVoice *v)
{
    v->running = false;
}

void xm_voice_set_volume(XmVoice *v, float volume)
{
    v->volume = volume;
}

void xm_voice_set_channel_volumes(XmVoice *v, const float *vol, unsigned n)
{
    if (!vol || n != v->fmt.channels) return;
    for (unsigned c = 0; c < n; c++) v->chvol[c] = vol[c];
}

void xm_voice_set_matrix(XmVoice *v, unsigned out_channels, const float *levels)
{
    if (!levels || out_channels < 1 || out_channels > XM_MAX_CHANNELS) return;
    unsigned n = out_channels * v->fmt.channels;
    for (unsigned i = 0; i < n; i++) v->matrix[i] = levels[i];
    v->m_out = out_channels;
    v->matrix_set = true;
}

void xm_voice_set_ratio(XmVoice *v, float ratio)
{
    if (ratio != ratio) return;                       /* NaN: keep the old ratio */
    if (ratio < 1.0f / 1024.0f) ratio = 1.0f / 1024.0f;
    if (ratio > 1024.0f) ratio = 1024.0f;
    v->ratio = ratio;
}

void xm_voice_state(const XmVoice *v, unsigned *queued, uint64_t *frames_played, void **current_context)
{
    if (queued) *queued = v->count;
    if (frames_played) *frames_played = v->frames_played;
    if (current_context) *current_context = v->count > 0 ? v->queue[v->head].context : NULL;
}

uint32_t xm_voice_bytes_needed(const XmVoice *v, unsigned out_frames, unsigned out_rate)
{
    if (!v->running || out_rate == 0) return 0;
    double step = (double)v->fmt.rate * v->ratio / (double)out_rate;
    uint64_t need = (uint64_t)((double)out_frames * step);
    if ((double)need < (double)out_frames * step) need++;

    uint64_t have = 0;
    for (unsigned i = 0; i < v->count; i++) {
        const XmEntry *e = &v->queue[(v->head + i) % XM_MAX_QUEUED];
        if (e->loop_count == 255) return 0;          /* a loop for ever never runs out */
        uint64_t loop_len = e->loop_count ? (uint64_t)(e->loop_end - e->loop_begin) : 0;
        uint64_t play = e->play_end - e->play_begin;
        if (i > 0 || !e->started) {
            have += play + (uint64_t)e->loop_count * loop_len;
        } else if (v->in_loop) {
            have += (e->loop_end - v->cur) + (uint64_t)(v->passes_left > 0 ? v->passes_left - 1 : 0) * loop_len;
        } else {
            have += (e->play_end - v->cur) + (uint64_t)e->loop_count * loop_len;
        }
    }
    if (have >= need) return 0;
    return (uint32_t)((need - have) * v->frame_bytes);
}

/* ---- the source -------------------------------------------------------------------------- */

static void push_ev(XmVoice *v, XmEventKind kind, void *context)
{
    if (v->n_pend >= XM_PEND_MAX) return;
    XmEvent *x = &v->pend[v->n_pend++];
    x->kind = kind;
    x->tag = v->tag;
    x->context = context;
}

static void read_frame(const XmFormat *f, const uint8_t *p, float *dst)
{
    for (unsigned c = 0; c < f->channels; c++) {
        if (f->is_float) {
            float x;
            memcpy(&x, p + 4 * c, 4);
            dst[c] = x;
        } else if (f->bits == 8) {
            dst[c] = (float)(((double)p[c] - 128.0) / 128.0);
        } else if (f->bits == 16) {
            uint32_t u = (uint32_t)p[2 * c] | ((uint32_t)p[2 * c + 1] << 8);
            int32_t x = u >= 0x8000u ? (int32_t)u - 0x10000 : (int32_t)u;
            dst[c] = (float)((double)x / 32768.0);
        } else if (f->bits == 24) {
            uint32_t u = (uint32_t)p[3 * c] | ((uint32_t)p[3 * c + 1] << 8) | ((uint32_t)p[3 * c + 2] << 16);
            int32_t x = u >= 0x800000u ? (int32_t)u - 0x1000000 : (int32_t)u;
            dst[c] = (float)((double)x / 8388608.0);
        } else {
            uint32_t u = (uint32_t)p[4 * c] | ((uint32_t)p[4 * c + 1] << 8) |
                         ((uint32_t)p[4 * c + 2] << 16) | ((uint32_t)p[4 * c + 3] << 24);
            int64_t x = u >= 0x80000000u ? (int64_t)u - 0x100000000LL : (int64_t)u;
            dst[c] = (float)((double)x / 2147483648.0);
        }
    }
}

static void finish(XmVoice *v, XmEntry *e)
{
    push_ev(v, XM_EV_BUFFER_END, e->context);
    if (e->end_of_stream) {
        push_ev(v, XM_EV_STREAM_END, e->context);
        v->frames_played = 0;
    }
    v->head = (v->head + 1) % XM_MAX_QUEUED;
    v->count--;
    v->in_loop = false;
    v->exit_loop = false;
}

/* Reads the next source frame of the queue into dst.  False when no buffer is queued.  At most
 * three events are made, and only when no event is waiting (the caller drains first). */
static bool fetch(XmVoice *v, float *dst)
{
    if (v->count == 0) return false;
    XmEntry *e = &v->queue[v->head];
    if (!e->started) {
        e->started = true;
        v->cur = e->play_begin;
        v->in_loop = false;
        v->exit_loop = false;
        v->passes_left = e->loop_count;
        push_ev(v, XM_EV_BUFFER_START, e->context);
    }
    read_frame(&v->fmt, e->data + (size_t)v->cur * v->frame_bytes, dst);
    v->frames_played++;
    v->cur++;

    uint32_t end = v->in_loop ? e->loop_end : e->play_end;
    if (v->cur == end) {
        if (v->in_loop) {                                   /* the end of an extra pass */
            push_ev(v, XM_EV_LOOP_END, e->context);
            if (!v->exit_loop && (e->loop_count == 255 || --v->passes_left > 0)) v->cur = e->loop_begin;
            else finish(v, e);
        } else if (e->loop_count > 0 && !v->exit_loop) {    /* the end of the play region: the loop starts */
            v->in_loop = true;
            v->cur = e->loop_begin;
        } else {
            finish(v, e);
        }
    }
    return true;
}

/* ---- the mix ----------------------------------------------------------------------------- */

/* Hands out waiting events (BUFFER_ENDs of flushed buffers first) while max_ev allows. */
static unsigned drain(XmVoice *v, XmEvent *ev, unsigned nev, unsigned max_ev)
{
    unsigned i = 0;
    while (i < v->n_ends && nev < max_ev) {
        ev[nev].kind = XM_EV_BUFFER_END;
        ev[nev].tag = v->tag;
        ev[nev].context = v->ends[i];
        nev++;
        i++;
    }
    if (i) {
        memmove(v->ends, v->ends + i, (v->n_ends - i) * sizeof v->ends[0]);
        v->n_ends -= i;
    }
    i = 0;
    while (i < v->n_pend && nev < max_ev) ev[nev++] = v->pend[i++];
    if (i) {
        memmove(v->pend, v->pend + i, (v->n_pend - i) * sizeof v->pend[0]);
        v->n_pend -= i;
    }
    return nev;
}

/* The matrix level from source channel s to output o. */
static float level(const XmVoice *v, unsigned oc, unsigned o, unsigned s)
{
    unsigned ch = v->fmt.channels;
    if (v->matrix_set && v->m_out == oc) return v->matrix[o * ch + s];
    if (ch == oc) return o == s ? 1.0f : 0.0f;
    if (ch == 1) return o < 2 ? 1.0f : 0.0f;
    if (ch == 2 && oc == 1) return 0.5f;
    return o == s ? 1.0f : 0.0f;
}

static void mix_frame(const XmVoice *v, const float *s, float *dst, unsigned oc)
{
    unsigned ch = v->fmt.channels;
    for (unsigned o = 0; o < oc; o++) {
        float acc = 0.0f;
        for (unsigned c = 0; c < ch; c++) acc += level(v, oc, o, c) * s[c];
        dst[o] += acc;
    }
}

static unsigned mix_voice(XmVoice *v, float *out, unsigned out_frames, unsigned out_rate, unsigned oc,
                          XmEvent *ev, unsigned nev, unsigned max_ev)
{
    if (!v->running) return drain(v, ev, nev, max_ev);

    unsigned ch = v->fmt.channels;
    double step = (double)v->fmt.rate * v->ratio / (double)out_rate;
    float gain[XM_MAX_CHANNELS];
    for (unsigned c = 0; c < ch; c++) gain[c] = v->volume * v->chvol[c];

    unsigned produced = 0;
    for (;;) {
        nev = drain(v, ev, nev, max_ev);
        if (v->n_pend || v->n_ends) break;                 /* no room for its events: the voice waits */
        if (produced == out_frames) break;

        if (!v->have0) {
            if (!fetch(v, v->f0)) break;
            v->have0 = true;
            continue;
        }
        if (v->t >= 1.0) {                                  /* move on to the next source frame */
            if (!v->have1 && !v->ended) {
                if (fetch(v, v->f1)) v->have1 = true;
                else v->ended = true;
                continue;
            }
            v->t -= 1.0;
            if (v->have1) {
                memcpy(v->f0, v->f1, sizeof v->f0);
                v->have1 = false;
            } else {                                        /* ended: the last frame is used up */
                v->have0 = false;
                v->ended = false;
            }
            continue;
        }
        if (v->t > 0.0 && !v->have1 && !v->ended) {         /* interpolation needs the next frame */
            if (fetch(v, v->f1)) v->have1 = true;
            else v->ended = true;
            continue;
        }

        float s[XM_MAX_CHANNELS];
        if (v->t > 0.0 && v->have1) {
            float w = (float)v->t;
            for (unsigned c = 0; c < ch; c++) s[c] = v->f0[c] + (v->f1[c] - v->f0[c]) * w;
        } else {
            for (unsigned c = 0; c < ch; c++) s[c] = v->f0[c];
        }
        for (unsigned c = 0; c < ch; c++) s[c] *= gain[c];
        mix_frame(v, s, out + (size_t)produced * oc, oc);
        produced++;
        v->t += step;
    }
    return drain(v, ev, nev, max_ev);
}

unsigned xm_mix(XmVoice *const *voices, unsigned n, float *out, unsigned out_frames, unsigned out_rate, unsigned out_channels,
                XmEvent *ev, unsigned max_ev)
{
    if (!out || out_rate == 0 || out_channels == 0 || out_channels > XM_MAX_CHANNELS) return 0;
    memset(out, 0, sizeof(float) * (size_t)out_frames * out_channels);
    unsigned nev = 0;
    for (unsigned i = 0; i < n; i++) {
        if (voices[i]) nev = mix_voice(voices[i], out, out_frames, out_rate, out_channels, ev, nev, max_ev);
    }
    return nev;
}
