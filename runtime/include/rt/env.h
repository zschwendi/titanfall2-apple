/* The guest's environment block, command line and start-up information (docs/RUNTIME.md
 * section 8.2).  Implemented in runtime/core/env.c; tests in runtime/tests/core/env*.c.
 *
 * Nothing the guest sees is inherited from the host.
 *   - Environment: a fixed set of variables describing the pretend machine, then the
 *     embedding program's list RtConfig.env_overrides (rt_env_apply_config), then overrides
 *     named RT_GUEST_<NAME> in the host environment (rt_env_apply_overrides).  A later
 *     one wins.
 *   - Command line: "C:\Titanfall2\Titanfall2.exe" followed by the host program's arguments,
 *     each quoted by the rules the Microsoft C runtime parses (rt_cmdline_build_w).
 *   - Start-up information: no inherited handles (rt_env_startup_info).
 *
 * Text.  Inside the runtime names and values are UTF-16, so the W functions carry any text
 * exactly.  The A functions carry ASCII only: an A call that meets a byte >= 0x80, or an A
 * call that would return a value containing a character >= 0x80, is fatal and names the
 * character.  Code page 1252 is the locale unit's business (rt-core-locale); this file does not
 * convert code pages.  Variable names are compared with ASCII case folding only.
 *
 * Errors.  Each function returns what its Win32 function returns and reports a failure through
 * an out parameter, `DWORD *err`: the Win32 error the shim must set (0 when there is none).
 * The core never touches the thread's last error, so every function can be called from any
 * host thread, guest or not.  A shim is therefore one line of logic around the call:
 *
 *     DWORD err;
 *     DWORD n = rt_env_get_w(name, buf, size, &err);
 *     if (err) return FAIL(ctx, err, 0);
 *     return n;
 *
 * Every case the runtime does not handle is fatal (RUNTIME.md section 9.2), and the report
 * names the offending value.  Those cases are listed in the comments of each function.
 */
#ifndef RT_ENV_H
#define RT_ENV_H

#include <stddef.h>

#include "rt/wintypes.h"

/* Characters in one variable's value, not counting the terminator (SetEnvironmentVariable). */
#define RT_ENV_VALUE_MAX  32767u

#include "rt/win/process.h"    /* STARTUPINFOW */

/* ---- start-up ----------------------------------------------------------------------------- */

/* Called by rt_init (declared in rt/init.h; its place is after rt_path_init and before
 * rt_module_registry_init).  Builds the environment from the machine description, then
 * RtConfig.env_overrides, then the host's RT_GUEST_ overrides, and the command line from
 * RtConfig.argc / argv.  Fatal if called twice, or if an argument or override is not valid
 * UTF-8, or if an override has an empty name, has no '=' or has a value longer than
 * RT_ENV_VALUE_MAX.
 *
 * RtConfig.argv holds the arguments given to the host program, WITHOUT its own name:
 * argv[0] of main() is not passed on (rt/init.h says the same). */
void rt_env_init(void);

/* Apply a NULL-terminated list of UTF-8 "NAME=value" strings: every entry sets the guest
 * variable NAME to the value (everything after the FIRST '=', so a value may contain '=';
 * an empty value sets the empty string).  The name is matched case-insensitively like any
 * other, and an existing variable keeps its spelling.  rt_env_init calls this with
 * RtConfig.env_overrides.  A NULL list changes nothing.  A name that begins with '=' (the
 * per-drive current-directory entries) cannot be set this way.
 * Fatal: before rt_env_init has built the environment; an entry with no '=', an empty name
 * (an entry that begins with '='), a name that is not ASCII, invalid UTF-8, or a value
 * longer than RT_ENV_VALUE_MAX characters.  Sets no last error; takes the environment's
 * lock. */
void rt_env_apply_config(const char *const *entries);

/* Apply overrides from a NULL-terminated list of host "NAME=value" strings (the layout of
 * environ).  Every entry whose name begins with RT_GUEST_ sets the guest variable named by
 * the rest of the name: RT_GUEST_TEMP=D:\x sets TEMP.  The name is matched case-insensitively
 * like any other, and an existing variable keeps its spelling.  Other entries are ignored.
 * rt_env_init calls this with the host's environ.  A NULL list changes nothing.
 * Fatal: an entry with no '=', an empty name, a name that is not ASCII, invalid UTF-8, or a
 * value longer than RT_ENV_VALUE_MAX characters. */
void rt_env_apply_overrides(char *const *entries);

/* ---- environment variables ------------------------------------------------------------------ */

/* GetEnvironmentVariableW.  Copies the value of `name` into buf, which holds `size` WCHARs
 * including the terminator, and returns the number of WCHARs copied, not counting the
 * terminator.  Names compare case-insensitively.
 *
 * If the buffer is too small, nothing is written and the return value is the size needed
 * including the terminator; *err is 0 (this is how Windows reports it, and it is not a
 * failure).  So size 0 with buf NULL is a size query.
 *
 * If the variable does not exist, returns 0, sets *err to ERROR_ENVVAR_NOT_FOUND and leaves
 * buf alone.
 *
 * A variable whose value is empty returns 0 with *err 0.  The shim leaves the last error alone
 * on that path, as on every other success.
 *
 * Fatal: a NULL name; a name containing '=' after its first character; a name that is not
 * ASCII; a NULL buf when the value fits in `size`.
 * A name may BEGIN with '=': the C runtime's _chdir keeps the per-drive current directory in
 * variables named "=C:", set through SetEnvironmentVariable and listed in the block as
 * "=C:=C:\dir".  Such a variable is stored, returned and listed like any other; the runtime
 * gives it no meaning and creates none itself. */
DWORD rt_env_get_w(const WCHAR *name, WCHAR *buf, DWORD size, DWORD *err);

/* GetEnvironmentVariableA: the same, with bytes.  `size` counts bytes.  Fatal additionally if
 * a byte of the name is >= 0x80, or if the value of the variable found contains a character
 * >= 0x80 (whether or not it fits). */
DWORD rt_env_get_a(const CHAR *name, CHAR *buf, DWORD size, DWORD *err);

/* SetEnvironmentVariableW.  A NULL value deletes the variable.  An empty value sets it to
 * the empty string (it still exists).  A new variable is added at the end of the block; an
 * existing one keeps its place and its spelling.  Returns TRUE on success, FALSE with *err:
 *   - delete of a variable that does not exist: ERROR_ENVVAR_NOT_FOUND;
 *   - value longer than RT_ENV_VALUE_MAX characters: ERROR_INVALID_PARAMETER;
 *   - an empty name with a value: ERROR_INVALID_PARAMETER; an empty name with NULL: as for a
 *     missing variable.
 * The last three are our choice; the documentation does not name the errors.  Fatal: the
 * name cases of rt_env_get_w. */
BOOL rt_env_set_w(const WCHAR *name, const WCHAR *value, DWORD *err);

/* SetEnvironmentVariableA: the same, with bytes.  Fatal additionally for a non-ASCII byte in
 * the name or in the value. */
BOOL rt_env_set_a(const CHAR *name, const CHAR *value, DWORD *err);

/* ExpandEnvironmentStringsA(lpSrc, lpDst, nSize): copy `src` to `dst` with every reference
 * %NAME% replaced by the value of the variable NAME.
 *
 * THE TEXT, from the page: the form is "%variableName%"; "Case is ignored when looking up
 * the environment-variable name"; "If the name is not found, the %variableName% portion is
 * left unexpanded".  The string is read once from left to right.  A `%` that no later `%`
 * follows is copied with the rest of the string as it is.  Otherwise the text between the
 * two is a name: if such a variable exists its value is copied in place of the reference
 * (the value is not read again: a `%` in it stays), and if not, the reference is copied as
 * it is, both `%` included, and reading goes on after its second `%`.  None of what Cmd.exe
 * adds, such as %NAME:a=b%, is understood (the page: "this function does not support all
 * the features that Cmd.exe supports").
 *
 * SIZES, for the A form, where L is the length of the result in bytes without its
 * terminator.  If size >= L + 2: the result and its terminator are written and L + 1 is
 * returned.  Otherwise L + 2 is returned and, if dst is not NULL, dst[0] is set to 0: an
 * empty string, also when size is 0.  (The page: "When using ANSI strings, the buffer size
 * should be the string length, plus terminating null character, plus one"; the numbers and
 * the empty string are what Wine's kernel32/tests/environ.c test_ExpandEnvironmentStringsA
 * requires of Windows in its table of 49 calls, the row with size 0 included.  That table has
 * one required value per call; the older calls at the top of the same test accept L + 1 as
 * well, with comments that name Windows versions before 7.)  So dst NULL with size 0 asks
 * for the size.
 *
 * It cannot fail: it never returns 0, has no error to give and so takes no `DWORD *err`,
 * and never touches last error, also when the buffer is too small (the same Wine table).
 *
 * FATAL, each because what Windows does is not verified or this unit cannot carry the text:
 *   - src NULL (Wine's test allows three different results by Windows version);
 *   - a byte >= 0x80 in src, or a character >= 0x80 in a value that is copied (the rule of
 *     this header for every A function);
 *   - a reference to a variable that does not exist, when the text between its second `%`
 *     and the next `%` is the name of a variable that does: "%NOPE%TMP%" with TMP set.
 *     Windows either reads on after the second `%`, as described above, and leaves "TMP%",
 *     or reads that `%` again as the start of %TMP%; no test read decides it;
 *   - a result longer than 32,767 bytes, or a src longer than that (the page: "The size of
 *     the lpSrc and lpDst buffers is limited to 32K", for Windows XP and Server 2003);
 *   - dst NULL when size >= L + 2;
 *   - a call before rt_env_init.
 * The page also says of dst: "this buffer cannot be the same as the lpSrc buffer"; the
 * result is built before dst is written, so an overlap is not looked for.
 * Takes the environment's lock for the look-ups.
 *
 * THERE IS NO W FORM: no boot-path DLL imports ExpandEnvironmentStringsW.  It differs in
 * more than the character type (the same Wine file, test_ExpandEnvironmentStringsW): it
 * returns L + 1 whether or not the buffer holds the result, and it leaves a cut-off piece of
 * the result in a buffer that is too small.  Whoever builds it starts from that table.
 * Sources: Microsoft's page of ExpandEnvironmentStringsA (processenv.h) and Wine's
 * kernel32/tests/environ.c, read 2026-10-10. */
DWORD rt_env_expand_a(const CHAR *src, CHAR *dst, DWORD size);

/* GetEnvironmentStringsW.  A new block in guest memory: each "NAME=value" followed by a NUL,
 * then one more NUL (so an environment with no variables is a single NUL).  It is a snapshot:
 * later changes do not show in it.  Release it with rt_env_block_free.  NULL with *err
 * ERROR_NOT_ENOUGH_MEMORY when guest memory is exhausted. */
WCHAR *rt_env_block_w(DWORD *err);

/* GetEnvironmentStringsA: the same, one byte per character.  Fatal if any value in the
 * environment has a character >= 0x80 (the block cannot carry it). */
CHAR *rt_env_block_a(DWORD *err);

/* FreeEnvironmentStringsW and FreeEnvironmentStringsA: free a block from either function.
 * Both free any live block, as they do on Windows, where both release a local allocation.
 * Returns TRUE.  NULL is accepted and does nothing.  Fatal: a pointer that is not a live block
 * from rt_env_block_w or rt_env_block_a (never handed out, or freed already): the report names
 * the pointer.  That is the bug Windows would turn into heap corruption. */
BOOL rt_env_block_free(void *block);

/* ---- command line -------------------------------------------------------------------------- */

/* GetCommandLineW: the command line, one string for the life of the process, the same pointer
 * on every call.  The shim must not free it.  Built in guest memory at start-up. */
WCHAR *rt_cmdline_w(void);

/* GetCommandLineA: the same, in bytes.  Fatal if the command line has a character >= 0x80: it
 * is an argument of the host program that the A form cannot carry until the locale unit
 * converts code pages.  The W form carries it exactly. */
CHAR *rt_cmdline_a(void);

/* Build a Windows command line: `program` followed by the arguments `args[0 .. nargs-1]`,
 * separated by single spaces.  The program is always quoted.  Each argument is quoted only
 * when it is empty or contains a space, a tab or a double quote.  Inside those quotes a run of
 * backslashes followed by a double quote is doubled and the quote is escaped, and a run of
 * backslashes at the end of the argument is doubled before the closing quote; other
 * backslashes are literal.  These are the rules of the Microsoft C runtime's argument parser
 * as its documentation describes them (not verified against the parser itself: the test in
 * runtime/tests/core/env_cmdline.c splits the result with a second implementation of those
 * documented rules, on every short combination of spaces, tabs, quotes and backslashes).
 *
 * Writes the result and its terminator into out (cch WCHARs) if it fits, and returns the
 * number of WCHARs needed INCLUDING the terminator, as GetEnvironmentVariable does.  If it
 * does not fit, nothing is written.  out may be NULL (cch is then ignored): a size query. */
SIZE_T rt_cmdline_build_w(const WCHAR *program, const WCHAR *const *args, DWORD nargs,
                          WCHAR *out, SIZE_T cch);

/* ---- start-up information ------------------------------------------------------------------ */

/* GetStartupInfoW: copy the start-up information into *out (fatal if out is NULL).  Reports
 * no inherited handles: hStdInput, hStdOutput and hStdError are NULL, lpDesktop and lpTitle
 * are NULL, dwFlags is 0 and wShowWindow is 0.  cb is sizeof(STARTUPINFOW).  The zero flags
 * are our choice: Windows reports what the creating process passed, and no process created us. */
void rt_env_startup_info(STARTUPINFOW *out);

#endif /* RT_ENV_H */
