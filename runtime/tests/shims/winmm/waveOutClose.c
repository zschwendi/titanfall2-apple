#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"
#include <stdint.h>
#include <string.h>

TEST(winmm, waveOutClose)
{
    unsigned char hdr[48];
    unsigned i, changed;
    MMRESULT r;

    memset(hdr, 0xA5, sizeof hdr);

    T_CASE("returns MMSYSERR_INVALHANDLE for NULL");
    r = CALL(winmm, waveOutClose, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("returns MMSYSERR_INVALHANDLE for handle 0x44");
    r = CALL(winmm, waveOutClose, (HWAVEOUT)(uintptr_t)0x44);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutClose, (HWAVEOUT)(uintptr_t)0x44);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, waveOutClose, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("leaves a 48-byte header preset to 0xA5 unchanged");
    CALL(winmm, waveOutClose, NULL);
    CALL(winmm, waveOutClose, (HWAVEOUT)(uintptr_t)0x44);
    changed = 0;
    for (i = 0; i < sizeof hdr; i++)
        if (hdr[i] != 0xA5) changed++;
    T_EQ(changed, 0u);

    T_CASE("logs that there is no audio output device");
    r = CALL(winmm, waveOutClose, NULL);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "waveOutClose: there is no audio output device"));
}
