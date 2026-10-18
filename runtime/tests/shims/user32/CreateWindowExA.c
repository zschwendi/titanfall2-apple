#include <string.h>

#include "rt/test.h"
#include "rt/shim.h"
#include "rt/window.h"

/* Expected values: rt/window.h, rt_window_create (the comment there names its source for each
 * statement: win.c, msg.c, review numbers).  The guest procedure records every message it gets. */
static UINT seen[32];
static LPARAM seen_lp[32];
static WPARAM seen_wp[32];
static unsigned nseen;
static CREATESTRUCTW cs_seen;

static void note(UINT msg, WPARAM wp, LPARAM lp)
{
    if (nseen < 32) { seen[nseen] = msg; seen_wp[nseen] = wp; seen_lp[nseen] = lp; nseen++; }
}

/* A window procedure that records the messages and answers 1 to WM_NCCREATE. */
GUEST_FN(rec)
{
    const UINT msg = (UINT)ctx->r[2];
    if (msg == WM_NCCREATE) cs_seen = *(const CREATESTRUCTW *)(uintptr_t)ctx->r[9];
    note(msg, (WPARAM)ctx->r[8], (LPARAM)ctx->r[9]);
    ctx->r[0] = (msg == WM_NCCREATE);
    GUEST_RET(ctx);
}

/* Refuses WM_NCCREATE: answers 0 to everything. */
GUEST_FN(refuse)
{
    note((UINT)ctx->r[2], (WPARAM)ctx->r[8], (LPARAM)ctx->r[9]);
    ctx->r[0] = 0;
    GUEST_RET(ctx);
}

/* Answers 1 to WM_NCCREATE and -1 to WM_CREATE. */
GUEST_FN(refuse_create)
{
    const UINT msg = (UINT)ctx->r[2];
    note(msg, (WPARAM)ctx->r[8], (LPARAM)ctx->r[9]);
    ctx->r[0] = (msg == WM_NCCREATE) ? 1 : (msg == WM_CREATE) ? (uint64_t)(LRESULT)-1 : 0;
    GUEST_RET(ctx);
}

static bool five_seq(void)
{
    static const UINT want[5] = { 0x81, 0x83, 0x01, 0x05, 0x03 };
    if (nseen != 5) return false;
    for (unsigned i = 0; i < 5; i++) if (seen[i] != want[i]) return false;
    return true;
}

static HWND make(const char *cls, DWORD style, HWND parent, INT x, INT y, INT cx, INT cy)
{
    return CALL(user32, CreateWindowExA, 0, cls, NULL, style, x, y, cx, cy, parent, NULL, NULL, NULL);
}

static DWORD gwl_style(HWND h)
{
    return (DWORD)CALL(user32, GetWindowLongPtrW, h, GWL_STYLE);
}

TEST(user32, CreateWindowExA)
{
    WNDCLASSEXW ca = { .cbSize = sizeof ca, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = (LPCWSTR)(uintptr_t)"CA" };
    T_TRUE(rt_wndclass_register(&ca, false) != 0);          /* an A class: the strings are narrow */
    WNDCLASSEXW cn = { .cbSize = sizeof cn, .lpfnWndProc = T_GUEST_ADDR(refuse), .lpszClassName = (LPCWSTR)(uintptr_t)"CN" };
    T_TRUE(rt_wndclass_register(&cn, false) != 0);
    WNDCLASSEXW cc = { .cbSize = sizeof cc, .lpfnWndProc = T_GUEST_ADDR(refuse_create), .lpszClassName = (LPCWSTR)(uintptr_t)"CC" };
    T_TRUE(rt_wndclass_register(&cc, false) != 0);
    WNDCLASSEXW cw = { .cbSize = sizeof cw, .lpfnWndProc = T_GUEST_ADDR(rec), .lpszClassName = u"CW" };
    T_TRUE(rt_wndclass_register(&cw, true) != 0);           /* a W class: for the stop below */

    T_CASE("a hidden popup of an A class: its handle and the five creation messages");
    CALL(kernel32, SetLastError, 0x1234);
    nseen = 0;
    HWND h = make("CA", WS_POPUP, NULL, 10, 20, 300, 200);
    T_REQUIRE(h != NULL);
    T_TRUE(five_seq());
    T_EQ(seen_lp[3], (LPARAM)(300 | (200 << 16)));          /* WM_SIZE: the client size */
    T_EQ(seen_lp[4], (LPARAM)(10 | (20 << 16)));            /* WM_MOVE: the client origin, screen coordinates */
    T_EQ(cs_seen.x, 10);
    T_EQ(cs_seen.y, 20);
    T_EQ(cs_seen.cx, 300);
    T_EQ(cs_seen.cy, 200);
    T_EQ((DWORD)cs_seen.style, WS_POPUP);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a popup's rectangle is its position and size");
    RECT r;
    T_EQ(CALL(user32, GetWindowRect, h, &r), TRUE);
    T_TRUE(r.left == 10 && r.top == 20 && r.right == 310 && r.bottom == 220);
    HWND p = make("CA", WS_POPUP, NULL, 100, 200, 150, 150);
    T_REQUIRE(p != NULL);
    T_EQ(CALL(user32, GetWindowRect, p, &r), TRUE);
    T_TRUE(r.left == 100 && r.top == 200 && r.right == 250 && r.bottom == 350);

    T_CASE("a negative size is taken as zero");
    HWND z = make("CA", WS_POPUP, NULL, 10, 10, -10, -10);
    T_REQUIRE(z != NULL);
    T_EQ(CALL(user32, GetWindowRect, z, &r), TRUE);
    T_TRUE(r.left == 10 && r.top == 10 && r.right == 10 && r.bottom == 10);

    T_CASE("GWL_STYLE of a popup: 0x84000000, with WS_CLIPSIBLINGS");
    T_EQ(gwl_style(h), 0x84000000u);
    T_TRUE((gwl_style(h) & WS_CLIPSIBLINGS) != 0);

    T_CASE("a child: the five messages, then its parent gets WM_PARENTNOTIFY");
    nseen = 0;
    HWND c = CALL(user32, CreateWindowExA, 0, "CA", NULL, WS_CHILD, 5, 6, 50, 40, p, (HMENU)(uintptr_t)7, NULL, NULL);
    T_REQUIRE(c != NULL);
    T_TRUE(nseen == 6 && seen[5] == 0x210);                 /* the five, then WM_PARENTNOTIFY to the parent */
    T_EQ(seen_wp[5] & 0xFFFF, 1);                           /* low word: WM_CREATE */
    T_EQ(seen_wp[5] >> 16, 7);                              /* high word: the child's identifier */
    T_EQ(seen_lp[5], (LPARAM)c);                            /* lParam: the child's handle */

    T_CASE("two children of one parent are in creation order");
    HWND c2 = make("CA", WS_CHILD, p, 5, 60, 50, 40);
    T_REQUIRE(c2 != NULL);
    T_EQ(rt_window_get(p, GW_CHILD), c);

    T_CASE("no such class: NULL and ERROR_CANNOT_FIND_WND_CLASS");
    T_TRUE(make("Nope", WS_POPUP, NULL, 0, 0, 1, 1) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_CANNOT_FIND_WND_CLASS);

    T_CASE("a child with no parent: NULL and ERROR_TLW_WITH_WSCHILD, the procedure is not called");
    nseen = 0;
    T_TRUE(make("CA", WS_CHILD, NULL, 0, 0, 1, 1) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_TLW_WITH_WSCHILD);
    T_EQ(nseen, 0);

    T_CASE("a parent that is not a window: NULL and ERROR_INVALID_WINDOW_HANDLE");
    T_TRUE(make("CA", WS_POPUP, (HWND)(uintptr_t)0x40000FF0, 0, 0, 1, 1) == NULL);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    HWND k = make("CA", WS_POPUP, NULL, 0, 0, 1, 1);
    T_REQUIRE(k != NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* Stops of the core (rt_window_create: not implemented), each after the cases above. */
    T_TRAPS(make("BUTTON", WS_POPUP, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(CALL(user32, CreateWindowExA, 0, (LPCVOID)(uintptr_t)0x0001, NULL, WS_POPUP, 0, 0, 1, 1, NULL, NULL, NULL, NULL), "rt_window_create: not implemented");
    T_TRAPS(make("CA", 0, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CA", WS_POPUP | WS_THICKFRAME, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CN", WS_POPUP, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CC", WS_POPUP, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(CALL(user32, CreateWindowExA, 0, "CA", NULL, WS_CHILD, 0, 0, 1, 1, RT_HWND_DESKTOP, NULL, NULL, NULL), "rt_window_create: not implemented");
    T_TRAPS(CALL(user32, CreateWindowExA, 0, "CA", NULL, WS_POPUP, 0, 0, 1, 1, HWND_MESSAGE, NULL, NULL, NULL), "rt_window_create: not implemented");
    T_TRAPS(make("CW", WS_POPUP, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CA", WS_POPUP | WS_VISIBLE, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CA", WS_POPUP | WS_MINIMIZE, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CA", WS_POPUP | WS_MAXIMIZE, NULL, 0, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CA", WS_POPUP, NULL, CW_USEDEFAULT, 0, 1, 1), "rt_window_create: not implemented");
    T_TRAPS(make("CA", WS_POPUP, NULL, 0, 0, CW_USEDEFAULT, 1), "rt_window_create: not implemented");
    T_TRAPS(CALL(user32, CreateWindowExA, 0, "CA", NULL, WS_CHILD | WS_POPUP, 0, 0, 1, 1, p, NULL, NULL, NULL), "rt_window_create: not implemented");
    T_TRAPS(CALL(user32, CreateWindowExA, 0, "CA", NULL, WS_POPUP, 0, 0, 1, 1, NULL, (HMENU)(uintptr_t)1, NULL, NULL), "rt_window_create: not implemented");
}
