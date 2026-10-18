#include "rt/test.h"

TEST(kernel32, OutputDebugStringA)
{
    unsigned n;

    T_CASE("the text is logged on the guest.debug channel");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, "hello debug world");
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "hello debug world"));
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    /* The text is data, never the format: a percent sign comes out as a percent sign. */
    CALL(kernel32, OutputDebugStringA, "100%s done %d%%");
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "100%s done %d%%"));

    T_CASE("one log line per line of text without the line ending");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, "first\r\nsecond\nthird");
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 3);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "first"));
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "second"));
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "third"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "firstsecond"));    /* three lines, not one */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "first\r"));        /* the line ending is not printed ... */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "first\\x0d"));     /* ... nor escaped when it is half of "\r\n" */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\n"));
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, "alpha\n");          /* a line ending at the end adds no empty line */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "alpha"));
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, "beta\r\n");
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "beta"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "beta\\x0d"));
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, "one\n\ntwo");      /* an empty line between two line endings is a line */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 3);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "one"));
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "two"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "onetwo"));

    T_CASE("bytes outside printable ASCII are escaped");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, "caf\xe9 \x01 \x7f\tx\ry");
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);            /* a lone \r is no line ending */
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "caf\\xe9 \\x01 \\x7f\tx\\x0dy"));   /* the tab stays a tab */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\xe9"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\x01"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\x7f"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\r"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\\x09"));
    CALL(kernel32, OutputDebugStringA, "hi\x80\xff!");
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "hi\\x80\\xff!"));
    CALL(kernel32, OutputDebugStringA, "bounds ~\x1f");     /* 0x7E and 0x20 are printable, 0x1F is not */
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "bounds ~\\x1f"));
    CALL(kernel32, OutputDebugStringA, "end\r");            /* a lone \r at the end is escaped, not dropped */
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "end\\x0d"));
    CALL(kernel32, OutputDebugStringA, "back\\slash \\x41");    /* a backslash is printed as is */
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "back\\slash \\x41"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "back\\x5c"));

    T_CASE("NULL and the empty string log nothing");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, NULL);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n);
    CALL(kernel32, OutputDebugStringA, "");
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n);

    T_CASE("a line longer than the buffer is continued on the next log line");
    char a1000[1001], a1001[1002], a2500[2501], esc[1000];
    memset(a1000, 'A', 1000); a1000[1000] = 0;
    memset(a1001, 'A', 1001); a1001[1001] = 0;
    memset(a2500, 'A', 2500); a2500[2500] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, a2500);              /* 1000 + 1000 + 500 */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 3);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, a1000));             /* the lines are cut at 1000 ... */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, a1001));            /* ... and none is longer */
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, a1000);              /* exactly full is still one line */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, a1001);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 2);
    /* An escape is never cut in the middle: 996 characters and "\xe9" (four characters) fill
     * the line exactly; 997 characters and "\xe9" do not, so the escape starts the next line. */
    memset(esc, 'B', 996); esc[996] = (char)0xE9; esc[997] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, esc);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "B\\xe9"));
    memset(esc, 'C', 997); esc[997] = (char)0xE9; esc[998] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringA, esc);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 2);
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "C\\x"));           /* no line holds the start of the escape */

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(kernel32, OutputDebugStringA, "some text\nmore text");
    CALL(kernel32, OutputDebugStringA, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
