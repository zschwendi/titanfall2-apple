/* Which functions ran: the hook of a gate3 whose lifted C was compiled with
 * -finstrument-functions (tools/functest/startcov.py builds and runs it).
 * One byte per 4 bytes of the program's text in a shared file, set at a function's entry;
 * nothing else: no lock, no allocation, no float (lifted code runs in the guest float mode).
 * The file is $COVHOOK_OUT; without it the bytes go nowhere. */
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define COV_BYTES (96u << 20)                 /* 384 MB of text; the gate3 binary has about 220 */
static uint8_t cov_none[16384];
static uint8_t *cov_map = cov_none;
static uintptr_t cov_base, cov_n = sizeof cov_none;

__attribute__((constructor, no_instrument_function)) static void cov_init(void) {
    const char *path = getenv("COVHOOK_OUT");
    cov_base = (uintptr_t)_dyld_get_image_header(0);
    if (!path || !*path) return;
    int fd = open(path, O_RDWR | O_CREAT, 0644);
    if (fd < 0 || ftruncate(fd, COV_BYTES) != 0) return;
    void *p = mmap(NULL, COV_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) return;
    cov_map = p;
    cov_n = COV_BYTES;
}

__attribute__((no_instrument_function)) void __cyg_profile_func_enter(void *fn, void *site) {
    (void)site;
    uintptr_t i = ((uintptr_t)fn - cov_base) >> 2;
    if (i < cov_n) cov_map[i] = 1;
}

__attribute__((no_instrument_function)) void __cyg_profile_func_exit(void *fn, void *site) { (void)fn; (void)site; }
