#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"              /* HWAVEOUT */

TEST(winmm, waveOutWrite)
{
    unsigned char hdr[48];
    MMRESULT r;
    int i, changed;

    T_CASE("returns MMSYSERR_INVALHANDLE for a NULL and a non-NULL device handle");
    r = CALL(winmm, waveOutWrite, NULL, NULL, 0);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutWrite, (HWAVEOUT)(uintptr_t)0x44, NULL, 0);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("leaves the header unchanged");
    for (i = 0; i < 48; i++) hdr[i] = 0xA5;
    r = CALL(winmm, waveOutWrite, NULL, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    for (i = 0, changed = 0; i < 48; i++) if (hdr[i] != 0xA5) changed++;
    T_EQ(changed, 0);
    r = CALL(winmm, waveOutWrite, (HWAVEOUT)(uintptr_t)0x44, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    for (i = 0, changed = 0; i < 48; i++) if (hdr[i] != 0xA5) changed++;
    T_EQ(changed, 0);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutWrite, NULL, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(winmm, waveOutWrite, (HWAVEOUT)(uintptr_t)0x44, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no audio output device");
    r = CALL(winmm, waveOutWrite, (HWAVEOUT)(uintptr_t)0x44, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "waveOutWrite: there is no audio output device"));
}
