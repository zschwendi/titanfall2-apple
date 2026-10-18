/* user32.dll and imm32.dll: the structures and constants the window contract uses
 * (docs/handoff/rt-contract-window.md).  The core that answers is rt/window.h.
 *
 * Layouts are the x64 ones of the Windows SDK; every structure a guest can pass has its size
 * asserted below, and each size was counted against the member list of Wine's include/winuser.h
 * and imm.h.  Constants: each value equals Wine's include/winuser.h, winuser.rh or winerror.h
 * (docs/reviews/contracts-check.md); QS_ALLINPUT and QS_ALLEVENTS are the Windows 7 SDK's
 * (no QS_TOUCH, QS_POINTER).
 */
#ifndef RT_WIN_USER_H
#define RT_WIN_USER_H

#include "rt/wintypes.h"
#include "rt/win/base.h"

typedef void   *HWINSTA;
typedef GUESTFN MONITORENUMPROC;       /* BOOL (HMONITOR, HDC, LPRECT, LPARAM) */

/* ---- classes and windows ---------------------------------------------------------------------- */

typedef struct { UINT style; WNDPROC lpfnWndProc; INT cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon;
                 HCURSOR hCursor; HBRUSH hbrBackground; LPCSTR lpszMenuName, lpszClassName; } WNDCLASSA;
typedef struct { UINT style; WNDPROC lpfnWndProc; INT cbClsExtra, cbWndExtra; HINSTANCE hInstance; HICON hIcon;
                 HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName; } WNDCLASSW;
typedef struct { UINT cbSize, style; WNDPROC lpfnWndProc; INT cbClsExtra, cbWndExtra; HINSTANCE hInstance;
                 HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCSTR lpszMenuName, lpszClassName;
                 HICON hIconSm; } WNDCLASSEXA;
typedef struct { UINT cbSize, style; WNDPROC lpfnWndProc; INT cbClsExtra, cbWndExtra; HINSTANCE hInstance;
                 HICON hIcon; HCURSOR hCursor; HBRUSH hbrBackground; LPCWSTR lpszMenuName, lpszClassName;
                 HICON hIconSm; } WNDCLASSEXW;
/* What WM_NCCREATE and WM_CREATE carry in lParam.  The A and W forms differ only in the
 * character type of the two strings. */
typedef struct { LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; INT cy, cx, y, x;
                 LONG style; LPCSTR lpszName, lpszClass; DWORD dwExStyle; } CREATESTRUCTA;
typedef struct { LPVOID lpCreateParams; HINSTANCE hInstance; HMENU hMenu; HWND hwndParent; INT cy, cx, y, x;
                 LONG style; LPCWSTR lpszName, lpszClass; DWORD dwExStyle; } CREATESTRUCTW;
/* What WM_WINDOWPOSCHANGING and WM_WINDOWPOSCHANGED carry in lParam (WINDOWPOS's page), what
 * WM_NCCALCSIZE carries when wParam is TRUE (NCCALCSIZE_PARAMS's page: the proposed window
 * rectangle, the window rectangle before, the client rectangle before), and WM_GETMINMAXINFO's
 * five points.  Layouts from memory of winuser.h; the sizes are asserted below. */
typedef struct { HWND hwnd, hwndInsertAfter; INT x, y, cx, cy; UINT flags; } WINDOWPOS;
typedef struct { RECT rgrc[3]; WINDOWPOS *lppos; } NCCALCSIZE_PARAMS;
typedef struct { POINT ptReserved, ptMaxSize, ptMaxPosition, ptMinTrackSize, ptMaxTrackSize; } MINMAXINFO;
typedef struct { HWND hwnd; UINT message; WPARAM wParam; LPARAM lParam; DWORD time; POINT pt; } MSG;
typedef MSG *PMSG, *LPMSG;
typedef struct { BOOL fInherit, fReserved; DWORD dwFlags; } USEROBJECTFLAGS;
typedef struct { HANDLE hDevice; DWORD dwType; } RAWINPUTDEVICELIST;
typedef RAWINPUTDEVICELIST *PRAWINPUTDEVICELIST;
typedef struct { DWORD dwIndex, dwStyle; POINT ptCurrentPos; RECT rcArea; } CANDIDATEFORM;   /* imm32 */
typedef CANDIDATEFORM *LPCANDIDATEFORM;

_Static_assert(sizeof(WNDCLASSA) == 72 && sizeof(WNDCLASSW) == 72, "WNDCLASS");
_Static_assert(sizeof(WNDCLASSEXA) == 80 && sizeof(WNDCLASSEXW) == 80, "WNDCLASSEX");
_Static_assert(sizeof(CREATESTRUCTA) == 80 && sizeof(CREATESTRUCTW) == 80, "CREATESTRUCT");
_Static_assert(sizeof(WINDOWPOS) == 40 && sizeof(NCCALCSIZE_PARAMS) == 56 && sizeof(MINMAXINFO) == 40, "WINDOWPOS");
_Static_assert(sizeof(MSG) == 48 && __builtin_offsetof(MSG, time) == 32 && __builtin_offsetof(MSG, pt) == 36, "MSG");
_Static_assert(sizeof(USEROBJECTFLAGS) == 12 && sizeof(RAWINPUTDEVICELIST) == 16 && sizeof(CANDIDATEFORM) == 32,
               "small user32 and imm32 structures");

/* Window styles (CreateWindowEx dwStyle) and extended styles. */
#define WS_OVERLAPPED        0x00000000u
#define WS_POPUP             0x80000000u
#define WS_CHILD             0x40000000u
#define WS_MINIMIZE          0x20000000u
#define WS_VISIBLE           0x10000000u
#define WS_DISABLED          0x08000000u
#define WS_CLIPSIBLINGS      0x04000000u
#define WS_CLIPCHILDREN      0x02000000u
#define WS_MAXIMIZE          0x01000000u
#define WS_CAPTION           0x00C00000u     /* WS_BORDER | WS_DLGFRAME */
#define WS_BORDER            0x00800000u
#define WS_DLGFRAME          0x00400000u
#define WS_THICKFRAME        0x00040000u
#define WS_SYSMENU           0x00080000u
#define WS_EX_DLGMODALFRAME  0x00000001u
#define WS_EX_NOPARENTNOTIFY 0x00000004u    /* from memory: added after the review's comparison (V1) */
#define WS_EX_TOPMOST        0x00000008u
#define WS_EX_TOOLWINDOW     0x00000080u
#define WS_EX_WINDOWEDGE     0x00000100u
#define WS_EX_CLIENTEDGE     0x00000200u
#define WS_EX_STATICEDGE     0x00020000u
#define CW_USEDEFAULT        ((INT)0x80000000)

/* Values that stand where a window handle is expected. */
#define HWND_BROADCAST       ((HWND)(uintptr_t)0xFFFF)
#define HWND_MESSAGE         ((HWND)(intptr_t)-3)       /* as a parent: a message-only window */
#define HWND_TOP             ((HWND)(uintptr_t)0)
#define HWND_BOTTOM          ((HWND)(uintptr_t)1)
#define HWND_TOPMOST         ((HWND)(intptr_t)-1)
#define HWND_NOTOPMOST       ((HWND)(intptr_t)-2)

/* GetWindowLong / GetWindowLongPtr / GetClassLongPtr indexes. */
#define GWL_STYLE            (-16)
#define GWL_EXSTYLE          (-20)
#define GWL_ID               (-12)
#define GWLP_WNDPROC         (-4)
#define GWLP_HINSTANCE       (-6)
#define GWLP_HWNDPARENT      (-8)
#define GWLP_USERDATA        (-21)
#define GCLP_MENUNAME        (-8)
#define GCLP_HBRBACKGROUND   (-10)
#define GCLP_HCURSOR         (-12)
#define GCLP_HICON           (-14)
#define GCLP_HMODULE         (-16)
#define GCL_CBWNDEXTRA       (-18)
#define GCL_CBCLSEXTRA       (-20)
#define GCLP_WNDPROC         (-24)
#define GCL_STYLE            (-26)
#define GCW_ATOM             (-32)
#define GCLP_HICONSM         (-34)

/* GetWindow. */
#define GW_HWNDFIRST 0
#define GW_HWNDLAST  1
#define GW_HWNDNEXT  2
#define GW_HWNDPREV  3
#define GW_OWNER     4
#define GW_CHILD     5
#define GW_ENABLEDPOPUP 6

/* ShowWindow and SetWindowPos. */
#define SW_HIDE            0
#define SW_SHOWNORMAL      1
#define SW_SHOW            5
#define SW_SHOWDEFAULT     10
#define SWP_NOSIZE         0x0001u
#define SWP_NOMOVE         0x0002u
#define SWP_NOZORDER       0x0004u
#define SWP_NOACTIVATE     0x0010u
#define SWP_FRAMECHANGED   0x0020u
#define SWP_SHOWWINDOW     0x0040u
#define SWP_HIDEWINDOW     0x0080u
#define SWP_NOOWNERZORDER  0x0200u
#define SWP_NOSENDCHANGING 0x0400u
#define SWP_NOREDRAW       0x0008u
#define SWP_NOCOPYBITS     0x0100u
#define SWP_NOCLIENTSIZE   0x0800u     /* in a WINDOWPOS only: the client size did not change (from memory of Wine's winuser.h; */
#define SWP_NOCLIENTMOVE   0x1000u     /* not in the SDK headers; named by review D7)                                           */
#define SWP_DEFERERASE     0x2000u
#define SWP_ASYNCWINDOWPOS 0x4000u
#define SIZE_RESTORED      0
#define WA_INACTIVE        0
#define WA_ACTIVE          1

/* ---- messages --------------------------------------------------------------------------------- */

#define WM_NULL           0x0000
#define WM_CREATE         0x0001
#define WM_DESTROY        0x0002
#define WM_MOVE           0x0003
#define WM_SIZE           0x0005
#define WM_SETTEXT        0x000C
#define WM_GETTEXT        0x000D
#define WM_GETTEXTLENGTH  0x000E
#define WM_PAINT          0x000F
#define WM_CLOSE          0x0010
#define WM_QUIT           0x0012
#define WM_GETMINMAXINFO  0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED  0x0047
#define WM_NCCREATE       0x0081
#define WM_NCDESTROY      0x0082
#define WM_NCCALCSIZE     0x0083
#define WM_ACTIVATE       0x0006
#define WM_SETFOCUS       0x0007
#define WM_KILLFOCUS      0x0008
#define WM_SHOWWINDOW     0x0018
#define WM_ACTIVATEAPP    0x001C
#define WM_NCACTIVATE     0x0086
#define WM_KEYDOWN        0x0100
#define WM_KEYUP          0x0101
#define WM_CHAR           0x0102
#define WM_SYSKEYDOWN     0x0104
#define WM_SYSKEYUP       0x0105
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION   0x010E
#define WM_IME_COMPOSITION      0x010F
#define WM_TIMER          0x0113
#define WM_IME_SETCONTEXT       0x0281
#define WM_IME_NOTIFY           0x0282
#define WM_IME_CONTROL          0x0283
#define WM_IME_COMPOSITIONFULL  0x0284
#define WM_IME_SELECT           0x0285
#define WM_IME_CHAR             0x0286
#define WM_IME_SYSTEM           0x0287     /* not in the SDK headers: Wine imm32/tests/imm32.c, test_ImmIsUIMessage (review C7) */
#define WM_PARENTNOTIFY          0x0210     /* review C2 */
#define WM_USER           0x0400     /* 0x0400 to 0x7FFF: private to a window class */
#define WM_APP            0x8000     /* 0x8000 to 0xBFFF: private to an application */
#define RT_WM_REGISTERED_FIRST 0xC000   /* 0xC000 to 0xFFFF: RegisterWindowMessage */

#define PM_NOREMOVE       0x0000u
#define PM_REMOVE         0x0001u
#define PM_NOYIELD        0x0002u
#define QS_ALLINPUT       0x04FFu
#define QS_ALLEVENTS      0x04BFu
#define QS_POSTMESSAGE    0x0008u
#define QS_SENDMESSAGE    0x0040u

/* ---- the desktop ------------------------------------------------------------------------------- */

#define SM_CXSCREEN          0
#define SM_CYSCREEN          1
#define SM_CXICON            11
#define SM_CYICON            12
#define SM_CXCURSOR          13
#define SM_CYCURSOR          14
#define SM_SWAPBUTTON        23
#define SM_CXSMICON          49
#define SM_CYSMICON          50
#define SM_CLEANBOOT         67
#define SM_XVIRTUALSCREEN    76
#define SM_YVIRTUALSCREEN    77
#define SM_CXVIRTUALSCREEN   78
#define SM_CYVIRTUALSCREEN   79
#define SM_CMONITORS         80
#define SM_SAMEDISPLAYFORMAT 81
#define SM_REMOTESESSION     0x1000

#define SPI_GETWORKAREA      0x0030u
#define UOI_FLAGS            1
#define WSF_VISIBLE          0x0001u
#define DISP_CHANGE_SUCCESSFUL 0

/* ---- cursors, icons, keyboard, hooks, clipboard ------------------------------------------------ */

#define IDC_ARROW       32512      /* the integer inside MAKEINTRESOURCE */
#define IDC_IBEAM       32513
#define IDC_WAIT        32514
#define IDC_CROSS       32515
#define IDC_UPARROW     32516
#define IDC_SIZENWSE    32642
#define IDC_SIZENESW    32643
#define IDC_SIZEWE      32644
#define IDC_SIZENS      32645
#define IDC_SIZEALL     32646
#define IDC_NO          32648
#define IDC_HAND        32649
#define IDC_APPSTARTING 32650
#define IDC_HELP        32651
#define IDI_APPLICATION 32512
#define IDI_HAND        32513
#define IDI_QUESTION    32514
#define IDI_EXCLAMATION 32515
#define IDI_ASTERISK    32516
#define IDI_WINLOGO     32517
#define IDI_SHIELD      32518

#define WH_KEYBOARD     2
#define WH_MOUSE        7
#define WH_KEYBOARD_LL  13
#define WH_MOUSE_LL     14

#define CF_TEXT         1
#define CF_OEMTEXT      7
#define CF_UNICODETEXT  13
#define CF_LOCALE       16

#define RIM_TYPEMOUSE    0
#define RIM_TYPEKEYBOARD 1
#define RIM_TYPEHID      2

/* ---- message boxes ------------------------------------------------------------------------------ */

#define MB_OK              0x00000000u
#define MB_TYPEMASK        0x0000000Fu
#define MB_ICONMASK        0x000000F0u
#define MB_ICONERROR       0x00000010u
#define IDOK               1

/* ---- error codes of user32 (winerror.h; values as Wine's include/winerror.h) ------------------------------------------- */

#ifndef ERROR_INVALID_WINDOW_HANDLE
#define ERROR_INVALID_WINDOW_HANDLE    1400
#define ERROR_INVALID_CURSOR_HANDLE    1402
#define ERROR_INVALID_ACCEL_HANDLE     1403
#define ERROR_INVALID_HOOK_HANDLE      1404
#define ERROR_TLW_WITH_WSCHILD         1406
#define ERROR_CANNOT_FIND_WND_CLASS    1407
#define ERROR_CLASS_ALREADY_EXISTS     1410
#define ERROR_CLASS_DOES_NOT_EXIST     1411
#define ERROR_CLASS_HAS_WINDOWS        1412
#define ERROR_INVALID_INDEX            1413
#define ERROR_CLIPBOARD_NOT_OPEN       1418
#define ERROR_CONTROL_ID_NOT_FOUND     1421
#define ERROR_INVALID_HOOK_FILTER      1426
#define ERROR_INVALID_FILTER_PROC      1427
#define ERROR_HOOK_NEEDS_HMOD          1428
#define ERROR_GLOBAL_ONLY_HOOK         1429
#endif
/* Resource lookups (LoadCursor, LoadIcon).  Values from memory of winerror.h; that 1813 and 1814
 * are what FindResource reports for a missing type and a missing name is asserted by Wine's
 * kernel32/tests/resource.c (test_find_resource). */
#ifndef ERROR_RESOURCE_NAME_NOT_FOUND
#define ERROR_RESOURCE_TYPE_NOT_FOUND  1813
#define ERROR_RESOURCE_NAME_NOT_FOUND  1814
#endif

#endif /* RT_WIN_USER_H */
