/* Win32 error codes, wait results and the few HRESULT / multimedia codes the runtime uses.
 * Values are those of winerror.h, winbase.h and mmsystem.h.  Planner-owned: a shim that
 * needs a code that is not here stops and reports (docs/RUNTIME.md section 10.1).
 */
#ifndef RT_WIN_ERROR_H
#define RT_WIN_ERROR_H

#define ERROR_SUCCESS                    0
#define NO_ERROR                         0
#define ERROR_INVALID_FUNCTION           1
#define ERROR_FILE_NOT_FOUND             2
#define ERROR_PATH_NOT_FOUND             3
#define ERROR_TOO_MANY_OPEN_FILES        4
#define ERROR_ACCESS_DENIED              5
#define ERROR_INVALID_HANDLE             6
#define ERROR_NOT_ENOUGH_MEMORY          8
#define ERROR_BAD_FORMAT                 11
#define ERROR_INVALID_ACCESS             12
#define ERROR_INVALID_DATA               13
#define ERROR_OUTOFMEMORY                14
#define ERROR_INVALID_DRIVE              15
#define ERROR_CURRENT_DIRECTORY          16
#define ERROR_NOT_SAME_DEVICE            17
#define ERROR_NO_MORE_FILES              18
#define ERROR_WRITE_PROTECT              19
#define ERROR_NOT_READY                  21
#define ERROR_BAD_LENGTH                 24
#define ERROR_SEEK                       25
#define ERROR_WRITE_FAULT                29
#define ERROR_READ_FAULT                 30
#define ERROR_GEN_FAILURE                31
#define ERROR_SHARING_VIOLATION          32
#define ERROR_LOCK_VIOLATION             33
#define ERROR_HANDLE_EOF                 38
#define ERROR_HANDLE_DISK_FULL           39
#define ERROR_BAD_NETPATH                53
#define ERROR_NOT_SUPPORTED              50
#define ERROR_DEV_NOT_EXIST              55
#define ERROR_FILE_EXISTS                80
#define ERROR_CANNOT_MAKE                82
#define ERROR_INVALID_PARAMETER          87
#define ERROR_BROKEN_PIPE                109
#define ERROR_OPEN_FAILED                110
#define ERROR_BUFFER_OVERFLOW            111
#define ERROR_DISK_FULL                  112
#define ERROR_CALL_NOT_IMPLEMENTED       120
#define ERROR_SEM_TIMEOUT                121
#define ERROR_INSUFFICIENT_BUFFER        122
#define ERROR_INVALID_NAME               123
#define ERROR_MOD_NOT_FOUND              126
#define ERROR_PROC_NOT_FOUND             127
#define ERROR_NEGATIVE_SEEK              131
#define ERROR_DIR_NOT_EMPTY              145
#define ERROR_NOT_LOCKED                 158
#define ERROR_BAD_PATHNAME               161
#define ERROR_BUSY                       170
#define ERROR_ALREADY_EXISTS             183
#define ERROR_BAD_EXE_FORMAT             193
#define ERROR_ENVVAR_NOT_FOUND           203
#define ERROR_FILENAME_EXCED_RANGE       206
#define ERROR_BAD_PIPE                   230
#define ERROR_PIPE_BUSY                  231
#define ERROR_NO_DATA                    232
#define ERROR_MORE_DATA                  234
#define ERROR_NO_MORE_ITEMS              259
#define ERROR_DIRECTORY                  267
#define ERROR_NOT_OWNER                  288
#define ERROR_TOO_MANY_POSTS             298
#define ERROR_PARTIAL_COPY               299
#define ERROR_INVALID_ADDRESS            487
#define ERROR_ARITHMETIC_OVERFLOW        534
#define ERROR_OPERATION_ABORTED          995
#define ERROR_IO_INCOMPLETE              996
#define ERROR_IO_PENDING                 997
#define ERROR_NOACCESS                   998
#define ERROR_INVALID_FLAGS              1004
#define ERROR_FILE_INVALID               1006
#define ERROR_KEY_DELETED                1018
#define ERROR_CHILD_MUST_BE_VOLATILE     1021
#define ERROR_NO_UNICODE_TRANSLATION     1113
#define ERROR_DLL_INIT_FAILED            1114
#define ERROR_IO_DEVICE                  1117
#define ERROR_MAPPED_ALIGNMENT           1132
#define ERROR_OLD_WIN_VERSION            1150
#define ERROR_NOT_FOUND                  1168
#define ERROR_PRIVILEGE_NOT_HELD         1314
#define ERROR_COMMITMENT_LIMIT           1455
#define ERROR_TIMEOUT                    1460
#define ERROR_CANT_RESOLVE_FILENAME      1921

/* Results of the wait functions (section 4.2). */
#define WAIT_OBJECT_0                    0u
#define WAIT_ABANDONED                   0x80u
#define WAIT_ABANDONED_0                 WAIT_ABANDONED   /* the documentation's name for the base value */
#define WAIT_IO_COMPLETION               0xC0u
#define WAIT_TIMEOUT                     258u
#define WAIT_FAILED                      0xFFFFFFFFu
#define MAXIMUM_WAIT_OBJECTS             64u              /* most objects one wait call may name (winnt.h) */

/* HRESULTs. */
#define S_OK                             ((HRESULT)0)
#define S_FALSE                          ((HRESULT)1)
#define E_NOTIMPL                        ((HRESULT)0x80004001)
#define E_NOINTERFACE                    ((HRESULT)0x80004002)
#define E_POINTER                        ((HRESULT)0x80004003)
#define E_FAIL                           ((HRESULT)0x80004005)
#define E_OUTOFMEMORY                    ((HRESULT)0x8007000E)
#define E_INVALIDARG                     ((HRESULT)0x80070057)
#define REGDB_E_CLASSNOTREG              ((HRESULT)0x80040154)

/* Everything from here to the multimedia timer results was added for the rows of unit
 * rt-shim-rows-2; each value was checked against Wine's public headers (include/winerror.h,
 * ntstatus.h, shellapi.h, mmsystem.h) on 2026-10-10. */
#define RPC_E_CHANGED_MODE               ((HRESULT)0x80010106)
#define CO_E_NOTINITIALIZED              ((HRESULT)0x800401F0)

/* RPC_STATUS values of rpcrt4 (winerror.h): system error codes. */
#define RPC_S_OK                         0
#define RPC_S_OUT_OF_MEMORY              ERROR_OUTOFMEMORY
#define RPC_S_INVALID_STRING_UUID        1705
#define RPC_S_UUID_LOCAL_ONLY            1824

/* avrt (winerror.h). */
#define ERROR_INVALID_TASK_INDEX         1551

/* NTSTATUS values the bcrypt functions return (ntstatus.h). */
#define STATUS_INVALID_HANDLE            ((NTSTATUS)0xC0000008)
#define STATUS_INVALID_PARAMETER         ((NTSTATUS)0xC000000D)
#define STATUS_NOT_FOUND                 ((NTSTATUS)0xC0000225)
#define STATUS_BUFFER_TOO_SMALL          ((NTSTATUS)0xC0000023)   /* values: Wine's public include/ntstatus.h */
#define STATUS_NOT_SUPPORTED             ((NTSTATUS)0xC00000BB)

/* ShellExecute's failure values, 32 and below (shellapi.h). */
#define SE_ERR_FNF                       2
#define SE_ERR_PNF                       3
#define SE_ERR_ACCESSDENIED              5
#define SE_ERR_OOM                       8
#define SE_ERR_NOASSOC                   31

/* MMRESULT values of the wave and mixer functions (mmsystem.h). */
#define MMSYSERR_NOERROR                 0u
#define MMSYSERR_BADDEVICEID             2u
#define MMSYSERR_INVALHANDLE             5u
#define MMSYSERR_NODRIVER                6u
#define MMSYSERR_INVALFLAG               10u
#define MMSYSERR_INVALPARAM              11u

/* Multimedia timer results (mmsystem.h). */
#define TIMERR_NOERROR                   0u
#define TIMERR_NOCANDO                   97u

#endif /* RT_WIN_ERROR_H */
