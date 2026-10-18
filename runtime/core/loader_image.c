/* The loader: a DLL file and its image (docs/RUNTIME.md sections 1.1, 1.3, 1.6 steps 1, 2
 * and 5).  Internal interface and each function's specification: loader.h.
 *
 * One routine, build(), turns the bytes of a DLL file into a flat image at a destination:
 * a host buffer at start-up (rt_ldr_image_scan: the import and TLS tables are read from it
 * and it is thrown away) or the module's place in the image arena at load time
 * (rt_ldr_image_map).  Both verify the file the same way, so a file that changed between
 * start-up and the load is caught.
 *
 * WHAT IS REFUSED (fatal, with the file's host path and the reason).  The loader pairs the
 * file with lifted code and with a base chosen from the manifest's SizeOfImage, so anything
 * unexpected is a broken build or install, not a condition the guest handles:
 *   - identity: file size, PE timestamp, SHA-256 differ from the module table's row (not
 *     compared for a row with RT_MODDEF_UNCHECKED); SizeOfImage differs (always compared);
 *   - not a PE32+ x64 DLL: no "MZ", e_lfanew outside the file, no "PE\0\0", Machine not
 *     0x8664, optional-header magic not 0x20B, IMAGE_FILE_DLL not set, an optional header
 *     shorter than its fixed part plus the directories it counts;
 *   - layout this loader does not handle (what Windows does with each is not verified):
 *     SectionAlignment other than 0x1000; SizeOfImage not a multiple of it; more than
 *     RT_LDR_MAX_SECTIONS sections; SizeOfHeaders beyond the file or the first section;
 *     a section whose address is not a multiple of 0x1000, that starts before the end of
 *     the previous one, that runs past SizeOfImage, or whose raw bytes run past the file;
 *   - a data directory that does not lie inside the image;
 *   - relocations: a block outside the directory or shorter than its header, an entry of a
 *     type other than ABSOLUTE (skipped) and DIR64, a DIR64 target outside the image.
 * A section's bytes are the first min(SizeOfRawData, VirtualSize) bytes of its raw data;
 * the rest of the image is zero.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <CommonCrypto/CommonDigest.h>

#include "loader.h"

#define BAD(lm, ...) bad((lm), __VA_ARGS__)

static _Noreturn void bad(const RtLdrModule *lm, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static _Noreturn void bad(const RtLdrModule *lm, const char *fmt, ...) {
    char why[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, sizeof why, fmt, ap);
    va_end(ap);
    rt_fatal("%s: cannot use the file %s: %s", lm->m->name, lm->host_path, why);
}

void *rt_ldr_rva(const RtImage *img, uint32_t rva, uint32_t len) {
    if (rva > img->size || len > img->size - rva) return NULL;
    return img->bytes + rva;
}

const char *rt_ldr_cstr(const RtImage *img, uint32_t rva) {
    if (rva >= img->size) return NULL;
    return memchr(img->bytes + rva, 0, img->size - rva) ? (const char *)img->bytes + rva : NULL;
}

/* Where the DLL is: C:\Titanfall2\<dir>\<file> through the path layer.  False if there is
 * no such file. */
static bool locate(RtLdrModule *lm) {
    const RtModuleDef *d = lm->m->def;
    char win[RT_PATH_MAX];
    const char *dir = d->dir ? d->dir : "";
    snprintf(win, sizeof win, "C:\\Titanfall2\\%s%s%s", dir, dir[0] ? "\\" : "", d->file);
    DWORD e = rt_path_to_host_a(win, RT_PATH_EXISTING, lm->host_path);
    if (e == 0) return true;
    lm->host_path[0] = 0;
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return false;
    rt_fatal("%s: the path %s cannot be translated (error %u)", lm->m->name, win, e);
}

/* The whole file in a malloc'd buffer, or NULL if it does not exist. */
static uint8_t *read_file(RtLdrModule *lm, size_t *size) {
    if (!locate(lm)) return NULL;
    int fd = open(lm->host_path, O_RDONLY);
    if (fd < 0 && errno == ENOENT) { lm->host_path[0] = 0; return NULL; }
    struct stat st;
    if (fd < 0 || fstat(fd, &st) != 0) BAD(lm, "it cannot be opened (%s)", strerror(errno));
    if (st.st_size < 0x40 || st.st_size > INT32_MAX) BAD(lm, "its size is %lld bytes", (long long)st.st_size);
    uint8_t *buf = malloc((size_t)st.st_size);
    if (!buf) rt_fatal("out of memory");
    for (size_t got = 0; got < (size_t)st.st_size; ) {
        ssize_t n = read(fd, buf + got, (size_t)st.st_size - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) BAD(lm, "it cannot be read (%s)", n ? strerror(errno) : "shorter than its size");
        got += (size_t)n;
    }
    close(fd);
    *size = (size_t)st.st_size;
    return buf;
}

static void check_identity(const RtLdrModule *lm, const uint8_t *file, size_t size, const RtImage *img) {
    const RtModuleDef *d = lm->m->def;
    if (img->size != d->size_of_image)
        BAD(lm, "its SizeOfImage is %#x and the lifted code was made from a DLL with %#x", img->size, d->size_of_image);
    if (d->flags & RT_MODDEF_UNCHECKED) {
        rt_log_once(RT_CH_LOADER, RT_LL_WARN, lm->m->name, "%s: the file's identity is not checked (test manifest row)", lm->m->name);
        return;
    }
    if (size != d->file_size) BAD(lm, "it is %zu bytes and the lifted code was made from a DLL of %u", size, d->file_size);
    if (img->timestamp != d->pe_timestamp)
        BAD(lm, "its PE timestamp is %#x and the lifted code was made from a DLL with %#x", img->timestamp, d->pe_timestamp);
    unsigned char md[CC_SHA256_DIGEST_LENGTH];
    char hex[2 * CC_SHA256_DIGEST_LENGTH + 1];
    CC_SHA256(file, (CC_LONG)size, md);
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) snprintf(hex + 2 * i, 3, "%02x", md[i]);
    if (!d->sha256 || strcmp(hex, d->sha256))
        BAD(lm, "its SHA-256 is %s and the lifted code was made from a DLL with %s", hex, d->sha256 ? d->sha256 : "(none)");
}

/* Parse and verify `file`, fill *img (bytes = dest) and lay the image out at `dest`, which
 * must be SizeOfImage writable zero bytes; dest NULL only parses and verifies. */
static void build(const RtLdrModule *lm, const uint8_t *file, size_t size, RtImage *img, uint8_t *dest) {
    memset(img, 0, sizeof *img);
    img->m = lm->m;
    uint32_t nt;
    memcpy(&nt, file + IMAGE_DOS_LFANEW, 4);
    if (file[0] != 'M' || file[1] != 'Z') BAD(lm, "it does not start with MZ");
    if (nt > size || size - nt < 24 + sizeof(IMAGE_OPTIONAL_HEADER64) - sizeof(IMAGE_DATA_DIRECTORY) * 16)
        BAD(lm, "its PE header offset %#x is outside the file", nt);
    IMAGE_FILE_HEADER fh;
    IMAGE_OPTIONAL_HEADER64 oh;
    memset(&oh, 0, sizeof oh);
    memcpy(&fh, file + nt + 4, sizeof fh);
    if (memcmp(file + nt, "PE\0\0", 4)) BAD(lm, "it has no PE signature");
    if (fh.Machine != IMAGE_FILE_MACHINE_AMD64) BAD(lm, "its machine type is %#x, not x64", fh.Machine);
    if (!(fh.Characteristics & IMAGE_FILE_DLL)) BAD(lm, "it is not a DLL (characteristics %#x)", fh.Characteristics);
    size_t opt = nt + 24, fixed = sizeof oh - sizeof oh.DataDirectory;
    memcpy(&oh, file + opt, fixed);
    if (oh.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC) BAD(lm, "its optional header magic is %#x, not PE32+", oh.Magic);
    unsigned ndirs = oh.NumberOfRvaAndSizes < 16 ? oh.NumberOfRvaAndSizes : 16;
    if (fh.SizeOfOptionalHeader < fixed + 8 * ndirs || size - opt < fh.SizeOfOptionalHeader)
        BAD(lm, "its optional header (%u bytes) is too short for %u data directories", fh.SizeOfOptionalHeader, ndirs);
    memcpy(oh.DataDirectory, file + opt + fixed, 8 * ndirs);

    img->preferred_base = oh.ImageBase;
    img->size = oh.SizeOfImage;
    img->headers_size = oh.SizeOfHeaders;
    img->entry_rva = oh.AddressOfEntryPoint;
    img->timestamp = fh.TimeDateStamp;
    memcpy(img->dir, oh.DataDirectory, sizeof img->dir);
    check_identity(lm, file, size, img);

    if (oh.SectionAlignment != 0x1000) BAD(lm, "its SectionAlignment is %#x; only 0x1000 is handled", oh.SectionAlignment);
    if (img->size == 0 || img->size % 0x1000) BAD(lm, "its SizeOfImage %#x is not a multiple of 4 KB", img->size);
    if (fh.NumberOfSections > RT_LDR_MAX_SECTIONS) BAD(lm, "it has %u sections", fh.NumberOfSections);
    size_t sh = opt + fh.SizeOfOptionalHeader;
    if (sh + 40u * fh.NumberOfSections > size || oh.SizeOfHeaders > size || oh.SizeOfHeaders < sh + 40u * fh.NumberOfSections
        || oh.SizeOfHeaders > img->size)
        BAD(lm, "its headers (SizeOfHeaders %#x, %u sections) do not fit the file", oh.SizeOfHeaders, fh.NumberOfSections);
    if (img->entry_rva >= img->size) BAD(lm, "its entry point %#x is outside the image", img->entry_rva);
    for (unsigned i = 0; i < 16; i++)
        if (img->dir[i].Size && i != 4 /* the certificate table holds a file offset */
            && (img->dir[i].VirtualAddress > img->size || img->dir[i].Size > img->size - img->dir[i].VirtualAddress))
            BAD(lm, "its data directory %u (%#x + %#x) is outside the image", i, img->dir[i].VirtualAddress, img->dir[i].Size);

    if (dest) memcpy(dest, file, oh.SizeOfHeaders);
    uint32_t end = (oh.SizeOfHeaders + 0xFFFu) & ~0xFFFu;        /* end of what is laid out so far */
    img->nsections = fh.NumberOfSections;
    for (unsigned i = 0; i < fh.NumberOfSections; i++) {
        IMAGE_SECTION_HEADER s;
        memcpy(&s, file + sh + 40u * i, sizeof s);
        RtLdrSection *o = &img->sections[i];
        memcpy(o->name, s.Name, 8);
        o->rva = s.VirtualAddress;
        o->vsize = s.VirtualSize ? s.VirtualSize : s.SizeOfRawData;
        o->raw_off = s.PointerToRawData;
        o->raw_size = s.SizeOfRawData;
        o->chars = s.Characteristics;
        if (o->rva % 0x1000 || o->rva < end || o->rva > img->size || o->vsize > img->size - o->rva)
            BAD(lm, "its section %u (%s) at %#x + %#x is misplaced (previous end %#x, SizeOfImage %#x)",
                i, o->name, o->rva, o->vsize, end, img->size);
        uint32_t ncopy = o->raw_size < o->vsize ? o->raw_size : o->vsize;
        if (ncopy && (o->raw_off > size || ncopy > size - o->raw_off))
            BAD(lm, "the raw data of its section %u (%s) is outside the file", i, o->name);
        if (dest && ncopy) memcpy(dest + o->rva, file + o->raw_off, ncopy);
        end = (o->rva + o->vsize + 0xFFFu) & ~0xFFFu;
    }
    img->bytes = dest;
}

bool rt_ldr_image_scan(RtLdrModule *lm, RtImage *out) {
    size_t size;
    uint8_t *file = read_file(lm, &size);
    if (!file) return false;
    RtImage probe;
    build(lm, file, size, &probe, NULL);            /* verifies; gives SizeOfImage */
    uint8_t *flat = calloc(1, probe.size);
    if (!flat) rt_fatal("out of memory");
    build(lm, file, size, out, flat);
    free(file);
    return true;
}

void rt_ldr_image_release(RtImage *img) {
    if (img->mapped) rt_fatal("rt_ldr_image_release on the mapped image of %s", img->m->name);
    free(img->bytes);
    img->bytes = NULL;
}

/* Section 1.6 step 1: add base - ImageBase to every DIR64 slot. */
static void relocate(const RtLdrModule *lm, RtImage *img) {
    IMAGE_DATA_DIRECTORY d = img->dir[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    uint64_t delta = lm->m->base - img->preferred_base;
    unsigned long long applied = 0;
    for (uint32_t off = 0; off < d.Size; ) {
        IMAGE_BASE_RELOCATION b;
        if (d.Size - off < sizeof b) BAD(lm, "its relocation directory ends inside a block header at %#x", off);
        memcpy(&b, img->bytes + d.VirtualAddress + off, sizeof b);
        if (b.SizeOfBlock < sizeof b || b.SizeOfBlock % 2 || b.SizeOfBlock > d.Size - off)
            BAD(lm, "its relocation block at %#x has size %#x", off, b.SizeOfBlock);
        for (uint32_t e = sizeof b; e < b.SizeOfBlock; e += 2) {
            uint16_t v;
            memcpy(&v, img->bytes + d.VirtualAddress + off + e, 2);
            unsigned type = v >> 12;
            if (type == IMAGE_REL_BASED_ABSOLUTE) continue;
            if (type != IMAGE_REL_BASED_DIR64) BAD(lm, "it has a relocation of type %u at block %#x", type, b.VirtualAddress);
            uint8_t *slot = rt_ldr_rva(img, b.VirtualAddress + (v & 0xFFFu), 8);
            if (!slot || b.VirtualAddress > img->size) BAD(lm, "it relocates %#x, outside the image", b.VirtualAddress + (v & 0xFFFu));
            uint64_t x;
            memcpy(&x, slot, 8);
            x += delta;
            memcpy(slot, &x, 8);
            applied++;
        }
        off += b.SizeOfBlock;
    }
    rt_log(RT_CH_LOADER, RT_LL_INFO, "%s: image built at %#llx (preferred %#llx), %llu relocations", lm->m->name,
           (unsigned long long)lm->m->base, (unsigned long long)img->preferred_base, applied);
}

void rt_ldr_image_map(RtLdrModule *lm) {
    RtModule *m = lm->m;
    if (lm->img.mapped) rt_fatal("rt_ldr_image_map(%s): the image is already built", m->name);
    size_t size;
    uint8_t *file = read_file(lm, &size);
    if (!file) rt_fatal("%s: its DLL file was there when the runtime started and is gone now", m->name);
    /* Commit first, copy afterwards (rt/vm.h: a commit zeroes what it adds). */
    if (rt_vm_alloc(m->base, m->size, MEM_COMMIT, PAGE_READWRITE, RT_VM_IMAGE) != m->base)
        rt_fatal("%s: cannot commit the image at %#llx (error %u)", m->name, (unsigned long long)m->base, rt_thread_get_last_error());
    build(lm, file, size, &lm->img, (uint8_t *)(uintptr_t)m->base);
    free(file);
    relocate(lm, &lm->img);
    uint32_t nt;
    memcpy(&nt, lm->img.bytes + IMAGE_DOS_LFANEW, 4);
    memcpy(lm->img.bytes + nt + 24 + offsetof(IMAGE_OPTIONAL_HEADER64, ImageBase), &m->base, 8);
    lm->img.mapped = true;
}

void rt_ldr_image_protect(RtLdrModule *lm) {
    const RtImage *img = &lm->img;
    DWORD old;
    if (!img->mapped) rt_fatal("rt_ldr_image_protect(%s): the image is not built", lm->m->name);
    if (!rt_vm_protect(lm->m->base, img->size, PAGE_READONLY, &old))
        rt_fatal("%s: cannot protect the image (error %u)", lm->m->name, rt_thread_get_last_error());
    for (unsigned i = 0; i < img->nsections; i++) {
        const RtLdrSection *s = &img->sections[i];
        DWORD p = (s->chars & IMAGE_SCN_MEM_WRITE) ? PAGE_READWRITE
                : (s->chars & IMAGE_SCN_MEM_EXECUTE) ? PAGE_EXECUTE_READ : PAGE_READONLY;
        if (s->vsize == 0 || p == PAGE_READONLY) continue;
        if (!rt_vm_protect(lm->m->base + s->rva, s->vsize, p, &old))
            rt_fatal("%s: cannot protect section %s (error %u)", lm->m->name, s->name, rt_thread_get_last_error());
    }
}
