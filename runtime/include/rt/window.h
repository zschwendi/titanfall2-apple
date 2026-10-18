/* Windows, classes, messages, the desktop, cursor, keyboard, hooks and the clipboard with no
 * screen (user32.dll; docs/handoff/rt-contract-window.md is the contract and says, for every
 * imported function, which of these it sits on).  Device contexts and GDI objects: rt/gdi.h.
 *
 *     piece (unit of work)   file                   state        who
 *     (this unit)            core/win.c             built        the user table, the desktop, frames of
 *                                                                frameless windows, the active/focus getters
 *     (this unit)            core/win_msgbox.c      built        rt_message_box
 *     win-class              core/win_class.c       built        classes, registered messages
 *     win-window             core/win_window.c      built        create, destroy, position, show, send, dispatch, DefWindowProc
 *     win-query              core/win_query.c       built        what a window record is asked
 *     win-state              core/win_state.c       built        show (its checks), paint state, focus, capture
 *     win-queue              core/win_queue.c       built        the message queue of a thread
 *     win-input              core/win_input.c       built        cursor and keyboard state
 *     win-hook               core/win_hook.c        built        hook records
 *     win-clipboard          core/win_clipboard.c   built        the clipboard of this process
 *     win-keymap             core/win_keymap.c      built        the US layout: MapVirtualKeyEx, a key's character, TranslateMessage
 *     win-frame              core/win.c             part built   rt_window_frame: fixed frames and captions; not thick
 *                                                                frames, menus, tool-window captions
 * (The section titles below still carry the mark of the day they were specified.)
 * A function marked [not built] calls RT_CORE_UNIMPLEMENTED with its piece's name; its comment
 * here is what its body must do.  Where a comment says "(piece)" the unit that builds the
 * piece settles the point and writes it here.
 *
 * NO SCREEN.  Nothing in this header draws, and no window is ever on a screen: there is
 * none before Gate 4 (PLAN.md gives real windows and input to SDL3 there).  A window is a
 * RECORD: class, procedure, styles, rectangle, text, extra bytes, owner thread.  Everything a
 * record is asked is answered from the record as Windows documents; everything that would
 * need the screen STOPS (below).  A window can be created, queried, positioned, sent messages
 * and destroyed.  It can also be SHOWN ON ITS RECORD (the owner's decision of 10 October, with
 * the do-nothing device): ShowWindow sets WS_VISIBLE on the record, makes it the active,
 * focused, foreground window and sends what Windows sends; nothing appears anywhere, no paint
 * message is ever sent and no input ever arrives (rt_window_show).
 *
 * SOURCES.  This header was written from memory.  Part of it was then checked against
 * Microsoft's documentation and Wine's conformance tests, twice.  docs/reviews/contracts-check.md:
 * its rows are cited here as "review V1" to "V20" for what was confirmed, "C1" to "C9" for what
 * was wrong and is corrected below, "N1" to "N11" for what could not be settled.
 * docs/reviews/contracts-check-2.md: "V21" to "V57" and "Q1" to "Q4" confirmed, "D1" to "D21"
 * wrong and corrected below, "S1" to "S35" not settled.  docs/reviews/win-window-sequences.md
 * (the message sequences of creation, show, positioning and destruction): cited as "seq S1" to
 * "seq S18" for a sequence and "seq 1.1" to "seq 6.8" for a row.  A statement that names a test file of
 * Wine, a Microsoft page or a V, C, D or Q row is checked; one that names an N or S row is NOT:
 * the row says what was looked at and what one call on Windows would settle.  EVERY OTHER
 * statement about Windows behaviour here is FROM MEMORY and not verified, whether or not the
 * comment repeats the mark: "documented" alone means remembered from the function's
 * learn.microsoft.com page; "Wine" alone means remembered from Wine's user32 or win32u
 * sources.  The desktop's numbers come from the trace of the original
 * (docs/findings/30-proton-trace.md, section 9).
 *
 * STOPS.  A case this runtime does not answer ends the run with
 *     FATAL <core function>: not implemented: <what, with the offending value>
 * (rt_fatal; the core has no Ctx everywhere).  It never returns a default.  A shim adds no
 * guard for such a case: it lists it in RT-TRAPS as `core: <case>` and proves it with T_TRAPS.
 *
 * LAST ERROR.  Convention 1 of runtime/README.md: a function here that fails sets the calling
 * thread's last error itself (rt_thread_set_last_error) and says which code; none sets it on
 * success.  A shim returns the result and sets nothing.  The functions that cannot fail say so.
 *
 * LOCKS.  One host lock (os_unfair_lock, core/win_priv.h) guards every record, list and
 * table of this header and of rt/gdi.h.  It is held for a few instructions and NEVER across
 * a guest call (a window procedure, a hook, an enumeration callback), a wait or a log line.
 * A function that calls guest code takes a reference to the record, releases the lock, calls,
 * and looks the record up again afterwards: the procedure may have destroyed the window.
 *
 * THREADS.  A window belongs to the guest thread that created it; so does a message queue.
 * Posting to another thread's window works (rt_msg_post).  SENDING a message to another
 * thread's window is a stop: the piece win-queue has the queues and not cross-thread sends,
 * which nothing has asked for yet.  Destroying another thread's window is not a stop: it fails as on
 * Windows (rt_window_destroy; review V15).  Every function may be called from any guest thread.
 *
 * HANDLES.  Windows, device contexts, GDI objects and hooks are objects (rt/handle.h) in a
 * second handle table, "user" (RUNTIME.md 4.1): generations 256 to 511, so every such handle
 * is 0x40000000 or above and never equals a kernel handle; CloseHandle on one fails.  The
 * values below are not in any table: they are constants the core recognises.
 */
#ifndef RT_WINDOW_H
#define RT_WINDOW_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/win/base.h"
#include "rt/win/user.h"

/* ---- opaque constants (all below 0x40000000; none is ever freed) ----------------------------- */
#define RT_HWND_DESKTOP      ((HWND)(uintptr_t)0x00010010)     /* GetDesktopWindow */
#define RT_HWINSTA           ((HWINSTA)(uintptr_t)0x00010020)  /* GetProcessWindowStation: "WinSta0" */
#define RT_HMONITOR_PRIMARY  ((HMONITOR)(uintptr_t)0x00010001) /* the one monitor */
#define RT_HKL_US            ((HKL)(uintptr_t)0x04090409)      /* US English keyboard, the only layout (RUNTIME.md 8.4) */
#define RT_HCURSOR_SYSTEM(id) ((HCURSOR)(uintptr_t)(0x00010000u | (unsigned)(id)))  /* LoadCursor(NULL, IDC_x) */
#define RT_HICON_SYSTEM(id)   ((HICON)(uintptr_t)(0x00020000u | (unsigned)(id)))    /* LoadIcon(NULL, IDI_x) */

/* Text that crosses into the core: the guest's pointer, wide or narrow (code page 1252),
 * exactly one of the two set; both NULL is the guest's NULL.  A pointer below 0x10000 is an
 * atom (MAKEINTATOM), where the function's comment allows one. */
typedef struct RtText { const WCHAR *w; const char *a; } RtText;
#define RT_TEXT_W(p)  ((RtText){ .w = (p), .a = NULL })
#define RT_TEXT_A(p)  ((RtText){ .w = NULL, .a = (p) })

/* ---- the desktop (core/win.c, built) ------------------------------------------------------------ */

/* What this machine's desktop is.  One monitor, the primary, at the origin.
 *   width, height   1920 x 1080: the display mode the traced run of the original set and
 *                   drew at (finding 30 section 9, "Setting display mode: 1920x1080@60" and the
 *                   swap chain's size).  The traced machine's own desktop mode, 5120x2880, is
 *                   only inferred there and is not used.
 *   bits_per_pixel  32: the traced swap chain was B8G8R8A8.
 *   refresh_hz      60: the same log line.
 *   dpi             96: what Windows tells a process that has not declared itself DPI-aware
 *                   ("High DPI Desktop Application Development on Windows": DPI unaware
 *                   applications render at 96; Wine user32/tests/sysparams.c
 *                   test_dpi_stock_objects).  None of the 20 boot-path DLLs imports SetProcessDPIAware;
 *                   the executable's manifest cannot be read (it is never opened).
 *   monitor, work   {0, 0, 1920, 1080} both: there is no shell and so no taskbar (the same
 *                   reason GetShellWindow answers NULL). */
typedef struct RtDesktop {
    LONG  width, height;
    DWORD bits_per_pixel, refresh_hz, dpi;
    RECT  monitor, work;
    LONG  caption_height;                   /* NONCLIENTMETRICS.iCaptionHeight: see rt_window_frame */
} RtDesktop;
const RtDesktop *rt_desktop(void);          /* never NULL, never changes, needs no init */

/* GetSystemMetrics(index): true and *value for the metrics this runtime answers, false for
 * every other index (the shim then stops with the index: a metric is never guessed).
 *   SM_CXSCREEN, SM_CYSCREEN, SM_CXVIRTUALSCREEN, SM_CYVIRTUALSCREEN   1920, 1080, 1920, 1080
 *   SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN   0, 0        SM_CMONITORS          1
 *   SM_SAMEDISPLAYFORMAT   1                           SM_REMOTESESSION      0
 *   SM_CLEANBOOT           0 (a normal boot)           SM_SWAPBUTTON         0
 *   SM_CXICON, SM_CYICON, SM_CXCURSOR, SM_CYCURSOR   32 at 96 dpi (Wine user32/tests/sysparams.c,
 *                                                    test_GetSystemMetrics)
 *   SM_CXSMICON, SM_CYSMICON   16
 * Not answered on purpose: every frame, caption, border and scroll-bar size (they belong to
 * rt_window_frame's piece), and everything about the mouse and its buttons (Gate 4's input).
 * Sets no last error. */
bool rt_desktop_metric(INT index, INT *value);

/* GetDeviceCaps(display dc, index): true and *value for
 *   HORZRES, DESKTOPHORZRES 1920   VERTRES, DESKTOPVERTRES 1080   BITSPIXEL 32   PLANES 1
 *   LOGPIXELSX, LOGPIXELSY 96      VREFRESH 60
 *   HORZSIZE 508, VERTSIZE 286     millimetres.  NOT Windows' values: Windows reports the
 *                                  monitor's physical size and there is no monitor.  This
 *                                  contract's choice: the pixel size at 96 per inch, rounded
 *                                  (1920 * 25.4 / 96 and 1080 * 25.4 / 96); review N5
 * false for every other index (the caller stops with it).  Sets no last error. */
bool rt_desktop_device_cap(INT index, INT *value);

/* The frame Windows puts around a client area: AdjustWindowRectEx, the default WM_NCCALCSIZE
 * and the client rectangle of a record.  *insets receives how far the window rectangle
 * extends beyond the client area on each side (all four >= 0).
 * THE ALGORITHM is the one Wine's tests compare with Windows for many styles (user32/tests/
 * win.c, wine_AdjustWindowRectEx and test_AWR_flags; review N4):
 *   every side   1 if the extended style has WS_EX_STATICEDGE and not WS_EX_DLGMODALFRAME;
 *                else 2 if it has WS_EX_DLGMODALFRAME or the style has WS_DLGFRAME;
 *                plus 1 if the style has WS_BORDER or WS_DLGFRAME, or WS_EX_DLGMODALFRAME;
 *                plus 2 (SM_CXEDGE: Wine user32/tests/sysparams.c) for WS_EX_CLIENTEDGE
 *   the top      plus the caption height + 1 for a style with all of WS_CAPTION
 * So no frame bit gives {0, 0, 0, 0}; WS_CAPTION gives 3 all round and 3 + 23 on top; the
 * game's own window (WS_CAPTION, WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE: docs/trace/
 * window-calls.md) gives {5, 28, 5, 5}.  WS_EX_WINDOWEDGE alone adds nothing.
 * THE CAPTION HEIGHT is rt_desktop()->caption_height, 22.  It is NOT a fact about Windows:
 * iCaptionHeight is a setting of the user's (NONCLIENTMETRICS's page; SystemParametersInfo,
 * SPI_SETNONCLIENTMETRICS), like the display mode, and this desktop's value is THIS RUNTIME'S
 * CHOICE: 22 is the usual figure of a default Windows 7 or 10 at 96 dpi, from memory and not
 * verified (reviews N4, S3: no source read gives the defaults).  Nothing is ever drawn with
 * it; it only has to be the same in AdjustWindowRectEx, WM_NCCALCSIZE and GetClientRect,
 * which all come here.  GetSystemMetrics does not answer SM_CYCAPTION (rt_desktop_metric).
 * [not built: win-frame] false, and the caller stops with the styles, for the three cases
 * whose number this desktop does not have: WS_THICKFRAME (the sizing border: iBorderWidth
 * and, since Vista, the padded border), menu TRUE (iMenuHeight), and WS_CAPTION with
 * WS_EX_TOOLWINDOW (iSmCaptionHeight).  A top-level window that is neither WS_POPUP nor
 * WS_CHILD always has a caption on Windows (from memory): rt_window_create stops for such a
 * window before it asks here.
 * Sets no last error. */
bool rt_window_frame(DWORD style, DWORD exstyle, BOOL menu, RECT *insets);

/* GetActiveWindow, GetFocus, GetForegroundWindow.  NULL until a window has been activated:
 * by its first show (rt_window_show) or by a SetWindowPos without SWP_NOACTIVATE.  Cannot
 * fail; set no last error. */
HWND rt_window_active(void);
HWND rt_window_focus(void);
HWND rt_window_foreground(void);

/* ---- message boxes (core/win_msgbox.c, built) -------------------------------------------------- */

/* MessageBoxA/W(hWnd, lpText, lpCaption, uType).  Nobody can read a box or press its button,
 * and the answer (IDOK, IDYES, ...) would be invented: so every message box ENDS THE RUN,
 * after its text has been put where it will be read.  A fatal engine error ends in one.
 *   1. The caption and the text go to the log, channel `trap`, level ERROR, one rt_log line
 *      per line of text ("\n" or "\r\n" ends a line), each as
 *          MessageBox caption: <caption>          MessageBox text: <line>
 *      Narrow text is code page 1252 and wide text UTF-16; both are written as UTF-8, a
 *      control character other than tab as \xNN, an unpaired surrogate as \uNNNN.  NULL text
 *      is the line `MessageBox text: (null)`; a NULL caption is `MessageBox caption: (null)`
 *      (Windows shows "Error" for a NULL caption; the log says what was passed).
 *   2. The fatal report (ONE REPORT, rt/crash.h):
 *          FATAL guest called MessageBox: <caption>
 *            text: <the first 2,000 bytes of the text as written to the log, line ends as "\n" + 4 spaces>
 *            uType 0x<type>, owner window 0x<hWnd>
 *      then the lines of rt_report_context (guest caller, thread, registers), then abort().
 * Never returns, never fails, sets no last error.  `ctx` may be NULL (printed as no guest
 * thread).  Text is read up to its terminator; at most 16,000 bytes of it are logged. */
_Noreturn void rt_message_box(Ctx *ctx, HWND owner, RtText text, RtText caption, UINT type);

/* ---- classes and registered messages (core/win_class.c, built) --------------------------------- */

/* RegisterClassExA/W (the shims of RegisterClassA/W build a WNDCLASSEX with cbSize 80 and
 * hIconSm NULL).  `unicode` false: the two string members are `const char *` in code page
 * 1252.  The class is kept by name, compared without regard to ASCII case, stored wide.
 * (names compare without regard to case and a class atom is a name of the registered-name
 * table, 0xC000 up: Wine user32/tests/class.c, test_instances and ClassTest; review V40).
 * Returns the class atom, a number from 0xC000 up taken from the table that
 * rt_wndmsg_register also uses (that the same string gives the same number in both: from
 * memory, review S22).  A name keeps its number for the life of the process, also across
 * an unregistration (this runtime's choice; on Windows the name leaves the table, V40).
 * hInstance NULL IS THE MAIN MODULE: it is stored, and compared, as the executable's handle
 * (rt_module_exe()->base, what GetModuleHandle(NULL) gives), so GCLP_HMODULE of such a class
 * is the executable (class.c, test_instances: "null handle means the same thing as main
 * module"; review D6).  0 with last error:
 *   ERROR_INVALID_PARAMETER       cbSize not 80 (one less and one more are both asserted);
 *                                 cbClsExtra or cbWndExtra negative (class.c, test_instances;
 *                                 review Q3).  From memory: wc NULL; a NULL class name; a name
 *                                 longer than 255 characters (WNDCLASSEXW's page gives 256 as the
 *                                 maximum length and says a longer name fails, with no code;
 *                                 whether a name of exactly 256 registers is not settled, S23)
 *   ERROR_CLASS_ALREADY_EXISTS    (1410) a class of that name is registered with the same
 *                                 hInstance, NULL and the executable's handle being the same
 *                                 one (the same test; Q3, D6)
 * These are looked at first, in that order, then the stops.  Stored as given: style,
 * lpfnWndProc, both extra sizes (the class's extra bytes start as zero), hIcon, hCursor,
 * hbrBackground, hIconSm; hInstance as said above.  Nothing is checked about the handles:
 * nothing is ever drawn with them, and a NULL icon, cursor or brush is legal.
 * STOPS: a class name that is an atom; an empty name (what Windows answers is not verified);
 * lpfnWndProc 0 (what Windows answers is not settled, Q3); lpszMenuName not NULL (menus are
 * not built); cbClsExtra or cbWndExtra above 40 (a record holds 40 bytes.  Windows fails such
 * a registration, "About Window Classes", review V57, but with which code is not settled,
 * S2: so no code is invented); a name already registered with a DIFFERENT hInstance
 * (per-module classes of one name are not built); more than 256 classes; the table of names
 * full (16,384 names). */
ATOM rt_wndclass_register(const WNDCLASSEXW *wc, bool unicode);

/* UnregisterClassA/W(lpClassName, hInstance).  `name` may be an atom.  TRUE and the class
 * is gone.  FALSE with ERROR_CLASS_DOES_NOT_EXIST (no such class; also a NULL name, an empty
 * one and one longer than 255 characters, which no class can have) or ERROR_CLASS_HAS_WINDOWS
 * (a window of the class still exists).  Both codes: Wine user32/tests/class.c.  `inst` is
 * not compared (one class per name, see above). */
BOOL rt_wndclass_unregister(RtText name, HINSTANCE inst);

/* RegisterWindowMessageA(lpString): the message number of `name`, 0xC000 to 0xFFFF, the same
 * for the same string for the life of the process (RegisterWindowMessageA's page, return
 * value and remarks; Wine user32/tests/msg.c, test_broadcast; review Q4).  ASCII case is
 * ignored (from memory: review S22).  Cannot fail for a name of 1 to 255 characters.
 * STOPS: NULL, the empty string, a longer name (what Windows answers for the three is not
 * settled, S22), a pointer value below 0x10000 (an atom), a full table (16,384 names, shared
 * with the classes). */
UINT rt_wndmsg_register(RtText name);

/* ---- windows (core/win_window.c) [built: win-window] -------------------------------------------- */
/* MESSAGES THE SYSTEM SENDS go to the procedure the record has at that moment, called as
 * guest code on the calling thread with no lock held and last error left alone.  The
 * procedure may be one of our own shims: the game's class procedure is DefWindowProcW
 * itself (docs/trace/window-calls.md).  After every such call the record is looked up
 * again by its handle. */

typedef struct RtWindowCreate {
    DWORD     exstyle, style;
    RtText    cls, title;              /* cls may be an atom */
    INT       x, y, cx, cy;
    HWND      parent;                  /* NULL, RT_HWND_DESKTOP, HWND_MESSAGE or a window */
    HMENU     menu;                    /* a child's identifier, else must be NULL */
    HINSTANCE instance;
    LPVOID    param;
    bool      unicode;                 /* CreateWindowExW */
} RtWindowCreate;

/* CreateWindowExA/W of a hidden WS_POPUP or WS_CHILD window.  Makes the record and sends, in
 * this order (seq S2, seq 1.1 and 1.3; Wine user32/tests/msg.c, WmShowPopupFirstDrawSeq_1 and
 * WmCreateChildSeq; reviews V6, C2):
 *   1. WM_NCCREATE, lParam a CREATESTRUCT.
 *   2. WM_NCCALCSIZE, wParam 0, lParam the window rectangle.  What the procedure answers is
 *      CHECKED AND NOT USED: a record's client area is always its window rectangle less
 *      rt_window_frame, and a procedure that answers with another rectangle STOPS.
 *   3. WM_CREATE, the same CREATESTRUCT.
 *   4. WM_SIZE (wParam SIZE_RESTORED, lParam MAKELONG(client width, client height)), then
 *      WM_MOVE (lParam the client area's top left corner: screen coordinates for a popup,
 *      the parent's client coordinates for a child).
 *   5. For a WS_CHILD window without WS_EX_NOPARENTNOTIFY: WM_PARENTNOTIFY to the parent,
 *      wParam MAKEWPARAM(WM_CREATE, the child's identifier), lParam the child's handle (the
 *      two parameters: from memory of the message's page).
 * The handle is valid from before the first message (seq 1.7).
 * THE CREATESTRUCT (seq 1.8): lpCreateParams, hInstance, hMenu, hwndParent, x, y, cx, cy and
 * style are the caller's values as passed (cx and cy also when negative); dwExStyle is the
 * extended style the window gets (below); the two strings are the caller's pointers.
 * THE STYLES THE WINDOW GETS (seq 1.10; win.c, test_window_styles): a window that is not
 * WS_CHILD gains WS_CLIPSIBLINGS; WS_EX_WINDOWEDGE is set when the window has
 * WS_EX_DLGMODALFRAME, or WS_DLGFRAME or WS_THICKFRAME without WS_EX_STATICEDGE, and cleared
 * otherwise, whatever was passed.  GWL_STYLE and GWL_EXSTYLE read so from inside WM_NCCREATE on.
 * THE TITLE is stored by the default processing of WM_NCCREATE (rt_window_def_proc; seq 5.2),
 * not by this function: a procedure that does not pass WM_NCCREATE on leaves the window untitled.
 * THE RECTANGLE is {x, y, x + cx, y + cy}, in screen coordinates for a top-level window (a
 * WS_POPUP at (100, 200) of 150 x 150 has (100, 200, 250, 350): win.c, test_map_points;
 * review V33).  A negative cx or cy is taken as 0 (win.c, test_CreateWindow; review D8).
 * A new top-level window is first in the Z order, a new child LAST among its siblings
 * (seq 1.14; review D2).  The owner thread is the caller.  A parent of RT_HWND_DESKTOP for a
 * window that is not a child is no owner (from memory).
 * One log line, channel `window`, level WARN, for each top-level window: handle, class, title,
 * styles, position and size, the caller (module+rva) and the thread.
 * NULL with last error, looked at in this order before any stop but the first: a predefined
 * control class or a class atom below 0xC000 STOPS (BUTTON, COMBOBOX, EDIT, LISTBOX,
 * MDICLIENT, SCROLLBAR, STATIC, COMBOLBOX, IME, MESSAGE, #32768 to #32772: none is
 * registered); ERROR_CANNOT_FIND_WND_CLASS (1407: no class of that name, also when the class
 * is unregistered while the window is made; win.c, test_CreateWindow; review V22);
 * ERROR_TLW_WITH_WSCHILD (1406: WS_CHILD with a NULL parent, the procedure is not called;
 * win.c, test_parent_owner; review C3); ERROR_INVALID_WINDOW_HANDLE (the parent is not a
 * window; from memory, review S9).
 * STOPS, each naming its case:
 *   - a window that has WS_THICKFRAME or has neither WS_POPUP nor WS_CHILD: WM_GETMINMAXINFO
 *     comes first for it (seq S1, seq 1.2; review C1) and carries frame sizes this desktop
 *     does not have; such a window also gains WS_CAPTION and is limited to its tracking sizes;
 *   - WM_NCCREATE answered 0, or WM_CREATE answered -1: CreateWindowEx returns NULL (the two
 *     messages' pages; seq 1.11, 1.12), but which messages Windows sends to the window first
 *     is not settled (seq 1.13, review N1), so none is invented;
 *   - a message-only window (parent HWND_MESSAGE): its creation sequence is not settled (N1);
 *   - a class registered with the A function and a creation with the W one, or the reverse:
 *     Windows gives the procedure a CREATESTRUCT of ITS character type with the text
 *     converted (msg.c, test_create_name; seq 1.9), which is not built;
 *   - WS_VISIBLE in `style` (Windows creates the window hidden and then shows it, seq S4 and
 *     1.5: not built, the game shows its window with ShowWindow); WS_MINIMIZE or WS_MAXIMIZE;
 *     CW_USEDEFAULT in x or cx (it means 0 for a popup or a child, seq 1.15; what the
 *     CREATESTRUCT then carries was not read); WS_CHILD together with WS_POPUP; a WS_CHILD
 *     window of the desktop; a menu for a window that is not a child; a parent or owner that
 *     belongs to another thread; a window destroyed by its own procedure during creation;
 *     more than 4,096 windows. */
HWND rt_window_create(Ctx *ctx, const RtWindowCreate *args);

/* DestroyWindow(h).  The messages, in this order (the pages of WM_DESTROY and WM_NCDESTROY;
 * DestroyWindow's remarks; msg.c, test_DestroyWindow with destroy_window_with_children,
 * WmDestroyChildSeq, WmDestroyOverlappedSeq; review D1; seq S18, seq 4.1 to 4.4):
 *   1. If `h` is a WS_CHILD window without WS_EX_NOPARENTNOTIFY: WM_PARENTNOTIFY to its
 *      parent, wParam MAKEWPARAM(WM_DESTROY, the child's identifier), lParam the child's
 *      handle (the layout: from memory).  Children destroyed WITH their parent send none.
 *   2. If `h` is shown: WM_WINDOWPOSCHANGING with SWP_HIDEWINDOW | SWP_NOACTIVATE |
 *      SWP_NOSIZE | SWP_NOMOVE, the record loses WS_VISIBLE, WM_WINDOWPOSCHANGED with those
 *      and SWP_NOCLIENTSIZE | SWP_NOCLIENTMOVE (no WM_SHOWWINDOW); then, if it was the active
 *      window, its deactivation as under rt_window_show.
 *   3. Every window OWNED by `h` is destroyed completely, by these same rules.
 *   4. WM_DESTROY to `h`, then to each child in Z order, each child before its own children.
 *   5. WM_NCDESTROY in post-order: the children of a window before the window, `h` last.  A
 *      record leaves the table after its WM_NCDESTROY returns.
 * So for P with child C and C's child G: WM_DESTROY to P, C, G, then WM_NCDESTROY to G, C, P.
 * TRUE, last error untouched.  FALSE with ERROR_INVALID_WINDOW_HANDLE (the code: from memory,
 * review S10); with ERROR_ACCESS_DENIED for a window of another thread, which is what the
 * game's own DestroyWindow gets at the end of a session (msg.c, test_interthread_messages;
 * review V15, seq 4.5).  STOPS: RT_HWND_DESKTOP; a shown child; a window that gets a new
 * child while it is destroyed; the active window going away while another top-level window
 * is shown (which window Windows activates then is not settled: seq 4.9). */
BOOL rt_window_destroy(Ctx *ctx, HWND h);

/* DefWindowProcA/W for a record.  Answers:
 *   WM_NCCREATE  stores the title from the CREATESTRUCT, returns 1 (seq 5.2)
 *   WM_NCCALCSIZE (wParam 0 or TRUE)  shrinks the RECT at lParam (the first of an
 *                         NCCALCSIZE_PARAMS when wParam is TRUE) by rt_window_frame and
 *                         returns 0; lParam NULL: 0 (win.c, test_nonclient_area; review V23,
 *                         seq 5.3, 5.4).  A frame rt_window_frame refuses STOPS
 *   WM_CREATE, WM_DESTROY, WM_NCDESTROY, WM_NULL, WM_MOVE, WM_SIZE, WM_PARENTNOTIFY   0
 *   WM_CLOSE              rt_window_destroy; 0 (WM_CLOSE's remarks; V24, seq 5.8)
 *   WM_SETTEXT  stores the text, returns 1    WM_GETTEXT, WM_GETTEXTLENGTH  copy it out, length
 *                         (the three messages' pages; V25)
 *   WM_WINDOWPOSCHANGING  0.  For a window that has WS_THICKFRAME or is overlapped and a
 *                         WINDOWPOS without SWP_NOSIZE Windows first sends WM_GETMINMAXINFO
 *                         (seq 3.5): that case STOPS
 *   WM_WINDOWPOSCHANGED   sends WM_MOVE unless the WINDOWPOS has SWP_NOCLIENTMOVE, then WM_SIZE
 *                         unless it has SWP_NOCLIENTSIZE; returns 0 (the two messages' pages;
 *                         msg.c, WmSWP_ResizeSeq, WmSWP_MoveSeq; review D7, seq 3.4)
 *   WM_ACTIVATE           a window that is activated and not minimized gets the keyboard
 *                         focus: WM_KILLFOCUS to the window that had it (wParam the new
 *                         one), WM_SETFOCUS to this one (wParam the old one, 0 for none);
 *                         returns 0 (WM_ACTIVATE's remarks; seq 5.11, 2.6)
 *   WM_NCACTIVATE         TRUE (from memory; the page says the result is ignored for wParam
 *                         TRUE and that TRUE lets a deactivation proceed)
 *   WM_SHOWWINDOW, WM_ACTIVATEAPP, WM_SETFOCUS, WM_KILLFOCUS   0 (what a procedure is told to
 *                         return; DefWindowProc's own value is not settled, seq 2.14, and the
 *                         sender of these, which is this file, does not look at it)
 *   0x0400 to 0xFFFF (WM_USER, WM_APP and registered messages)   0: default processing does
 *                         nothing with a message it does not define (from memory, S24)
 *   an invalid window     0 (from memory, S24)
 * STOPS: every other message below 0x0400 (WM_GETMINMAXINFO among them: its default result is
 * not settled, seq 5.1) and above 0xFFFF, with its number; a message that carries text
 * (rt_window_send has the list) through the function of the other character type than the
 * window's. */
LRESULT rt_window_def_proc(Ctx *ctx, HWND h, UINT msg, WPARAM w, LPARAM l, bool unicode);

/* CallWindowProcA/W: call `proc` with the four arguments and return its result.  NOTHING IS
 * CONVERTED: a procedure here is always a plain guest address (Windows converts only when it
 * is given one of the procedure "handles" it hands out between A and W, seq 6.2, and this
 * runtime hands out none: rt_window_get_long stops where Windows would).  The game calls its
 * class procedure, DefWindowProcW, through CallWindowProcA in just this way.  STOPS: proc 0. */
LRESULT rt_window_call_proc(Ctx *ctx, WNDPROC proc, HWND h, UINT msg, WPARAM w, LPARAM l, bool unicode);

/* SendMessageA/W to a window of the calling thread: its procedure is called directly and
 * its result returned.  An invalid handle: 0 with ERROR_INVALID_WINDOW_HANDLE (from memory:
 * review S11, not settled).
 * STOPS: a window of another thread; HWND_BROADCAST; RT_HWND_DESKTOP; a message that carries
 * text or a character when `unicode` differs from the window's own character type (Windows
 * converts, seq 6.1 to 6.6; not built).  The list (seq 6.7, from each message's page):
 * WM_CREATE, WM_NCCREATE, WM_SETTEXT, WM_GETTEXT, WM_GETTEXTLENGTH, WM_CHAR, WM_DEADCHAR,
 * WM_SYSCHAR, WM_SYSDEADCHAR, WM_IME_CHAR, WM_MENUCHAR, WM_CHARTOITEM, WM_SETTINGCHANGE,
 * WM_DEVMODECHANGE, WM_ASKCBFORMATNAME, WM_MDICREATE.  The string messages of the predefined
 * controls are not in it: no such window exists. */
LRESULT rt_window_send(Ctx *ctx, HWND h, UINT msg, WPARAM w, LPARAM l, bool unicode);

/* DispatchMessageA/W: msg->hwnd NULL returns 0; otherwise as rt_window_send with the
 * message's fields (an invalid window: 0 with ERROR_INVALID_WINDOW_HANDLE; both answers from
 * memory: review S11, not settled).  STOPS: msg NULL; WM_TIMER with a callback in lParam. */
LRESULT rt_window_dispatch(Ctx *ctx, const MSG *msg, bool unicode);

/* SetWindowPos on a record of the calling thread (seq S13 to S16, seq 3.1 to 3.10; msg.c,
 * WmSWP_ResizeSeq, WmSWP_MoveSeq, WmFrameChanged, WmZOrder; review D7).  In this order:
 *   1. WM_WINDOWPOSCHANGING with the call AS IT WAS MADE: hwndInsertAfter, x, y, cx, cy and
 *      flags as passed, also where SWP_NOMOVE or SWP_NOSIZE make them meaningless (seq 3.1).
 *      Not sent with SWP_NOSENDCHANGING.  The procedure may change x, y, cx, cy.
 *   2. If nothing changes (no move, no size, no order, no SWP_FRAMECHANGED, no activation):
 *      TRUE, and nothing more is sent (seq 3.8).
 *   3. WM_NCCALCSIZE with wParam TRUE if the size changes or SWP_FRAMECHANGED is given, not
 *      for a move alone (seq 3.6); its answer is checked as in rt_window_create.
 *   4. The record gets its rectangle and its place: HWND_TOP, HWND_BOTTOM, or behind the
 *      sibling `after`; an `after` that is a window but not a sibling does nothing (seq 3.10).
 *   5. Without SWP_NOACTIVATE a top-level window that is not the active one is activated,
 *      HIDDEN OR NOT (seq 3.9): the messages of rt_window_show's steps 3 to 5.
 *   6. WM_WINDOWPOSCHANGED with the rectangle the window now has and the flags plus (seq 3.2,
 *      3.3): SWP_NOMOVE, SWP_NOSIZE for a position or size that did not change;
 *      SWP_NOCLIENTMOVE, SWP_NOCLIENTSIZE when the client area kept its corner, its size;
 *      SWP_NOREDRAW for a hidden window.  The default procedure then sends WM_MOVE, WM_SIZE.
 * TRUE; FALSE with ERROR_INVALID_WINDOW_HANDLE (from memory).
 * STOPS: a window of another thread; RT_HWND_DESKTOP; flag bits Windows does not define;
 * SWP_SHOWWINDOW, and SWP_HIDEWINDOW for a shown window (a window is shown and hidden with
 * ShowWindow); HWND_TOPMOST and HWND_NOTOPMOST; an `after` that is no window; a child without
 * SWP_NOACTIVATE (what Windows activates is not settled); an activation while another window
 * is active; a procedure that changes the flags or the order in WM_WINDOWPOSCHANGING; a frame
 * rt_window_frame refuses.  The game calls no SetWindowPos in a whole session. */
BOOL rt_window_set_pos(Ctx *ctx, HWND h, HWND after, INT x, INT y, INT cx, INT cy, UINT flags);

/* SetWindowTextA/W, GetWindowTextA: through WM_SETTEXT and WM_GETTEXT sent to the window's
 * procedure, as Windows does for a window of the calling process (the two functions' pages;
 * Wine user32/tests/win.c, test_gettext: one message each; review V26).  `cap` counts
 * characters, terminator included; `cap` 0: 0, nothing is sent and the buffer is not touched
 * (the same test).  FALSE or 0 with ERROR_INVALID_WINDOW_HANDLE (the code: from memory,
 * review S12).  STOPS: those of rt_window_send (a window of another thread; the function of
 * the other character type). */
BOOL rt_window_set_text(Ctx *ctx, HWND h, RtText text);
INT  rt_window_get_text(Ctx *ctx, HWND h, void *buf, INT cap, bool unicode);

/* FOR THE DEVICE.  What a swap chain asks of its output window (docs/handoff/d3d-contract.md
 * section 5): the size of the client area, for a CreateSwapChain or ResizeBuffers that is
 * passed a width or height of 0.  false (nothing written, no last error set) for a handle
 * that is not a window, which also answers "is this a window"; STOPS for a frame
 * rt_window_frame refuses.  May be called from any thread; calls no guest code.  Whether the
 * window is minimized is rt_window_is_iconic (FALSE for every record today). */
bool rt_window_client_size(HWND h, UINT *width, UINT *height);

/* ---- what a record is asked (core/win_query.c) [not built: win-query] -------------------------- */
/* Each takes the lock, reads the record and returns; none calls guest code.  `h` invalid
 * (0, a stale or foreign handle, a constant other than RT_HWND_DESKTOP where that is
 * allowed): the failure value with ERROR_INVALID_WINDOW_HANDLE, and nothing is written
 * (Wine user32/tests/win.c: test_handles for GetWindowRect, test_map_points for
 * ClientToScreen; review V27.  For the other functions of this section the code is from
 * memory: reviews S15, S16, S35). */

/* GetWindowRect: the window rectangle in screen coordinates.  GetClientRect: {0, 0, w, h}
 * of the window rectangle less its frame (rt_window_frame; a frame it does not know STOPS).
 * RT_HWND_DESKTOP gives the monitor rectangle in both (GetWindowRect: Wine
 * user32/tests/sysparams.c, test_dpi_mapping, review V28; GetClientRect of it: from memory).
 * STOPS: `out` NULL, whatever `h` is (what Windows does is not settled, review S13: a NULL
 * POINT crashes ClientToScreen there, so no code is invented). */
BOOL rt_window_get_rect(HWND h, RECT *out);
BOOL rt_window_get_client_rect(HWND h, RECT *out);

/* ClientToScreen, ScreenToClient: add, subtract, the screen position of the client area's
 * top left corner (the window rectangle's corner plus the frame's left and top).  For
 * RT_HWND_DESKTOP the point is unchanged.  A child's rectangle is kept relative to its
 * parent's client area and the offsets add up the chain. */
BOOL rt_window_client_to_screen(HWND h, POINT *pt);
BOOL rt_window_screen_to_client(HWND h, POINT *pt);

/* GetWindowLongA, GetWindowLongPtrA/W.  `size` is 4 (GetWindowLong) or 8.  index >= 0 reads
 * `size` bytes of the window's extra bytes at that offset; index + size beyond cbWndExtra: 0
 * with ERROR_INVALID_INDEX (Wine user32/tests/win.c, test_desktop and test_hwnd_message;
 * review V29).  Negative indexes, both sizes: GWL_STYLE, GWL_EXSTYLE, GWL_ID (a
 * child's identifier), GWLP_USERDATA; with size 4 the result is the low 32 bits,
 * sign-extended (a user data of 1<<32 | 123 reads 123).  Negative indexes, size 8 only:
 * GWLP_HINSTANCE, GWLP_HWNDPARENT (the parent, or the owner of a top-level window),
 * GWLP_WNDPROC (the guest address).  With size 4 those three pointer indexes are refused, as
 * 64-bit Windows refuses them: 0 with ERROR_INVALID_INDEX (Wine user32/tests/win.c,
 * test_set_window_long_size; review C4, V14).  Any other negative index: 0 with
 * ERROR_INVALID_INDEX (from memory).  A result of 0 is also a valid value, as on Windows.
 * STOPS: size 8 and GWLP_WNDPROC with `unicode` different from the window's character type
 * (Windows returns a handle there); RT_HWND_DESKTOP. */
LONG_PTR rt_window_get_long(HWND h, INT index, unsigned size, bool unicode);

/* SetWindowLongA, SetWindowLongPtrA/W: store and return the previous value, by the rules of
 * rt_window_get_long.  A successful call whose previous value was 0 leaves last error alone
 * (the caller clears it first: SetWindowLongPtrA's page, return value; review V30).  With
 * size 8 GWLP_WNDPROC replaces the procedure (subclassing), except that a `value` of 0
 * changes nothing and returns the procedure the window has (Wine user32/tests/win.c,
 * test_SetWindowLong; V30).  With size 4 the three pointer indexes GWLP_WNDPROC,
 * GWLP_HINSTANCE and GWLP_HWNDPARENT return 0 with ERROR_INVALID_INDEX and NOTHING changes
 * (the same test; review C4).  STOPS: GWL_STYLE or GWL_EXSTYLE that would set WS_VISIBLE or
 * change a bit of the frame (WS_CAPTION, WS_THICKFRAME, the WS_EX edges); GWLP_HWNDPARENT
 * with size 8; the two stops of rt_window_get_long. */
LONG_PTR rt_window_set_long(HWND h, INT index, LONG_PTR value, unsigned size, bool unicode);

/* GetClassLongPtrA of the window's class: GCLP_WNDPROC, GCLP_HICON, GCLP_HICONSM,
 * GCLP_HCURSOR, GCLP_HBRBACKGROUND, GCLP_HMODULE, GCL_STYLE, GCL_CBWNDEXTRA, GCL_CBCLSEXTRA,
 * GCW_ATOM as registered (GCLP_HMODULE of a class registered with hInstance NULL is the main
 * module, see rt_wndclass_register); index >= 0 reads 8 bytes of the class's extra bytes,
 * which start as zero (WNDCLASSEXW's page, cbClsExtra; Wine user32/tests/class.c, ClassTest;
 * review V31).  Anything else, an unknown negative index or an index past the extra bytes: 0
 * with ERROR_INVALID_INDEX (from memory: review S14, not settled).
 * STOPS: GCLP_MENUNAME; RT_HWND_DESKTOP. */
ULONG_PTR rt_wndclass_get_long(HWND h, INT index, bool unicode);

/* IsIconic: FALSE for every valid record (a minimized record cannot exist: creation with
 * WS_MINIMIZE stops, and so does every minimizing ShowWindow) and for RT_HWND_DESKTOP.
 * IsWindowVisible: TRUE for a record that has WS_VISIBLE and whose ancestors all have it
 * (IsWindowVisible's page), so FALSE until rt_window_show has shown it; FALSE for a
 * message-only window; TRUE for RT_HWND_DESKTOP.  An invalid handle: FALSE with
 * ERROR_INVALID_WINDOW_HANDLE (FALSE is documented; the code is from memory, review S15). */
BOOL rt_window_is_iconic(HWND h);
BOOL rt_window_is_visible(HWND h);

/* GetWindow(h, cmd) and GetTopWindow(parent).  The order among siblings is changed only by
 * rt_window_set_pos and starts as Windows makes it:
 *   top-level windows   NEWEST first: a new one goes to the top of the Z order (Wine
 *                       user32/tests/win.c, test_popup_zorder; review V32)
 *   children of one parent   OLDEST first: a new child goes to the BOTTOM of its siblings
 *                       (CreateWindowExA's remarks; win.c, test_children_zorder: five WS_CHILD
 *                       windows come back in creation order; dialog.c, test_GetDlgItem; review D2)
 * GW_OWNER, GW_CHILD (the first child, so the first one created), GW_HWNDFIRST, GW_HWNDLAST,
 * GW_HWNDNEXT, GW_HWNDPREV.  rt_window_top(parent) is GW_CHILD of it; rt_window_top(NULL) and
 * rt_window_top(RT_HWND_DESKTOP): the first top-level window (GetTopWindow's page; V32).
 * Message-only windows are never listed among the top-level windows ("Window Features": a
 * message-only window has no Z order and cannot be enumerated; review V33).
 * cmd 6 (GW_ENABLEDPOPUP) on a record: NULL with last error untouched, always.  It asks for an
 * enabled VISIBLE popup that `h` owns, and no record is visible (win.c, test_GW_ENABLEDPOPUP:
 * no owned window, and a hidden owned window; review D3).
 * From memory (review S16, not settled): NULL with no last error set when there is no such
 * window for the six commands above; NULL with ERROR_INVALID_PARAMETER for a cmd of 7 or more. */
HWND rt_window_get(HWND h, UINT cmd);
HWND rt_window_top(HWND parent);

/* GetWindowThreadProcessId: the owner thread's guest id; *pid, if not NULL, receives the
 * process id of the process block (rt/thread.h).  0 with ERROR_INVALID_WINDOW_HANDLE (0 is
 * documented; the code is from memory, review S35), and *pid is not written.
 * STOPS: RT_HWND_DESKTOP (which process owns the desktop is not decided). */
DWORD rt_window_thread(HWND h, DWORD *pid);

/* FindWindowA(lpClassName, lpWindowName): the first top-level window of this process, in Z
 * order, whose class matches `cls` (ASCII case ignored; an atom allowed; NULL matches all)
 * and whose title equals `title` exactly (NULL matches all; "" matches a window with an
 * empty title or none, and no other; the title is read from the record, no WM_GETTEXT is
 * sent: Wine user32/tests/win.c, test_FindWindow; review V21).  No other process has windows:
 * this machine runs nothing else, so a search for another program's window finds NULL.
 * NULL leaves last error alone (documented on FindWindowA's page: "does not modify the last
 * error value").  Message-only windows are not searched: FROM MEMORY, and doubtful (review
 * S17: FindWindowExA(NULL, NULL, ...) does find them on Windows; FindWindowA was not tested
 * on them). */
HWND rt_window_find(RtText cls, RtText title);

/* EnumWindows: copy the handles of the top-level windows (not message-only), in Z order,
 * into out[0..cap) and return how many there are in all.  The shim calls the guest callback
 * for each with no lock held and stops at the first that returns 0. */
unsigned rt_window_list_top(HWND *out, unsigned cap);

/* GetDlgItem(hDlg, id): the child of `dlg` whose identifier is `id`; of several with that
 * identifier the first in Z order; the children's own children are not searched (Wine
 * user32/tests/dialog.c, test_GetDlgItem).  NULL with ERROR_CONTROL_ID_NOT_FOUND (from
 * memory: the page names no code and Wine's dialog tests do not assert one; reviews N10,
 * S8), or ERROR_INVALID_WINDOW_HANDLE for `dlg` (from memory). */
HWND rt_window_child_by_id(HWND dlg, INT id);

/* ---- show, paint state, focus, capture (core/win_state.c) [not built: win-state] --------------- */

/* ShowWindow(h, cmd) for a top-level window of the calling thread.  Returns nonzero if the
 * window was shown before the call and 0 if it was hidden (ShowWindow's page; msg.c,
 * test_ShowWindow; seq 2.1).  A command that changes nothing sends nothing (seq 2.2).
 * SW_SHOWNORMAL (1), SW_SHOW (5), SW_SHOWDEFAULT (10) on a HIDDEN window (seq S6, which is
 * S5's steps 1 to 8; msg.c, WmShowPopupFirstDrawSeq_1, WmShowNormal; seq 2.3, 2.6, 2.7):
 *   1. WM_SHOWWINDOW, wParam 1, lParam 0.
 *   2. WM_WINDOWPOSCHANGING, flags SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE; the record then
 *      has WS_VISIBLE and is first in the Z order.
 *   3. WM_ACTIVATEAPP, wParam 1, lParam 0 (no thread was active before).
 *   4. WM_NCACTIVATE, wParam 1; the window is now the active and the foreground window.
 *   5. WM_ACTIVATE, wParam WA_ACTIVE, lParam 0; its default processing gives the window the
 *      focus (WM_SETFOCUS, wParam 0).  A procedure that does not pass WM_ACTIVATE on leaves
 *      the focus where it was (what Windows does then was not read).
 *   6. WM_WINDOWPOSCHANGED, flags SWP_SHOWWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOCLIENTSIZE
 *      | SWP_NOCLIENTMOVE: so no WM_SIZE and no WM_MOVE follow (a popup had them at creation).
 * The optional messages of the tables are not sent: the second WM_WINDOWPOSCHANGING of the
 * activation, WM_QUERYNEWPALETTE, the input method's, and every paint message (WM_NCPAINT,
 * WM_ERASEBKGND, WM_PAINT): there is no screen, and a record has no update region.
 * SW_SHOWDEFAULT is SW_SHOWNORMAL: this process was started without STARTF_USESHOWWINDOW
 * (win.c, test_showwindow_proc_modify_flags; seq 2.8), so no command is ever replaced by a
 * start-up one (seq 2.9 has the rule for a process that was).
 * The same three commands on a window that is shown: nonzero, nothing sent (seq S7).
 * SW_HIDE (0) on a shown window (seq S8, seq 2.11; msg.c, WmHideOverlappedSeq): WM_SHOWWINDOW
 * (0, 0); WM_WINDOWPOSCHANGING with SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE; the record
 * loses WS_VISIBLE; WM_WINDOWPOSCHANGED with those and both no-client flags; then, if it was
 * the active window, WM_NCACTIVATE (0), WM_ACTIVATE (WA_INACTIVE), WM_ACTIVATEAPP (0) and,
 * if it had the focus, WM_KILLFOCUS (0); no window is active or focused afterwards.  Nonzero.
 * SW_HIDE on a hidden window: 0, nothing sent, from any thread (seq S9; review V34).
 * An invalid handle: FALSE with ERROR_INVALID_WINDOW_HANDLE (from memory: review S18).  Last
 * error is otherwise untouched.
 * STOPS: every other command (minimize, maximize, restore, the no-activate shows), naming
 * the class and the creator; a child or a message-only window; a window of another thread;
 * an activation while another window is active, and a hide of the active window while
 * another top-level window is shown (which window Windows activates is not settled: seq
 * 4.9, 2.12); a procedure that answers WM_NCACTIVATE (0) with FALSE. */
BOOL rt_window_show(HWND h, INT cmd);

/* UpdateWindow, InvalidateRect, ValidateRect on a record: TRUE and nothing happens, because
 * a window that was never shown has no update region and gets no WM_PAINT (UpdateWindow's
 * page: nothing is sent when the region is empty; Wine user32/tests/msg.c,
 * test_paint_messages; review V35).  h NULL: rt_window_update FALSE with
 * ERROR_INVALID_WINDOW_HANDLE and rt_window_invalidate (all windows) TRUE (both V35);
 * rt_window_validate TRUE (from memory: review S19, its result on Windows is not settled).
 * A stale or foreign handle: FALSE with ERROR_INVALID_WINDOW_HANDLE in all three (from
 * memory, S19). */
BOOL rt_window_update(HWND h);
BOOL rt_window_invalidate(HWND h, const RECT *rc, BOOL erase);
BOOL rt_window_validate(HWND h, const RECT *rc);

/* SetFocus, SetActiveWindow, SetForegroundWindow.  h NULL:
 *   rt_window_set_focus        returns the previous focus (NULL) and leaves no window
 *                              focused; last error untouched (SetFocus's page; Wine
 *                              user32/tests/msg.c, test_SetFocus; review V36)
 *   rt_window_set_active       NULL, last error UNTOUCHED (Windows 10 and later; older ones
 *                              returned the previous window: Wine user32/tests/win.c,
 *                              test_SetActiveWindow_0_proc; review D4)
 *   rt_window_set_foreground   FALSE with ERROR_INVALID_WINDOW_HANDLE (win.c,
 *                              test_SetForegroundWindow; V36)
 * A handle that is not NULL and not a window: NULL / FALSE with ERROR_INVALID_WINDOW_HANDLE
 * (SetFocus and SetActiveWindow: msg.c, test_SetFocus, V36; SetForegroundWindow: from memory).
 * STOPS: a valid record ("activation needs a visible window"): Gate 4. */
HWND rt_window_set_focus(HWND h);
HWND rt_window_set_active(HWND h);
BOOL rt_window_set_foreground(HWND h);

/* SetCapture(h): `h` (a record of the calling thread) captures the mouse, hidden as every
 * record is; returns the window that had it, or NULL.  ReleaseCapture: TRUE and the capture
 * is cleared, TRUE also when nothing was captured (SetCapture's page; Wine
 * user32/tests/win.c, test_capture_2 and test_capture_3; review V37).  No mouse message ever
 * arrives, so the state is only stored.  Capturing again with the window that holds the
 * capture returns that window and changes nothing.  STOPS: a capture change while another
 * window holds it (WM_CAPTURECHANGED would have to be sent); `h` NULL or not a window.
 *
 * A WINDOW OF ANOTHER THREAD (unit gate3-run-6, 11 October; a stop until then): NULL, the
 * capture is not changed, last error ERROR_ACCESS_DENIED.  The game does this: its main
 * thread calls SetCapture for the window that the device's thread made (inputsystem+0x85dd),
 * and the function returns without looking at the result.  Where the answer comes from:
 *   - the recording of the original under Proton (docs/trace/window-calls.md, section 3): the
 *     same call from the same site, five times, each answered NULL, and the game ran on;
 *   - Microsoft's page (read 11 October): the function "sets the mouse capture to the
 *     specified window belonging to the current thread", its parameter is "a handle to the
 *     window in the current thread"; it says nothing of a failure or of the last error;
 *   - Wine's server (server/queue.c, read 11 October, source not a conformance test):
 *     check_queue_input_window refuses a window whose thread has another input queue than
 *     the caller's with STATUS_ACCESS_DENIED, and the reply's previous window is 0.
 * So NULL and "not changed" are what the recorded run got.  The last error is Wine's status
 * as its client side maps it [from memory]; what Windows sets is NOT verified, and nothing
 * the game does at this call reads it.  Threads that share an input queue
 * (AttachThreadInput) may capture for each other on Windows and on Wine: not built, the
 * game's recording has no such call. */
HWND rt_window_set_capture(HWND h);
BOOL rt_window_release_capture(void);

/* ---- the message queue (core/win_queue.c) [built] --------------------------------------------- */
/* A guest thread gets its queue at its first call here, or when another thread first posts
 * to a window of it.  With no screen NOTHING POSTS BY ITSELF: there is no input, no paint and
 * no timer message.  A queue holds only what guest code posts (rt_msg_post, from any thread),
 * a posted WM_QUIT among it.
 *
 * Settled by the unit that built the piece (rt-win-queue), with the source of each:
 *   - The queue of a thread is found by the thread's id; no slot of RtThread.sub is used, and
 *     a queue is not freed when its thread ends (this runtime's choice; ids are never reused).
 *   - "The thread looked at its queue" is every call of rt_msg_peek or rt_msg_get that does
 *     not fail for its window filter, whatever its filters and whether or not it took a
 *     message: from then on what the queue holds is old, to rt_msg_wait and to rt_msg_get's
 *     own sleep (MsgWaitForMultipleObjects's page, return value WAIT_OBJECT_0 + nCount and
 *     Remarks: "the input is no longer considered new"; review V8).
 *   - A posted WM_QUIT is a message in the order posted, like any other, and passes the window
 *     filter like any other (Wine user32/tests/msg.c, test_quit_message: from memory).  There
 *     is no PostQuitMessage: no boot-path DLL imports it.
 *   - When a window is dropped, the messages posted to it leave the queue with it
 *     (DestroyWindow's page: from memory).
 *   - `unicode` changes nothing: a message comes out as it was posted.  The one case where
 *     Windows would convert, a character message with a character of 0x80 or above posted
 *     through PostMessageA, is a stop of rt_msg_post. */

/* PeekMessageA/W: the oldest posted message that passes the filter (min == max == 0: every
 * message), copied to *out member by member (the padding of a MSG is not written; whether
 * Windows writes it is not verified); removed if flags has PM_REMOVE.  The filter `h`:
 *   NULL                   any window of the thread, and thread messages
 *   (HWND)-1               thread messages only: those posted with hwnd NULL (PeekMessageA's
 *                          page, parameter hWnd; Wine user32/tests/msg.c, test_PostMessage)
 *   a window of the thread its messages and those of the windows below it (PeekMessageW's
 *                          page, Remarks: "or any of its children as specified by the IsChild
 *                          function")
 *   anything else          FALSE with ERROR_INVALID_WINDOW_HANDLE, *out untouched (msg.c,
 *                          test_invalid_window: 0xdeadbeef gives FALSE and 1400; review D5)
 * FALSE, *out untouched, last error untouched, when there is no such message: so on an empty
 * queue it returns FALSE at once, every time.  MSG.time is rt_time_tick_ms at the post,
 * MSG.pt the cursor position then (MSG's page; review V38).  STOPS: out NULL; a flag other
 * than PM_REMOVE and PM_NOYIELD; h 1 or 0xFFFF (Windows reads them as -1, test_PostMessage;
 * not built); h a window of another thread, RT_HWND_DESKTOP included (the page says the
 * window "must belong to the current thread"; what Windows answers is not verified); min or
 * max above 0xFFFF (the page: the high word is reserved for the system); min above max; a
 * posted WM_QUIT that the window filter lets through and the range leaves out, when no
 * older message passes (the page: "PeekMessage always retrieves WM_QUIT messages, no matter
 * which values you specify"; whether that holds for a posted one, and in which order, is
 * not verified). */
BOOL rt_msg_peek(Ctx *ctx, MSG *out, HWND h, UINT min, UINT max, UINT flags, bool unicode);

/* GetMessageA: as rt_msg_peek with PM_REMOVE, stops included, but waits (rt_wait on the
 * queue's object, rt/handle.h, not alertable) until a message is there: it sleeps until the
 * next post and looks again.  Returns 0 for WM_QUIT, 1 otherwise, -1 with
 * ERROR_INVALID_WINDOW_HANDLE for an `h` that is not a window.  On a queue nobody posts to
 * it waits for ever, as on Windows: the first time a thread is about to sleep here it logs
 * one line, channel `window`, level WARN: `GetMessage: thread 0x<id> waits on an empty
 * queue` (once for each thread, not for each call). */
INT rt_msg_get(Ctx *ctx, MSG *out, HWND h, UINT min, UINT max, bool unicode);

/* PostMessageA(h, ...): append to the queue of the window's thread, or of the calling thread
 * when h is NULL (a thread message: Wine user32/tests/msg.c, test_PostMessage; review V38),
 * and wake a thread that waits on it.  FALSE with ERROR_INVALID_WINDOW_HANDLE (the code:
 * from memory, review S20).  STOPS: HWND_BROADCAST; (HWND)-1 (Wine reads it as
 * HWND_BROADCAST: from memory); RT_HWND_DESKTOP; a queue of 10,000 messages (the Windows
 * limit: PostMessage's page, "at most 10,000 messages").
 * WHICH MESSAGES.  Windows refuses to post a system message that carries a pointer
 * (ERROR_MESSAGE_SYNC_ONLY: msg.c, test_WM_DEVICECHANGE; review V38), and which those are is
 * a table this runtime does not have.  So: 0x0400 to 0xFFFF (class, application and
 * registered messages) are posted as they are; below that only WM_NULL, WM_CLOSE, WM_QUIT,
 * WM_COMMAND (0x111), WM_SYSCOMMAND (0x112), the keyboard messages 0x100 to 0x107 and the
 * mouse messages 0x200 to 0x20E (that these carry no pointer: from memory).  STOPS: every
 * other message below 0x0400; a message above 0xFFFF; a character message (0x102, 0x103,
 * 0x106, 0x107) whose wParam is 0x80 or above (the A to W conversion is not built). */
BOOL rt_msg_post(Ctx *ctx, HWND h, UINT msg, WPARAM w, LPARAM l);

/* MsgWaitForMultipleObjects(n, handles, wait_all, ms, wake_mask): rt_wait (rt/handle.h) on
 * the n objects plus, as one more, the calling thread's queue object, which is signalled
 * while the queue holds a message that arrived since the thread last looked at its queue
 * (above).  Not alertable.  Returns WAIT_OBJECT_0 + i for handle i (a signalled handle comes
 * before the queue), WAIT_OBJECT_0 + n for the queue, WAIT_ABANDONED_0 + i, WAIT_TIMEOUT,
 * WAIT_FAILED with ERROR_INVALID_HANDLE for a handle that is not open or cannot be waited
 * on.  n may be 0: the queue alone, with either value of wait_all
 * (MsgWaitForMultipleObjects's page; msg.c, test_WM_DEVICECHANGE; review V39).
 * So when nobody posts it is WaitForMultipleObjects with the same time limit.
 * Only posted messages wake it: wake_mask must contain QS_POSTMESSAGE (QS_ALLINPUT and
 * QS_ALLEVENTS do).  STOPS: a wake_mask without it; wait_all TRUE with n > 0; n > 63;
 * `handles` NULL with n > 0; the same handle twice (the page: the array "may not contain
 * multiple copies of the same handle"; what Windows answers is not verified); and A SECOND
 * WAIT after one that returned for the queue, when the thread has not looked at its queue
 * between and the message is still new: the page names PeekMessage, GetMessage,
 * GetQueueStatus and WaitMessage as what makes input old and does not say whether the
 * wait's own return does (Wine's server does make it old: from memory). */
DWORD rt_msg_wait(Ctx *ctx, DWORD n, const HANDLE *handles, BOOL wait_all, DWORD ms, DWORD wake_mask);

/* GetMessageTime: MSG.time of the last message the calling thread took with rt_msg_get or
 * rt_msg_peek(PM_REMOVE); 0 if it has taken none (the 0 and the PeekMessage half: from
 * memory, review S21; the page names GetMessage only).  Cannot fail.  Fatal on a host thread
 * that is not a guest thread. */
LONG rt_msg_time(void);

/* ---- cursor and keyboard state (core/win_input.c) [not built: win-input] ----------------------- */
/* Nobody moves the mouse or presses a key before Gate 4: the state is what guest code set. */

/* GetCursorPos / SetCursorPos.  The position starts at {960, 540}, the middle of the screen
 * (this contract's choice: Windows has no documented starting point).  Setting clamps to the
 * clip rectangle: left <= x <= right - 1 and top <= y <= bottom - 1 (a RECT's right and
 * bottom edges lie outside it: SetCursorPos's page; Wine user32/tests/input.c,
 * test_SetCursorPos; review V42).  Always TRUE, except rt_cursor_get_pos with `out` NULL:
 * FALSE with ERROR_NOACCESS (998; the same test; review D10). */
BOOL rt_cursor_get_pos(POINT *out);
BOOL rt_cursor_set_pos(INT x, INT y);

/* ClipCursor(rc) / GetClipCursor.  The clip starts as, and rc NULL restores, the whole
 * screen (rt_desktop()->monitor); a rectangle is intersected with the screen.  A new clip
 * moves a cursor that lies outside it to the nearest point inside (by the rule of
 * rt_cursor_set_pos).  TRUE.  (GetClipCursor's page; Wine user32/tests/input.c,
 * test_ClipCursor_desktop; monitor.c, test_ChangeDisplaySettingsEx; review V42.)
 * Looked at first: an INVERTED rectangle, right < left or bottom < top as given, is refused:
 * FALSE with ERROR_ACCESS_DENIED (5), and the clip and the cursor stay as they were
 * (input.c, test_ClipCursor_desktop; review D11).
 * STOPS: a rectangle that is empty and not inverted, left == right or top == bottom (Windows
 * accepts it, D11; which clip it then reports was not read); a rectangle with no point on the
 * screen (empty after the intersection; what Windows does is not verified); `out` NULL in
 * rt_cursor_get_clip. */
BOOL rt_cursor_clip(const RECT *rc);
BOOL rt_cursor_get_clip(RECT *out);

/* ShowCursor(bShow): the display counter, +1 for TRUE and -1 for FALSE, returned after the
 * change.  It starts at 0, the documented start on a machine with a mouse (ShowCursor's page;
 * -1 without one); that this machine
 * has one is this contract's choice (Gate 4 gives it a real one). */
INT rt_cursor_show(BOOL show);

/* SetCursor(h): make `h` the current cursor and return the previous one (SetCursor's page;
 * Wine user32/tests/cursoricon.c, test_SetCursor; review V43).  The current cursor is NULL at
 * start (from memory: review S31; with no window Windows may have the arrow).
 * `h` is NULL or a system cursor (RT_HCURSOR_SYSTEM of an IDC_ value in rt/win/user.h).
 * STOPS: any other value (no other cursor can exist here: every function that makes one
 * stops.  Windows answers NULL with ERROR_INVALID_CURSOR_HANDLE for a handle that is no
 * cursor, the same test). */
HCURSOR rt_cursor_set(HCURSOR h);

/* GetKeyState(vk): the state of key `vk` as the calling thread's taken messages left it:
 * bit 15 down, bit 0 the toggle (docs/handoff/rt-contract-input.md C35, K17 to K19).  0 for
 * every key unless an input script is loaded (rt/input.h).  One table for the process.
 * STOPS: vk outside 0-255 (what Windows answers is not settled, review S30). */
SHORT rt_key_state(INT vk);

/* ---- the US keyboard layout (core/win_keymap.c; unit rt-input-keys) --------------------------------
 * The contract is docs/handoff/rt-contract-input.md, rows K1 to K40, with the table of the keys
 * this runtime has: the 26 letters, the 10 digits, the 11 punctuation keys, Backspace, Tab,
 * Enter, Esc, Space; without a character Shift, Control, Caps Lock, F1 to F9, F11, F12, the
 * arrows, Insert, Delete, Home, End, Page Up, Page Down; ALT and F10 as system keys.  ANY
 * OTHER KEY IS A STOP in every function below (K33). */

/* MapVirtualKeyExA.  For layout RT_HKL_US (or NULL): map_type 0 a virtual key to its scan
 * code (the left one of two; an extended key's low byte; VK_LSHIFT to VK_RMENU their own);
 * 1 a scan code to its virtual key; 3 the same with left and right told apart (0x2A VK_LSHIFT,
 * 0x36 VK_RSHIFT, 0x1D VK_LCONTROL, 0x38 VK_LMENU); 2 a virtual key to its unshifted
 * character, a letter as its capital, 0 for a key without a character (K34, K35).
 * STOPS: another layout; map_type 4 and above; a key or scan code the table does not have; a
 * scan code above 0xFF or of the numeric keypad block 0x47 to 0x53 (K37). */
UINT rt_key_map(UINT code, UINT map_type, HKL layout);

/* The character TranslateMessage posts for a WM_KEYDOWN of `vk` with Shift or Control down and
 * Caps Lock toggled (K24 to K32): 0 to 0x7F (0 is a character: Control+Shift+2), or
 * RT_KEY_NO_CHAR for a key or a combination that posts nothing.
 * STOPS: a key the table does not have, VK_PACKET, VK_PROCESSKEY, a system key (K7, K16,
 * K33); Shift+Enter without Control (K27); Control+Esc (K31). */
#define RT_KEY_NO_CHAR  (-1)
int rt_key_char(UINT vk, bool shift, bool control, bool caps);

/* The table's scan code and extended-key flag of `vk` (the left key of two); false for a key
 * the table does not have.  For the input script's `key down|up VK` (K38). */
bool rt_key_scan(UINT vk, UINT *scan, bool *ext);

/* TranslateMessage(m) (K9 to K15).  TRUE for WM_KEYDOWN and WM_KEYUP, FALSE for a message
 * that is not a key message; `m` is only read.  A WM_KEYDOWN whose key has a character in the
 * calling thread's key state (rt_key_state: Shift, Control, the toggle of Caps Lock) posts
 * WM_CHAR with that character and the key-down's lParam to m->hwnd, as rt_msg_post does:
 * behind the posted messages that wait, in front of waiting input.  The last error is not
 * changed.  STOPS: NULL; WM_SYSKEYDOWN and WM_SYSKEYUP; a WM_KEYDOWN with wParam above 0xFF
 * or while ALT is down in the key state; and rt_key_char's. */
BOOL rt_key_translate(Ctx *ctx, const MSG *m);

/* ---- hooks (core/win_hook.c) [not built: win-hook] --------------------------------------------- */

/* SetWindowsHookExA(idHook, lpfn, hmod, dwThreadId): a hook record; its handle.  Only the
 * four types whose events are real input are accepted: WH_KEYBOARD, WH_MOUSE,
 * WH_KEYBOARD_LL, WH_MOUSE_LL.  No input exists before Gate 4, so the procedure is never
 * called.  NULL with last error, looked at in this order:
 *   ERROR_INVALID_HOOK_FILTER   a type outside -1 to 14 (the range: every type in it installs,
 *                               Wine user32/tests/msg.c, test_set_hook, review V44; the code
 *                               for one outside it: from memory, review S29)
 *   ERROR_INVALID_FILTER_PROC   (1427) lpfn 0, whatever the type
 *   ERROR_GLOBAL_ONLY_HOOK      (1429) WH_KEYBOARD_LL or WH_MOUSE_LL with dwThreadId not 0
 *   ERROR_HOOK_NEEDS_HMOD       (1428) any type but the two _LL types with dwThreadId 0 (a
 *                               global hook) and hmod NULL (asserted for WH_CBT)
 * The last three: Wine user32/tests/msg.c, test_set_hook (docs/reviews/contracts-check.md C5
 * and docs/reviews/win-behaviour-check-5.md Z2, which read the same checks); the order among
 * them is from memory.  Accepted, then: WH_KEYBOARD_LL and WH_MOUSE_LL with dwThreadId 0, hmod
 * NULL or not; WH_KEYBOARD and WH_MOUSE for the calling thread (hmod NULL or not: a thread
 * hook may have a module or none, Z2), or globally with a module.
 * STOPS: every other valid type that passes the four checks (their events are things this core itself
 * does: creating a window, sending a message, taking one from a queue; calling hooks there
 * is not built); WH_KEYBOARD or WH_MOUSE with a dwThreadId that is neither 0 nor the calling
 * thread's (a hook on another thread: not built, and what Windows answers for an id that is
 * no thread is not verified).
 * UnhookWindowsHookEx: TRUE and the record is gone; FALSE with ERROR_INVALID_HOOK_HANDLE. */
HHOOK rt_hook_set(INT type, HOOKPROC fn, HINSTANCE mod, DWORD thread_id);
BOOL  rt_hook_remove(HHOOK h);

/* ---- the clipboard (core/win_clipboard.c) [not built: win-clipboard] --------------------------- */
/* The clipboard of this process alone: no other program exists to put anything on it.  It
 * starts empty.  Gate 4 joins it to the host's pasteboard.
 * Checked: Wine user32/tests/clipboard.c (test_ClipboardOwner, clipboard_thread,
 * test_RegisterClipboardFormatA, set_clipboard_data_thread, test_handles, test_synthesized;
 * reviews V45, D19, D20, D21).  "The thread that has it open" is the host thread
 * (pthread_self), so the rules hold for any thread that calls.
 *   rt_clipboard_open(owner)   `owner` is NULL or a window, else FALSE with
 *                              ERROR_INVALID_WINDOW_HANDLE (looked at first; V45).  Then:
 *                              - closed: TRUE; it is now open with `owner`, by the caller.
 *                              - open by the calling thread: TRUE if `owner` is the value it
 *                                is open with (NULL included); FALSE with ERROR_ACCESS_DENIED
 *                                for any other owner, and it stays open as it was.  A second
 *                                open follows the OWNER, not the thread (D20: open with NULL,
 *                                then with a window fails, then with NULL succeeds).  Windows
 *                                gives that code or leaves last error alone, both seen; this
 *                                contract takes the code.  Opens are not counted: one close
 *                                closes.
 *                              - open by another thread: FALSE with ERROR_ACCESS_DENIED
 *                                (FALSE for a different owner: D20; for two NULL owners, and
 *                                the code: from memory, review S32).  STOPS: another thread
 *                                opens with the WINDOW it is open with (Windows: TRUE, D20;
 *                                who may then close is not built).
 *   rt_clipboard_close()       TRUE; FALSE with ERROR_CLIPBOARD_NOT_OPEN if the caller has not.
 *                              The formats that were set stay until rt_clipboard_empty.
 *   The three below fail the same way, FALSE or NULL with ERROR_CLIPBOARD_NOT_OPEN, when the
 *   calling thread does not have the clipboard open (V45, asserted with the clipboard closed;
 *   while another thread has it open: from memory).
 *   rt_clipboard_empty()       TRUE and no format is left; FALSE with ERROR_CLIPBOARD_NOT_OPEN.
 *                              The handles that were set are forgotten, not freed (the guest's
 *                              memory handles are its own in this runtime).
 *   rt_clipboard_set(f, data)  remember `data` (a memory handle, kept as a value and never
 *                              read) for format f and return it (V45); NULL with
 *                              ERROR_CLIPBOARD_NOT_OPEN.  Format 0 is refused: NULL with
 *                              ERROR_CLIPBOARD_NOT_OPEN (1418) ALTHOUGH the clipboard is open,
 *                              looked at before `data`; every other number is accepted,
 *                              registered or not (D21).  Setting a format that is set replaces
 *                              its handle (Windows also frees the old one; here it is
 *                              forgotten).  STOPS: data NULL (Windows: delayed rendering, V45);
 *                              a 17th format.
 *   rt_clipboard_get(f)        the handle set for f (V45); NULL with ERROR_NOT_FOUND (1168) if
 *                              none is set (Windows 11; older versions left last error alone;
 *                              D19); NULL with ERROR_CLIPBOARD_NOT_OPEN.  STOPS: f is CF_TEXT,
 *                              CF_OEMTEXT or CF_UNICODETEXT, f is not set and another of the
 *                              three is (Windows converts: test_synthesized, V45). */
BOOL   rt_clipboard_open(HWND owner);
BOOL   rt_clipboard_close(void);
BOOL   rt_clipboard_empty(void);
HANDLE rt_clipboard_set(UINT format, HANDLE data);
HANDLE rt_clipboard_get(UINT format);

/* ---- the host window (core/win_host.c) ------------------------------------------------------- */
/* THE HOST WINDOW.  RT_HOST_WINDOW=appkit gives the game's top-level window a real macOS
 * window; appkit-hidden the same, never shown (tests); unset or none: no host window, and
 * nothing of AppKit is touched. */
bool  rt_host_window_wanted(void);
/* The host's layer (a CAMetalLayer *) for a top-level window, or NULL when no host window
 * is wanted.  The first call for a window opens its host window and attaches the host as
 * an input source.  Any guest thread; NEVER with a lock of the window core held (it waits
 * for the process's first thread).  One window per process: a second window gets NULL
 * and one warning line. */
void *rt_window_host_layer(HWND h);
/* true for the window that has the host window (false for every window when none is open). */
bool  rt_window_is_host_window(HWND h);
/* true once a host window is open (after the first rt_window_host_layer that opened one). */
bool  rt_window_host_open(void);

#endif /* RT_WINDOW_H */
