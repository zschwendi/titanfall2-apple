/* Files, directories, standard handles and file mappings (docs/RUNTIME.md section 7.3).
 *
 * The comments are the specification.  Path translation is not here: rt/path.h (sections 7.1
 * and 7.2) turns a Windows name into a host path, case folding included, and every function
 * below that takes a name goes through it.
 *
 * WHERE THE BODIES ARE (docs/handoff/rt-contract-file.md has the state of each piece):
 *     core/file.c       BUILT   the file object and its lifetime, rt_file_init, rt_file_create
 *     core/file_io.c    stub    rt_file_read, _write, _seek, _flush, _size, _set_eof
 *     core/file_ov.c    BUILT   the OVERLAPPED forms, rt_file_overlapped_result, rt_file_read_ex
 *     core/file_attr.c  stub    rt_file_get_attributes[_ex], _set_attributes, _info_by_handle, _set_times
 *     core/file_dir.c   stub    rt_file_delete, rt_file_move, rt_dir_create, rt_dir_remove, rt_drive_type,
 *                               rt_disk_space
 *     core/file_find.c  stub    rt_find_first_w/_a, rt_find_next_w/_a, rt_find_close
 *     core/file_std.c   stub    rt_std_get, rt_std_set, rt_file_type, rt_console_ctrl_handler
 *     core/file_map.c   BUILT   rt_section_create, rt_section_open, rt_view_map, rt_view_unmap
 * A stub ends the run with "unimplemented runtime core function" (rt/log.h); none returns a
 * default.
 *
 * WHICH FUNCTION A SHIM CALLS.  One call each; the function does the translation, the handle
 * lookup, the work and the last error.  Parameters the functions do not take are the shim's
 * to ignore: security attributes and the inherit flag (section 4.1).
 *
 *     CreateFileW/A            rt_file_create           GetFileAttributesW/A     rt_file_get_attributes
 *     CloseHandle              rt_handle_close          GetFileAttributesExW/A   rt_file_get_attributes_ex
 *     ReadFile                 rt_file_read             SetFileAttributesW/A     rt_file_set_attributes
 *     WriteFile                rt_file_write            GetFileInformationByHandle  rt_file_info_by_handle
 *     SetFilePointerEx         rt_file_seek             SetFileTime              rt_file_set_times
 *     SetFilePointer           rt_file_seek (*)         DeleteFileW/A            rt_file_delete
 *     FlushFileBuffers         rt_file_flush            MoveFileW/A, MoveFileExW rt_file_move
 *     GetFileSizeEx            rt_file_size             CreateDirectoryW/A       rt_dir_create
 *     GetFileSize              rt_file_size (*)         RemoveDirectoryW/A       rt_dir_remove
 *     SetEndOfFile             rt_file_set_eof          GetDriveTypeW/A          rt_drive_type
 *                                                       GetDiskFreeSpace[Ex]W/A  rt_disk_space
 *     GetOverlappedResult      rt_file_overlapped_result
 *     ReadFileEx               rt_file_read_ex          FindFirstFileW / A       rt_find_first_w / _a
 *     GetStdHandle             rt_std_get               FindNextFileW / A        rt_find_next_w / _a
 *     SetStdHandle             rt_std_set               FindClose                rt_find_close
 *     GetFileType              rt_file_type             CreateFileMappingW/A     rt_section_create
 *     SetConsoleCtrlHandler    rt_console_ctrl_handler  OpenFileMappingW/A       rt_section_open
 *                                                       MapViewOfFile            rt_view_map
 *                                                       UnmapViewOfFile          rt_view_unmap
 *     (*) the shim adapts the 32-bit protocol; the rule is in the handoff, "shims this unlocks".
 * GetCurrentDirectory, SetCurrentDirectory and GetFullPathName call rt/path.h directly.  The
 * other console functions call nothing: there is no console (STANDARD HANDLES below).
 *
 * NAMES.  A function that takes a file name takes an RtName: the guest's pointer, wide or
 * narrow, as it came.  A W shim passes RT_NAME_W(lpFileName), an A shim RT_NAME_A(lpFileName);
 * the bytes of an A name are code page 1252 (rt_path_to_host_a).  No shim converts or looks
 * at a name.  Exactly one member is set.  Both NULL (the guest passed NULL) is answered as
 * rt_path_to_host_w(NULL) answers it: ERROR_INVALID_PARAMETER (Windows reports
 * ERROR_PATH_NOT_FOUND for CreateFileW(NULL) as far as remembered; not verified.  For
 * DeleteFile and CreateDirectory it is verified, and those two answer ERROR_PATH_NOT_FOUND:
 * rt_file_delete, rt_dir_create).  Both set is
 * a fatal report.  A name is translated ONCE per call, with the flags the function states;
 * every error of the translation (ERROR_PATH_NOT_FOUND, ERROR_FILE_NOT_FOUND,
 * ERROR_INVALID_NAME, ERROR_FILENAME_EXCED_RANGE, ERROR_BAD_PATHNAME, ERROR_ACCESS_DENIED,
 * ERROR_INVALID_PARAMETER) becomes the function's failure with that code, before anything
 * else is looked at except where a function says "checked first".
 *
 * LAST ERROR: the convention of rt/handle.h (runtime/README.md, first row).  Every function
 * that fails sets the calling thread's last error itself, through rt_thread_set_last_error,
 * and says which codes; a shim returns the failure value and sets nothing.  None touches it
 * on success, with ONE exception: rt_file_create and rt_section_create set it on success too
 * (and rt_file_read_ex, to 0, as ReadFileEx is documented to).
 * A failed host call is mapped by rt_win32_from_errno(errno, RT_ERR_FILE), or RT_ERR_DIR where
 * the function says "directory call"; "mapped" below means that, and the codes it can give
 * are not repeated at each function.
 *
 * THREADS.  Every function may be called from any thread at any time: a guest thread, or a
 * host thread that is not one (the functions need no Ctx, except rt_file_read_ex; on a host
 * thread last error is the host thread's own, rt/thread.h).  NEVER call one with the
 * dispatcher lock held, or with any other lock of the runtime that must not be held across a
 * blocking host call: they open, read, write and sync.
 *
 * LOCKS.  The file layer has one host lock of its own, the FILE LOCK (an os_unfair_lock
 * private to core/file.c; the other pieces reach what it guards only through the functions
 * of core/file_priv.h).  It guards the list of open files (the sharing log) and nothing
 * else; it is a leaf: held for a few instructions, never across a host call that can block,
 * a log line, an allocation or a call out of core/file.c.  A find handle has a lock of its own for its cursor; file_std.c and
 * file_map.c each have one for their lists.  None of them is ever held together with another.
 * WHERE A FILE DIES: the last reference (rt_handle_close of the last handle, or the
 * rt_object_unref of an operation in flight) closes the host descriptor, and unlinks for
 * FILE_FLAG_DELETE_ON_CLOSE, on the calling thread; it takes the file lock, so no reference
 * to a file is dropped while holding it.
 *
 * ONE HANDLE, SEVERAL THREADS.  A file object has one position, the host descriptor's, shared
 * by every handle to the object (DuplicateHandle gives a handle to the same object; a second
 * CreateFile gives a new object with its own position).  A read or a write without an
 * OVERLAPPED is ONE host call that transfers and advances the position atomically, so
 * threads reading one handle get distinct, adjacent pieces and never the same bytes twice.
 *
 * "STOPS" below means the unimplemented report (rt_fatal with the offending value): the run
 * ends.  It is used wherever what Windows does was not verified; "not verified" says so.
 * Nothing here was checked on a Windows machine: the sources are the Microsoft documentation
 * pages of the functions named and Wine's kernelbase as remembered.
 */
#ifndef RT_FILE_H
#define RT_FILE_H

#include "runtime.h"
#include "rt/wintypes.h"
#include "rt/handle.h"
#include "rt/win/base.h"
#include "rt/win/error.h"
#include "rt/win/file.h"

/* A file name as the guest gave it (NAMES above). */
typedef struct RtName { const WCHAR *w; const char *a; } RtName;
#define RT_NAME_W(p)  ((RtName){ .w = (p), .a = NULL })
#define RT_NAME_A(p)  ((RtName){ .w = NULL, .a = (p) })

/* dwVolumeSerialNumber of every file: fixed, so that runs are comparable. */
#define RT_VOLUME_SERIAL  0x52325446u

/* ---- open (core/file.c, built) -------------------------------------------------------------- */

/* CreateFileW / CreateFileA(lpFileName, dwDesiredAccess, dwShareMode, security,
 * dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile).  Returns a new handle of kind
 * RT_K_FILE, or INVALID_HANDLE_VALUE with last error.  In this order:
 *
 *  1. STOPS (not verified what Windows answers, or not built):
 *       template_file not NULL;
 *       share with a bit outside FILE_SHARE_READ | _WRITE | _DELETE;
 *       access with a bit outside GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL
 *         | DELETE | READ_CONTROL | SYNCHRONIZE | 0x1FF (the file-specific rights);
 *       access with FILE_APPEND_DATA and none of GENERIC_WRITE, GENERIC_ALL, FILE_WRITE_DATA
 *         (an append-only handle);
 *       flags_attrs with a bit outside the attributes READONLY | HIDDEN | SYSTEM | ARCHIVE |
 *         NORMAL | TEMPORARY | NOT_CONTENT_INDEXED and the flags WRITE_THROUGH | OVERLAPPED |
 *         NO_BUFFERING | RANDOM_ACCESS | SEQUENTIAL_SCAN | DELETE_ON_CLOSE | BACKUP_SEMANTICS.
 *  2. ERROR_INVALID_PARAMETER: disposition not 1 to 5; TRUNCATE_EXISTING without write access
 *     (the documentation requires GENERIC_WRITE; the code is what Windows gives for access 0,
 *     GENERIC_READ and FILE_WRITE_DATA alone: Wine's kernel32/tests/file.c test_CreateFile).
 *  3. The name is translated: RT_PATH_EXISTING for OPEN_EXISTING and TRUNCATE_EXISTING,
 *     RT_PATH_CREATE for the other three.  (Never RT_PATH_DELETE: a create cannot remove a
 *     directory of the drive's skeleton, and the flag would refuse NUL.)
 *  4. The host open.  The handle can read if access has GENERIC_READ, GENERIC_ALL or
 *     FILE_READ_DATA, and write if it has GENERIC_WRITE, GENERIC_ALL or FILE_WRITE_DATA; with
 *     neither it is good for attributes and sizes only.
 *       CREATE_NEW         create; the name exists: ERROR_FILE_EXISTS
 *       CREATE_ALWAYS      create, or open and truncate to 0
 *       OPEN_EXISTING      open; no such file: ERROR_FILE_NOT_FOUND
 *       OPEN_ALWAYS        open, or create
 *       TRUNCATE_EXISTING  open and truncate to 0; no such file: ERROR_FILE_NOT_FOUND
 *     Whether the file existed is decided without a race: two threads that create one name
 *     with CREATE_NEW get exactly one handle; with OPEN_ALWAYS or CREATE_ALWAYS both succeed
 *     and exactly one is told the file is new.  A new file has host mode 0644, or 0444 with
 *     FILE_ATTRIBUTE_READONLY, and the spelling the caller gave; the cached listing of its
 *     directory is dropped (rt_path_invalidate).  The other attribute bits are not stored.
 *     A host failure is mapped: a read-only file opened for writing or with CREATE_ALWAYS,
 *     and a directory opened for writing, are ERROR_ACCESS_DENIED.
 *  5. What was opened.  A directory: a handle only with FILE_FLAG_BACKUP_SEMANTICS (it then
 *     serves rt_file_info_by_handle and nothing that reads or writes), otherwise
 *     ERROR_ACCESS_DENIED.  NUL (/dev/null): a character device; reads give end of file,
 *     writes are discarded; no disposition creates or truncates anything; with CREATE_NEW it
 *     STOPS (not verified).  A host object that is neither a regular file nor a directory
 *     STOPS.
 *  6. Flags.  OVERLAPPED is recorded (rt_file_read).  DELETE_ON_CLOSE: the name is unlinked
 *     when THIS object dies, whatever else has the file open (a failing unlink is logged).
 *     NO_BUFFERING turns the host cache off for the descriptor; SEQUENTIAL_SCAN and
 *     RANDOM_ACCESS turn read-ahead on and off; alignment rules are not enforced.
 *     WRITE_THROUGH is recorded and does nothing.
 *  7. Sharing modes are RECORDED AND NOT ENFORCED.  If another object has the same host file
 *     open and Windows would refuse this open (the new access against an old share mode, or
 *     an old access against the new share mode, each for read, write and DELETE), one line
 *     containing "sharing violation not enforced" is logged on `file` at level warn, and the
 *     open succeeds.  An open whose access has none of the three (attributes only) takes no
 *     part, on either side, as in the kernel.
 *  8. ERROR_NOT_ENOUGH_MEMORY: no memory for the object, or the handle table is full.
 *
 * LAST ERROR ON SUCCESS: always set.  ERROR_ALREADY_EXISTS if the disposition is CREATE_ALWAYS
 * or OPEN_ALWAYS and the file existed (documented); otherwise 0 (documented for those two
 * dispositions when the file is new; for the other three it is what kernelbase does as
 * remembered, not verified).  NUL counts as not existing (not verified).
 * Takes the file lock (the list of open files).  Fatal: a call before rt_file_init. */
HANDLE rt_file_create(RtName name, DWORD access, DWORD share, DWORD disposition,
                      DWORD flags_attrs, HANDLE template_file);

/* How many open files that can be written have their host path in the host directory `dir`
 * or below it (both sides through realpath; 0 for a directory that does not exist).  `first`
 * (may be NULL) gets the host path of one of them, "" when there is none.  For a driver that
 * ends a run by itself and wants a moment at which nothing under a directory is half written
 * (gate3/run.c, THE STATE).  A file of the null device, the standard handles and a mapping
 * have no host path and are not counted.  Takes the file lock. */
unsigned rt_file_writers_under(const char *dir, char *first, size_t n);

/* The same count, without waiting: -1 when another thread holds the file lock (then nothing is
 * counted and `first` is ""), else the count.  For a caller that may run on a signal stack, and
 * must not wait for a lock the stopped thread may hold (gate3/run.c, run_end_lines). */
int rt_file_writers_under_try(const char *dir, char *first, size_t n);

/* ---- read, write, position (core/file_io.c) ------------------------------------------------- */

/* ReadFile(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead, lpOverlapped).
 * In this order:
 *  1. got NULL and ov NULL: STOPS (Windows faults or fails; not verified).
 *  2. If got is not NULL, *got = 0, before anything else can fail (documented).
 *  3. `h` is not an open file handle (NULL, INVALID_HANDLE_VALUE, closed, another kind):
 *     FALSE, ERROR_INVALID_HANDLE.
 *  4. ov not NULL: the result of rt_file_ov_read (core/file_ov.c; OVERLAPPED below).
 *  5. A directory handle: STOPS (not verified; before the access test, which a directory
 *     handle would always fail: rt_file_create gives it neither read nor write access).
 *  6. The handle cannot read (rt_file_create, step 4): FALSE, ERROR_ACCESS_DENIED.
 *     Then: a disk file's handle that was opened with FILE_FLAG_OVERLAPPED (ov is NULL
 *     here): FALSE, ERROR_INVALID_PARAMETER, whatever len is; nothing is read and the
 *     position stays (documented: "The lpOverlapped parameter must not be NULL"; the code
 *     is what Windows gives by Wine's ntdll/tests/file.c test_read_write).
 *  7. len 0: TRUE, nothing is read, no host call.
 *  8. One host read of len bytes at the position (ONE HANDLE, SEVERAL THREADS), repeated only
 *     if a signal interrupted it before any byte.  n >= 0 bytes came: *got = n, TRUE.  n is
 *     smaller than len only at the end of the file, and 0 there: END OF FILE IS TRUE WITH
 *     *got == 0, not an error (documented).  A host failure: FALSE, mapped (a buffer the
 *     kernel refuses is ERROR_NOACCESS).  A len above 0x40000000 is read in pieces of that
 *     size, stopping at the first short piece; an error after the first piece is TRUE with
 *     the bytes so far. */
BOOL rt_file_read(HANDLE h, void *buf, DWORD len, DWORD *got, OVERLAPPED *ov);

/* WriteFile.  Steps 1 to 4 as rt_file_read (with rt_file_ov_write), then:
 *  5. A directory handle: STOPS (before the access test, as in rt_file_read).
 *  6. The handle cannot write: FALSE, ERROR_ACCESS_DENIED.  Then, as rt_file_read: a disk
 *     file's handle opened with FILE_FLAG_OVERLAPPED and no ov: FALSE,
 *     ERROR_INVALID_PARAMETER, nothing written.
 *  7. len 0: TRUE, *put = 0, no host call: the file is neither truncated nor extended.
 *  8. Host writes at the position until len bytes are written (a regular file takes them in
 *     one call; a character device may take them in several).  TRUE with *put = len.  A host
 *     failure: FALSE, mapped (a full disk is ERROR_DISK_FULL), and *put is the number of
 *     bytes written before it (what Windows stores then is not verified).
 * Writing beyond the end extends the file; the gap left by a seek reads as zeros.  Bytes
 * written to a character device (a standard handle, NUL) go to its host descriptor as they
 * are. */
BOOL rt_file_write(HANDLE h, const void *buf, DWORD len, DWORD *put, OVERLAPPED *ov);

/* SetFilePointerEx(hFile, liDistanceToMove, lpNewFilePointer, dwMoveMethod).
 *   method    FILE_BEGIN, FILE_CURRENT or FILE_END; anything else: FALSE,
 *             ERROR_INVALID_PARAMETER (Wine's code; not verified)
 *   new_pos   may be NULL; receives the new position on success and is untouched on failure
 * Moves the position by ONE host call (lseek with the matching whence), so that it is atomic
 * against reads and writes of other threads.  A position before the start of the file: FALSE,
 * ERROR_NEGATIVE_SEEK, and the position is unchanged (the host refuses with EINVAL, which
 * for this call means exactly that).  Any other host failure is mapped.  A position beyond
 * the end is allowed and does not change the file.
 * FALSE with ERROR_INVALID_HANDLE as rt_file_read step 3.  A character device or a directory
 * handle STOPS (not verified). */
BOOL rt_file_seek(HANDLE h, LONGLONG distance, LONGLONG *new_pos, DWORD method);

/* FlushFileBuffers: fsync on the descriptor (not F_FULLFSYNC).  TRUE, or FALSE with
 * ERROR_INVALID_HANDLE (step 3), ERROR_ACCESS_DENIED (the handle cannot write: documented), or
 * the mapped host failure.  A character device or a directory handle STOPS (Windows fails
 * for a console and is not verified for other devices). */
BOOL rt_file_flush(HANDLE h);

/* GetFileSizeEx: *size = the file's size now (fstat).  `size` NULL STOPS.  TRUE, or FALSE with
 * ERROR_INVALID_HANDLE or the mapped host failure.  A character device or a directory handle
 * STOPS (not verified). */
BOOL rt_file_size(HANDLE h, LONGLONG *size);

/* SetEndOfFile: the file's size becomes the handle's position (ftruncate); a longer file
 * reads as zeros in the new part.  The position does not move.  TRUE, or FALSE with
 * ERROR_INVALID_HANDLE, ERROR_ACCESS_DENIED (the handle cannot write) or the mapped host
 * failure.  A character device or a directory handle STOPS. */
BOOL rt_file_set_eof(HANDLE h);

/* ---- OVERLAPPED (core/file_ov.c; an "opus" piece, see the handoff) ---------------------------- */
/* RUNTIME.md 7.3: all overlapped I/O is PERFORMED SYNCHRONOUSLY inside the call.  The piece is
 * built; the first comment of core/file_ov.c says what a program can observe that differs
 * from Windows's asynchronous completion, and what was settled from the documentation.
 *
 * rt_file_read / rt_file_write with ov != NULL, for a handle opened with or without
 * FILE_FLAG_OVERLAPPED:
 *   - a directory handle STOPS (as step 5), and so does a character device (not built);
 *   - the transfer is at the offset ov->Offset | ov->OffsetHigh << 32 (pread, pwrite), all
 *     len bytes or up to the end of the file.  Offset and OffsetHigh are never written
 *     (documented).  An offset with the top bit set STOPS (0xFFFFFFFF:0xFFFFFFFF is the
 *     documented "write at the end of the file": not built);
 *   - ov->Internal = the NTSTATUS (STATUS_SUCCESS, STATUS_END_OF_FILE), ov->InternalHigh =
 *     the bytes transferred; never STATUS_PENDING;
 *   - then, if ov->hEvent is not NULL, that event is SET (rt_event_set_object on the object
 *     behind it; a handle that is not an event STOPS, before anything is transferred);
 *   - the call returns TRUE with *got = the bytes (if got is not NULL), and never FALSE with
 *     ERROR_IO_PENDING: a result Windows itself gives when the data is cached;
 *   - a read of len > 0 that begins at or beyond the end of the file: FALSE,
 *     ERROR_HANDLE_EOF, Internal = STATUS_END_OF_FILE, InternalHigh = 0, the event set
 *     (documented as one of two ways for an overlapped handle).  A handle WITHOUT the
 *     flag answers the same, and its position stays (Wine's tests on Windows:
 *     ntdll/tests/file.c test_read_write, kernel32/tests/file.c test_eof);
 *   - len 0: TRUE with 0 bytes, wherever the offset is;
 *   - a handle that cannot read (write): FALSE, ERROR_ACCESS_DENIED.  After this and after a
 *     failing host call (mapped), Internal = STATUS_UNSUCCESSFUL (0xC0000001), InternalHigh
 *     = 0 and the event is set: no old result stays in the OVERLAPPED (not Windows's values);
 *   - a handle WITHOUT the flag has its position moved to offset + bytes after a transfer
 *     that succeeded (documented); a handle with it keeps whatever position it had.
 * rt_file_ov_read and rt_file_ov_write are declared in core/file_priv.h. */

/* GetOverlappedResult(hFile, lpOverlapped, lpNumberOfBytesTransferred, bWait).  Reads the
 * result out of *ov: Internal STATUS_SUCCESS: *transferred = InternalHigh, TRUE.
 * STATUS_END_OF_FILE: *transferred = 0, FALSE, ERROR_HANDLE_EOF.  STATUS_PENDING (which this
 * runtime never stores: the guest's own initial value): wait FALSE: FALSE,
 * ERROR_IO_INCOMPLETE; wait TRUE: STOPS.  Any other status STOPS.  ERROR_INVALID_HANDLE as
 * rt_file_read step 3.  ov or transferred NULL STOPS.  Nothing is waited for and the handle
 * is only checked: the status is the low 32 bits of Internal. */
BOOL rt_file_overlapped_result(HANDLE h, OVERLAPPED *ov, DWORD *transferred, BOOL wait);

/* ReadFileEx(hFile, lpBuffer, nNumberOfBytesToRead, lpOverlapped, lpCompletionRoutine): the
 * game's main data path (docs/findings/30-proton-trace.md: 38,354 calls, one thread).  The
 * read is done as above, hEvent is ignored (documented), and `routine` is queued to the
 * CALLING thread as an APC with the arguments (dwErrorCode 0, bytes, ov), to run at that
 * thread's next alertable wait (rt_apc_queue_args, rt/sync.h); TRUE, and last error is set to
 * 0 (documented: "GetLastError will return ERROR_SUCCESS").  A read at or beyond the end of
 * the file is reported through the routine: TRUE, last error 0, Internal STATUS_END_OF_FILE,
 * InternalHigh 0, and the routine is queued with (ERROR_HANDLE_EOF, 0, ov).  That is Windows
 * Vista and later (the request is pending and its APC runs with STATUS_END_OF_FILE: Wine's
 * ntdll/tests/file.c read_file_test; before Vista the call failed with ERROR_HANDLE_EOF and
 * no routine ran).  A failure queues nothing:
 * ERROR_INVALID_HANDLE, ERROR_ACCESS_DENIED (the handle cannot read), the mapped host
 * failure, ERROR_NOT_ENOUGH_MEMORY (no memory for the APC).  A handle without
 * FILE_FLAG_OVERLAPPED, ov NULL or routine 0: STOPS (not documented, not verified).  `ctx` is
 * the calling guest thread's (NULL is fatal). */
BOOL rt_file_read_ex(Ctx *ctx, HANDLE h, void *buf, DWORD len, OVERLAPPED *ov, GUESTFN routine);

/* ---- game data that is only partly on this machine (core/file_partial.c) ------------------------ */
/* An install file may hold only some of its byte ranges here.  Such a file is NOT in the
 * install tree (every file there is whole): its bytes are a sparse host file of the true
 * size beside the install root, with a map that says which ranges are the install's
 * (core/file_partial.c, first comment; docs/handoff/rt-game-data.md).  The translation of a
 * name leads there, so that for the functions of this header the file is in its place:
 *   - size, attributes, find-file (under the install's spelling): the true file's.  The
 *     times are those of the fetch;
 *   - rt_file_read (with and without an OVERLAPPED) and rt_file_read_ex: a read whose bytes,
 *     up to the end of the file, are all present is served; a read that touches an absent
 *     byte STOPS ("game data: a read touches bytes that are not on this Mac": the guest
 *     path, offset and length, the caller's module+rva, and the command that fetches them).
 *     Never zeros and never a short read;
 *   - rt_file_create with write access or a truncating disposition, rt_file_delete,
 *     rt_file_move, rt_section_create: STOP, with the command that fetches the whole file.
 * And, where the map has a manifest of the install: a name the install has and this machine
 * does not STOPS wherever a name is translated (ERROR_FILE_NOT_FOUND and ERROR_PATH_NOT_FOUND
 * are for names the install does not have either), and so does a rt_find_first_* whose
 * pattern matches install entries that are not here.
 * What does not go through this header never sees a partial file: the loader looks for a DLL
 * in the install tree only (tools/gamedata/fetch.py fetches every DLL whole). */

/* ---- attributes and times (core/file_attr.c) -------------------------------------------------- */
/* ATTRIBUTES, everywhere in this header (one function computes them, rt_file_attr_from_stat
 * of core/file_priv.h): a directory is exactly FILE_ATTRIBUTE_DIRECTORY; a regular file is
 * FILE_ATTRIBUTE_ARCHIVE, plus FILE_ATTRIBUTE_READONLY if its host mode has no owner write
 * bit.  Never FILE_ATTRIBUTE_NORMAL, HIDDEN or SYSTEM.  TIMES are the host's: creation =
 * birth time, last access, last write = modification, as FILETIME
 * (rt_time_filetime_from_unix).  SIZE is the host size for a file and 0 for a directory.
 * Symbolic links are followed.  A host object of any other type STOPS. */

/* GetFileAttributesW/A.  Name translated with RT_PATH_EXISTING.  Returns the attributes, or
 * INVALID_FILE_ATTRIBUTES with last error: the translation's, or the mapped failure of stat.
 * NUL STOPS (not verified). */
DWORD rt_file_get_attributes(RtName name);

/* GetFileAttributesExW/A(lpFileName, fInfoLevelId, lpFileInformation).  level other than
 * GetFileExInfoStandard: FALSE, ERROR_INVALID_PARAMETER, checked first (Wine's code; not
 * verified).  out NULL STOPS.  Otherwise as rt_file_get_attributes, filling all of *out;
 * *out is untouched on failure. */
BOOL rt_file_get_attributes_ex(RtName name, DWORD level, WIN32_FILE_ATTRIBUTE_DATA *out);

/* SetFileAttributesW/A.  attrs with a bit outside READONLY | HIDDEN | SYSTEM | ARCHIVE |
 * NORMAL | TEMPORARY | NOT_CONTENT_INDEXED STOPS, checked first.  Name translated with
 * RT_PATH_EXISTING.  A regular file: READONLY set removes every write bit of the host mode,
 * READONLY clear adds the owner's write bit; the other bits are not stored (section 7.3) and
 * are logged once at debug.  A directory: nothing changes.  TRUE, or FALSE with the
 * translation's error or the mapped failure of stat or chmod. */
BOOL rt_file_set_attributes(RtName name, DWORD attrs);

/* GetFileInformationByHandle.  Fills all of *out from fstat: attributes, times and size as
 * above; dwVolumeSerialNumber = RT_VOLUME_SERIAL; nNumberOfLinks = the host link count for a
 * file, 1 for a directory; nFileIndexHigh/Low = the host inode number.  out NULL STOPS.  A
 * character device STOPS (not verified).  FALSE with ERROR_INVALID_HANDLE or the mapped
 * failure. */
BOOL rt_file_info_by_handle(HANDLE h, BY_HANDLE_FILE_INFORMATION *out);

/* SetFileTime(hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime).  Each pointer may be
 * NULL, and a FILETIME of 0 also means "leave unchanged" (documented).  Access and write
 * times are set with futimens.  A creation time that is given and not 0, a FILETIME of
 * 0xFFFFFFFF:0xFFFFFFFF, and a time before 1970 STOP (not built).  A character device STOPS.
 * The handle's access is not checked.  FALSE with ERROR_INVALID_HANDLE or the mapped failure. */
BOOL rt_file_set_times(HANDLE h, const FILETIME *creation, const FILETIME *access, const FILETIME *write);

/* ---- names: delete, move, directories (core/file_dir.c) --------------------------------------- */

/* DeleteFileW/A.  A NULL name: FALSE, ERROR_PATH_NOT_FOUND (Wine's kernel32/tests/file.c
 * test_DeleteFileW).  Name translated with RT_PATH_EXISTING | RT_PATH_DELETE.  A directory:
 * FALSE, ERROR_ACCESS_DENIED.  A file whose host mode has no owner write bit (read-only):
 * FALSE, ERROR_ACCESS_DENIED, and the file stays (both documented; the host itself would
 * delete it).  Otherwise unlink; failure mapped.  A file that is open stays readable through
 * its handles; if an open of it lacks FILE_SHARE_DELETE the line "sharing violation not
 * enforced" is logged (rt_file_log_if_open) and the delete proceeds.  The directory's cached
 * listing is dropped. */
BOOL rt_file_delete(RtName name);

/* MoveFileW/A (flags 0) and MoveFileExW(lpExistingFileName, lpNewFileName, dwFlags).
 *  1. flags with a bit outside MOVEFILE_REPLACE_EXISTING | _COPY_ALLOWED | _WRITE_THROUGH
 *     STOPS; `to` with both members NULL STOPS (MoveFileEx's delete-at-reboot form).
 *  2. `from` is translated with RT_PATH_EXISTING | RT_PATH_RENAME, then `to` with
 *     RT_PATH_CREATE | RT_PATH_RENAME; the first error is the result.
 *  3. If `to` exists and is not the same host file as `from` (device and inode): without
 *     REPLACE_EXISTING: FALSE, ERROR_ALREADY_EXISTS (documented; a naive rename would
 *     replace).  With it: a directory on either side: FALSE, ERROR_ACCESS_DENIED; a read-only
 *     target STOPS (not verified).
 *  4. rename.  Failure mapped; a move between two host volumes (ERROR_NOT_SAME_DEVICE)
 *     STOPS if MOVEFILE_COPY_ALLOWED was given (the copy is not built).
 *  5. The cached listings of both directories are dropped; rt_file_log_if_open as for delete.
 * Files and directories move alike.  The check of step 3 and the rename are two host calls:
 * a file created between them is replaced (accepted). */
BOOL rt_file_move(RtName from, RtName to, DWORD flags);

/* CreateDirectoryW/A.  Name translated with RT_PATH_CREATE | RT_PATH_DIR; mkdir with mode
 * 0755 (a directory call for the mapping): the name exists, as a file or a directory: FALSE,
 * ERROR_ALREADY_EXISTS (the game does this 116 times on the way to the menu); a missing
 * parent: ERROR_PATH_NOT_FOUND (documented: "One or more intermediate directories do not
 * exist"), and so is a NULL name (Wine's kernel32/tests/directory.c test_CreateDirectoryW).
 * The parent's cached listing is dropped. */
BOOL rt_dir_create(RtName name);

/* RemoveDirectoryW/A.  Name translated with RT_PATH_EXISTING | RT_PATH_DIR | RT_PATH_DELETE.
 * A name that leads to the current directory (rt_path_get_cwd_w; relative names too): FALSE,
 * ERROR_SHARING_VIOLATION, nothing removed, as Windows, which holds a handle on the current
 * directory (review X10; Wine kernel32/tests/directory.c test_RemoveDirectoryA and
 * test_RemoveDirectoryW).  This is checked before the skeleton refusal, so the install root,
 * which is the start directory, gets it too while it is current.
 * What the name leads to is not a directory: STOPS (Windows reports ERROR_DIRECTORY by
 * Wine's tests; not verified).  rmdir (a directory call): not empty: FALSE,
 * ERROR_DIR_NOT_EMPTY.  The parent's cached listing is dropped.  (Host files the guest did
 * not make, .DS_Store, also make a directory "not empty": an open question in the handoff.) */
BOOL rt_dir_remove(RtName name);

/* GetDriveTypeW/A(lpRootPathName).  Looks at the text only and never sets last error.
 * Both members NULL (the drive of the current directory): DRIVE_FIXED.  A letter and a colon
 * with no slash ("C:"), or with a slash of either kind ("C:\", "c:/"), answer as the root
 * does: DRIVE_FIXED for C in either case, DRIVE_NO_ROOT_DIR for another letter.  "C", "c",
 * "C?", "c?", "?" and the empty text answer DRIVE_NO_ROOT_DIR (review X9; another letter with
 * no colon STOPS, not verified).  Any other text STOPS (not verified). */
UINT rt_drive_type(RtName root);

/* GetDiskFreeSpaceExW/A(lpDirectoryName, lpFreeBytesAvailableToCaller, lpTotalNumberOfBytes,
 * lpTotalNumberOfFreeBytes); GetDiskFreeSpaceW/A turns the same three numbers into clusters.
 *   dir     a directory of the guest's tree (the page: "This parameter does not have to
 *           specify the root directory on a disk"), or both members NULL: "the function
 *           uses the root of the current disk", which is C:\ (the only drive)
 *   avail   NULL, or receives the bytes free for the caller     (host: f_bavail * f_bsize)
 *   total   NULL, or receives the size of the volume in bytes   (host: f_blocks * f_bsize)
 *   free    NULL, or receives all free bytes                    (host: f_bfree * f_bsize)
 *           (the page says of each of the three: "This parameter can be NULL")
 * WHICH VOLUME.  The name is translated with RT_PATH_EXISTING | RT_PATH_DIR, and the answer
 * is statfs of the host directory it leads to: the numbers of the HOST VOLUME that holds
 * that directory.  The guest sees one drive, but the install root, the state directory's
 * home tree and its virtual directories (C:\ itself is <state>/vfs) may lie on different
 * host volumes, so C:\Titanfall2 and C:\Users\Player can report different numbers: each
 * is the room a file written there would have.
 * TRUE, or FALSE with last error and nothing written:
 *   the errors of the translation (NAMES); in particular ERROR_PATH_NOT_FOUND for another
 *       drive ("D:\") and for a directory that does not exist.  Windows gives that code
 *       for a drive that does not exist (Wine kernel32/tests/drive.c test_GetDiskFreeSpaceW)
 *   ERROR_PATH_NOT_FOUND for the empty string, whatever the translation says to it (the
 *       same Wine test requires exactly this code of GetDiskFreeSpaceW(L""); of the A
 *       function it accepts this code or ERROR_INVALID_NAME)
 *   a failing statfs, mapped (a directory call)
 * The roots Wine's test requires to succeed do: NULL, "\", "/" and "C:\".
 * STOPS: a name that leads to a file and not to a directory (Windows is believed to answer
 * ERROR_DIRECTORY; not verified); a volume whose numbers do not fit 64 bits of bytes.
 * Last error is left alone on success.  The numbers are the host's at the moment of the
 * call and change between calls.
 * Sources: Microsoft's page of GetDiskFreeSpaceExA (fileapi.h) and Wine's
 * kernel32/tests/drive.c, read 2026-10-10. */
BOOL rt_disk_space(RtName dir, ULONGLONG *avail, ULONGLONG *total, ULONGLONG *free);

/* ---- find-file (core/file_find.c) -------------------------------------------------------------- */

/* FindFirstFileW(lpFileName, lpFindFileData).  The whole directory is read here; the handle
 * (kind RT_K_FIND, closed only by rt_find_close: CloseHandle on it fails) holds the matches.
 *  1. out NULL STOPS.  The name is translated with RT_PATH_WILDCARD: the host directory, "/",
 *     and the last component unchanged, which is the PATTERN.
 *  2. The pattern STOPS if it is empty, contains `?`, `<`, `>` or `"`, ends in `.`, or ends
 *     in ".*" without being exactly "*.*" (DOS matching rules that are not verified).
 *  3. The directory is opened (a directory call: failure mapped) and every entry, `.` and
 *     `..` included, is a candidate.  A name MATCHES if it equals the pattern with each `*`
 *     standing for any run of characters, empty and with periods included, comparing ASCII
 *     letters without case and everything else exactly; the pattern "*.*" matches every
 *     name, also one without a period.  A pattern without `*` matches at most one name.
 *  4. A matching entry is dropped (logged on `file`) if its stat fails, if it is neither a
 *     regular file nor a directory, or if its name is not valid UTF-8 or longer than 259
 *     WCHARs.
 *  5. ORDER: `.`, then `..`, then the others sorted by name, comparing UTF-16 code units
 *     with a-z folded to A-Z ("_x" sorts after "Z"), and by the unfolded units where that
 *     ties.  Never the host's order.
 *  6. No match: INVALID_HANDLE_VALUE, ERROR_FILE_NOT_FOUND.  No memory or a full handle
 *     table: ERROR_NOT_ENOUGH_MEMORY.  Otherwise the first match is stored in *out and the
 *     handle returned.
 * AN ENTRY: *out is zeroed, then dwFileAttributes, the three times and the size
 * (nFileSizeHigh, nFileSizeLow) as ATTRIBUTES above, cFileName = the on-disk spelling,
 * NUL-terminated; dwReserved0, dwReserved1 and cAlternateFileName stay 0.
 * `.` and `..` are reported in the drive's root too (Windows does not; accepted). */
HANDLE rt_find_first_w(const WCHAR *pattern, WIN32_FIND_DATAW *out);

/* FindNextFileW: the next match into *out, TRUE; after the last one FALSE with
 * ERROR_NO_MORE_FILES, every time, and *out untouched.  FALSE with ERROR_INVALID_HANDLE for a
 * handle that is not an open find handle.  out NULL STOPS.  Two threads on one handle each
 * get different entries, every entry exactly once (the handle's own lock). */
BOOL rt_find_next_w(HANDLE find, WIN32_FIND_DATAW *out);

/* The A forms: the pattern is read as code page 1252 (rt_path_to_host_a) and cFileName is
 * written in it; everything else as the W forms.  An entry whose name has a character
 * outside code page 1252, or more than 259 bytes, STOPS when it is about to be returned. */
HANDLE rt_find_first_a(const char *pattern, WIN32_FIND_DATAA *out);
BOOL   rt_find_next_a(HANDLE find, WIN32_FIND_DATAA *out);

/* FindClose: rt_handle_close_kind(find, RT_K_FIND).  TRUE, or FALSE with ERROR_INVALID_HANDLE. */
BOOL rt_find_close(HANDLE find);

/* ---- standard handles and the console (core/file_std.c) ---------------------------------------- */
/* STANDARD HANDLES (section 7.3).  rt_file_init makes three file objects and their handles
 * before any guest code runs:
 *     STD_INPUT_HANDLE    reads give end of file at once (the host's /dev/null); cannot write
 *     STD_OUTPUT_HANDLE   writes go to the host process's descriptor 1, byte for byte
 *     STD_ERROR_HANDLE    writes go to the host process's descriptor 2 (where the log goes
 *                         unless RT_LOG_FILE is set)
 * Each is a character device that is not a console: rt_file_type says FILE_TYPE_CHAR, so
 * that the C runtime flushes after every write and uses WriteFile.  If descriptor 1 or 2 is
 * closed in the host process, that handle writes to /dev/null (logged).  They are ordinary
 * handles: CloseHandle and DuplicateHandle work, and the host's own descriptors 1 and 2 stay
 * open whatever the guest closes.
 * THERE IS NO CONSOLE.  The console functions call no core function; each shim fails as
 * Windows does without a console (the codes are in the handoff). */

/* GetStdHandle(nStdHandle): the value stored for STD_INPUT_HANDLE, STD_OUTPUT_HANDLE or
 * STD_ERROR_HANDLE, whatever it is, last error untouched; for any other value
 * INVALID_HANDLE_VALUE with ERROR_INVALID_HANDLE.  No new handle is made: closing the result
 * closes the standard handle. */
HANDLE rt_std_get(DWORD which);

/* SetStdHandle(nStdHandle, hHandle): stores `h` as it is, NULL and invalid values included,
 * and closes nothing; TRUE.  Another `which`: FALSE, ERROR_INVALID_HANDLE. */
BOOL rt_std_set(DWORD which, HANDLE h);

/* GetFileType: FILE_TYPE_DISK for a file or a directory handle, FILE_TYPE_CHAR for a
 * character device (the standard handles, NUL), last error untouched.  For anything that is
 * not an open file handle (NULL, INVALID_HANDLE_VALUE, a handle of another kind):
 * FILE_TYPE_UNKNOWN with ERROR_INVALID_HANDLE. */
DWORD rt_file_type(HANDLE h);

/* SetConsoleCtrlHandler(HandlerRoutine, Add).  The process keeps a list of handlers and a
 * flag "ignore Ctrl+C"; nothing ever calls a handler, because nothing sends a console event.
 *   add nonzero, routine 0:      the flag is set; TRUE
 *   add zero,    routine 0:      the flag is cleared; TRUE
 *   add nonzero, routine not 0:  appended to the list (the same routine may be there twice); TRUE
 *   add zero,    routine not 0:  the entry added last with this routine is removed; TRUE.  A
 *                                routine that is not in the list STOPS (not verified)
 * More than 64 entries STOPS. */
BOOL rt_console_ctrl_handler(GUESTFN routine, BOOL add);

/* ---- file mappings (core/file_map.c, built; docs/handoff/rt-file-map.md) ------------------------ */
/* THE BOUNDARY WITH rt/vm.h.  A section is an object of kind RT_K_SECTION that owns a host
 * descriptor of its own: a dup of the file's (the file handle may be closed while the section
 * lives), or an unlinked temporary file under <state>/tmp for a section with no file.  A view
 * is made exactly as rt/vm.h says under FILE VIEWS: ONE rt_vm_alloc(0, size, MEM_RESERVE |
 * MEM_COMMIT, protect, RT_VM_MAPPED), then mmap(MAP_FIXED | MAP_SHARED) of the descriptor
 * over [base, base + size rounded up to the host page) (MAP_PRIVATE for a copy-on-write view),
 * and it goes back with rt_vm_release(base, RT_VM_MAPPED), never munmap.  The file offset of a
 * view is a multiple of 64 KB, which the host page (16 KB) divides.  Each view holds a
 * reference to its section (documented: "Mapped views of a file mapping object maintain
 * internal references to the object"); file_map.c keeps the list of views (base, section)
 * under its own lock.  NAMED sections live in the handle unit's name space: rt_object_new and
 * the initialisation of the fields are one critical section of file_map.c's lock, which
 * rt_section_open also takes (rt/handle.h, "A NAMED OBJECT IS VISIBLE...").
 *
 * The four points this header left to the piece are decided below and marked DECIDED; "from
 * memory" means Wine's tests and NTSTATUS table as remembered, not verified on Windows.
 *
 * WHAT A VIEW IS TO THE REST OF THE RUNTIME.  rt_vm_query says MEM_MAPPED, committed, with the
 * view's protection (PAGE_READONLY, PAGE_READWRITE or PAGE_WRITECOPY, which stays
 * PAGE_WRITECOPY after a write: rt/vm.h) and the base as AllocationBase.  rt_vm_free on it
 * fails (ERROR_INVALID_PARAMETER) and a MEM_COMMIT inside it is the VM unit's unimplemented
 * report.  rt_vm_protect works on it; the host refuses to make a shared view of a file opened
 * without write access writable, and that is the VM unit's fatal report.  A view of a file
 * opened WITH write access can be made writable that way even if the section or the view is
 * read-only (Windows refuses; not enforced).  Its address is in the VM arena like every
 * rt_vm_alloc(0, ...): above 4 GB.
 * WHAT IS NOT WINDOWS, and stays so:
 *   - a handle has no access rights of its own: every handle to a section, also one from
 *     rt_section_open or from rt_section_create on a name that existed, maps what the
 *     SECTION's protection allows (Windows also limits it by the access the handle was opened
 *     with);
 *   - a name stays in the name space until the object dies, so while a view is mapped the name
 *     can still be opened after its last handle was closed (Windows drops the name with the
 *     last handle; from memory);
 *   - the file layer does not know that a file is mapped: rt_file_set_eof, a delete or a
 *     truncating open succeed where Windows fails with ERROR_USER_MAPPED_FILE, and touching a
 *     view beyond the new end of its file is then a host fault (SIGBUS, the fatal report);
 *   - with 16 KB host pages, the up to three 4 KB pages after the end of a view, to the end of
 *     its last host page, are accessible and show the file there (on Windows they fault). */

/* CreateFileMappingW/A(hFile, security, flProtect, dwMaximumSizeHigh:Low, lpName).
 *   file      a file handle, or INVALID_HANDLE_VALUE for a section with no file.  NULL STOPS
 *             (Windows treats it as INVALID_HANDLE_VALUE, from memory).  A directory handle
 *             or a character device STOPS (not verified).
 *   protect   PAGE_READONLY, PAGE_READWRITE or PAGE_WRITECOPY, alone or with SEC_COMMIT;
 *             anything else STOPS
 *   max_size  0: the file's size now (no file: NULL, ERROR_INVALID_PARAMETER; an empty file:
 *             NULL, ERROR_FILE_INVALID; both documented).  Smaller than the file: the section
 *             is that many bytes.  Larger than the file with PAGE_READWRITE: the file is
 *             extended to it and the new part reads as zeros (documented; if the host
 *             refuses: NULL, ERROR_DISK_FULL, documented).  DECIDED (1): larger than the file
 *             with PAGE_READONLY or PAGE_WRITECOPY: NULL, ERROR_NOT_ENOUGH_MEMORY, and the
 *             file is untouched (the kernel's STATUS_SECTION_TOO_BIG for a read-only section
 *             one byte larger than its file: Wine's kernel32/tests/virtual.c
 *             test_MapViewOfFile; that the status becomes this code is from memory).  A section with no
 *             file has its size rounded up to 4 KB (Wine virtual.c test_MapViewOfFile: 4090 gives 4096) and reads
 *             as zeros (documented).
 *   name      NULL, or the name (an A shim converts it to UTF-16, as for events)
 * DECIDED (4), the file handle's access against `protect` ("The file must be opened with
 * access rights that are compatible with the protection flags"; the header had no rule):
 * PAGE_READONLY and PAGE_WRITECOPY need a handle that can read, PAGE_READWRITE one that can
 * read and write (rt_file_create step 4; documented), else NULL with ERROR_ACCESS_DENIED
 * (Windows gives that or ERROR_INVALID_PARAMETER: Wine's kernel32/tests/virtual.c
 * test_MapViewOfFile).  A section with no file takes any of the three.
 * ORDER: the stops; `file` (ERROR_INVALID_HANDLE, ERROR_ACCESS_DENIED) and the size; then the
 * name.  So a call with a name that exists and arguments that are wrong fails with the
 * arguments' error (as the NT kernel, which builds the section before it inserts the name;
 * from memory).  A file is extended only for a NEW section: a name that exists together with
 * a max_size larger than the file STOPS (whether Windows extends the file then is not
 * verified).
 * Returns a new handle.  LAST ERROR ON SUCCESS is always set, as rt_event_create does:
 * ERROR_ALREADY_EXISTS when the name existed (the handle is to that section, with its file,
 * protection and size, not the ones given), else 0.  Fails with NULL (not
 * INVALID_HANDLE_VALUE) and ERROR_INVALID_HANDLE (the name belongs to another kind; `file` is
 * not a file handle), ERROR_NOT_ENOUGH_MEMORY (also: no backing for a section with no file,
 * logged), or the codes above.  Eight threads that create one name get eight handles to ONE
 * object; exactly one is told 0. */
HANDLE rt_section_create(HANDLE file, DWORD protect, ULONGLONG max_size, const WCHAR *name);

/* OpenFileMappingW/A(dwDesiredAccess, bInheritHandle, lpName): as rt_event_open (rt/sync.h),
 * for kind RT_K_SECTION: NULL with ERROR_FILE_NOT_FOUND, ERROR_INVALID_HANDLE,
 * ERROR_INVALID_PARAMETER or ERROR_NOT_ENOUGH_MEMORY.  Last error untouched on success.  The
 * desired access is not taken: see "a handle has no access rights" above. */
HANDLE rt_section_open(const WCHAR *name);

/* MapViewOfFile(hFileMappingObject, dwDesiredAccess, dwFileOffsetHigh:Low,
 * dwNumberOfBytesToMap): the guest address of the view, or 0 with last error.  In this order:
 *  1. access, DECIDED (2a).  Exactly one of these forms, anything else STOPS (FILE_MAP_EXECUTE,
 *     0, FILE_MAP_WRITE | FILE_MAP_COPY, unknown bits):
 *       FILE_MAP_WRITE, FILE_MAP_WRITE | FILE_MAP_READ, FILE_MAP_ALL_ACCESS
 *                         a read-write view, PAGE_READWRITE: writes reach the file and every
 *                         other shared view (the three are documented as equivalent)
 *       FILE_MAP_READ     a read-only view, PAGE_READONLY: a write faults
 *       FILE_MAP_COPY, FILE_MAP_COPY | FILE_MAP_READ
 *                         a copy-on-write view, PAGE_WRITECOPY: it shows the file, a write
 *                         changes a private copy and never the file or another view
 *                         (documented).  NOT WINDOWS: the view shows the file AS IT WAS WHEN
 *                         THE VIEW WAS MAPPED; later changes of the file, through another
 *                         view or a write, do not show in it, in a written page or not
 *                         (measured on this host: a private mapping is a snapshot; on
 *                         Windows a page not yet written is remembered to follow the file,
 *                         not verified).  Logged once at warn.
 *  2. offset not a multiple of 64 KB: 0, ERROR_MAPPED_ALIGNMENT (the rule is documented, the
 *     code is STATUS_MAPPED_ALIGNMENT's, from memory; checked before the handle as ReactOS
 *     does, not verified).
 *  3. ERROR_INVALID_HANDLE: not a section handle.
 *  4. DECIDED (2b): a read-write view of a section that is not PAGE_READWRITE: 0,
 *     ERROR_ACCESS_DENIED (Windows gives that or ERROR_INVALID_PARAMETER: Wine's
 *     kernel32/tests/virtual.c test_MapViewOfFile).  Read-only and copy-on-write views are
 *     allowed for all three protections (documented at CreateFileMapping: PAGE_READONLY and
 *     PAGE_WRITECOPY "allow views to be mapped for read-only or copy-on-write access").
 *  5. size, DECIDED (3).  offset at or beyond the section's size: 0, ERROR_ACCESS_DENIED.
 *     size 0: from offset to the end of the section (documented).  A view that ends beyond
 *     the section's size rounded up to 4 KB: 0, ERROR_ACCESS_DENIED (documented that it
 *     fails; the code is STATUS_INVALID_VIEW_SIZE's, from memory).  One that ends beyond the
 *     size but inside that last 4 KB page STOPS (not verified which of the two Windows
 *     compares with; it cannot happen for a section with no file).
 *  6. ERROR_NOT_ENOUGH_MEMORY: no room in the VM arena (rt_vm_alloc's), no host memory.  A
 *     failing host mmap is logged and mapped.
 * The view is whole 4 KB pages to VirtualQuery, type MEM_MAPPED, and outlives every handle to
 * its section and to the file.  Bytes of the last page after the end of the file are zeros.
 * Two shared views of one section, or of two sections of one file, show the same memory.
 * Last error untouched on success. */
uint64_t rt_view_map(HANDLE section, DWORD access, ULONGLONG offset, SIZE_T size);

/* UnmapViewOfFile(lpBaseAddress): unmaps the view that contains `base`.  The documentation
 * asks for the address rt_view_map returned ("must be identical to the value returned");
 * Windows takes any address inside the view (Wine's kernel32/tests/virtual.c
 * test_MapViewOfFile), and so does this: the whole view goes, logged at debug.  TRUE, or
 * FALSE with ERROR_INVALID_ADDRESS for an address in no view: a view already unmapped, the
 * byte after a view, a VirtualAlloc region, 0.  Drops the view's reference to its section;
 * what was written through a shared view stays in the file. */
BOOL rt_view_unmap(uint64_t base);

#endif /* RT_FILE_H */
