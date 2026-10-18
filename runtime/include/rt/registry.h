/* The registry: an in-memory tree of keys and values kept in one text file
 * (docs/RUNTIME.md section 8.3).  Implementation: runtime/core/registry.c.
 *
 * This is the core under the advapi32 shims RegOpenKeyExA/W, RegCreateKeyExA/W, RegCloseKey,
 * RegQueryValueExA/W, RegSetValueExA/W, RegDeleteValueA/W, RegEnumValueA/W, RegEnumKeyExA/W,
 * RegQueryInfoKeyA/W, RegDeleteKeyA/W (RegDeleteKeyExA/W), RegFlushKey, and the shlwapi shim
 * SHDeleteKeyA/W (rt_reg_delete_tree_*).  Each function here
 * has the arguments and the return value of the Windows function named in its comment, minus
 * the calling convention, so a shim is a one-line forwarder:
 *
 *     SHIM(advapi32, LSTATUS, RegQueryValueExW, HKEY key, LPCWSTR name, LPDWORD reserved,
 *                    LPDWORD type, LPBYTE data, LPDWORD cb)
 *     {
 *         return rt_reg_query_value_ex_w(key, name, reserved, type, data, cb);
 *     }
 *
 * THREE RULES THAT HOLD FOR EVERY FUNCTION BELOW
 *
 * 1. RETURN VALUE.  Every function returns a Win32 error code (ERROR_SUCCESS, 0, on success),
 *    as the Windows registry functions do.  They do NOT set the thread's last error, on success
 *    or on failure, and the shim must not either.  A failed lookup of a handle goes through the
 *    handle table, which sets ERROR_INVALID_HANDLE; the core puts the caller's last error back,
 *    so a failing call leaves last error as it was.
 *
 * 2. A CASE WE DO NOT HANDLE IS FATAL, NOT A GUESS (RUNTIME.md 9.2).  When a call asks for
 *    something this core does not model, or for something about which the behaviour of Windows
 *    is not established, the function does not return: it ends in rt_unimplemented, one
 *    "FATAL unimplemented" block that names the Windows function and the offending value.  Each
 *    such case is listed in the comment of the function.  A failure that Windows itself reports
 *    (key not found, buffer too small, access denied) is a normal return.
 *
 * 3. ONE LOCK.  All functions are thread-safe and may be called from any host thread, guest or
 *    not.  They take one internal lock and never call guest code.
 *
 * WHAT THE TREE IS
 *
 * Three root keys exist: HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER and HKEY_USERS.  HKEY_USERS has
 * no subkeys (there are no user profiles to list: there is no ".DEFAULT" and no SID key) and
 * HKEY_CURRENT_USER is a root of its own, not a link to a SID key.  The other predefined keys
 * are views:
 *     HKEY_CLASSES_ROOT   is HKEY_LOCAL_MACHINE\Software\Classes (RUNTIME.md 8.3; Windows
 *                         merges HKCU\Software\Classes into it, this runtime does not)
 *     HKEY_CURRENT_CONFIG is HKEY_LOCAL_MACHINE\System\CurrentControlSet\Hardware Profiles\Current
 * The remaining predefined handles (HKEY_PERFORMANCE_DATA, HKEY_PERFORMANCE_TEXT,
 * HKEY_PERFORMANCE_NLSTEXT, HKEY_CURRENT_USER_LOCAL_SETTINGS) are fatal when used; any other
 * handle with the top bit set (HKEY_DYN_DATA included, which only Windows 9x has) is
 * ERROR_INVALID_HANDLE.  Nothing is redirected for the 32-bit and 64-bit registry views: the
 * flags KEY_WOW64_32KEY and KEY_WOW64_64KEY are accepted, alone or together, and name the same
 * tree, where "Wow6432Node" is an ordinary key name.  The tree has no symbolic links
 * (REG_OPTION_CREATE_LINK is fatal), no security descriptors and no change notification.
 *
 * HANDLES.  A predefined key is a constant; Windows spells HKEY_LOCAL_MACHINE (and the others)
 * as the 32-bit value 0x8000000n sign-extended to 64 bits.  A guest may hold it truncated to 32
 * bits (0x0000000080000002, for example when it was stored in a DWORD) or extended
 * (0xFFFFFFFF80000002).  THE RULE: a handle is a predefined key if its low 32 bits are one of
 * the constants below and its high 32 bits are all zero or all one; both spellings name the
 * same key.  A handle with the top bit of its low 32 bits set whose high half is anything else
 * is ERROR_INVALID_HANDLE.  Any other handle is looked up in the kernel handle table (rt/handle.h)
 * where an open key is an object of kind RT_K_REGKEY, so DuplicateHandle and CloseHandle work
 * on it as well as RegCloseKey.  A predefined key is never in the table: closing it does
 * nothing and succeeds.
 *
 * ACCESS.  An opened key remembers the access mask it was opened with.  Generic rights are
 * mapped (GENERIC_READ and GENERIC_EXECUTE to KEY_READ, GENERIC_WRITE to KEY_WRITE,
 * GENERIC_ALL to KEY_ALL_ACCESS), MAXIMUM_ALLOWED is KEY_ALL_ACCESS (there are no ACLs), and a
 * predefined key has full access.  Each operation needs the right Windows documents and fails
 * with ERROR_ACCESS_DENIED without it: query value, query info and enumerate values need
 * KEY_QUERY_VALUE; set and delete value need KEY_SET_VALUE; enumerate subkeys needs
 * KEY_ENUMERATE_SUB_KEYS.  Opening, creating and deleting keys check no right of the handle that
 * names the parent: the Windows kernel checks the parent's ACL for those, not the mask of the
 * handle (UNSURE; there are no ACLs here, so they succeed).  samDesired = 0 is accepted and gives
 * a handle that can be closed, flushed and used as the base of an open, and nothing else (UNSURE:
 * what Windows 7 does).  An access mask with a bit that is none of the documented ones is fatal.
 *
 * ORDER OF CHECKS in a function that takes a key: the arguments (ERROR_INVALID_PARAMETER), the
 * handle (ERROR_INVALID_HANDLE), the handle's access right (ERROR_ACCESS_DENIED), whether the key
 * was deleted (ERROR_KEY_DELETED), then the operation.  A leading backslash in a subkey of an open
 * or create (ERROR_BAD_PATHNAME, NAMES) is decided after the handle, since under HKEY_CLASSES_ROOT
 * the handle decides it, and before the deleted check.  The lists below name the errors of each
 * function and are not in that order.
 *
 * DELETED KEYS.  A key that is deleted while handles to it are open stays alive until they are
 * closed (as on Windows) and every operation on such a handle except closing it returns
 * ERROR_KEY_DELETED.
 *
 * NAMES
 *
 * Key and value names compare case-insensitively and keep the case they were created with:
 * opening "software" finds a key created as "Software", the stored spelling is what
 * enumeration returns, and writing a value as "FOO" over a value created as "Foo" keeps
 * "Foo".  Case is folded one UTF-16 code unit at a time through rt_wchar_upper
 * (rt/locale.h), as Windows folds with its upper-case table; a name containing a code unit
 * whose case that table does not cover (rt_wchar_case_known) is fatal.  Names are counted
 * UTF-16 strings; the A functions convert them through the ANSI code page (1252) with
 * rt/locale.h and count their lengths in bytes of the converted text.  A character the code
 * page lacks is converted as WideCharToMultiByte converts it with flags 0: by Windows's
 * best-fit table for code page 1252, or to '?' where the table has nothing.
 * Limits: a key name is at most 255 characters, a value name at most 16,383, the subkey
 * string of a call at most 32,767; longer is fatal (the code Windows returns is not
 * established).  A subkey string is names joined by single backslashes.  A NULL or empty
 * string means "the key itself".  RegOpenKeyEx and RegCreateKeyEx (Wine's advapi32/tests/registry.c,
 * test_reg_open_key and test_reg_create_key) ignore trailing backslashes, and a leading one is
 * ERROR_BAD_PATHNAME, except under the predefined HKEY_CLASSES_ROOT, where it is skipped (`\clsid`
 * there opens the key clsid).  Fatal, UNSURE what Windows does: two backslashes together; a
 * string of backslashes only under HKEY_CLASSES_ROOT; a leading backslash in a subkey of a handle
 * that is itself below HKEY_CLASSES_ROOT.  The delete functions (RegDeleteKeyEx, RegDeleteTree,
 * SHDeleteKey) have no evidence for the trailing and leading cases, so a backslash at either end
 * is fatal there too.  A key name cannot contain a backslash; neither kind of name can contain a NUL.
 *
 * The default value of a key is the value with the empty name.  A NULL value name and an
 * empty one name it alike.
 *
 * VALUES
 *
 * A value is a type (any 32-bit number; the Windows types are REG_NONE 0, REG_SZ 1,
 * REG_EXPAND_SZ 2, REG_BINARY 3, REG_DWORD 4, REG_DWORD_BIG_ENDIAN 5, REG_LINK 6,
 * REG_MULTI_SZ 7, REG_RESOURCE_LIST 8, REG_FULL_RESOURCE_DESCRIPTOR 9,
 * REG_RESOURCE_REQUIREMENTS_LIST 10, REG_QWORD 11) and 0 to 1,048,576 bytes of data.  Data is
 * kept exactly as written and returned exactly as stored: no type is validated against its
 * size (a REG_DWORD of 8 bytes stays 8 bytes), and string data is stored with or without its
 * terminator as the caller wrote it, with no terminator added, a wide string of odd byte
 * count kept as is.  The one thing added on the way out is described at
 * rt_reg_query_value_ex_w.  The W functions store the bytes they are given.  The A functions
 * convert the data of REG_SZ, REG_EXPAND_SZ and REG_MULTI_SZ between the ANSI code page and
 * UTF-16 (byte for byte of the converted text: no terminator is added, none is removed), and
 * leave the data of every other type alone; the registry itself holds UTF-16.  Values of a key
 * enumerate in the order they were created; subkeys enumerate sorted by name, compared
 * case-insensitively code unit by code unit (UNSURE: that is how a Windows hive orders them).
 * The indices of an enumeration are stable while nothing changes; creating or deleting
 * shifts them, as on Windows.  HKEY_LOCAL_MACHINE and HKEY_USERS have no values and cannot get
 * any or get new direct subkeys (ERROR_ACCESS_DENIED; UNSURE that this is the code Windows 7
 * gives); HKEY_CURRENT_USER is an ordinary key.
 *
 * A key that is not there is ERROR_FILE_NOT_FOUND, and so is a value that is not there.
 * Each distinct missing key path and each distinct missing (key, value name) is logged ONCE on
 * the `registry` channel at level warn (case-insensitively: the same name in another spelling
 * is not logged again), so the first runs show what the game asks for.  Nothing is invented
 * for an absent name.
 *
 * THE SEED
 *
 * At start-up the tree is the contents of the file, plus, for each of the following that the file
 * does not define, the seed (from rt/machine.h and rt_cpuid, so that it cannot disagree with
 * GetVersionEx or cpuid).  The seed is not stored in the file (a file written long ago must not
 * out-vote today's machine description) unless the guest changes it: a seeded value that the guest
 * overwrites with different data is written to the file and then wins over the seed at the
 * next start.  Deleting a seeded value or key is fatal (it would come back on the next start;
 * decide deliberately).
 *   HKLM\HARDWARE\DESCRIPTION\System\CentralProcessor\<n>, n = 0 .. processor count - 1:
 *       ~MHz (REG_DWORD, RtMachine.cpu_hz / 1,000,000), Identifier (REG_SZ, "Intel64 Family 6
 *       Model 13 Stepping 8": from cpuid leaves 0 and 1), VendorIdentifier (REG_SZ, "GenuineIntel":
 *       cpuid leaf 0), and ProcessorNameString (REG_SZ: the brand string of cpuid leaves
 *       0x80000002 to 0x80000004) only if that string is not empty - today it is, so the value
 *       is absent, which is the honest answer for a part with no brand string
 *   HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion:
 *       CurrentVersion ("6.1"), CurrentBuild and CurrentBuildNumber ("7601"), CSDVersion
 *       ("Service Pack 1"): all REG_SZ, from RtMachine
 *   HKLM\SYSTEM\CurrentControlSet\Control\Windows:  CSDVersion (REG_DWORD, 0x100: SP major << 8
 *       | minor)
 *   HKLM\SYSTEM\CurrentControlSet\Control\ProductOptions:  ProductType (REG_SZ "WinNT", a
 *       workstation)
 *   the empty keys HKLM\SOFTWARE\Classes and HKLM\SYSTEM\CurrentControlSet\Hardware
 *       Profiles\Current, which the views above need
 * Nothing under the keys of the game's publisher or its launcher is seeded: absent is the
 * correct default and what the game sees there is the project owner's decision.  Nothing
 * else is seeded either (no MachineGuid, no ProductName, no SystemRoot): the game's first runs
 * log what it asks for, and each addition is deliberate.  The seeded keys are not
 * "HKLM\HARDWARE" volatile as on Windows, and ControlSet001 and the Select key are not
 * modelled: CurrentControlSet is an ordinary key.
 *
 * PERSISTENCE: WHEN THE FILE IS SAVED, AND WHY
 *
 * The file is <state_dir>/registry.txt.  It is rewritten WHOLE after EVERY call that changes
 * what the file would say (create, set, delete), before the call returns.  Why each change and
 * not on close or flush: this runtime stops on purpose, with an abort and a report, at the
 * first unimplemented thing, and this house loses power without warning, so a registry that
 * is saved "later" loses exactly the changes made just before the stop.  The game writes
 * rarely (in the one traced run it wrote nothing of its own), so a full write per change
 * costs a few milliseconds a few times.  A change that leaves the text of the file as it is
 * (a change inside a volatile key, a seeded value written back as it was, a value written with
 * data it already had) writes nothing; making or deleting a volatile key does write, because the
 * last write time of its parent moves.  RegFlushKey therefore has nothing to do and succeeds.
 *
 * How: the text is built in memory, written to <state_dir>/registry.txt.tmp, forced to the
 * disk with F_FULLFSYNC (plain fsync does not flush the drive's cache on macOS), and renamed
 * over registry.txt.  A power cut or kill leaves either the old file or the new one, whole,
 * never half of either; a stale .tmp is deleted at start-up and the next save overwrites it.
 * If the save cannot be done (disk full, directory not writable) the runtime stops with a
 * report naming the path and the error, rather than let the guest believe a write that was not
 * kept.  Two instances of the runtime on one state directory are not detected and will
 * overwrite each other.
 *
 * THE FILE
 *
 * UTF-8 text, lines ended by LF (a CR before it is ignored), no byte-order mark.  Written in
 * ASCII only (anything else is an escape); read with raw UTF-8 as well.  Blank lines and lines
 * whose first non-blank character is # are ignored (and not kept when the file is rewritten).
 * Every other line is one of these, and anything else stops the runtime at start-up with the
 * file name, the LINE NUMBER and the reason:
 *
 *     titanfall-registry 1                      the header: first line that is not blank or comment
 *     [HKEY_LOCAL_MACHINE\Software\Foo] time=01DC1234ABCD0000 class="text"
 *                                               a key; the following value lines belong to it
 *     "Name" = sz:"text"                        a value of the key
 *     @ = dword:0000002a                        the default value of the key (the empty name)
 *
 * KEY LINES.  The path is a root (HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER or HKEY_USERS, exactly
 * so) and names joined by backslashes, in square brackets at the start of the line (blanks
 * before it are allowed).  Parents need no lines of their own: a key line creates its parents.  A
 * name is written as it is, except that a character outside the printable ASCII 0x20 to 0x7E, a
 * percent sign and a closing square bracket are written %uXXXX (four hex digits of the UTF-16
 * code unit; a character outside the Basic Multilingual Plane is two of them).  After the
 * bracket, optionally and in any order, separated by blanks: time=<16 hex digits> the last
 * write time as a FILETIME (without it the key gets the start-up time), and class="<text>"
 * (without it the key has no class).  A key appears at most once.  Volatile keys are never
 * written.
 *
 * VALUE LINES.  A name is a double-quoted string, or @ for the default value, then =, then the
 * data (blanks around the = are allowed).  Inside quotes a backslash and a double quote are
 * written \\ and \", every other character outside 0x20 to 0x7E is written \uXXXX as above
 * (raw UTF-8 is also read); no other escape exists.  A value appears at most once per key.  The
 * data is one of
 *     sz:"text"                REG_SZ: text and one terminating NUL, nothing else
 *     expand_sz:"text"         REG_EXPAND_SZ, likewise
 *     multi_sz:"a","b"         REG_MULTI_SZ: each string and a NUL, then one more NUL; each string
 *                              non-empty; "multi_sz:" alone is the single NUL
 *     dword:0000002a           REG_DWORD of exactly 4 bytes: 8 hex digits, little-endian number
 *     qword:000000000000002a   REG_QWORD of exactly 8 bytes: 16 hex digits
 *     hex:de,ad,be,ef          REG_BINARY: bytes as two hex digits separated by commas ("hex:" alone
 *                              is no bytes)
 *     hex(7):de,ad             any type number (decimal) and its bytes; this form holds what
 *                              the others cannot: a string without its terminator, an odd
 *                              byte count, a REG_DWORD of 8 bytes, REG_NONE and the rest
 * The writer chooses the first form that holds the value exactly, so reading a file and
 * writing it back changes a hand-edited value only in spelling; the reader accepts every form
 * for the types it fits (hex digits in either case).  Text strings may hold \u0000.  Names and
 * classes may not.
 *
 * ORDER, so that two saves of one tree are byte-identical: the roots in the order above, each key
 * before its subkeys, subkeys in name order, a key's values in creation order.  A seeded key or
 * value that the guest has not changed is left out; so is a key whose only content is that.
 * The text of the file is therefore not a function of the machine description.
 *
 * TEST HOOKS (rt_registry_path, rt_registry_test_reload) are at the end.
 */
#ifndef RT_REGISTRY_H
#define RT_REGISTRY_H

#include "rt/wintypes.h"
#include "rt/win/base.h"        /* FILETIME, SECURITY_ATTRIBUTES */

#include "rt/win/error.h"
#include "rt/win/registry.h"    /* HKEY_*, REG_*, KEY_*, REGSAM */

/* Limits (see NAMES above). */
#define RT_REG_KEY_NAME_MAX      255u       /* characters of one key name */
#define RT_REG_VALUE_NAME_MAX    16383u     /* characters of a value name */
#define RT_REG_SUBKEY_MAX        32767u     /* characters of the subkey string of one call */
#define RT_REG_VALUE_DATA_MAX    1048576u   /* bytes of one value's data */

/* ---- start-up ------------------------------------------------------------------------------ */

/* Called by rt_init (its place is after rt_env_init and before rt_locale_init; this definition
 * replaces the weak no-op of core/init.c by being linked).  Reads <state_dir>/registry.txt if
 * it exists, adds the seed, deletes a stale registry.txt.tmp.  A file that does not parse is
 * fatal and the report names the line.  Fatal if called twice, or if the state directory is
 * missing from the configuration.  Writes nothing. */
void rt_registry_init(void);

/* ---- keys ---------------------------------------------------------------------------------- */

/* RegOpenKeyExW.  Opens the existing key `subkey` below `parent` (a predefined key or an open
 * key) and stores a new handle in *result.  `subkey` NULL or empty: a new handle to the key
 * `parent` itself - except that if `parent` is a predefined key, *result is `parent` exactly as
 * passed (the documentation: "the same handle passed into the function").  `options` is 0 or
 * REG_OPTION_OPEN_LINK (there are no links, so it changes nothing); any other value is fatal.
 * `sam` is the access mask (see ACCESS above).  *result is written only on success.
 *   ERROR_INVALID_PARAMETER   result is NULL
 *   ERROR_INVALID_HANDLE      parent is not a predefined key nor an open key handle
 *   ERROR_BAD_PATHNAME        a leading backslash (NAMES), except under HKEY_CLASSES_ROOT
 *   ERROR_KEY_DELETED         parent was deleted
 *   ERROR_FILE_NOT_FOUND      some name of the path is not there (logged once)
 * checked in that order.  Fatal: options other than 0 and REG_OPTION_OPEN_LINK; an undocumented
 * bit in sam; a predefined handle other than the six the tree models; a path beyond a limit or
 * of a shape NAMES makes fatal; a name with a code unit whose case is not covered. */
LSTATUS rt_reg_open_key_ex_w(HKEY parent, LPCWSTR subkey, DWORD options, REGSAM sam, PHKEY result);
/* RegOpenKeyExA: the same, `subkey` in the ANSI code page. */
LSTATUS rt_reg_open_key_ex_a(HKEY parent, LPCSTR subkey, DWORD options, REGSAM sam, PHKEY result);

/* RegCreateKeyExW.  Opens `subkey` below `parent`, creating it and any missing parents on the
 * way, and stores a new handle in *result and REG_CREATED_NEW_KEY or REG_OPENED_EXISTING_KEY
 * in *disposition (if not NULL).  `reserved` must be 0.  `cls` is the key's class (a string
 * the key keeps, returned by enumeration and by RegQueryInfoKey); it is used only when the key
 * is created, and NULL or empty means none.  `options` is REG_OPTION_NON_VOLATILE (0) or
 * REG_OPTION_VOLATILE (a volatile key and everything below it lives until the process ends and
 * is not written to the file), alone or with REG_OPTION_OPEN_LINK (ignored, as in open); the
 * options of a call that finds the key existing do not change it.  A key that is created
 * non-volatile below a volatile key is ERROR_CHILD_MUST_BE_VOLATILE.  `sa`: NULL, or a structure
 * whose lpSecurityDescriptor is NULL (bInheritHandle means nothing here); a security
 * descriptor is fatal.  Creating a key changes the last write time of its parent.  Saves the
 * file if the key is not volatile.  `subkey` NULL is treated as empty (the documentation says it
 * cannot be NULL; UNSURE what Windows does), and empty is a new handle to `parent` itself with
 * REG_OPENED_EXISTING_KEY (and for a predefined `parent`, a new handle, not the predefined one
 * - unlike open; the documentation says so).
 *   ERROR_INVALID_PARAMETER   result is NULL
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED   as for open
 *   ERROR_BAD_PATHNAME        as for open
 *   ERROR_ACCESS_DENIED       the key must be created and the parent is HKEY_LOCAL_MACHINE or
 *                             HKEY_USERS itself
 *   ERROR_CHILD_MUST_BE_VOLATILE
 * Fatal: a nonzero `reserved`; options with a bit other than the two above and OPEN_LINK
 * (REG_OPTION_CREATE_LINK and REG_OPTION_BACKUP_RESTORE included); the fatal cases of open. */
LSTATUS rt_reg_create_key_ex_w(HKEY parent, LPCWSTR subkey, DWORD reserved, LPCWSTR cls, DWORD options,
                               REGSAM sam, const SECURITY_ATTRIBUTES *sa, PHKEY result, LPDWORD disposition);
/* RegCreateKeyExA: the same, `subkey` and `cls` in the ANSI code page. */
LSTATUS rt_reg_create_key_ex_a(HKEY parent, LPCSTR subkey, DWORD reserved, LPCSTR cls, DWORD options,
                               REGSAM sam, const SECURITY_ATTRIBUTES *sa, PHKEY result, LPDWORD disposition);

/* RegCloseKey.  Closes an open key handle.  A predefined key: ERROR_SUCCESS and nothing happens.
 * ERROR_INVALID_HANDLE for NULL, for a handle already closed, and for a handle of another kind
 * (an event, say).  Handles to a deleted key can be closed. */
LSTATUS rt_reg_close_key(HKEY key);

/* RegFlushKey.  ERROR_SUCCESS: every change is already on disk (see PERSISTENCE).
 * ERROR_INVALID_HANDLE as for close; ERROR_KEY_DELETED for a deleted key. */
LSTATUS rt_reg_flush_key(HKEY key);

/* RegDeleteKeyExW (RegDeleteKeyW is this with sam 0 and reserved 0).  Deletes the key `subkey`
 * of `parent`.  The key must have no subkeys (Windows 7 reports ERROR_ACCESS_DENIED if it has
 * some).  Handles that are open to it stay valid but are ERROR_KEY_DELETED.  Changes the last
 * write time of the key's parent.  The handle's access rights do not matter (the documentation:
 * they "do not affect the delete operation").  `sam` may carry only the two WOW64 flags, which
 * name the same tree; `reserved` must be 0.
 *   ERROR_INVALID_PARAMETER   subkey is NULL (the documentation: it cannot be NULL)
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED   as for open
 *   ERROR_FILE_NOT_FOUND      no such key (logged once)
 *   ERROR_ACCESS_DENIED       the key has subkeys
 * Fatal: an empty subkey (Windows NT deletes the key `parent` itself; not modelled); a subkey with
 * a backslash at either end or two together (NAMES); a nonzero reserved or a sam with other bits;
 * deleting a seeded key (see THE SEED). */
LSTATUS rt_reg_delete_key_ex_w(HKEY parent, LPCWSTR subkey, REGSAM sam, DWORD reserved);
LSTATUS rt_reg_delete_key_ex_a(HKEY parent, LPCSTR subkey, REGSAM sam, DWORD reserved);

/* SHDeleteKeyW/A(hkey, pszSubKey) of shlwapi (the page: "Deletes a subkey and all its
 * descendants"): delete the key `subkey` of `parent` together with every key below it and
 * all their values.  It is the delete that rt_reg_delete_key_ex_* refuses, a key that has
 * subkeys; a key without subkeys is deleted as there.
 * ALL OR NOTHING: everything is checked first, then the whole tree goes at once, and the
 * file is saved ONCE, after the last key (PERSISTENCE: a stop or a power cut leaves the file
 * with the whole tree or without it).  Every key of the tree becomes a deleted key: handles
 * that are open to any of them stay valid and are ERROR_KEY_DELETED (DELETED KEYS).  The last
 * write time of the deleted key's parent changes.  The access rights of `parent` do not
 * matter, as for rt_reg_delete_key_ex_*; `subkey` may be a path of several names.
 *   ERROR_SUCCESS             the key and everything below it are gone (Wine
 *                             shlwapi/tests/shreg.c test_SHDeleteKey: a key with two subkeys
 *                             is deleted, and opening it afterwards is ERROR_FILE_NOT_FOUND)
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED   as for open
 *   ERROR_FILE_NOT_FOUND      no such key (logged once).  The page promises only "a nonzero
 *                             error code defined in Winerror.h"; this code is the one every
 *                             function of this header gives for a key that is not there
 * Fatal: a NULL or empty subkey (the page: "The address of a null-terminated string
 * specifying the name of the key to delete"; it does not say what Windows deletes without
 * one, and Wine's test calls it so without looking at the result); a seeded key anywhere in
 * the tree, found before anything is deleted (see THE SEED: it would come back at the next
 * start); the fatal cases of a subkey string (NAMES).
 * Sources: Microsoft's page of SHDeleteKeyA (shlwapi.h) and Wine's shlwapi/tests/shreg.c,
 * read 2026-10-10. */
LSTATUS rt_reg_delete_tree_w(HKEY parent, LPCWSTR subkey);
LSTATUS rt_reg_delete_tree_a(HKEY parent, LPCSTR subkey);

/* ---- values -------------------------------------------------------------------------------- */

/* RegQueryValueExW.  Reads the value `name` of `key` (NULL or empty: the default value) and
 * stores its type in *type (if not NULL), its data in `data` and its size in bytes in *cb.
 * `reserved` must be NULL.
 *   - data NULL: nothing is copied; if cb is not NULL the size is stored in *cb.  So
 *     (NULL, NULL) asks only whether the value exists, and (NULL, &n) asks for the size.
 *   - data not NULL needs cb not NULL; *cb is the size of the buffer in bytes.  If the data
 *     fits (exactly or with room to spare) it is copied and *cb becomes its size.  If not, the
 *     result is ERROR_MORE_DATA, *cb becomes the size needed, *type is stored, and the buffer is
 *     not touched (the documentation: its contents are undefined).
 *   - A string type (REG_SZ, REG_EXPAND_SZ, REG_MULTI_SZ) whose stored data does not end with a
 *     UTF-16 NUL, when `data` has room for two more bytes after it, is followed by one NUL; *cb
 *     does not count it.  (Wine's tests say Windows NT does this; UNSURE for Windows 7.  The data
 *     in *cb is exactly as stored either way.)
 *   - A failure other than ERROR_MORE_DATA leaves *type, *cb and the buffer alone.
 *   ERROR_INVALID_PARAMETER   reserved is not NULL; data is not NULL and cb is NULL
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED
 *   ERROR_ACCESS_DENIED       the key lacks KEY_QUERY_VALUE
 *   ERROR_FILE_NOT_FOUND      no such value (logged once)
 *   ERROR_MORE_DATA
 * checked in that order. */
LSTATUS rt_reg_query_value_ex_w(HKEY key, LPCWSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cb);
/* RegQueryValueExA: the same with `name` in the ANSI code page; the data of a string type is
 * converted to the ANSI code page and *cb counts the BYTES OF THE CONVERTED TEXT (a size query
 * converts to measure).  The NUL rule above applies to one byte.  Fatal: a string type whose
 * stored size is odd; a character the code page cannot show. */
LSTATUS rt_reg_query_value_ex_a(HKEY key, LPCSTR name, LPDWORD reserved, LPDWORD type, LPBYTE data, LPDWORD cb);

/* RegSetValueExW.  Creates or replaces the value `name` (NULL or empty: the default value)
 * with the given type and the `cb` bytes at `data`.  `reserved` must be 0.  `data` may be NULL
 * only with cb 0 (an empty value).  An existing value keeps its spelling and its place in the
 * order of values.  Changes the key's last write time and saves the file, except that writing a
 * value with the type and data it already has does neither.
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED
 *   ERROR_ACCESS_DENIED       the key lacks KEY_SET_VALUE; or is HKEY_LOCAL_MACHINE or HKEY_USERS
 * Fatal: a nonzero reserved; data NULL with cb nonzero; cb above RT_REG_VALUE_DATA_MAX; a
 * name beyond RT_REG_VALUE_NAME_MAX or with a code unit whose case is not covered. */
LSTATUS rt_reg_set_value_ex_w(HKEY key, LPCWSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD cb);
/* RegSetValueExA: the same with `name` in the ANSI code page; the data of a string type is
 * converted from the ANSI code page to UTF-16 (cb bytes become cb code units, no terminator
 * added or removed); other types are stored as given. */
LSTATUS rt_reg_set_value_ex_a(HKEY key, LPCSTR name, DWORD reserved, DWORD type, const BYTE *data, DWORD cb);

/* RegDeleteValueW.  Deletes the value `name` (NULL or empty: the default value).
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED
 *   ERROR_ACCESS_DENIED       the key lacks KEY_SET_VALUE
 *   ERROR_FILE_NOT_FOUND      no such value (logged once)
 * Fatal: deleting a seeded value (see THE SEED). */
LSTATUS rt_reg_delete_value_w(HKEY key, LPCWSTR name);
LSTATUS rt_reg_delete_value_a(HKEY key, LPCSTR name);

/* ---- enumeration --------------------------------------------------------------------------- */

/* RegEnumValueW.  The value number `index` (from 0) of `key`, in creation order.  `name` and
 * `cch_name` are required: *cch_name is the buffer's size in characters INCLUDING the NUL, and
 * on success the number of characters stored, not counting it.  `data` and `cb`, `type` as for
 * a query (both optional: data NULL asks nothing, and data without cb is
 * ERROR_INVALID_PARAMETER).  `reserved` must be NULL.
 *   ERROR_INVALID_PARAMETER   reserved not NULL; name or cch_name NULL; data without cb
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED
 *   ERROR_ACCESS_DENIED       the key lacks KEY_QUERY_VALUE
 *   ERROR_NO_MORE_ITEMS       index is not less than the number of values
 *   ERROR_MORE_DATA           the name does not fit (nothing is written; *cch_name is left as it
 *                             was) or the data does not fit (the name is stored, *cb and *type are)
 * The name is stored before the data is looked at.  UNSURE: whether Windows updates *cch_name
 * when the name does not fit. */
LSTATUS rt_reg_enum_value_w(HKEY key, DWORD index, LPWSTR name, LPDWORD cch_name, LPDWORD reserved,
                            LPDWORD type, LPBYTE data, LPDWORD cb);
LSTATUS rt_reg_enum_value_a(HKEY key, DWORD index, LPSTR name, LPDWORD cch_name, LPDWORD reserved,
                            LPDWORD type, LPBYTE data, LPDWORD cb);

/* RegEnumKeyExW.  The subkey number `index` of `key`, in name order: its name in `name` (and
 * *cch_name as for values), its class in `cls` (*cch_cls is the buffer's size including the NUL,
 * on success the length; a key without a class gives the empty string) and its last write time in
 * *last_write.  `cls`, `cch_cls`, `last_write` are optional; `cls` without `cch_cls` is
 * ERROR_INVALID_PARAMETER.
 *   ERROR_INVALID_PARAMETER, ERROR_INVALID_HANDLE, ERROR_KEY_DELETED
 *   ERROR_ACCESS_DENIED       the key lacks KEY_ENUMERATE_SUB_KEYS
 *   ERROR_NO_MORE_ITEMS
 *   ERROR_MORE_DATA           the name or the class does not fit (nothing is written for what
 *                             does not fit; the lengths are left as they were)
 * checked in that order. */
LSTATUS rt_reg_enum_key_ex_w(HKEY key, DWORD index, LPWSTR name, LPDWORD cch_name, LPDWORD reserved,
                             LPWSTR cls, LPDWORD cch_cls, PFILETIME last_write);
LSTATUS rt_reg_enum_key_ex_a(HKEY key, DWORD index, LPSTR name, LPDWORD cch_name, LPDWORD reserved,
                             LPSTR cls, LPDWORD cch_cls, PFILETIME last_write);

/* RegQueryInfoKeyW.  Every output pointer is optional.  `cls` and `cch_cls` give the class of
 * this key as for enumeration, except that on ERROR_MORE_DATA *cch_cls is the length needed
 * (UNSURE: what Windows does).  `n_subkeys`, `n_values`: counts.  `max_subkey_len`: the
 * longest subkey name in characters (no NUL); `max_class_len`: the longest class of a subkey, in
 * characters; `max_value_name_len`: the longest value name in characters; `max_value_len`: the
 * largest value data in bytes.  `last_write`: the key's last write time.  The A form reports
 * lengths in the converted text's characters, which for the ANSI code page are the same
 * numbers, and its class in the ANSI code page.  `sd_len` (the size of the security
 * descriptor) is not modelled: a non-NULL pointer is fatal.
 *   ERROR_INVALID_PARAMETER   reserved not NULL; cls without cch_cls
 *   ERROR_INVALID_HANDLE, ERROR_KEY_DELETED
 *   ERROR_ACCESS_DENIED       the key lacks KEY_QUERY_VALUE
 *   ERROR_MORE_DATA          the class does not fit */
LSTATUS rt_reg_query_info_key_w(HKEY key, LPWSTR cls, LPDWORD cch_cls, LPDWORD reserved,
                                LPDWORD n_subkeys, LPDWORD max_subkey_len, LPDWORD max_class_len,
                                LPDWORD n_values, LPDWORD max_value_name_len, LPDWORD max_value_len,
                                LPDWORD sd_len, PFILETIME last_write);
LSTATUS rt_reg_query_info_key_a(HKEY key, LPSTR cls, LPDWORD cch_cls, LPDWORD reserved,
                                LPDWORD n_subkeys, LPDWORD max_subkey_len, LPDWORD max_class_len,
                                LPDWORD n_values, LPDWORD max_value_name_len, LPDWORD max_value_len,
                                LPDWORD sd_len, PFILETIME last_write);

/* ---- test hooks: not for shims -------------------------------------------------------------- */

/* The host path of the registry file (<state_dir>/registry.txt).  Valid after rt_init. */
const char *rt_registry_path(void);

/* Throw the tree away and read it again from the file exactly as start-up does: the file, the
 * seed, the stale .tmp removed.  Every open key handle becomes a deleted key (ERROR_KEY_DELETED).
 * Warnings already logged for missing names are not logged again.  This is what a restart of the
 * runtime would find, and it stops the runtime, as start-up does, on a file that does not parse. */
void rt_registry_test_reload(void);

#endif /* RT_REGISTRY_H */
