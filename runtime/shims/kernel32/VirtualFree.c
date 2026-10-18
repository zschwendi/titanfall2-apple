/* RT-STATUS: partial
 * RT-DOC:    https://learn.microsoft.com/windows/win32/api/memoryapi/nf-memoryapi-virtualfree
 * RT-CASES:  releases a region reserved by VirtualAlloc, with any state of its pages; decommits committed pages, which read as zero when committed again; decommitting a page that is not committed does not fail; MEM_RELEASE with a size fails with ERROR_INVALID_PARAMETER; MEM_RELEASE at an address that is not a region base fails with ERROR_INVALID_ADDRESS; an address in no reservation fails with ERROR_INVALID_ADDRESS; a free type that is neither MEM_DECOMMIT nor MEM_RELEASE fails with ERROR_INVALID_PARAMETER; does not change last error on success
 * RT-TRAPS:  VirtualFree on an image or a file view; VirtualFree on memory outside both arenas
 */
#include "rt/shim.h"
#include "rt/vm.h"

/* The kind of memory at an address, for the calls the core would free anyway.  VirtualFree
 * is for private memory: the documentation says nothing about an image or a file view, so
 * the call is refused here rather than given a code the documentation does not name. */
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

SHIM(kernel32, BOOL, VirtualFree, LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)
{
    uint64_t addr = (uint64_t)(uintptr_t)lpAddress;
    int kind = region_kind(addr);

    if (kind == REGION_IMAGE_OR_VIEW)
        RT_UNIMPLEMENTED(ctx, "VirtualFree on an image or a file view: the failure is not documented");
    if (kind == REGION_HOST)
        RT_UNIMPLEMENTED(ctx, "VirtualFree on memory outside both arenas: the core cannot change it");

    return rt_vm_free(addr, dwSize, dwFreeType);
}
