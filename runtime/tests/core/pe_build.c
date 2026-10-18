/* The PE writer for test images (rt/test_pe.h): the slot files the loader's tests make,
 * read back with the loader's own reader and by hand. */
#include <stdio.h>
#include <stdlib.h>

#include "rt/test.h"
#include "rt/test_pe.h"
#include "rt/vm.h"
#include "../../core/loader.h"

/* Reads of a scanned or mapped image; a range that is not in the image reads as a sentinel
 * that no case expects. */
static uint64_t u64_at(const RtImage *img, uint32_t rva) {
    uint64_t v = UINT64_C(0x5E5E5E5E5E5E5E5E);
    const void *p = rt_ldr_rva(img, rva, 8);
    if (p) memcpy(&v, p, 8);
    return v;
}

static uint32_t u32_at(const RtImage *img, uint32_t rva) {
    uint32_t v = 0x5E5E5E5Eu;
    const void *p = rt_ldr_rva(img, rva, 4);
    if (p) memcpy(&v, p, 4);
    return v;
}

static uint16_t u16_at(const RtImage *img, uint32_t rva) {
    uint16_t v = 0x5E5Eu;
    const void *p = rt_ldr_rva(img, rva, 2);
    if (p) memcpy(&v, p, 2);
    return v;
}

/* Slot a: entry F(0), data, va cells, exports, imports, TLS.  Slot b: one va cell, no
 * relocation directory. */
TEST_SETUP(core, pe_build)
{
    RtTestPe *pe = rt_test_pe_new();
    rt_test_pe_entry(pe, RT_TEST_LIFT_RVA(0));
    rt_test_pe_put(pe, 0x2100, "DATA", 4);
    rt_test_pe_put_va(pe, 0x2010, 0x3000);
    rt_test_pe_put_va(pe, 0x3ff8, RT_TEST_LIFT_RVA(1));
    rt_test_pe_export(pe, "Zeta", 5, RT_TEST_LIFT_RVA(2));
    rt_test_pe_export(pe, "Alpha", 3, 0x2100);
    rt_test_pe_export(pe, NULL, 7, RT_TEST_LIFT_RVA(3));
    rt_test_pe_forward(pe, "Fwd", 4, "OTHER.Name");
    rt_test_pe_import(pe, "kernel32.dll", "GetLastError", 0, 0x2020);
    rt_test_pe_import(pe, "kernel32.dll", NULL, 77, 0x2028);
    rt_test_pe_import(pe, "user32.dll", "MessageBoxA", 0, 0x2040);
    rt_test_pe_tls(pe, 0x3100, 16, 32, 0x2080, 0);
    rt_test_pe_write(pe, "t_lift_a.dll");

    RtTestPe *pb = rt_test_pe_new();
    rt_test_pe_put_va(pb, 0x2010, 0x3000);
    rt_test_pe_no_relocs(pb);
    rt_test_pe_write(pb, "t_lift_b.dll");
}

TEST(core, pe_build)
{
    RtModule    *A = NULL, *B = NULL;
    RtLdrModule *LA = NULL, *LB = NULL;
    RtImage      img, imgb;

    T_PLAN(1, "the loader's reader accepts the file") {
        A = rt_module_find("t_lift_a.dll");
        LA = rt_ldr_module(A);
        T_REQUIRE(rt_ldr_image_scan(LA, &img));
    }

    T_PLAN(2, "the header fields") {
        T_EQ(img.size, 0x10000);
        T_EQ(img.entry_rva, RT_TEST_LIFT_RVA(0));
        T_EQ(img.preferred_base, RT_TEST_PE_BASE);
        T_EQ(img.timestamp, RT_TEST_PE_TIMESTAMP);
        T_EQ(img.nsections, 4);
    }

    T_PLAN(3, "the sections in order") {
        static const struct { const char *name; uint32_t rva; uint32_t bits; } want[4] = {
            { ".text",   0x1000, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_EXECUTE },
            { ".data",   0x2000, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE },
            { ".rdata",  0x3000, IMAGE_SCN_MEM_READ },
            { ".tables", 0x8000, IMAGE_SCN_MEM_READ },
        };
        for (unsigned i = 0; i < 4; i++) {
            const RtLdrSection *s = &img.sections[i];
            T_TRUE(!strcmp(s->name, want[i].name));
            T_EQ(s->rva, want[i].rva);
            T_EQ(s->chars & (IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE | IMAGE_SCN_MEM_EXECUTE), want[i].bits);
        }
    }

    T_PLAN(4, "the data the test put in") {
        const void *p = rt_ldr_rva(&img, 0x2100, 4);
        T_TRUE(p != NULL && memcmp(p, "DATA", 4) == 0);
        T_EQ(u64_at(&img, 0x2010), RT_TEST_PE_BASE + 0x3000);
        T_EQ(u64_at(&img, 0x3ff8), RT_TEST_PE_BASE + RT_TEST_LIFT_RVA(1));
    }

    T_PLAN(5, "the export directory") {
        const IMAGE_DATA_DIRECTORY ed = img.dir[IMAGE_DIRECTORY_ENTRY_EXPORT];
        const IMAGE_EXPORT_DIRECTORY *d = rt_ldr_rva(&img, ed.VirtualAddress, sizeof *d);
        T_REQUIRE(d != NULL);
        T_EQ(d->Base, 3);
        T_EQ(d->NumberOfFunctions, 5);
        T_EQ(d->NumberOfNames, 3);
        const uint32_t *names = rt_ldr_rva(&img, d->AddressOfNames, 4 * d->NumberOfNames);
        const uint32_t *fns = rt_ldr_rva(&img, d->AddressOfFunctions, 4 * d->NumberOfFunctions);
        T_REQUIRE(names != NULL && fns != NULL);
        static const char *const order[3] = { "Alpha", "Fwd", "Zeta" };
        for (unsigned i = 0; i < 3; i++) {
            const char *s = rt_ldr_cstr(&img, names[i]);
            T_TRUE(s != NULL && !strcmp(s, order[i]));
        }
        /* The function of ordinal o is entry o - Base. */
        T_EQ(fns[3 - d->Base], 0x2100);
        T_EQ(fns[5 - d->Base], RT_TEST_LIFT_RVA(2));
        T_EQ(fns[7 - d->Base], RT_TEST_LIFT_RVA(3));
        T_EQ(fns[6 - d->Base], 0);
        uint32_t fwd = fns[4 - d->Base];
        T_TRUE(fwd >= ed.VirtualAddress && fwd < ed.VirtualAddress + ed.Size);
        const char *f = rt_ldr_cstr(&img, fwd);
        T_TRUE(f != NULL && !strcmp(f, "OTHER.Name"));
    }

    T_PLAN(6, "the import directory") {
        const IMAGE_DATA_DIRECTORY id = img.dir[IMAGE_DIRECTORY_ENTRY_IMPORT];
        const IMAGE_IMPORT_DESCRIPTOR *d = rt_ldr_rva(&img, id.VirtualAddress, 3 * sizeof *d);
        T_REQUIRE(d != NULL);
        const char *dll0 = rt_ldr_cstr(&img, d[0].Name);
        const char *dll1 = rt_ldr_cstr(&img, d[1].Name);
        T_TRUE(dll0 != NULL && !strcmp(dll0, "kernel32.dll"));
        T_TRUE(dll1 != NULL && !strcmp(dll1, "user32.dll"));
        T_TRUE(d[2].OriginalFirstThunk == 0 && d[2].TimeDateStamp == 0 && d[2].ForwarderChain == 0
               && d[2].Name == 0 && d[2].FirstThunk == 0);
        T_EQ(d[0].FirstThunk, 0x2020);
        T_EQ(d[1].FirstThunk, 0x2040);

        /* The first name-table entries of kernel32: by name, then by ordinal, then the end. */
        const uint64_t *nt = rt_ldr_rva(&img, d[0].OriginalFirstThunk, 3 * sizeof *nt);
        T_REQUIRE(nt != NULL);
        const char *gle = rt_ldr_cstr(&img, (uint32_t)nt[0] + 2);
        T_TRUE(u16_at(&img, (uint32_t)nt[0]) == 0 && gle != NULL && !strcmp(gle, "GetLastError"));
        T_EQ(nt[1], IMAGE_ORDINAL_FLAG64 | 77);
        T_EQ(nt[2], 0);

        /* The slots hold the file's fill, the gap after the last slot of a DLL is zero. */
        T_EQ(u64_at(&img, 0x2020), UINT64_C(0xBAD0BAD0BAD0BAD0));
        T_EQ(u64_at(&img, 0x2028), UINT64_C(0xBAD0BAD0BAD0BAD0));
        T_EQ(u64_at(&img, 0x2040), UINT64_C(0xBAD0BAD0BAD0BAD0));
        T_EQ(u64_at(&img, 0x2030), 0);
    }

    T_PLAN(7, "the TLS directory") {
        const IMAGE_DATA_DIRECTORY td = img.dir[IMAGE_DIRECTORY_ENTRY_TLS];
        const IMAGE_TLS_DIRECTORY64 *t = rt_ldr_rva(&img, td.VirtualAddress, sizeof *t);
        T_REQUIRE(t != NULL);
        T_EQ(t->StartAddressOfRawData, RT_TEST_PE_BASE + 0x3100);
        T_EQ(t->EndAddressOfRawData, RT_TEST_PE_BASE + 0x3110);
        T_EQ(t->AddressOfIndex, RT_TEST_PE_BASE + 0x2080);
        T_EQ(t->AddressOfCallBacks, 0);
        T_EQ(t->SizeOfZeroFill, 32);
    }

    T_PLAN(8, "the relocations") {
        const IMAGE_DATA_DIRECTORY rd = img.dir[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        const IMAGE_DATA_DIRECTORY td = img.dir[IMAGE_DIRECTORY_ENTRY_TLS];
        /* The DIR64 fields: the two data cells and the three non-zero addresses of the TLS
         * directory (start, end, index; the callback array is 0 and has no field to fix). */
        const uint32_t want[5] = { 0x2010, 0x3ff8, td.VirtualAddress, td.VirtualAddress + 8, td.VirtualAddress + 16 };
        uint32_t got[16];
        unsigned bad = 0, ng = 0;
        for (uint32_t off = 0; off < rd.Size; ) {
            uint32_t page = u32_at(&img, rd.VirtualAddress + off);
            uint32_t bsize = u32_at(&img, rd.VirtualAddress + off + 4);
            if (bsize < 8 || bsize > rd.Size - off) { bad++; break; }
            if (bsize % 4 != 0) bad++;
            for (uint32_t e = 8; e < bsize; e += 2) {
                uint16_t v = u16_at(&img, rd.VirtualAddress + off + e);
                if (v >> 12 == IMAGE_REL_BASED_DIR64) {
                    if (ng < 16) got[ng] = page + (v & 0xFFF);
                    ng++;
                }
            }
            off += bsize;
        }
        T_EQ(bad, 0);
        T_EQ(ng, 5);
        for (unsigned w = 0; w < 5; w++) {
            bool found = false;
            for (unsigned g = 0; g < ng && g < 16; g++)
                if (got[g] == want[w]) found = true;
            T_TRUE(found);
        }
    }

    T_PLAN(9, "map a: the relocated cell") {
        rt_ldr_image_map(LA);
        T_EQ(LD64(A->base + 0x2010), A->base + 0x3000);
    }

    T_PLAN(10, "slot b has no relocation directory") {
        B = rt_module_find("t_lift_b.dll");
        LB = rt_ldr_module(B);
        T_TRUE(rt_ldr_image_scan(LB, &imgb));
        T_EQ(imgb.dir[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size, 0);
        rt_ldr_image_map(LB);
        T_EQ(LD64(B->base + 0x2010), RT_TEST_PE_BASE + 0x3000);
    }

    T_PLAN(11, "misuse is not tested") { T_TRUE(1); }
}
