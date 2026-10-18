/* The macOS key codes to the runtime's Windows virtual keys (rt/mackeys.h; core/mac_keys.c).
 *
 * Expected values are written here by hand from the system header HIToolbox/Events.h (the
 * kVK_ constants, checked against the SDK of the Xcode in use), not read from the table under
 * test.  The Windows virtual keys are those of core/win_keymap.c (rt/window.h: rt_key_scan).
 */
#include "rt/test.h"
#include "rt/window.h"
#include "rt/mackeys.h"

TEST(core, mac_keys)
{
    T_CASE("spot values from Events.h by name (kVK_ANSI_A, kVK_Return, ...)");
    uint32_t vk = 0xFFFF;
    T_TRUE(rt_mac_key_to_vk(0x00, &vk) && vk == 0x41);     /* kVK_ANSI_A to VK_A */
    T_TRUE(rt_mac_key_to_vk(0x01, &vk) && vk == 0x53);     /* kVK_ANSI_S to VK_S */
    T_TRUE(rt_mac_key_to_vk(0x24, &vk) && vk == 0x0D);     /* kVK_Return to VK_RETURN */
    T_TRUE(rt_mac_key_to_vk(0x31, &vk) && vk == 0x20);     /* kVK_Space to VK_SPACE */
    T_TRUE(rt_mac_key_to_vk(0x35, &vk) && vk == 0x1B);     /* kVK_Escape to VK_ESCAPE */
    T_TRUE(rt_mac_key_to_vk(0x33, &vk) && vk == 0x08);     /* kVK_Delete to VK_BACK */
    T_TRUE(rt_mac_key_to_vk(0x7B, &vk) && vk == 0x25);     /* kVK_LeftArrow to VK_LEFT */
    T_TRUE(rt_mac_key_to_vk(0x7A, &vk) && vk == 0x70);     /* kVK_F1 to VK_F1 */
    T_TRUE(rt_mac_key_to_vk(0x12, &vk) && vk == 0x31);     /* kVK_ANSI_1 to digit 1 */
    T_TRUE(rt_mac_key_to_vk(0x1D, &vk) && vk == 0x30);     /* kVK_ANSI_0 to digit 0 */

    T_CASE("the modifiers and the navigation keys (kVK_Option, kVK_Help, kVK_ForwardDelete)");
    T_TRUE(rt_mac_key_to_vk(0x38, &vk) && vk == 0x10 && rt_mac_key_to_vk(0x3C, &vk) && vk == 0x10);
    T_TRUE(rt_mac_key_to_vk(0x3B, &vk) && vk == 0x11 && rt_mac_key_to_vk(0x3E, &vk) && vk == 0x11);
    T_TRUE(rt_mac_key_to_vk(0x3A, &vk) && vk == 0x12 && rt_mac_key_to_vk(0x3D, &vk) && vk == 0x12);
    T_TRUE(rt_mac_key_to_vk(0x39, &vk) && vk == 0x14);
    T_TRUE(rt_mac_key_to_vk(0x72, &vk) && vk == 0x2D && rt_mac_key_to_vk(0x75, &vk) && vk == 0x2E);
    T_TRUE(rt_mac_key_to_vk(0x73, &vk) && vk == 0x24 && rt_mac_key_to_vk(0x77, &vk) && vk == 0x23);
    T_TRUE(rt_mac_key_to_vk(0x74, &vk) && vk == 0x21 && rt_mac_key_to_vk(0x79, &vk) && vk == 0x22);

    T_CASE("no Mac key code appears in two rows");
    unsigned n = rt_mac_key_count();
    T_TRUE(n > 0);
    for (unsigned i = 0; i < n; i++) {
        uint16_t a = 0, b = 0;
        uint32_t va = 0, vb = 0;
        T_TRUE(rt_mac_key_row(i, &a, &va));
        for (unsigned j = i + 1; j < n; j++) {
            T_TRUE(rt_mac_key_row(j, &b, &vb));
            T_TRUE(a != b);
        }
    }
    uint16_t dummy = 0;
    uint32_t dummy_vk = 0;
    T_TRUE(!rt_mac_key_row(n, &dummy, &dummy_vk));

    T_CASE("every row's Windows key is one the runtime's table knows (rt_key_scan)");
    for (unsigned i = 0; i < n; i++) {
        uint16_t mac = 0;
        uint32_t row_vk = 0;
        UINT scan = 0;
        bool ext = false;
        T_TRUE(rt_mac_key_row(i, &mac, &row_vk));
        T_TRUE(rt_key_scan((UINT)row_vk, &scan, &ext));
    }

    T_CASE("every letter A to Z and every digit 0 to 9 is the target of exactly one row");
    for (uint32_t target = 'A'; target <= 'Z'; target++) {
        unsigned hits = 0;
        for (unsigned i = 0; i < n; i++) {
            uint16_t mac = 0;
            uint32_t row_vk = 0;
            T_TRUE(rt_mac_key_row(i, &mac, &row_vk));
            if (row_vk == target) hits++;
        }
        T_EQ(hits, 1u);
    }
    for (uint32_t target = '0'; target <= '9'; target++) {
        unsigned hits = 0;
        for (unsigned i = 0; i < n; i++) {
            uint16_t mac = 0;
            uint32_t row_vk = 0;
            T_TRUE(rt_mac_key_row(i, &mac, &row_vk));
            if (row_vk == target) hits++;
        }
        T_EQ(hits, 1u);
    }

    T_CASE("Mac keys the runtime's table does not have answer false (Command, keypad 0)");
    T_TRUE(!rt_mac_key_to_vk(0x37, &vk));     /* kVK_Command */
    T_TRUE(!rt_mac_key_to_vk(0x52, &vk));     /* kVK_ANSI_Keypad0 */
    T_TRUE(!rt_mac_key_to_vk(0x3F, &vk));     /* kVK_Function */
    T_TRUE(!rt_mac_key_to_vk(0x6A, &vk));     /* kVK_F16: beyond the table's F keys */
}
