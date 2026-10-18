/* Text for exception codes, records and contexts: rt_exc_code_name, rt_exc_describe,
 * rt_exc_format_record and rt_exc_format_context (core/report.h, "core/excdecode.c"). */
#include "rt/test.h"
#include "rt/test_child.h"
#include "rt/vm.h"
#include "rt/win/exception.h"
#include "rt/../../core/report.h"

/* ---- helpers, called only from the cases ----------------------------------------------------------- */

static bool name_is(DWORD code, const char *want) {
    const char *s = rt_exc_code_name(code);
    return s != NULL && strcmp(s, want) == 0;
}

/* A guest region: 0x20000 bytes reserved, the first 0x10000 committed read-write. */
static uint64_t guest_region(void) {
    uint64_t g = rt_vm_alloc(0, 0x20000, MEM_RESERVE, PAGE_READWRITE, RT_VM_PRIVATE);
    rt_vm_alloc(g, 0x10000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    return g;
}

/* A NUL-terminated string written into guest memory, byte by byte. */
static void put_str(uint64_t at, const char *s) {
    size_t i;
    for (i = 0; s[i]; i++) ST8(at + i, (uint8_t)s[i]);
    ST8(at + i, 0);
}

/* rt_module_describe as text: the module description of addr, in a buffer kept until the next call. */
static const char *mod_text(uint64_t addr) {
    static char buf[256];
    rt_module_describe(addr, buf, sizeof buf);
    return buf;
}

/* The good-throw layout of cases 9 to 15: ThrowInfo at g+0x100, CatchableTypeArray at g+0x200,
 * CatchableType at g+0x300, TypeDescriptor at g+0x400 whose name is at g+0x410. */
static void throw_setup(uint64_t g) {
    ST32(g + 0x100 + 12, 0x200);
    ST32(g + 0x200, 1);
    ST32(g + 0x204, 0x300);
    ST32(g + 0x304, 0x400);
    put_str(g + 0x410, ".?AVbad_alloc@std@@");
}

/* ---- the tests ------------------------------------------------------------------------------------ */

TEST(core, excdecode_name)
{
    T_PLAN(1, "the names of the EXCEPTION_ and STATUS_ codes") {
        T_TRUE(name_is(0xC0000005u, "EXCEPTION_ACCESS_VIOLATION"));
        T_TRUE(name_is(0xC0000409u, "STATUS_STACK_BUFFER_OVERRUN"));
        T_TRUE(name_is(0x80000003u, "EXCEPTION_BREAKPOINT"));
        T_TRUE(name_is(0xC00000FDu, "EXCEPTION_STACK_OVERFLOW"));
        T_TRUE(name_is(0xC0000094u, "EXCEPTION_INT_DIVIDE_BY_ZERO"));
        T_TRUE(name_is(0x40000015u, "STATUS_FATAL_APP_EXIT"));
        T_TRUE(name_is(0x80000001u, "EXCEPTION_GUARD_PAGE"));
    }
    T_PLAN(2, "the debugger conventions and the runtime's own codes") {
        T_TRUE(name_is(0x40010006u, "DBG_PRINTEXCEPTION_C"));
        T_TRUE(name_is(0x4001000Au, "DBG_PRINTEXCEPTION_WIDE_C"));
        T_TRUE(name_is(0x406D1388u, "thread name"));
        T_TRUE(name_is(0xE06D7363u, "C++ exception"));
        T_TRUE(name_is(0xC06D007Eu, "delay-load: module not found"));
        T_TRUE(name_is(0xC06D007Fu, "delay-load: procedure not found"));
        T_TRUE(name_is(0xC06D0057u, "delay-load: invalid parameter"));
    }
    T_PLAN(3, "codes that are not in the list give NULL") {
        T_TRUE(rt_exc_code_name(0u) == NULL);
        T_TRUE(rt_exc_code_name(0x100u) == NULL);
        T_TRUE(rt_exc_code_name(0xC0000004u) == NULL);
        T_TRUE(rt_exc_code_name(0xE06D7364u) == NULL);
    }
}

TEST(core, excdecode_describe)
{
    T_PLAN(4, "a code that appends nothing keeps the buffer and returns its length") {
        char b[8] = "ab";
        uint64_t a[3] = {0, 0x1234, 0};
        T_EQ(rt_exc_describe(b, sizeof b, 2, 0x100u, 3, a), 2);
        T_TRUE(strcmp(b, "ab") == 0);
    }
    T_PLAN(5, "the access kinds of an access violation") {
        uint64_t a[2] = {0, 0x1234};
        char b[128] = "";
        size_t r = rt_exc_describe(b, sizeof b, 0, 0xC0000005u, 2, a);
        T_TRUE(strcmp(b, "  read of address 0x0000000000001234\n") == 0);
        T_EQ(r, strlen(b));
        b[0] = 0; a[0] = 1;
        r = rt_exc_describe(b, sizeof b, 0, 0xC0000005u, 2, a);
        T_TRUE(strcmp(b, "  write of address 0x0000000000001234\n") == 0);
        T_EQ(r, strlen(b));
        b[0] = 0; a[0] = 8;
        r = rt_exc_describe(b, sizeof b, 0, 0xC0000005u, 2, a);
        T_TRUE(strcmp(b, "  execute of address 0x0000000000001234\n") == 0);
        T_EQ(r, strlen(b));
        b[0] = 0; a[0] = 5;
        r = rt_exc_describe(b, sizeof b, 0, 0xC0000005u, 2, a);
        T_TRUE(strcmp(b, "  access of address 0x0000000000001234\n") == 0);
        T_EQ(r, strlen(b));
    }
    T_PLAN(6, "an access violation with one parameter; an in-page error") {
        uint64_t a[2] = {0, 0x1234};
        char b[128] = "";
        rt_exc_describe(b, sizeof b, 0, 0xC0000005u, 1, a);
        T_TRUE(strcmp(b, "") == 0);
        uint64_t c[2] = {1, UINT64_C(0x11000000000)};
        rt_exc_describe(b, sizeof b, 0, 0xC0000006u, 2, c);
        T_TRUE(strcmp(b, "  write of address 0x0000011000000000\n") == 0);
    }
    T_PLAN(7, "a C++ exception with two parameters, or a wrong magic number") {
        uint64_t a[4] = {0x19930520u, 0, 0, 0};
        char b[128] = "";
        rt_exc_describe(b, sizeof b, 0, 0xE06D7363u, 2, a);
        T_TRUE(strcmp(b, "  C++ exception with unexpected parameters\n") == 0);
        b[0] = 0; a[0] = 0x19930521u;
        rt_exc_describe(b, sizeof b, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(b, "  C++ exception with unexpected parameters\n") == 0);
    }
    T_PLAN(8, "a rethrow: a throw with no exception object") {
        uint64_t a[4] = {UINT64_C(0xFFFFFFFF19930520), 0, 0, 0};
        char b[128] = "";
        rt_exc_describe(b, sizeof b, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(b, "  C++ rethrow (throw;) with no exception object\n") == 0);
    }
    T_PLAN(20, "a buffer of 16 bytes keeps 15 bytes and ends with a NUL") {
        uint64_t a[2] = {0, 0x1234};
        char full[128] = "";
        rt_exc_describe(full, sizeof full, 0, 0xC0000005u, 2, a);
        char b16[16];
        memset(b16, 'Z', sizeof b16);
        size_t r = rt_exc_describe(b16, sizeof b16, 0, 0xC0000005u, 2, a);
        T_EQ(r, 15);
        T_TRUE(b16[15] == 0);
        T_TRUE(memcmp(b16, full, 15) == 0);
        memset(b16, 'Z', sizeof b16);
        r = rt_exc_describe(b16, sizeof b16, 0, 0xE06D7363u, 2, a);
        T_EQ(r, 15);
        T_TRUE(b16[15] == 0);
        T_TRUE(memcmp(b16, "  C++ exception with unexpected parameters\n", 15) == 0);
    }
}

/* The text of a C++ throw whose type cannot be read, with its ThrowInfo address t. */
static void throw_unreadable(char *want, size_t n, uint64_t t) {
    snprintf(want, n, "  C++ exception; the type cannot be read (ThrowInfo at 0x%llx)\n", (unsigned long long)t);
}

TEST(core, excdecode_throw)
{
    uint64_t g = guest_region();

    T_PLAN(9, "a good throw names its type and object") {
        throw_setup(g);
        char want[256] = "";
        snprintf(want, sizeof want, "  C++ exception of type .?AVbad_alloc@std@@ (object at 0x%llx)\n",
                 (unsigned long long)(g + 0x800));
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x100, g};
        char buf[2048] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(10, "the same throw with three parameters: no image base, so no type") {
        throw_setup(g);
        char want[256] = "";
        throw_unreadable(want, sizeof want, g + 0x100);
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x100, g};
        char buf[2048] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 3, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(11, "a CatchableTypeArray count of 0 or of -1 cannot be read") {
        throw_setup(g);
        char want[256] = "";
        throw_unreadable(want, sizeof want, g + 0x100);
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x100, g};
        char buf[2048] = "";
        ST32(g + 0x200, 0);
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0;
        ST32(g + 0x200, 0xFFFFFFFF);
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
        ST32(g + 0x200, 1);                             /* restore */
    }
    T_PLAN(12, "a ThrowInfo that is reserved, host memory, or too near the end") {
        throw_setup(g);
        int host = 0;
        uint64_t H = (uint64_t)(uintptr_t)&host;
        char want[256] = "";
        char buf[2048] = "";
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x10000, g};
        throw_unreadable(want, sizeof want, g + 0x10000);
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0; a[2] = H;
        throw_unreadable(want, sizeof want, H);
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0; a[2] = g + 0xFFF8;
        throw_unreadable(want, sizeof want, g + 0xFFF8);
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(13, "an empty type name cannot be read") {
        throw_setup(g);
        ST8(g + 0x410, 0);
        char want[256] = "";
        throw_unreadable(want, sizeof want, g + 0x100);
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x100, g};
        char buf[2048] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(14, "a type name of 200 bytes is read as its first 127") {
        throw_setup(g);
        for (int i = 0; i < 200; i++) ST8(g + 0x410 + i, 'A');
        ST8(g + 0x410 + 200, 0);
        char name[128];
        memset(name, 'A', 127);
        name[127] = 0;
        char want[256] = "";
        snprintf(want, sizeof want, "  C++ exception of type %s (object at 0x%llx)\n", name,
                 (unsigned long long)(g + 0x800));
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x100, g};
        char buf[2048] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(15, "a type name with no NUL before unreadable memory is 16 bytes") {
        throw_setup(g);
        ST32(g + 0x304, 0xFFE0);
        for (int i = 0; i < 16; i++) ST8(g + 0xFFF0 + i, 'B');
        char name[17];
        memset(name, 'B', 16);
        name[16] = 0;
        char want[256] = "";
        snprintf(want, sizeof want, "  C++ exception of type %s (object at 0x%llx)\n", name,
                 (unsigned long long)(g + 0x800));
        uint64_t a[4] = {0x19930520u, g + 0x800, g + 0x100, g};
        char buf[2048] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xE06D7363u, 4, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
}

/* The DLL name of the delay-load cases: the string at g+0x1100, and the descriptor's szDll at I+24. */
static void dll_name(uint64_t g) {
    put_str(g + 0x1100, "steam_api64.dll");
    ST64(g + 0x1000 + 24, g + 0x1100);
}

TEST(core, excdecode_delayload)
{
    uint64_t g = guest_region();

    T_PLAN(16, "a delay-load code whose descriptor cannot be read") {
        const char *want = "  delay-load failure; the descriptor cannot be read\n";
        char buf[256] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Eu, 0, NULL);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0;
        uint64_t a[1] = {g + 0x10000};
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Eu, 1, a);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0; a[0] = g + 0xFFC0;
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Eu, 1, a);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(17, "a delay-load of a named procedure") {
        uint64_t I = g + 0x1000;
        dll_name(g);
        put_str(g + 0x1200, "SteamAPI_Init");
        ST32(I + 32, 1);
        ST64(I + 40, g + 0x1200);
        ST32(I + 64, 127);
        uint64_t a[1] = {I};
        char buf[256] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Fu, 1, a);
        T_TRUE(strcmp(buf, "  delay-load of steam_api64.dll!SteamAPI_Init failed, last error 127\n") == 0);
    }
    T_PLAN(18, "a delay-load of an ordinal, with two of the codes") {
        uint64_t I = g + 0x1000;
        dll_name(g);
        ST32(I + 32, 0);
        ST64(I + 40, UINT64_C(0xAAAAAAAA0000004D));
        ST32(I + 64, 126);
        uint64_t a[1] = {I};
        char buf[256] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Eu, 1, a);
        T_TRUE(strcmp(buf, "  delay-load of steam_api64.dll!#77 failed, last error 126\n") == 0);
        buf[0] = 0;
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D0057u, 1, a);
        T_TRUE(strcmp(buf, "  delay-load of steam_api64.dll!#77 failed, last error 126\n") == 0);
    }
    T_PLAN(19, "a delay-load with no DLL name, then a name pointer that cannot be read") {
        uint64_t I = g + 0x1000;
        ST64(I + 24, 0);
        ST32(I + 32, 0);
        ST64(I + 40, UINT64_C(0xAAAAAAAA0000004D));
        ST32(I + 64, 126);
        uint64_t a[1] = {I};
        char buf[256] = "";
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Eu, 1, a);
        T_TRUE(strcmp(buf, "  delay-load of ?!#77 failed, last error 126\n") == 0);
        dll_name(g);
        ST32(I + 32, 1);
        ST64(I + 40, g + 0x10000);
        ST32(I + 64, 126);
        buf[0] = 0;
        rt_exc_describe(buf, sizeof buf, 0, 0xC06D007Fu, 1, a);
        T_TRUE(strcmp(buf, "  delay-load of steam_api64.dll!? failed, last error 126\n") == 0);
    }
}

/* An EXCEPTION_RECORD at r: code +0, flags +4, chained +8, address +16, count +24. */
static void record_put(uint64_t r, DWORD code, DWORD flags, uint64_t chained, uint64_t address, DWORD count) {
    ST32(r + 0, code);
    ST32(r + 4, flags);
    ST64(r + 8, chained);
    ST64(r + 16, address);
    ST32(r + 24, count);
}

static void record_unreadable(char *want, size_t n, uint64_t r) {
    snprintf(want, n, "  exception record at 0x%llx is not readable guest memory\n", (unsigned long long)r);
}

TEST(core, excdecode_record)
{
    uint64_t g = guest_region();

    T_PLAN(21, "no record, a record that cannot be read, host memory") {
        char want[256] = "";
        char buf[2048] = "";
        int host = 0;
        uint64_t H = (uint64_t)(uintptr_t)&host;
        rt_exc_format_record(buf, sizeof buf, 0, 0);
        T_TRUE(strcmp(buf, "  no exception record\n") == 0);
        buf[0] = 0;
        record_unreadable(want, sizeof want, g + 0x10000);
        rt_exc_format_record(buf, sizeof buf, 0, g + 0x10000);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0;
        record_unreadable(want, sizeof want, g + 0xFF80);
        rt_exc_format_record(buf, sizeof buf, 0, g + 0xFF80);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0;
        record_unreadable(want, sizeof want, H);
        rt_exc_format_record(buf, sizeof buf, 0, H);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(22, "a record with an access violation and two parameters") {
        char want[512] = "";
        char buf[2048] = "";
        record_put(g + 0x2000, 0xC0000005u, 1, 0, 0x12345678u, 2);
        ST64(g + 0x2000 + 32, 1);
        ST64(g + 0x2000 + 40, 0x10);
        snprintf(want, sizeof want,
                 "  exception 0xc0000005 (EXCEPTION_ACCESS_VIOLATION), flags 0x1, at %s (0x12345678)\n"
                 "  2 parameters: 0x1 0x10\n"
                 "  write of address 0x0000000000000010\n", mod_text(0x12345678u));
        rt_exc_format_record(buf, sizeof buf, 0, g + 0x2000);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(23, "a record with no parameters, at the address of an import trap") {
        char want[512] = "";
        char buf[2048] = "";
        uint64_t trap = (uint64_t)(uintptr_t)rt_test_trap_import();
        record_put(g + 0x2000, 0x100u, 0, 0, trap, 0);
        snprintf(want, sizeof want, "  exception 0x00000100, flags 0x0, at %s (0x%llx)\n  0 parameters\n",
                 mod_text(trap), (unsigned long long)trap);
        rt_exc_format_record(buf, sizeof buf, 0, g + 0x2000);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(24, "a record with twenty parameters prints fifteen") {
        char want[1024] = "";
        char buf[2048] = "";
        record_put(g + 0x2000, 0x100u, 0, 0, 0x12345678u, 20);
        for (int i = 0; i < 15; i++) ST64(g + 0x2000 + 32 + 8 * i, (uint64_t)(i + 1));
        snprintf(want, sizeof want,
                 "  exception 0x00000100, flags 0x0, at %s (0x12345678)\n"
                 "  15 parameters: 0x1 0x2 0x3 0x4 0x5 0x6 0x7 0x8 0x9 0xa 0xb 0xc 0xd 0xe 0xf\n",
                 mod_text(0x12345678u));
        rt_exc_format_record(buf, sizeof buf, 0, g + 0x2000);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(25, "a chained record is named, not printed") {
        char want[512] = "";
        char buf[2048] = "";
        record_put(g + 0x2000, 0x100u, 0, 0xABC, 0x12345678u, 0);
        snprintf(want, sizeof want,
                 "  exception 0x00000100, flags 0x0, at %s (0x12345678)\n"
                 "  0 parameters\n"
                 "  chained record at 0xabc, not printed\n",
                 mod_text(0x12345678u));
        rt_exc_format_record(buf, sizeof buf, 0, g + 0x2000);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(26, "the text is appended after what the buffer holds") {
        char buf[2048] = "X\n";
        record_put(g + 0x2000, 0x100u, 0, 0, 0x12345678u, 0);
        rt_exc_format_record(buf, sizeof buf, 2, g + 0x2000);
        T_TRUE(strncmp(buf, "X\n  exception", 13) == 0);
    }
}

static void context_unreadable(char *want, size_t n, uint64_t c) {
    snprintf(want, n, "  context record at 0x%llx is not readable guest memory\n", (unsigned long long)c);
}

TEST(core, excdecode_context)
{
    uint64_t g = guest_region();

    T_PLAN(27, "no context, a context that cannot be read, a context that does not fit") {
        char want[256] = "";
        char buf[2048] = "";
        rt_exc_format_context(buf, sizeof buf, 0, 0);
        T_TRUE(strcmp(buf, "  no context record\n") == 0);
        buf[0] = 0;
        context_unreadable(want, sizeof want, g + 0x10000);
        rt_exc_format_context(buf, sizeof buf, 0, g + 0x10000);
        T_TRUE(strcmp(buf, want) == 0);
        buf[0] = 0;
        context_unreadable(want, sizeof want, g + 0xFC00);
        rt_exc_format_context(buf, sizeof buf, 0, g + 0xFC00);
        T_TRUE(strcmp(buf, want) == 0);
    }
    T_PLAN(28, "a context: rip, eflags and mxcsr, then the sixteen registers") {
        uint64_t c = g + 0x3000;
        for (uint64_t k = 0; k < 1232; k += 8) ST64(c + k, 0);
        ST64(c + 0xF8, 0x7777);
        ST32(c + 0x44, 0x246);
        ST32(c + 0x34, 0x1F80);
        uint64_t r[16];
        for (int i = 0; i < 16; i++) {
            r[i] = 0x1000 + (uint64_t)i;
            ST64(c + 0x78 + 8 * (uint64_t)i, r[i]);
        }
        char want[2048] = "";
        snprintf(want, sizeof want, "  context: rip=%s (0x%016llx) eflags=%08x mxcsr=%08x\n",
                 mod_text(0x7777u), (unsigned long long)0x7777u, 0x246u, 0x1F80u);
        rt_report_regs(want, sizeof want, strlen(want), r, "    ");
        char buf[2048] = "";
        rt_exc_format_context(buf, sizeof buf, 0, c);
        T_TRUE(strcmp(buf, want) == 0);
    }
}
