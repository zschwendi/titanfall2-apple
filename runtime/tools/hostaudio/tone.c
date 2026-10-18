/* The tone for the owner (make host-audio-tone; not run by any test): one second of a 440 Hz sine
 * at amplitude 0.2, in blocks of 480 frames, through rt_audio_sink_open(48000, 2).  RT_HOST_AUDIO
 * decides the sink: only "coreaudio" makes sound; unset, the sink is silent and says so.  The
 * program sets nothing in the environment.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "rt/audio.h"

#define RATE     48000u
#define CHANNELS 2u
#define BLOCK    480u
#define SECONDS  1u

int main(void)
{
    RtAudioSink *s = rt_audio_sink_open(RATE, CHANNELS);
    if (!s) {
        printf("tone: no sink opened (see the log line above)\n");
        return 1;
    }
    float blk[BLOCK * CHANNELS];
    unsigned long total = RATE * SECONDS;
    unsigned long n = 0;
    while (n < total) {
        for (unsigned i = 0; i < BLOCK; i++) {
            double t = (double)(n + i) / RATE;
            float v = (float)(0.2 * sin(2.0 * 3.14159265358979323846 * 440.0 * t));
            blk[2 * i] = v;
            blk[2 * i + 1] = v;
        }
        rt_audio_sink_write(s, blk, BLOCK);
        n += BLOCK;
    }
    printf("tone: sink %s, %llu frames written\n", rt_audio_sink_kind(s), rt_audio_sink_frames(s));
    if (strcmp(rt_audio_sink_kind(s), "coreaudio") != 0)
        printf("tone: no sound was made: RT_HOST_AUDIO is not coreaudio\n");
    rt_audio_sink_close(s);
    return 0;
}
