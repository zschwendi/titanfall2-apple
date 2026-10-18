#include "rt/test.h"
#include "rt/log.h"

TEST(winmm, PlaySoundA)
{
    BOOL r;
    char name[] = "chord.wav";

    T_CASE("returns FALSE for a file name with flags 0x20001");
    r = CALL(winmm, PlaySoundA, name, NULL, 0x20001);
    T_EQ(r, FALSE);

    T_CASE("returns FALSE for NULL with flags 0");
    r = CALL(winmm, PlaySoundA, NULL, NULL, 0);
    T_EQ(r, FALSE);

    T_CASE("leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    r = CALL(winmm, PlaySoundA, name, NULL, 0x20001);
    T_EQ(r, FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("logs that there is no audio output device");
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "PlaySoundA: there is no audio output device (flags 0x20001)"));
}
