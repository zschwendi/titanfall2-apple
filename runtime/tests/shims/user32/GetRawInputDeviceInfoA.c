#include "rt/test.h"
#include "rt/rawinput.h"
#include "rt/input.h"
#include "rt/win/error.h"

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

/* Windows (Microsoft, GetRawInputDeviceInfo; source input.c test_GetRawInputDeviceList, which tests the
 * W function on handle 0xdeadbeef; the block gives this A test): a NULL pcbSize fails with
 * ERROR_NOACCESS (998), checked before the handle; with a size pointer, an invalid handle fails with
 * ERROR_INVALID_HANDLE and the size is left untouched. */
TEST(user32, GetRawInputDeviceInfoA)
{
    unsigned char buf[16];
    UINT size;
    unsigned before;

    /* uiCommand is passed as 0: the shim does not read it, and no RIDI_ constant is in rt/win/. */
    T_CASE("no device attached is logged once");
    before = T_LOG_COUNT(RT_CH_WINDOW);
    size = 77;
    CALL(user32, GetRawInputDeviceInfoA, (HANDLE)0x44, 0, NULL, NULL);
    CALL(user32, GetRawInputDeviceInfoA, (HANDLE)0x44, 0, buf, &size);
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW) - before, 1);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "GetRawInputDeviceInfoA: no raw-input device is attached"));

    T_CASE("NULL size pointer gives ERROR_NOACCESS");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceInfoA, (HANDLE)0x44, 0, NULL, NULL), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
    preset(buf, sizeof buf, 0xA5);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceInfoA, (HANDLE)0x44, 0, buf, NULL), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_NOACCESS);
    T_TRUE(still_preset(buf, sizeof buf, 0xA5));

    T_CASE("every handle is invalid gives ERROR_INVALID_HANDLE");
    size = 77;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceInfoA, (HANDLE)0x44, 0, NULL, &size), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceInfoA, NULL, 0, NULL, &size), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(size, 77);

    T_CASE("nothing is read or written");
    preset(buf, sizeof buf, 0xA5);
    size = 77;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputDeviceInfoA, (HANDLE)0x44, 0, buf, &size), (UINT)-1);
    T_EQ(size, 77);
    T_TRUE(still_preset(buf, sizeof buf, 0xA5));
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);

    rt_input_script_text("test", "anchor start\n");
    size = 32;
    T_TRAPS(CALL(user32, GetRawInputDeviceInfoA, RT_HRAWDEV_MOUSE, 0x2000000bu, buf, &size), "the mouse or the keyboard of an input script");
}
