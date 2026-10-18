#include "rt/test.h"
#include "rt/log.h"

TEST(winmm, mixerGetLineInfoA)
{
    unsigned char buf[200];
    MMRESULT r;
    size_t i;

    T_CASE("returns MMSYSERR_BADDEVICEID for identifier 0");
    memset(buf, 0xA5, sizeof buf);
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0, (LPMIXERLINEA)buf, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("returns MMSYSERR_BADDEVICEID for identifier 5");
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)5, (LPMIXERLINEA)buf, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);

    T_CASE("returns MMSYSERR_INVALHANDLE for a handle that was never given out");
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0x1234, (LPMIXERLINEA)buf, 0x80000000u);
    T_EQ(r, MMSYSERR_INVALHANDLE);

    T_CASE("writes nothing through pmxl");
    memset(buf, 0xA5, sizeof buf);
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)5, (LPMIXERLINEA)buf, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    for (i = 0; i < sizeof buf; i++)
        if (buf[i] != 0xA5) break;
    T_EQ(i, sizeof buf);
    memset(buf, 0xA5, sizeof buf);
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0x1234, (LPMIXERLINEA)buf, 0x80000000u);
    T_EQ(r, MMSYSERR_INVALHANDLE);
    for (i = 0; i < sizeof buf; i++)
        if (buf[i] != 0xA5) break;
    T_EQ(i, sizeof buf);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0, (LPMIXERLINEA)buf, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no mixer device");
    r = CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0, (LPMIXERLINEA)buf, 0);
    T_EQ(r, MMSYSERR_BADDEVICEID);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "mixerGetLineInfoA: there is no mixer device"));

    T_TRAPS(CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0, (LPMIXERLINEA)buf, 0x0000000Fu), "flags 0x0000000F: a query value the function does not have");
    T_TRAPS(CALL(winmm, mixerGetLineInfoA, (HMIXEROBJ)(size_t)0, (LPMIXERLINEA)buf, 0x10000000u), "flags 0x10000000: an object that is not a mixer");
}
