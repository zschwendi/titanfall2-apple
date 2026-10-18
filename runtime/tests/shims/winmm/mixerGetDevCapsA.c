#include "rt/test.h"
#include "rt/log.h"

TEST(winmm, mixerGetDevCapsA)
{
    unsigned char buf[80];
    MMRESULT r;
    size_t i;

    T_CASE("returns MMSYSERR_BADDEVICEID for identifier 0");
    memset(buf, 0xA5, sizeof buf);
    r = CALL(winmm, mixerGetDevCapsA, 0, buf, sizeof buf);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("returns MMSYSERR_BADDEVICEID for identifier 5");
    r = CALL(winmm, mixerGetDevCapsA, 5, buf, sizeof buf);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("writes nothing through pmxcaps");
    memset(buf, 0xA5, sizeof buf);
    r = CALL(winmm, mixerGetDevCapsA, 5, buf, sizeof buf);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    for (i = 0; i < sizeof buf; i++)
        if (buf[i] != 0xA5) break;
    T_EQ(i, sizeof buf);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, mixerGetDevCapsA, 0, buf, sizeof buf);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no mixer device");
    r = CALL(winmm, mixerGetDevCapsA, 0, buf, sizeof buf);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "mixerGetDevCapsA: there is no mixer device"));
}
