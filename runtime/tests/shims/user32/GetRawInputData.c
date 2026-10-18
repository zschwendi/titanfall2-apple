#include "rt/test.h"
#include "rt/win/user.h"
#include "rt/win/rawinput.h"
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

/* Windows (Microsoft, GetRawInputData: on error the return value is (UINT)-1; Wine conformance
 * test user32/tests/input.c, test_GetRawInputData: GetRawInputData(NULL, RID_INPUT, NULL, &size,
 * sizeof(RAWINPUTHEADER)) gives -1 with ERROR_INVALID_HANDLE). */
TEST(user32, GetRawInputData)
{
    UINT size;
    unsigned char buf[48];

    T_CASE("NULL handle with RID_INPUT gives (UINT)-1 and ERROR_INVALID_HANDLE");
    size = 0xdeadbeef;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputData, NULL, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(size, 0xdeadbeef);

    T_CASE("any handle with RID_HEADER gives (UINT)-1 and ERROR_INVALID_HANDLE, buffer and size unchanged");
    preset(buf, sizeof buf, 0xA5);
    size = 48;
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, GetRawInputData, (HRAWINPUT)0x1234, RID_HEADER, buf, &size, sizeof(RAWINPUTHEADER)), (UINT)-1);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_HANDLE);
    T_EQ(size, 48);
    T_TRUE(still_preset(buf, sizeof buf, 0xA5));

    T_TRAPS(CALL(user32, GetRawInputData, (HRAWINPUT)0x1234, RID_INPUT, buf, NULL, sizeof(RAWINPUTHEADER)),
            "GetRawInputData with pcbSize NULL");
    T_TRAPS(CALL(user32, GetRawInputData, (HRAWINPUT)0x1234, RID_INPUT, buf, &size, 16),
            "GetRawInputData with cbSizeHeader other than 24");
    T_TRAPS(CALL(user32, GetRawInputData, (HRAWINPUT)0x1234, 0x10000004u, buf, &size, sizeof(RAWINPUTHEADER)),
            "GetRawInputData with uiCommand other than RID_INPUT or RID_HEADER");

    /* Expected values: Microsoft's page GetRawInputData (pData NULL: the required size in *pcbSize,
     * result 0; otherwise the number of bytes copied) and the sizes of the contract's C1, C2, C4:
     * a header is 24 bytes and a mouse packet 48, which is what the recorded game was answered. */
    T_CASE("with an input script a size query gives 0 and the size 48, RID_INPUT copies the 48 bytes of the packet and leaves the size alone, RID_HEADER copies 24, and the handle can be read again");
    rt_input_script_text("test", "anchor start\n");
    RAWINPUT in = { .header = { .dwType = RIM_TYPEMOUSE, .dwSize = 48, .hDevice = RT_HRAWDEV_MOUSE, .wParam = RIM_INPUT } }, out;
    in.data.mouse.usButtonFlags = RI_MOUSE_LEFT_BUTTON_DOWN;
    in.data.mouse.lLastX = -3;
    in.data.mouse.lLastY = 7;
    const HRAWINPUT h = rt_raw_input_new(&in);
    T_EQ((uint64_t)(uintptr_t)h, RT_HRAWINPUT_BASE + 1);
    size = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetRawInputData, h, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER)), 0);
    T_EQ(size, 48);
    size = 60;
    preset(&out, sizeof out, 0xA5);
    T_EQ(CALL(user32, GetRawInputData, h, RID_INPUT, &out, &size, sizeof(RAWINPUTHEADER)), 48);
    T_EQ(size, 60);
    T_TRUE(out.header.dwType == RIM_TYPEMOUSE && out.header.dwSize == 48 && out.header.hDevice == RT_HRAWDEV_MOUSE && out.header.wParam == RIM_INPUT);
    T_TRUE(out.data.mouse.usFlags == 0 && out.data.mouse.usButtonFlags == RI_MOUSE_LEFT_BUTTON_DOWN && out.data.mouse.lLastX == -3 && out.data.mouse.lLastY == 7);
    preset(buf, sizeof buf, 0xA5);
    size = 24;
    T_EQ(CALL(user32, GetRawInputData, h, RID_HEADER, buf, &size, sizeof(RAWINPUTHEADER)), 24);
    T_TRUE(((RAWINPUTHEADER *)buf)->dwSize == 48 && still_preset(buf + 24, 24, 0xA5));
    size = 48;
    T_EQ(CALL(user32, GetRawInputData, h, RID_INPUT, &out, &size, sizeof(RAWINPUTHEADER)), 48);
    T_EQ(T_LAST_ERROR(), 0x1234);

    size = 48;
    T_TRAPS(CALL(user32, GetRawInputData, (HRAWINPUT)0x1234, RID_INPUT, &out, &size, sizeof(RAWINPUTHEADER)), "which this runtime did not issue");
    size = 47;
    T_TRAPS(CALL(user32, GetRawInputData, h, RID_INPUT, &out, &size, sizeof(RAWINPUTHEADER)), "a buffer of 47 bytes for 48");
    rt_raw_input_release(h);
    size = 48;
    T_TRAPS(CALL(user32, GetRawInputData, h, RID_INPUT, &out, &size, sizeof(RAWINPUTHEADER)), "whose packet is gone");
}
