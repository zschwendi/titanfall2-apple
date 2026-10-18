/* Test support for the loader's tests (rt/test_pe.h): the PE writer for the test images of the
 * slots t_lift_a/b/c.dll.  The image is laid out as the comment there says.  rt_test_pe_write
 * builds the .tables section (export directory, import descriptors, TLS directory, exception
 * directory, relocations)
 * in a host buffer and writes the file with rt_test_install_write (loader_fixtures.c).  This
 * file calls only rt/test.h, rt/win/pe.h, rt_test_install_write and the host C library.  State:
 * one RtTestPe per image, from rt_test_pe_new until rt_test_pe_write frees it; no global state.
 * Misuse aborts the test program with a message on stderr. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/test_pe.h"

#define PE_SEC_TEXT   0x1000u       /* the RVAs of the layout in rt/test_pe.h */
#define PE_SEC_DATA   0x2000u
#define PE_SEC_RDATA  0x3000u
#define PE_SEC_TABLES 0x8000u
#define PE_SEC_BYTES  0x1000u       /* each of .text, .data and .rdata */
#define PE_IMAGE_SIZE 0x10000u      /* SizeOfImage */
#define PE_HDR_SIZE   0x400u        /* SizeOfHeaders */
#define PE_FILE_ALIGN 0x200u        /* FileAlignment */
#define PE_NT_OFF     0x40u         /* e_lfanew: the "PE\0\0" signature */
#define PE_TABLES_MAX (PE_IMAGE_SIZE - PE_SEC_TABLES)
#define PE_FILE_EXECUTABLE_IMAGE 0x0002u
#define PE_DLL_DYNAMIC_BASE      0x0040u   /* DllCharacteristics: the image has relocations */

struct PeExport { char *name, *forwarder; uint32_t ordinal, rva; };   /* forwarder NULL: a real export */
struct PeImport { char *dll, *name; uint32_t ordinal, slot; };        /* name NULL: by ordinal */
struct PeFunc { uint32_t begin, end, unwind; };                       /* one RUNTIME_FUNCTION row */

struct RtTestPe {
    uint8_t sec[3][PE_SEC_BYTES];               /* .text, .data, .rdata */
    struct PeExport *exp; uint32_t nexp, capexp;
    struct PeImport *imp; uint32_t nimp, capimp;
    uint32_t *rel; uint32_t nrel, caprel;       /* RVAs of the DIR64 fields */
    struct PeFunc *fn; uint32_t nfn, capfn;     /* the exception directory, in the order given */
    bool no_relocs;
    uint32_t entry;
    bool has_tls;
    uint32_t tls_tpl, tls_size, tls_zero, tls_index, tls_cb;
};

static _Noreturn void pe_die(const char *what, const char *msg) {
    fprintf(stderr, "test support: %s: %s\n", what, msg);
    abort();
}

static char *pe_dup(const char *s) {
    char *p = malloc(strlen(s) + 1);
    if (!p) pe_die("rt_test_pe", "out of memory");
    return strcpy(p, s);
}

static void pe_put_le(uint8_t *p, uint64_t v, unsigned n) {
    for (unsigned i = 0; i < n; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* The bytes at [rva, rva + n) of section `want` (0 .text, 1 .data, 2 .rdata; -1: any), or abort. */
static uint8_t *pe_sec_at(RtTestPe *pe, uint32_t rva, uint32_t n, int want, const char *what) {
    if (rva < PE_SEC_TEXT || rva - PE_SEC_TEXT >= 3 * PE_SEC_BYTES) pe_die(what, "RVA outside .text, .data and .rdata");
    uint32_t s = (rva - PE_SEC_TEXT) / PE_SEC_BYTES, off = (rva - PE_SEC_TEXT) % PE_SEC_BYTES;
    if (n > PE_SEC_BYTES - off || (want >= 0 && (int)s != want)) pe_die(what, "range is not inside the section it belongs in");
    return &pe->sec[s][off];
}

static void pe_add_reloc(RtTestPe *pe, uint32_t rva) {
    if (pe->nrel == pe->caprel) {
        pe->caprel = pe->caprel ? 2 * pe->caprel : 16;
        pe->rel = realloc(pe->rel, pe->caprel * sizeof *pe->rel);
        if (!pe->rel) pe_die("rt_test_pe", "out of memory");
    }
    pe->rel[pe->nrel++] = rva;
}

RtTestPe *rt_test_pe_new(void) {
    RtTestPe *pe = calloc(1, sizeof *pe);
    if (!pe) pe_die("rt_test_pe_new", "out of memory");
    return pe;
}

void rt_test_pe_put(RtTestPe *pe, uint32_t rva, const void *bytes, uint32_t n) {
    memcpy(pe_sec_at(pe, rva, n, -1, "rt_test_pe_put"), bytes, n);
}

void rt_test_pe_put32(RtTestPe *pe, uint32_t rva, uint32_t v) {
    uint8_t b[4];
    pe_put_le(b, v, 4);
    rt_test_pe_put(pe, rva, b, 4);
}

void rt_test_pe_put64(RtTestPe *pe, uint32_t rva, uint64_t v) {
    uint8_t b[8];
    pe_put_le(b, v, 8);
    rt_test_pe_put(pe, rva, b, 8);
}

void rt_test_pe_put_va(RtTestPe *pe, uint32_t rva, uint32_t target_rva) {
    if (target_rva >= PE_IMAGE_SIZE) pe_die("rt_test_pe_put_va", "target RVA outside the image");
    rt_test_pe_put64(pe, rva, RT_TEST_PE_BASE + target_rva);
    pe_add_reloc(pe, rva);
}

void rt_test_pe_no_relocs(RtTestPe *pe) {
    pe->no_relocs = true;
}

void rt_test_pe_entry(RtTestPe *pe, uint32_t rva) {
    if (rva >= PE_IMAGE_SIZE) pe_die("rt_test_pe_entry", "RVA outside the image");
    pe->entry = rva;
}

static void pe_add_export(RtTestPe *pe, const char *name, const char *fwd, uint32_t ordinal, uint32_t rva) {
    for (uint32_t i = 0; i < pe->nexp; i++)
        if (pe->exp[i].ordinal == ordinal) pe_die("rt_test_pe_export", "ordinal given twice");
    if (pe->nexp == pe->capexp) {
        pe->capexp = pe->capexp ? 2 * pe->capexp : 8;
        pe->exp = realloc(pe->exp, pe->capexp * sizeof *pe->exp);
        if (!pe->exp) pe_die("rt_test_pe_export", "out of memory");
    }
    struct PeExport *e = &pe->exp[pe->nexp++];
    e->name = name ? pe_dup(name) : NULL;
    e->forwarder = fwd ? pe_dup(fwd) : NULL;
    e->ordinal = ordinal;
    e->rva = rva;
}

void rt_test_pe_export(RtTestPe *pe, const char *name, uint32_t ordinal, uint32_t rva) {
    if (rva >= PE_IMAGE_SIZE) pe_die("rt_test_pe_export", "RVA outside the image");
    pe_add_export(pe, name, NULL, ordinal, rva);
}

void rt_test_pe_forward(RtTestPe *pe, const char *name, uint32_t ordinal, const char *forwarder) {
    pe_add_export(pe, name, forwarder, ordinal, 0);
}

void rt_test_pe_import(RtTestPe *pe, const char *dll, const char *name, uint32_t ordinal, uint32_t slot_rva) {
    if (slot_rva % 8 != 0) pe_die("rt_test_pe_import", "import slot is not 8-aligned");
    if (pe->nimp && strcmp(pe->imp[pe->nimp - 1].dll, dll) == 0) {
        if (slot_rva != pe->imp[pe->nimp - 1].slot + 8)
            pe_die("rt_test_pe_import", "imports of one DLL must have slots 8 bytes apart, ascending");
    } else {
        for (uint32_t i = 0; i < pe->nimp; i++)
            if (strcmp(pe->imp[i].dll, dll) == 0) pe_die("rt_test_pe_import", "imports of one DLL must be added one after another");
    }
    uint8_t *slot = pe_sec_at(pe, slot_rva, 8, 1, "rt_test_pe_import");
    pe_put_le(slot, 0xBAD0BAD0BAD0BAD0ull, 8);
    if (pe->nimp == pe->capimp) {
        pe->capimp = pe->capimp ? 2 * pe->capimp : 8;
        pe->imp = realloc(pe->imp, pe->capimp * sizeof *pe->imp);
        if (!pe->imp) pe_die("rt_test_pe_import", "out of memory");
    }
    struct PeImport *m = &pe->imp[pe->nimp++];
    m->dll = pe_dup(dll);
    m->name = name ? pe_dup(name) : NULL;
    m->ordinal = ordinal;
    m->slot = slot_rva;
}

void rt_test_pe_tls(RtTestPe *pe, uint32_t template_rva, uint32_t template_size, uint32_t zero_fill,
                    uint32_t index_rva, uint32_t callbacks_rva) {
    pe_sec_at(pe, template_rva, template_size, 2, "rt_test_pe_tls");
    pe_sec_at(pe, index_rva, 4, 1, "rt_test_pe_tls");
    if (callbacks_rva) pe_sec_at(pe, callbacks_rva, 8, 2, "rt_test_pe_tls");
    pe->has_tls = true;
    pe->tls_tpl = template_rva;
    pe->tls_size = template_size;
    pe->tls_zero = zero_fill;
    pe->tls_index = index_rva;
    pe->tls_cb = callbacks_rva;
}

void rt_test_pe_pdata(RtTestPe *pe, uint32_t begin_rva, uint32_t end_rva, uint32_t unwind_rva,
                      const void *unwind_info, uint32_t n) {
    if (begin_rva >= end_rva || end_rva > PE_IMAGE_SIZE) pe_die("rt_test_pe_pdata", "not a range of RVAs inside the image");
    if (unwind_info) memcpy(pe_sec_at(pe, unwind_rva, n, 2, "rt_test_pe_pdata"), unwind_info, n);
    if (pe->nfn == pe->capfn) {
        pe->capfn = pe->capfn ? 2 * pe->capfn : 16;
        pe->fn = realloc(pe->fn, pe->capfn * sizeof *pe->fn);
        if (!pe->fn) pe_die("rt_test_pe_pdata", "out of memory");
    }
    pe->fn[pe->nfn++] = (struct PeFunc){ begin_rva, end_rva, unwind_rva };
}

/* The .tables section in host memory: offset 0 is RVA PE_SEC_TABLES. */
struct PeTab { uint8_t b[PE_TABLES_MAX]; uint32_t used; };

static uint32_t pe_tab_alloc(struct PeTab *t, uint32_t n, uint32_t align) {
    uint32_t off = (t->used + align - 1) / align * align;
    if (off > PE_TABLES_MAX || n > PE_TABLES_MAX - off) pe_die("rt_test_pe_write", ".tables is larger than 0x8000 bytes");
    t->used = off + n;
    return off;
}

static uint32_t pe_rva(uint32_t off) {
    return PE_SEC_TABLES + off;
}

static void pe_dir(IMAGE_DATA_DIRECTORY *dd, unsigned idx, uint32_t rva, uint32_t size) {
    dd[idx].VirtualAddress = rva;
    dd[idx].Size = size;
}

/* Export directory, then the EAT, the name pointers, the name ordinals, the names (sorted by
 * strcmp), the forwarder strings and the DLL name: one block, so its Size is the block's. */
static void pe_emit_exports(RtTestPe *pe, struct PeTab *t, const char *file, IMAGE_DATA_DIRECTORY *dd) {
    uint32_t base = UINT32_MAX, last = 0, nnames = 0;
    for (uint32_t i = 0; i < pe->nexp; i++) {
        if (pe->exp[i].ordinal < base) base = pe->exp[i].ordinal;
        if (pe->exp[i].ordinal > last) last = pe->exp[i].ordinal;
        if (pe->exp[i].name) nnames++;
    }
    uint32_t nfn = last - base + 1;
    uint32_t start = pe_tab_alloc(t, sizeof(IMAGE_EXPORT_DIRECTORY), 4);
    uint32_t eat = pe_tab_alloc(t, 4 * nfn, 4), ent = pe_tab_alloc(t, 4 * nnames, 4);
    uint32_t eot = pe_tab_alloc(t, 2 * nnames, 2);

    /* the named exports, sorted by strcmp (insertion sort: a handful of entries) */
    uint32_t *ord = calloc(nnames + 1, sizeof *ord);
    if (!ord) pe_die("rt_test_pe_write", "out of memory");
    uint32_t n = 0;
    for (uint32_t i = 0; i < pe->nexp; i++) {
        if (!pe->exp[i].name) continue;
        uint32_t j = n;
        while (j > 0 && strcmp(pe->exp[ord[j - 1]].name, pe->exp[i].name) > 0) { ord[j] = ord[j - 1]; j--; }
        ord[j] = i;
        n++;
    }
    for (uint32_t k = 0; k < nnames; k++) {
        const struct PeExport *e = &pe->exp[ord[k]];
        uint32_t so = pe_tab_alloc(t, (uint32_t)strlen(e->name) + 1, 1);
        memcpy(t->b + so, e->name, strlen(e->name) + 1);
        pe_put_le(t->b + ent + 4 * k, pe_rva(so), 4);
        pe_put_le(t->b + eot + 2 * k, e->ordinal - base, 2);
    }
    free(ord);

    /* the function table; a forwarder's RVA is its string, which lies in this block */
    for (uint32_t i = 0; i < pe->nexp; i++) {
        const struct PeExport *e = &pe->exp[i];
        uint32_t rva = e->rva;
        if (e->forwarder) {
            uint32_t fo = pe_tab_alloc(t, (uint32_t)strlen(e->forwarder) + 1, 1);
            memcpy(t->b + fo, e->forwarder, strlen(e->forwarder) + 1);
            rva = pe_rva(fo);
        }
        pe_put_le(t->b + eat + 4 * (e->ordinal - base), rva, 4);
    }
    uint32_t dllo = pe_tab_alloc(t, (uint32_t)strlen(file) + 1, 1);
    memcpy(t->b + dllo, file, strlen(file) + 1);

    IMAGE_EXPORT_DIRECTORY ed;
    memset(&ed, 0, sizeof ed);
    ed.Name = pe_rva(dllo);
    ed.Base = base;
    ed.NumberOfFunctions = nfn;
    ed.NumberOfNames = nnames;
    ed.AddressOfFunctions = pe_rva(eat);
    ed.AddressOfNames = pe_rva(ent);
    ed.AddressOfNameOrdinals = pe_rva(eot);
    memcpy(t->b + start, &ed, sizeof ed);
    pe_dir(dd, IMAGE_DIRECTORY_ENTRY_EXPORT, pe_rva(start), t->used - start);
}

/* One import descriptor per run of imports of one DLL: name table (u64 per import, 0-ended),
 * hint/name entries, the DLL name, then the zero descriptor that ends the list. */
static void pe_emit_imports(RtTestPe *pe, struct PeTab *t, IMAGE_DATA_DIRECTORY *dd) {
    uint32_t groups = 0;
    for (uint32_t i = 0; i < pe->nimp; i++)
        if (i == 0 || strcmp(pe->imp[i].dll, pe->imp[i - 1].dll) != 0) groups++;
    uint32_t desc = pe_tab_alloc(t, (groups + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR), 4);
    uint32_t g = 0;
    for (uint32_t i = 0; i < pe->nimp; g++) {
        uint32_t j = i;
        while (j < pe->nimp && strcmp(pe->imp[j].dll, pe->imp[i].dll) == 0) j++;
        uint32_t nt = pe_tab_alloc(t, 8 * (j - i + 1), 8);
        for (uint32_t k = i; k < j; k++) {
            uint64_t v = IMAGE_ORDINAL_FLAG64 | pe->imp[k].ordinal;
            if (pe->imp[k].name) {
                const char *nm = pe->imp[k].name;
                uint32_t hn = pe_tab_alloc(t, 2 + (uint32_t)strlen(nm) + 1, 2);
                memcpy(t->b + hn + 2, nm, strlen(nm) + 1);       /* hint 0 */
                v = pe_rva(hn);
            }
            pe_put_le(t->b + nt + 8 * (k - i), v, 8);
        }
        const char *dll = pe->imp[i].dll;
        uint32_t dn = pe_tab_alloc(t, (uint32_t)strlen(dll) + 1, 1);
        memcpy(t->b + dn, dll, strlen(dll) + 1);
        IMAGE_IMPORT_DESCRIPTOR d;
        memset(&d, 0, sizeof d);
        d.OriginalFirstThunk = pe_rva(nt);
        d.Name = pe_rva(dn);
        d.FirstThunk = pe->imp[i].slot;
        memcpy(t->b + desc + g * sizeof d, &d, sizeof d);
        i = j;
    }
    pe_dir(dd, IMAGE_DIRECTORY_ENTRY_IMPORT, pe_rva(desc), (groups + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR));
}

/* The TLS directory; its address fields that are not 0 are DIR64 relocations, unless
 * rt_test_pe_no_relocs. */
static void pe_emit_tls(RtTestPe *pe, struct PeTab *t, IMAGE_DATA_DIRECTORY *dd) {
    uint32_t off = pe_tab_alloc(t, sizeof(IMAGE_TLS_DIRECTORY64), 8);
    IMAGE_TLS_DIRECTORY64 d;
    memset(&d, 0, sizeof d);
    d.StartAddressOfRawData = RT_TEST_PE_BASE + pe->tls_tpl;
    d.EndAddressOfRawData = RT_TEST_PE_BASE + pe->tls_tpl + pe->tls_size;
    d.AddressOfIndex = RT_TEST_PE_BASE + pe->tls_index;
    d.AddressOfCallBacks = pe->tls_cb ? RT_TEST_PE_BASE + pe->tls_cb : 0;
    d.SizeOfZeroFill = pe->tls_zero;
    memcpy(t->b + off, &d, sizeof d);
    if (!pe->no_relocs) {
        pe_add_reloc(pe, pe_rva(off));
        pe_add_reloc(pe, pe_rva(off) + 8);
        pe_add_reloc(pe, pe_rva(off) + 16);
        if (pe->tls_cb) pe_add_reloc(pe, pe_rva(off) + 24);
    }
    pe_dir(dd, IMAGE_DIRECTORY_ENTRY_TLS, pe_rva(off), sizeof d);
}

static int pe_cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* Blocks by 4 KB page, sorted; each block padded to an even number of entries with ABSOLUTE. */
static void pe_emit_relocs(RtTestPe *pe, struct PeTab *t, IMAGE_DATA_DIRECTORY *dd) {
    qsort(pe->rel, pe->nrel, sizeof *pe->rel, pe_cmp_u32);
    uint32_t n = 0;
    for (uint32_t i = 0; i < pe->nrel; i++)
        if (n == 0 || pe->rel[i] != pe->rel[n - 1]) pe->rel[n++] = pe->rel[i];
    uint32_t start = pe_tab_alloc(t, 0, 4);
    for (uint32_t i = 0; i < n; ) {
        uint32_t page = pe->rel[i] & ~0xFFFu, j = i;
        while (j < n && (pe->rel[j] & ~0xFFFu) == page) j++;
        uint32_t cnt = j - i, padded = cnt + (cnt & 1), size = 8 + 2 * padded;
        uint32_t blk = pe_tab_alloc(t, size, 4);
        IMAGE_BASE_RELOCATION h;
        h.VirtualAddress = page;
        h.SizeOfBlock = size;
        memcpy(t->b + blk, &h, sizeof h);
        for (uint32_t k = 0; k < cnt; k++) {
            uint16_t e = (uint16_t)((IMAGE_REL_BASED_DIR64 << 12) | (pe->rel[i + k] & 0xFFFu));
            pe_put_le(t->b + blk + 8 + 2 * k, e, 2);
        }
        i = j;
    }
    pe_dir(dd, IMAGE_DIRECTORY_ENTRY_BASERELOC, pe_rva(start), t->used - start);
}

static int pe_cmp_func(const void *a, const void *b) {
    uint32_t x = ((const struct PeFunc *)a)->begin, y = ((const struct PeFunc *)b)->begin;
    return (x > y) - (x < y);
}

/* The exception directory: the RUNTIME_FUNCTION rows, 12 bytes each, sorted by BeginAddress. */
static void pe_emit_pdata(RtTestPe *pe, struct PeTab *t, IMAGE_DATA_DIRECTORY *dd) {
    qsort(pe->fn, pe->nfn, sizeof *pe->fn, pe_cmp_func);
    uint32_t off = pe_tab_alloc(t, 12 * pe->nfn, 4);
    for (uint32_t i = 0; i < pe->nfn; i++) {
        pe_put_le(t->b + off + 12 * i, pe->fn[i].begin, 4);
        pe_put_le(t->b + off + 12 * i + 4, pe->fn[i].end, 4);
        pe_put_le(t->b + off + 12 * i + 8, pe->fn[i].unwind, 4);
    }
    pe_dir(dd, IMAGE_DIRECTORY_ENTRY_EXCEPTION, pe_rva(off), 12 * pe->nfn);
}

static void pe_free(RtTestPe *pe) {
    for (uint32_t i = 0; i < pe->nexp; i++) { free(pe->exp[i].name); free(pe->exp[i].forwarder); }
    for (uint32_t i = 0; i < pe->nimp; i++) { free(pe->imp[i].dll); free(pe->imp[i].name); }
    free(pe->exp);
    free(pe->imp);
    free(pe->rel);
    free(pe->fn);
    free(pe);
}

/* The headers (SizeOfHeaders 0x400), then the raw data of the four sections in order. */
size_t rt_test_pe_write(RtTestPe *pe, const char *file) {
    return rt_test_pe_write_in(pe, "bin/x64_retail", file);
}

size_t rt_test_pe_write_in(RtTestPe *pe, const char *dir, const char *file) {
    struct PeTab *t = calloc(1, sizeof *t);
    if (!t) pe_die("rt_test_pe_write", "out of memory");
    IMAGE_DATA_DIRECTORY dd[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
    memset(dd, 0, sizeof dd);
    if (pe->nexp) pe_emit_exports(pe, t, file, dd);
    if (pe->nimp) pe_emit_imports(pe, t, dd);
    if (pe->has_tls) pe_emit_tls(pe, t, dd);
    if (pe->nfn) pe_emit_pdata(pe, t, dd);
    if (!pe->no_relocs && pe->nrel) pe_emit_relocs(pe, t, dd);

    /* .tables: the used bytes rounded to FileAlignment (at least one unit) */
    uint32_t tsize = (t->used + PE_FILE_ALIGN - 1) / PE_FILE_ALIGN * PE_FILE_ALIGN;
    if (tsize == 0) tsize = PE_FILE_ALIGN;
    uint32_t raw_data = PE_HDR_SIZE + PE_SEC_BYTES, raw_rdata = raw_data + PE_SEC_BYTES;
    uint32_t raw_tab = raw_rdata + PE_SEC_BYTES;
    size_t total = (size_t)raw_tab + tsize;
    uint8_t *f = calloc(1, total);
    if (!f) pe_die("rt_test_pe_write", "out of memory");

    IMAGE_FILE_HEADER fh;
    memset(&fh, 0, sizeof fh);
    fh.Machine = IMAGE_FILE_MACHINE_AMD64;
    fh.NumberOfSections = 4;
    fh.TimeDateStamp = RT_TEST_PE_TIMESTAMP;
    fh.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    fh.Characteristics = PE_FILE_EXECUTABLE_IMAGE | IMAGE_FILE_DLL;
    IMAGE_OPTIONAL_HEADER64 oh;
    memset(&oh, 0, sizeof oh);
    oh.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    oh.SizeOfCode = PE_SEC_BYTES;
    oh.SizeOfInitializedData = 2 * PE_SEC_BYTES + tsize;
    oh.AddressOfEntryPoint = pe->entry;
    oh.BaseOfCode = PE_SEC_TEXT;
    oh.ImageBase = RT_TEST_PE_BASE;
    oh.SectionAlignment = PE_SEC_BYTES;
    oh.FileAlignment = PE_FILE_ALIGN;
    oh.SizeOfImage = PE_IMAGE_SIZE;
    oh.SizeOfHeaders = PE_HDR_SIZE;
    oh.DllCharacteristics = dd[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size ? PE_DLL_DYNAMIC_BASE : 0;
    oh.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;
    memcpy(oh.DataDirectory, dd, sizeof dd);

    static const char *const names[4] = { ".text", ".data", ".rdata", ".tables" };
    const uint32_t vaddr[4] = { PE_SEC_TEXT, PE_SEC_DATA, PE_SEC_RDATA, PE_SEC_TABLES };
    const uint32_t rawp[4] = { PE_HDR_SIZE, raw_data, raw_rdata, raw_tab };
    const uint32_t size[4] = { PE_SEC_BYTES, PE_SEC_BYTES, PE_SEC_BYTES, tsize };
    const uint32_t chars[4] = {
        IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ,
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE,
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
        IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ,
    };
    IMAGE_SECTION_HEADER sh[4];
    memset(sh, 0, sizeof sh);
    for (unsigned i = 0; i < 4; i++) {
        memcpy(sh[i].Name, names[i], strlen(names[i]));
        sh[i].VirtualSize = size[i];
        sh[i].VirtualAddress = vaddr[i];
        sh[i].SizeOfRawData = size[i];
        sh[i].PointerToRawData = rawp[i];
        sh[i].Characteristics = chars[i];
    }
    /* DOS header; NT headers at 0x40: signature, file header, optional header, section table */
    pe_put_le(f, IMAGE_DOS_SIGNATURE, 2);
    pe_put_le(f + IMAGE_DOS_LFANEW, PE_NT_OFF, 4);
    pe_put_le(f + PE_NT_OFF, IMAGE_NT_SIGNATURE, 4);
    memcpy(f + PE_NT_OFF + 4, &fh, sizeof fh);
    memcpy(f + PE_NT_OFF + 4 + sizeof fh, &oh, sizeof oh);
    memcpy(f + PE_NT_OFF + 4 + sizeof fh + sizeof oh, sh, sizeof sh);
    memcpy(f + PE_HDR_SIZE, pe->sec[0], PE_SEC_BYTES);
    memcpy(f + raw_data, pe->sec[1], PE_SEC_BYTES);
    memcpy(f + raw_rdata, pe->sec[2], PE_SEC_BYTES);
    memcpy(f + raw_tab, t->b, tsize);

    char path[256];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    rt_test_install_write(path, f, total);
    free(f);
    free(t);
    pe_free(pe);
    return total;
}
