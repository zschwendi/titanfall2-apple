/* Game data that is only partly on this machine (core/file_partial.c, rt/file.h "game data").
 * Made-up files and maps: the map directory is <install root>.partial, beside the harness's
 * install root; a partial file's bytes are in its files/ and its ranges in its ranges/, both
 * under the path in lower case, and nothing of it is in the install tree.
 *
 * Where the expected values come from: the rule itself (the brief of unit rt-game-data: the
 * true size is seen, a read wholly inside present ranges is served, a read that touches an
 * absent byte ends the run with the path, the offset, the length and the fetch command, and
 * never gives zeros or a short read), and for the bytes served, the bytes this test wrote.
 * What Windows answers for a read past the end of a file is rt/file.h's (rt_file_read step 8
 * and OVERLAPPED), tested in file_io.c and file_ov.c.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "rt/test.h"
#include "rt/file.h"
#include "rt/log.h"

int rt_file_test_fd(HANDLE h);

#define RD   GENERIC_READ
#define WR   GENERIC_WRITE
#define SH   (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
#define BAD  INVALID_HANDLE_VALUE
#define CMD  "fetch: tools/gamedata/fetch.py "

static const char *I(const char *rel) {
    static char buf[4][2048];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "%s/%s", rt_test_install_root(), rel);
    return b;
}
/* A path in the map directory. */
static const char *M(const char *rel) {
    static char buf[4][2048];
    static unsigned k;
    char *b = buf[k++ & 3];
    snprintf(b, sizeof buf[0], "%s.partial/%s", rt_test_install_root(), rel);
    return b;
}
static void put_text(const char *host, const char *text) {
    int fd = open(host, O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || write(fd, text, strlen(text)) != (ssize_t)strlen(text)) rt_test_fail(__FILE__, __LINE__, "put %s: %s", host, strerror(errno));
    if (fd >= 0) close(fd);
}
static void dirs(void) {
    mkdir(M(""), 0755);
    mkdir(M("ranges"), 0755);
    mkdir(M("files"), 0755);
}
/* The byte the install has at offset i of a made-up file. */
static uint8_t byte_at(uint64_t i) { return (uint8_t)(0x41 + i % 53); }

/* p.bin: 100 bytes in the install; [10,30), [50,60) and [90,100) are here, the rest is a hole
 * of the sparse file (which the host reads as zeros: the bytes that must never be served). */
static void make_p(void) {
    dirs();
    int fd = open(M("files/p.bin"), O_CREAT | O_TRUNC | O_WRONLY, 0644);
    if (fd < 0 || ftruncate(fd, 100)) rt_test_fail(__FILE__, __LINE__, "p.bin: %s", strerror(errno));
    static const int span[3][2] = { { 10, 30 }, { 50, 60 }, { 90, 100 } };
    for (int s = 0; s < 3; s++)
        for (int i = span[s][0]; i < span[s][1]; i++) {
            const uint8_t b = byte_at((uint64_t)i);
            if (pwrite(fd, &b, 1, i) != 1) rt_test_fail(__FILE__, __LINE__, "p.bin: pwrite");
        }
    close(fd);
    put_text(M("ranges/p.bin.ranges"), "gamedata-ranges 1\npath p.bin\nsize 100\n10 30\n50 60\n90 100\nend 3\n");
}
static bool is_install(const uint8_t *buf, uint64_t off, uint64_t n) {
    for (uint64_t i = 0; i < n; i++) if (buf[i] != byte_at(off + i)) return false;
    return true;
}
static HANDLE cfa(const char *win, DWORD access, DWORD disp, DWORD fa) {
    return rt_file_create(RT_NAME_A(win), access, SH, disp, fa, NULL);
}
static HANDLE open_p(DWORD fa) { return cfa("C:\\Titanfall2\\p.bin", RD, OPEN_EXISTING, fa); }
static void set_pos(HANDLE h, off_t to) { lseek(rt_file_test_fd(h), to, SEEK_SET); }
static off_t pos(HANDLE h) { return lseek(rt_file_test_fd(h), 0, SEEK_CUR); }
static off_t host_size(const char *host) { struct stat st; return stat(host, &st) == 0 ? st.st_size : -1; }
static OVERLAPPED at(uint64_t offset) {
    OVERLAPPED ov;
    memset(&ov, 0, sizeof ov);
    ov.Offset = (DWORD)offset;
    ov.OffsetHigh = (DWORD)(offset >> 32);
    return ov;
}
/* For T_TRAPS: a read of `len` bytes at the position `off`. */
static void read_at_pos(HANDLE h, off_t off, DWORD len) {
    uint8_t buf[128];
    DWORD got;
    set_pos(h, off);
    rt_file_read(h, buf, len, &got, NULL);
}
static void read_at_ov(HANDLE h, uint64_t off, DWORD len) {
    uint8_t buf[128];
    DWORD got;
    OVERLAPPED ov = at(off);
    rt_file_read(h, buf, len, &got, &ov);
}

GUEST_FN(routine) { GUEST_RET(ctx); }

TEST(core, file_partial_read)
{
    uint8_t buf[128];
    DWORD got;
    make_p();

    T_PLAN(1, "size, attributes and the listing are the true file's") {
        HANDLE h = open_p(0);
        T_REQUIRE(h != BAD);
        LONGLONG size = 0;
        T_EQ(rt_file_size(h, &size), TRUE);
        T_EQ(size, 100);
        WIN32_FILE_ATTRIBUTE_DATA ad;
        T_EQ(rt_file_get_attributes_ex(RT_NAME_A("C:\\Titanfall2\\p.bin"), 0, &ad), TRUE);
        T_EQ(ad.nFileSizeLow, 100);
        WIN32_FIND_DATAA fd;
        HANDLE f = rt_find_first_a("C:\\Titanfall2\\p*", &fd);
        T_REQUIRE(f != BAD);
        T_TRUE(strcmp(fd.cFileName, "p.bin") == 0);
        T_EQ(fd.nFileSizeLow, 100);
        T_EQ(rt_find_close(f), TRUE);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(rt_file_get_attributes(RT_NAME_A("p.BIN")), FILE_ATTRIBUTE_ARCHIVE);
        struct stat st;
        T_TRUE(lstat(I("p.bin"), &st) != 0);                /* nothing of it is in the install tree */
    }

    T_PLAN(2, "a read at the position, wholly inside a present range, is served and moves the position") {
        HANDLE h = open_p(0);
        T_REQUIRE(h != BAD);
        set_pos(h, 10);
        got = 77;
        T_EQ(rt_file_read(h, buf, 20, &got, NULL), TRUE);
        T_EQ(got, 20);
        T_TRUE(is_install(buf, 10, 20));
        T_EQ(pos(h), 30);
        set_pos(h, 52);
        T_EQ(rt_file_read(h, buf, 3, &got, NULL), TRUE);
        T_TRUE(got == 3 && is_install(buf, 52, 3));
        T_EQ(rt_file_read(h, buf, 5, &got, NULL), TRUE);    /* and goes on from there */
        T_TRUE(got == 5 && is_install(buf, 55, 5));
        T_EQ(pos(h), 60);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(3, "a read at the position that touches an absent byte ends the run: path, offset, length, command") {
        HANDLE h = open_p(0);
        T_REQUIRE(h != BAD);
        T_TRAPS(read_at_pos(h, 10, 21), "a read touches bytes that are not on this Mac");
        T_TRAPS(read_at_pos(h, 10, 21), "C:\\Titanfall2\\p.bin  (100 bytes in the install, 3 ranges of it here)");
        T_TRAPS(read_at_pos(h, 10, 21), "offset 0xa length 0x15  (rt_file_read)");
        T_TRAPS(read_at_pos(h, 10, 21), "absent  0x1e to 0x1f,");
        T_TRAPS(read_at_pos(h, 10, 21), "  caller  ");
        T_TRAPS(read_at_pos(h, 10, 21), "\n  " CMD "'p.bin' 0xa+0x15");
        T_TRAPS(read_at_pos(h, 0, 1), CMD "'p.bin' 0x0+0x1");                 /* before the first range */
        T_TRAPS(read_at_pos(h, 30, 5), CMD "'p.bin' 0x1e+0x5");               /* wholly in a hole */
        T_TRAPS(read_at_pos(h, 9, 2), "absent  0x9 to 0xa,");                 /* one byte early */
        T_TRAPS(read_at_pos(h, 25, 30), "absent  0x1e to 0x32,");             /* two ranges and the gap between */
        T_TRAPS(read_at_pos(h, 60, 30), "absent  0x3c to 0x5a,");             /* from the end of one to the next */
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(4, "the end of the file: what is asked beyond it needs no bytes") {
        HANDLE h = open_p(0);
        T_REQUIRE(h != BAD);
        set_pos(h, 90);
        got = 77;
        T_EQ(rt_file_read(h, buf, 50, &got, NULL), TRUE);   /* [90,140) is [90,100): present */
        T_EQ(got, 10);
        T_TRUE(is_install(buf, 90, 10));
        T_EQ(rt_file_read(h, buf, 10, &got, NULL), TRUE);   /* at the end: TRUE with 0 bytes */
        T_EQ(got, 0);
        T_TRAPS(read_at_pos(h, 80, 50), CMD "'p.bin' 0x50+0x14");            /* asked 0x32, 0x14 exist */
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(5, "a read with an OVERLAPPED: served inside a range, the stop outside") {
        HANDLE h = open_p(0);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(50);
        got = 77;
        T_EQ(rt_file_read(h, buf, 10, &got, &ov), TRUE);
        T_EQ(got, 10);
        T_TRUE(is_install(buf, 50, 10));
        T_EQ(ov.InternalHigh, 10);
        T_TRAPS(read_at_ov(h, 55, 10), "offset 0x37 length 0xa  (rt_file_read)");
        T_TRAPS(read_at_ov(h, 55, 10), CMD "'p.bin' 0x37+0xa");
        T_TRAPS(read_at_ov(h, 49, 2), "absent  0x31 to 0x32,");
        ov = at(95);
        T_EQ(rt_file_read(h, buf, 64, &got, &ov), TRUE);    /* to the end of the file */
        T_TRUE(got == 5 && is_install(buf, 95, 5));
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(6, "ReadFileEx on an overlapped handle: the same rule") {
        HANDLE h = open_p(FILE_FLAG_OVERLAPPED);
        T_REQUIRE(h != BAD);
        OVERLAPPED ov = at(12);
        memset(buf, 0, sizeof buf);
        T_EQ(rt_file_read_ex(T_CTX(), h, buf, 8, &ov, T_GUEST_ADDR(routine)), TRUE);
        T_TRUE(is_install(buf, 12, 8));
        T_EQ(ov.InternalHigh, 8);
        OVERLAPPED hole = at(64);
        T_TRAPS(rt_file_read_ex(T_CTX(), h, buf, 16, &hole, T_GUEST_ADDR(routine)), "offset 0x40 length 0x10  (rt_file_read_ex)");
        T_TRAPS(rt_file_read_ex(T_CTX(), h, buf, 16, &hole, T_GUEST_ADDR(routine)), CMD "'p.bin' 0x40+0x10");
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(7, "a file with no map entry is whole, a sparse one too") {
        int fd = open(I("w.bin"), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        T_REQUIRE(fd >= 0 && ftruncate(fd, 64) == 0 && pwrite(fd, "xy", 2, 62) == 2);
        close(fd);
        HANDLE h = cfa("C:\\Titanfall2\\w.bin", RD | WR, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        T_EQ(rt_file_read(h, buf, 64, &got, NULL), TRUE);
        T_TRUE(got == 64 && buf[0] == 0 && buf[62] == 'x' && buf[63] == 'y');
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(8, "a file with an empty map: every read is the stop, the size is still the true one") {
        /* Both files are named by the path in lower case; the path line keeps the install's
         * spelling.  The directory Sub is not in the install tree at first. */
        mkdir(M("ranges/sub"), 0755);
        mkdir(M("files/sub"), 0755);
        int fd = open(M("files/sub/e (1).bin"), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        T_REQUIRE(fd >= 0 && ftruncate(fd, 4096) == 0);
        close(fd);
        put_text(M("ranges/sub/e (1).bin.ranges"), "gamedata-ranges 1\npath Sub/E (1).bin\nsize 4096\nend 0\n");
        HANDLE h = cfa("C:\\Titanfall2\\sub\\e (1).BIN", RD, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        LONGLONG size = 0;
        T_EQ(rt_file_size(h, &size), TRUE);
        T_EQ(size, 4096);
        T_TRAPS(read_at_pos(h, 0, 16), "C:\\Titanfall2\\Sub\\E (1).bin  (4096 bytes in the install, 0 ranges of it here)");
        T_TRAPS(read_at_pos(h, 0, 16), CMD "'Sub/E (1).bin' 0x0+0x10");
        T_EQ(rt_handle_close(h), TRUE);
        /* A listing of its directory shows it under the install's spelling, among the files
         * that are there; one that is in the tree as well is shown once. */
        mkdir(I("Sub"), 0755);
        put_text(I("Sub/a.txt"), "a");
        WIN32_FIND_DATAA fd2;
        HANDLE f = rt_find_first_a("C:\\Titanfall2\\sub\\*", &fd2);
        T_REQUIRE(f != BAD);
        T_TRUE(strcmp(fd2.cFileName, ".") == 0);
        T_EQ(rt_find_next_a(f, &fd2), TRUE);
        T_TRUE(strcmp(fd2.cFileName, "..") == 0);
        T_EQ(rt_find_next_a(f, &fd2), TRUE);
        T_TRUE(strcmp(fd2.cFileName, "a.txt") == 0);
        T_EQ(rt_find_next_a(f, &fd2), TRUE);
        T_TRUE(strcmp(fd2.cFileName, "E (1).bin") == 0);
        T_EQ(fd2.nFileSizeLow, 4096);
        T_EQ(rt_find_next_a(f, &fd2), FALSE);
        T_EQ(rt_find_close(f), TRUE);
        put_text(I("Sub/E (1).bin"), "whole now");
        f = rt_find_first_a("C:\\Titanfall2\\sub\\e*", &fd2);
        T_REQUIRE(f != BAD);
        T_EQ(fd2.nFileSizeLow, 9);
        T_EQ(rt_find_next_a(f, &fd2), FALSE);
        T_EQ(rt_find_close(f), TRUE);
    }
}

TEST(core, file_partial_refused)
{
    make_p();

    T_PLAN(1, "an open for writing or truncation stops before anything is opened: the file keeps its size") {
        T_TRAPS(cfa("C:\\Titanfall2\\p.bin", RD | WR, OPEN_EXISTING, 0), "an open for writing or truncation of a file that is only partly on this Mac");
        T_TRAPS(cfa("C:\\Titanfall2\\p.bin", RD | WR, OPEN_EXISTING, 0), "\n  " CMD "'p.bin'");
        T_TRAPS(cfa("C:\\Titanfall2\\p.bin", RD, CREATE_ALWAYS, 0), "an open for writing or truncation");
        T_TRAPS(cfa("C:\\Titanfall2\\p.bin", WR, TRUNCATE_EXISTING, 0), "an open for writing or truncation");
        T_TRAPS(cfa("C:\\Titanfall2\\p.bin", WR, OPEN_ALWAYS, 0), "an open for writing or truncation");
        T_TRAPS(cfa("C:\\Titanfall2\\p.bin", RD | WR, CREATE_NEW, 0), "FATAL");      /* never a new file over it */
        T_EQ(host_size(M("files/p.bin")), 100);
        struct stat st;
        T_TRUE(lstat(I("p.bin"), &st) != 0);
    }

    T_PLAN(2, "a section, a delete, a move") {
        HANDLE h = open_p(0);
        T_REQUIRE(h != BAD);
        T_TRAPS(rt_section_create(h, PAGE_READONLY, 0, NULL), "a file mapping of a file that is only partly on this Mac");
        T_TRAPS(rt_section_create(h, PAGE_READONLY, 0, NULL), "\n  " CMD "'p.bin'");
        T_EQ(rt_handle_close(h), TRUE);
        T_TRAPS(rt_file_delete(RT_NAME_A("C:\\Titanfall2\\p.bin")), "a delete of a file that is only partly on this Mac");
        T_TRAPS(rt_file_move(RT_NAME_A("C:\\Titanfall2\\p.bin"), RT_NAME_A("C:\\Titanfall2\\q.bin"), 0), "a move of a file");
        put_text(I("q.bin"), "q");
        T_TRAPS(rt_file_move(RT_NAME_A("C:\\Titanfall2\\q.bin"), RT_NAME_A("C:\\Titanfall2\\p.bin"), MOVEFILE_REPLACE_EXISTING), "a move onto it of a file");
        T_EQ(host_size(M("files/p.bin")), 100);
    }
}

TEST(core, file_partial_bad_map)
{
    make_p();

    T_PLAN(1, "a map that is cut short, miscounts or disagrees with the file is fatal, never \"whole\"") {
        put_text(M("ranges/p.bin.ranges"), "gamedata-ranges 1\npath p.bin\nsize 100\n10 30\n");
        T_TRAPS(open_p(0), "is cut short or does not count its ranges");
        put_text(M("ranges/p.bin.ranges"), "gamedata-ranges 1\npath p.bin\nsize 100\n10 30\nend 2\n");
        T_TRAPS(open_p(0), "is cut short or does not count its ranges");
        put_text(M("ranges/p.bin.ranges"), "gamedata-ranges 1\npath p.bin\nsize 200\n10 30\nend 1\n");
        T_TRAPS(open_p(0), "says 200 bytes and the file");
        put_text(M("ranges/p.bin.ranges"), "gamedata-ranges 1\npath p.bin\nsize 100\n10 30\n30 40\nend 2\n");
        T_TRAPS(open_p(0), "line 5 is not a range inside the file after the one before");
        put_text(M("ranges/p.bin.ranges"), "gamedata-ranges 1\npath p.bin\nsize 100\n90 101\nend 1\n");
        T_TRAPS(open_p(0), "line 4 is not a range");
        put_text(M("ranges/p.bin.ranges"), "ranges\n");
        T_TRAPS(open_p(0), "does not begin with");
        put_text(M("ranges/p.bin.ranges"), "");
        T_TRAPS(open_p(0), "is cut short");
        chmod(M("ranges/p.bin.ranges"), 0);
        T_TRAPS(open_p(0), "cannot be opened");
        chmod(M("ranges/p.bin.ranges"), 0644);
        put_text(M("ranges/gone.bin.ranges"), "gamedata-ranges 1\npath gone.bin\nsize 100\nend 0\n");
        T_TRAPS(cfa("C:\\Titanfall2\\gone.bin", RD, OPEN_EXISTING, 0), "its bytes are not at");
    }

    T_PLAN(2, "without a manifest a miss is a miss, and one line says that it cannot be told apart") {
        T_EQ(cfa("C:\\Titanfall2\\nothing.txt", RD, OPEN_EXISTING, 0), BAD);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        T_EQ(rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\nodir\\nothing.txt")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        T_TRUE(T_LOGGED(RT_CH_FILE, "no manifest of the install"));
    }
}

static void find_a(const char *pattern) {
    WIN32_FIND_DATAA fd;
    rt_find_first_a(pattern, &fd);
}

TEST(core, file_partial_manifest)
{
    dirs();
    /* The install: vpk/enable.txt, vpk/Big.vpk, here.txt, Sub/Deep.TXT, lone/one.txt.  This
     * machine: the directory vpk (empty), here.txt, and the directory lone with its file. */
    put_text(M("manifest.tsv"), "gamedata-manifest 1\n"
             "d\t0\tvpk\nf\t12\tvpk/enable.txt\nf\t5000000000\tvpk/Big.vpk\nf\t5\there.txt\n"
             "d\t0\tSub\nf\t7\tSub/Deep.TXT\nd\t0\tlone\nf\t3\tlone/one.txt\nf\t64\tlone/Part.vpk\n");
    mkdir(I("vpk"), 0755);
    mkdir(I("lone"), 0755);
    put_text(I("here.txt"), "hello");
    put_text(I("lone/one.txt"), "one");
    mkdir(M("ranges/lone"), 0755);
    mkdir(M("files/lone"), 0755);
    put_text(M("ranges/lone/part.vpk.ranges"), "gamedata-ranges 1\npath lone/Part.vpk\nsize 64\n0 8\nend 1\n");
    {
        int fd = open(M("files/lone/part.vpk"), O_CREAT | O_TRUNC | O_WRONLY, 0644);
        if (fd < 0 || pwrite(fd, "PARTDATA", 8, 0) != 8 || ftruncate(fd, 64)) rt_test_fail(__FILE__, __LINE__, "part.vpk");
        close(fd);
    }

    T_PLAN(1, "a name the install has and this machine lacks is the stop, with the install's spelling") {
        T_TRAPS(cfa("C:\\Titanfall2\\VPK\\ENABLE.TXT", RD, OPEN_EXISTING, 0), "a file of the install is not on this Mac");
        T_TRAPS(cfa("C:\\Titanfall2\\VPK\\ENABLE.TXT", RD, OPEN_EXISTING, 0), "C:\\Titanfall2\\vpk\\enable.txt  (12 bytes in the install)");
        T_TRAPS(cfa("C:\\Titanfall2\\VPK\\ENABLE.TXT", RD, OPEN_EXISTING, 0), "\n  " CMD "--open 'vpk/enable.txt'");
        T_TRAPS(rt_file_get_attributes(RT_NAME_A("vpk/big.vpk")), CMD "--open 'vpk/Big.vpk'");      /* relative, other slash */
        T_TRAPS(rt_file_get_attributes(RT_NAME_W(u"c:\\titanfall2\\vpk\\..\\vpk\\big.vpk")), CMD "--open 'vpk/Big.vpk'");
        T_TRAPS(cfa("C:\\Titanfall2\\vpk\\enable.txt", RD | WR, OPEN_ALWAYS, 0), CMD "--open 'vpk/enable.txt'");
        T_TRAPS(cfa("C:\\Titanfall2\\vpk\\enable.txt", WR, CREATE_NEW, 0), CMD "--open 'vpk/enable.txt'");
    }

    T_PLAN(2, "a directory of the install that is not here") {
        T_TRAPS(cfa("C:\\Titanfall2\\sub\\deep.txt", RD, OPEN_EXISTING, 0), CMD "--open 'Sub/Deep.TXT'");
        T_TRAPS(cfa("C:\\Titanfall2\\sub\\other.txt", RD, OPEN_EXISTING, 0), "a directory of the install is not on this Mac");
        T_TRAPS(cfa("C:\\Titanfall2\\sub\\other.txt", RD, OPEN_EXISTING, 0), "\n  " CMD "--manifest");
        T_TRAPS(rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\sub")), CMD "--manifest");
        T_TRAPS(find_a("C:\\Titanfall2\\sub\\*"), CMD "--manifest");
    }

    T_PLAN(3, "a name the install does not have stays a miss") {
        T_EQ(cfa("C:\\Titanfall2\\zip0.zip", RD, OPEN_EXISTING, 0), BAD);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        T_EQ(rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\vpk\\client_014.vpk")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        T_EQ(rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\platform\\vpk\\enable.txt")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        /* A directory the install lacks, below one the install has and this machine has (lone)
         * or lacks (Sub): "path not found" is the install's own answer, so no stop.  The game
         * asks for platform\resource\game_x64.ico; the install has platform and no
         * platform\resource, and the recorded start has the lookup failing (files-start.md). */
        T_EQ(rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\lone\\nosuch\\x.ico")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        T_EQ(rt_find_first_a("C:\\Titanfall2\\lone\\nosuch\\x.ico", &(WIN32_FIND_DATAA){0}), BAD);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        T_EQ(rt_file_get_attributes(RT_NAME_A("C:\\Titanfall2\\sub\\nosuch\\x.ico")), INVALID_FILE_ATTRIBUTES);
        T_EQ(T_LAST_ERROR(), ERROR_PATH_NOT_FOUND);
        T_EQ(rt_file_get_attributes(RT_NAME_A("C:\\Users\\Player\\vpk\\enable.txt")), INVALID_FILE_ATTRIBUTES);   /* not the install */
        WIN32_FIND_DATAA fd;
        T_EQ(rt_find_first_a("C:\\Titanfall2\\vpk\\zip0.zip", &fd), BAD);
        T_EQ(T_LAST_ERROR(), ERROR_FILE_NOT_FOUND);
        HANDLE h = cfa("C:\\Titanfall2\\vpk\\made.here", RD | WR, CREATE_NEW, 0);    /* the game's own new file */
        T_TRUE(h != BAD);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(unlink(I("vpk/made.here")), 0);
    }

    T_PLAN(4, "a listing that would leave out entries of the install is the stop") {
        T_TRAPS(find_a("C:\\Titanfall2\\vpk\\enable.txt"), CMD "--open 'vpk/enable.txt'");       /* an existence check */
        T_TRAPS(find_a("C:\\Titanfall2\\vpk\\*.txt"), CMD "--open 'vpk/enable.txt'");
        T_TRAPS(find_a("C:\\Titanfall2\\vpk\\*"), "would leave out 2 entries of the install");
        T_TRAPS(find_a("C:\\Titanfall2\\vpk\\*"), "\n  " CMD "--open-matching 'vpk/*'");
        T_TRAPS(find_a("C:\\Titanfall2\\*"), CMD "--manifest");                                  /* the directory Sub */
        WIN32_FIND_DATAA fd;
        HANDLE f = rt_find_first_a("C:\\Titanfall2\\here*", &fd);                               /* all of it is here */
        T_REQUIRE(f != BAD);
        T_TRUE(strcmp(fd.cFileName, "here.txt") == 0);
        T_EQ(rt_find_close(f), TRUE);
        f = rt_find_first_a("C:\\Titanfall2\\lone\\*", &fd);      /* one.txt is here and Part.vpk partial: no stop */
        T_REQUIRE(f != BAD);
        T_EQ(rt_find_close(f), TRUE);
        f = rt_find_first_a("C:\\Titanfall2\\lone\\part.VPK", &fd);
        T_REQUIRE(f != BAD);
        T_TRUE(strcmp(fd.cFileName, "Part.vpk") == 0);
        T_EQ(fd.nFileSizeLow, 64);
        T_EQ(rt_find_close(f), TRUE);
        HANDLE h = cfa("C:\\Titanfall2\\lone\\part.vpk", RD, OPEN_EXISTING, 0);
        T_REQUIRE(h != BAD);
        char buf[16];
        DWORD got = 0;
        T_EQ(rt_file_read(h, buf, 8, &got, NULL), TRUE);
        T_TRUE(got == 8 && memcmp(buf, "PARTDATA", 8) == 0);
        T_EQ(rt_handle_close(h), TRUE);
    }

    T_PLAN(5, "CREATE_ALWAYS keeps nothing of the install's file: it may create") {
        HANDLE h = cfa("C:\\Titanfall2\\vpk\\enable.txt", RD | WR, CREATE_ALWAYS, 0);
        T_TRUE(h != BAD);
        T_EQ(rt_handle_close(h), TRUE);
        T_EQ(unlink(I("vpk/enable.txt")), 0);
    }
}
