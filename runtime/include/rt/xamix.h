/* The mixing core of the XAudio2 source voices (runtime/xaudio/mix.c).  Plain C: no COM, no
 * threads, no guest calls, no global state, no lock, no allocation inside xm_mix.  The COM
 * objects and the output thread are another unit's; this is the arithmetic alone.
 */
#ifndef RT_XAMIX_H
#define RT_XAMIX_H

#include <stdbool.h>
#include <stdint.h>

typedef struct XmFormat {          /* a voice's sample format */
    uint32_t rate;                 /* frames a second, 1000 to 200000 */
    uint16_t channels;             /* 1 to 8 */
    uint16_t bits;                 /* 8 (unsigned), 16, 24 or 32 (signed integers, little-endian), or 32 with is_float */
    bool     is_float;
} XmFormat;

typedef struct XmBuffer {          /* one submitted buffer; the bytes stay the caller's and must stay valid until its END event */
    const void *data; uint32_t bytes;
    uint32_t play_begin, play_length;      /* in frames; play_length 0 = to the end of the buffer */
    uint32_t loop_begin, loop_length;      /* in frames; loop_length 0 = to the end of the play region */
    uint32_t loop_count;                   /* 0 = no loop, 1 to 254 = that many extra passes of the loop region, 255 = for ever */
    void    *context;                      /* handed back in events */
    bool     end_of_stream;
} XmBuffer;

typedef enum { XM_EV_BUFFER_START, XM_EV_BUFFER_END, XM_EV_LOOP_END, XM_EV_STREAM_END } XmEventKind;
typedef struct XmEvent { XmEventKind kind; void *tag; void *context; } XmEvent;   /* tag: the voice's */

typedef struct XmVoice XmVoice;
enum { XM_MAX_QUEUED = 64, XM_MAX_CHANNELS = 8 };

XmVoice *xm_voice_new(const XmFormat *fmt, void *tag);    /* NULL for a format outside the limits above */
void     xm_voice_free(XmVoice *v);
int      xm_voice_submit(XmVoice *v, const XmBuffer *b);  /* 0; -1 the queue holds XM_MAX_QUEUED; -2 a bad buffer (NULL data, 0 bytes,
                                                             bytes not a whole number of frames, a play or loop region outside the buffer,
                                                             a loop region outside the play region, loop_count 0 with a loop region given) */
void     xm_voice_flush(XmVoice *v);                      /* drops every queued buffer that has not started playing; each gets a
                                                             BUFFER_END event at the next xm_mix */
void     xm_voice_exit_loop(XmVoice *v);                  /* the playing buffer finishes its current pass and does not loop again */
void     xm_voice_start(XmVoice *v);
void     xm_voice_stop(XmVoice *v);                       /* keeps its place */
void     xm_voice_set_volume(XmVoice *v, float volume);   /* all channels; default 1 */
void     xm_voice_set_channel_volumes(XmVoice *v, const float *vol, unsigned n);    /* n must equal the voice's channels */
void     xm_voice_set_matrix(XmVoice *v, unsigned out_channels, const float *levels);
              /* levels[out * src_channels + src]; until set, the default: same count = identity; mono to N = the one channel to the
                 first two outputs (or the one); stereo to mono = half of each; anything else = channel i to output i while both exist */
void     xm_voice_set_ratio(XmVoice *v, float ratio);     /* playback speed, 1/1024 to 1024; default 1 */
void     xm_voice_state(const XmVoice *v, unsigned *queued, uint64_t *frames_played, void **current_context);
              /* queued counts the playing buffer too; frames_played counts source frames consumed since the voice was made
                 or since the last buffer flagged end_of_stream finished */
uint32_t xm_voice_bytes_needed(const XmVoice *v, unsigned out_frames, unsigned out_rate);
              /* how many more bytes the voice needs queued to fill out_frames of output at its rate and ratio; 0 if it has enough or is stopped */

/* Mixes out_frames frames of every started voice into out (out_channels interleaved floats a frame; out is ZEROED first).
 * Integer samples are scaled to -1..1 (8-bit: (x-128)/128; 16: /32768; 24: /8388608; 32: /2147483648).  A voice whose rate
 * times ratio differs from out_rate is resampled by linear interpolation between neighbouring frames (keep the fractional
 * position in the voice across calls, and across buffers).  A voice that runs out of buffers contributes silence for the rest.
 * Events are appended in the order they happen (per voice: BUFFER_START when a buffer's first frame is consumed, LOOP_END at
 * the end of each loop pass, BUFFER_END when its last frame is consumed or it was flushed, STREAM_END after the BUFFER_END of
 * a buffer with end_of_stream).  Returns the number of events written; if max_ev would be exceeded the mix stops early for
 * that voice and continues next call (no event is lost).  The output is NOT clamped. */
unsigned xm_mix(XmVoice *const *voices, unsigned n, float *out, unsigned out_frames, unsigned out_rate, unsigned out_channels,
                XmEvent *ev, unsigned max_ev);

#endif
