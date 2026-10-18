/* Ranks 10 to 30 of the first-call table (docs/findings/21-gate2-exports.md section 3), as
 * far as they are typed in.  The rows are in calls_10_30.inc; how to write an entry: calls.h.
 * Signatures and checks are the finding's; where an entry had to read a function to know how
 * to call it, the entry says what it read.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "rt/init.h"
#include "rt/machine.h"
#include "rt/thread.h"
#include "rt/time.h"
#include "rt/win/sync.h"
#include "calls.h"
#include "host.h"
#include "pool.h"

#define BOOL8(x)  ((uint8_t)(x))        /* a guest bool is al */

/* ---- 10. Uniform random numbers ------------------------------------------------------------- */

/* The finding's model: the state is the seed as given; a draw first replaces a state <= 0 by
 * max(-state, 1), then state = state * 48271 mod 2147483647. */
static int32_t rnd_next(int32_t *s) {
    if (*s <= 0) { int64_t v = -(int64_t)*s; *s = v < 1 || v > 0x7fffffff ? 1 : (int32_t)v; }
    *s = (int32_t)((uint64_t)*s * 48271u % 2147483647u);
    return *s;
}
static int32_t rnd_int(int32_t *s, int32_t lo, int32_t hi) {
    uint32_t n = (uint32_t)hi - (uint32_t)lo + 1;
    if (n <= 1 || n - 1 > 0x7fffffffu) return lo;
    uint32_t limit = 0x7fffffffu - 0x80000000u % n, x;
    do x = (uint32_t)rnd_next(s); while (x > limit);
    return (int32_t)((uint32_t)lo + x % n);
}

void gate2_run_10(Call *c) {
    static const int32_t seeds[] = { 1, 0, -5 };
    EXPORT(ctor, c->vstdlib, "??0CUniformRandomStream@@QEAA@XZ");
    EXPORT(set_seed, c->vstdlib, "?SetSeed@CUniformRandomStream@@UEAAXH@Z");
    EXPORT(get_seed, c->vstdlib, "?GetSeed@CUniformRandomStream@@UEAAHXZ");
    EXPORT(rand_int, c->vstdlib, "?RandomInt@CUniformRandomStream@@UEAAHHH@Z");
    EXPORT(g_seed, c->vstdlib, "RandomSeed");
    EXPORT(g_int, c->vstdlib, "RandomInt");
    EXPORT(g_get, c->vstdlib, "GetCurrentRandomSeed");
    EXPORT(g_zero_max, c->vstdlib, "RandomIntZeroMax");
    uint64_t *obj = gate2_mem(0x38);
    EXPECT("the constructor returns the object", GUEST_CALL(c->ctx, ctor, obj), (uint64_t)(uintptr_t)obj);
    EXPECT("the object's vptr - base", obj[0] - c->vstdlib->base, 0x5b020);     /* the exported vtable */
    for (unsigned k = 0; k < 3; k++) {
        int32_t s = seeds[k];
        GUEST_CALL(c->ctx, set_seed, obj, S32(seeds[k]));
        GUEST_CALL(c->ctx, g_seed, S32(seeds[k]));
        EXPECT("GetSeed after SetSeed", (int32_t)GUEST_CALL(c->ctx, get_seed, obj), seeds[k]);
        EXPECT("GetCurrentRandomSeed after RandomSeed", (int32_t)GUEST_CALL(c->ctx, g_get), seeds[k]);
        for (int i = 0; i < 1000; i++) {
            int32_t want = rnd_int(&s, 0, 99);
            int32_t got = (int32_t)GUEST_CALL(c->ctx, rand_int, obj, 0, 99);
            if (got != want) { gate2_fail_text(c, "seed %d, draw %d of RandomInt(0, 99) on the object: got %d, the model gives %d", seeds[k], i, got, want); return; }
            EXPECT("GetSeed tracks the state", (int32_t)GUEST_CALL(c->ctx, get_seed, obj), s);
            got = (int32_t)GUEST_CALL(c->ctx, g_int, 0, 99);
            if (got != want) { gate2_fail_text(c, "seed %d, draw %d of the global RandomInt(0, 99): got %d, the object gave %d", seeds[k], i, got, want); return; }
            EXPECT("GetCurrentRandomSeed tracks the state", (int32_t)GUEST_CALL(c->ctx, g_get), s);
        }
        if (k == 1) {                           /* seed 0 behaves as seed 1: the same state after 1000 draws */
            int32_t one = 1;
            for (int i = 0; i < 1000; i++) rnd_int(&one, 0, 99);
            EXPECT("state after 1000 draws from seed 0 against seed 1", s, one);
        }
        EXPECT("RandomInt(7, 7)", (int32_t)GUEST_CALL(c->ctx, rand_int, obj, 7, 7), 7);
        EXPECT("RandomInt(9, 3)", (int32_t)GUEST_CALL(c->ctx, rand_int, obj, 9, 3), 9);
        EXPECT("RandomInt(7, 7) and (9, 3) draw nothing", (int32_t)GUEST_CALL(c->ctx, get_seed, obj), s);
        /* RandomIntZeroMax was read (5 instructions): the installed stream's slot 3 with (0, 0x7fff). */
        EXPECT("RandomIntZeroMax", (int32_t)GUEST_CALL(c->ctx, g_zero_max), rnd_int(&s, 0, 0x7fff));
    }
}

/* ---- 11. CThreadMutex -------------------------------------------------------------------------- */

typedef struct { GUESTFN fn; void *obj; uint8_t got; } OneCall;
static void one_bool_call(Call *c, void *arg) {
    OneCall *o = arg;
    o->got = BOOL8(GUEST_CALL(c->ctx, o->fn, o->obj));
}

void gate2_run_11(Call *c) {
    EXPORT(ctor, c->tier0, "??0CThreadMutex@@QEAA@XZ");
    EXPORT(dtor, c->tier0, "??1CThreadMutex@@QEAA@XZ");
    EXPORT(lock, c->tier0, "?Lock@CThreadMutex@@QEAAXXZ");
    EXPORT(unlock, c->tier0, "?Unlock@CThreadMutex@@QEAAXXZ");
    EXPORT(try_lock, c->tier0, "?TryLock@CThreadMutex@@QEAA_NXZ");
    CRITICAL_SECTION *m = gate2_mem(0x30);
    memset(m, 0xEE, 0x30);
    GUEST_CALL(c->ctx, ctor, m);
    EXPECT("after the constructor: recursion count", m->RecursionCount, 0);
    EXPECT("after the constructor: owner", (uint64_t)m->OwningThread, 0);
    GUEST_CALL(c->ctx, lock, m);
    EXPECT("after Lock: recursion count", m->RecursionCount, 1);
    EXPECT("after Lock: owner", (uint64_t)m->OwningThread, RT_FIRST_THREAD_ID);
    EXPECT("TryLock on the owner", BOOL8(GUEST_CALL(c->ctx, try_lock, m)), 1);
    EXPECT("after TryLock: recursion count", m->RecursionCount, 2);
    /* From a second thread, while this one holds it.  TryLock goes through the pointer a static
     * constructor fetched (tier0.dll+0x19a400); were it NULL, TryLock would block here and the
     * driver's 60 s limit would report it. */
    EXPECT_TRUE("the TryEnterCriticalSection pointer fetched at load is not NULL", *(uint64_t *)(uintptr_t)(c->tier0->base + 0x19a400) != 0);
    OneCall o = { try_lock, m, 0xEE };
    if (!gate2_on_thread(c, one_bool_call, &o, NULL)) return;
    EXPECT("TryLock from a second thread while locked", o.got, 0);
    EXPECT("after the second thread's TryLock: recursion count", m->RecursionCount, 2);
    EXPECT("after the second thread's TryLock: owner", (uint64_t)m->OwningThread, RT_FIRST_THREAD_ID);
    GUEST_CALL(c->ctx, unlock, m);
    GUEST_CALL(c->ctx, unlock, m);
    EXPECT("after two Unlocks: recursion count", m->RecursionCount, 0);
    EXPECT("after two Unlocks: owner", (uint64_t)m->OwningThread, 0);
    GUEST_CALL(c->ctx, dtor, m);
}

/* ---- 12. Main-thread identity ---------------------------------------------------------------- */

typedef struct { GUESTFN in_main, declare; uint8_t before, after; } MainThread;
static void declare_main(Call *c, void *arg) {
    MainThread *m = arg;
    m->before = BOOL8(GUEST_CALL(c->ctx, m->in_main));
    GUEST_CALL(c->ctx, m->declare);
    m->after = BOOL8(GUEST_CALL(c->ctx, m->in_main));
}

void gate2_run_12(Call *c) {
    MainThread m = { .before = 0xEE, .after = 0xEE };
    m.in_main = gate2_export(c, c->tier0, "ThreadInMainThread");
    m.declare = gate2_export(c, c->tier0, "DeclareCurrentThreadIsMainThread");
    if (!m.in_main || !m.declare) return;
    EXPECT("ThreadInMainThread on the thread that loaded tier0", BOOL8(GUEST_CALL(c->ctx, m.in_main)), 1);
    if (!gate2_on_thread(c, declare_main, &m, NULL)) return;
    EXPECT("ThreadInMainThread on a second thread before it declares", m.before, 0);
    EXPECT("ThreadInMainThread on the second thread after it declares", m.after, 1);
    EXPECT("ThreadInMainThread on the first thread after the second declared", BOOL8(GUEST_CALL(c->ctx, m.in_main)), 0);
}

/* ---- 13. CThreadEvent and CThreadSemaphore ---------------------------------------------------- */

void gate2_run_13(Call *c) {
    EXPORT(ev_ctor, c->tier0, "??0CThreadEvent@@QEAA@_N@Z");
    EXPORT(ev_dtor, c->tier0, "??1CThreadEvent@@QEAA@XZ");
    EXPORT(ev_set, c->tier0, "?Set@CThreadEvent@@QEAA_NXZ");
    EXPORT(ev_reset, c->tier0, "?Reset@CThreadEvent@@QEAA_NXZ");
    EXPORT(ev_check, c->tier0, "?Check@CThreadEvent@@QEAA_NXZ");
    EXPORT(ev_wait, c->tier0, "?Wait@CThreadEvent@@QEAA_NI@Z");
    EXPORT(sem_ctor, c->tier0, "??0CThreadSemaphore@@QEAA@HH@Z");
    EXPORT(sem_dtor, c->tier0, "??1CThreadSemaphore@@QEAA@XZ");
    EXPORT(sem_release, c->tier0, "?Release@CThreadSemaphore@@QEAA_NHPEAH@Z");
    EXPORT(sem_wait, c->tier0, "?Wait@CThreadSyncObject@@QEAA_NI@Z");     /* the semaphore's Wait is its base class's */
    uint64_t *ev = gate2_mem(16), *sem = gate2_mem(16);
    int32_t *prev = gate2_mem(4);

    GUEST_CALL(c->ctx, ev_ctor, ev, 1);                                     /* manual reset */
    EXPECT_TRUE("the event has a handle", ev[0] != 0);
    EXPECT("Check on a new event", BOOL8(GUEST_CALL(c->ctx, ev_check, ev)), 0);
    EXPECT("Set", BOOL8(GUEST_CALL(c->ctx, ev_set, ev)), 1);
    EXPECT("Check after Set", BOOL8(GUEST_CALL(c->ctx, ev_check, ev)), 1);
    EXPECT("Check again (manual reset: still set)", BOOL8(GUEST_CALL(c->ctx, ev_check, ev)), 1);
    EXPECT("Reset", BOOL8(GUEST_CALL(c->ctx, ev_reset, ev)), 1);
    EXPECT("Check after Reset", BOOL8(GUEST_CALL(c->ctx, ev_check, ev)), 0);
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    EXPECT("Wait(20) on a reset event", BOOL8(GUEST_CALL(c->ctx, ev_wait, ev, 20)), 0);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (double)(t1.tv_sec - t0.tv_sec) * 1e3 + (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
    if (ms < 19.0 || ms > 2000.0) { gate2_fail_text(c, "Wait(20) on a reset event returned after %.1f ms", ms); return; }
    GUEST_CALL(c->ctx, ev_dtor, ev);

    GUEST_CALL(c->ctx, sem_ctor, sem, 1, 3);
    EXPECT_TRUE("the semaphore has a handle", sem[0] != 0);
    EXPECT("semaphore(1, 3): Wait(0)", BOOL8(GUEST_CALL(c->ctx, sem_wait, sem, 0)), 1);
    EXPECT("semaphore: second Wait(0)", BOOL8(GUEST_CALL(c->ctx, sem_wait, sem, 0)), 0);
    *prev = -1;
    EXPECT("Release(2, &prev)", BOOL8(GUEST_CALL(c->ctx, sem_release, sem, 2, prev)), 1);
    EXPECT("prev: the count before the release", *prev, 0);
    EXPECT("semaphore: Wait(0) after Release(2), first", BOOL8(GUEST_CALL(c->ctx, sem_wait, sem, 0)), 1);
    EXPECT("semaphore: Wait(0) after Release(2), second", BOOL8(GUEST_CALL(c->ctx, sem_wait, sem, 0)), 1);
    EXPECT("semaphore: Wait(0) after Release(2), third", BOOL8(GUEST_CALL(c->ctx, sem_wait, sem, 0)), 0);
    GUEST_CALL(c->ctx, sem_dtor, sem);
}

/* ---- 14. UTF-8 and UTF-16 conversion ----------------------------------------------------------- */

void gate2_run_14(Call *c) {
    /* ASCII, U+00E9 (2 bytes), U+20AC (3 bytes), U+1F600 (4 bytes, a surrogate pair), and "". */
    static const struct { const char *u8; uint16_t u16[8]; int n8, n16; } text[] = {
        { "Titan", { 'T', 'i', 't', 'a', 'n', 0 }, 6, 6 },
        { "a\xC3\xA9z", { 'a', 0x00E9, 'z', 0 }, 5, 4 },
        { "\xE2\x82\xAC", { 0x20AC, 0 }, 4, 2 },
        { "x\xF0\x9F\x98\x80", { 'x', 0xD83D, 0xDE00, 0 }, 6, 4 },
        { "", { 0 }, 1, 1 },
    };
    EXPORT(u8_to_uni, c->vstdlib, "V_UTF8ToUnicode");
    EXPORT(uni_to_u8, c->vstdlib, "V_UnicodeToUTF8");
    EXPORT(u8_to_ucs2, c->vstdlib, "V_UTF8ToUCS2");
    EXPORT(ucs2_to_u8, c->vstdlib, "V_UCS2ToUTF8");
    char *s8 = gate2_mem(64), *d8 = gate2_mem(64);
    uint16_t *d16 = gate2_mem(128), *s16 = gate2_mem(128);
    for (unsigned i = 0; i < sizeof text / sizeof text[0]; i++) {
        int n8 = text[i].n8, n16 = text[i].n16;         /* counts including the terminator */
        memcpy(s8, text[i].u8, (size_t)n8);
        memcpy(s16, text[i].u16, (size_t)n16 * 2);

        memset(d16, 0xEE, 128);
        EXPECT("V_UTF8ToUnicode: count with the terminator", (int32_t)GUEST_CALL(c->ctx, u8_to_uni, s8, d16, S32(128)), n16);
        EXPECT("V_UTF8ToUnicode: UTF-16 differs (memcmp)", memcmp(d16, text[i].u16, (size_t)n16 * 2) != 0, 0);
        memset(d8, 0xEE, 64);
        EXPECT("V_UnicodeToUTF8: count with the terminator", (int32_t)GUEST_CALL(c->ctx, uni_to_u8, d16, d8, S32(64)), n8);
        EXPECT("V_UnicodeToUTF8: the round trip differs (memcmp)", memcmp(d8, text[i].u8, (size_t)n8) != 0, 0);

        memset(d16, 0xEE, 128);
        EXPECT("V_UTF8ToUCS2: count with the terminator", (int32_t)GUEST_CALL(c->ctx, u8_to_ucs2, s8, S32(n8), d16, S32(128)), n16);
        EXPECT("V_UTF8ToUCS2: UTF-16 differs (memcmp)", memcmp(d16, text[i].u16, (size_t)n16 * 2) != 0, 0);
        memset(d8, 0xEE, 64);
        EXPECT("V_UCS2ToUTF8: count with the terminator", (int32_t)GUEST_CALL(c->ctx, ucs2_to_u8, s16, d8, S32(64)), n8);
        EXPECT("V_UCS2ToUTF8: UTF-8 differs (memcmp)", memcmp(d8, text[i].u8, (size_t)n8) != 0, 0);
    }
    EXPECT("V_UTF8ToUnicode with a NULL source", (int32_t)GUEST_CALL(c->ctx, u8_to_uni, 0, d16, S32(128)), 0);
    /* Destinations that are too small: 3 wide characters, 3 bytes.  The last element is a terminator. */
    memcpy(s8, "Titan", 6);
    memset(d16, 0xEE, 128);
    GUEST_CALL(c->ctx, u8_to_uni, s8, d16, S32(6));
    EXPECT("V_UTF8ToUnicode into 6 bytes: the last element", d16[2], 0);
    EXPECT("V_UTF8ToUnicode into 6 bytes: the element after the buffer", d16[3], 0xEEEE);
    memcpy(s16, text[0].u16, 12);
    memset(d8, 0xEE, 64);
    GUEST_CALL(c->ctx, uni_to_u8, s16, d8, S32(3));
    EXPECT("V_UnicodeToUTF8 into 3 bytes: the last byte", (uint8_t)d8[2], 0);
    EXPECT("V_UnicodeToUTF8 into 3 bytes: the byte after the buffer", (uint8_t)d8[3], 0xEE);
}

/* ---- 15. Plat_GetProcAddress ------------------------------------------------------------------- */

void gate2_run_15(Call *c) {
    EXPORT(f, c->tier0, "Plat_GetProcAddress");
    RtModule *k32 = rt_module_find("kernel32.dll");
    char *lib = gate2_mem(32), *name = gate2_mem(32);
    strcpy(lib, "Kernel32.dll");
    strcpy(name, "GetTickCount");
    uint64_t want = rt_module_export(k32, "GetTickCount", 0);
    EXPECT_TRUE("the runtime's kernel32 has GetTickCount", want != 0);
    EXPECT("Plat_GetProcAddress(Kernel32.dll, GetTickCount)", GUEST_CALL(c->ctx, f, lib, name), want);
    strcpy(name, "NoSuchExport");
    EXPECT("Plat_GetProcAddress(Kernel32.dll, NoSuchExport)", GUEST_CALL(c->ctx, f, lib, name), 0);
    strcpy(lib, "NoSuchLibrary.dll");
    strcpy(name, "X");
    EXPECT("Plat_GetProcAddress(NoSuchLibrary.dll, X)", GUEST_CALL(c->ctx, f, lib, name), 0);
    strcpy(lib, "vstdlib.dll");
    strcpy(name, "RandomInt");
    EXPECT("Plat_GetProcAddress(vstdlib.dll, RandomInt) - base", GUEST_CALL(c->ctx, f, lib, name) - c->vstdlib->base, 0x21bb0);
}

/* ---- calls through a vtable -------------------------------------------------------------------- */

/* Slot `n` of the object's vtable. */
static GUESTFN slot(uint64_t obj, unsigned n) {
    return ((const uint64_t *)(uintptr_t)*(const uint64_t *)(uintptr_t)obj)[n];
}
static const char *gstr(uint64_t p) { return (const char *)(uintptr_t)p; }
static char *gdup(const char *s) {
    char *p = gate2_mem(strlen(s) + 1);
    strcpy(p, s);
    return p;
}
#define EXPECT_STR(what, got, want) \
    do { uint64_t p_ = (got); if (!p_ || strcmp(gstr(p_), (want)) != 0) { \
        gate2_fail_text(c, "%s: got %s%.80s%s, expected \"%s\"", (what), p_ ? "\"" : "", p_ ? gstr(p_) : "NULL", p_ ? "\"" : "", (want)); return; } } while (0)

/* ---- 17. CommandLine() and the CCommandLine object -------------------------------------------- */

void gate2_run_17(Call *c) {
    static const char line[] = "game.exe -foo 5 +bar baz -neg -3";
    EXPORT(command_line, c->tier0, "CommandLine");
    uint64_t cl = GUEST_CALL(c->ctx, command_line);
    EXPECT("CommandLine() - base", cl - c->tier0->base, 0x6b040);
    EXPECT("the object's vptr - base", *(uint64_t *)(uintptr_t)cl - c->tier0->base, 0x521a8);
    GUEST_CALL(c->ctx, slot(cl, 1), cl, gdup(line));                        /* CreateCmdLine(const char *) */
    EXPECT_STR("GetCmdLine()", GUEST_CALL(c->ctx, slot(cl, 3), cl), line);
    EXPECT("ParmCount()", (int32_t)GUEST_CALL(c->ctx, slot(cl, 10), cl), 7);
    EXPECT("FindParm(-foo)", (int32_t)GUEST_CALL(c->ctx, slot(cl, 11), cl, gdup("-foo")), 1);
    EXPECT("FindParm(-FOO)", (int32_t)GUEST_CALL(c->ctx, slot(cl, 11), cl, gdup("-FOO")), 1);
    EXPECT("FindParm(-nope)", (int32_t)GUEST_CALL(c->ctx, slot(cl, 11), cl, gdup("-nope")), 0);
    EXPECT("ParmValue(-foo, 99)", (int32_t)GUEST_CALL(c->ctx, slot(cl, 8), cl, gdup("-foo"), 99), 5);
    EXPECT_STR("ParmValue(+bar, \"x\")", GUEST_CALL(c->ctx, slot(cl, 9), cl, gdup("+bar"), gdup("x")), "baz");
    EXPECT("ParmValue(-neg, 7)", (int32_t)GUEST_CALL(c->ctx, slot(cl, 8), cl, gdup("-neg"), 7), 7);
    EXPECT_STR("GetParm(4)", GUEST_CALL(c->ctx, slot(cl, 12), cl, 4), "baz");
    EXPECT_STR("GetParm(99)", GUEST_CALL(c->ctx, slot(cl, 12), cl, 99), "");
}

/* ---- 18. GetCPUInformation and the clock-speed data exports ------------------------------------ */

void gate2_run_18(Call *c) {
    EXPORT(get, c->tier0, "GetCPUInformation");
    EXPORT(p_speed, c->tier0, "g_ClockSpeed");
    EXPORT(p_dw, c->tier0, "g_dwClockSpeed");
    EXPORT(p_sec, c->tier0, "g_ClockSpeedSecondsMultiplier");
    EXPORT(p_ms, c->tier0, "g_ClockSpeedMillisecondsMultiplier");
    EXPORT(p_us, c->tier0, "g_ClockSpeedMicrosecondsMultiplier");
    uint64_t info = GUEST_CALL(c->ctx, get);
    EXPECT("GetCPUInformation() - base", info - c->tier0->base, 0xa99d8);
    const uint8_t *b = (const uint8_t *)(uintptr_t)info;
    int64_t speed, g_speed;
    uint32_t size, g_dw;
    double sec, ms, us;
    memcpy(&size, b, 4);
    memcpy(&speed, b + 8, 8);
    memcpy(&g_speed, gstr(p_speed), 8);
    memcpy(&g_dw, gstr(p_dw), 4);
    memcpy(&sec, gstr(p_sec), 8);
    memcpy(&ms, gstr(p_ms), 8);
    memcpy(&us, gstr(p_us), 8);
    EXPECT("first dword (the structure's size)", size, 0x18);
    EXPECT("qword at +8 against g_ClockSpeed", (uint64_t)speed, (uint64_t)g_speed);
    EXPECT("g_dwClockSpeed against the low half of g_ClockSpeed", g_dw, (uint32_t)g_speed);
    if (speed < 500000000 || speed > 6000000000) { gate2_fail_text(c, "the measured clock speed is %lld Hz, outside 5e8 to 6e9", (long long)speed); return; }
    double d = sec * (double)speed - 1.0;
    if (d < -1e-9 || d > 1e-9) { gate2_fail_text(c, "g_ClockSpeedSecondsMultiplier * speed = 1 %+g", d); return; }
    d = ms * (double)speed / 1e3 - 1.0;
    if (d < -1e-9 || d > 1e-9) { gate2_fail_text(c, "g_ClockSpeedMillisecondsMultiplier * speed / 1e3 = 1 %+g", d); return; }
    d = us * (double)speed / 1e6 - 1.0;
    if (d < -1e-9 || d > 1e-9) { gate2_fail_text(c, "g_ClockSpeedMicrosecondsMultiplier * speed / 1e6 = 1 %+g", d); return; }
    /* The byte at +5 is the one the routine stores GetSystemInfo's processor count in (read). */
    EXPECT("logical-processor byte (+5) against the machine description", b[5], rt_machine()->cpu_count);
    EXPECT("GetCPUInformation() a second time", GUEST_CALL(c->ctx, get), info);
    static char note[200];
    snprintf(note, sizeof note, "clock speed %lld Hz (the runtime's rdtsc is nominally %llu); flags +4 %#x, +7 %#x; logical %u, physical %u",
             (long long)speed, (unsigned long long)rt_machine()->cpu_hz, b[4], b[7], b[5], b[6]);
    gate2_note(c, note);
}

/* ---- 19. Logging channel lookup ----------------------------------------------------------------- */

void gate2_run_19(Call *c) {
    EXPORT(count, c->tier0, "LoggingSystem_GetChannelCount");
    EXPORT(find, c->tier0, "LoggingSystem_FindChannel");
    int32_t n = (int32_t)GUEST_CALL(c->ctx, count);
    if (n < 2) { gate2_fail(c, "LoggingSystem_GetChannelCount() (at least 2 expected)", (uint64_t)(int64_t)n, 2); return; }
    int32_t general = (int32_t)GUEST_CALL(c->ctx, find, gdup("General"));
    int32_t lower = (int32_t)GUEST_CALL(c->ctx, find, gdup("general"));
    int32_t loading = (int32_t)GUEST_CALL(c->ctx, find, gdup("LOADING"));
    if (general < 0 || general >= n) { gate2_fail_text(c, "FindChannel(General) = %d with %d channels", general, n); return; }
    if (loading < 0 || loading >= n) { gate2_fail_text(c, "FindChannel(LOADING) = %d with %d channels", loading, n); return; }
    EXPECT_TRUE("General and LOADING have distinct ids", general != loading);
    EXPECT("FindChannel(general) against FindChannel(General)", lower, general);
    EXPECT("FindChannel(nothing)", (int32_t)GUEST_CALL(c->ctx, find, gdup("nothing")), -1);
}

/* ---- 20. AllocateThreadID / FreeThreadID -------------------------------------------------------- */

typedef struct { GUESTFN alloc, release; uint32_t index; uint64_t got; } ThreadId;
static void second_thread_id(Call *c, void *arg) {
    ThreadId *t = arg;
    GUEST_CALL(c->ctx, t->alloc);
    t->got = rt_tls_get(c->ctx, t->index);
    GUEST_CALL(c->ctx, t->release);
}

void gate2_run_20(Call *c) {
    ThreadId t = { .got = 0xEE };
    t.alloc = gate2_export(c, c->tier0, "AllocateThreadID");
    t.release = gate2_export(c, c->tier0, "FreeThreadID");
    if (!t.alloc || !t.release) return;
    t.index = *(uint32_t *)(uintptr_t)(c->tier0->base + 0x19a3cc);
    EXPECT_TRUE("a static constructor allocated the TLS index at tier0.dll+0x19a3cc", t.index != 0xFFFFFFFFu && t.index < 64);
    GUEST_CALL(c->ctx, t.alloc);
    EXPECT("the first id", rt_tls_get(c->ctx, t.index), 1);
    GUEST_CALL(c->ctx, t.release);
    GUEST_CALL(c->ctx, t.alloc);
    EXPECT("the id after Free and Allocate again", rt_tls_get(c->ctx, t.index), 1);
    if (!gate2_on_thread(c, second_thread_id, &t, NULL)) return;
    EXPECT("the id a second thread gets while the first holds 1", t.got, 2);
    EXPECT("the first thread's id afterwards", rt_tls_get(c->ctx, t.index), 1);
    GUEST_CALL(c->ctx, t.release);
}

/* ---- 21. MemAllocScratch / MemFreeScratch -------------------------------------------------------- */

void gate2_run_21(Call *c) {
    EXPORT(alloc, c->tier0, "MemAllocScratch");
    EXPORT(release, c->tier0, "MemFreeScratch");
    uint8_t *a = (uint8_t *)(uintptr_t)GUEST_CALL(c->ctx, alloc, S32(100));
    EXPECT_TRUE("MemAllocScratch(100) is not NULL", a != NULL);
    /* The capacity the routine keeps (tier0.dll+0xb3dec, read from its code). */
    int32_t cap = *(int32_t *)(uintptr_t)(c->tier0->base + 0xb3dec);
    if (cap < 0x800) { gate2_fail(c, "the scratch buffer's capacity after the first allocation (at least 0x800 expected)", (uint64_t)(int64_t)cap, 0x800); return; }
    memset(a, 0xA1, 100);
    uint8_t *b = (uint8_t *)(uintptr_t)GUEST_CALL(c->ctx, alloc, S32(200));
    EXPECT("b - a", (uint64_t)(b - a), 100);
    memset(b, 0xB2, 200);
    GUEST_CALL(c->ctx, release);
    uint8_t *d = (uint8_t *)(uintptr_t)GUEST_CALL(c->ctx, alloc, S32(150));
    EXPECT("c - b", (uint64_t)(d - b), 0);
    memset(d, 0xC3, 150);
    for (int i = 0; i < 100; i++) EXPECT("a byte of the first block after the others were written", a[i], 0xA1);
    GUEST_CALL(c->ctx, release);
    GUEST_CALL(c->ctx, release);
    EXPECT("Alloc after everything was freed, against the first block", GUEST_CALL(c->ctx, alloc, S32(8)), (uint64_t)(uintptr_t)a);
    GUEST_CALL(c->ctx, release);
}

/* ---- 22. The game allocator ---------------------------------------------------------------------- */

void gate2_run_22(Call *c) {
    EXPORT(create, c->tier0, "CreateGlobalMemAlloc");
    EXPORT(op_new, c->tier0, "??2?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAPEAX_K@Z");
    EXPORT(op_delete, c->tier0, "??3?$CAlignedNewDelete@$07Ualigned_tmp_t@@@@SAXPEAX@Z");
    uint64_t al = GUEST_CALL(c->ctx, create);
    EXPECT_TRUE("CreateGlobalMemAlloc() is not NULL", al != 0);
    EXPECT("CreateGlobalMemAlloc() a second time", GUEST_CALL(c->ctx, create), al);
    EXPECT("the allocator's vptr - base", *(uint64_t *)(uintptr_t)al - c->tier0->base, 0x52fb8);
    uint8_t *p = (uint8_t *)(uintptr_t)GUEST_CALL(c->ctx, slot(al, 1), al, 100);          /* Alloc(size) */
    EXPECT_TRUE("Alloc(100) is not NULL", p != NULL);
    uint64_t size = GUEST_CALL(c->ctx, slot(al, 8), al, p);                                /* GetSize(ptr) */
    if (size < 100 || size > 0x10000) { gate2_fail_text(c, "GetSize of a 100-byte block is %llu", (unsigned long long)size); return; }
    for (int i = 0; i < 100; i++) p[i] = (uint8_t)(i * 7 + 1);
    uint8_t *q = (uint8_t *)(uintptr_t)GUEST_CALL(c->ctx, slot(al, 3), al, p, 1000);      /* Realloc(ptr, size) */
    EXPECT_TRUE("Realloc(p, 1000) is not NULL", q != NULL);
    for (int i = 0; i < 100; i++) EXPECT("a byte of the first 100 after Realloc", q[i], (uint8_t)(i * 7 + 1));
    size = GUEST_CALL(c->ctx, slot(al, 8), al, q);
    if (size < 1000) { gate2_fail_text(c, "GetSize after Realloc to 1000 is %llu", (unsigned long long)size); return; }
    memset(q, 0x5A, 1000);
    GUEST_CALL(c->ctx, slot(al, 5), al, q);                                                /* Free(ptr) */
    uint8_t *big = (uint8_t *)(uintptr_t)GUEST_CALL(c->ctx, slot(al, 1), al, 0x900);      /* the large path */
    EXPECT_TRUE("Alloc(0x900) is not NULL", big != NULL);
    size = GUEST_CALL(c->ctx, slot(al, 8), al, big);
    if (size < 0x900) { gate2_fail_text(c, "GetSize of a 0x900-byte block is %llu", (unsigned long long)size); return; }
    memset(big, 0xA5, 0x900);
    GUEST_CALL(c->ctx, slot(al, 5), al, big);
    uint64_t n = GUEST_CALL(c->ctx, op_new, 40);
    EXPECT_TRUE("CAlignedNewDelete operator new(40) is not NULL", n != 0);
    EXPECT("operator new(40): the pointer modulo 8", n & 7, 0);
    memset((void *)(uintptr_t)n, 0x77, 40);
    GUEST_CALL(c->ctx, op_delete, n);
    /* "delete frees the block it took from": the same size asked again gets the block back. */
    EXPECT("operator new(40) after delete, against the block deleted", GUEST_CALL(c->ctx, op_new, 40), n);
    EXPECT("g_pMemAllocSingleton is the allocator or still NULL (value 1 = neither)",
           *(uint64_t *)(uintptr_t)(c->tier0->base + 0xb42a8) != al && *(uint64_t *)(uintptr_t)(c->tier0->base + 0xb42a8) != 0, 0);
}

/* ---- 23. KeyValuesSystem ---------------------------------------------------------------------------- */

void gate2_run_23(Call *c) {
    EXPORT(get, c->vstdlib, "KeyValuesSystem");
    uint64_t kv = GUEST_CALL(c->ctx, get);
    EXPECT("KeyValuesSystem() - base", kv - c->vstdlib->base, 0x78f90);
    EXPECT("the object's vptr - base", *(uint64_t *)(uintptr_t)kv - c->vstdlib->base, 0x5c0d8);
    int32_t abc = (int32_t)GUEST_CALL(c->ctx, slot(kv, 3), kv, gdup("abc"), 1);            /* GetSymbolForString(name, create) */
    int32_t again = (int32_t)GUEST_CALL(c->ctx, slot(kv, 3), kv, gdup("abc"), 1);
    int32_t abd = (int32_t)GUEST_CALL(c->ctx, slot(kv, 3), kv, gdup("abd"), 1);
    EXPECT("GetSymbolForString(abc) a second time", again, abc);
    EXPECT_TRUE("abc and abd have different symbols", abc != abd);
    EXPECT_STR("GetStringForSymbol(abc)", GUEST_CALL(c->ctx, slot(kv, 4), kv, S32(abc)), "abc");
    EXPECT_STR("GetStringForSymbol(abd)", GUEST_CALL(c->ctx, slot(kv, 4), kv, S32(abd)), "abd");
    EXPECT("GetSymbolForString(abe, create = false)", (int32_t)GUEST_CALL(c->ctx, slot(kv, 3), kv, gdup("abe"), 0), -1);
}

/* ---- 24. Plat_FloatTime, Plat_MSTime, Timer_GetTimeUS, Plat_GetClockStart ------------------------ */

static double host_s(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

void gate2_run_24(Call *c) {
    EXPORT(float_time, c->tier0, "Plat_FloatTime");
    EXPORT(ms_time, c->tier0, "Plat_MSTime");
    EXPORT(us_time, c->tier0, "Timer_GetTimeUS");
    EXPORT(clock_start, c->tier0, "Plat_GetClockStart");
    EXPORT(bench, c->tier0, "Plat_SetBenchmarkMode");
    EXPORT(sleep, rt_module_find("kernel32.dll"), "Sleep");
    /* The host's clock is read around each call, so that a slow machine cannot fail the entry:
     * the guest's interval must lie between the host's inner and outer ones. */
    double h0 = host_s();
    double f1 = rt_guest_call_fp(c->ctx, float_time, NULL, 0);
    double h1 = host_s();
    if (!(f1 >= 0.0 && f1 <= h1 - h0 + 0.001)) { gate2_fail_text(c, "the first Plat_FloatTime() is %.6f s; the call took %.6f s", f1, h1 - h0); return; }
    GUEST_CALL(c->ctx, sleep, 50);
    double h2 = host_s();
    double f2 = rt_guest_call_fp(c->ctx, float_time, NULL, 0);
    double h3 = host_s();
    double d = f2 - f1;
    if (!(d >= 0.045 && d >= h2 - h1 - 0.001 && d <= h3 - h0 + 0.001)) {
        gate2_fail_text(c, "Plat_FloatTime advanced %.6f s across Sleep(50); the host's clock says between %.6f and %.6f", d, h2 - h1, h3 - h0);
        return;
    }
    if (d > 0.08 && h2 - h1 <= 0.08) { gate2_fail_text(c, "Plat_FloatTime advanced %.6f s across Sleep(50), above 0.08, and the host's clock says %.6f", d, h2 - h1); return; }
    EXPECT_TRUE("Plat_GetClockStart() is the counter stored at first use (not 0)", GUEST_CALL(c->ctx, clock_start) != 0);
    for (int i = 0; i < 2; i++) {
        uint32_t m1 = (uint32_t)GUEST_CALL(c->ctx, ms_time);
        int64_t us = (int64_t)GUEST_CALL(c->ctx, us_time);
        uint32_t m2 = (uint32_t)GUEST_CALL(c->ctx, ms_time);
        if (us < (int64_t)m1 * 1000 - 1000 || us > (int64_t)m2 * 1000 + 1000) {
            gate2_fail_text(c, "Timer_GetTimeUS() = %lld between Plat_MSTime() = %u and %u", (long long)us, m1, m2);
            return;
        }
        double f = rt_guest_call_fp(c->ctx, float_time, NULL, 0);
        if ((double)m2 > f * 1000.0 + 1.0 || (double)m1 < f2 * 1000.0 - 1.0) { gate2_fail_text(c, "Plat_MSTime() = %u, %u against Plat_FloatTime() = %.6f", m1, m2, f); return; }
    }
    /* Benchmark mode: each call adds the constant at tier0.dll+0x6c0e8 (read from the routine) to
     * a counter and returns the sum. */
    double step;
    memcpy(&step, gstr(c->tier0->base + 0x6c0e8), 8);
    if (step != 1.0 / 66.0) { gate2_fail_text(c, "the benchmark-mode step is %.17g, not 1/66", step); return; }
    GUEST_CALL(c->ctx, bench, 1);
    double b0 = rt_guest_call_fp(c->ctx, float_time, NULL, 0);
    for (int i = 0; i < 3; i++) {
        double b1 = rt_guest_call_fp(c->ctx, float_time, NULL, 0);
        if (b1 != b0 + step) { gate2_fail_text(c, "benchmark mode: Plat_FloatTime went from %.17g to %.17g, not by 1/66 exactly", b0, b1); return; }
        b0 = b1;
    }
    GUEST_CALL(c->ctx, bench, 0);
}

/* ---- 25. Local time ---------------------------------------------------------------------------------- */

void gate2_run_25(Call *c) {
    EXPORT(convert, c->tier0, "Plat_ConvertToLocalTime");
    EXPORT(local, c->tier0, "Plat_GetLocalTime");
    /* The zone the runtime reports to GetTimeZoneInformation is a fixed offset with no
     * daylight rule (rt/time.h: rt_time_zone_bias_minutes), so the expected fields are those
     * of UTC shifted by that offset, with tm_isdst 0. */
    int64_t off = -(int64_t)rt_time_zone_bias_minutes() * 60;
    int32_t *out = gate2_mem(64);
    time_t t = (time_t)(1700000000 + off);
    struct tm h;
    gmtime_r(&t, &h);
    const int want[9] = { h.tm_sec, h.tm_min, h.tm_hour, h.tm_mday, h.tm_mon, h.tm_year, h.tm_wday, h.tm_yday, 0 };
    memset(out, 0xEE, 64);
    GUEST_CALL(c->ctx, convert, (uint64_t)1700000000, out);
    for (unsigned k = 0; k < 9; k++)
        if (out[k] != want[k]) {
            gate2_fail_text(c, "Plat_ConvertToLocalTime(1700000000): field %u of struct tm is %d; UTC%+lld min gives %d",
                            k, out[k], (long long)(off / 60), want[k]);
            return;
        }
    EXPECT("Plat_ConvertToLocalTime: the byte after the 36 of struct tm", ((uint8_t *)out)[36], 0xEE);
    memset(out, 0xEE, 64);
    time_t before = time(NULL);
    GUEST_CALL(c->ctx, local, out);
    time_t after = time(NULL);
    struct tm g = { .tm_sec = out[0], .tm_min = out[1], .tm_hour = out[2], .tm_mday = out[3], .tm_mon = out[4], .tm_year = out[5] };
    int64_t got = (int64_t)timegm(&g) - off;
    if (got < (int64_t)before - 2 || got > (int64_t)after + 2) {
        gate2_fail_text(c, "Plat_GetLocalTime is %lld s away from the host's time in the zone UTC%+lld min", (long long)(got - (int64_t)before), (long long)(off / 60));
        return;
    }
}

/* ---- 26. Msg / Warning with a logging listener ----------------------------------------------------- */

void gate2_run_26(Call *c) {
    static Gate2Listener rec;
    EXPORT(reg, c->tier0, "LoggingSystem_RegisterLoggingListener");
    EXPORT(msg, c->tier0, "Msg");
    EXPORT(warning, c->tier0, "Warning");
    EXPORT(con_msg, c->tier0, "?ConMsg@@YAXPEBDZZ");
    GUEST_CALL(c->ctx, reg, gate2_listener(&rec));
    GUEST_CALL(c->ctx, msg, gdup("n=%d s=%s\n"), 7, gdup("x"));
    EXPECT("calls of the listener's Log slot after Msg", rec.calls, 1);
    if (strcmp(rec.text[0], "n=7 s=x\n") != 0) { gate2_fail_text(c, "Msg: the listener received \"%.80s\"", rec.text[0]); return; }
    EXPECT("Msg: severity (0 = message)", rec.severity[0], 0);
    GUEST_CALL(c->ctx, warning, gdup("w=%d %s\n"), -3, gdup("yz"));
    /* Warning in this build makes two Log calls when the line before it was ended: first the
     * prefix "Error: [n] " with a running count (tier0.dll+0xe5e0, read after the entry first
     * failed expecting one call), then the text.  Both carry the warning severity. */
    if (rec.calls != 3 || strcmp(rec.text[1], "Error: [1] ") != 0 || strcmp(rec.text[2], "w=-3 yz\n") != 0) {
        gate2_fail_text(c, "Warning: the listener's Log slot was called %u times: \"%.60s\" (severity %u), then \"%.60s\" (severity %u); "
                           "expected the prefix \"Error: [1] \" and then the text",
                        rec.calls - 1, rec.text[1], rec.severity[1], rec.text[2], rec.severity[2]);
        return;
    }
    EXPECT("Warning: severity of the prefix (1 = warning)", rec.severity[1], 1);
    EXPECT("Warning: severity of the text (1 = warning)", rec.severity[2], 1);
    EXPECT("Warning: the prefix and the text are on the channel Msg used", rec.channel[1] == rec.channel[0] && rec.channel[2] == rec.channel[0], 1);
    GUEST_CALL(c->ctx, con_msg, gdup("%s=%u\n"), gdup("c"), 42);
    EXPECT("calls of the listener's Log slot after ConMsg", rec.calls, 4);
    if (strcmp(rec.text[3], "c=42\n") != 0) { gate2_fail_text(c, "ConMsg: the listener received \"%.80s\"", rec.text[3]); return; }
    EXPECT("ConMsg: severity (0 = message)", rec.severity[3], 0);
    EXPECT("calls of the listener's slot 0", rec.slot0_calls, 0);
}

/* ---- 27. CreateSimpleThread / ThreadJoin / ThreadSleep / ThreadGetPriority ------------------------ */

void gate2_run_27(Call *c) {
    EXPORT(create, c->tier0, "CreateSimpleThread");
    EXPORT(join, c->tier0, "ThreadJoin");
    EXPORT(sleep, c->tier0, "ThreadSleep");
    EXPORT(priority, c->tier0, "ThreadGetPriority");
    EXPORT(fn, c->tier0, "Plat_GetMemPageSize");
    unsigned before = rt_thread_count();
    HANDLE h = (HANDLE)(uintptr_t)GUEST_CALL(c->ctx, create, fn, 0, 0);
    EXPECT_TRUE("CreateSimpleThread returns a handle", h != NULL);
    EXPECT("ThreadJoin(handle, 60000)", BOOL8(GUEST_CALL(c->ctx, join, h, 60000)), 1);
    DWORD code = 0xEEEEEEEEu;
    EXPECT_TRUE("the handle is a thread's", rt_thread_get_exit_code(h, &code));
    EXPECT("the thread's exit code", code, 4);
    EXPECT("ThreadGetPriority(handle) (0 = normal)", (int32_t)GUEST_CALL(c->ctx, priority, h), 0);
    EXPECT("ThreadGetPriority(0), the calling thread", (int32_t)GUEST_CALL(c->ctx, priority, 0), 0);
    EXPECT("guest threads after the join", rt_thread_count(), before);
    double h0 = host_s();
    GUEST_CALL(c->ctx, sleep, 20);
    double d = host_s() - h0;
    if (d < 0.019) { gate2_fail_text(c, "ThreadSleep(20) returned after %.1f ms", d * 1e3); return; }
}

/* ---- 28. ConfigureAndInitJobThreads with no workers -------------------------------------------------- */

void gate2_run_28(Call *c) {
    EXPORT(command_line, c->tier0, "CommandLine");
    EXPORT(configure, c->tier0, "ConfigureAndInitJobThreads");
    EXPORT(workers, c->tier0, "?JT_NumWorkerThreads@@YAIXZ");
    uint64_t cl = GUEST_CALL(c->ctx, command_line);
    EXPECT_TRUE("CommandLine() is not NULL", cl != 0);
    GUEST_CALL(c->ctx, slot(cl, 1), cl, gdup("game.exe -numworkerthreads 0"));
    unsigned before = rt_thread_count();
    GUEST_CALL(c->ctx, configure);
    EXPECT("JT_NumWorkerThreads()", (uint32_t)GUEST_CALL(c->ctx, workers), 0);
    EXPECT("guest threads after the call", rt_thread_count(), before);
}

/* ---- 29. Built-in self tests ------------------------------------------------------------------------- */

static Gate2Pool pool29;

/* On the pool watcher's host thread (gate2/pool.h), when the round in progress is the stop
 * that the test routine's own code leads to.  Records FAIL and ends the child. */
static void pool29_verdict(Call *c) {
    const Gate2PoolRound *r = &pool29.now;
    gate2_fail_text(c, "RunThreadPoolTests does not return, and as its code reads cannot (on Windows either): round %u of its test "
                       "routine (%u threads) gave the pool the same %u jobs that an earlier round ran to their end; the pool's add "
                       "routine (vstdlib.dll+0x156a0) refuses a finished job, so %u of %u were refused, 0 counted, and the routine "
                       "waits without limit for \"all counted\". Before it: %u rounds, %u with every job counted, %u with none "
                       "taken. GATE2_POOL_REARM=1 runs the pool through every round (docs/handoff/gate2-threads.md, stop 8)",
                    pool29.rounds + 1, r->threads, r->adds, r->adds - r->taken, r->adds, pool29.rounds, pool29.rounds_all_counted,
                    pool29.rounds_none_taken);
    gate2_end(c);
}

void gate2_run_29(Call *c) {
    /* The finding types all three as void(void) from their names.  The two tier0 routines take
     * (int listSize, int tests) and return a bool: each stores ecx as the size, refuses more than
     * 0x10000 and passes edx on as the repeat count, and vstdlib's RunThreadPoolTests itself calls
     * both with (10000, 1) before its own tests (read after a first run with no arguments ended in
     * a bad_alloc or in no answer, depending on what the registers happened to hold).  They report
     * their own failures through Msg, as lines that contain "FAIL" or "invalid value". */
    static Gate2Listener rec;
    static const struct { int vstdlib, args; const char *name, *key; } tests[3] = {
        { 0, 1, "RunTSListTests", "list" }, { 0, 1, "RunTSQueueTests", "queue" }, { 1, 0, "RunThreadPoolTests", "pool" } };
    static char note[400];
    /* GATE2_SELFTESTS=<list of list, queue, pool>: run only those (for counting an intermittent
     * fault of one of them; RunThreadPoolTests itself begins with the other two).  Unset: all. */
    const char *only = getenv("GATE2_SELFTESTS");
    if (only && !*only) only = NULL;
    EXPORT(reg, c->tier0, "LoggingSystem_RegisterLoggingListener");
    GUEST_CALL(c->ctx, reg, gate2_listener(&rec));
    rec.echo = true;                            /* a STOP then names the last line a test printed */
    unsigned said[3] = { 0, 0, 0 }, last = 0, before = rt_thread_count();
    for (int i = 0; i < 3; i++) {
        if (only && !strstr(only, tests[i].key)) continue;
        GUESTFN f = gate2_export(c, tests[i].vstdlib ? c->vstdlib : c->tier0, tests[i].name);
        if (!f) return;
        gate2_progress(tests[i].name);
        if (tests[i].vstdlib) {
            /* The pool's Start, Stop and add routines are counted from here on, and a host
             * thread ends the entry with FAIL if the test routine reaches the stop its own
             * code leads to (gate2/pool.h); GATE2_POOL_IDLE is how long nothing must have
             * happened before it looks, in milliseconds. */
            const char *idle = getenv("GATE2_POOL_IDLE");
            if (!gate2_pool_watch(c, &pool29)) return;
            gate2_pool_watcher(c, idle && atoi(idle) > 0 ? (unsigned)atoi(idle) : 3000, pool29_verdict);
        }
        if (tests[i].args) EXPECT(tests[i].name, BOOL8(GUEST_CALL(c->ctx, f, 10000, 1)), 1);
        else               GUEST_CALL(c->ctx, f);
        said[i] = rec.calls - last;
        last = rec.calls;
        if (rec.fails) {
            for (char *p = rec.first_fail; *p; p++) if (*p == '\n') *p = ' ';
            gate2_fail_text(c, "%s reported %u failure(s) of its own; the first: \"%.150s\"", tests[i].name, rec.fails, rec.first_fail);
            return;
        }
        if (rec.worst >= 1) { gate2_fail_text(c, "%s raised a message of severity %u", tests[i].name, rec.worst); return; }
    }
    EXPECT("guest threads after the three tests", rt_thread_count(), before);
    int n = snprintf(note, sizeof note, "messages received by the listener: %u, %u, %u, none with FAIL%s%s", said[0], said[1], said[2],
                     only ? "; only GATE2_SELFTESTS=" : "", only ? only : "");
    if (pool29.rounds && n > 0 && (size_t)n < sizeof note)
        snprintf(note + n, sizeof note - (size_t)n, "; the pool test: %u rounds, %u with every job counted, %u with none taken, %lu jobs taken, "
                 "%lu counted%s", pool29.rounds, pool29.rounds_all_counted, pool29.rounds_none_taken, pool29.total_taken, pool29.total_counted,
                 pool29.rearm ? "; A DIAGNOSTIC RUN, GATE2_POOL_REARM=1: the driver re-armed finished jobs, without which the test does not return" : "");
    gate2_note(c, note);
}

/* ---- 30. Plat_ExitProcess ---------------------------------------------------------------------------- */

void gate2_run_30(Call *c) {
    EXPORT(f, c->tier0, "Plat_ExitProcess");
    /* This entry is already a child of the driver; the call is made in a child of its own. */
    fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { gate2_fail_text(c, "fork failed"); return; }
    if (pid == 0) {
        GUEST_CALL(c->ctx, f, 7);
        _exit(99);                              /* Plat_ExitProcess returned */
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    if (!WIFEXITED(status)) { gate2_fail_text(c, "the child that called Plat_ExitProcess(7) ended by signal %d", WTERMSIG(status)); return; }
    EXPECT("exit status of the child that called Plat_ExitProcess(7)", WEXITSTATUS(status), 7);
}
