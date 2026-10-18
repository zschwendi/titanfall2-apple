/* The dispatch table, shim stubs, trap stubs and the miss hook
 * (docs/RUNTIME.md sections 1.4, 2 and 9.1).
 */
#include "rt/test.h"
#include "rt/vm.h"
#include "rttest_calls.h"

TEST(core, dispatch_table)
{
    T_TRUE(rt_disp_sealed());
    T_EQ(g_disp_shift, 64 - RT_DISP_BITS);
    T_EQ(g_disp_mask, (UINT64_C(1) << RT_DISP_BITS) - 1);

    /* Every shim of both tables, the return-to-host stub and the harness's stubs are keys. */
    T_TRUE(rt_disp_count() >= rt_shim_count + rttest_shim_count + 2);
    uint64_t stub = rt_test_import_slot(RT_ID_rttest_Poke);
    T_TRUE(rt_disp_find(stub) == RT_ID_rttest_Poke->thunk);
    T_TRUE(rt_lookup(T_CTX(), stub) == RT_ID_rttest_Poke->thunk);

    /* A miss returns the single hook after recording the target; it does not return NULL. */
    T_TRUE(rt_disp_find(stub + 1) == NULL);
    T_TRUE(rt_disp_find(0) == NULL);
    T_TRUE(rt_lookup(T_CTX(), stub + 1) == rt_dispatch_miss);
    T_EQ(RT_THREAD(T_CTX())->miss_target, stub + 1);
    /* Guest address 0 is the key of an empty slot, so its lookup "hits"; what it gets must
     * still be a function that reports the call, never NULL. */
    T_TRUE(rt_lookup(T_CTX(), 0) != NULL);

    /* The table is read-only: nothing is inserted after start-up. */
    T_TRAPS(rt_disp_add(stub + 32, RT_ID_rttest_Poke->thunk), "after the dispatch table was sealed");
}

TEST(core, shim_stub_addresses)
{
    /* Shim i of a native module, sorted by name, lives at base + 0x10000 + 16*i, and its 16
     * bytes read as `mov eax, i; ret; int3...` (section 2). */
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL && m->nshims == rttest_shim_count && m->nshims >= 2);
    for (unsigned i = 0; i < m->nshims; i++) {
        uint64_t stub = m->base + RT_NATIVE_SHIM_STUBS + 16 * (uint64_t)i;
        if (i > 0) T_TRUE(strcmp(m->shims[i - 1]->name, m->shims[i]->name) < 0);
        T_EQ(rt_test_import_slot(m->shims[i]), stub);
        T_TRUE(rt_disp_find(stub) == m->shims[i]->thunk);
        T_EQ(LD8(stub), 0xB8);
        T_EQ(LD32(stub + 1), i);
        T_EQ(LD8(stub + 5), 0xC3);
        T_EQ(LD8(stub + 6), 0xCC);
        T_EQ(LD8(stub + 15), 0xCC);
    }
    /* The slot value is a genuine guest address inside the module that owns the shim. */
    T_TRUE(rt_module_from_address(rt_test_import_slot(RT_ID_rttest_Poke)) == m);
    T_TRUE(rt_vm_in_image_arena(m->base));
}

TEST(core, dispatch_miss)
{
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    /* Not in any module and not guest memory. */
    T_TRAPS(rt_test_invoke(0x12345678, 0, NULL, NULL), "FATAL dispatch miss: call to guest address 0x12345678 (0x12345678)");
    T_TRAPS(rt_test_invoke(0x12345678, 0, NULL, NULL), "the address is in no module and not in guest memory");
    T_TRAPS(rt_test_invoke(0x12345678, 0, NULL, NULL), "called from rttest.dll!rt_test_return   thread 0x1004");
    /* A null function pointer. */
    T_TRAPS(rt_test_invoke(0, 0, NULL, NULL), "a call through a null function pointer");
    /* The VM arena: the guest generated code. */
    T_TRAPS(rt_test_invoke(T_CTX()->gs_base, 0, NULL, NULL), "the address is in the VM arena");
    /* Inside a native module, but not a stub. */
    T_TRAPS(rt_test_invoke(m->base + 0x123, 0, NULL, NULL), "(rttest.dll+0x123)");
    T_TRAPS(rt_test_invoke(m->base + 0x123, 0, NULL, NULL), "inside native module rttest.dll but is not one of its stubs");
    /* The lifter's current entry point reports the same way. */
    T_TRAPS(rt_dispatch(T_CTX(), 0x4242), "FATAL dispatch miss: call to guest address 0x4242");
}

TEST(core, call_site_return_check)
{
    /* What RT_CHECK_RSP of harness/runtime.h calls when a call in lifted code comes back with
     * rsp moved, or with the return address it pushed no longer in its slot (issue 49). */
    Ctx *ctx = T_CTX();
    const uint64_t rsp = ctx->r[4];
    T_TRAPS(rt_rsp_mismatch(ctx, 0x4242, rsp - 8),
            "FATAL a call in lifted code did not return to its call site: the call returning to 0x4242 (0x4242) came back with rsp = ");
    T_TRAPS(rt_rsp_mismatch(ctx, 0x4242, rsp - 8), "was expected (off by 8)\n  a longjmp, an exception unwind, a stack switch");
    ST64(rsp - 8, 0x777);
    T_TRAPS(rt_rsp_mismatch(ctx, 0x4242, rsp),
            "the return address of the call returning to 0x4242 (0x4242) was overwritten with 0x777 (0x777) before the callee returned");
}

TEST(core, unimplemented_import)
{
    /* An import with no shim is bound to a trap stub made for that (library, name).  The
     * module "loads"; the report comes only when the import is called, and names it and
     * its caller (section 9.1). */
    uint64_t by_name = rt_test_trap_import(), by_ordinal = rt_test_trap_import_by_ordinal();
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    T_TRUE(by_name != by_ordinal);
    T_TRUE(rt_module_from_address(by_name) == m && rt_module_from_address(by_ordinal) == m);
    T_TRUE(by_name >= m->base + RT_NATIVE_TRAP_STUBS && by_name < m->base + RT_NATIVE_VTABLES);
    T_TRUE(rt_disp_find(by_name) != NULL && rt_disp_find(by_name) != rt_disp_find(by_ordinal));
    char where[64];
    T_TRUE(!strcmp(rt_module_describe(by_name, where, sizeof where), "rttest.dll!NotImplemented"));

    /* The same import always gets the same address. */
    T_EQ(rt_native_import("rttest.dll", "NotImplemented", 0), by_name);
    T_EQ(rt_native_import("RTTEST", NULL, 77), by_ordinal);

    T_TRAPS(rt_test_invoke(by_name, 0, NULL, NULL), "FATAL unimplemented import rttest.dll!NotImplemented\n");
    T_TRAPS(rt_test_invoke(by_name, 0, NULL, NULL), "  called from rttest.dll!rt_test_return   thread 0x1004 \"\"\n  rcx=");
    T_TRAPS(rt_test_invoke(by_ordinal, 0, NULL, NULL), "FATAL unimplemented import rttest.dll!#77\n");
    /* A tail jump through the slot reaches the same report. */
    T_TRAPS(rt_dispatch(T_CTX(), by_name), "FATAL unimplemented import rttest.dll!NotImplemented\n");

    /* Trap stubs are dispatch-table keys, so a new one cannot appear after start-up. */
    T_TRAPS(rt_native_import("rttest.dll", "NeverAskedFor", 0), "no shim and no trap stub, and the dispatch table is sealed");
    T_TRAPS(rt_native_import("nosuch.dll", "Anything", 0), "nosuch.dll is not a native module of the manifest");
    T_TRAPS(rt_native_stub(m, "late", rt_dispatch_miss), "after the dispatch table was sealed");
}

TEST(core, lifter_trap)
{
    /* TRAP(rva) in lifted code (harness/runtime.h) ends here. */
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    T_TRAPS(rt_trap(T_CTX(), m->base + 0x777), "FATAL lifted code reached a trap at rttest.dll+0x777");
}
