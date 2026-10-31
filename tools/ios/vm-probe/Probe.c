#include "Probe.h"
#include "rt/vm.h"
#include <errno.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <sys/mman.h>

/* VM_FLAGS_FIXED does not replace existing mappings: OVERWRITE is deliberately
 * absent. Reserve inaccessible address space only, just like the mmap probes. */
static uint64_t fixed_reservation(uint64_t base, uint64_t size, int *error) {
    _Static_assert(sizeof(vm_address_t) == sizeof(uint64_t), "64-bit VM API required");
    vm_address_t address = (vm_address_t)base;
    *error = vm_map(mach_task_self(), &address, (vm_size_t)size, 0,
        VM_FLAGS_FIXED, MACH_PORT_NULL, 0, FALSE, VM_PROT_NONE,
        VM_PROT_READ | VM_PROT_WRITE, VM_INHERIT_NONE);
    return *error == KERN_SUCCESS ? (uint64_t)address : 0;
}

size_t tf_vm_probe(TFVMProbe *results, size_t capacity) {
    TFVMProbe plans[] = {
        { .name = "upstream", .method = "mmap-hint", .image_base = RT_IMAGE_ARENA_BASE,
          .image_size = RT_IMAGE_ARENA_SIZE, .vm_base = RT_VM_ARENA_BASE,
          .vm_size = RT_VM_ARENA_SIZE },
        { .name = "compact-candidate-only", .method = "mmap-hint", .image_base = UINT64_C(8) << 30,
          .image_size = UINT64_C(4) << 30, .vm_base = UINT64_C(16) << 30,
          .vm_size = UINT64_C(16) << 30 },
        { .name = "upstream", .method = "vm-map-fixed", .fixed = 1, .image_base = RT_IMAGE_ARENA_BASE,
          .image_size = RT_IMAGE_ARENA_SIZE, .vm_base = RT_VM_ARENA_BASE,
          .vm_size = RT_VM_ARENA_SIZE },
        { .name = "compact-candidate-only", .method = "vm-map-fixed", .fixed = 1, .image_base = UINT64_C(8) << 30,
          .image_size = UINT64_C(4) << 30, .vm_base = UINT64_C(16) << 30,
          .vm_size = UINT64_C(16) << 30 },
        /* On the physical M5 iPad, mmap returned 448 GiB for the first 4 GiB
         * reservation. Test that observed region without overwriting anything. */
        { .name = "ios-high-candidate-only", .method = "vm-map-fixed", .fixed = 1,
          .image_base = RT_IOS_IMAGE_ARENA_BASE, .image_size = RT_IMAGE_ARENA_SIZE,
          .vm_base = RT_IOS_VM_ARENA_BASE, .vm_size = RT_IOS_VM_ARENA_SIZE },
    };
    _Static_assert(sizeof plans / sizeof plans[0] == TF_VM_PROBE_COUNT, "update probe capacity");
    size_t count = sizeof plans / sizeof plans[0];
    if (capacity < count) return 0;
    for (size_t i = 0; i < count; ++i) {
        TFVMProbe r = plans[i];
        if (r.fixed) {
            r.image_actual = fixed_reservation(r.image_base, r.image_size, &r.image_error);
            r.vm_actual = fixed_reservation(r.vm_base, r.vm_size, &r.vm_error);
            r.exact = r.image_actual == r.image_base && r.vm_actual == r.vm_base;
            if (r.vm_actual) vm_deallocate(mach_task_self(), r.vm_actual, r.vm_size);
            if (r.image_actual) vm_deallocate(mach_task_self(), r.image_actual, r.image_size);
            results[i] = r;
            continue;
        }
        /* Same non-overwriting reservations as rt_vm_init. No physical pages
         * are committed, and both mappings are held until the pair is tested. */
        errno = 0;
        void *image = mmap((void *)(uintptr_t)r.image_base, r.image_size,
            PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        r.image_error = image == MAP_FAILED ? errno : 0;
        r.image_actual = image == MAP_FAILED ? 0 : (uintptr_t)image;
        errno = 0;
        void *vm = mmap((void *)(uintptr_t)r.vm_base, r.vm_size,
            PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        r.vm_error = vm == MAP_FAILED ? errno : 0;
        r.vm_actual = vm == MAP_FAILED ? 0 : (uintptr_t)vm;
        r.exact = r.image_actual == r.image_base && r.vm_actual == r.vm_base;
        if (vm != MAP_FAILED) munmap(vm, r.vm_size);
        if (image != MAP_FAILED) munmap(image, r.image_size);
        results[i] = r;
    }
    return count;
}
