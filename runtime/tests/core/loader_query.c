/* The loader: questions about loaded modules (runtime/core/loader_query.c; specification: the
 * seven functions in rt/module.h).  Plan: docs/handoff/rt-contract-loader.md, task 3.  No
 * guest code runs: the test sets RtModule.loaded of slot a itself. */
#include <pthread.h>

#include "rt/test_pe.h"
#include "../../core/loader.h"

#define SET_ERR()  rt_thread_set_last_error(0x1234)
#define KPATH      "C:\\Windows\\system32\\kernel32.dll"

static bool weq(const WCHAR *w, const char *s) {
    size_t i = 0;
    for (; s[i]; i++) if (w[i] != (WCHAR)(unsigned char)s[i]) return false;
    return w[i] == 0;
}

static void *taker(void *arg) {
    unsigned *failed = arg;
    for (unsigned i = 0; i < 1000; i++) {
        RtModule *m = NULL;
        *failed += !rt_module_handle_ex(0, u"user32", true, &m) || m == NULL;
    }
    return NULL;
}

TEST_SETUP(core, loader_query) { rt_test_pe_write(rt_test_pe_new(), "t_lift_a.dll"); }

TEST(core, loader_query)
{
    RtModule *A = rt_module_find("t_lift_a.dll"), *K = rt_module_find("kernel32.dll"), *E = rt_module_exe();
    RtModule *m = NULL;
    WCHAR wbuf[300];
    char  abuf[300];
    T_REQUIRE(A != NULL && K != NULL && E != NULL && !A->loaded);

    T_PLAN(1, "NULL is the executable") {
        T_TRUE(rt_module_handle_w(NULL) == E);
        T_TRUE(rt_module_handle_a(NULL) == E);
    }
    T_PLAN(2, "names: case, extension, path") {
        T_TRUE(rt_module_handle_w(u"kernel32.dll") == K);
        T_TRUE(rt_module_handle_w(u"KERNEL32") == K);
        T_TRUE(rt_module_handle_w(u"C:\\Windows\\System32\\Kernel32.DLL") == K);
        T_TRUE(rt_module_handle_a("kernel32") == K);
    }
    T_PLAN(3, "a name the registry does not have") {
        unsigned lines = T_LOG_COUNT(RT_CH_LOADER);
        SET_ERR();
        T_TRUE(rt_module_handle_w(u"nosuch.dll") == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        T_EQ(T_LOG_COUNT(RT_CH_LOADER), lines);
    }
    T_PLAN(4, "a registry module that is not loaded is not found") {
        A->loaded = false;
        SET_ERR();
        T_TRUE(rt_module_handle_w(u"t_lift_a.dll") == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        SET_ERR();
        T_TRUE(rt_module_handle_a("t_lift_a") == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        A->loaded = true;
        T_TRUE(rt_module_handle_w(u"t_lift_a.dll") == A);
        T_TRUE(rt_module_handle_a("T_LIFT_A") == A);
    }
    T_PLAN(5, "success leaves the last error alone") {
        SET_ERR();
        T_TRUE(rt_module_handle_w(u"kernel32.dll") == K);
        T_TRUE(rt_module_handle_w(u"KERNEL32") == K);
        T_TRUE(rt_module_handle_a("kernel32") == K);
        T_TRUE(rt_module_handle_w(NULL) == E);
        T_EQ(T_LAST_ERROR(), 0x1234);
    }
    T_PLAN(6, "from a handle: exactly the base of a loaded module") {
        SET_ERR();
        T_TRUE(rt_module_from_handle(NULL) == E);
        T_TRUE(rt_module_from_handle((HMODULE)(uintptr_t)K->base) == K);
        T_EQ(T_LAST_ERROR(), 0x1234);
        T_TRUE(rt_module_from_handle((HMODULE)(uintptr_t)(K->base + 1)) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        SET_ERR();
        T_TRUE(rt_module_from_handle((HMODULE)(uintptr_t)(K->base + 0x1000)) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        SET_ERR();
        T_TRUE(rt_module_from_handle((HMODULE)(uintptr_t)0x1234) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        A->loaded = false;
        SET_ERR();
        T_TRUE(rt_module_from_handle((HMODULE)(uintptr_t)A->base) == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
        A->loaded = true;
        T_TRUE(rt_module_from_handle((HMODULE)(uintptr_t)A->base) == A);
    }
    T_PLAN(7, "from a code address; never a last error") {
        SET_ERR();
        T_TRUE(rt_module_from_pc(K->base + 0x10008) == K);
        T_TRUE(rt_module_from_pc(K->base + RT_NATIVE_SLOT) == NULL);     /* the gap after the module */
        T_TRUE(rt_module_from_pc(0) == NULL);
        A->loaded = false;
        T_TRUE(rt_module_from_pc(A->base + 0x1000) == NULL);
        A->loaded = true;
        T_TRUE(rt_module_from_pc(A->base + 0x1000) == A);
        T_EQ(T_LAST_ERROR(), 0x1234);
    }
    T_PLAN(8, "handle_ex counts a reference") {
        int32_t before = K->load_count;
        m = NULL;
        T_EQ(rt_module_handle_ex(0, u"kernel32", true, &m), TRUE);
        T_TRUE(m == K);
        T_EQ(K->load_count, before + 1);
    }
    T_PLAN(9, "UNCHANGED_REFCOUNT, bytes for a name") {
        int32_t before = K->load_count;
        m = NULL;
        T_EQ(rt_module_handle_ex(GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, "kernel32", false, &m), TRUE);
        T_TRUE(m == K);
        T_EQ(K->load_count, before);
    }
    T_PLAN(10, "PIN counts like no flag") {
        int32_t before = K->load_count;
        m = NULL;
        T_EQ(rt_module_handle_ex(GET_MODULE_HANDLE_EX_FLAG_PIN, u"kernel32", true, &m), TRUE);
        T_TRUE(m == K);
        T_EQ(K->load_count, before + 1);
    }
    T_PLAN(11, "FROM_ADDRESS") {
        int32_t before = K->load_count;
        m = NULL;
        T_EQ(rt_module_handle_ex(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 (void *)(uintptr_t)(K->base + 0x10008), true, &m), TRUE);
        T_TRUE(m == K);
        T_EQ(K->load_count, before);
        m = K;
        SET_ERR();
        T_EQ(rt_module_handle_ex(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, NULL, true, &m), FALSE);    /* not the executable */
        T_TRUE(m == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    }
    T_PLAN(12, "a NULL name is the executable") {
        m = NULL;
        T_EQ(rt_module_handle_ex(0, NULL, true, &m), TRUE);
        T_TRUE(m == E);
    }
    T_PLAN(13, "not found clears the result") {
        m = K;
        SET_ERR();
        T_EQ(rt_module_handle_ex(0, u"nosuch", true, &m), FALSE);
        T_TRUE(m == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_MOD_NOT_FOUND);
    }
    T_PLAN(14, "bad flags and a NULL result pointer") {
        int32_t before = K->load_count;
        m = K;
        SET_ERR();
        T_EQ(rt_module_handle_ex(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, u"kernel32", true, &m), FALSE);
        T_TRUE(m == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        m = K;
        SET_ERR();
        T_EQ(rt_module_handle_ex(8, u"kernel32", true, &m), FALSE);
        T_TRUE(m == NULL);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        SET_ERR();
        T_EQ(rt_module_handle_ex(0, u"kernel32", true, NULL), FALSE);
        T_EQ(T_LAST_ERROR(), ERROR_INVALID_PARAMETER);
        T_EQ(K->load_count, before);
    }
    T_PLAN(15, "file names of a system module and of the executable") {
        T_EQ(rt_module_filename_w(K, wbuf, 260), 32);
        T_TRUE(weq(wbuf, KPATH));
        T_EQ(rt_module_filename_a(E, abuf, 260), 28);
        T_TRUE(!strcmp(abuf, "C:\\Titanfall2\\Titanfall2.exe"));
    }
    T_PLAN(16, "file names of a lifted and a stubbed module: the manifest's spelling") {
        T_EQ(rt_module_filename_a(A, abuf, 260), strlen("C:\\Titanfall2\\bin\\x64_retail\\t_lift_a.dll"));
        T_TRUE(!strcmp(abuf, "C:\\Titanfall2\\bin\\x64_retail\\t_lift_a.dll"));
        RtModule *g = rt_module_find("gfsdk_ssao.win64.dll");
        T_REQUIRE(g != NULL);
        rt_module_filename_a(g, abuf, 260);
        T_TRUE(!strcmp(abuf, "C:\\Titanfall2\\bin\\x64_retail\\GFSDK_SSAO.win64.dll"));
        rt_module_filename_w(g, wbuf, 260);
        T_TRUE(weq(wbuf, "C:\\Titanfall2\\bin\\x64_retail\\GFSDK_SSAO.win64.dll"));
    }
    T_PLAN(17, "an exact fit") {
        memset(wbuf, 0xFF, sizeof wbuf);
        SET_ERR();
        T_EQ(rt_module_filename_w(K, wbuf, 33), 32);
        T_TRUE(weq(wbuf, KPATH));
        T_EQ(wbuf[33], 0xFFFF);
        T_EQ(T_LAST_ERROR(), 0x1234);
    }
    T_PLAN(18, "a buffer that is too small is filled, terminated, and reported") {
        memset(wbuf, 0xFF, sizeof wbuf);
        SET_ERR();
        T_EQ(rt_module_filename_w(K, wbuf, 32), 32);
        unsigned wrong = 0;
        for (unsigned i = 0; i < 31; i++) wrong += wbuf[i] != (WCHAR)KPATH[i];
        T_EQ(wrong, 0);
        T_EQ(wbuf[29], 'd');
        T_EQ(wbuf[30], 'l');
        T_EQ(wbuf[31], 0);
        T_EQ(wbuf[32], 0xFFFF);                                     /* nothing past cch */
        T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
        memset(abuf, 0x7F, sizeof abuf);
        SET_ERR();
        T_EQ(rt_module_filename_a(K, abuf, 32), 32);
        T_TRUE(!strncmp(abuf, KPATH, 31) && abuf[31] == 0 && abuf[32] == 0x7F);
        T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
        wbuf[0] = 0xFFFF;
        SET_ERR();
        T_EQ(rt_module_filename_w(K, wbuf, 1), 1);
        T_EQ(wbuf[0], 0);
        T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
    }
    T_PLAN(19, "a buffer of no characters") {
        wbuf[0] = 0xFFFF;
        SET_ERR();
        T_EQ(rt_module_filename_w(K, wbuf, 0), 0);
        T_EQ(T_LAST_ERROR(), ERROR_INSUFFICIENT_BUFFER);
        T_EQ(wbuf[0], 0xFFFF);
        T_EQ(rt_module_filename_w(K, NULL, 0), 0);
    }
    T_PLAN(20, "misuse is fatal") {
        T_TRAPS(rt_module_filename_w(NULL, wbuf, 10), "null");
        T_TRAPS(rt_module_filename_a(K, NULL, 10), "null");
    }
    T_PLAN(21, "four threads take 1000 references each") {
        RtModule *u = rt_module_find("user32.dll");
        T_REQUIRE(u != NULL);
        int32_t before = u->load_count;
        pthread_t th[4];
        unsigned failed[4] = { 0, 0, 0, 0 };
        for (unsigned i = 0; i < 4; i++) T_EQ(pthread_create(&th[i], NULL, taker, &failed[i]), 0);
        for (unsigned i = 0; i < 4; i++) { pthread_join(th[i], NULL); T_EQ(failed[i], 0); }
        T_EQ(u->load_count, before + 4000);
    }
}
