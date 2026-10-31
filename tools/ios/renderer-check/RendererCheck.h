#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int tf_renderer_check(const char *frameworks, const char *output,
                      uint32_t width, uint32_t height, uint32_t refresh);
#ifdef __cplusplus
}
#endif
