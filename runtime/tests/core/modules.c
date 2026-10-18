/* The module registry and native modules (docs/RUNTIME.md sections 1.2, 1.3, 1.5). */
#include <stdio.h>

#include "rt/test.h"
#include "rt/vm.h"
#include "rttest_calls.h"

TEST(core, module_names)
{
    /* Section 1.5: last path component, lower case, ".dll" appended if there is no extension. */
    RtModule *k = rt_module_find("kernel32.dll");
    T_REQUIRE(k != NULL);
    T_TRUE(rt_module_find("KERNEL32") == k);
    T_TRUE(rt_module_find("Kernel32.DLL") == k);
    T_TRUE(rt_module_find("C:\\Windows\\System32\\kernel32.dll") == k);
    T_TRUE(rt_module_find("c:/windows/system32/KERNEL32") == k);
    T_TRUE(rt_module_find_w(u"Kernel32.dll") == k);
    T_TRUE(rt_module_find_w(u"C:\\Windows\\system32\\KERNEL32") == k);
    T_TRUE(!strcmp(k->name, "kernel32.dll") && !strcmp(k->lib, "kernel32"));
    T_TRUE(rt_module_find("nosuch.dll") == NULL);
    T_TRUE(rt_module_find("") == NULL);
    T_TRUE(rt_module_find("kernel32.exe") == NULL);
    T_TRUE(rt_module_find_w(u"k\u00e9rnel32") == NULL);
    T_TRUE(rt_module_find(NULL) == NULL && rt_module_find_w(NULL) == NULL);

    /* Punctuation in a DLL name becomes _ in the library identifier (section 10.2). */
    RtModule *g = rt_module_find("GFSDK_SSAO.win64.dll");
    T_REQUIRE(g != NULL);
    T_TRUE(!strcmp(g->name, "gfsdk_ssao.win64.dll") && !strcmp(g->lib, "gfsdk_ssao_win64"));
    T_EQ(g->kind, RT_MOD_STUBBED);

    /* The executable has a native module of its own: GetModuleHandle(NULL). */
    RtModule *exe = rt_module_exe();
    T_REQUIRE(exe != NULL);
    T_EQ(exe->kind, RT_MOD_EXE);
    T_TRUE(rt_module_find("Titanfall2.exe") == exe);
    T_TRUE(exe->native && exe->loaded);
}

TEST(core, module_bases)
{
    /* Section 1.2: every module has its base from start-up, 64 KB aligned with a 64 KB gap,
     * inside the image arena, below 8 TB; native modules follow in manifest order. */
    unsigned n = rt_module_count();
    T_TRUE(n == rt_module_def_count + rttest_module_def_count);
    /* Lifted modules come first, in registry order, then the native ones; the lifted rows
     * written `place=late` have theirs from the second half of the arena on, whatever the
     * other two groups hold (rt/module.h, RT_MODDEF_LATE). */
    uint64_t expect = RT_IMAGE_ARENA_BASE;
    unsigned nlifted = 0, nlate = 0;
    T_EQ(RT_IMAGE_LATE_BASE, UINT64_C(0x10080000000));
    for (int pass = 0; pass < 3; pass++) {
        if (pass == 2) {
            T_TRUE(expect <= RT_IMAGE_LATE_BASE);
            expect = RT_IMAGE_LATE_BASE;
        }
        for (unsigned i = 0; i < n; i++) {
            RtModule *m = rt_module_at(i);
            T_REQUIRE(m != NULL);
            bool late = (m->def->flags & RT_MODDEF_LATE) != 0;
            T_TRUE(!(late && m->native));
            if ((m->native ? 1 : late ? 2 : 0) != pass) continue;
            nlifted += !m->native;
            nlate += late;
            T_EQ(m->base, expect);
            T_EQ(m->size, m->native ? RT_NATIVE_SLOT : m->def->size_of_image);
            T_EQ(m->base & 0xFFFF, 0);
            T_TRUE(rt_vm_in_image_arena(m->base) && rt_vm_in_image_arena(m->base + m->size - 1));
            T_TRUE(m->base + m->size < UINT64_C(0x80000000000));
            T_TRUE(rt_module_from_address(m->base) == m);
            T_TRUE(rt_module_from_address(m->base + m->size - 1) == m);
            T_TRUE(rt_module_from_address(m->base + m->size) == NULL);      /* the gap */
            expect = ((expect + m->size + 0xFFFF) & ~UINT64_C(0xFFFF)) + 0x10000;
        }
    }
    T_EQ(nlifted, 5);                                   /* runtime/tests/abi/modules.def */
    T_EQ(nlate, 1);
    /* The late row stands between the other lifted rows and nothing: the first native module
     * is where it would be without it, behind the four lifted modules that are not late. */
    RtModule *late = rt_module_find("t_ui(11).dll");
    T_REQUIRE(late != NULL && !late->native);
    T_EQ(late->base, RT_IMAGE_LATE_BASE);
    uint64_t first_native = RT_IMAGE_ARENA_BASE;
    for (unsigned i = 0; i < n; i++) {
        RtModule *m = rt_module_at(i);
        if (!m->native && m != late) first_native = ((first_native + m->size + 0xFFFF) & ~UINT64_C(0xFFFF)) + 0x10000;
    }
    T_EQ(rt_module_exe()->base, first_native);
    T_TRUE(rt_module_at(n) == NULL);
    T_TRUE(rt_module_at(0) == rt_module_exe());
    T_TRUE(rt_module_from_address(0x1234) == NULL);
    T_TRUE(rt_module_from_address(T_CTX()->gs_base) == NULL);
}

TEST(core, native_module_image)
{
    /* A minimal valid PE header with no export directory at +0 (section 1.3). */
    RtModule *m = rt_module_find("kernel32.dll");
    T_REQUIRE(m != NULL);
    uint64_t b = m->base;
    T_EQ(LD16(b), 0x5A4D);                          /* MZ */
    uint64_t pe = b + LD32(b + 0x3C);
    T_EQ(LD32(pe), 0x00004550);                     /* PE\0\0 */
    T_EQ(LD16(pe + 4), 0x8664);                     /* x64 */
    T_EQ(LD16(pe + 6), 0);                          /* no sections */
    T_TRUE(LD16(pe + 0x16) & 0x2000);               /* a DLL */
    uint64_t opt = pe + 0x18;
    T_EQ(LD16(opt), 0x20B);                         /* PE32+ */
    T_EQ(LD64(opt + 0x18), b);                      /* ImageBase is where it is */
    T_EQ(LD32(opt + 0x38), RT_NATIVE_SLOT);         /* SizeOfImage */
    T_EQ(LD32(opt + 0x6C), 16);                     /* NumberOfRvaAndSizes */
    T_EQ(LD64(opt + 0x70), 0);                      /* export directory: none */
    T_EQ(LD64(opt + 0x78), 0);                      /* import directory: none */
    T_TRUE(!(LD16(rt_module_exe()->base + LD32(rt_module_exe()->base + 0x3C) + 0x16) & 0x2000));   /* the exe is not a DLL */
}

TEST(core, module_describe)
{
    char buf[96];
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    T_TRUE(!strcmp(rt_module_describe(rt_test_import_slot(RT_ID_rttest_Poke), buf, sizeof buf), "rttest.dll!Poke"));
    /* One byte into a stub is not the stub: it is described as an offset in the module. */
    T_TRUE(strstr(rt_module_describe(rt_test_import_slot(RT_ID_rttest_Poke) + 1, buf, sizeof buf), "rttest.dll+0x1") == buf);
    T_TRUE(!strcmp(rt_module_describe(m->base + 0x1a40, buf, sizeof buf), "rttest.dll+0x1a40"));
    T_TRUE(!strcmp(rt_module_describe(rt_test_trap_import(), buf, sizeof buf), "rttest.dll!NotImplemented"));
    T_TRUE(!strcmp(rt_module_describe(rt_test_trap_import_by_ordinal(), buf, sizeof buf), "rttest.dll!#77"));
    T_TRUE(!strcmp(rt_module_describe(0x1234, buf, sizeof buf), "0x1234"));
    T_TRUE(strstr(rt_module_describe(T_CTX()->gs_base, buf, sizeof buf), "vm:0x11") == buf);
    char tiny[8];
    rt_module_describe(m->base + 0x1a40, tiny, sizeof tiny);
    T_EQ(strlen(tiny), 7);                          /* truncated, still terminated */
}

TEST(core, module_export)
{
    /* GetProcAddress on a native module searches the shim table (section 1.5). */
    RtModule *m = rt_module_find("rttest.dll");
    T_REQUIRE(m != NULL);
    T_EQ(rt_module_export(m, "Poke", 0), rt_test_import_slot(RT_ID_rttest_Poke));
    T_EQ(rt_module_export(m, NULL, 7), rt_test_import_slot(RT_ID_rttest_ByOrdinal));
    /* Every lookup is one debug line that names what was asked for and the answer (the call
     * trace shows the name's address only): a load's log is read for these names. */
    char line[96];
    snprintf(line, sizeof line, "GetProcAddress(rttest.dll, Poke) = %#llx", (unsigned long long)rt_test_import_slot(RT_ID_rttest_Poke));
    T_TRUE(T_LOGGED(RT_CH_IMPORT, line));
    snprintf(line, sizeof line, "GetProcAddress(rttest.dll, #7) = %#llx", (unsigned long long)rt_test_import_slot(RT_ID_rttest_ByOrdinal));
    T_TRUE(T_LOGGED(RT_CH_IMPORT, line));

    /* A name we do not have returns NULL with ERROR_PROC_NOT_FOUND, with a warning logged once
     * per (module, name); it does not trap.  The C runtime probes for optional functions this
     * way.  Lines counted: the debug line of each lookup, and the warning the first time. */
    rt_set_last_error(T_CTX(), 0);
    unsigned before = T_LOG_COUNT(RT_CH_IMPORT);
    T_EQ(rt_module_export(m, "FlsAlloc", 0), 0);
    T_EQ(T_LAST_ERROR(), ERROR_PROC_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "rttest.dll!FlsAlloc is not provided"));
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "GetProcAddress(rttest.dll, FlsAlloc) = NULL"));
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 2);
    T_EQ(rt_module_export(m, "FlsAlloc", 0), 0);            /* again: no second warning */
    T_EQ(T_LOG_COUNT(RT_CH_IMPORT), before + 3);
    T_EQ(rt_module_export(m, NULL, 999), 0);
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "rttest.dll!#999 is not provided"));
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "GetProcAddress(rttest.dll, #999) = NULL"));
    /* An import that is only a trap stub is not an export: probing for it gets NULL too. */
    T_EQ(rt_module_export(m, "NotImplemented", 0), 0);
    /* Case matters, as on Windows. */
    T_EQ(rt_module_export(m, "poke", 0), 0);
}
