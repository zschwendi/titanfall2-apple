/* The registry (rt/registry.h): rt_reg_delete_tree_w and _a, the core of SHDeleteKey.
 *
 * Where the expected values come from: the comment of the two functions in rt/registry.h.  The
 * first case is the sequence of Wine's shlwapi/tests/shreg.c (test_SHDeleteKey) with the results
 * that test requires of Windows; the codes for a key that is not there, a bad handle and a
 * deleted key are the ones the header gives every function; the rest is the header's own rules
 * (all or nothing, deleted keys, the seed, the file).
 */
#include "registry_util.c"

#include "rt/time.h"

/* A clock that moves at every reading, so that a last write time can be seen to change. */
static uint64_t fake_now = UINT64_C(0x01DC000000000000);
static uint64_t fake_utc(void) { return fake_now += 10000000; }

static uint64_t write_time(HKEY k)
{
    FILETIME ft = { 0, 0 };
    T_EQ(rt_reg_query_info_key_w(k, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, &ft), ERROR_SUCCESS);
    return ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
}

TEST(core, registry_delete_tree)
{
    Ctx *ctx = T_CTX();
    rt_set_last_error(ctx, 0x5EA1);

    T_CASE("a key with two subkeys is deleted and no longer opens");
    cl(mk(HKEY_CURRENT_USER, "Software\\Wine\\Test\\ODBC\\ODBC.INI"));
    cl(mk(HKEY_CURRENT_USER, "Software\\Wine\\Test\\ODBC\\ODBCINST.INI"));
    T_EQ(rt_reg_delete_key_ex_a(HKEY_CURRENT_USER, "Software\\Wine\\Test\\ODBC", 0, 0), ERROR_ACCESS_DENIED); /* the plain delete refuses it */
    T_EQ(rt_reg_delete_tree_a(HKEY_CURRENT_USER, "Software\\Wine\\Test\\ODBC"), ERROR_SUCCESS);
    {
        HKEY k = NULL;
        T_EQ(rt_reg_open_key_ex_a(HKEY_CURRENT_USER, "Software\\Wine\\Test\\ODBC", 0, KEY_READ, &k), ERROR_FILE_NOT_FOUND);
    }
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Wine\\Test"));         /* the parent stays */

    T_CASE("a deep tree goes with all its values, its siblings stay, and the parent's last write time moves");
    {
        rt_time_set_source(NULL, fake_utc);
        HKEY top = mk(HKEY_CURRENT_USER, "Software\\Tree");
        HKEY a = mk(top, "A"), b = mk(a, "B"), c = mk(b, "C"), d = mk(c, "D"), side = mk(b, "Side"), keep = mk(top, "Keep");
        T_EQ(put_sz(a, "va", "one"), ERROR_SUCCESS);
        T_EQ(put_dw(b, "vb", 2), ERROR_SUCCESS);
        T_EQ(put_sz(d, NULL, "the default value"), ERROR_SUCCESS);
        T_EQ(put_dw(side, "vs", 4), ERROR_SUCCESS);
        T_EQ(put_dw(keep, "vk", 5), ERROR_SUCCESS);
        cl(a); cl(b); cl(c); cl(d); cl(side);
        const uint64_t before = write_time(top);
        DWORD nk = 0, nv = 0;
        counts(top, &nk, &nv);
        T_EQ(nk, 2);

        T_EQ(rt_reg_delete_tree_w(top, W("a")), ERROR_SUCCESS);         /* names compare ignoring case */
        counts(top, &nk, &nv);
        T_EQ(nk, 1);
        T_TRUE(!has_key(top, "A") && !has_key(top, "A\\B") && !has_key(top, "A\\B\\C\\D") && !has_key(top, "A\\B\\Side"));
        T_TRUE(has_key(top, "Keep") && is_dw(keep, "vk", 5));
        T_TRUE(write_time(top) > before);
        rt_time_set_source(NULL, NULL);

        /* a key of the same name can be made again and is empty */
        HKEY again = mk(top, "A");
        counts(again, &nk, &nv);
        T_EQ(nk, 0);
        T_EQ(nv, 0);
        cl(again);
        T_EQ(rt_reg_delete_tree_a(top, "A"), ERROR_SUCCESS);            /* a key without subkeys goes too */
        T_TRUE(!has_key(top, "A"));
        cl(keep); cl(top);
    }

    T_CASE("the subkey may be a path, and only its last key goes");
    cl(mk(HKEY_CURRENT_USER, "Software\\Path\\One\\Two\\Three"));
    cl(mk(HKEY_CURRENT_USER, "Software\\Path\\One\\Other"));
    T_EQ(rt_reg_delete_tree_a(HKEY_CURRENT_USER, "Software\\Path\\One\\Two"), ERROR_SUCCESS);
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Path\\One\\Other"));
    T_TRUE(!has_key(HKEY_CURRENT_USER, "Software\\Path\\One\\Two"));

    T_CASE("a call whose handle is found leaves last error alone, when it succeeds and when it fails");
    T_EQ(rt_reg_delete_tree_a(HKEY_CURRENT_USER, "Software\\Path\\NotHere"), ERROR_FILE_NOT_FOUND);
    T_EQ(T_LAST_ERROR(), 0x5EA1);       /* (a handle that is not found may leave ERROR_INVALID_HANDLE: rt/registry.h, rule 1) */

    T_CASE("handles open to keys of the tree stay valid and are ERROR_KEY_DELETED");
    {
        HKEY top = mk(HKEY_CURRENT_USER, "Software\\Held");
        HKEY x = mk(top, "X"), y = mk(x, "Y"), z = mk(y, "Z");
        T_EQ(put_dw(z, "v", 1), ERROR_SUCCESS);
        T_EQ(rt_reg_delete_tree_a(top, "X"), ERROR_SUCCESS);
        DWORD n = 0;
        T_EQ(rt_reg_query_info_key_w(x, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
        T_EQ(rt_reg_query_info_key_w(y, NULL, NULL, NULL, &n, NULL, NULL, NULL, NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
        T_EQ(rt_reg_query_value_ex_w(z, W("v"), NULL, NULL, NULL, NULL), ERROR_KEY_DELETED);
        T_EQ(put_dw(y, "new", 1), ERROR_KEY_DELETED);
        T_EQ(rt_reg_delete_tree_a(x, "Y"), ERROR_KEY_DELETED);          /* a deleted key as the parent */
        cl(x); cl(y); cl(z);
        T_EQ(rt_reg_close_key(z), ERROR_INVALID_HANDLE);                /* closed once */
        cl(top);
    }

    T_CASE("a key that is not there is ERROR_FILE_NOT_FOUND and a bad handle ERROR_INVALID_HANDLE; nothing changes");
    {
        HKEY top = mk(HKEY_CURRENT_USER, "Software\\Missing");
        cl(mk(top, "Here\\Below"));
        T_EQ(rt_reg_delete_tree_a(top, "NotHere"), ERROR_FILE_NOT_FOUND);
        T_EQ(rt_reg_delete_tree_w(top, W("NotHere\\Below")), ERROR_FILE_NOT_FOUND);
        T_EQ(rt_reg_delete_tree_a(top, "Here\\NotBelow"), ERROR_FILE_NOT_FOUND);
        T_TRUE(has_key(top, "Here\\Below"));
        T_EQ(rt_reg_delete_tree_a(NULL, "Here"), ERROR_INVALID_HANDLE);
        cl(top);
        T_EQ(rt_reg_delete_tree_a(top, "Here"), ERROR_INVALID_HANDLE);  /* a closed handle */
        T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Missing\\Here\\Below"));
    }

    T_CASE("the file loses the tree in one piece and a new start does not bring it back");
    {
        HKEY top = mk(HKEY_CURRENT_USER, "Software\\Saved");
        HKEY g = mk(top, "Gone"), g2 = mk(g, "Deeper");
        T_EQ(put_sz(g2, "v", "text"), ERROR_SUCCESS);
        cl(g2); cl(g); cl(top);
        char *t = reg_text(NULL);
        T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Saved\\Gone]") &&
               contains(t, "[HKEY_CURRENT_USER\\Software\\Saved\\Gone\\Deeper]"));
        free(t);
        T_EQ(rt_reg_delete_tree_a(HKEY_CURRENT_USER, "Software\\Saved\\Gone"), ERROR_SUCCESS);
        t = reg_text(NULL);
        T_TRUE(contains(t, "[HKEY_CURRENT_USER\\Software\\Saved]") && !contains(t, "Saved\\Gone"));
        free(t);
        T_TRUE(!exists(state_path("registry.txt.tmp")));
        rt_registry_test_reload();
        T_TRUE(has_key(HKEY_CURRENT_USER, "Software\\Saved"));
        T_TRUE(!has_key(HKEY_CURRENT_USER, "Software\\Saved\\Gone"));
    }

    T_CASE("a seeded key anywhere in the tree is fatal and nothing is deleted; a guest's key below one goes");
    {
        cl(mk(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\.rttest\\shell\\open"));
        T_TRAPS(rt_reg_delete_tree_a(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes"), "deleting the seeded key");
        T_TRAPS(rt_reg_delete_tree_a(HKEY_LOCAL_MACHINE, "HARDWARE"), "deleting the seeded key");
        T_TRAPS(rt_reg_delete_tree_w(HKEY_LOCAL_MACHINE, W("SYSTEM\\CurrentControlSet\\Control")), "deleting the seeded key");
        T_TRUE(has_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\.rttest\\shell\\open"));
        T_TRUE(has_key(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0"));
        T_EQ(rt_reg_delete_tree_a(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\.rttest"), ERROR_SUCCESS);
        T_TRUE(!has_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes\\.rttest"));
        T_TRUE(has_key(HKEY_LOCAL_MACHINE, "SOFTWARE\\Classes"));
    }

    T_CASE("no subkey is fatal");
    T_TRAPS(rt_reg_delete_tree_a(HKEY_CURRENT_USER, NULL), "SHDeleteKeyA: a NULL subkey");
    T_TRAPS(rt_reg_delete_tree_w(HKEY_CURRENT_USER, NULL), "SHDeleteKeyW: a NULL subkey");
    T_TRAPS(rt_reg_delete_tree_a(HKEY_CURRENT_USER, ""), "SHDeleteKeyA: an empty subkey");
    T_TRUE(has_key(HKEY_CURRENT_USER, "Software"));
}
