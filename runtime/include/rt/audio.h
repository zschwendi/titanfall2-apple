/* Where mixed sound goes (core/audio_sink.c).  The runtime mixes the game's sound 10 ms at a
 * time and hands the frames to a sink chosen by the environment variable RT_HOST_AUDIO:
 *   unset, "" or "none"   no sound; writes wait for the frames' time
 *   "file:<path>"         the same waiting; the frames are also written to <path> as a WAV file
 *   "file-fast:<path>"    the file, without waiting
 *   "coreaudio"           the Mac's speakers (host_audio.h): the frames go to the default output
 *                         device, which paces the writer; RT_HOST_AUDIO_VOLUME (0 to 1, default 1)
 *                         sets the volume.  Opening it plays sound.
 */
#ifndef RT_AUDIO_H
#define RT_AUDIO_H

typedef struct RtAudioSink RtAudioSink;

/* Opens the sink that RT_HOST_AUDIO names, for interleaved 32-bit float frames.  rate 8000 to
 * 192000, channels 1 to 8; otherwise NULL.  Anything else, or a file that cannot be created:
 * NULL, with one log line. */
RtAudioSink *rt_audio_sink_open(unsigned rate, unsigned channels);

/* RT_HOST_AUDIO_VOLUME as a number from 0 to 1: unset gives 1; anything else gives 1 with a log
 * line.  Opens nothing. */
float rt_audio_sink_volume_from_env(void);

/* Takes `frames` frames.  The waiting sinks keep real time: the first write notes the clock;
 * each write returns when (frames written so far, this write included) / rate seconds have
 * passed since then, and at once if that moment is already in the past by more than 100 ms
 * (the writer fell behind: the sink then moves its start so that it does not try to catch
 * up).  Samples are not clamped: the file sink writes them as they are. */
void rt_audio_sink_write(RtAudioSink *s, const float *interleaved, unsigned frames);

/* Frames taken so far, and the sink's kind as text ("none", "file", "file-fast", "coreaudio"). */
unsigned long long rt_audio_sink_frames(const RtAudioSink *s);
const char *rt_audio_sink_kind(const RtAudioSink *s);

/* Finishes the file (the two lengths of the WAV header are written here, and the file is
 * closed) and frees the sink.  The WAV header is the 44-byte form: a 16-byte fmt chunk with
 * format tag 3 (IEEE float), 32 bits. */
void rt_audio_sink_close(RtAudioSink *s);

#endif
