/* The macOS key codes to the runtime's Windows virtual keys (rt/mackeys.h).  Each number is
 * the kVK_ constant of HIToolbox's Events.h (named in the comment beside it); each Windows
 * virtual key is one that core/win_keymap.c's table has (the rt/win/user.h VK_ names).
 * A Mac key the runtime's table does not have (keypad, Command, Fn, F13 to F20, ISO and JIS
 * keys) has no row.  Every Windows key of win_keymap.c's table has a Mac key, so none is left out.
 */
#include "rt/mackeys.h"

typedef struct { uint16_t mac; uint16_t vk; } MacKey;

static const MacKey mac_keys[] = {
    /* letters: kVK_ANSI_A ... kVK_ANSI_Z */
    { 0x00, 0x41 }, /* kVK_ANSI_A */
    { 0x0B, 0x42 }, /* kVK_ANSI_B */
    { 0x08, 0x43 }, /* kVK_ANSI_C */
    { 0x02, 0x44 }, /* kVK_ANSI_D */
    { 0x0E, 0x45 }, /* kVK_ANSI_E */
    { 0x03, 0x46 }, /* kVK_ANSI_F */
    { 0x05, 0x47 }, /* kVK_ANSI_G */
    { 0x04, 0x48 }, /* kVK_ANSI_H */
    { 0x22, 0x49 }, /* kVK_ANSI_I */
    { 0x26, 0x4A }, /* kVK_ANSI_J */
    { 0x28, 0x4B }, /* kVK_ANSI_K */
    { 0x25, 0x4C }, /* kVK_ANSI_L */
    { 0x2E, 0x4D }, /* kVK_ANSI_M */
    { 0x2D, 0x4E }, /* kVK_ANSI_N */
    { 0x1F, 0x4F }, /* kVK_ANSI_O */
    { 0x23, 0x50 }, /* kVK_ANSI_P */
    { 0x0C, 0x51 }, /* kVK_ANSI_Q */
    { 0x0F, 0x52 }, /* kVK_ANSI_R */
    { 0x01, 0x53 }, /* kVK_ANSI_S */
    { 0x11, 0x54 }, /* kVK_ANSI_T */
    { 0x20, 0x55 }, /* kVK_ANSI_U */
    { 0x09, 0x56 }, /* kVK_ANSI_V */
    { 0x0D, 0x57 }, /* kVK_ANSI_W */
    { 0x07, 0x58 }, /* kVK_ANSI_X */
    { 0x10, 0x59 }, /* kVK_ANSI_Y */
    { 0x06, 0x5A }, /* kVK_ANSI_Z */
    /* digits of the top row: kVK_ANSI_1 ... kVK_ANSI_0 */
    { 0x12, 0x31 }, /* kVK_ANSI_1 */
    { 0x13, 0x32 }, /* kVK_ANSI_2 */
    { 0x14, 0x33 }, /* kVK_ANSI_3 */
    { 0x15, 0x34 }, /* kVK_ANSI_4 */
    { 0x17, 0x35 }, /* kVK_ANSI_5 */
    { 0x16, 0x36 }, /* kVK_ANSI_6 */
    { 0x1A, 0x37 }, /* kVK_ANSI_7 */
    { 0x1C, 0x38 }, /* kVK_ANSI_8 */
    { 0x19, 0x39 }, /* kVK_ANSI_9 */
    { 0x1D, 0x30 }, /* kVK_ANSI_0 */
    /* punctuation */
    { 0x29, 0xBA }, /* kVK_ANSI_Semicolon */
    { 0x18, 0xBB }, /* kVK_ANSI_Equal */
    { 0x2B, 0xBC }, /* kVK_ANSI_Comma */
    { 0x1B, 0xBD }, /* kVK_ANSI_Minus */
    { 0x2F, 0xBE }, /* kVK_ANSI_Period */
    { 0x2C, 0xBF }, /* kVK_ANSI_Slash */
    { 0x32, 0xC0 }, /* kVK_ANSI_Grave */
    { 0x21, 0xDB }, /* kVK_ANSI_LeftBracket */
    { 0x2A, 0xDC }, /* kVK_ANSI_Backslash */
    { 0x1E, 0xDD }, /* kVK_ANSI_RightBracket */
    { 0x27, 0xDE }, /* kVK_ANSI_Quote */
    /* the character keys */
    { 0x33, 0x08 }, /* kVK_Delete (Backspace) */
    { 0x30, 0x09 }, /* kVK_Tab */
    { 0x24, 0x0D }, /* kVK_Return */
    { 0x35, 0x1B }, /* kVK_Escape */
    { 0x31, 0x20 }, /* kVK_Space */
    /* modifiers: both Shift, both Control, both Option (VK_MENU), Caps Lock */
    { 0x38, 0x10 }, /* kVK_Shift */
    { 0x3C, 0x10 }, /* kVK_RightShift */
    { 0x3B, 0x11 }, /* kVK_Control */
    { 0x3E, 0x11 }, /* kVK_RightControl */
    { 0x3A, 0x12 }, /* kVK_Option */
    { 0x3D, 0x12 }, /* kVK_RightOption */
    { 0x39, 0x14 }, /* kVK_CapsLock */
    /* function keys F1 to F12 */
    { 0x7A, 0x70 }, /* kVK_F1 */
    { 0x78, 0x71 }, /* kVK_F2 */
    { 0x63, 0x72 }, /* kVK_F3 */
    { 0x76, 0x73 }, /* kVK_F4 */
    { 0x60, 0x74 }, /* kVK_F5 */
    { 0x61, 0x75 }, /* kVK_F6 */
    { 0x62, 0x76 }, /* kVK_F7 */
    { 0x64, 0x77 }, /* kVK_F8 */
    { 0x65, 0x78 }, /* kVK_F9 */
    { 0x6D, 0x79 }, /* kVK_F10 */
    { 0x67, 0x7A }, /* kVK_F11 */
    { 0x6F, 0x7B }, /* kVK_F12 */
    /* the navigation block; kVK_Help is the Mac key in the Insert position */
    { 0x74, 0x21 }, /* kVK_PageUp */
    { 0x79, 0x22 }, /* kVK_PageDown */
    { 0x77, 0x23 }, /* kVK_End */
    { 0x73, 0x24 }, /* kVK_Home */
    { 0x7B, 0x25 }, /* kVK_LeftArrow */
    { 0x7E, 0x26 }, /* kVK_UpArrow */
    { 0x7C, 0x27 }, /* kVK_RightArrow */
    { 0x7D, 0x28 }, /* kVK_DownArrow */
    { 0x72, 0x2D }, /* kVK_Help (Insert) */
    { 0x75, 0x2E }, /* kVK_ForwardDelete */
};

#define MAC_KEY_COUNT (sizeof mac_keys / sizeof mac_keys[0])

bool rt_mac_key_to_vk(uint16_t mac_key_code, uint32_t *vk)
{
    for (unsigned i = 0; i < MAC_KEY_COUNT; i++) {
        if (mac_keys[i].mac == mac_key_code) {
            *vk = mac_keys[i].vk;
            return true;
        }
    }
    return false;
}

unsigned rt_mac_key_count(void)
{
    return (unsigned)MAC_KEY_COUNT;
}

bool rt_mac_key_row(unsigned i, uint16_t *mac_key_code, uint32_t *vk)
{
    if (i >= MAC_KEY_COUNT) return false;
    *mac_key_code = mac_keys[i].mac;
    *vk = mac_keys[i].vk;
    return true;
}
