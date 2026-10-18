/* shell32: the folder identifiers of SHGetFolderPath and SHGetSpecialFolderPath (shlobj.h;
 * checked against Wine's include/shlobj.h).  The folders exist in the guest's file tree as
 * docs/RUNTIME.md section 7.1 lays it out; the SE_ERR_ values are in rt/win/error.h. */
#ifndef RT_WIN_SHELL_H
#define RT_WIN_SHELL_H

#define CSIDL_DESKTOP             0x0000
#define CSIDL_PERSONAL            0x0005      /* the user's Documents */
#define CSIDL_MYDOCUMENTS         CSIDL_PERSONAL
#define CSIDL_APPDATA             0x001a      /* AppData\Roaming */
#define CSIDL_LOCAL_APPDATA       0x001c      /* AppData\Local */
#define CSIDL_COMMON_APPDATA      0x0023
#define CSIDL_WINDOWS             0x0024
#define CSIDL_SYSTEM              0x0025
#define CSIDL_PROFILE             0x0028      /* the user's profile directory */
#define CSIDL_FLAG_CREATE         0x8000
#define CSIDL_FLAG_MASK           0xff00

/* dwFlags of SHGetFolderPath. */
#define SHGFP_TYPE_CURRENT        0u
#define SHGFP_TYPE_DEFAULT        1u

#endif /* RT_WIN_SHELL_H */
