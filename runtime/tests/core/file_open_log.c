/* Two diagnostics of core/file.c that a driver uses (gate3/run.c): RT_FILE_OPEN_LOG=1, one
 * numbered info line per successful open that has a host path (unit gate3-run-12 added the
 * switch without a test; unit gate3-run-13, item 5), and rt_file_writers_under, the number of
 * open files with write access under a host directory.  The expected values are the two
 * comments that define them (file_publish in core/file.c; rt/file.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/test.h"
#include "rt/file.h"

void rt_file_test_open_log_reset(void);

#define SH (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)

static HANDLE open_w(const WCHAR *win, DWORD access, DWORD disp) {
    return rt_file_create(RT_NAME_W(win), access, SH, disp, 0, NULL);
}

TEST(core, file_open_log)
{
    char want[2400];

    T_CASE("without the switch an open writes no line");
    unsetenv("RT_FILE_OPEN_LOG");
    rt_file_test_open_log_reset();
    HANDLE a = open_w(u"C:\\Titanfall2\\one.bin", GENERIC_WRITE, CREATE_ALWAYS);
    T_REQUIRE(a != INVALID_HANDLE_VALUE);
    T_TRUE(!T_LOGGED(RT_CH_FILE, "open 1: "));
    T_EQ(rt_handle_close(a), TRUE);

    T_CASE("RT_FILE_OPEN_LOG=0 is off as well");
    setenv("RT_FILE_OPEN_LOG", "0", 1);
    rt_file_test_open_log_reset();
    a = open_w(u"C:\\Titanfall2\\one.bin", GENERIC_READ, OPEN_EXISTING);
    T_REQUIRE(a != INVALID_HANDLE_VALUE);
    T_TRUE(!T_LOGGED(RT_CH_FILE, "open 1: "));
    T_EQ(rt_handle_close(a), TRUE);

    T_CASE("RT_FILE_OPEN_LOG=1: one line per successful open, numbered from 1, with the host path and the access asked for");
    setenv("RT_FILE_OPEN_LOG", "1", 1);
    rt_file_test_open_log_reset();
    a = open_w(u"C:\\Titanfall2\\one.bin", GENERIC_READ, OPEN_EXISTING);
    T_REQUIRE(a != INVALID_HANDLE_VALUE);
    snprintf(want, sizeof want, "open 1: %s/one.bin (access 0x80000000)", rt_test_install_root());
    T_TRUE(T_LOGGED(RT_CH_FILE, want));
    HANDLE b = open_w(u"C:\\Users\\Player\\two.bin", GENERIC_READ | GENERIC_WRITE, CREATE_ALWAYS);
    T_REQUIRE(b != INVALID_HANDLE_VALUE);
    snprintf(want, sizeof want, "open 2: %s/home/two.bin (access 0xc0000000)", rt_test_state_dir());
    T_TRUE(T_LOGGED(RT_CH_FILE, want));

    T_CASE("an open that fails has no line and takes no number; the null device has no host path and no line");
    T_EQ(open_w(u"C:\\Titanfall2\\none.bin", GENERIC_READ, OPEN_EXISTING), INVALID_HANDLE_VALUE);
    HANDLE nul = open_w(u"NUL", GENERIC_WRITE, OPEN_EXISTING);
    T_REQUIRE(nul != INVALID_HANDLE_VALUE);
    HANDLE c = open_w(u"C:\\Titanfall2\\one.bin", GENERIC_READ, OPEN_EXISTING);
    T_REQUIRE(c != INVALID_HANDLE_VALUE);
    snprintf(want, sizeof want, "open 3: %s/one.bin (access 0x80000000)", rt_test_install_root());
    T_TRUE(T_LOGGED(RT_CH_FILE, want));
    T_TRUE(!T_LOGGED(RT_CH_FILE, "open 4: "));
    T_EQ(rt_handle_close(c), TRUE);
    T_EQ(rt_handle_close(nul), TRUE);
    T_EQ(rt_handle_close(a), TRUE);
    T_EQ(rt_handle_close(b), TRUE);
    unsetenv("RT_FILE_OPEN_LOG");
    rt_file_test_open_log_reset();
}

/* core/file.c: the test hook that takes the file lock (hold) or gives it back. */
void rt_file_test_hold_lock(bool hold);

TEST(core, file_writers_under_try)
{
    char first[2400];

    T_CASE("with the file lock free, the try-lock counts as rt_file_writers_under does");
    first[0] = 'x';
    T_EQ(rt_file_writers_under_try(rt_test_state_dir(), first, sizeof first), 0);
    T_EQ(first[0], 0);
    HANDLE w = open_w(u"C:\\Users\\Player\\try.bin", GENERIC_WRITE, CREATE_ALWAYS);
    T_REQUIRE(w != INVALID_HANDLE_VALUE);
    T_EQ(rt_file_writers_under_try(rt_test_state_dir(), first, sizeof first), 1);
    T_TRUE(strstr(first, "try.bin") != NULL);

    T_CASE("with the file lock held by another thread, the try-lock says unknown (-1) and counts nothing");
    rt_file_test_hold_lock(true);
    first[0] = 'x';
    T_EQ(rt_file_writers_under_try(rt_test_state_dir(), first, sizeof first), -1);
    T_EQ(first[0], 0);
    rt_file_test_hold_lock(false);
    T_EQ(rt_file_writers_under_try(rt_test_state_dir(), NULL, 0), 1);

    T_EQ(rt_handle_close(w), TRUE);
    T_EQ(rt_file_writers_under_try(rt_test_state_dir(), NULL, 0), 0);
}

TEST(core, file_writers_under)
{
    char first[2400], want[2400], home[2400];
    snprintf(home, sizeof home, "%s/home", rt_test_state_dir());

    T_CASE("nothing open for writing under the state directory: 0, and `first` is empty");
    first[0] = 'x';
    T_EQ(rt_file_writers_under(rt_test_state_dir(), first, sizeof first), 0);
    T_EQ(first[0], 0);
    T_EQ(rt_file_writers_under("/nonexistent/directory", NULL, 0), 0);

    T_CASE("a file open for reading is not counted; one open for writing is, with its path, until it is closed");
    HANDLE w = open_w(u"C:\\Users\\Player\\save.bin", GENERIC_WRITE, CREATE_ALWAYS);
    T_REQUIRE(w != INVALID_HANDLE_VALUE);
    T_EQ(rt_handle_close(w), TRUE);
    HANDLE r = open_w(u"C:\\Users\\Player\\save.bin", GENERIC_READ, OPEN_EXISTING);
    T_REQUIRE(r != INVALID_HANDLE_VALUE);
    T_EQ(rt_file_writers_under(rt_test_state_dir(), NULL, 0), 0);
    w = open_w(u"C:\\Users\\Player\\save.bin", GENERIC_READ | GENERIC_WRITE, OPEN_EXISTING);
    T_REQUIRE(w != INVALID_HANDLE_VALUE);
    T_EQ(rt_file_writers_under(rt_test_state_dir(), first, sizeof first), 1);
    snprintf(want, sizeof want, "%s/save.bin", home);
    T_TRUE(strcmp(first, want) == 0);
    T_EQ(rt_file_writers_under(home, NULL, 0), 1);

    T_CASE("a file open for writing somewhere else is not under the directory; a name that only begins like it is not either");
    HANDLE o = open_w(u"C:\\Titanfall2\\other.bin", GENERIC_WRITE, CREATE_ALWAYS);
    T_REQUIRE(o != INVALID_HANDLE_VALUE);
    T_EQ(rt_file_writers_under(rt_test_state_dir(), NULL, 0), 1);
    T_EQ(rt_file_writers_under(rt_test_install_root(), NULL, 0), 1);
    snprintf(want, sizeof want, "%s/hom", rt_test_state_dir());
    T_EQ(rt_file_writers_under(want, NULL, 0), 0);
    HANDLE nul = open_w(u"NUL", GENERIC_WRITE, OPEN_EXISTING);
    T_REQUIRE(nul != INVALID_HANDLE_VALUE);
    T_EQ(rt_file_writers_under("/", NULL, 0), 2);              /* the two files; not the null device */

    T_EQ(rt_handle_close(w), TRUE);
    T_EQ(rt_file_writers_under(rt_test_state_dir(), first, sizeof first), 0);
    T_EQ(first[0], 0);
    T_EQ(rt_handle_close(r), TRUE);
    T_EQ(rt_handle_close(o), TRUE);
    T_EQ(rt_handle_close(nul), TRUE);
}
