#include "rt/test.h"

/* A NUL-terminated UTF-16 string from a C string: one WCHAR per byte of `s`.  WCHAR is
 * uint16_t; wchar_t is never used. */
static void wide(WCHAR *out, const char *s) {
    for (; *s; s++) *out++ = (unsigned char)*s;
    *out = 0;
}

TEST(kernel32, OutputDebugStringW)
{
    WCHAR w[2600];
    unsigned n;

    T_CASE("the text is logged on the guest.debug channel");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    wide(w, "hello debug world");
    CALL(kernel32, OutputDebugStringW, w);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "hello debug world"));
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    /* The text is data, never the format: a percent sign comes out as a percent sign. */
    wide(w, "100%s done %d%%");
    CALL(kernel32, OutputDebugStringW, w);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "100%s done %d%%"));

    T_CASE("one log line per line of text without the line ending");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    wide(w, "first\r\nsecond\nthird");
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 3);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "first"));
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "second"));
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "third"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "firstsecond"));    /* three lines, not one */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "first\r"));        /* the line ending is not printed ... */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "first\\x0d"));     /* ... nor escaped when it is half of "\r\n" */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\n"));
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    wide(w, "alpha\n");                                     /* a line ending at the end adds no empty line */
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "alpha"));
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    wide(w, "beta\r\n");
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "beta"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "beta\\x0d"));
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    wide(w, "one\n\ntwo");                                  /* an empty line between two line endings is a line */
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 3);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "one"));
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "two"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "onetwo"));

    T_CASE("characters of code page 1252 are written as OutputDebugStringA writes them");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    wide(w, "caf\xe9 \x01 \x7f\tx\ry");                     /* the bytes are the 1252 characters */
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);            /* a lone \r is no line ending */
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "caf\\xe9 \\x01 \\x7f\tx\\x0dy"));   /* the tab stays a tab */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\xe9"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\x01"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\x7f"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\r"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "\\x09"));
    WCHAR euro_quote[] = { 'a', 0x20AC, 'b', 0x2019, 0 };  /* U+20AC is 0x80 and U+2019 is 0x92 in 1252 */
    CALL(kernel32, OutputDebugStringW, euro_quote);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "a\\x80b\\x92"));
    wide(w, "back\\slash \\x41");                           /* a backslash is printed as is */
    CALL(kernel32, OutputDebugStringW, w);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "back\\slash \\x41"));
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "back\\x5c"));

    T_CASE("NULL and the empty string log nothing");
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringW, NULL);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n);
    wide(w, "");
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n);

    T_CASE("a line longer than the buffer is continued on the next log line");
    char a1000[1001], a1001[1002];                          /* the expected lines, as text */
    memset(a1000, 'A', 1000); a1000[1000] = 0;
    memset(a1001, 'A', 1001); a1001[1001] = 0;
    for (unsigned i = 0; i < 2500; i++) w[i] = 'A';
    w[2500] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringW, w);                  /* 1000 + 1000 + 500 */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 3);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, a1000));             /* the lines are cut at 1000 ... */
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, a1001));            /* ... and none is longer */
    for (unsigned i = 0; i < 1000; i++) w[i] = 'A';
    w[1000] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringW, w);                  /* exactly full is still one line */
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    for (unsigned i = 0; i < 1001; i++) w[i] = 'A';
    w[1001] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 2);
    /* An escape is never cut in the middle: 996 characters and "\xe9" (four characters) fill
     * the line exactly; 997 characters and "\xe9" do not, so the escape starts the next line. */
    for (unsigned i = 0; i < 996; i++) w[i] = 'B';
    w[996] = 0xE9; w[997] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 1);
    T_TRUE(T_LOGGED(RT_CH_GUEST_DEBUG, "B\\xe9"));
    for (unsigned i = 0; i < 997; i++) w[i] = 'C';
    w[997] = 0xE9; w[998] = 0;
    n = T_LOG_COUNT(RT_CH_GUEST_DEBUG);
    CALL(kernel32, OutputDebugStringW, w);
    T_EQ(T_LOG_COUNT(RT_CH_GUEST_DEBUG), n + 2);
    T_TRUE(!T_LOGGED(RT_CH_GUEST_DEBUG, "C\\x"));           /* no line holds the start of the escape */

    /* Not a listed case: a character that code page 1252 does not have (RT-TRAPS).  Each call
     * stops the run in a child process, so the report is what is checked. */
    WCHAR cjk[] = { 0x4E2D, 0 };
    T_TRAPS(CALL(kernel32, OutputDebugStringW, cjk), "U+4E2D");
    WCHAR abar[] = { 'a', 0x0100, 0 };                      /* best fit would print 'a': not guessed */
    T_TRAPS(CALL(kernel32, OutputDebugStringW, abar), "U+0100");
    WCHAR lone[] = { 0xD83D, 0 };                           /* an unpaired surrogate */
    T_TRAPS(CALL(kernel32, OutputDebugStringW, lone), "U+D83D");

    T_CASE("does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    wide(w, "some text\nmore text");
    CALL(kernel32, OutputDebugStringW, w);
    CALL(kernel32, OutputDebugStringW, NULL);
    T_EQ(T_LAST_ERROR(), 0x1234);
}
