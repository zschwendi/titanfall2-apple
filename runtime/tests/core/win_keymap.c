/* The US layout's table (rt/window.h: rt_key_map, rt_key_char, rt_key_scan; core/win_keymap.c).
 *
 * Expected values are the rows of docs/handoff/rt-contract-input.md ("K22"): the scan codes
 * are the "Scan 1 Make" of Microsoft's "Keyboard Input Overview", the virtual-key codes its
 * "Virtual-Key Codes" page, the characters each key's two US legends; the left and right
 * answers of MapVirtualKey and every Control combination are assertions of Wine's conformance
 * tests (dlls/user32/tests/input.c: test_key_map, test_ToUnicode).  Written out here by hand,
 * not read from the table under test.
 */
#include "rt/test.h"
#include "rt/window.h"

#define NONE RT_KEY_NO_CHAR

TEST(core, win_keymap)
{
    T_CASE("MapVirtualKey type 0: the scan code of a virtual key (K22, K34, K35)");
    static const char letter_scan[26] = { 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                          0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C };
    for (UINT c = 'A'; c <= 'Z'; c++) T_EQ(rt_key_map(c, 0, RT_HKL_US), (UINT)letter_scan[c - 'A']);
    for (UINT c = '1'; c <= '9'; c++) T_EQ(rt_key_map(c, 0, NULL), c - '1' + 2);
    T_EQ(rt_key_map('0', 0, NULL), 0x0B);
    static const struct { UINT vk, scan; int plain, shift; } other[] = {
        { 0xBA, 0x27, ';', ':' }, { 0xBB, 0x0D, '=', '+' }, { 0xBC, 0x33, ',', '<' }, { 0xBD, 0x0C, '-', '_' }, { 0xBE, 0x34, '.', '>' },
        { 0xBF, 0x35, '/', '?' }, { 0xC0, 0x29, '`', '~' }, { 0xDB, 0x1A, '[', '{' }, { 0xDC, 0x2B, '\\', '|' }, { 0xDD, 0x1B, ']', '}' },
        { 0xDE, 0x28, '\'', '"' }, { 0x08, 0x0E, 8, 8 }, { 0x09, 0x0F, 9, 9 }, { 0x1B, 0x01, 0x1B, 0x1B }, { 0x20, 0x39, ' ', ' ' } };
    for (unsigned i = 0; i < sizeof other / sizeof other[0]; i++) T_EQ(rt_key_map(other[i].vk, 0, NULL), other[i].scan);
    T_TRUE(rt_key_map(0x0D, 0, NULL) == 0x1C && rt_key_map(0x14, 0, NULL) == 0x3A && rt_key_map(0x70, 0, NULL) == 0x3B
           && rt_key_map(0x78, 0, NULL) == 0x43 && rt_key_map(0x7A, 0, NULL) == 0x57 && rt_key_map(0x7B, 0, NULL) == 0x58);
    /* the left one of two; the sides' own codes; no prefix by type 0; an extended key's low byte */
    T_TRUE(rt_key_map(0x10, 0, NULL) == 0x2A && rt_key_map(0xA0, 0, NULL) == 0x2A && rt_key_map(0xA1, 0, NULL) == 0x36);
    T_TRUE(rt_key_map(0x11, 0, NULL) == 0x1D && rt_key_map(0xA2, 0, NULL) == 0x1D && rt_key_map(0xA3, 0, NULL) == 0x1D);
    T_TRUE(rt_key_map(0x26, 0, NULL) == 0x48 && rt_key_map(0x25, 0, NULL) == 0x4B && rt_key_map(0x2E, 0, NULL) == 0x53);

    T_CASE("types 1 and 3: the virtual key of a scan code, without and with the side (K34, K35)");
    for (UINT c = 'A'; c <= 'Z'; c++) T_TRUE(rt_key_map((UINT)letter_scan[c - 'A'], 1, NULL) == c && rt_key_map((UINT)letter_scan[c - 'A'], 3, NULL) == c);
    for (unsigned i = 0; i < sizeof other / sizeof other[0]; i++) T_EQ(rt_key_map(other[i].scan, 1, NULL), other[i].vk);
    T_TRUE(rt_key_map(0x2A, 1, NULL) == 0x10 && rt_key_map(0x36, 1, NULL) == 0x10 && rt_key_map(0x1D, 1, NULL) == 0x11);
    T_TRUE(rt_key_map(0x2A, 3, NULL) == 0xA0 && rt_key_map(0x36, 3, NULL) == 0xA1 && rt_key_map(0x1D, 3, NULL) == 0xA2);
    T_TRUE(rt_key_map(0x1C, 1, NULL) == 0x0D && rt_key_map(0x3B, 3, NULL) == 0x70 && rt_key_map(0x44, 1, NULL) == 0x79);

    T_CASE("type 2: the unshifted character, a letter as its capital, 0 for a key without one (K34)");
    for (UINT c = 'A'; c <= 'Z'; c++) T_EQ(rt_key_map(c, 2, NULL), c);
    for (UINT c = '0'; c <= '9'; c++) T_EQ(rt_key_map(c, 2, NULL), c);
    for (unsigned i = 0; i < sizeof other / sizeof other[0]; i++) T_EQ(rt_key_map(other[i].vk, 2, NULL), (UINT)other[i].plain);
    T_TRUE(rt_key_map(0x0D, 2, NULL) == 0x0D && rt_key_map(0x10, 2, NULL) == 0 && rt_key_map(0x70, 2, NULL) == 0 && rt_key_map(0x25, 2, NULL) == 0
           && rt_key_map(0xA1, 2, NULL) == 0);

    T_CASE("a key's character: plain, with Shift, with Caps Lock, with both (K24, K25, K26)");
    static const char digit_shift[] = ")!@#$%^&*(";
    for (UINT c = 'A'; c <= 'Z'; c++)
        T_TRUE(rt_key_char(c, false, false, false) == (int)c + 32 && rt_key_char(c, true, false, false) == (int)c
               && rt_key_char(c, false, false, true) == (int)c && rt_key_char(c, true, false, true) == (int)c + 32);
    for (UINT c = '0'; c <= '9'; c++)
        T_TRUE(rt_key_char(c, false, false, false) == (int)c && rt_key_char(c, true, false, false) == digit_shift[c - '0']
               && rt_key_char(c, false, false, true) == (int)c && rt_key_char(c, true, false, true) == digit_shift[c - '0']);
    for (unsigned i = 0; i < sizeof other / sizeof other[0]; i++)
        T_TRUE(rt_key_char(other[i].vk, false, false, false) == other[i].plain && rt_key_char(other[i].vk, true, false, false) == other[i].shift
               && rt_key_char(other[i].vk, false, false, true) == other[i].plain);
    T_EQ(rt_key_char(0x0D, false, false, false), 0x0D);
    T_EQ(rt_key_char(0x0D, false, false, true), 0x0D);

    T_CASE("Control, and Control with Shift (K28, K29, K30): Wine's test table of ToUnicode");
    for (UINT c = 'A'; c <= 'Z'; c++)
        T_TRUE(rt_key_char(c, false, true, false) == (int)c - 64 && rt_key_char(c, true, true, false) == (int)c - 64
               && rt_key_char(c, false, true, true) == (int)c - 64);
    T_TRUE(rt_key_char(0xDB, false, true, false) == 0x1B && rt_key_char(0xDC, false, true, false) == 0x1C && rt_key_char(0xDD, false, true, false) == 0x1D);
    T_TRUE(rt_key_char(0x0D, false, true, false) == 0x0A && rt_key_char(0x0D, true, true, false) == NONE);
    T_TRUE(rt_key_char(0x09, false, true, false) == NONE && rt_key_char(0x09, true, true, false) == NONE);
    T_TRUE(rt_key_char(0x20, false, true, false) == ' ' && rt_key_char(0x08, false, true, false) == 0x7F);
    for (UINT c = '0'; c <= '9'; c++) T_EQ(rt_key_char(c, false, true, false), NONE);
    static const UINT no_ctrl[] = { 0xC0, 0xBD, 0xBB, 0xBA, 0xDE, 0xBC, 0xBE, 0xBF };          /* ` - = ; ' , . / */
    for (unsigned i = 0; i < 8; i++) T_EQ(rt_key_char(no_ctrl[i], false, true, false), NONE);
    T_TRUE(rt_key_char('2', true, true, false) == 0 && rt_key_char('6', true, true, false) == 0x1E && rt_key_char(0xBD, true, true, false) == 0x1F);
    static const UINT no_both[] = { '1', '3', '4', '5', '7', '8', '9', '0', 0xBB, 0xBA, 0xDE, 0xBC, 0xBE, 0xBF, 0xC0, 0xDB, 0xDC, 0xDD, 0x20, 0x08 };
    for (unsigned i = 0; i < sizeof no_both / sizeof no_both[0]; i++) T_EQ(rt_key_char(no_both[i], true, true, false), NONE);

    T_CASE("keys without a character, whatever is held (K32); the script's scan code and extended flag (K38)");
    static const UINT silent[] = { 0x10, 0x11, 0x14, 0x70, 0x78, 0x7A, 0x7B, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E };
    for (unsigned i = 0; i < sizeof silent / sizeof silent[0]; i++)
        T_TRUE(rt_key_char(silent[i], false, false, false) == NONE && rt_key_char(silent[i], true, false, false) == NONE
               && rt_key_char(silent[i], false, true, false) == NONE && rt_key_char(silent[i], true, true, true) == NONE);
    UINT scan = 0;
    bool ext = true;
    T_TRUE(rt_key_scan(0x20, &scan, &ext) && scan == 0x39 && !ext);
    T_TRUE(rt_key_scan(0x27, &scan, &ext) && scan == 0x4D && ext);
    T_TRUE(rt_key_scan(0x10, &scan, &ext) && scan == 0x2A && !ext);
    T_TRUE(!rt_key_scan(0x60, &scan, &ext) && !rt_key_scan(0xA1, &scan, &ext));

    T_CASE("the stops: nothing outside the table is answered (K7, K16, K27, K31, K33, K37)");
    T_TRAPS(rt_key_map('A', 0, (HKL)(uintptr_t)0x04070407), "only the US layout exists");
    T_TRAPS(rt_key_map('A', 4, NULL), "map type 4");
    T_TRAPS(rt_key_map(0x60, 0, NULL), "virtual key 0x60 by map type 0");
    T_TRAPS(rt_key_map(0x5B, 2, NULL), "virtual key 0x5b by map type 2");
    T_TRAPS(rt_key_map(0x47, 1, NULL), "scan code 0x47 of the numeric keypad block");
    T_TRAPS(rt_key_map(0xE01D, 3, NULL), "scan code 0xe01d with a prefix byte");
    T_TRAPS(rt_key_map(0x7F, 1, NULL), "scan code 0x7f by map type 1");
    T_TRAPS(rt_key_char(0x60, false, false, false), "virtual key 0x60 is not in this runtime's US table");
    T_TRAPS(rt_key_char(0x90, false, false, false), "virtual key 0x90 is not in this runtime's US table");
    T_TRAPS(rt_key_char(0xE7, false, false, false), "VK_PACKET");
    T_TRAPS(rt_key_char(0x12, false, false, false), "is a system key");
    T_TRAPS(rt_key_char(0x79, false, false, false), "is a system key");
    T_TRAPS(rt_key_char(0x0D, true, false, false), "Shift+Enter");
    T_TRAPS(rt_key_char(0x1B, false, true, false), "Control+Esc");
}
