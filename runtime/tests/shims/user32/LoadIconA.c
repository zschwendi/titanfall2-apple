#include "rt/test.h"
#include "rt/log.h"
#include "rt/module.h"
#include "rt/win/user.h"

TEST(user32, LoadIconA)
{
    static const unsigned idi[] = {
        IDI_APPLICATION, IDI_HAND, IDI_QUESTION, IDI_EXCLAMATION,
        IDI_ASTERISK, IDI_WINLOGO, IDI_SHIELD,
    };
    unsigned i;

    T_CASE("predefined icon of every IDI_ identifier, same handle on each call");
    T_EQ(CALL(user32, LoadIconA, NULL, (LPCVOID)(uintptr_t)IDI_APPLICATION), (HICON)0x27F00);
    T_EQ(CALL(user32, LoadIconA, NULL, (LPCVOID)(uintptr_t)IDI_APPLICATION), (HICON)0x27F00);
    for (i = 0; i < sizeof idi / sizeof idi[0]; i++)
        T_EQ(CALL(user32, LoadIconA, NULL, (LPCVOID)(uintptr_t)idi[i]),
             (HICON)(uintptr_t)(0x20000u + idi[i]));

    T_CASE("unknown identifier fails with ERROR_RESOURCE_NAME_NOT_FOUND");
    T_EQ(CALL(user32, LoadIconA, NULL, (LPCVOID)(uintptr_t)100), (HICON)NULL);
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);
    T_EQ(CALL(user32, LoadIconA, NULL, (LPCVOID)(uintptr_t)32519), (HICON)NULL);   /* just past IDI_SHIELD */
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);

    T_CASE("success does not change last error");
    CALL(kernel32, SetLastError, 0x1234);
    CALL(user32, LoadIconA, NULL, (LPCVOID)(uintptr_t)IDI_APPLICATION);
    T_EQ(T_LAST_ERROR(), 0x1234);

    T_CASE("an icon of the executable is not there: NULL with ERROR_RESOURCE_NAME_NOT_FOUND and one WARN line");
    HINSTANCE exe = (HINSTANCE)(uintptr_t)rt_module_exe()->base;
    T_TRUE(exe != NULL && exe == (HINSTANCE)CALL(kernel32, GetModuleHandleA, NULL));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, LoadIconA, exe, (LPCVOID)(uintptr_t)101), (HICON)NULL);
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "LoadIconA: icon 101 of the executable"));
    T_EQ(T_LOG_COUNT(RT_CH_WINDOW), 1);
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, LoadIconA, exe, (LPCVOID)"GameIcon"), (HICON)NULL);
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);
    T_TRUE(T_LOGGED(RT_CH_WINDOW, "LoadIconA: icon \"GameIcon\" of the executable"));
    CALL(kernel32, SetLastError, 0x1234);
    T_EQ(CALL(user32, LoadIconA, exe, (LPCVOID)(uintptr_t)IDI_APPLICATION), (HICON)NULL);   /* a system id is no id of the executable */
    T_EQ(T_LAST_ERROR(), ERROR_RESOURCE_NAME_NOT_FOUND);

    /* RT-TRAPS: any other module's resources are not read (a DLL's icon, a handle that is no
     * module): never guessed. */
    T_TRAPS(CALL(user32, LoadIconA, (HINSTANCE)((uintptr_t)exe + 0x1000), (LPCVOID)(uintptr_t)101),
            "an icon from the resources of module");
    T_TRAPS(CALL(user32, LoadIconA, (HINSTANCE)(uintptr_t)0x400000, (LPCVOID)(uintptr_t)1),
            "an icon from the resources of module");
}
