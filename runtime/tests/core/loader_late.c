/* A lifted module shaped like the game's 21st DLL, ui(11).dll (gate3/modules.def;
 * docs/findings/34-ui-dll.md): a file name with a parenthesis, a directory of its own under
 * the install root, no imports, no relocation directory, an entry point that returns 1, and
 * functions the host reaches by name only.  The row is slot d of the test program's list
 * (runtime/tests/abi/modules.def), written `place=late`.
 *
 * Where the expected values come from: the path is the one the recording shows the pak
 * loader passing (LoadLibraryA("r2\paks\Win64\ui(11).dll") from rtech_game+0xa23e, with the
 * test's file name in place of the game's); the base is RT_IMAGE_LATE_BASE by the rule of
 * rt/module.h; an export's address is the module's base plus the RVA this test wrote into
 * the export directory; the functions' answers are this file's. */
#include <string.h>

#include "rt/test_pe.h"
#include "rt/module.h"
#include "rt/shim.h"

#define LATE_FILE  "t_ui(11).dll"
#define LATE_PATH  "r2\\paks\\Win64\\" LATE_FILE

static unsigned entries, entry_reason;
static uint64_t entry_base;

static void late_entry(Ctx *ctx) {                      /* ui(11).dll+0x1000 is `mov eax, 1; ret` */
    entries++;
    entry_base = ctx->r[1];
    entry_reason = (unsigned)ctx->r[2];
    ctx->r[0] = 1;
    GUEST_RET(ctx);
}
static void late_part(Ctx *ctx)        { ctx->r[0] = ctx->r[1] + 11; GUEST_RET(ctx); }
static void late_part_hidden(Ctx *ctx) { ctx->r[0] = ctx->r[1] + 22; GUEST_RET(ctx); }

TEST_SETUP(core, loader_late)
{
    RtTestPe *pe = rt_test_pe_new();
    rt_test_pe_entry(pe, RT_TEST_LIFT_RVA(0));
    rt_test_pe_export(pe, "a_part", 1, RT_TEST_LIFT_RVA(1));
    rt_test_pe_export(pe, "a_part__hidden", 2, RT_TEST_LIFT_RVA(2));
    rt_test_pe_no_relocs(pe);
    rt_test_pe_write_in(pe, "r2/paks/Win64", LATE_FILE);
    rt_test_lift_set('d', 0, late_entry);
    rt_test_lift_set('d', 1, late_part);
    rt_test_lift_set('d', 2, late_part_hidden);
    entries = 0;
}

TEST(core, loader_late)
{
    RtModule *m = rt_module_find(LATE_FILE);
    T_REQUIRE(m != NULL);
    HMODULE base = (HMODULE)(uintptr_t)m->base;

    T_CASE("the row is a lifted module at the start of the late group, present and not yet loaded");
    T_TRUE(!m->native);
    T_EQ(m->base, RT_IMAGE_LATE_BASE);
    T_EQ(m->size, 0x10000);
    T_TRUE(!m->loaded);
    T_EQ(entries, 0);

    T_CASE("the pak loader's relative path, parenthesis and all, loads it: the handle is its base, the entry point ran once");
    T_EQ(CALL(kernel32, LoadLibraryA, LATE_PATH), base);
    T_TRUE(m->loaded);
    T_EQ(m->load_count, 1);
    T_EQ(entries, 1);
    T_EQ(entry_base, m->base);
    T_EQ(entry_reason, DLL_PROCESS_ATTACH);
    T_TRUE(T_LOGGED(RT_CH_LOADER, LATE_FILE ": loaded at 0x10080000000"));

    T_CASE("a second load, by the bare name in another case, gives the same handle and does not run the entry point again");
    T_EQ(CALL(kernel32, LoadLibraryA, "T_UI(11).DLL"), base);
    T_EQ(m->load_count, 2);
    T_EQ(entries, 1);

    T_CASE("the same name without its parenthesis, or with another number, is no module");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, LoadLibraryA, "r2\\paks\\Win64\\t_ui.dll"), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, LoadLibraryA, "r2\\paks\\Win64\\t_ui(12).dll"), NULL);
    T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);

    T_CASE("GetProcAddress by name gives the lifted functions, which run");
    GUESTFN part = CALL(kernel32, GetProcAddress, base, "a_part");
    GUESTFN hidden = CALL(kernel32, GetProcAddress, base, "a_part__hidden");
    T_EQ(part, m->base + RT_TEST_LIFT_RVA(1));
    T_EQ(hidden, m->base + RT_TEST_LIFT_RVA(2));
    uint64_t arg = 100;
    T_EQ(rt_guest_call(T_CTX(), part, 1, &arg), 111);
    T_EQ(rt_guest_call(T_CTX(), hidden, 1, &arg), 122);

    T_CASE("a name it does not export gives NULL with ERROR_PROC_NOT_FOUND");
    CALL(kernel32, SetLastError, 0);
    T_EQ(CALL(kernel32, GetProcAddress, base, "a_part__shown"), 0);
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);

    T_CASE("an address inside it is described by the file's own name");
    char buf[64];
    T_TRUE(!strcmp(rt_module_describe(m->base + 0x1010, buf, sizeof buf), LATE_FILE "+0x1010"));
    T_TRUE(rt_module_from_address(m->base + 0x1010) == m);

    T_CASE("its file name is the path under the install root");
    char name[260];
    T_EQ(CALL(kernel32, GetModuleFileNameA, base, name, sizeof name), strlen("C:\\Titanfall2\\" LATE_PATH));
    T_TRUE(!strcmp(name, "C:\\Titanfall2\\" LATE_PATH));
}
