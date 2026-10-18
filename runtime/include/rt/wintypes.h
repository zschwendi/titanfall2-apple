/* Windows type names with fixed widths (docs/RUNTIME.md section 3.3).
 *
 * arm64 macOS and x64 Windows agree on the layout of fixed-width integers, pointers, float
 * and double, so plain C declared with these names describes guest memory exactly.  Four C
 * types differ between the two platforms and are FORBIDDEN in any guest-visible type and in
 * any shim signature:  long, unsigned long (64-bit here, 32-bit on Windows), wchar_t (32-bit
 * here, 16-bit on Windows) and size_t (use SIZE_T).  The generator and `make shim-check`
 * reject them.
 *
 * Every name a SHIM line may use must also be in runtime/tools/types.def, which gives its
 * argument class.  The build checks that the two agree (build/gen/types_check.c).
 *
 * Never include a real Windows header, or an Objective-C or Cocoa header (their BOOL is a
 * different type), in a file that includes this one.
 */
#ifndef RT_WINTYPES_H
#define RT_WINTYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- integers ------------------------------------------------------------------------- */

typedef int32_t   BOOL, INT, LONG, INT32, LONG32;
typedef uint32_t  DWORD, UINT, ULONG, UINT32, ULONG32, DWORD32;
typedef int16_t   SHORT, INT16;
typedef uint16_t  WORD, USHORT, UINT16, ATOM, LANGID;
typedef char      CHAR;
typedef int8_t    INT8;
typedef uint8_t   BYTE, UCHAR, UINT8, BOOLEAN;
typedef uint16_t  WCHAR;                      /* one UTF-16 code unit; never wchar_t */

typedef int64_t   LONGLONG, LONG64, INT64, LONG_PTR, INT_PTR, SSIZE_T, LPARAM, LRESULT;
typedef uint64_t  ULONGLONG, ULONG64, UINT64, DWORD64, DWORDLONG,
                  ULONG_PTR, UINT_PTR, DWORD_PTR, SIZE_T, WPARAM;

typedef float     FLOAT;

typedef int32_t   HRESULT, NTSTATUS, LSTATUS;
typedef uint32_t  LCID, LCTYPE, COLORREF, MMRESULT, ACCESS_MASK;

/* ---- pointers -------------------------------------------------------------------------- */
/* Guest address equals host address, so a guest pointer argument is an ordinary C pointer. */

typedef void        *PVOID, *LPVOID;
typedef const void  *LPCVOID;
typedef CHAR        *PSTR, *LPSTR, *PCHAR;
typedef const CHAR  *PCSTR, *LPCSTR;
typedef WCHAR       *PWSTR, *LPWSTR, *PWCHAR;
typedef const WCHAR *PCWSTR, *LPCWSTR;
typedef BYTE        *PBYTE, *LPBYTE;
typedef BOOL        *PBOOL, *LPBOOL;
typedef WORD        *PWORD, *LPWORD;
typedef INT         *PINT, *LPINT;
typedef UINT        *PUINT;
typedef LONG        *PLONG, *LPLONG;
typedef ULONG       *PULONG;
typedef DWORD       *PDWORD, *LPDWORD;
typedef LONGLONG    *PLONGLONG;
typedef ULONGLONG   *PULONGLONG;
typedef SIZE_T      *PSIZE_T;
typedef ULONG_PTR   *PULONG_PTR;
typedef DWORD_PTR   *PDWORD_PTR;

/* ---- handles --------------------------------------------------------------------------- */
/* All handle types are void *.  Kernel handles are 32-bit values in a 64-bit slot (section
 * 4.1); an HMODULE is the module's image base (section 1.3). */

typedef void *HANDLE;
typedef HANDLE *PHANDLE, *LPHANDLE;
typedef void *HMODULE, *HINSTANCE, *HLOCAL, *HGLOBAL, *HRSRC;
typedef void *HKEY;
typedef HKEY *PHKEY;
typedef void *HWND, *HDC, *HMENU, *HICON, *HCURSOR, *HBRUSH, *HBITMAP, *HFONT, *HGDIOBJ,
             *HMONITOR, *HHOOK, *HKL, *HDROP, *HPALETTE, *HRGN, *HPEN, *HACCEL, *HIMC, *HGLRC;

/* ---- guest code addresses -------------------------------------------------------------- */
/* A GUESTFN is the guest address of code.  C cannot call it: use GUEST_CALL (rt/shim.h). */

typedef uint64_t GUESTFN;
typedef GUESTFN  FARPROC, PROC, WNDPROC, DLGPROC, HOOKPROC, TIMERPROC, WNDENUMPROC,
                 LPTHREAD_START_ROUTINE, PTHREAD_START_ROUTINE, PHANDLER_ROUTINE,
                 LPTOP_LEVEL_EXCEPTION_FILTER, PAPCFUNC, PFLS_CALLBACK_FUNCTION,
                 LPOVERLAPPED_COMPLETION_ROUTINE, LOCALE_ENUMPROCW, LOCALE_ENUMPROCA;

/* ---- constants every shim uses ---------------------------------------------------------- */

#ifndef TRUE
#define TRUE  1
#endif
#ifndef FALSE
#define FALSE 0
#endif

_Static_assert(sizeof(BOOL) == 4 && sizeof(DWORD) == 4 && sizeof(LONG) == 4, "32-bit Windows integers");
_Static_assert(sizeof(WCHAR) == 2 && sizeof(WORD) == 2, "16-bit Windows integers");
_Static_assert(sizeof(SIZE_T) == 8 && sizeof(LONG_PTR) == 8 && sizeof(HANDLE) == 8 && sizeof(PVOID) == 8,
               "64-bit Windows integers, pointers and handles");
_Static_assert(sizeof(GUESTFN) == 8, "guest code address");

#endif /* RT_WINTYPES_H */
