#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"              /* HWAVEOUT */

TEST(winmm, waveOutSetVolume)
{
    MMRESULT r;

    T_CASE("returns MMSYSERR_INVALHANDLE for a NULL and a non-NULL device handle");
    r = CALL(winmm, waveOutSetVolume, NULL, 0xFFFFFFFFu);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutSetVolume, (HWAVEOUT)(uintptr_t)0x44, 0xFFFFFFFFu);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("returns MMSYSERR_INVALHANDLE whatever the volume");
    r = CALL(winmm, waveOutSetVolume, NULL, 0);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutSetVolume, (HWAVEOUT)(uintptr_t)0x44, 0);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutSetVolume, NULL, 0xFFFFu);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutSetVolume, (HWAVEOUT)(uintptr_t)0x44, 0xFFFFu);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutSetVolume, NULL, 0x7FFF8000u);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutSetVolume, (HWAVEOUT)(uintptr_t)0x44, 0x7FFF8000u);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutSetVolume, NULL, 0xFFFFFFFFu);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(winmm, waveOutSetVolume, (HWAVEOUT)(uintptr_t)0x44, 0);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no audio output device");
    r = CALL(winmm, waveOutSetVolume, (HWAVEOUT)(uintptr_t)0x44, 0xFFFFFFFFu);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "waveOutSetVolume: there is no audio output device"));
}
