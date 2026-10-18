/* Logging (docs/RUNTIME.md section 9.5), the fatal reports, and the errno mapping (3.4). */
#include <errno.h>

#include "rt/test.h"

TEST(core, log_channels)
{
    unsigned before = T_LOG_COUNT(RT_CH_VM);
    rt_log(RT_CH_VM, RT_LL_INFO, "value %d and %s", 42, "text");
    T_TRUE(T_LOGGED(RT_CH_VM, "value 42 and text"));
    T_TRUE(!T_LOGGED(RT_CH_FILE, "value 42 and text"));         /* channels are separate */
    T_EQ(T_LOG_COUNT(RT_CH_VM), before + 1);

    /* A channel prints nothing below its level, and RT_LOG_ON says so in advance. */
    uint8_t saved = rt_log_level[RT_CH_VM];
    rt_log_level[RT_CH_VM] = RT_LL_WARN;
    T_TRUE(RT_LOG_ON(RT_CH_VM, RT_LL_ERROR) && RT_LOG_ON(RT_CH_VM, RT_LL_WARN));
    T_TRUE(!RT_LOG_ON(RT_CH_VM, RT_LL_INFO) && !RT_LOG_ON(RT_CH_VM, RT_LL_DEBUG));
    rt_log(RT_CH_VM, RT_LL_INFO, "hidden line");
    rt_log(RT_CH_VM, RT_LL_WARN, "shown line");
    T_TRUE(!T_LOGGED(RT_CH_VM, "hidden line") && T_LOGGED(RT_CH_VM, "shown line"));
    rt_log_level[RT_CH_VM] = saved;

    /* A long message is not cut. */
    char big[3000];
    memset(big, 'z', sizeof big - 1);
    big[sizeof big - 1] = 0;
    rt_log(RT_CH_VM, RT_LL_INFO, "%s END", big);
    T_TRUE(T_LOGGED(RT_CH_VM, "zzzz END"));

    T_TRUE(!strcmp(rt_log_channel_name(RT_CH_GUEST_DEBUG), "guest.debug"));
    T_TRUE(!strcmp(rt_log_channel_name(RT_CH_TRACE), "trace"));
    T_TRUE(!strcmp(rt_log_channel_name(RT_CH_LOADER), "loader"));
}

TEST(core, log_once)
{
    /* "Logged once": once per distinct key on its channel. */
    unsigned before = T_LOG_COUNT(RT_CH_REGISTRY);
    T_TRUE(rt_log_once(RT_CH_REGISTRY, RT_LL_WARN, "HKLM\\Software\\A", "key %s not found", "A"));
    T_TRUE(!rt_log_once(RT_CH_REGISTRY, RT_LL_WARN, "HKLM\\Software\\A", "key %s not found", "A"));
    T_TRUE(rt_log_once(RT_CH_REGISTRY, RT_LL_WARN, "HKLM\\Software\\B", "key %s not found", "B"));
    T_EQ(T_LOG_COUNT(RT_CH_REGISTRY), before + 2);
    /* The same key on another channel is another event. */
    T_TRUE(rt_log_once(RT_CH_FILE, RT_LL_WARN, "HKLM\\Software\\A", "other channel"));
    T_TRUE(T_LOGGED(RT_CH_REGISTRY, "key A not found") && T_LOGGED(RT_CH_REGISTRY, "key B not found"));
}

TEST(core, fatal_reports)
{
    /* rt_fatal: a broken invariant.  One block that starts with FATAL, then abort. */
    T_TRAPS(rt_fatal("the %s is %d", "answer", 42), "FATAL runtime: the answer is 42\n  thread 0x1004");
    /* An unbuilt core function names itself and the unit of work that will build it. */
    T_TRAPS(rt_core_unimplemented("rt_example", "rt-core-example"),
            "FATAL unimplemented runtime core function rt_example (unit of work: rt-core-example)");
    /* RT_UNIMPLEMENTED outside any shim still reports where it was called. */
    T_TRAPS(RT_UNIMPLEMENTED(T_CTX(), "flag %#x", 0x40), "FATAL unimplemented: flag 0x40\n  at runtime/tests/core/log_and_errors.c:");
    /* A Ctx with no RtThread behind it (host NULL), and no Ctx at all, are reported as "no
     * guest thread"; the reporter used to dereference ctx->host and die of signal 11 with
     * nothing printed (review R33). */
    Ctx bare;
    memset(&bare, 0, sizeof bare);
    T_TRAPS(RT_UNIMPLEMENTED(&bare, "bare %d", 7), "FATAL unimplemented: bare 7\n  at runtime/tests/core/log_and_errors.c:");
    T_TRAPS(RT_UNIMPLEMENTED(&bare, "bare %d", 7), "\n  (no guest thread)\n");
    T_TRAPS(RT_UNIMPLEMENTED(NULL, "none %d", 8), "FATAL unimplemented: none 8\n  at runtime/tests/core/log_and_errors.c:");
    T_TRAPS(RT_UNIMPLEMENTED(NULL, "none %d", 8), "\n  (no guest thread)\n");
}

TEST(core, win32_from_errno)
{
    /* The one place errno becomes a Win32 error.  The context decides where Windows does. */
    T_EQ(rt_win32_from_errno(ENOENT, RT_ERR_FILE), ERROR_FILE_NOT_FOUND);
    T_EQ(rt_win32_from_errno(ENOENT, RT_ERR_DIR), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_win32_from_errno(ENOTDIR, RT_ERR_FILE), ERROR_PATH_NOT_FOUND);
    T_EQ(rt_win32_from_errno(EEXIST, RT_ERR_FILE), ERROR_FILE_EXISTS);
    T_EQ(rt_win32_from_errno(EEXIST, RT_ERR_DIR), ERROR_ALREADY_EXISTS);
    T_EQ(rt_win32_from_errno(EACCES, RT_ERR_FILE), ERROR_ACCESS_DENIED);
    T_EQ(rt_win32_from_errno(EPERM, RT_ERR_FILE), ERROR_ACCESS_DENIED);
    T_EQ(rt_win32_from_errno(EROFS, RT_ERR_FILE), ERROR_ACCESS_DENIED);
    T_EQ(rt_win32_from_errno(EISDIR, RT_ERR_FILE), ERROR_ACCESS_DENIED);
    T_EQ(rt_win32_from_errno(EBADF, RT_ERR_FILE), ERROR_INVALID_HANDLE);
    T_EQ(rt_win32_from_errno(EMFILE, RT_ERR_FILE), ERROR_TOO_MANY_OPEN_FILES);
    T_EQ(rt_win32_from_errno(ENOMEM, RT_ERR_MEMORY), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(rt_win32_from_errno(ENOSPC, RT_ERR_FILE), ERROR_DISK_FULL);
    T_EQ(rt_win32_from_errno(ENOTEMPTY, RT_ERR_DIR), ERROR_DIR_NOT_EMPTY);
    T_EQ(rt_win32_from_errno(EXDEV, RT_ERR_FILE), ERROR_NOT_SAME_DEVICE);
    T_EQ(rt_win32_from_errno(ENAMETOOLONG, RT_ERR_FILE), ERROR_FILENAME_EXCED_RANGE);
    T_EQ(rt_win32_from_errno(EINVAL, RT_ERR_GENERIC), ERROR_INVALID_PARAMETER);
    T_EQ(rt_win32_from_errno(ESPIPE, RT_ERR_FILE), ERROR_SEEK);
    T_EQ(rt_win32_from_errno(EPIPE, RT_ERR_FILE), ERROR_BROKEN_PIPE);
    T_EQ(rt_win32_from_errno(EAGAIN, RT_ERR_FILE), ERROR_SHARING_VIOLATION);
    T_EQ(rt_win32_from_errno(EAGAIN, RT_ERR_THREAD), ERROR_NOT_ENOUGH_MEMORY);
    T_EQ(rt_win32_from_errno(ETIMEDOUT, RT_ERR_THREAD), ERROR_SEM_TIMEOUT);

    /* An errno nobody mapped is fatal, so the table grows deliberately; so is "no error". */
    T_TRAPS(rt_win32_from_errno(9999, RT_ERR_FILE), "errno 9999");
    T_TRAPS(rt_win32_from_errno(9999, RT_ERR_FILE), "has no Win32 mapping in context file; add it to runtime/core/error.c");
    T_TRAPS(rt_win32_from_errno(0, RT_ERR_DIR), "errno 0 (no error) has no Win32 mapping in context directory");
}
