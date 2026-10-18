/* Logging (docs/RUNTIME.md section 9.5).  Interface and the RT_LOG syntax: rt/log.h. */
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core.h"
#include "rt/test_fork.h"

uint8_t rt_log_level[RT_CH_COUNT];

static const char *const channel_names[RT_CH_COUNT] = {
    "loader", "import", "vm", "thread", "file", "registry", "trap", "guest.debug", "net", "trace", "window",
    "com",
};
static const char *const level_names[] = { "error", "warn", "info", "debug" };

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;
static FILE     *log_out;
static _Atomic int log_fd = 2;       /* log_out's descriptor, for rt_log_raw: read with no lock */
static RtLogSink log_sink;
static void     *log_sink_user;

/* Keys already seen by rt_log_once, per channel: a small chained hash set. */
typedef struct OnceKey { struct OnceKey *next; char key[]; } OnceKey;
#define ONCE_BUCKETS 256
static OnceKey *once[RT_CH_COUNT][ONCE_BUCKETS];

const char *rt_log_channel_name(RtChannel ch) {
    return (unsigned)ch < RT_CH_COUNT ? channel_names[ch] : "?";
}

static FILE *out_file(void) { return log_out ? log_out : stderr; }

void rt_log_init(void) {
    for (int i = 0; i < RT_CH_COUNT; i++) rt_log_level[i] = RT_LL_WARN;

    const char *file = getenv("RT_LOG_FILE");
    if (file && *file && !log_out) {
        log_out = fopen(file, "w");
        if (!log_out) { fprintf(stderr, "FATAL runtime: cannot open RT_LOG_FILE '%s'\n", file); abort(); }
        atomic_store(&log_fd, fileno(log_out));
    }

    const char *spec = getenv("RT_LOG");
    if (!spec || !*spec) return;
    char *copy = strdup(spec);
    for (char *tok = strtok(copy, ","); tok; tok = strtok(NULL, ",")) {
        int level = RT_LL_DEBUG;
        char *colon = strchr(tok, ':');
        if (colon) {
            *colon = 0;
            level = -1;
            for (int l = 0; l < 4; l++) if (!strcmp(colon + 1, level_names[l])) level = l;
            if (level < 0) rt_fatal("RT_LOG: unknown level '%s' (levels: error, warn, info, debug)", colon + 1);
        }
        int found = 0;
        for (int i = 0; i < RT_CH_COUNT; i++) {
            if (!strcmp(tok, "all") || !strcmp(tok, channel_names[i])) { rt_log_level[i] = (uint8_t)level; found = 1; }
        }
        if (!found) rt_fatal("RT_LOG: unknown channel '%s' (channels: loader, import, vm, thread, file, "
                             "registry, trap, guest.debug, net, trace, window, all)", tok);
    }
    free(copy);
}

void rt_log_flush(void) {
    pthread_mutex_lock(&log_lock);
    fflush(out_file());
    pthread_mutex_unlock(&log_lock);
}

void rt_log_set_sink(RtLogSink sink, void *user) {
    pthread_mutex_lock(&log_lock);
    log_sink = sink;
    log_sink_user = user;
    pthread_mutex_unlock(&log_lock);
}

/* Test only (rt/test_fork.h): in a forked child, whose copy of the mutex may be held by a
 * thread that does not exist there. */
void rt_log_forked_child(void) {
    log_lock = (pthread_mutex_t)PTHREAD_MUTEX_INITIALIZER;
}

/* The two functions of the fatal report (rt/log.h).  Neither waits for anything. */

void rt_log_raw(const char *text, size_t len) {
    int fd = atomic_load_explicit(&log_fd, memory_order_relaxed);
    while (len) {
        ssize_t w = write(fd, text, len);
        if (w < 0) { if (errno == EINTR) continue; return; }
        text += w;
        len -= (size_t)w;
    }
}

bool rt_log_flush_try(void) {
    if (pthread_mutex_trylock(&log_lock) != 0) return false;
    fflush(out_file());
    pthread_mutex_unlock(&log_lock);
    return true;
}

/* Format and emit one line.  Called with log_lock held. */
static void emit(RtChannel ch, RtLogLevel level, const char *fmt, va_list ap) {
    char stack[1024];
    char *line = stack;
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(stack, sizeof stack, fmt, ap);
    if (n >= (int)sizeof stack) {
        line = malloc((size_t)n + 1);
        if (line) vsnprintf(line, (size_t)n + 1, fmt, ap2);
        else line = stack;
    }
    va_end(ap2);
    if (log_sink) log_sink(ch, level, line, log_sink_user);
    else fprintf(out_file(), "%s: %s\n", channel_names[ch], line);
    if (line != stack) free(line);
}

void rt_logv(RtChannel ch, RtLogLevel level, const char *fmt, va_list ap) {
    if ((unsigned)ch >= RT_CH_COUNT || !RT_LOG_ON(ch, level)) return;
    pthread_mutex_lock(&log_lock);
    emit(ch, level, fmt, ap);
    pthread_mutex_unlock(&log_lock);
}

void rt_log(RtChannel ch, RtLogLevel level, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    rt_logv(ch, level, fmt, ap);
    va_end(ap);
}

bool rt_log_once(RtChannel ch, RtLogLevel level, const char *key, const char *fmt, ...) {
    if ((unsigned)ch >= RT_CH_COUNT || !RT_LOG_ON(ch, level)) return false;
    uint32_t h = 2166136261u;
    for (const char *p = key; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
    bool printed = false;
    pthread_mutex_lock(&log_lock);
    OnceKey **bucket = &once[ch][h % ONCE_BUCKETS], *k;
    for (k = *bucket; k; k = k->next) if (!strcmp(k->key, key)) break;
    if (!k) {
        k = malloc(sizeof *k + strlen(key) + 1);
        if (k) { strcpy(k->key, key); k->next = *bucket; *bucket = k; }
        va_list ap;
        va_start(ap, fmt);
        emit(ch, level, fmt, ap);
        va_end(ap);
        printed = true;
    }
    pthread_mutex_unlock(&log_lock);
    return printed;
}
