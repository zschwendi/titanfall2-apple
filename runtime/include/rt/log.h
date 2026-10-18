/* Logging, fatal errors and "not implemented" (docs/RUNTIME.md sections 9.1, 9.2, 9.5).
 *
 * One function, rt_log, with a fixed set of channels.  The environment variable RT_LOG
 * selects what is printed:
 *
 *     RT_LOG=trace                 channel `trace` at its most verbose level
 *     RT_LOG=loader,import:info    two channels, the second at level info
 *     RT_LOG=all:debug             everything
 *
 * A channel named without a level gets `debug`.  Channels not named print warnings and
 * errors only.  Output goes to stderr, or to the file named by RT_LOG_FILE.
 */
#ifndef RT_LOG_H
#define RT_LOG_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Ctx;

typedef enum {
    RT_CH_LOADER = 0,     /* "loader"       module loading, exports, relocations */
    RT_CH_IMPORT,         /* "import"       import binding, GetProcAddress misses */
    RT_CH_VM,             /* "vm"           VirtualAlloc family, arenas */
    RT_CH_THREAD,         /* "thread"       thread start and exit, ordering mode */
    RT_CH_FILE,           /* "file"         paths, opens, sharing-mode notes */
    RT_CH_REGISTRY,       /* "registry"     keys and values asked for */
    RT_CH_TRAP,           /* "trap"         fatal reports */
    RT_CH_GUEST_DEBUG,    /* "guest.debug"  OutputDebugString text */
    RT_CH_NET,            /* "net"          sockets: hosts asked for, destinations with no route */
    RT_CH_TRACE,          /* "trace"        one line per shim call and return */
    RT_CH_WINDOW,         /* "window"       windows created, queues waited on (rt/window.h) */
    RT_CH_COM,            /* "com"          interface objects: the first call of each method, the last release (rt/com.h) */
    RT_CH_COUNT
} RtChannel;

typedef enum { RT_LL_ERROR = 0, RT_LL_WARN = 1, RT_LL_INFO = 2, RT_LL_DEBUG = 3 } RtLogLevel;

/* Lowest level each channel prints; read by RT_LOG_ON.  Set once by rt_log_init. */
extern uint8_t rt_log_level[RT_CH_COUNT];

/* True if a message on `ch` at `level` would be printed.  Use it to skip expensive
 * formatting; rt_log tests it again. */
#define RT_LOG_ON(ch, level)  (rt_log_level[(ch)] >= (level))

/* Print one line: "<channel>: <message>\n".  Thread-safe; lines never interleave.  A
 * message with embedded newlines is printed as is. */
void rt_log(RtChannel ch, RtLogLevel level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void rt_logv(RtChannel ch, RtLogLevel level, const char *fmt, va_list ap);

/* "Logged once" in RUNTIME.md: print only the first time `key` is seen on this channel.
 * `key` is any string that identifies the event (a registry path, "user32.dll!Foo").
 * Returns true if the line was printed now. */
bool rt_log_once(RtChannel ch, RtLogLevel level, const char *key, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

/* Parse RT_LOG and RT_LOG_FILE.  Called first thing by rt_init; safe to call again. */
void rt_log_init(void);

/* Flush buffered output.  Called by every fatal path and by process exit. */
void rt_log_flush(void);

/* Divert every printed line to `sink` instead of the output file (NULL restores it).  The
 * line has no trailing newline.  Used by the test harness (T_LOGGED). */
typedef void (*RtLogSink)(RtChannel ch, RtLogLevel level, const char *line, void *user);
void rt_log_set_sink(RtLogSink sink, void *user);

const char *rt_log_channel_name(RtChannel ch);

/* ---- the fatal report: output that takes no lock (review finding R14) ----------------------- */

/* Write `len` bytes of `text` to the log output as they are, with write(2) on the output's
 * file descriptor (stderr's, or the file RT_LOG_FILE named), repeating after a short write
 * and after EINTR.  No channel prefix, never to the sink, nothing appended.
 *   TAKES NO LOCK and allocates nothing: it may be called from a signal handler, with any
 *   lock held (the log's own included), and before rt_log_init (the descriptor is then 2).
 *   It does not flush what rt_log has buffered: text printed earlier through rt_log may
 *   reach a file output AFTER this text.  rt_log_flush_try first puts that right.
 *   An error other than EINTR ends the write silently; there is nowhere left to report it.
 * This is the writer of every fatal report (rt/crash.h).  Ordinary logging uses rt_log. */
void rt_log_raw(const char *text, size_t len);

/* Flush what rt_log has buffered, but only if that can be done without waiting: try the
 * log's lock once; if it is free, flush the output, release the lock and return true; if
 * another thread, or the calling thread itself (a fault inside rt_log), holds it, do
 * nothing and return false.  Never blocks.  For fatal reports and process exit; everything
 * else calls rt_log_flush. */
bool rt_log_flush_try(void);

/* ---- fatal errors ----------------------------------------------------------------------- */
/* Each prints one block that starts with "FATAL" and aborts (SIGABRT; or exit status 134 where
 * the program asked for the quiet end: rt/crash.h, rt_report_quiet).  None returns.  All
 * of them, and the reports of rt/crash.h, are written by rt_log_raw under the one-report
 * rule of rt/crash.h ("ONE REPORT"): they take no lock, so they may be called with any lock
 * held, and when two threads fail at once the process prints one report, not two mixed. */

/* A runtime invariant failed, or the runtime was misused. */
_Noreturn void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* A core function that is declared but not built yet.  Every such function calls this as
 * its whole body, naming the unit of work that will implement it; it must never return a
 * default instead. */
_Noreturn void rt_core_unimplemented(const char *function, const char *unit);
#define RT_CORE_UNIMPLEMENTED(unit)  rt_core_unimplemented(__func__, (unit))

/* A shim was asked for a case it does not handle (section 9.2).  Prints the block of
 * section 9.1: the import, the case, the guest caller, the argument registers.  `fmt` says
 * what was asked for and should include the offending value. */
_Noreturn void rt_unimplemented(struct Ctx *ctx, const char *file, int line, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
#define RT_UNIMPLEMENTED(ctx, ...)  rt_unimplemented((ctx), __FILE__, __LINE__, __VA_ARGS__)

#endif /* RT_LOG_H */
