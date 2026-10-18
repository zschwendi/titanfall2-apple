/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
 * RT-CASES:  reserves a region at no address, 64 KB aligned; reserves at a free address, rounded down to 64 KB; reserve and commit in one step gives zeroed, writable memory; commits a range inside a reserved region, rounded down to the page; committing a committed page does not fail; commit outside any reservation fails with ERROR_INVALID_ADDRESS; MEM_RESET succeeds on committed memory; MEM_LARGE_PAGES fails with ERROR_PRIVILEGE_NOT_HELD; a protection that is not valid fails with ERROR_INVALID_PARAMETER; a size of zero fails with ERROR_INVALID_PARAMETER; MEM_RESET with another flag fails with ERROR_INVALID_PARAMETER; does not change last error on success
 * RT-TRAPS:  MEM_WRITE_WATCH; MEM_PHYSICAL; MEM_RESET with no address; MEM_RESET on an image or a file view; MEM_COMMIT on an image or a file view; memory outside both arenas
 */
#include "rt/shim.h"
#include "rt/vm.h"

/* The kind of memory at an address, for the calls the core refuses by it.  The documentation
 * says such a call fails but names no code for it, so the refusal is made here, loudly. */
enum { REGION_OK, REGION_IMAGE_OR_VIEW, REGION_HOST };

static int region_kind(uint64_t addr)
{
    MEMORY_BASIC_INFORMATION mbi;

    if (rt_vm_query(addr, &mbi) == 0) return REGION_OK;     /* above the user range: the core fails it */
    if (mbi.State == MEM_FREE) return REGION_OK;
    if (rt_vm_in_image_arena(addr) || rt_vm_in_vm_arena(addr))
        return (mbi.Type == MEM_IMAGE || mbi.Type == MEM_MAPPED) ? REGION_IMAGE_OR_VIEW : REGION_OK;
    return REGION_HOST;                                      /* host memory outside both arenas */
}

SHIM(kernel32, LPVOID, VirtualAlloc, LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)
{
    uint64_t addr = (uint64_t)(uintptr_t)lpAddress;
    int kind;

    /* Allocation types Windows has and this runtime does not build.  They are refused before
     * the core checks the other arguments, so an invalid call that asks for one of them stops
     * the run where Windows would have answered with an error. */
    if (flAllocationType & MEM_WRITE_WATCH)
        RT_UNIMPLEMENTED(ctx, "MEM_WRITE_WATCH: writes are not tracked by this runtime");
    if (flAllocationType & MEM_PHYSICAL)
        RT_UNIMPLEMENTED(ctx, "MEM_PHYSICAL: address windowing is not built");
    if (flAllocationType == MEM_RESET && addr == 0)
        RT_UNIMPLEMENTED(ctx, "MEM_RESET with no address: what Windows does is not documented");

    /* Commit and reset of a range that is not private memory: the documentation says the call
     * fails there, and names no code.  The core refuses the same cases after its own checks. */
    if (addr != 0 && (flAllocationType == MEM_RESET ||
                      ((flAllocationType & MEM_COMMIT) && !(flAllocationType & (MEM_RESERVE | MEM_RESET))))) {
        kind = region_kind(addr);
        if (kind == REGION_IMAGE_OR_VIEW && flAllocationType == MEM_RESET)
            RT_UNIMPLEMENTED(ctx, "MEM_RESET on an image or a file view: the failure code is not documented");
        if (kind == REGION_IMAGE_OR_VIEW)
            RT_UNIMPLEMENTED(ctx, "MEM_COMMIT on an image or a file view: the failure code is not documented");
        if (kind == REGION_HOST)
            RT_UNIMPLEMENTED(ctx, "memory outside both arenas (host memory): commit or reset is not built");
    }

    return (LPVOID)(uintptr_t)rt_vm_alloc(addr, dwSize, flAllocationType, flProtect, RT_VM_PRIVATE);
}
