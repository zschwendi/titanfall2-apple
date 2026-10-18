/* Overlapped I/O, performed synchronously (docs/RUNTIME.md section 7.3).
 * Specification: rt/file.h, OVERLAPPED.  Shared internals: core/file_priv.h.
 *
 * BUILT: rt_file_ov_read, rt_file_ov_write, rt_file_overlapped_result, rt_file_read_ex.
 *
 * THE DESIGN.  Nothing is ever pending.  Every request is done inside the call that makes it,
 * with pread or pwrite at the offset of its OVERLAPPED, and is reported as a request that
 * completed at once: Internal and InternalHigh hold the final result when the call returns,
 * the event of the OVERLAPPED is set, the call returns TRUE (or FALSE with the final error),
 * and for ReadFileEx the completion routine is queued to the CALLING thread as an APC of
 * three arguments (rt_apc_queue_args, rt/sync.h), which that thread runs at its next
 * alertable wait and nowhere else.  There is no helper thread, no queue of requests and no
 * state: a request is the caller's stack frame.  Windows may complete any overlapped request
 * this way (ReadFile, "Testing for the End of a File": "ReadFile completed synchronously ...
 * system caching may cause this condition on most files after the first read"), so a program
 * that is correct on Windows handles every result given here.
 * pread and pwrite never read or move the host descriptor's position, so requests of several
 * threads through one handle cannot get each other's bytes, and a handle opened with
 * FILE_FLAG_OVERLAPPED keeps its position.  A handle WITHOUT the flag has its position set to
 * offset + bytes after a transfer that succeeded (ReadFile, "Synchronization and File
 * Position"), by a second host call: the bytes are always those of the offset asked for, and
 * a plain read of another thread through the same handle falls before or after the move.
 *
 * WHAT A PROGRAM CAN OBSERVE THAT WINDOWS'S ASYNCHRONOUS COMPLETION WOULD SHOW DIFFERENTLY.
 * None of these can give a program wrong bytes; they change when and in what order it learns
 * of a result that is the same.
 *   - STATUS_PENDING is never in Internal after a call (HasOverlappedIoCompleted is true at
 *     once) and no call fails with ERROR_IO_PENDING.  A program that takes TRUE from ReadFile
 *     on an overlapped handle for an error would stop working; Windows gives TRUE for cached
 *     data.
 *   - The buffer is filled, and the event set, before the call returns; the caller is blocked
 *     for the length of the transfer (a slow read is a hitch, not an error).
 *   - Completion routines are queued in the order of the ReadFileEx calls (Windows: "The
 *     system may call the waiting completion routines in any order").  A routine that itself
 *     calls ReadFileEx gets its next routine queued at once, and the alertable wait that is
 *     running them runs that one too: a chain of N reads completes inside ONE wait, which
 *     returns WAIT_IO_COMPLETION only when the chain ends, however short its timeout.
 *   - A failure is the call's own (FALSE): nothing is reported later through
 *     GetOverlappedResult after a pending return, and a routine is called with dwErrorCode 0
 *     or, for THE END OF THE FILE, ERROR_HANDLE_EOF.  The end of the file has two forms:
 *       ReadFile with an OVERLAPPED: FALSE with ERROR_HANDLE_EOF from the call (documented as
 *       one of the two ways: "An asynchronous read operation can encounter the end of a file
 *       during the initiating call to ReadFile or during subsequent asynchronous operations";
 *       the sample's "ReadFile returned FALSE and EOF condition, async EOF not triggered").
 *       It is what Windows before Vista does; Vista and later take the other way for an
 *       overlapped handle on a disk file (ERROR_IO_PENDING, then ERROR_HANDLE_EOF from
 *       GetOverlappedResult: Wine's ntdll/tests/file.c test_read_write; kernel32/tests/file.c
 *       test_overlapped_read says the same of Windows 8 and later).  Kept, because this
 *       runtime never answers ERROR_IO_PENDING and a program must take both.
 *       ReadFileEx: TRUE, and the routine is called with ERROR_HANDLE_EOF and 0 bytes, which
 *       is what Windows Vista and later do (the request is pending and its APC runs once, the
 *       status block holding STATUS_END_OF_FILE: ntdll/tests/file.c read_file_test).  Before
 *       Vista the call failed instead; the game needs Windows 7, so the later form is the one
 *       it was tested on.
 *   - Cancel.  There is nothing to cancel: CancelIo would find no request and a routine that
 *     is already queued still runs, with dwErrorCode 0 (on Windows a cancelled request
 *     completes with ERROR_OPERATION_ABORTED, or normally if it was already done, so a
 *     program must take both).  The game does not import CancelIo.
 *   - A file handle is not waitable here (rt/handle.h), so the documented use of the file
 *     handle itself as the completion signal when hEvent is NULL fails in the wait.
 *   - After a call that fails outright (no access, a host error) Internal holds a failure
 *     status and the event is set; on Windows Internal is left at STATUS_PENDING and the
 *     event reset (from memory: kernel32 stores STATUS_PENDING before the system call, and a
 *     request that fails at once completes nothing).  Chosen so that a program which ignores
 *     the failure and asks for the result cannot be told an old success: see the stops.
 *
 * WHERE THE RUN STOPS INSTEAD, because a guess could hand the program wrong bytes, a wrong
 * position, or a wait that never ends (each is RT_FILE_STOP, "<function>: not implemented"):
 *   - rt_file_overlapped_result on Internal == STATUS_PENDING with bWait TRUE (the value is
 *     the program's own: there is no request whose end could be waited for), and on any
 *     status but success and end of file (a failed request: Windows gives its error code, and
 *     the status stored here after a failure is not the one Windows would have);
 *   - an offset with the top bit set: 0xFFFFFFFF:0xFFFFFFFF is the documented "write at the
 *     end of the file", and the others are not valid (the kernel's "use the file position"
 *     value is among them, from memory);
 *   - an OVERLAPPED given for a character device (offsets are ignored there by the
 *     documentation: not built) or a directory handle (as rt_file_read step 5);
 *   - hEvent that is not NULL and not an event handle, before anything is transferred;
 *   - rt_file_read_ex on a handle without the flag, with lpOverlapped NULL or without a
 *     routine (rt/file.h; the documentation only says the handle "can be any handle opened
 *     with the FILE_FLAG_OVERLAPPED flag").
 *
 * SETTLED FROM WINE'S CONFORMANCE TESTS (what they record of Windows; docs/reviews/
 * win-behaviour-check.md has the list):
 *   - a read with an OVERLAPPED that begins at or beyond the end of the file on a handle
 *     WITHOUT FILE_FLAG_OVERLAPPED: FALSE, ERROR_HANDLE_EOF, Internal STATUS_END_OF_FILE,
 *     InternalHigh 0 (ntdll/tests/file.c test_read_write; kernel32/tests/file.c test_eof).
 *     "TRUE with 0 bytes" (Testing for the End of a File) is the read WITHOUT an OVERLAPPED.
 *
 * SETTLED FROM THE DOCUMENTATION (learn.microsoft.com), where rt/file.h or memory was unsure:
 *   - Offset and OffsetHigh are never written: "it is specified by the user; the system will
 *     not modify these values" (OVERLAPPED).  The ReadFile page says of a handle without the
 *     flag "The system updates the OVERLAPPED offset and the file pointer before ReadFile
 *     returns"; the structure's own page is taken, and the file pointer is updated.
 *   - "Functions such as ReadFile and WriteFile set this handle to the nonsignaled state
 *     before they begin an I/O operation.  When the operation has completed, the handle is
 *     set to the signaled state" (OVERLAPPED): since the request completes inside the call,
 *     the two are one step here and the event is set.
 *   - ReadFileEx: "ignores the OVERLAPPED structure's hEvent member"; "does use the
 *     OVERLAPPED structure's Internal and InternalHigh members"; on success "GetLastError
 *     will return ERROR_SUCCESS", so rt_file_read_ex sets last error 0 when it returns TRUE:
 *     the one function of this file that touches last error on success.
 *   - A read that begins before the end and reaches it succeeds with the bytes there are.
 *
 * STATE: none.  LOCKS: none of its own.  The event of an OVERLAPPED is set with
 * rt_event_set_object and the routine queued with rt_apc_queue_args; both take the dispatcher
 * lock themselves, so no lock is held around them, and the caller holds none.
 */
#include <errno.h>
#include <unistd.h>

#include "file_priv.h"

#include "rt/sync.h"
#include "rt/thread.h"

/* A transfer of more than this many bytes is made in pieces, as in core/file_io.c (the host
 * refuses a single call above 2 GB). */
#define OV_PIECE  0x40000000u

/* What Internal holds after a call that failed for another reason than the end of the file:
 * STATUS_UNSUCCESSFUL.  Not the status Windows would store (see the top); it is there so
 * that rt_file_overlapped_result stops instead of reporting the OVERLAPPED's previous
 * result. */
#define OV_STATUS_FAILED  0xC0000001u

/* The file offset of an OVERLAPPED, or the stop for one this file does not do. */
static off_t ov_offset(const char *fn, const OVERLAPPED *ov) {
    const uint64_t off = (uint64_t)ov->Offset | (uint64_t)ov->OffsetHigh << 32;
    if (off >> 63)
        rt_fatal("%s: not implemented: an OVERLAPPED with the offset 0x%llx (0xFFFFFFFF:0xFFFFFFFF is \"write at the end "
                 "of the file\"; no other offset with the top bit set is valid)", fn, (unsigned long long)off);
    return (off_t)off;
}

/* The object behind ov->hEvent with a reference, or NULL if there is none.  A handle that is
 * not an event's is a stop (rt/file.h), made before anything is transferred. */
static RtObject *ov_event(const char *fn, const OVERLAPPED *ov) {
    if (!ov->hEvent) return NULL;
    RtObject *e = rt_handle_ref(ov->hEvent, RT_K_EVENT);
    if (!e) rt_fatal("%s: not implemented: OVERLAPPED.hEvent %p is not an event handle", fn, (void *)ov->hEvent);
    return e;
}

/* The request is over: store its result, then signal.  In this order, so that a thread the
 * event wakes finds the result.  `event` may be NULL; its reference is dropped. */
static void ov_complete(OVERLAPPED *ov, uint32_t status, DWORD bytes, RtObject *event) {
    ov->InternalHigh = bytes;
    ov->Internal = status;
    if (event) {
        rt_event_set_object(event);
        rt_object_unref(event);
    }
}

/* A call fails for another reason than the end of the file: returns `error`. */
static DWORD ov_failed(OVERLAPPED *ov, RtObject *event, DWORD error) {
    ov_complete(ov, OV_STATUS_FAILED, 0, event);
    return error;
}

/* A handle without FILE_FLAG_OVERLAPPED: the position follows the transfer. */
static void ov_move_position(const char *fn, const RtFile *f, off_t to) {
    if (lseek(f->fd, to, SEEK_SET) < 0)
        rt_fatal("%s: the position of a handle without FILE_FLAG_OVERLAPPED could not be moved to %lld after a transfer "
                 "with an OVERLAPPED: errno %d", fn, (long long)to, errno);
}

/* The checks every transfer with an OVERLAPPED begins with; `fn` is the public function. */
static void ov_check_type(const char *fn, const RtFile *f) {
    if (f->type == RT_FT_DIR) rt_fatal("%s: not implemented: a directory handle (with an OVERLAPPED)", fn);
    if (f->type != RT_FT_DISK)
        rt_fatal("%s: not implemented: an OVERLAPPED given for a character device (a standard handle, NUL)", fn);
}

/* The read of rt_file_ov_read and rt_file_read_ex: len bytes at ov's offset, or up to the
 * end of the file.  Returns 0 with *bytes, or the Win32 error of the call (last error is the
 * caller's to set); *ov holds the result either way and `event` (may be NULL; its reference
 * is dropped) is set. */
static DWORD ov_read_at(const char *fn, RtFile *f, void *buf, DWORD len, OVERLAPPED *ov, RtObject *event, DWORD *bytes) {
    const off_t off = ov_offset(fn, ov);
    if (!f->can_read) return ov_failed(ov, event, ERROR_ACCESS_DENIED);
    rt_partial_require(f, (uint64_t)off, len, fn);     /* game data only partly here: the stop, or all present */
    DWORD total = 0;
    int en = 0;
    while (total < len) {
        const size_t want = (len - total) < OV_PIECE ? (size_t)(len - total) : OV_PIECE;
        ssize_t n;
        do n = pread(f->fd, (uint8_t *)buf + total, want, off + (off_t)total); while (n < 0 && errno == EINTR);
        if (n < 0) { en = errno; break; }
        total += (DWORD)n;
        if ((size_t)n < want) break;                /* the end of the file: a short piece */
    }
    if (en && total == 0) return ov_failed(ov, event, rt_win32_from_errno(en, RT_ERR_FILE));
    if (len != 0 && total == 0) {
        /* The read begins at or beyond the end of the file.  With or without
         * FILE_FLAG_OVERLAPPED the call fails with ERROR_HANDLE_EOF and the OVERLAPPED holds
         * STATUS_END_OF_FILE and 0 bytes; the position of a handle without the flag stays
         * (Wine's tests on Windows: ntdll/tests/file.c test_read_write, kernel32/tests/file.c
         * test_eof). */
        ov_complete(ov, STATUS_END_OF_FILE, 0, event);
        return ERROR_HANDLE_EOF;
    }
    if (!f->overlapped) ov_move_position(fn, f, off + (off_t)total);
    ov_complete(ov, STATUS_SUCCESS, total, event);
    *bytes = total;
    return ERROR_SUCCESS;
}

BOOL rt_file_ov_read(RtFile *f, void *buf, DWORD len, DWORD *got, OVERLAPPED *ov) {
    ov_check_type("rt_file_read", f);
    RtObject *event = ov_event("rt_file_read", ov);
    DWORD n = 0;
    const DWORD e = ov_read_at("rt_file_read", f, buf, len, ov, event, &n);
    if (e) return rt_file_fail(e);
    if (got) *got = n;
    return TRUE;
}

BOOL rt_file_ov_write(RtFile *f, const void *buf, DWORD len, DWORD *put, OVERLAPPED *ov) {
    ov_check_type("rt_file_write", f);
    const off_t off = ov_offset("rt_file_write", ov);
    RtObject *event = ov_event("rt_file_write", ov);
    if (!f->can_write) return rt_file_fail(ov_failed(ov, event, ERROR_ACCESS_DENIED));
    DWORD total = 0;
    int en = 0;
    while (total < len) {
        const size_t want = (len - total) < OV_PIECE ? (size_t)(len - total) : OV_PIECE;
        ssize_t n;
        do n = pwrite(f->fd, (const uint8_t *)buf + total, want, off + (off_t)total); while (n < 0 && errno == EINTR);
        if (n < 0) { en = errno; break; }
        total += (DWORD)n;
    }
    if (en) {
        /* As rt_file_write step 8: *put is what was written before the failure. */
        if (put) *put = total;
        return rt_file_fail(ov_failed(ov, event, rt_win32_from_errno(en, RT_ERR_FILE)));
    }
    if (!f->overlapped) ov_move_position("rt_file_write", f, off + (off_t)total);
    ov_complete(ov, STATUS_SUCCESS, total, event);
    if (put) *put = total;
    return TRUE;
}

BOOL rt_file_overlapped_result(HANDLE h, OVERLAPPED *ov, DWORD *transferred, BOOL wait) {
    if (!ov) RT_FILE_STOP("rt_file_overlapped_result", "lpOverlapped NULL");
    if (!transferred) RT_FILE_STOP("rt_file_overlapped_result", "lpNumberOfBytesTransferred NULL");
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    rt_file_unref(f);
    /* The status is the low half of Internal, as HasOverlappedIoCompleted reads it. */
    const uint32_t status = (uint32_t)ov->Internal;
    switch (status) {
    case STATUS_SUCCESS:
        *transferred = (DWORD)ov->InternalHigh;
        return TRUE;
    case STATUS_END_OF_FILE:
        *transferred = 0;
        return rt_file_fail(ERROR_HANDLE_EOF);
    case STATUS_PENDING:
        /* Never stored by this runtime: the program's own initial value, or a request it
         * never made. */
        if (!wait) return rt_file_fail(ERROR_IO_INCOMPLETE);
        RT_FILE_STOP("rt_file_overlapped_result", "bWait TRUE with Internal STATUS_PENDING: no request is ever pending, "
                     "so nothing would end the wait (OVERLAPPED %p)", (void *)ov);
    default:
        RT_FILE_STOP("rt_file_overlapped_result", "the result of an OVERLAPPED whose Internal is 0x%08x (0x%08x is a call "
                     "of this runtime that failed; the error Windows reports for it is not built)", status,
                     OV_STATUS_FAILED);
    }
}

BOOL rt_file_read_ex(Ctx *ctx, HANDLE h, void *buf, DWORD len, OVERLAPPED *ov, GUESTFN routine) {
    if (!ctx || !RT_THREAD(ctx)) rt_fatal("rt_file_read_ex: no ctx, or a ctx that belongs to no thread");
    if (!ov) RT_FILE_STOP("rt_file_read_ex", "lpOverlapped NULL");
    if (!routine) RT_FILE_STOP("rt_file_read_ex", "lpCompletionRoutine NULL");
    RtFile *f = rt_file_ref(h);
    if (!f) return FALSE;
    if (!f->overlapped) {
        rt_file_unref(f);
        RT_FILE_STOP("rt_file_read_ex", "handle %p was opened without FILE_FLAG_OVERLAPPED", (void *)h);
    }
    ov_check_type("rt_file_read_ex", f);
    DWORD n = 0;
    const DWORD e = ov_read_at("rt_file_read_ex", f, buf, len, ov, NULL, &n);      /* hEvent is ignored (documented) */
    rt_file_unref(f);
    /* The end of the file is reported THROUGH THE ROUTINE: the call succeeds and the routine
     * gets ERROR_HANDLE_EOF with 0 bytes (Windows Vista and later, where the request is
     * pending and completes with STATUS_END_OF_FILE: Wine's ntdll/tests/file.c read_file_test
     * and test_read_write).  Every other failure is the call's, and queues nothing. */
    if (e && e != ERROR_HANDLE_EOF) return rt_file_fail(e);
    /* VOID CALLBACK routine(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED
     * lpOverlapped), by the calling thread at its next alertable wait. */
    const uint64_t args[3] = { e, n, (uint64_t)(uintptr_t)ov };
    if (!rt_apc_queue_args(ctx, RT_THREAD(ctx), routine, 3, args)) {
        /* No memory for the call (the queue set last error): the read never happened as far
         * as the program can tell, and no routine will run for it. */
        ov->InternalHigh = 0;
        ov->Internal = OV_STATUS_FAILED;
        return FALSE;
    }
    rt_thread_set_last_error(ERROR_SUCCESS);
    return TRUE;
}
