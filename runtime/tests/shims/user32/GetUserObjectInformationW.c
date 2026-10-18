#include "rt/test.h"
#include "rt/window.h"

/* True if the n bytes at p are all v. */
static int bytes_are(const unsigned char *p, int n, unsigned char v)
{
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != v) return 0;
    return 1;
}

TEST(user32, GetUserObjectInformationW)
{
    unsigned char buf[16];
    USEROBJECTFLAGS f;
    DWORD need;

    T_CASE("UOI_FLAGS of the window station (last error left alone)");
    memset(buf, 0xA5, sizeof buf);
    need = 0;
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, GetUserObjectInformationW, RT_HWINSTA, UOI_FLAGS, buf, 16, &need), TRUE);
    memcpy(&f, buf, sizeof f);
    T_EQ(f.fInherit, FALSE);
    T_EQ(f.fReserved, FALSE);
    T_EQ(f.dwFlags, WSF_VISIBLE);
    T_EQ(need, 12);
    T_TRUE(bytes_are(buf + 12, 4, 0xA5));
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("a length below 12 or a NULL buffer fails with ERROR_INSUFFICIENT_BUFFER and stores 12");
    memset(buf, 0xA5, sizeof buf);
    need = 0;
    T_EQ(CALL(user32, GetUserObjectInformationW, RT_HWINSTA, UOI_FLAGS, buf, 8, &need), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(need, 12);
    T_TRUE(bytes_are(buf, 16, 0xA5));           /* nothing is written */
    need = 0;
    T_EQ(CALL(user32, GetUserObjectInformationW, RT_HWINSTA, UOI_FLAGS, NULL, 16, &need), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    T_EQ(need, 12);

    T_CASE("a NULL length pointer is allowed");
    memset(buf, 0xA5, sizeof buf);
    T_EQ(CALL(user32, GetUserObjectInformationW, RT_HWINSTA, UOI_FLAGS, buf, 16, NULL), TRUE);
    memcpy(&f, buf, sizeof f);
    T_EQ(f.dwFlags, WSF_VISIBLE);
    T_TRUE(bytes_are(buf + 12, 4, 0xA5));
    T_EQ(CALL(user32, GetUserObjectInformationW, RT_HWINSTA, UOI_FLAGS, buf, 8, NULL), FALSE);
    T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);

    /* RT-TRAPS: an index other than UOI_FLAGS (2 is UOI_NAME) and a handle other than the window
     * station are never guessed: the run stops with the value. */
    T_TRAPS(CALL(user32, GetUserObjectInformationW, RT_HWINSTA, 2, buf, 16, &need), "an index other than UOI_FLAGS");
    T_TRAPS(CALL(user32, GetUserObjectInformationW, (HANDLE)(uintptr_t)0x1234, UOI_FLAGS, buf, 16, &need), "a handle other than the window station");
}
