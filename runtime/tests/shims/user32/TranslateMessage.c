#include "rt/test.h"
#include "rt/win/user.h"

/* A MSG with every byte set, so that a write to any part of it shows; `message` is the message. */
static void set_msg(MSG *m, UINT message)
{
    memset(m, 0xA5, sizeof *m);
    m->message = message;
}

/* A key message for the calling thread (no window): virtual key `vk`, scan code `scan`, the
 * padding and the rest set as above.  Expected characters and lParams: docs/handoff/
 * rt-contract-input.md, rows K2, K3, K9 to K12 and the table of the US layout (Microsoft's
 * pages).  With no input script every key is up, so the characters are the unshifted ones. */
static void key_msg(MSG *m, UINT message, WPARAM vk, UINT scan)
{
    set_msg(m, message);
    m->hwnd = NULL;
    m->wParam = vk;
    m->lParam = (LPARAM)(scan << 16 | 1 | (message == WM_KEYUP ? 0xC0000000u : 0));
}

TEST(user32, TranslateMessage)
{
    static const UINT not_translated[4] = { WM_NULL, WM_USER, WM_CHAR, 0xC001 };
    MSG m, before, got;
    int i;

    T_CASE("a message that is not translated returns FALSE");
    for (i = 0; i < 4; i++) {
        set_msg(&m, not_translated[i]);
        T_EQ(CALL(user32, TranslateMessage, &m), FALSE);
    }
    T_EQ(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE), FALSE);

    T_CASE("WM_KEYUP returns TRUE and posts nothing");
    key_msg(&m, WM_KEYUP, 'A', 0x1E);
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_EQ(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE), FALSE);

    T_CASE("WM_KEYDOWN of a character key returns TRUE and posts WM_CHAR");
    static const struct { WPARAM vk; UINT scan; WPARAM ch; } keys[] = {
        { 'A', 0x1E, 'a' }, { 'Z', 0x2C, 'z' }, { '7', 0x08, '7' }, { 0xBA, 0x27, ';' }, { 0xDC, 0x2B, '\\' },
        { 0x0D, 0x1C, 0x0D }, { 0x1B, 0x01, 0x1B }, { 0x09, 0x0F, 0x09 }, { 0x08, 0x0E, 0x08 }, { 0x20, 0x39, 0x20 } };
    for (i = 0; i < 10; i++) {
        key_msg(&m, WM_KEYDOWN, keys[i].vk, keys[i].scan);
        T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
        memset(&got, 0, sizeof got);
        T_EQ(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE), TRUE);
        T_TRUE(got.hwnd == NULL && got.message == WM_CHAR && got.wParam == keys[i].ch && got.lParam == m.lParam);
        T_EQ(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE), FALSE);
    }
    key_msg(&m, WM_KEYDOWN, 'A', 0x1E);                 /* a repeat: the WM_CHAR has the key-down's lParam, bit 30 and all */
    m.lParam |= 0x40000000;
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_TRUE(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE) && got.message == WM_CHAR && got.wParam == 'a' && got.lParam == 0x401E0001);

    T_CASE("WM_KEYDOWN of a key without a character returns TRUE and posts nothing");
    static const struct { WPARAM vk; UINT scan; } silent[] = { { 0x10, 0x2A }, { 0x11, 0x1D }, { 0x14, 0x3A }, { 0x70, 0x3B }, { 0x26, 0x148 } };
    for (i = 0; i < 5; i++) {
        key_msg(&m, WM_KEYDOWN, silent[i].vk, silent[i].scan);
        T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
        T_EQ(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE), FALSE);
    }

    T_CASE("the MSG is never written");
    for (i = 0; i < 4; i++) {
        set_msg(&m, not_translated[i]);
        memcpy(&before, &m, sizeof m);
        CALL(user32, TranslateMessage, &m);
        T_EQ(memcmp(&m, &before, sizeof m), 0);
    }
    key_msg(&m, WM_KEYDOWN, 'Q', 0x10);
    memcpy(&before, &m, sizeof m);
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_EQ(memcmp(&m, &before, sizeof m), 0);
    T_TRUE(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE) && got.wParam == 'q');
    key_msg(&m, WM_KEYUP, 'Q', 0x10);
    memcpy(&before, &m, sizeof m);
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_EQ(memcmp(&m, &before, sizeof m), 0);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    set_msg(&m, WM_CHAR);
    T_EQ(CALL(user32, TranslateMessage, &m), FALSE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    key_msg(&m, WM_KEYDOWN, 'Q', 0x10);
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    key_msg(&m, WM_KEYDOWN, 'Q', 0x10);                 /* for a window that does not exist: the post fails, the error stays the caller's */
    m.hwnd = (HWND)(uintptr_t)0x7A5A5;
    T_EQ(CALL(user32, TranslateMessage, &m), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);
    T_TRUE(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE) && got.wParam == 'q' && got.hwnd == NULL);
    T_EQ(CALL(user32, PeekMessageA, &got, NULL, 0, 0, PM_REMOVE), FALSE);

    /* RT-TRAPS: a NULL lpMsg; the core's stops (rt_key_translate, rt_key_char): the two system
     * key messages (WM_SYSKEYDOWN 0x104, WM_SYSKEYUP 0x105), a key the US table does not have
     * (the numeric keypad's 0, Num Lock), VK_PACKET, VK_PROCESSKEY, a wParam that is no key. */
    T_TRAPS(CALL(user32, TranslateMessage, NULL), "NULL lpMsg");
    key_msg(&m, WM_SYSKEYDOWN, 'A', 0x1E);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "a system key message 0x104");
    key_msg(&m, WM_SYSKEYUP, 'A', 0x1E);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "a system key message 0x105");
    key_msg(&m, WM_KEYDOWN, 0x60, 0x52);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "virtual key 0x60 is not in this runtime's US table");
    key_msg(&m, WM_KEYDOWN, 0x90, 0x45);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "virtual key 0x90 is not in this runtime's US table");
    key_msg(&m, WM_KEYDOWN, 0xE7, 0);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "VK_PACKET");
    key_msg(&m, WM_KEYDOWN, 0xE5, 0);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "VK_PROCESSKEY");
    key_msg(&m, WM_KEYDOWN, 0x141, 0x1E);
    T_TRAPS(CALL(user32, TranslateMessage, &m), "wParam 0x141, which is no virtual-key code");
}
