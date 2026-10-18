#include "rt/test.h"
#include "rt/file.h"

/* NUL-terminated UTF-16 roots as the guest passes them.  The shim looks at none of them; the
 * core's answer is the oracle. */
static const WCHAR root_c_bs[] = { 'C', ':', '\\', 0 };
static const WCHAR root_lc_bs[] = { 'c', ':', '\\', 0 };
static const WCHAR root_c_fs[] = { 'C', ':', '/', 0 };
static const WCHAR root_d_bs[] = { 'D', ':', '\\', 0 };
static const WCHAR root_z_bs[] = { 'Z', ':', '\\', 0 };
static const WCHAR root_c[] = { 'C', ':', 0 };
static const WCHAR root_lc[] = { 'c', ':', 0 };
static const WCHAR root_d[] = { 'D', ':', 0 };
static const WCHAR root_letter_c[] = { 'C', 0 };
static const WCHAR root_letter_lc[] = { 'c', 0 };
static const WCHAR root_c_q[] = { 'C', '?', 0 };
static const WCHAR root_lc_q[] = { 'c', '?', 0 };
static const WCHAR root_q[] = { '?', 0 };
static const WCHAR root_empty[] = { 0 };
static const WCHAR root_c_tail[] = { 'C', ':', '\\', 'T', 'i', 't', 'a', 'n', 'f', 'a', 'l', 'l', '2', 0 };

TEST(kernel32, GetDriveTypeW)
{
    T_CASE("NULL root is DRIVE_FIXED");
    T_EQ(CALL(kernel32, GetDriveTypeW, NULL), DRIVE_FIXED);

    T_CASE("C root with a backslash is DRIVE_FIXED in either letter case");
    T_EQ(CALL(kernel32, GetDriveTypeW, root_c_bs), DRIVE_FIXED);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_lc_bs), DRIVE_FIXED);

    T_CASE("C root with a forward slash is DRIVE_FIXED");
    T_EQ(CALL(kernel32, GetDriveTypeW, root_c_fs), DRIVE_FIXED);

    T_CASE("D and Z roots with a backslash are DRIVE_NO_ROOT_DIR");
    T_EQ(CALL(kernel32, GetDriveTypeW, root_d_bs), DRIVE_NO_ROOT_DIR);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_z_bs), DRIVE_NO_ROOT_DIR);

    /* Windows answers "X:" as "X:\" does (review X9, Wine kernel32/tests/drive.c). */
    T_CASE("C: with no slash is DRIVE_FIXED and D: with no slash is DRIVE_NO_ROOT_DIR");
    T_EQ(CALL(kernel32, GetDriveTypeW, root_c), DRIVE_FIXED);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_lc), DRIVE_FIXED);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_d), DRIVE_NO_ROOT_DIR);

    /* Windows answers these texts DRIVE_NO_ROOT_DIR (review X9).  The last error is not
     * asserted for them. */
    T_CASE("C, c, C?, c?, ? and the empty root are DRIVE_NO_ROOT_DIR");
    T_EQ(CALL(kernel32, GetDriveTypeW, root_letter_c), DRIVE_NO_ROOT_DIR);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_letter_lc), DRIVE_NO_ROOT_DIR);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_c_q), DRIVE_NO_ROOT_DIR);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_lc_q), DRIVE_NO_ROOT_DIR);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_q), DRIVE_NO_ROOT_DIR);
    T_EQ(CALL(kernel32, GetDriveTypeW, root_empty), DRIVE_NO_ROOT_DIR);

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetDriveTypeW, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetDriveTypeW, root_c_bs);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetDriveTypeW, root_c_fs);
    T_EQ(T_LAST_ERROR(), 0x1234);
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, GetDriveTypeW, root_z_bs);
    T_EQ(T_LAST_ERROR(), 0x1234);

    /* The stop of the core: a root with a path after the slash.  Listed in RT-TRAPS. */
    T_TRAPS(CALL(kernel32, GetDriveTypeW, root_c_tail), "rt_drive_type: not implemented");
}
