#include "RuntimeCheck.h"
#include "rt/init.h"
#include "rt/dispatch.h"
#include "rt/module.h"
#include "rt/vm.h"

TFRuntimeCheck tf_runtime_check(const char *state_dir, const char *install_root) {
    RtConfig cfg = {.state_dir = state_dir, .install_root = install_root, .tso = RT_TSO_OFF};
    rt_init(&cfg);
    TFRuntimeCheck r = {.runtime_started = 1, .native_module_count = rt_module_count(),
        .dispatch_entries = rt_disp_count(), .image_base = RT_IMAGE_ARENA_BASE,
        .vm_base = RT_VM_ARENA_BASE, .vm_size = RT_VM_ARENA_SIZE};
    uint64_t p = rt_vm_alloc(0, 0x10000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE);
    r.allocated = p != 0;
    if (!p) return r;
    volatile uint64_t *word = (volatile uint64_t *)(uintptr_t)p;
    r.zero_filled = *word == 0;
    *word = UINT64_C(0x1122334455667788);
    r.read_write = *word == UINT64_C(0x1122334455667788);
    DWORD old = 0;
    r.read_only_protection = rt_vm_protect(p, 0x10000, PAGE_READONLY, &old) && old == PAGE_READWRITE;
    MEMORY_BASIC_INFORMATION info = {0};
    r.query_matches = rt_vm_query(p, &info) == sizeof info && info.State == MEM_COMMIT &&
        info.Protect == PAGE_READONLY && *word == UINT64_C(0x1122334455667788);
    r.decommit = rt_vm_free(p, 0x10000, MEM_DECOMMIT);
    int recommitted = rt_vm_alloc(p, 0x10000, MEM_COMMIT, PAGE_READWRITE, RT_VM_PRIVATE) == p;
    r.recommit_zero_filled = recommitted && *word == 0;
    r.release = rt_vm_free(p, 0, MEM_RELEASE);
    r.passed = r.zero_filled && r.read_write && r.read_only_protection && r.query_matches &&
        r.decommit && r.recommit_zero_filled && r.release;
    return r;
}
