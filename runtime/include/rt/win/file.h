/* Guest-visible constants and structs of the file layer (fileapi.h, winbase.h, winnt.h,
 * wincon.h).  Rules as in rt/win/base.h.  Planner-owned.  The core that uses them: rt/file.h.
 */
#ifndef RT_WIN_FILE_H
#define RT_WIN_FILE_H

#include "rt/wintypes.h"
#include "rt/win/base.h"

/* ---- CreateFile -------------------------------------------------------------------------- */
/* dwDesiredAccess: the generic rights are in rt/win/base.h; these are the file-specific ones. */
#define FILE_READ_DATA            0x00000001u
#define FILE_WRITE_DATA           0x00000002u
#define FILE_APPEND_DATA          0x00000004u
#define FILE_READ_EA              0x00000008u
#define FILE_WRITE_EA             0x00000010u
#define FILE_EXECUTE              0x00000020u
#define FILE_READ_ATTRIBUTES      0x00000080u
#define FILE_WRITE_ATTRIBUTES     0x00000100u
#define FILE_GENERIC_READ         0x00120089u
#define FILE_GENERIC_WRITE        0x00120116u
#define FILE_GENERIC_EXECUTE      0x001200A0u
#define FILE_ALL_ACCESS           0x001F01FFu

/* dwShareMode */
#define FILE_SHARE_READ           0x00000001u
#define FILE_SHARE_WRITE          0x00000002u
#define FILE_SHARE_DELETE         0x00000004u

/* dwCreationDisposition */
#define CREATE_NEW                1u
#define CREATE_ALWAYS             2u
#define OPEN_EXISTING             3u
#define OPEN_ALWAYS               4u
#define TRUNCATE_EXISTING         5u

/* dwFlagsAndAttributes: attributes (also what GetFileAttributes returns) ... */
#define FILE_ATTRIBUTE_READONLY   0x00000001u
#define FILE_ATTRIBUTE_HIDDEN     0x00000002u
#define FILE_ATTRIBUTE_SYSTEM     0x00000004u
#define FILE_ATTRIBUTE_DIRECTORY  0x00000010u
#define FILE_ATTRIBUTE_ARCHIVE    0x00000020u
#define FILE_ATTRIBUTE_DEVICE     0x00000040u
#define FILE_ATTRIBUTE_NORMAL     0x00000080u
#define FILE_ATTRIBUTE_TEMPORARY  0x00000100u
#define FILE_ATTRIBUTE_NOT_CONTENT_INDEXED 0x00002000u
#define INVALID_FILE_ATTRIBUTES   0xFFFFFFFFu
/* ... and flags */
#define FILE_FLAG_WRITE_THROUGH       0x80000000u
#define FILE_FLAG_OVERLAPPED          0x40000000u
#define FILE_FLAG_NO_BUFFERING        0x20000000u
#define FILE_FLAG_RANDOM_ACCESS       0x10000000u
#define FILE_FLAG_SEQUENTIAL_SCAN     0x08000000u
#define FILE_FLAG_DELETE_ON_CLOSE     0x04000000u
#define FILE_FLAG_BACKUP_SEMANTICS    0x02000000u
#define FILE_FLAG_POSIX_SEMANTICS     0x01000000u
#define FILE_FLAG_SESSION_AWARE       0x00800000u
#define FILE_FLAG_OPEN_REPARSE_POINT  0x00200000u
#define FILE_FLAG_OPEN_NO_RECALL      0x00100000u
#define FILE_FLAG_FIRST_PIPE_INSTANCE 0x00080000u

/* ---- handles, positions, sizes ----------------------------------------------------------- */
#define FILE_TYPE_UNKNOWN         0u
#define FILE_TYPE_DISK            1u
#define FILE_TYPE_CHAR            2u
#define FILE_TYPE_PIPE            3u

#define STD_INPUT_HANDLE          ((DWORD)-10)
#define STD_OUTPUT_HANDLE         ((DWORD)-11)
#define STD_ERROR_HANDLE          ((DWORD)-12)

#define FILE_BEGIN                0u
#define FILE_CURRENT              1u
#define FILE_END                  2u
#define INVALID_SET_FILE_POINTER  0xFFFFFFFFu
#define INVALID_FILE_SIZE         0xFFFFFFFFu

/* What OVERLAPPED.Internal holds: an NTSTATUS (HasOverlappedIoCompleted tests STATUS_PENDING). */
#define STATUS_SUCCESS            0x00000000u
#define STATUS_PENDING            0x00000103u
#define STATUS_END_OF_FILE        0xC0000011u

/* ---- names -------------------------------------------------------------------------------- */
#define MOVEFILE_REPLACE_EXISTING   0x00000001u
#define MOVEFILE_COPY_ALLOWED       0x00000002u
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004u
#define MOVEFILE_WRITE_THROUGH      0x00000008u

#define DRIVE_UNKNOWN             0u
#define DRIVE_NO_ROOT_DIR         1u
#define DRIVE_REMOVABLE           2u
#define DRIVE_FIXED               3u
#define DRIVE_REMOTE              4u
#define DRIVE_CDROM               5u
#define DRIVE_RAMDISK             6u

/* ---- file mappings ------------------------------------------------------------------------ */
/* flProtect of CreateFileMapping: a PAGE_* value (rt/win/memory.h), optionally with: */
#define SEC_IMAGE                 0x01000000u
#define SEC_RESERVE               0x04000000u
#define SEC_COMMIT                0x08000000u
#define SEC_NOCACHE               0x10000000u
#define SEC_LARGE_PAGES           0x80000000u
/* dwDesiredAccess of MapViewOfFile and OpenFileMapping */
#define FILE_MAP_COPY             0x00000001u
#define FILE_MAP_WRITE            0x00000002u
#define FILE_MAP_READ             0x00000004u
#define FILE_MAP_EXECUTE          0x00000020u
#define FILE_MAP_ALL_ACCESS       0x000F001Fu

/* ---- console ------------------------------------------------------------------------------ */
#define CTRL_C_EVENT              0u
#define CTRL_BREAK_EVENT          1u
#define CTRL_CLOSE_EVENT          2u

/* ---- structs ------------------------------------------------------------------------------ */
typedef INT GET_FILEEX_INFO_LEVELS;      /* an enum on Windows: 4 bytes */
#define GetFileExInfoStandard     0
typedef INT FINDEX_INFO_LEVELS;
#define FindExInfoStandard        0
#define FindExInfoBasic           1
typedef INT FINDEX_SEARCH_OPS;
#define FindExSearchNameMatch          0
#define FindExSearchLimitToDirectories 1
/* dwAdditionalFlags of FindFirstFileEx (fileapi.h; from memory of the header: 1, 2, 4). */
#define FIND_FIRST_EX_CASE_SENSITIVE        0x00000001u
#define FIND_FIRST_EX_LARGE_FETCH           0x00000002u
#define FIND_FIRST_EX_ON_DISK_ENTRIES_ONLY  0x00000004u

typedef struct {                                                    /* 36 bytes */
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    nFileSizeHigh, nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA;

typedef struct {                                                    /* 52 bytes */
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    dwVolumeSerialNumber;
    DWORD    nFileSizeHigh, nFileSizeLow;
    DWORD    nNumberOfLinks;
    DWORD    nFileIndexHigh, nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;

typedef struct {                                                    /* 592 bytes */
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    nFileSizeHigh, nFileSizeLow;
    DWORD    dwReserved0, dwReserved1;
    WCHAR    cFileName[MAX_PATH];
    WCHAR    cAlternateFileName[14];
} WIN32_FIND_DATAW;

typedef struct {                                                    /* 320 bytes */
    DWORD    dwFileAttributes;
    FILETIME ftCreationTime, ftLastAccessTime, ftLastWriteTime;
    DWORD    nFileSizeHigh, nFileSizeLow;
    DWORD    dwReserved0, dwReserved1;
    CHAR     cFileName[MAX_PATH];
    CHAR     cAlternateFileName[14];
} WIN32_FIND_DATAA;

typedef struct { SHORT Left, Top, Right, Bottom; } SMALL_RECT;      /* 8 bytes */
typedef struct {                                                    /* 22 bytes */
    COORD      dwSize, dwCursorPosition;
    WORD       wAttributes;
    SMALL_RECT srWindow;
    COORD      dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO;

typedef WIN32_FILE_ATTRIBUTE_DATA  *LPWIN32_FILE_ATTRIBUTE_DATA;
typedef BY_HANDLE_FILE_INFORMATION *PBY_HANDLE_FILE_INFORMATION, *LPBY_HANDLE_FILE_INFORMATION;
typedef WIN32_FIND_DATAW           *PWIN32_FIND_DATAW, *LPWIN32_FIND_DATAW;
typedef WIN32_FIND_DATAA           *PWIN32_FIND_DATAA, *LPWIN32_FIND_DATAA;
typedef CONSOLE_SCREEN_BUFFER_INFO *PCONSOLE_SCREEN_BUFFER_INFO;

_Static_assert(sizeof(WIN32_FILE_ATTRIBUTE_DATA) == 36 && offsetof(WIN32_FILE_ATTRIBUTE_DATA, nFileSizeHigh) == 28,
               "WIN32_FILE_ATTRIBUTE_DATA");
_Static_assert(sizeof(BY_HANDLE_FILE_INFORMATION) == 52 && offsetof(BY_HANDLE_FILE_INFORMATION, dwVolumeSerialNumber) == 28 &&
               offsetof(BY_HANDLE_FILE_INFORMATION, nNumberOfLinks) == 40 &&
               offsetof(BY_HANDLE_FILE_INFORMATION, nFileIndexHigh) == 44, "BY_HANDLE_FILE_INFORMATION");
_Static_assert(sizeof(WIN32_FIND_DATAW) == 592 && offsetof(WIN32_FIND_DATAW, cFileName) == 44 &&
               offsetof(WIN32_FIND_DATAW, cAlternateFileName) == 564, "WIN32_FIND_DATAW");
_Static_assert(sizeof(WIN32_FIND_DATAA) == 320 && offsetof(WIN32_FIND_DATAA, cFileName) == 44 &&
               offsetof(WIN32_FIND_DATAA, cAlternateFileName) == 304, "WIN32_FIND_DATAA");
_Static_assert(sizeof(SMALL_RECT) == 8, "SMALL_RECT");
_Static_assert(sizeof(CONSOLE_SCREEN_BUFFER_INFO) == 22 && offsetof(CONSOLE_SCREEN_BUFFER_INFO, srWindow) == 10,
               "CONSOLE_SCREEN_BUFFER_INFO");

#endif /* RT_WIN_FILE_H */
