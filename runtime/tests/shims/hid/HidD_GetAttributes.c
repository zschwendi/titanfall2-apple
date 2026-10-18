#include "rt/test.h"
#include "rt/win/devices.h"

/* This machine has no HID collection, so no handle is a HID device's: the shim traps on every
 * call and there is no case to check. */
TEST(hid, HidD_GetAttributes)
{
    HIDD_ATTRIBUTES attr;

    T_TRAPS(CALL(hid, HidD_GetAttributes, NULL, &attr), "this machine has no HID collection");
}
