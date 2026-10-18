// dxback_priv.h -- what every file of the DXMT back end needs (unit dxback-split).  The group files (resources.cpp,
// shaders.cpp, ...) each define one dxback_fill_<group> declared at the end; dxback.cpp calls them from build_table.
// The header order matters: RT_D3DBACK_OWN_TYPES is defined before rt/d3dback.h (see dxback.cpp).
#define RT_D3DBACK_OWN_TYPES 1
#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <atomic>
#include <string>

#include <windows.h>
#include <d3d11.h>
#include "dxmt_native_host.h"   // DXMT's header: the hooks (third_party/dxmt/include/native)

// rt/shim.h would make this typedef; the header only passes the context on, as a pointer (rt/log.h: struct Ctx)
typedef struct Ctx Ctx;
#ifndef TF_INLINE
#define TF_INLINE static inline
#endif
#include "rt/d3dback.h"

// ---- what the runtime provides (declared here, not included: rt/log.h and rt/crash.h declare Windows types) ----
extern "C" {
// rt/log.h: fatal report; ends the process.  Its line is the report.
[[noreturn]] void rt_fatal(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
}

// ---- the group fillers: each file fills its own entries of the table, and nothing else ----
void dxback_fill_resources(RtD3dBack &t);   // resources.cpp
void dxback_fill_shaders(RtD3dBack &t);     // shaders.cpp
void dxback_fill_states(RtD3dBack &t);      // states.cpp
void dxback_fill_bindbuf(RtD3dBack &t);     // bindbuf.cpp
void dxback_fill_bindshader(RtD3dBack &t);  // bindshader.cpp
void dxback_fill_pipeline(RtD3dBack &t);    // pipeline.cpp
void dxback_fill_draw(RtD3dBack &t);        // draw.cpp
void dxback_fill_data(RtD3dBack &t);        // data.cpp
void dxback_fill_swapchain(RtD3dBack &t);   // swapchain.cpp (unit dxback-swapchain)

// ---- shared between the files of the back end (dxback.cpp keeps these) ----
// The immediate context the device was made with (kept from CreateDevice until the device is released); NULL when unknown.
ID3D11DeviceContext *dxback_immediate_context(void *device);
// A swap chain was released (after DXMT's Release): its native window is freed (swapchain.cpp).
void dxback_swapchain_released(void *swapchain);
