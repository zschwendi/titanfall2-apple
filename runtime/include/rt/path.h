/* Path translation: the virtual C: drive (docs/RUNTIME.md sections 7.1 and 7.2).
 *
 * Built in runtime/core/path.c (unit rt-core-path).  The comments are the specification.
 *
 * The guest sees only a small Windows file tree, mapped by a table set at start-up:
 *
 *   C:\Titanfall2                          RtConfig.install_root; also the current directory at start
 *   C:\Users\Player                        <RtConfig.state_dir>/home
 *   C:\Users\Player\AppData\Local\Temp     <RtConfig.state_dir>/tmp
 *   C:\Windows, C:\Windows\system32        exist and are empty
 *
 * Anything else is ERROR_PATH_NOT_FOUND.  ALL translation goes through these functions; no
 * shim touches a path string itself (`make shim-check` rejects it).
 *
 * Errors of the host calls that follow a translation are mapped by rt_win32_from_errno
 * (rt/shim.h), never by hand.
 */
#ifndef RT_PATH_H
#define RT_PATH_H

#include "rt/wintypes.h"

#define RT_PATH_MAX  1024       /* bytes of a host path buffer, NUL included */

/* Flags for rt_path_to_host_*. */
#define RT_PATH_EXISTING   0x0001u   /* the last component must exist (case folding applies to it too) */
#define RT_PATH_CREATE     0x0002u   /* the last component may be new: keep the caller's spelling for it */
#define RT_PATH_DIR        0x0004u   /* the path names a directory: a missing last component is
                                        ERROR_PATH_NOT_FOUND rather than ERROR_FILE_NOT_FOUND */
#define RT_PATH_WILDCARD   0x0008u   /* the last component may contain * and ?: translate the directory
                                        and append the pattern unchanged (FindFirstFile) */
#define RT_PATH_DELETE     0x0010u   /* the caller is about to delete what the path names (DeleteFile,
                                        RemoveDirectory, a create that truncates or replaces) */
#define RT_PATH_RENAME     0x0020u   /* the caller is about to move what the path names, or move
                                        something onto it (MoveFile: for the source AND the target) */

/* RT_PATH_DELETE and RT_PATH_RENAME protect the SKELETON of the drive.  The directories of
 * the table above are real host directories (C:\Windows is <state>/vfs/Windows), and so are
 * the directories that lead to one; without these flags a translated path to one of them
 * is an ordinary host path, and a host rmdir or rename on it would take a piece of the
 * virtual drive away.  With either flag (they do the same; two names so that the call says
 * what it is about to do) the translation fails with ERROR_ACCESS_DENIED for a path that
 * names:
 *   - the root C:\, or one of the directories of the table (in any spelling: case, a
 *     trailing separator, `.` and `..`, a relative path that resolves to one);
 *   - a directory on the way to one: C:\Users\Player\AppData and ...\AppData\Local, which
 *     lead to the Temp root;
 *   - NUL, in every spelling these functions take for it (NUL, C:\dir\nul, \\.\NUL), and
 *     also together with RT_PATH_DIR, where NUL without these flags is ERROR_PATH_NOT_FOUND:
 *     with one of these flags NUL is ERROR_ACCESS_DENIED and nothing else.
 * Everything else translates exactly as without the flag.  The refusal comes after the
 * errors of the path itself (another drive, a bad name) and before the disk is looked at,
 * so it does not depend on RT_PATH_EXISTING.  The flags combine with RT_PATH_EXISTING,
 * RT_PATH_CREATE and RT_PATH_DIR; with RT_PATH_WILDCARD they are a bug of the caller, as
 * every other flag is.  EVERY caller that deletes, renames or replaces passes one: the
 * check is made nowhere else.  (ERROR_ACCESS_DENIED is this runtime protecting its tree;
 * what Windows answers for the same call on its own C:\Windows is not verified.) */

/* Called once by rt_init: remember the roots from the configuration, create the host
 * directories behind the virtual ones, ask each mapped root whether its volume is
 * case-sensitive, set the guest's current directory to C:\Titanfall2.  Every other
 * function here is fatal before it.  Fatal if a root cannot be used. */
void rt_path_init(void);

/* Translate a Windows path to a host path.  Accepts drive-absolute (C:\x), rooted (\x) and
 * relative paths (against the guest's current directory, which is runtime state and not
 * the host's), both slashes, `.` and `..` resolved textually as Windows does, and the \\?\
 * prefix.  NUL maps to /dev/null.  UNC paths, other drives and device paths fail and are
 * logged on the `file` channel.  On a case-sensitive volume each component is resolved by
 * exact match first and then by an ASCII case-folded lookup in a cached directory listing
 * (section 7.2); new files keep the caller's spelling.
 * Returns 0, or a Win32 error (ERROR_PATH_NOT_FOUND, ERROR_FILE_NOT_FOUND,
 * ERROR_INVALID_NAME, ERROR_FILENAME_EXCED_RANGE, ERROR_BAD_PATHNAME, ERROR_ACCESS_DENIED);
 * it does NOT set last error, the shim does.  The A form takes bytes in code page 1252.
 * Unknown flag bits and flag combinations that mean nothing are the unimplemented report.
 * This is a translation that looks at the disk; the text-only form, for GetFullPathName,
 * is rt_path_full_w below. */
DWORD rt_path_to_host_w(const WCHAR *win, uint32_t flags, char host[RT_PATH_MAX]);
DWORD rt_path_to_host_a(const char *win, uint32_t flags, char host[RT_PATH_MAX]);

/* *file_part of rt_path_full_w when the result has no last component to point at. */
#define RT_PATH_NO_PART  0xFFFFFFFFu

/* The full path of `win` as TEXT: the core of GetFullPathNameW and GetFullPathNameA.
 *
 * It applies the normalisation of rt_path_to_host_* (one piece of code does both, so the two
 * can not disagree on a path both accept) and NOTHING else: the disk is not looked at,
 * nothing has to exist or to lie inside the virtual tree, no character is checked, case is
 * not folded, and the paths the translation refuses (other drives, UNC, device paths) are
 * answered like any other.  A result is therefore no promise that rt_path_to_host_* takes it.
 *
 * ARGUMENTS
 *   win        The name, NUL-terminated UTF-16; a guest's pointer is passed as it came.
 *              NULL, "" and a name of nothing but spaces are ERROR_INVALID_NAME.
 *   out, cch   The buffer and its size in WCHARs, terminator included.  `out` may be NULL
 *              only when cch is 0 (the call that asks for the size).
 *   len        Never NULL.  Receives what GetFullPathNameW returns (SIZES below).
 *   file_part  May be NULL.  Receives the index in `out` of the first WCHAR of the last
 *              component, or RT_PATH_NO_PART (FILE PART below).
 *
 * RETURNS 0 or a Win32 error, and NEVER sets last error: the convention of every function of
 * this header (runtime/README.md).  The shim does `if (e) return FAIL(ctx, e, 0);` and
 * otherwise returns *len without touching last error.  The errors:
 *   ERROR_INVALID_NAME          `win` is NULL, empty or only spaces.  (GetFullPathName returns
 *                               0 for these; the code is what Wine's tests record for
 *                               Windows 7, and later versions leave last error alone:
 *                               kernel32/tests/path.c test_GetFullPathNameA/W.)
 *   ERROR_FILENAME_EXCED_RANGE  `win`, or the result, is longer than 32767 WCHARs (not
 *                               verified: the limit is that of a UNICODE_STRING).
 * and no other.  On an error *len is 0, *file_part is RT_PATH_NO_PART and `out` is untouched.
 *
 * SIZES, the GetFullPathNameW convention.  L is the length of the result in WCHARs without
 * its terminator.  If cch >= L + 1: the result and its terminator are written, *len = L.
 * Otherwise (cch 0 included): NOTHING is written, *len = L + 1 (so *len > cch tells the two
 * apart), *file_part is RT_PATH_NO_PART, and the return is still 0: a buffer that is too
 * small is not an error and sets no last error.
 *
 * THE RESULT, by the form of `win`.  A separator is `\` or `/`; every `/` comes out as `\`.
 *   X:\rest        X any letter: `X:\`, then rest normalised.  The letter stays as written
 *                  ("c:\a" is "c:\a").
 *   C:rest         (no separator after the colon; also "C:" alone, and c:) the current
 *                  directory, `\`, rest normalised.
 *   X:rest         X another letter: `X:\`, then rest normalised: a drive that does not exist
 *                  has its root as current directory.  The "=X:" environment variables are
 *                  not consulted, and the letter stays as written (not verified).
 *   \rest          `C:\`, then rest normalised.  This includes `\??\C:\x`, which Win32 reads as
 *                  a rooted path: "C:\??\C:\x".
 *   rest           the current directory, `\`, rest normalised.  In these last two forms and
 *                  in C:rest the drive is spelled `C:` and the current directory is spelled
 *                  as rt_path_get_cwd_w returns it.
 *   \\.\rest  \\?\rest
 *                  these four characters, then rest normalised like any other: `\\?\` does
 *                  NOT switch normalisation off here, unlike in rt_path_to_host_* (the
 *                  Microsoft page "File path formats on Windows systems" says so for
 *                  GetFullPathName).  `..` can remove every component after the four
 *                  characters, a drive too: `\\?\C:\..` is `\\?\` (Wine does this; not
 *                  verified on Windows).
 *   \\server\share\rest
 *                  `\\server\share\`, then rest normalised.  Server and share are copied as
 *                  written, are not trimmed and are never removed by `..`; a run of
 *                  separators after either is one `\`, and one that is not there is not
 *                  added: `\\server`, `\\server\` and `\\server\share` come back as they are
 *                  (none of this is verified on Windows).
 *   A DOS device name: if `win` has one of the first five forms (it does not begin with two
 *   separators), the result so far does not end in `\`, and its last component is CON, PRN,
 *   AUX, NUL, COM1 to COM9, LPT1 to LPT9, CONIN$ or CONOUT$ in any case, the result is
 *   instead `\\.\` and that component as written: "C:\dir\nul" is `\\.\nul`, in any
 *   directory, as rt_path_to_host_* reads it.  Windows up to 10 does this, and also fails if
 *   the directory does not exist, which this function does not look at; Windows 11 is said
 *   to keep it for a bare name only (neither verified).
 *
 * NORMALISED means, as in rt_path_to_host_*: runs of separators are one; a segment `.` is
 * dropped; a segment `..` removes the component before it, back to the root of the form
 * (`C:\`, the four characters, the share) and no further; a segment that a separator follows
 * and that ends in exactly one period loses it ("a.\" is "a\"; "a..\", "...\" and "a. \"
 * stay); the last segment, if `win` does not end in a separator, loses all its trailing
 * periods and spaces.  The result ends in `\` in exactly
 * three cases: nothing is left after the root, and the root ends in one (`C:\`, `\\.\`,
 * `\\server\share\`); or `win` ended in a separator and a component is left ("C:\a\" stays
 * "C:\a\"; ".\" is the current directory and a `\`); or the last segment was nothing but
 * periods and spaces without being `.` or `..`, so that trimming removed it and left the
 * separator in front of it ("C:\a\..." and "C:\a\ " are "C:\a\"; Wine test_RtlGetFullPathName_U).  So "." is
 * the current directory with no `\` at its end, ".." from C:\Titanfall2 is "C:\".
 *
 * FILE PART.  *file_part is the index of the WCHAR after the last `\` of the result, when
 * the result was written and something follows that `\`.  It is RT_PATH_NO_PART when
 * nothing was written, when the result ends in `\`, when it came from a DOS device name
 * (`\\.\nul` from "nul"; a `\\.\nul` that was written out as such has the part "nul"), and
 * for `\\server` (as Windows: Wine ntdll/tests/path.c test_RtlGetFullPathName_U) and
 * `\\server\share` (not verified: Wine points at "share").  The shim
 * stores `out + *file_part`, or NULL, in the guest's lpFilePart if that is not NULL.
 *
 * NOT RETURNED BUT REPORTED (the unimplemented report, which ends the run): `len` NULL; `out`
 * NULL with cch > 0; an unpaired surrogate in `win` (Windows copies it; this code works in
 * UTF-8 and can not hold it); `\\.` or `\\?` with nothing after it; two separators with no
 * server name after them (`\\`, `\\\x`); a last component that is a device name followed by
 * `.` or `:` ("nul.txt", "NUL:"), under the conditions of the device rule above.  For the
 * last three, what Windows returns is not established, and a guess would be silent.
 * FATAL: a call before rt_path_init; no memory (malloc is used once a name, or the current
 * directory and the name together, pass a few hundred characters).
 *
 * THREADS AND LOCKS.  Any guest thread, at any time.  It takes the lock of the current
 * directory once, to copy it (the lock rt_path_set_cwd_w takes to store it; private to
 * core/path.c, held for one copy, nothing is called under it), and works on the copy: a
 * concurrent SetCurrentDirectory gives the old or the new directory, never a mixture.  The
 * copy is taken for every form, also those that do not use it.  It takes no other lock of
 * the runtime, except that an error is logged on the `file` channel at level debug (the
 * log's own lock).  NOT for a signal handler (the locks, and malloc for long names).
 *
 * THERE IS NO A FORM.  GetFullPathNameA converts the name to UTF-16 and the result back
 * itself (rt/locale.h; rt_path_to_host_a reads the same bytes as code page 1252).  In that
 * code page one byte is one WCHAR, so *len and *file_part are the byte counts too as long
 * as the result holds only its characters; the current directory can hold others (it is
 * set in UTF-16), and what the A shim does then is the file layer's decision. */
DWORD rt_path_full_w(const WCHAR *win, WCHAR *out, DWORD cch, DWORD *len, DWORD *file_part);

/* Translate a host path under one of the mapped roots back to its Windows form.  Writes at
 * most `cch` WCHARs including the terminator and returns the length written without it;
 * returns the required size including the terminator if `cch` is too small, and 0 if the
 * host path is under no mapped root. */
int rt_path_to_win_w(const char *host, WCHAR *out, int cch);

/* The guest's current directory as a normalised Windows path without a trailing backslash
 * (except for the root "C:\").  Get follows the GetCurrentDirectoryW convention for sizes.
 * Set returns 0 or a Win32 error; the directory must exist. */
DWORD rt_path_get_cwd_w(WCHAR *out, DWORD cch);
DWORD rt_path_set_cwd_w(const WCHAR *win);

/* The directories of the virtual drive that Windows functions name without a path
 * (docs/RUNTIME.md section 7.1: "SHGetFolderPath, GetTempPath, GetSystemDirectory and the
 * environment (8.2) all answer inside this tree").  A shim may not spell a path, so the text
 * comes from here.  Each is a directory of the table at the top or of the home tree, exists
 * from rt_path_init on, and translates with rt_path_to_host_*. */
typedef enum {
    RT_KNOWN_WINDOWS = 1,       /* C:\Windows                              GetWindowsDirectory, CSIDL_WINDOWS */
    RT_KNOWN_SYSTEM,            /* C:\Windows\system32                     GetSystemDirectory, CSIDL_SYSTEM */
    RT_KNOWN_TEMP,              /* C:\Users\Player\AppData\Local\Temp\    GetTempPath: WITH the backslash at the end */
    RT_KNOWN_PROFILE,           /* C:\Users\Player                         CSIDL_PROFILE */
    RT_KNOWN_DOCUMENTS,         /* C:\Users\Player\Documents               CSIDL_PERSONAL */
    RT_KNOWN_LOCAL_APPDATA,     /* C:\Users\Player\AppData\Local           CSIDL_LOCAL_APPDATA */
    RT_KNOWN_APPDATA            /* C:\Users\Player\AppData\Roaming         CSIDL_APPDATA */
} RtKnownPath;

/* The text of one known directory, by the size rule that GetSystemDirectory and GetTempPath
 * share (their pages): if the buffer holds the text and its terminator, both are written and
 * the return value is the length WITHOUT the terminator; otherwise NOTHING is written and
 * the return value is the size needed WITH it.  So a result above cch means "too small", and
 * cch 0 (out may then be NULL) asks for the size.  The A form gives the same characters as
 * bytes: every one is ASCII, so the two forms return the same numbers.
 * The texts never change: RT_KNOWN_TEMP is the value the environment STARTS with for TMP
 * and TEMP (core/env.c) and does not follow a guest that sets those variables, although the
 * page of GetTempPath says the function reads TMP, then TEMP, then USERPROFILE at each call.
 * What ends in a backslash: RT_KNOWN_TEMP only (the page of GetTempPath: "The returned
 * string ends with a backslash"; of GetSystemDirectory: "This path does not end with a
 * backslash unless the system directory is the root directory").
 * A buffer that is too small is left untouched: that is what GetTempPathA does on Windows
 * (Wine kernel32/tests/path.c test_GetTempPathA: "When the buffer is not long enough it
 * remains untouched"); for GetSystemDirectoryA the same test file checks the return value
 * only.  GetTempPathW is different, it fills its buffer with zeros (test_GetTempPathW); no
 * boot-path DLL imports it, and a shim for it has to do that itself.
 * Cannot fail and sets no last error; works before rt_path_init (the texts are constants);
 * takes no lock.
 * Fatal: `which` is not one of the values above; out NULL when cch is large enough for the
 * text (Wine's test of GetSystemDirectoryA notes that this crashes on Windows XP).
 * Sources: Microsoft's pages of GetTempPathA (fileapi.h) and GetSystemDirectoryA
 * (sysinfoapi.h) and Wine's kernel32/tests/path.c, read 2026-10-10. */
DWORD rt_path_known_w(RtKnownPath which, WCHAR *out, DWORD cch);
DWORD rt_path_known_a(RtKnownPath which, char *out, DWORD cch);

/* The runtime created, deleted or renamed something in host directory `host_dir`: drop its
 * cached listing (section 7.2). */
void rt_path_invalidate(const char *host_dir);

#endif /* RT_PATH_H */
