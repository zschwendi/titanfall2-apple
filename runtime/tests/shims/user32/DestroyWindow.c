#include "rt/test.h"
#include "rt/window.h"

/* The messages the test looks at, recorded in the order the procedure receives them. */
typedef struct { UINT msg; HWND h; WPARAM w; LPARAM l; } Rec;
static Rec log_[64];
static unsigned nlog;
static bool make_child_in_destroy;

static bool recorded(UINT msg)
{
    return msg == WM_DESTROY || msg == WM_NCDESTROY || msg == WM_PARENTNOTIFY
        || msg == WM_WINDOWPOSCHANGING || msg == WM_WINDOWPOSCHANGED;
}

static bool at(unsigned i, UINT msg, HWND h)
{
    return i < nlog && log_[i].msg == msg && log_[i].h == h;
}

static HWND make(Ctx *ctx, DWORD style, HWND parent)
{
    RtWindowCreate a = { .style = style, .cls = RT_TEXT_W(u"T"), .title = RT_TEXT_W(u"Title"),
                         .x = 10, .y = 20, .cx = 300, .cy = 200, .parent = parent, .unicode = true };
    return rt_window_create(ctx, &a);
}

/* Records the messages of interest; a window answers WM_NCCREATE with TRUE and the rest with 0. */
GUEST_FN(destroy_proc)
{
    UINT msg = (UINT)ctx->r[2];
    HWND h = (HWND)(uintptr_t)ctx->r[1];

    if (recorded(msg) && nlog < 64) {
        log_[nlog].msg = msg;
        log_[nlog].h = h;
        log_[nlog].w = (WPARAM)ctx->r[8];
        log_[nlog].l = (LPARAM)ctx->r[9];
        nlog++;
    }
    if (msg == WM_DESTROY && make_child_in_destroy) {
        make_child_in_destroy = false;
        make(ctx, WS_CHILD, h);                 /* a new child while the window is destroyed */
    }
    ctx->r[0] = (msg == WM_NCCREATE || msg == WM_NCACTIVATE);
    GUEST_RET(ctx);
}

TEST(user32, DestroyWindow)
{
    HWND h, p, c, g, o, p2, c2;

    WNDCLASSEXW wc = { .cbSize = sizeof wc, .lpfnWndProc = T_GUEST_ADDR(destroy_proc),
                       .lpszClassName = u"T", .cbWndExtra = 16 };
    rt_wndclass_register(&wc, true);

    T_CASE("destroys a window and invalidates its handle");
    h = make(T_CTX(), WS_POPUP, NULL);
    T_EQ(CALL(user32, DestroyWindow, h), TRUE);
    T_EQ(CALL(user32, DestroyWindow, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("fails with ERROR_INVALID_WINDOW_HANDLE for NULL, a free slot and a destroyed window");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, DestroyWindow, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, DestroyWindow, (HWND)(uintptr_t)0x40000FF0), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);
    h = make(T_CTX(), WS_POPUP, NULL);
    T_EQ(CALL(user32, DestroyWindow, h), TRUE);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(user32, DestroyWindow, h), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INVALID_WINDOW_HANDLE);

    T_CASE("returns TRUE and leaves last error alone");
    h = make(T_CTX(), WS_POPUP, NULL);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, DestroyWindow, h), TRUE);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("sends WM_DESTROY to a window, its children and grandchildren, then WM_NCDESTROY in post-order");
    p = make(T_CTX(), WS_POPUP, NULL);
    c = make(T_CTX(), WS_CHILD, p);
    g = make(T_CTX(), WS_CHILD, c);
    nlog = 0;
    T_EQ(CALL(user32, DestroyWindow, p), TRUE);
    T_EQ(nlog, 6);
    T_TRUE(at(0, WM_DESTROY, p));
    T_TRUE(at(1, WM_DESTROY, c));
    T_TRUE(at(2, WM_DESTROY, g));
    T_TRUE(at(3, WM_NCDESTROY, g));
    T_TRUE(at(4, WM_NCDESTROY, c));
    T_TRUE(at(5, WM_NCDESTROY, p));
    T_EQ(CALL(user32, DestroyWindow, g), FALSE);
    T_EQ(CALL(user32, DestroyWindow, c), FALSE);

    T_CASE("sends WM_PARENTNOTIFY to the parent of a child destroyed by itself and none for children destroyed with their parent");
    p = make(T_CTX(), WS_POPUP, NULL);
    c = make(T_CTX(), WS_CHILD, p);
    nlog = 0;
    T_EQ(CALL(user32, DestroyWindow, c), TRUE);
    T_EQ(nlog, 3);
    T_TRUE(at(0, WM_PARENTNOTIFY, p));
    T_EQ(log_[0].w & 0xFFFF, WM_DESTROY);
    T_TRUE(log_[0].l == (LPARAM)(uintptr_t)c);
    T_TRUE(at(1, WM_DESTROY, c));
    T_TRUE(at(2, WM_NCDESTROY, c));
    p2 = make(T_CTX(), WS_POPUP, NULL);
    c2 = make(T_CTX(), WS_CHILD, p2);
    nlog = 0;
    T_EQ(CALL(user32, DestroyWindow, p2), TRUE);
    T_EQ(nlog, 4);
    for (unsigned i = 0; i < nlog; i++) T_TRUE(log_[i].msg != WM_PARENTNOTIFY);
    T_TRUE(at(0, WM_DESTROY, p2));
    T_TRUE(at(1, WM_DESTROY, c2));
    T_TRUE(at(2, WM_NCDESTROY, c2));
    T_TRUE(at(3, WM_NCDESTROY, p2));
    T_EQ(CALL(user32, DestroyWindow, c2), FALSE);

    T_CASE("destroys owned windows completely before the owner's WM_DESTROY");
    p = make(T_CTX(), WS_POPUP, NULL);
    o = make(T_CTX(), WS_POPUP, p);
    nlog = 0;
    T_EQ(CALL(user32, DestroyWindow, p), TRUE);
    T_EQ(nlog, 4);
    T_TRUE(at(0, WM_DESTROY, o));
    T_TRUE(at(1, WM_NCDESTROY, o));
    T_TRUE(at(2, WM_DESTROY, p));
    T_TRUE(at(3, WM_NCDESTROY, p));
    T_EQ(CALL(user32, DestroyWindow, o), FALSE);

    T_CASE("hides a shown window before destroying it");
    h = make(T_CTX(), WS_POPUP, NULL);
    rt_window_show(h, SW_SHOW);
    nlog = 0;
    T_EQ(CALL(user32, DestroyWindow, h), TRUE);
    T_EQ(nlog, 4);
    T_TRUE(at(0, WM_WINDOWPOSCHANGING, h));
    T_TRUE(at(1, WM_WINDOWPOSCHANGED, h));
    T_TRUE(at(2, WM_DESTROY, h));
    T_TRUE(at(3, WM_NCDESTROY, h));

    /* The STOPS of rt_window_destroy that a test reaches: each ends in a fatal report. */
    T_TRAPS(CALL(user32, DestroyWindow, RT_HWND_DESKTOP), "rt_window_destroy: not implemented");
    p = make(T_CTX(), WS_POPUP, NULL);
    make_child_in_destroy = true;
    T_TRAPS(CALL(user32, DestroyWindow, p), "rt_window_destroy: not implemented");
}
