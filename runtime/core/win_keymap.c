/* The US keyboard layout: MapVirtualKeyEx, the character of a key, and TranslateMessage on them
 * (specification: rt/window.h; contract: docs/handoff/rt-contract-input.md, the rows K1 to K40,
 * cited as K<n>; private side: core/win_priv.h).  Built by unit rt-input-keys.  Tests:
 * tests/core/win_keymap.c, tests/core/input_keys.c, tests/shims/user32/TranslateMessage.c and
 * MapVirtualKeyExA.c.
 *
 * WHERE THE TABLE COMES FROM (K22 to K24): the scan code of each key is the "Scan 1 Make" of
 * Microsoft's "Keyboard Input Overview" (Scan Codes), its virtual-key code is Microsoft's
 * "Virtual-Key Codes" page, and its two characters are the key's two US legends as both pages
 * name the key ("1 and Bang", "Semicolon and Colon").  Nothing is copied from another
 * implementation; Wine's conformance tests and source were read to check it (the contract says
 * which row rests on what).
 *
 * A KEY THE TABLE DOES NOT HAVE IS A STOP, never "no character" and never 0 (K33, K37): the
 * table cannot tell a key that exists on no keyboard from one it left out.
 */
#include "win_priv.h"
#include "rt/win/user.h"

enum { F_EXT = 1, F_LETTER = 2, F_CHAR = 4, F_SYSTEM = 8 };
typedef struct { uint8_t vk, scan, flags; char plain, shift; } Key;

#define L(c, scan)          { c, scan, F_CHAR | F_LETTER, (char)(c + 32), c }
#define C(vk, scan, p, s)   { vk, scan, F_CHAR, p, s }
#define N(vk, scan)         { vk, scan, 0, 0, 0 }
#define X(vk, scan)         { vk, scan, F_EXT, 0, 0 }
static const Key keys[] = {
    L('A', 0x1E), L('B', 0x30), L('C', 0x2E), L('D', 0x20), L('E', 0x12), L('F', 0x21), L('G', 0x22), L('H', 0x23), L('I', 0x17),
    L('J', 0x24), L('K', 0x25), L('L', 0x26), L('M', 0x32), L('N', 0x31), L('O', 0x18), L('P', 0x19), L('Q', 0x10), L('R', 0x13),
    L('S', 0x1F), L('T', 0x14), L('U', 0x16), L('V', 0x2F), L('W', 0x11), L('X', 0x2D), L('Y', 0x15), L('Z', 0x2C),
    C('1', 0x02, '1', '!'), C('2', 0x03, '2', '@'), C('3', 0x04, '3', '#'), C('4', 0x05, '4', '$'), C('5', 0x06, '5', '%'),
    C('6', 0x07, '6', '^'), C('7', 0x08, '7', '&'), C('8', 0x09, '8', '*'), C('9', 0x0A, '9', '('), C('0', 0x0B, '0', ')'),
    C(0xBA, 0x27, ';', ':'), C(0xBB, 0x0D, '=', '+'), C(0xBC, 0x33, ',', '<'), C(0xBD, 0x0C, '-', '_'), C(0xBE, 0x34, '.', '>'),
    C(0xBF, 0x35, '/', '?'), C(0xC0, 0x29, '`', '~'), C(0xDB, 0x1A, '[', '{'), C(0xDC, 0x2B, '\\', '|'), C(0xDD, 0x1B, ']', '}'),
    C(0xDE, 0x28, '\'', '"'),
    C(0x08, 0x0E, 0x08, 0x08), C(0x09, 0x0F, 0x09, 0x09), C(0x0D, 0x1C, 0x0D, 0x0D), C(0x1B, 0x01, 0x1B, 0x1B), C(0x20, 0x39, ' ', ' '),
    N(0x10, 0x2A), N(0x11, 0x1D), N(0x14, 0x3A),                                        /* Shift, Control (the left ones), Caps Lock */
    N(0x70, 0x3B), N(0x71, 0x3C), N(0x72, 0x3D), N(0x73, 0x3E), N(0x74, 0x3F), N(0x75, 0x40), N(0x76, 0x41), N(0x77, 0x42),
    N(0x78, 0x43), N(0x7A, 0x57), N(0x7B, 0x58),                                        /* F1 to F9, F11, F12 */
    X(0x21, 0x49), X(0x22, 0x51), X(0x23, 0x4F), X(0x24, 0x47), X(0x25, 0x4B), X(0x26, 0x48), X(0x27, 0x4D), X(0x28, 0x50),
    X(0x2D, 0x52), X(0x2E, 0x53),                                                       /* Page Up to Down arrow, Insert, Delete */
    { 0x12, 0x38, F_SYSTEM, 0, 0 }, { 0x79, 0x44, F_SYSTEM, 0, 0 },                     /* ALT, F10: system keys (K7) */
};
#undef L
#undef C
#undef N
#undef X

static const Key *by_vk(UINT vk) {
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) if (keys[i].vk == vk) return &keys[i];
    return NULL;
}

bool rt_key_scan(UINT vk, UINT *scan, bool *ext)
{
    const Key *k = by_vk(vk);
    if (!k) return false;
    *scan = k->scan;
    *ext = (k->flags & F_EXT) != 0;
    return true;
}

UINT rt_key_map(UINT code, UINT map_type, HKL layout)
{
    if (layout && layout != RT_HKL_US) RT_WIN_STOP("rt_key_map", "layout %p: only the US layout exists (K21)", (void *)layout);
    switch (map_type) {
    case 0:                                             /* MAPVK_VK_TO_VSC (K34, K35) */
    case 2: {                                           /* MAPVK_VK_TO_CHAR */
        static const uint8_t side_scan[6] = { 0x2A, 0x36, 0x1D, 0x1D, 0x38, 0x38 };     /* VK_LSHIFT ... VK_RMENU, no prefix by type 0 */
        if (code >= 0xA0 && code <= 0xA5) return map_type == 0 ? side_scan[code - 0xA0] : 0;
        const Key *k = by_vk(code);
        if (!k || code > 0xFF)
            RT_WIN_STOP("rt_key_map", "virtual key 0x%x by map type %u: the key is not in this runtime's US table (K33)", code, map_type);
        if (map_type == 0) return k->scan;
        return !(k->flags & F_CHAR) ? 0 : (k->flags & F_LETTER) ? (UINT)k->shift : (UINT)(unsigned char)k->plain;
    }
    case 1:                                             /* MAPVK_VSC_TO_VK */
    case 3: {                                           /* MAPVK_VSC_TO_VK_EX: left and right told apart */
        if (code > 0xFF) RT_WIN_STOP("rt_key_map", "scan code 0x%x with a prefix byte (K37)", code);
        if (code == 0x36) return map_type == 3 ? 0xA1 : 0x10;
        if (code >= 0x47 && code <= 0x53)
            RT_WIN_STOP("rt_key_map", "scan code 0x%x of the numeric keypad block, which two keys share (K37)", code);
        for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            if (keys[i].scan != code || (keys[i].flags & F_EXT)) continue;
            const UINT vk = keys[i].vk;
            if (map_type == 3 && (vk == 0x10 || vk == 0x11 || vk == 0x12)) return vk == 0x10 ? 0xA0 : vk == 0x11 ? 0xA2 : 0xA4;
            return vk;
        }
        RT_WIN_STOP("rt_key_map", "scan code 0x%x by map type %u: no key of this runtime's US table has it (K37)", code, map_type);
    }
    default:
        RT_WIN_STOP("rt_key_map", "map type %u: 0 to 3 are built (K37)", map_type);
    }
}

int rt_key_char(UINT vk, bool shift, bool control, bool caps)
{
    const Key *k = vk <= 0xFF ? by_vk(vk) : NULL;
    if (vk == 0xE7 || vk == 0xE5)
        RT_WIN_STOP("rt_key_char", "virtual key 0x%x (%s): its translation is not built (K16)", vk, vk == 0xE7 ? "VK_PACKET" : "VK_PROCESSKEY");
    if (!k)
        RT_WIN_STOP("rt_key_char", "virtual key 0x%x is not in this runtime's US table (K33: the numeric keypad, the lock keys but Caps "
                    "Lock, Pause, Print Screen, the Windows keys and the keys of other keyboards are not built)", vk);
    if (k->flags & F_SYSTEM) RT_WIN_STOP("rt_key_char", "virtual key 0x%x is a system key, whose messages are not built (K7)", vk);
    if (!(k->flags & F_CHAR)) return RT_KEY_NO_CHAR;                                    /* K32 */
    if (!control) {
        if (vk == 0x0D && shift)                                                        /* K27 */
            RT_WIN_STOP("rt_key_char", "Shift+Enter: Microsoft's page says a linefeed, Wine's source a carriage return, and no test "
                        "was read (K27)");
        if (k->flags & F_LETTER) return shift != caps ? k->shift : k->plain;            /* K24, K25 */
        return shift ? k->shift : k->plain;                                             /* K24, K26 */
    }
    if (vk == 0x1B) RT_WIN_STOP("rt_key_char", "Control+Esc: Windows takes it itself and no window gets it (K31)");
    if (k->flags & F_LETTER) return (int)vk - 'A' + 1;                                  /* K28, K29: 1 to 26, Shift or not; K30 */
    if (shift) return vk == '2' ? 0 : vk == '6' ? 0x1E : vk == 0xBD ? 0x1F : RT_KEY_NO_CHAR;    /* K29 */
    switch (vk) {                                                                       /* K28, K30 */
    case 0xDB: return 0x1B;
    case 0xDC: return 0x1C;
    case 0xDD: return 0x1D;
    case 0x0D: return 0x0A;
    case 0x20: return 0x20;
    case 0x08: return 0x7F;
    default:   return RT_KEY_NO_CHAR;
    }
}

BOOL rt_key_translate(Ctx *ctx, const MSG *m)
{
    if (!m) RT_WIN_STOP("rt_key_translate", "a NULL MSG pointer");
    if (m->message == WM_SYSKEYDOWN || m->message == WM_SYSKEYUP)                       /* K7 */
        RT_WIN_STOP("rt_key_translate", "a system key message 0x%x (virtual key 0x%llx): WM_SYSCHAR and the system keys are not built (K7)",
                    m->message, (unsigned long long)m->wParam);
    if (m->message == WM_KEYUP) return TRUE;                                            /* K9, K12 */
    if (m->message != WM_KEYDOWN) return FALSE;                                         /* K9 */
    if (m->wParam > 0xFF)
        RT_WIN_STOP("rt_key_translate", "WM_KEYDOWN with wParam 0x%llx, which is no virtual-key code", (unsigned long long)m->wParam);
    if (rt_win_keystate_peek(0x12) & 0x80)
        RT_WIN_STOP("rt_key_translate", "WM_KEYDOWN for virtual key 0x%llx while ALT is down in the thread's key state (K7)",
                    (unsigned long long)m->wParam);
    const int ch = rt_key_char((UINT)m->wParam, (rt_win_keystate_peek(0x10) & 0x80) != 0, (rt_win_keystate_peek(0x11) & 0x80) != 0,
                               (rt_win_keystate_peek(0x14) & 1) != 0);                  /* K14 */
    if (ch == RT_KEY_NO_CHAR) return TRUE;                                              /* K9, K32 */
    /* K3, K13: WM_CHAR for the key message's window, with the key-down's lParam, as a post: behind
     * the posted messages that wait, in front of the input that waits.  The last error is the
     * caller's whatever the post did (K15: a window destroyed since the key was taken). */
    const DWORD err = rt_get_last_error(ctx);
    rt_msg_post(ctx, m->hwnd, WM_CHAR, (WPARAM)ch, m->lParam);
    rt_set_last_error(ctx, err);
    return TRUE;
}
