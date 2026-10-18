/* The speakers' sink (runtime/host/audio_ca.c): an AudioQueue on the default output device.
 * Used by core/audio_sink.c for RT_HOST_AUDIO=coreaudio.  Nothing here opens a device unless
 * rt_host_audio_open is called. */
#ifndef RT_HOST_AUDIO_H
#define RT_HOST_AUDIO_H

#include <stdbool.h>

typedef struct RtHostAudio RtHostAudio;

/* Opens the default output device for interleaved 32-bit float frames.  rate 8000 to 192000,
 * channels 1 to 8, volume 0 to 1 (clamped); NULL if it cannot be opened (with a log line). */
RtHostAudio *rt_host_audio_open(unsigned rate, unsigned channels, float volume);

/* Takes the frames.  Returns when they are in the ring; waits while the ring holds more than
 * the target (60 ms of frames), so that the writer is paced by the device.  Samples are clamped
 * to -1..1 here.  If the device stopped taking frames for 2 s, it returns anyway (the frames
 * not yet in the ring are dropped) and counts it. */
void rt_host_audio_write(RtHostAudio *a, const float *interleaved, unsigned frames);

/* Stops and disposes the queue, frees the ring, and logs the underrun and stall counts. */
void rt_host_audio_close(RtHostAudio *a);

/* Exercises the ring (wrap-around, taking more than there is, putting more than fits).  Opens
 * no device.  true when every check passes. */
bool rt_host_audio_ring_selftest(void);

#endif
