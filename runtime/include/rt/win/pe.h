/* The PE32+ image format and the loader's constants: what the loader reads from a DLL file
 * and what the LoadLibrary family is passed.  Layouts are those of winnt.h; every struct is
 * packed as on Windows and checked with _Static_assert.  Planner-owned.
 */
#ifndef RT_WIN_PE_H
#define RT_WIN_PE_H

#include <stdint.h>

/* ---- DllMain reasons ----------------------------------------------------------------------- */
#define DLL_PROCESS_DETACH  0
#define DLL_PROCESS_ATTACH  1
#define DLL_THREAD_ATTACH   2
#define DLL_THREAD_DETACH   3

/* ---- LoadLibraryEx flags ---------------------------------------------------------------- */
#define DONT_RESOLVE_DLL_REFERENCES          0x00000001u
#define LOAD_LIBRARY_AS_DATAFILE             0x00000002u
#define LOAD_WITH_ALTERED_SEARCH_PATH        0x00000008u
#define LOAD_IGNORE_CODE_AUTHZ_LEVEL         0x00000010u
#define LOAD_LIBRARY_AS_IMAGE_RESOURCE       0x00000020u
#define LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE   0x00000040u
#define LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR     0x00000100u
#define LOAD_LIBRARY_SEARCH_APPLICATION_DIR  0x00000200u
#define LOAD_LIBRARY_SEARCH_USER_DIRS        0x00000400u
#define LOAD_LIBRARY_SEARCH_SYSTEM32         0x00000800u
#define LOAD_LIBRARY_SEARCH_DEFAULT_DIRS     0x00001000u

/* ---- GetModuleHandleEx flags -------------------------------------------------------------- */
#define GET_MODULE_HANDLE_EX_FLAG_PIN                 0x00000001u
#define GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT  0x00000002u
#define GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS        0x00000004u

/* ---- the image ------------------------------------------------------------------------------ */
#define IMAGE_DOS_SIGNATURE            0x5A4Du         /* "MZ" at offset 0 */
#define IMAGE_DOS_LFANEW               0x3Cu           /* offset of the u32 that locates the NT headers */
#define IMAGE_NT_SIGNATURE             0x00004550u     /* "PE\0\0" */
#define IMAGE_FILE_MACHINE_AMD64       0x8664u
#define IMAGE_FILE_DLL                 0x2000u
#define IMAGE_NT_OPTIONAL_HDR64_MAGIC  0x020Bu
#define IMAGE_NUMBEROF_DIRECTORY_ENTRIES 16

#define IMAGE_DIRECTORY_ENTRY_EXPORT     0
#define IMAGE_DIRECTORY_ENTRY_IMPORT     1
#define IMAGE_DIRECTORY_ENTRY_EXCEPTION  3
#define IMAGE_DIRECTORY_ENTRY_BASERELOC  5
#define IMAGE_DIRECTORY_ENTRY_TLS        9
#define IMAGE_DIRECTORY_ENTRY_IAT        12
#define IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT 13

#define IMAGE_SCN_CNT_CODE               0x00000020u
#define IMAGE_SCN_CNT_INITIALIZED_DATA   0x00000040u
#define IMAGE_SCN_CNT_UNINITIALIZED_DATA 0x00000080u
#define IMAGE_SCN_MEM_DISCARDABLE        0x02000000u
#define IMAGE_SCN_MEM_EXECUTE            0x20000000u
#define IMAGE_SCN_MEM_READ               0x40000000u
#define IMAGE_SCN_MEM_WRITE              0x80000000u

#define IMAGE_REL_BASED_ABSOLUTE  0             /* padding: skipped */
#define IMAGE_REL_BASED_DIR64     10            /* add the delta to the u64 at the address */

#define IMAGE_ORDINAL_FLAG64      UINT64_C(0x8000000000000000)   /* import by ordinal: low 16 bits */

#pragma pack(push, 1)
typedef struct {
    uint16_t Machine, NumberOfSections;
    uint32_t TimeDateStamp, PointerToSymbolTable, NumberOfSymbols;
    uint16_t SizeOfOptionalHeader, Characteristics;
} IMAGE_FILE_HEADER;                                              /* 20 bytes, at e_lfanew + 4 */

typedef struct { uint32_t VirtualAddress, Size; } IMAGE_DATA_DIRECTORY;

typedef struct {
    uint16_t Magic; uint8_t MajorLinkerVersion, MinorLinkerVersion;
    uint32_t SizeOfCode, SizeOfInitializedData, SizeOfUninitializedData, AddressOfEntryPoint, BaseOfCode;
    uint64_t ImageBase;
    uint32_t SectionAlignment, FileAlignment;
    uint16_t MajorOperatingSystemVersion, MinorOperatingSystemVersion, MajorImageVersion, MinorImageVersion,
             MajorSubsystemVersion, MinorSubsystemVersion;
    uint32_t Win32VersionValue, SizeOfImage, SizeOfHeaders, CheckSum;
    uint16_t Subsystem, DllCharacteristics;
    uint64_t SizeOfStackReserve, SizeOfStackCommit, SizeOfHeapReserve, SizeOfHeapCommit;
    uint32_t LoaderFlags, NumberOfRvaAndSizes;
    IMAGE_DATA_DIRECTORY DataDirectory[IMAGE_NUMBEROF_DIRECTORY_ENTRIES];
} IMAGE_OPTIONAL_HEADER64;                                        /* 240 bytes, at e_lfanew + 24 */

typedef struct {
    uint8_t  Name[8];
    uint32_t VirtualSize, VirtualAddress, SizeOfRawData, PointerToRawData,
             PointerToRelocations, PointerToLinenumbers;
    uint16_t NumberOfRelocations, NumberOfLinenumbers;
    uint32_t Characteristics;
} IMAGE_SECTION_HEADER;                                           /* 40 bytes */

typedef struct {
    uint32_t Characteristics, TimeDateStamp; uint16_t MajorVersion, MinorVersion;
    uint32_t Name, Base, NumberOfFunctions, NumberOfNames,
             AddressOfFunctions, AddressOfNames, AddressOfNameOrdinals;      /* RVAs of u32[], u32[], u16[] */
} IMAGE_EXPORT_DIRECTORY;                                         /* 40 bytes */

typedef struct {
    uint32_t OriginalFirstThunk;      /* RVA of the import name table (u64 entries, 0-terminated) */
    uint32_t TimeDateStamp, ForwarderChain;
    uint32_t Name;                    /* RVA of the DLL name */
    uint32_t FirstThunk;              /* RVA of the import address table: the slots */
} IMAGE_IMPORT_DESCRIPTOR;            /* 20 bytes; the list ends at an all-zero descriptor */

typedef struct {
    uint64_t StartAddressOfRawData, EndAddressOfRawData;   /* virtual addresses (relocated) of the template */
    uint64_t AddressOfIndex;                               /* VA of the u32 that receives the module's index */
    uint64_t AddressOfCallBacks;                           /* VA of a 0-terminated array of callback VAs, or 0 */
    uint32_t SizeOfZeroFill, Characteristics;
} IMAGE_TLS_DIRECTORY64;                                   /* 40 bytes */

typedef struct { uint32_t VirtualAddress, SizeOfBlock; } IMAGE_BASE_RELOCATION;   /* then u16 entries: type << 12 | offset */
#pragma pack(pop)

_Static_assert(sizeof(IMAGE_FILE_HEADER) == 20 && sizeof(IMAGE_OPTIONAL_HEADER64) == 240, "PE headers");
_Static_assert(sizeof(IMAGE_SECTION_HEADER) == 40 && sizeof(IMAGE_EXPORT_DIRECTORY) == 40, "PE tables");
_Static_assert(sizeof(IMAGE_IMPORT_DESCRIPTOR) == 20 && sizeof(IMAGE_TLS_DIRECTORY64) == 40, "PE tables");

#endif /* RT_WIN_PE_H */
