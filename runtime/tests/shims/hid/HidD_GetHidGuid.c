#include "rt/test.h"
#include "rt/win/com.h"

/* The class GUID {4D1E55B2-F16F-11CF-88CB-001111000030} in memory: Data1, Data2 and Data3 in
 * little-endian byte order, then Data4 as it is written. */
static const uint8_t want[16] = {
    0xB2, 0x55, 0x1E, 0x4D, 0x6F, 0xF1, 0xCF, 0x11,
    0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30
};

TEST(hid, HidD_GetHidGuid)
{
    /* The GUID sits between eight guard bytes on each side: the shim may write only its 16. */
    struct {
        uint8_t pre[8];
        GUID g;
        uint8_t post[8];
    } box;
    uint8_t *bytes = (uint8_t *)&box.g;
    unsigned i;

    T_CASE("stores the device interface GUID of HIDClass devices in the caller's buffer");
    memset(&box, 0xA5, sizeof box);                  /* garbage: no wanted byte is 0xA5 */
    CALL(hid, HidD_GetHidGuid, &box.g);
    for (i = 0; i < 16; i++)
        T_EQ(bytes[i], want[i]);
    for (i = 0; i < 8; i++) {
        T_EQ(box.pre[i], 0xA5);
        T_EQ(box.post[i], 0xA5);
    }

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(hid, HidD_GetHidGuid, &box.g);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_TRAPS(CALL(hid, HidD_GetHidGuid, NULL), "HidD_GetHidGuid with NULL HidGuid");
}
