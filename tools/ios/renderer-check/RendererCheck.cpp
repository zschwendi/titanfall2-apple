#include "RendererCheck.h"
#include "dxmt_native_display.h"
// Use the existing real D3D11 shader conversion, draw and pixel-readback test.
#define main tf_offscreen_triangle_main
#include "../../dxmt/offscreen_triangle.cpp"
#undef main

int tf_renderer_check(const char *frameworks, const char *output,
                      uint32_t width, uint32_t height, uint32_t refresh) {
    const DXMTNativeDisplay display = {width, height, refresh, 1};
    if (!DXMTSetNativeDisplay(&display)) return 3;
    const std::string log = std::string(output) + "/renderer.log";
    FILE *stream = fopen(log.c_str(), "w");
    if (!stream) return 4;
    int saved_out = dup(STDOUT_FILENO), saved_error = dup(STDERR_FILENO);
    fflush(stdout); fflush(stderr);
    dup2(fileno(stream), STDOUT_FILENO); dup2(fileno(stream), STDERR_FILENO);
    setvbuf(stdout, nullptr, _IONBF, 0); setvbuf(stderr, nullptr, _IONBF, 0);
    setenv("DXMT_LOG_PATH", output, 1);
    setenv("DXMT_SHADER_CACHE_PATH", output, 1);
    char executable[] = "TFRendererCheck", option[] = "--ppm";
    std::string prefix = std::string(output) + "/triangle";
    char *args[] = {executable, const_cast<char *>(frameworks), option, prefix.data()};
    int result = tf_offscreen_triangle_main(4, args);
    fflush(stdout); fflush(stderr);
    dup2(saved_out, STDOUT_FILENO); dup2(saved_error, STDERR_FILENO);
    close(saved_out); close(saved_error); fclose(stream);
    return result;
}
