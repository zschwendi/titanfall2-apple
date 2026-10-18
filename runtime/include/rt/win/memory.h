/* Virtual-memory constants and MEMORY_BASIC_INFORMATION (winnt.h), for rt/vm.h and the
 * VirtualAlloc family.  docs/RUNTIME.md section 5.2.  Planner-owned.
 */
#ifndef RT_WIN_MEMORY_H
#define RT_WIN_MEMORY_H

#include "rt/wintypes.h"

/* Allocation type (VirtualAlloc) and free type (VirtualFree). */
#define MEM_COMMIT        0x00001000u
#define MEM_RESERVE       0x00002000u
#define MEM_DECOMMIT      0x00004000u
#define MEM_RELEASE       0x00008000u
#define MEM_RESET         0x00080000u
#define MEM_TOP_DOWN      0x00100000u
#define MEM_WRITE_WATCH   0x00200000u
#define MEM_PHYSICAL      0x00400000u
#define MEM_LARGE_PAGES   0x20000000u

/* State and type, as VirtualQuery reports them. */
#define MEM_FREE          0x00010000u
#define MEM_PRIVATE       0x00020000u
#define MEM_MAPPED        0x00040000u
#define MEM_IMAGE         0x01000000u

/* Page protection. */
#define PAGE_NOACCESS           0x001u
#define PAGE_READONLY           0x002u
#define PAGE_READWRITE          0x004u
#define PAGE_WRITECOPY          0x008u
#define PAGE_EXECUTE            0x010u
#define PAGE_EXECUTE_READ       0x020u
#define PAGE_EXECUTE_READWRITE  0x040u
#define PAGE_EXECUTE_WRITECOPY  0x080u
#define PAGE_GUARD              0x100u
#define PAGE_NOCACHE            0x200u
#define PAGE_WRITECOMBINE       0x400u

/* What the guest is told (section 5.1), whatever the host page size is. */
#define RT_GUEST_PAGE           0x1000u         /* GetSystemInfo dwPageSize */
#define RT_GUEST_GRANULARITY    0x10000u        /* dwAllocationGranularity */

/* Flags of the Heap* functions (docs/RUNTIME.md section 5.4). */
#define HEAP_NO_SERIALIZE           0x00000001u   /* ignored: zones are always thread-safe */
#define HEAP_GENERATE_EXCEPTIONS    0x00000004u   /* fatal on failure instead of NULL (exceptions are plan A) */
#define HEAP_ZERO_MEMORY            0x00000008u
#define HEAP_REALLOC_IN_PLACE_ONLY  0x00000010u
#define HEAP_CREATE_ENABLE_EXECUTE  0x00040000u   /* HeapCreate only */

typedef struct {
    PVOID  BaseAddress;
    PVOID  AllocationBase;
    DWORD  AllocationProtect;
    WORD   PartitionId;
    WORD   Reserved_;
    SIZE_T RegionSize;
    DWORD  State;
    DWORD  Protect;
    DWORD  Type;
    DWORD  Reserved2_;
} MEMORY_BASIC_INFORMATION;
typedef MEMORY_BASIC_INFORMATION *PMEMORY_BASIC_INFORMATION;

_Static_assert(sizeof(MEMORY_BASIC_INFORMATION) == 48, "MEMORY_BASIC_INFORMATION");
_Static_assert(offsetof(MEMORY_BASIC_INFORMATION, AllocationBase) == 8 &&
               offsetof(MEMORY_BASIC_INFORMATION, AllocationProtect) == 16 &&
               offsetof(MEMORY_BASIC_INFORMATION, RegionSize) == 24 &&
               offsetof(MEMORY_BASIC_INFORMATION, State) == 32 &&
               offsetof(MEMORY_BASIC_INFORMATION, Protect) == 36 &&
               offsetof(MEMORY_BASIC_INFORMATION, Type) == 40, "MEMORY_BASIC_INFORMATION offsets");

/* ---- LocalAlloc and GlobalAlloc flags (winbase.h) ------------------------------------------- */
/* The values are on the documentation pages of LocalAlloc and GlobalAlloc; the G names were
 * also checked against Wine's include/winbase.h. */
#define LMEM_FIXED                0x0000u
#define LMEM_MOVEABLE             0x0002u
#define LMEM_ZEROINIT             0x0040u
#define LPTR                      (LMEM_FIXED | LMEM_ZEROINIT)
#define LHND                      (LMEM_MOVEABLE | LMEM_ZEROINIT)
#define GMEM_FIXED                0x0000u
#define GMEM_MOVEABLE             0x0002u
#define GMEM_ZEROINIT             0x0040u
#define GPTR                      (GMEM_FIXED | GMEM_ZEROINIT)
#define GHND                      (GMEM_MOVEABLE | GMEM_ZEROINIT)

/* HeapInformationClass of HeapSetInformation and HeapQueryInformation (winnt.h; the three
 * values are on the documentation page of HeapSetInformation). */
typedef INT HEAP_INFORMATION_CLASS;
#define HeapCompatibilityInformation       0
#define HeapEnableTerminationOnCorruption  1
#define HeapOptimizeResources              3

#endif /* RT_WIN_MEMORY_H */
