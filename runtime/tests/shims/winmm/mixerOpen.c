#include "rt/test.h"
#include "rt/log.h"
#include "rt/win/mm.h"

TEST(winmm, mixerOpen)
{
    char mark;
    HMIXER h;
    MMRESULT r;

    T_CASE("returns MMSYSERR_BADDEVICEID for identifier 0 with flags 0");
    h = NULL;
    r = CALL(winmm, mixerOpen, &h, 0, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("returns MMSYSERR_NODRIVER for identifier 0 with flags 0x10000000");
    /* 0x10000000 is the wave-out object type (MIXER_OBJECTF_WAVEOUT); rt/win/mm.h has no name for it. */
    h = NULL;
    r = CALL(winmm, mixerOpen, &h, 0, 0, 0, 0x10000000u);
    T_EQ(r, MMSYSERR_NODRIVER);

    T_CASE("leaves *phmx unchanged");
    h = (HMIXER)&mark;
    r = CALL(winmm, mixerOpen, &h, 0, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_TRUE(h == (HMIXER)&mark);
    r = CALL(winmm, mixerOpen, &h, 0, 0, 0, 0x10000000u);
    T_EQ(r, MMSYSERR_NODRIVER);
    T_TRUE(h == (HMIXER)&mark);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = NULL;
    r = CALL(winmm, mixerOpen, &h, 0, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no mixer device");
    h = NULL;
    r = CALL(winmm, mixerOpen, &h, 0, 0, 0, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "mixerOpen: there is no mixer device"));
}
