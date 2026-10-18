// Litmus test: does thread_set_x86_64_compat() give a native arm64 thread x86-style (TSO) ordering?
// Writer: x = i; y = i (in that order). Reader: ry = y; rx = x. Under TSO rx >= ry always.
// Under ARM's weak model either the two stores or the two loads may be reordered -> rx < ry.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <mach/mach.h>
#include <mach/mach_traps.h>
#include <mach/mach_error.h>
#include <os/arch/arm64.h>

struct { volatile uint64_t x; char pad1[4096]; volatile uint64_t y; char pad2[4096]; } s __attribute__((aligned(16384)));
static atomic_int stop_flag;
static int want_compat;
static uint64_t violations, reads;
static kern_return_t kr_writer = -1, kr_reader = -1;

static void *writer(void *arg) {
    if (want_compat) kr_writer = thread_set_x86_64_compat(1);
    uint64_t i = 1;
    while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
        for (int k = 0; k < 4096; k++) { s.x = i; s.y = i; i++; }
    }
    return NULL;
}
static void *reader(void *arg) {
    if (want_compat) kr_reader = thread_set_x86_64_compat(1);
    uint64_t v = 0, n = 0;
    while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
        for (int k = 0; k < 4096; k++) { uint64_t ry = s.y; uint64_t rx = s.x; if (rx < ry) v++; n++; }
    }
    violations = v; reads = n;
    return NULL;
}
int main(int argc, char **argv) {
    want_compat = argc > 1 && atoi(argv[1]);
    double secs = argc > 2 ? atof(argv[2]) : 3.0;
    if (__builtin_available(macOS 26.6, *))
        printf("os_cross_arch_is_supported(X86_64) = %d\n", os_cross_arch_is_supported(OS_CROSS_ARCH_X86_64));
    if (__builtin_available(macOS 26.4, *))
        printf("os_custom_x18_abi_enabled() = %d\n", os_custom_x18_abi_enabled());
    pthread_t w, r;
    pthread_create(&w, NULL, writer, NULL);
    pthread_create(&r, NULL, reader, NULL);
    usleep((useconds_t)(secs * 1e6));
    atomic_store(&stop_flag, 1);
    pthread_join(w, NULL); pthread_join(r, NULL);
    if (want_compat)
        printf("thread_set_x86_64_compat(1): writer kr=%d (%s), reader kr=%d (%s)\n",
               kr_writer, mach_error_string(kr_writer), kr_reader, mach_error_string(kr_reader));
    printf("compat=%d reads=%llu violations=%llu\n", want_compat, reads, violations);
    return 0;
}
