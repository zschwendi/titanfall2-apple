/* errno to Win32 error (docs/RUNTIME.md sections 3.4 and 7.1).  Interface: rt/shim.h.
 *
 * The one place the mapping lives.  The values follow what Windows reports for the same
 * condition (and, where Windows has no direct equivalent, what Wine reports).  An errno
 * that is not in the table is fatal on purpose: add it here deliberately, with the Windows
 * behaviour checked, rather than letting a shim pick a code.
 */
#include <errno.h>
#include <string.h>

#include "core.h"

DWORD rt_win32_from_errno(int e, RtErrContext what) {
    const bool dir = what == RT_ERR_DIR;
    switch (e) {
    case ENOENT:        return dir ? ERROR_PATH_NOT_FOUND : ERROR_FILE_NOT_FOUND;
    case ENOTDIR:       return ERROR_PATH_NOT_FOUND;
    case EEXIST:        return dir ? ERROR_ALREADY_EXISTS : ERROR_FILE_EXISTS;
    case EACCES:
    case EPERM:
    case EROFS:         return ERROR_ACCESS_DENIED;
    case EISDIR:        return ERROR_ACCESS_DENIED;         /* opening a directory as a file */
    case EBADF:         return ERROR_INVALID_HANDLE;
    case EMFILE:
    case ENFILE:        return ERROR_TOO_MANY_OPEN_FILES;
    case ENOMEM:        return ERROR_NOT_ENOUGH_MEMORY;
    case ENOSPC:
    case EDQUOT:        return ERROR_DISK_FULL;
    case EFBIG:         return ERROR_DISK_FULL;
    case ENOTEMPTY:     return ERROR_DIR_NOT_EMPTY;
    case EXDEV:         return ERROR_NOT_SAME_DEVICE;
    case ENAMETOOLONG:  return ERROR_FILENAME_EXCED_RANGE;
    case ELOOP:         return ERROR_CANT_RESOLVE_FILENAME;
    case EINVAL:        return ERROR_INVALID_PARAMETER;
    case ESPIPE:        return ERROR_SEEK;
    case EFAULT:        return ERROR_NOACCESS;
    case EPIPE:         return ERROR_BROKEN_PIPE;
    case EIO:           return ERROR_IO_DEVICE;
    case EBUSY:         return ERROR_BUSY;
    case EAGAIN:        return what == RT_ERR_THREAD ? ERROR_NOT_ENOUGH_MEMORY : ERROR_SHARING_VIOLATION;
    case ETXTBSY:       return ERROR_SHARING_VIOLATION;
    case ENODEV:
    case ENXIO:         return ERROR_DEV_NOT_EXIST;
    case ENOEXEC:       return ERROR_BAD_FORMAT;
    case ETIMEDOUT:     return ERROR_SEM_TIMEOUT;
    case ENOTSUP:       return ERROR_NOT_SUPPORTED;
    case ENOSYS:        return ERROR_CALL_NOT_IMPLEMENTED;
    case EINTR:         return ERROR_OPERATION_ABORTED;
    case ERANGE:        return ERROR_ARITHMETIC_OVERFLOW;
    case EOVERFLOW:     return ERROR_ARITHMETIC_OVERFLOW;
    default: break;
    }
    static const char *const ctx_name[] = { "?", "file", "directory", "memory", "thread", "generic" };
    rt_fatal("rt_win32_from_errno: errno %d (%s) has no Win32 mapping in context %s; add it to "
             "runtime/core/error.c after checking what Windows reports", e, e ? strerror(e) : "no error",
             (unsigned)what < 6 ? ctx_name[what] : "?");
}
