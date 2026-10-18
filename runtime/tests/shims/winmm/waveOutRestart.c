#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"
#include <stdint.h>
#include <string.h>

TEST(winmm, waveOutRestart)
{
    unsigned char hdr[48];
    unsigned i, bad;
    MMRESULT r;

    T_CASE("returns MMSYSERR_INVALHANDLE for a NULL handle");
    r = CALL(winmm, waveOutRestart, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("returns MMSYSERR_INVALHANDLE for handle 0x44");
    r = CALL(winmm, waveOutRestart, (HWAVEOUT)(uintptr_t)0x44);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("leaves a 48-byte header buffer alone");
    memset(hdr, 0xA5, sizeof hdr);
    r = CALL(winmm, waveOutRestart, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    r = CALL(winmm, waveOutRestart, (HWAVEOUT)(uintptr_t)0x44);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    bad = 0;
    for (i = 0; i < 48; i++)
        if (hdr[i] != 0xA5) bad++;
    T_EQ(bad, 0);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutRestart, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(winmm, waveOutRestart, (HWAVEOUT)(uintptr_t)0x44);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no audio output device");
    r = CALL(winmm, waveOutRestart, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "waveOutRestart: there is no audio output device"));
}
