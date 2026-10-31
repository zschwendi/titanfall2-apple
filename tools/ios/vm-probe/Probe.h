#ifndef TF_VM_PROBE_H
#define TF_VM_PROBE_H
#include <stddef.h>
#include <stdint.h>
typedef struct {
    const char *name;
    const char *method;
    uint64_t image_base, image_size, vm_base, vm_size;
    uint64_t image_actual, vm_actual;
    int image_error, vm_error, exact, fixed;
} TFVMProbe;
#define TF_VM_PROBE_COUNT 5
size_t tf_vm_probe(TFVMProbe *results, size_t capacity);
#endif
