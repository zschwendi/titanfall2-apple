#include "rt/test.h"
#include "rt/log.h"
#include "rt/module.h"
#include "rt/win/mm.h"

TEST(dsound, DirectSoundCreate)
{
    HMODULE h;
    void *out;
    HRESULT hr;

    T_CASE("LoadLibraryA(\"dsound.dll\") gives a module");
    h = CALL(kernel32, LoadLibraryA, "dsound.dll");
    T_TRUE(h != NULL);

    T_CASE("GetProcAddress of DirectSoundCreate is not NULL");
    T_TRUE(CALL(kernel32, GetProcAddress, h, "DirectSoundCreate") != 0);

    T_CASE("returns DSERR_NODRIVER and leaves the out-pointer NULL");
    out = (void *)(uintptr_t)1;
    hr = CALL(dsound, DirectSoundCreate, NULL, (void **)&out, NULL);
    T_EQ(hr, DSERR_NODRIVER);
    T_TRUE(out == NULL);

    T_CASE("logs once that there is no DirectSound output device");
    T_TRUE(T_LOGGED(RT_CH_IMPORT, "DirectSoundCreate: there is no DirectSound output device (sound goes out through XAudio2)"));

    T_CASE("a NULL out-pointer returns E_INVALIDARG");
    hr = CALL(dsound, DirectSoundCreate, NULL, NULL, NULL);
    T_EQ(hr, E_INVALIDARG);
}
