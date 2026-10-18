/* The registry: the constants of winreg.h and winnt.h that the Reg* functions take
 * (docs/RUNTIME.md section 8.3).  Guest-visible.  The functions are in rt/registry.h, which
 * includes this file.
 */
#ifndef RT_WIN_REGISTRY_H
#define RT_WIN_REGISTRY_H

#include "rt/wintypes.h"
#include "rt/win/base.h"        /* the access rights */

#define HKEY_CLASSES_ROOT                ((HKEY)(intptr_t)(int32_t)0x80000000u)
#define HKEY_CURRENT_USER                ((HKEY)(intptr_t)(int32_t)0x80000001u)
#define HKEY_LOCAL_MACHINE               ((HKEY)(intptr_t)(int32_t)0x80000002u)
#define HKEY_USERS                       ((HKEY)(intptr_t)(int32_t)0x80000003u)
#define HKEY_PERFORMANCE_DATA            ((HKEY)(intptr_t)(int32_t)0x80000004u)
#define HKEY_CURRENT_CONFIG              ((HKEY)(intptr_t)(int32_t)0x80000005u)
#define HKEY_DYN_DATA                    ((HKEY)(intptr_t)(int32_t)0x80000006u)
#define HKEY_CURRENT_USER_LOCAL_SETTINGS ((HKEY)(intptr_t)(int32_t)0x80000007u)
#define HKEY_PERFORMANCE_TEXT            ((HKEY)(intptr_t)(int32_t)0x80000050u)
#define HKEY_PERFORMANCE_NLSTEXT         ((HKEY)(intptr_t)(int32_t)0x80000060u)

#define REG_NONE                         0u
#define REG_SZ                           1u
#define REG_EXPAND_SZ                    2u
#define REG_BINARY                       3u
#define REG_DWORD                        4u
#define REG_DWORD_LITTLE_ENDIAN          4u
#define REG_DWORD_BIG_ENDIAN             5u
#define REG_LINK                         6u
#define REG_MULTI_SZ                     7u
#define REG_RESOURCE_LIST                8u
#define REG_FULL_RESOURCE_DESCRIPTOR     9u
#define REG_RESOURCE_REQUIREMENTS_LIST   10u
#define REG_QWORD                        11u
#define REG_QWORD_LITTLE_ENDIAN          11u

#define KEY_QUERY_VALUE                  0x0001u
#define KEY_SET_VALUE                    0x0002u
#define KEY_CREATE_SUB_KEY               0x0004u
#define KEY_ENUMERATE_SUB_KEYS           0x0008u
#define KEY_NOTIFY                       0x0010u
#define KEY_CREATE_LINK                  0x0020u
#define KEY_WOW64_64KEY                  0x0100u
#define KEY_WOW64_32KEY                  0x0200u
#define KEY_WOW64_RES                    0x0300u
#define KEY_READ                         0x20019u    /* READ_CONTROL | QUERY_VALUE | ENUMERATE_SUB_KEYS | NOTIFY */
#define KEY_WRITE                        0x20006u    /* READ_CONTROL | SET_VALUE | CREATE_SUB_KEY */
#define KEY_EXECUTE                      0x20019u
#define KEY_ALL_ACCESS                   0xF003Fu

#define REG_OPTION_NON_VOLATILE          0x00000000u
#define REG_OPTION_VOLATILE              0x00000001u
#define REG_OPTION_CREATE_LINK           0x00000002u
#define REG_OPTION_BACKUP_RESTORE        0x00000004u
#define REG_OPTION_OPEN_LINK             0x00000008u
#define REG_CREATED_NEW_KEY              0x00000001u
#define REG_OPENED_EXISTING_KEY          0x00000002u

typedef ACCESS_MASK REGSAM;     /* the access-mask type of the registry functions */

#endif /* RT_WIN_REGISTRY_H */
