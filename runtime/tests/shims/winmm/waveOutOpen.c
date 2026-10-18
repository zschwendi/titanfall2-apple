#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"

TEST(winmm, waveOutOpen)
{
    HWAVEOUT h;
    MMRESULT r;
    int mark;

    T_CASE("WAVE_MAPPER returns MMSYSERR_NODRIVER");
    h = NULL;
    r = CALL(winmm, waveOutOpen, &h, WAVE_MAPPER, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_NODRIVER);
    T_EQ(h, NULL);

    T_CASE("device 0 and device 7 return MMSYSERR_BADDEVICEID");
    r = CALL(winmm, waveOutOpen, &h, 0, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    r = CALL(winmm, waveOutOpen, &h, 7, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("a preset *phwo keeps its value");
    h = (HWAVEOUT)&mark;
    r = CALL(winmm, waveOutOpen, &h, WAVE_MAPPER, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_NODRIVER);
    T_EQ(h, (HWAVEOUT)&mark);
    r = CALL(winmm, waveOutOpen, &h, 7, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_EQ(h, (HWAVEOUT)&mark);

    T_CASE("a NULL phwo answers the same");
    r = CALL(winmm, waveOutOpen, NULL, WAVE_MAPPER, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_NODRIVER);
    r = CALL(winmm, waveOutOpen, NULL, 7, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("last error is left alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutOpen, &h, WAVE_MAPPER, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_NODRIVER);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(winmm, waveOutOpen, &h, 7, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no audio output device");
    r = CALL(winmm, waveOutOpen, &h, WAVE_MAPPER, NULL, 0, 0, 0);
    T_EQ(r, MMSYSERR_NODRIVER);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "waveOutOpen: there is no audio output device"));
}
