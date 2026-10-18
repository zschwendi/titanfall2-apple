/* Declarations of the C library the call tables use, for the Windows reference program, which
 * is built without one: ref_libc.c has the portable part (and is tested natively against this
 * Mac's library), refcalls.c the part that asks Windows (time, sleep, memory, environment).
 * The headers under compat/ with the standard names (<stdio.h>, <string.h>, ...) include this.
 */
#ifndef REF_LIBC_H
#define REF_LIBC_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* string.h */
void  *memcpy(void *d, const void *s, size_t n);
void  *memset(void *d, int c, size_t n);
void  *memmove(void *d, const void *s, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
size_t strnlen(const char *s, size_t max);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strcpy(char *d, const char *s);
char  *strncpy(char *d, const char *s, size_t n);
char  *strchr(const char *s, int c);
char  *strstr(const char *h, const char *n);
size_t strspn(const char *s, const char *set);
size_t strcspn(const char *s, const char *set);

/* stdio.h: there is no FILE; fflush(stdout) writes out what refcalls.c has buffered */
int    vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap);
int    snprintf(char *buf, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int    sscanf(const char *s, const char *fmt, ...);
#define stdout ((void *)1)
#define stderr ((void *)2)
int    fflush(void *stream);

/* stdlib.h */
int    atoi(const char *s);
void  *malloc(size_t n);
void  *calloc(size_t n, size_t size);
void   free(void *p);
char  *getenv(const char *name);                /* refcalls.c: the child's own settings first, then the environment */

/* math.h */
#define INFINITY (__builtin_inff())
#define NAN      (__builtin_nanf(""))
double fabs(double x);
double sqrt(double x);
double floor(double x);
double ceil(double x);
float  powf(float x, float y);

/* time.h */
typedef int64_t time_t;
struct tm { int tm_sec, tm_min, tm_hour, tm_mday, tm_mon, tm_year, tm_wday, tm_yday, tm_isdst; };
struct timespec { time_t tv_sec; int64_t tv_nsec; };
#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 6
struct tm *gmtime_r(const time_t *t, struct tm *out);
time_t     timegm(struct tm *tm);
char      *asctime_r(const struct tm *tm, char *buf);
time_t     time(time_t *out);                   /* refcalls.c: GetSystemTimeAsFileTime */
int        clock_gettime(int clock, struct timespec *out);      /* refcalls.c: QueryPerformanceCounter */

/* unistd.h, sys/wait.h, errno.h: the call tables fork once (rank 30), which Windows cannot;
 * refcalls.c replaces that rank's function, and these exist so that the original compiles. */
typedef int pid_t;
int    usleep(unsigned microseconds);           /* refcalls.c: Sleep, rounded up to a millisecond */
pid_t  fork(void);                              /* always -1 */
pid_t  waitpid(pid_t pid, int *status, int options);
void   _exit(int code) __attribute__((noreturn));
extern int errno;
#define EINTR 4
#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WTERMSIG(s)    ((s) & 0x7f)

#endif /* REF_LIBC_H */
