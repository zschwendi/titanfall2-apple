#include "rt/test.h"
#include "rt/win/user.h"
#include "rt/win/error.h"
#include "rt/rawinput.h"
#include "rt/input.h"

/* Fills n bytes at p with v, and checks that they still hold v. */
static void preset(void *p, unsigned n, unsigned char v)
{
    unsigned char *b = p;
    for (unsigned i = 0; i < n; i++) b[i] = v;
}

static bool still_preset(const void *p, unsigned n, unsigned char v)
{
    const unsigned char *b = p;
    for (unsigned i = 0; i < n; i++)
        if (b[i] != v) return false;
    return true;
}

/* Windows (Microsoft, GetRawInputDeviceList; source input.c test_GetRawInputDeviceList):
 * a cbSize other than sizeof(RAWINPUTDEVICELIST) fails with ERROR_INVALID_PARAMETER, checked before
 * the count pointer; with a correct cbSize and a NULL count it fails with ERROR_NOACCESS (998);
 * with a list, the count is unchanged. */
TEST(user32, GetRawInputDeviceList)
{
    RAWINPUTDEVICELIST list[2];
    UINT n;
    unsigned before;

    T_CASE("no device attached is logged once");
    before = T_LOG_COUNT(RT_CH_WINDOW);
    n = 77;
    CALL(user32, GetRawInputDeviceList, NULL, &n, 16);
    CALL(user32, GetRawInputDeviceList, list, &n, 16);
    CALL(user32, GetRawInputDeviceList, NULL, &n, 8);
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW) - before, 1);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "GetRawInputDeviceList: no raw-input device is attached"));

    T_CASE("size query with NULL list gives 0 devices and count 0");
    n = 77;
    T_EQ(CALL(user32, GetRawInputDeviceList, NULL, &n, 16), 0);
    T_EQ(n, 0);

    T_CASE("list given gives 0 devices, count unchanged, list not written");
    n = 5;
    preset(list, sizeof list, 0xA5);
    T_EQ(CALL(user32, GetRawInputDeviceList, list, &n, 16), 0);
    T_EQ(n, 5);
    T_TRUE(still_preset(list, sizeof list, 0xA5));

    T_CASE("cbSize not 16 gives ERROR_INVALID_PARAMETER");
    n = 77;
    preset(list, sizeof list, 0xA5);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceList, list, &n, 8), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
    T_EQ(n, 77);
    T_TRUE(still_preset(list, sizeof list, 0xA5));
    /* The size is checked before the count pointer: a wrong size with a NULL count is still ERROR_INVALID_PARAMETER. */
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceList, NULL, NULL, 8), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);

    T_CASE("NULL count with cbSize 16 gives ERROR_NOACCESS");
    preset(list, sizeof list, 0xA5);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceList, NULL, NULL, 16), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceList, list, NULL, 16), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
    T_TRUE(still_preset(list, sizeof list, 0xA5));

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, GetRawInputDeviceList, NULL, &n, 16);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(user32, GetRawInputDeviceList, list, &n, 16);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Microsoft's page: the size query stores the number of devices and returns 0; with a list
     * the return value is the number stored.  The two devices are this runtime's (rt/rawinput.h). */
    T_CASE("with an input script the size query gives 2 and a list of two gets the mouse and the keyboard");
    rt_input_script_text("test", "anchor start\n");
    RAWINPUTDEVICELIST two[2];
    n = 77;
    T_EQ(CALL(user32, GetRawInputDeviceList, NULL, &n, 16), 0);
    T_EQ(n, 2);
    T_EQ(CALL(user32, GetRawInputDeviceList, two, &n, 16), 2);
    T_TRUE(two[0].hDevice == RT_HRAWDEV_MOUSE && two[0].dwType == 0 && two[1].hDevice == RT_HRAWDEV_KEYBOARD && two[1].dwType == 1);
    n = 1;
    T_TRAPS(CALL(user32, GetRawInputDeviceList, two, &n, 16), "with an input script a list with room for fewer than 2 devices");
}
