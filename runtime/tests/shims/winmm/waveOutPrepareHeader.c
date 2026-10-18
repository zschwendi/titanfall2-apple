#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"
#include <string.h>
#include <stdint.h>

TEST(winmm, waveOutPrepareHeader)
{
    unsigned char hdr[48];
    MMRESULT r;
    int i, changed;

    T_CASE("returns MMSYSERR_INVALHANDLE for a NULL device handle");
    memset(hdr, 0xA5, sizeof hdr);
    r = CALL(winmm, waveOutPrepareHeader, NULL, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("returns MMSYSERR_INVALHANDLE for a non-NULL device handle");
    r = CALL(winmm, waveOutPrepareHeader, (HWAVEOUT)(uintptr_t)0x44, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("leaves the header unchanged");
    memset(hdr, 0xA5, sizeof hdr);
    CALL(winmm, waveOutPrepareHeader, NULL, hdr, 48);
    changed = 0;
    for (i = 0; i < 48; i++) if (hdr[i] != 0xA5) changed++;
    T_EQ(changed, 0);
    memset(hdr, 0xA5, sizeof hdr);
    CALL(winmm, waveOutPrepareHeader, (HWAVEOUT)(uintptr_t)0x44, hdr, 48);
    changed = 0;
    for (i = 0; i < 48; i++) if (hdr[i] != 0xA5) changed++;
    T_EQ(changed, 0);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutPrepareHeader, NULL, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    r = CALL(winmm, waveOutPrepareHeader, (HWAVEOUT)(uintptr_t)0x44, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no audio output device");
    r = CALL(winmm, waveOutPrepareHeader, NULL, hdr, 48);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "waveOutPrepareHeader: there is no audio output device"));
}
