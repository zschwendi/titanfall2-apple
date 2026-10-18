/* The first-call table: the 30 items of docs/findings/21-gate2-exports.md section 3, cheapest
 * first.  The texts of each row (title, call, check, also) are the finding's.  Ranks 1 to 9
 * are typed in below; rows 10 to 30 (calls_10_30.inc) carry the texts and no code yet, and
 * report SKIP.  How to write an entry: calls.h.
 *
 * Export names are the DLLs' own spellings (`gate2 registry --exports` lists them); the
 * C++ ones are Microsoft-mangled.
 */
#include <time.h>
#include <string.h>

#include "rt/init.h"
#include "rt/machine.h"
#include "calls.h"
#include "host.h"

/* 1. Plat_GetMemPageSize */
static void run_1(Call *c) {
    EXPORT(f, c->tier0, "Plat_GetMemPageSize");
    EXPECT("Plat_GetMemPageSize()", (uint32_t)GUEST_CALL(c->ctx, f), 4);
}

/* 2. Mode flags */
static void run_2(Call *c) {
    static const char *const names[3][2] = { { "Plat_SetToolMode", "Plat_IsInToolMode" },
                                              { "Plat_SetBenchmarkMode", "Plat_IsInBenchmarkMode" },
                                              { "Plat_SetRenderToolMode", "Plat_IsInRenderToolMode" } };
    GUESTFN set[3], get[3];
    for (int i = 0; i < 3; i++) {
        set[i] = gate2_export(c, c->tier0, names[i][0]);
        get[i] = gate2_export(c, c->tier0, names[i][1]);
        if (!set[i] || !get[i]) return;
    }
    for (int i = 0; i < 3; i++) {
        GUEST_CALL(c->ctx, set[i], 1);
        EXPECT(names[i][1], (uint8_t)GUEST_CALL(c->ctx, get[i]), 1);
        GUEST_CALL(c->ctx, set[i], 0);
        EXPECT(names[i][1], (uint8_t)GUEST_CALL(c->ctx, get[i]), 0);
    }
    for (int i = 0; i < 3; i++) {               /* one flag set: the other two did not move */
        GUEST_CALL(c->ctx, set[i], 1);
        for (int k = 0; k < 3; k++) EXPECT(names[k][1], (uint8_t)GUEST_CALL(c->ctx, get[k]), k == i);
        GUEST_CALL(c->ctx, set[i], 0);
    }
}

/* 3. EncodeBinaryToString / DecodeBinaryFromString */
static void run_3(Call *c) {
    static const int sizes[] = { 0, 1, 6, 7, 8, 255, 4096 };
    EXPORT(enc, c->tier0, "EncodeBinaryToString");
    EXPORT(dec, c->tier0, "DecodeBinaryFromString");
    uint8_t *src = gate2_mem(4096), *str = gate2_mem(8192), *out = gate2_mem(4096);
    uint64_t *end = gate2_mem(8);
    uint32_t seed = 0x2545F491u;
    for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++) {
        int n = sizes[i], len = n + 4 + (n + 6) / 7;            /* string length without the terminator */
        for (int k = 0; k < n; k++) { seed = seed * 1664525u + 1013904223u; src[k] = (uint8_t)(seed >> 24); }
        memset(str, 0, 8192);
        EXPECT("EncodeBinaryToString: length", (int32_t)GUEST_CALL(c->ctx, enc, src, S32(n), str, S32(len + 1)), len);
        for (int k = 0; k < len; k++) EXPECT("EncodeBinaryToString: an output byte below 0x80 (value 1 = it is)", str[k] < 0x80, 0);
        EXPECT("EncodeBinaryToString: capacity one too small", (int32_t)GUEST_CALL(c->ctx, enc, src, S32(n), out, S32(len)), -(len + 1));
        memset(out, 0xEE, 4096);
        *end = 0;
        EXPECT("DecodeBinaryFromString: length", (int32_t)GUEST_CALL(c->ctx, dec, str, out, S32(n), end), n);
        EXPECT("DecodeBinaryFromString: *end - in", *end - (uint64_t)(uintptr_t)str, len);
        EXPECT("DecodeBinaryFromString: bytes differ from the source (memcmp)", memcmp(out, src, (size_t)n) != 0, 0);
        if (n > 0)
            EXPECT("DecodeBinaryFromString: output one byte too small", (int32_t)GUEST_CALL(c->ctx, dec, str, out, S32(n - 1), end), -n);
    }
    static const char bad[] = "\x01\x02\x03\x04\x85\x86\x87\x88";       /* first four bytes below 0x80 */
    memcpy(str, bad, sizeof bad);
    EXPECT("DecodeBinaryFromString: malformed input", (uint32_t)GUEST_CALL(c->ctx, dec, str, out, S32(64), end), 0x80000000u);
}

/* 4. V_UnicodeToUCS2 / V_UCS2ToUnicode */
static void run_4(Call *c) {
    static const uint16_t text[11] = { 'T', 'i', 't', 'a', 'n', 0x00E9, 0x20AC, 'l', 'l', '2', 0 };
    static const int lens[] = { 0, 1, 10 };
    EXPORT(to_ucs2, c->vstdlib, "V_UnicodeToUCS2");
    EXPORT(to_uni, c->vstdlib, "V_UCS2ToUnicode");
    uint16_t *src = gate2_mem(64), *dst = gate2_mem(64);
    for (unsigned i = 0; i < 3; i++) {
        int len = lens[i], need = 2 * (len + 1);
        memcpy(src, text, (size_t)len * 2);
        src[len] = 0;
        for (int d = need - 2; d <= need + 8; d += d < need ? 2 : 8) {     /* below, equal to, above the need */
            if (d < 2) continue;
            int want = d < need ? d : need;
            memset(dst, 0xEE, 64);
            EXPECT("V_UnicodeToUCS2: bytes copied", (int32_t)GUEST_CALL(c->ctx, to_ucs2, src, S32(need), dst, S32(d)), want);
            EXPECT("V_UnicodeToUCS2: contents differ (memcmp)", memcmp(dst, src, (size_t)want - 2) != 0, 0);
            memset(dst, 0xEE, 64);
            EXPECT("V_UCS2ToUnicode: bytes copied", (int32_t)GUEST_CALL(c->ctx, to_uni, src, dst, S32(d)), want);
            EXPECT("V_UCS2ToUnicode: terminator", dst[want / 2 - 1], 0);
            EXPECT("V_UCS2ToUnicode: contents differ (memcmp)", memcmp(dst, src, (size_t)want - 2) != 0, 0);
        }
    }
}

/* 5. V_UTF8_strncpy */
static void run_5(Call *c) {
    /* "a", U+00E9 (2 bytes), U+20AC (3), U+1F600 (4), "z": characters end at these offsets */
    static const char text[] = "a\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80z";
    static const int ends[] = { 0, 1, 3, 6, 10, 11 };
    EXPORT(f, c->vstdlib, "V_UTF8_strncpy");
    char *src = gate2_mem(32), *dst = gate2_mem(32);
    memcpy(src, text, sizeof text);
    for (int max = 1; max <= 14; max++) {       /* cuts inside each kind of character and at each boundary */
        int want = 0;
        for (unsigned k = 0; k < sizeof ends / sizeof ends[0]; k++) if (ends[k] <= max - 1) want = ends[k];
        memset(dst, 0xEE, 32);
        EXPECT("V_UTF8_strncpy: return value - dst", GUEST_CALL(c->ctx, f, dst, src, max) - (uint64_t)(uintptr_t)dst, 0);
        EXPECT("V_UTF8_strncpy: length of the result", strnlen(dst, 32), want);
        EXPECT("V_UTF8_strncpy: result is not a prefix of the source (memcmp)", memcmp(dst, src, (size_t)want) != 0, 0);
    }
}

/* 6. CThreadSpinRWLock */
static void run_6(Call *c) {
    EXPORT(ctor, c->tier0, "??0CThreadSpinRWLock@@QEAA@XZ");
    EXPORT(try_read, c->tier0, "?TryLockForRead@CThreadSpinRWLock@@QEAA_NXZ");
    EXPORT(unlock_read, c->tier0, "?UnlockRead@CThreadSpinRWLock@@QEAAXXZ");
    EXPORT(try_write, c->tier0, "?TryLockForWrite@CThreadSpinRWLock@@QEAA_NXZ");
    EXPORT(unlock_write, c->tier0, "?UnlockWrite@CThreadSpinRWLock@@QEAAXXZ");
    EXPORT(is_read, c->tier0, "?IsLockedForRead@CThreadSpinRWLock@@QEAA_NXZ");
    EXPORT(is_write, c->tier0, "?IsLockedForWrite@CThreadSpinRWLock@@QEAA_NXZ");
    uint32_t *lock = gate2_mem(8);
    lock[0] = lock[1] = 0xEEEEEEEEu;
    GUEST_CALL(c->ctx, ctor, lock);
    EXPECT("after the constructor: first dword", lock[0], 0);
    EXPECT("TryLockForRead (1)", (uint8_t)GUEST_CALL(c->ctx, try_read, lock), 1);
    EXPECT("TryLockForRead (2)", (uint8_t)GUEST_CALL(c->ctx, try_read, lock), 1);
    EXPECT("two readers: first dword", lock[0], 2);
    EXPECT("IsLockedForRead", (uint8_t)GUEST_CALL(c->ctx, is_read, lock), 1);
    EXPECT("TryLockForWrite while read-locked", (uint8_t)GUEST_CALL(c->ctx, try_write, lock), 0);
    GUEST_CALL(c->ctx, unlock_read, lock);
    GUEST_CALL(c->ctx, unlock_read, lock);
    EXPECT("readers gone: first dword", lock[0], 0);
    EXPECT("TryLockForWrite", (uint8_t)GUEST_CALL(c->ctx, try_write, lock), 1);
    EXPECT("write-locked: first dword", lock[0], 0x10000);
    EXPECT("write-locked: second dword (the writer's thread id)", lock[1], RT_FIRST_THREAD_ID);
    EXPECT("IsLockedForWrite", (uint8_t)GUEST_CALL(c->ctx, is_write, lock), 1);
    EXPECT("TryLockForRead while write-locked", (uint8_t)GUEST_CALL(c->ctx, try_read, lock), 0);
    GUEST_CALL(c->ctx, unlock_write, lock);
    EXPECT("unlocked: first dword", lock[0], 0);
}

/* 7. Thread-local read and write */
typedef struct { GUESTFN fn; uint32_t got; } OneRead;
static void read_u32(Call *c, void *arg) {
    OneRead *r = arg;
    r->got = (uint32_t)GUEST_CALL(c->ctx, r->fn);
}

static void run_7(Call *c) {
    EXPORT(get, c->tier0, "?JT_GetCurrentThread@@YAIXZ");
    EXPORT(set, c->tier0, "?JT_SetThreadIdx@@YAXI@Z");
    EXPORT(workers, c->tier0, "?JT_NumWorkerThreads@@YAIXZ");
    EXPECT("JT_GetCurrentThread on a fresh thread (the TLS template's value)", (uint32_t)GUEST_CALL(c->ctx, get), 28);
    GUEST_CALL(c->ctx, set, 5);
    EXPECT("JT_GetCurrentThread after JT_SetThreadIdx(5)", (uint32_t)GUEST_CALL(c->ctx, get), 5);
    EXPECT("JT_NumWorkerThreads before ConfigureAndInitJobThreads", (uint32_t)GUEST_CALL(c->ctx, workers), 0);
    OneRead r = { get, 0xEEEEEEEEu };
    if (!gate2_on_thread(c, read_u32, &r, NULL)) return;
    EXPECT("JT_GetCurrentThread on a second guest thread (its own copy of the template)", r.got, 28);
    EXPECT("JT_GetCurrentThread on the first thread afterwards", (uint32_t)GUEST_CALL(c->ctx, get), 5);
}

/* 8. CreateInterface and VStdLib_GetICVarFactory */
static void run_8(Call *c) {
    static const struct { const char *name; uint32_t rva; } known[3] = {
        { "VEngineCvar007", 0x72080 }, { "EventSystem001", 0x72370 }, { "VProcessUtils002", 0x743b0 } };
    static const char *const unknown[2] = { "NoSuchThing", "vengineCVAR007" };
    EXPORT(create, c->vstdlib, "CreateInterface");
    EXPORT(get_factory, c->vstdlib, "VStdLib_GetICVarFactory");
    uint64_t base = c->vstdlib->base;
    int32_t *rc = gate2_mem(4);
    char *name = gate2_mem(32);
    for (int i = 0; i < 3; i++) {
        strcpy(name, known[i].name);
        *rc = -1;
        EXPECT(known[i].name, GUEST_CALL(c->ctx, create, name, rc) - base, known[i].rva);
        EXPECT("CreateInterface: *returnCode for a known name", *rc, 0);
    }
    for (int i = 0; i < 2; i++) {
        strcpy(name, unknown[i]);
        *rc = -1;
        EXPECT(unknown[i], GUEST_CALL(c->ctx, create, name, rc), 0);
        EXPECT("CreateInterface: *returnCode for an unknown name", *rc, 1);
    }
    strcpy(name, known[0].name);
    EXPECT("CreateInterface with a NULL returnCode", GUEST_CALL(c->ctx, create, name, 0) - base, known[0].rva);
    GUESTFN factory = GUEST_CALL(c->ctx, get_factory);
    EXPECT("VStdLib_GetICVarFactory() - base", factory - base, 0x26140);
    EXPECT("the factory's answer for VEngineCvar007", GUEST_CALL(c->ctx, factory, name, rc) - base, known[0].rva);
}

/* 9. One-shim leaves */
static void run_9(Call *c) {
    EXPORT(cmd_a, c->tier0, "Plat_GetCommandLineA");
    EXPORT(cmd, c->tier0, "Plat_GetCommandLine");
    EXPORT(mem_free, c->tier0, "GetFreeMemory");
    EXPORT(mem_total, c->tier0, "GetTotalMemory");
    EXPORT(os_version, c->tier0, "Plat_GetOSVersion");
    EXPORT(shim, rt_module_find("kernel32.dll"), "GetCommandLineA");
    uint64_t line = GUEST_CALL(c->ctx, shim);
    EXPECT_TRUE("the GetCommandLineA shim returns a pointer", line != 0);
    EXPECT("Plat_GetCommandLineA() against the shim", GUEST_CALL(c->ctx, cmd_a), line);
    EXPECT("Plat_GetCommandLine() against the shim", GUEST_CALL(c->ctx, cmd), line);
    uint64_t total = GUEST_CALL(c->ctx, mem_total), avail = GUEST_CALL(c->ctx, mem_free);
    EXPECT_TRUE("GetFreeMemory() > 0", avail > 0);
    EXPECT_TRUE("GetTotalMemory() >= GetFreeMemory()", total >= avail);
    EXPECT("GetTotalMemory() against the machine description's physical memory", total, rt_machine()->phys_total);
    EXPECT("Plat_GetOSVersion() against the major version the runtime reports", (int32_t)GUEST_CALL(c->ctx, os_version), rt_machine()->os_major);
}

/* 16. Plat_gmtime
 * Corrected 2026-10-10 (unit gate2-calls-wide; docs/handoff/gate2-run-2.md, stop 5): the finding
 * listed 253402300799 (year 9999) as valid.  The Visual Studio C runtime's _gmtime64_s refuses
 * what lies after its documented range, which ends with 3000-12-31 23:59:59 UTC (32535215999);
 * its check allows 8 h + 13 h more for local-time arithmetic (75,600 s).  Expected values of
 * the accepted times: the host's gmtime_r.  Expected refusal: EINVAL (22), the documented
 * return of _gmtime64_s for a time out of range.
 *
 * Corrected again 2026-10-10 (unit ref-rank16; docs/handoff/ref-rank16.md): the two limits
 * were enumerators.  Where the compiler follows Microsoft's rules an enumerator is an int, so
 * the reference program (tools/ref, this file compiled for Windows) held -1824522369 where
 * the driver held 32535215999, and the real DLL was asked about a day in 1912.  They are
 * 64-bit constants now, and the assertion below does not compile where one is cut.
 *
 * What the real DLL answered is kept as a call of its own: Plat_gmtime(-1824522369) returns 22
 * (the run of 2026-10-10 against the real tier0.dll, build/ref/results/calls.txt).  Microsoft's
 * table of error conditions for _gmtime64_s says the same and says what is left behind: for a
 * time below 0, EINVAL and "All fields set to -1".
 *
 * Where exactly the range begins is recorded and not expected.  That table puts it at 0; the
 * DLL's own instruction (tier0+0x28f11) compares the time with -43200, twelve hours before
 * 1970.  The return values and the fields for -1, -43200 and -43201 go into the record, so
 * that a run against the real DLL is compared with ours on them (gate2/values.h, kind M). */
#define GMTIME_LAST_DOCUMENTED INT64_C(32535215999)
#define GMTIME_ALLOWANCE       INT64_C(75600)
#define GMTIME_BEFORE_1970     INT64_C(-1824522369)    /* 1912-03-08; the low 32 bits of the limit above */
_Static_assert(GMTIME_LAST_DOCUMENTED + GMTIME_ALLOWANCE == 32535291599 && sizeof(GMTIME_LAST_DOCUMENTED) == 8,
               "rank 16's limits are 64-bit values for every compiler that builds this table");
_Static_assert((int32_t)(uint32_t)GMTIME_LAST_DOCUMENTED == GMTIME_BEFORE_1970, "the time the reference run of 2026-10-10 passed");

static void run_16(Call *c) {
    static const int64_t times[] = { 0, 86399, 951782400, 1700000000, GMTIME_LAST_DOCUMENTED, GMTIME_LAST_DOCUMENTED + GMTIME_ALLOWANCE };
    static const int64_t refused[] = { GMTIME_LAST_DOCUMENTED + GMTIME_ALLOWANCE + 1, 253402300799, (int64_t)1 << 62 };
    static const int64_t edge[] = { -1, -43200, -43201 };
    EXPORT(f, c->tier0, "Plat_gmtime");
    int32_t *out = gate2_mem(64);
    for (unsigned i = 0; i < sizeof times / sizeof times[0]; i++) {
        time_t t = (time_t)times[i];
        struct tm h;
        gmtime_r(&t, &h);
        const int want[9] = { h.tm_sec, h.tm_min, h.tm_hour, h.tm_mday, h.tm_mon, h.tm_year, h.tm_wday, h.tm_yday, h.tm_isdst };
        memset(out, 0xEE, 64);
        int32_t rc = (int32_t)GUEST_CALL(c->ctx, f, (uint64_t)times[i], out);
        if (rc != 0) { gate2_fail_text(c, "Plat_gmtime(%lld), inside the C runtime's range, returns %d", (long long)times[i], rc); return; }
        for (unsigned k = 0; k < 9; k++)
            if (out[k] != want[k]) {
                gate2_fail_text(c, "Plat_gmtime(%lld): field %u of struct tm is %d, the host's gmtime_r gives %d",
                                (long long)times[i], k, out[k], want[k]);
                return;
            }
        EXPECT("Plat_gmtime: the byte after the 36 of struct tm", ((uint8_t *)out)[36], 0xEE);
    }
    if (c->map_only) {
        gate2_note(c, "the refused times are not run with --map-only: the error path reads the C runtime's per-thread data "
                      "through an import");
        return;
    }
    for (unsigned i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        memset(out, 0xEE, 64);
        int32_t rc = (int32_t)GUEST_CALL(c->ctx, f, (uint64_t)refused[i], out);
        if (rc != 22) { gate2_fail_text(c, "Plat_gmtime(%lld), after the C runtime's range, returns %d, not EINVAL (22)", (long long)refused[i], rc); return; }
        EXPECT("Plat_gmtime (refused): the byte after the 36 of struct tm", ((uint8_t *)out)[36], 0xEE);
    }
    memset(out, 0xEE, 64);
    EXPECT("Plat_gmtime(-1824522369), a time before 1970: the return value", (int32_t)GUEST_CALL(c->ctx, f, (uint64_t)GMTIME_BEFORE_1970, out), 22);
    for (unsigned k = 0; k < 9; k++)
        EXPECT("Plat_gmtime(-1824522369): a field of struct tm", S32(out[k]), S32(-1));
    EXPECT("Plat_gmtime(-1824522369): the byte after the 36 of struct tm", ((uint8_t *)out)[36], 0xEE);
    for (unsigned i = 0; i < sizeof edge / sizeof edge[0]; i++) {
        memset(out, 0xEE, 64);
        int32_t rc = (int32_t)GUEST_CALL(c->ctx, f, (uint64_t)edge[i], out);
        uint64_t fields = 0;
        for (unsigned k = 0; k < 9; k++) fields = gate2_stream(fields, S32(out[k]));
        gate2_value(c, "Plat_gmtime(-1), (-43200), (-43201): the return value", S32(rc));
        gate2_value(c, "Plat_gmtime(-1), (-43200), (-43201): the nine fields, as one digest", fields);
        EXPECT("Plat_gmtime (the edge at 1970): the byte after the 36 of struct tm", ((uint8_t *)out)[36], 0xEE);
    }
}

const Entry gate2_calls[] = {
    { 1, "Plat_GetMemPageSize", 0,
      "Call with no arguments.",
      "Returns 4 (the unit is not stated by the code; kilobytes inferred from the value).",
      "Only that the dispatch table reaches the function. Proves the driver, the export lookup and the return path.",
      NULL, run_1 },
    { 2, "Mode flags: Plat_SetToolMode, Plat_IsInToolMode, Plat_SetBenchmarkMode, Plat_IsInBenchmarkMode, Plat_SetRenderToolMode, Plat_IsInRenderToolMode", 0,
      "For each pair: set 1, read; set 0, read. Then set one flag and check the other two did not move.",
      "Round trip is exact; the three flags are independent bytes.",
      "Writes .data (zero-initialised); nothing from the entry point.",
      NULL, run_2 },
    { 3, "EncodeBinaryToString / DecodeBinaryFromString", 0,
      "Buffers of 0, 1, 6, 7, 8, 255 and 4096 pseudo-random bytes. Encode, then decode. Also: dst capacity one too small; an input whose first four bytes are below 0x80; a decode buffer one byte too small.",
      "Encode returns n + 4 + ceil(n/7) (string length without the terminator) and every output byte is >= 0x80; capacity too small returns -(n + 5 + ceil(n/7)); decode returns n and sets *end one past the last byte read, and the bytes match; malformed input returns 0x80000000; too-small output returns -n.",
      "None: no imports, no globals, no allocator. Best first check of integer code lifting.",
      NULL, run_3 },
    { 4, "V_UnicodeToUCS2 / V_UCS2ToUnicode", 0,
      "UTF-16 strings of 0, 1 and 10 characters with destination sizes below, equal to and above the need.",
      "Return value = bytes copied (V_UnicodeToUCS2: min(src, dst); V_UCS2ToUnicode: min(2*(len+1), dst)); V_UCS2ToUnicode terminates the destination; contents equal.",
      "Calls two small self-contained C runtime routines (wcslen, wcsncpy); no Windows imports. Pass sign-extended 32-bit sizes: V_UnicodeToUCS2 compares full 64-bit registers.",
      NULL, run_4 },
    { 5, "V_UTF8_strncpy", 0,
      "A UTF-8 string with 2-, 3- and 4-byte characters, copied with maxLen cutting inside each kind of character and at a boundary.",
      "Result is valid UTF-8, no longer than maxLen-1, and is the longest valid prefix; returns dst.",
      "Calls the C runtime strncpy only.",
      NULL, run_5 },
    { 6, "CThreadSpinRWLock (ctor, TryLockForRead, UnlockRead, TryLockForWrite, UnlockWrite, IsLockedForRead, IsLockedForWrite)", NEEDS_IMPORTS,
      "Zero 8 bytes through the constructor. TryLockForRead twice; IsLockedForRead; TryLockForWrite (must fail); UnlockRead twice; TryLockForWrite (succeeds); IsLockedForWrite; TryLockForRead (fails); UnlockWrite.",
      "Return values as listed; the first dword reads 2, then 0, then 0x10000, then 0; second dword equals the thread id after a write lock.",
      "Lock-prefixed atomics only. Good first test of the x86 memory-ordering mode.",
      NULL, run_6 },
    { 7, "Thread-local read and write: JT_GetCurrentThread, JT_SetThreadIdx, JT_NumWorkerThreads", NEEDS_TLS,
      "On a fresh thread call JT_GetCurrentThread; call JT_SetThreadIdx(5) and read again; read from a second guest thread; call JT_NumWorkerThreads before ConfigureAndInitJobThreads.",
      "First read returns 28 (the TLS template holds 28 at offset 0x10); then 5; the second thread still sees 28; worker count is 0.",
      "Static TLS: the loader writes the module's TLS index to tier0 RVA 0x19aaf0 and each thread's gs:[0x58] array holds the block (RUNTIME.md 1.6, 6.2). First test of that machinery; no Windows import.",
      NULL, run_7 },
    { 8, "CreateInterface and VStdLib_GetICVarFactory", NEEDS_ENTRY,
      "CreateInterface with \"VEngineCvar007\", \"EventSystem001\", \"VProcessUtils002\", \"NoSuchThing\", a wrong-case name, and a NULL returnCode. Then call the function VStdLib_GetICVarFactory returns, with \"VEngineCvar007\".",
      "The three names return vstdlib base + 0x72080, + 0x72370 and + 0x743b0 with *rc == 0; the others return NULL with *rc == 1; the factory's answer equals CreateInterface's; the function pointer equals base + 0x26140.",
      "Needs vstdlib's entry point to have run: three static constructors insert the list nodes. Before that the list is empty and every name fails.",
      NULL, run_8 },
    { 9, "One-shim leaves: Plat_GetCommandLineA, Plat_GetCommandLine, GetFreeMemory, GetTotalMemory, Plat_GetOSVersion", NEEDS_IMPORTS,
      "Call each once.",
      "Both command-line exports return the pointer the GetCommandLineA shim returns; total >= free > 0 and total equals the machine description's physical memory; OS version equals the major version the runtime reports.",
      "One shim each; no state.",
      NULL, run_9 },
#include "calls_10_30.inc"
};
const unsigned gate2_ncalls = sizeof gate2_calls / sizeof gate2_calls[0];
_Static_assert(sizeof gate2_calls / sizeof gate2_calls[0] == 30, "the first-call list has 30 items");
