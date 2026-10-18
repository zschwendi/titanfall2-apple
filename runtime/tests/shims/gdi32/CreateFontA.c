#include <string.h>

#include "rt/test.h"
#include "rt/gdi.h"

/* The oracle is the core's record: rt_gdi_get_object reads back the LOGFONTA the font was made
 * from, and every member is compared with the value the guest passed. */
TEST(gdi32, CreateFontA)
{
    LOGFONTA lf;
    HFONT h;
    char name40[41];
    int i;

    T_CASE("creates a font record from the fourteen arguments");
    h = CALL(gdi32, CreateFontA, -12, 7, 900, -450, 700, 1, 2, 3, 4, 5, 6, 7, 8, "Arial");
    T_TRUE(h != NULL);
    memset(&lf, 0xAA, sizeof lf);
    T_EQ(rt_gdi_get_object(h, 60, &lf, false), 60);
    T_EQ(lf.lfHeight, -12);
    T_EQ(lf.lfWidth, 7);
    T_EQ(lf.lfEscapement, 900);
    T_EQ(lf.lfOrientation, -450);
    T_EQ(lf.lfWeight, 700);
    T_EQ(lf.lfItalic, 1);
    T_EQ(lf.lfUnderline, 2);
    T_EQ(lf.lfStrikeOut, 3);
    T_EQ(lf.lfCharSet, 4);
    T_EQ(lf.lfOutPrecision, 5);
    T_EQ(lf.lfClipPrecision, 6);
    T_EQ(lf.lfQuality, 7);
    T_EQ(lf.lfPitchAndFamily, 8);
    T_TRUE(strcmp(lf.lfFaceName, "Arial") == 0);
    T_EQ(rt_gdi_delete(h), TRUE);

    T_CASE("a NULL face name is an empty face name");
    h = CALL(gdi32, CreateFontA, 16, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, NULL);
    T_TRUE(h != NULL);
    memset(&lf, 0xAA, sizeof lf);
    T_EQ(rt_gdi_get_object(h, 60, &lf, false), 60);
    T_EQ(lf.lfHeight, 16);
    T_EQ(lf.lfWeight, 400);
    T_EQ(lf.lfFaceName[0], 0);
    T_EQ(rt_gdi_delete(h), TRUE);

    T_CASE("a face name of 40 bytes is cut to its first 31");
    for (i = 0; i < 40; i++) name40[i] = (char)('A' + i % 26);
    name40[40] = 0;
    h = CALL(gdi32, CreateFontA, 20, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, name40);
    T_TRUE(h != NULL);
    memset(&lf, 0xAA, sizeof lf);
    T_EQ(rt_gdi_get_object(h, 60, &lf, false), 60);
    T_TRUE(memcmp(lf.lfFaceName, name40, 31) == 0);
    T_EQ(lf.lfFaceName[31], 0);
    T_EQ(rt_gdi_delete(h), TRUE);

    T_CASE("success leaves last error alone");
    CALL(kernel32, SetLastError, 0x1234);
    h = CALL(gdi32, CreateFontA, -12, 0, 0, 0, 400, 0, 0, 0, 0, 0, 0, 0, 0, "Arial");
    T_TRUE(h != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_EQ(rt_gdi_delete(h), TRUE);
}
