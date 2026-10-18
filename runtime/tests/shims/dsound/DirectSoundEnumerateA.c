#include "rt/test.h"
#include "rt/log.h"
#include "rt/module.h"
#include "rt/win/mm.h"

TEST(dsound, DirectSoundEnumerateA)
{
    HMODULE h;
    HRESULT hr;

    T_CASE("LoadLibraryA(\"dsound.dll\") gives a module");
    h = CALL(kernel32, LoadLibraryA, "dsound.dll");
    T_TRUE(h != NULL);

    T_CASE("GetProcAddress of DirectSoundEnumerateA is not NULL");
    T_TRUE(CALL(kernel32, GetProcAddress, h, "DirectSoundEnumerateA") != 0);

    T_CASE("returns DS_OK (0) and the callback is never called (a guest pointer that is not dereferenced)");
    hr = CALL(dsound, DirectSoundEnumerateA, (void *)(uintptr_t)0x1, NULL);
    T_EQ(hr, 0);

    T_CASE("logs once that there is no DirectSound output device to enumerate");
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "DirectSoundEnumerateA: there is no DirectSound output device to enumerate (sound goes out through XAudio2)"));

    T_CASE("a NULL callback returns E_INVALIDARG");
    hr = CALL(dsound, DirectSoundEnumerateA, NULL, NULL);
    T_EQ(hr, E_INVALIDARG);
}
