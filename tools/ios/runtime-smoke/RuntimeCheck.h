#ifndef TF_RUNTIME_CHECK_H
#define TF_RUNTIME_CHECK_H
#include <stdint.h>
/* Keep Windows BOOL declarations out of UIKit's Objective-C translation unit. */
typedef struct {
    uint64_t image_base, vm_base, vm_size, dispatch_entries;
    unsigned native_module_count;
    int runtime_started, allocated, zero_filled, read_write, read_only_protection;
    int query_matches, decommit, recommit_zero_filled, release, passed;
} TFRuntimeCheck;
TFRuntimeCheck tf_runtime_check(const char *state_dir, const char *install_root);
#endif
