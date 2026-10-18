#!/usr/bin/env python3
"""The contract of the do-nothing Direct3D device as a table, and its task rows (unit d3d-contract).

Every one of the 146 (interface, method) pairs finding 31 found the game calling
(docs/findings/31-d3d11-usage.json) is in exactly ONE of the lists below:

    COMMON     QueryInterface, AddRef, Release: the interface layer's, for every interface
    BUILT      a body written by unit d3d-contract (or a row's body, once it is written)
    ACCEPT     takes its arguments and returns success: a line of runtime/com/accept.def
    FORWARD    was such a line until unit pic-seam and is a body file since: accepted as before,
               and handed on to a back end when there is one (rt/d3dback.h)
    ROWS       an answer a small worker writes from a task row: one or two methods per row
    RESOURCES  an answer reserved for unit d3d-resources (creators, mapping, queries, the back buffer)
    STOPS      stays a stop, and why

Outside the 146, and in no list above:

    TRACED     a method the real game was recorded calling that finding 31's reading of the code
               did not find (docs/findings/40-d3d-call-trace.md), with a body and a test

    .venv/bin/python tools/swarm/d3d_rows.py --check      prove that, and print the counts
    .venv/bin/python tools/swarm/d3d_rows.py --table      the contract table (docs/handoff/d3d-contract.md, section 7)
    .venv/bin/python tools/swarm/d3d_rows.py --list       one line per row
    .venv/bin/python tools/swarm/d3d_rows.py --show ID    one brief

tools/swarm/make_shim_tasks.py calls problems(), show() and write() of this file, so its
--check, --show, --list and its writing of briefs and units cover these rows too.  A unit is
kind "method-task", model haiku; its check is `make method-check` (runtime/tools/check_method.py).

SOURCES.  "MS" is Microsoft's documentation of the method (learn.microsoft.com), "F35" finding 35
(what the native renderer reported on this Mac), "F31" finding 31, "model" the object model of
runtime/include/rt/nulldev.h.  A statement about Direct3D that was not read on Microsoft's page
on the day is marked "from memory"; the review before the wave checks those first.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CTX = "ID3D11DeviceContext"
DEV = "ID3D11Device"

# ---- built by unit d3d-contract ----------------------------------------------------------------------
BUILT = {
    ("IDXGIDevice1", "GetParent"): "the device's adapter for IDXGIAdapter1; any other IID is a stop | model; MS (read 10 Oct): names no result for an interface the parent lacks",
    ("IDXGIAdapter1", "GetParent"): "the adapter's factory for IDXGIFactory1; any other IID is a stop | model; MS as above",
    ("IDXGIAdapter1", "EnumOutputs"): "index 0: a new output object, S_OK; above: DXGI_ERROR_NOT_FOUND and NULL; a NULL out-pointer is a stop | MS (read 10 Oct); one output: design 3.4; NULL on not-found: Wine, from memory",
    ("IDXGIAdapter1", "GetDesc"): "\"Apple M3 Pro\", vendor 0x106b, 7151124480 bytes of video memory, the rest 0, the probe's LUID; S_OK; a NULL description is a stop | F35",
    ("IDXGIFactory1", "EnumAdapters1"): "index 0: a new adapter object, S_OK; above: DXGI_ERROR_NOT_FOUND and NULL; NULL out-pointer: DXGI_ERROR_INVALID_CALL | MS (read 10 Oct); one adapter: F35; NULL on not-found: Wine, from memory",
    ("IDXGIOutput", "GetDesc"): "\"\\\\.\\DISPLAY1\", the desktop rectangle of rt_desktop(), attached, rotation identity, the window contract's monitor handle; S_OK | F35 for the name; design 3.4 for the desktop",
    ("IDXGIOutput", "GetDisplayModeList"): "one mode, the desktop's (1920 by 1080 at 60 Hz), for format R8G8B8A8_UNORM and flags 0; count protocol and DXGI_ERROR_MORE_DATA; other formats and flags are stops | MS (read 10 Oct) for the protocol; the list is the contract's choice",
    ("ID3D11Device", "CreateBuffer"): "creator: S_OK and a new buffer holding the device (rt_nulldev_new), NdBuffer.desc as passed, memory of ByteWidth bytes; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateBuffer, D3D11_BUFFER_DESC, D3D11_CPU_ACCESS_FLAG (read 10 Oct by a subagent); a NULL out-pointer validates: S_FALSE",
    ("ID3D11Device", "CreateTexture1D"): "creator: S_OK and a new texture 1D holding the device (rt_nulldev_new), NdTexture.desc.d1, dim 2, memory by format and mip chain; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateTexture1D (read 10 Oct by a subagent); sizes: runtime/nulldev/memory.c; a NULL out-pointer validates: S_FALSE",
    ("ID3D11Device", "CreateTexture2D"): "creator: S_OK and a new texture 2D holding the device (rt_nulldev_new), NdTexture.desc.d2 with MipLevels 0 replaced by the real count, dim 3, memory by format, mip chain and array size; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateTexture2D, D3D11_TEXTURE2D_DESC (read 10 Oct by a subagent); sizes: runtime/nulldev/memory.c; a multisampled texture has no memory; a NULL out-pointer validates: S_FALSE",
    ("ID3D11Device", "CreateTexture3D"): "creator: S_OK and a new texture 3D holding the device (rt_nulldev_new), NdTexture.desc.d3, dim 4, memory by format and mip chain; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateTexture3D (read 10 Oct by a subagent); sizes: runtime/nulldev/memory.c; a NULL out-pointer validates: S_FALSE",
    ("ID3D11DeviceContext", "Map"): "writable memory at least as large as the subresource, ending at a guard page; row and depth pitch by format (design 3.2) | MS Map, D3D11_MAP, D3D11_MAPPED_SUBRESOURCE (read 10 Oct by a subagent); which combinations Windows refuses: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, E6), each a stop here; a read mapping of what the game did not write itself is a stop",
    ("ID3D11DeviceContext", "Unmap"): "ends the mapping Map recorded (NdMemory.mapped); with Map | MS Unmap (read 10 Oct by a subagent); an Unmap without a Map is a stop",
    ("ID3D11Device", "CreateBlendState"): "creator: S_OK and a new blend state holding the device (rt_nulldev_new), NdStateObject.desc.blend; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): an equal state returns the same interface; equal after normalisation, the normalised description kept: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, E5)",
    ("ID3D11Device", "CreateDepthStencilState"): "creator: S_OK and a new depth-stencil state holding the device (rt_nulldev_new), NdStateObject.desc.depth_stencil; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): an equal state returns the same interface; equal after normalisation, the normalised description kept: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, E5)",
    ("ID3D11Device", "CreateRasterizerState"): "creator: S_OK and a new rasterizer state holding the device (rt_nulldev_new), NdStateObject.desc.rasterizer; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): an equal state returns the same interface; equal after normalisation, the normalised description kept: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, E5)",
    ("ID3D11Device", "CreateSamplerState"): "creator: S_OK and a new sampler state holding the device (rt_nulldev_new), NdStateObject.desc.sampler; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): an equal state returns the same interface; equal after normalisation, the normalised description kept: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, E5)",
    ("ID3D11Device", "CreateShaderResourceView"): "creator: S_OK and a new shader-resource view holding the device (rt_nulldev_new), NdView.resource (held) and desc.srv as Windows reads it back: format UNKNOWN is the texture's, a count of 0xffffffff is all the rest, a NULL description is derived from the texture (docs/reviews/d3d-contract-check-2.md, F1; unit d3d-fix-2); a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): what a NULL description stands for; a NULL out-pointer validates: S_FALSE; which dimension goes with which resource is from memory",
    ("ID3D11Device", "CreateUnorderedAccessView"): "creator: S_OK and a new unordered-access view holding the device (rt_nulldev_new), NdView.resource (held) and desc.uav as Windows reads it back: format UNKNOWN is the texture's, a count of 0xffffffff is all the rest, a NULL description is derived from the texture (docs/reviews/d3d-contract-check-2.md, F1; unit d3d-fix-2); a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): what a NULL description stands for; a NULL out-pointer validates: S_FALSE; which dimension goes with which resource is from memory",
    ("ID3D11Device", "CreateRenderTargetView"): "creator: S_OK and a new render-target view holding the device (rt_nulldev_new), NdView.resource (held) and desc.rtv as Windows reads it back: format UNKNOWN is the texture's, a count of 0xffffffff is all the rest, a NULL description is derived from the texture (docs/reviews/d3d-contract-check-2.md, F1; unit d3d-fix-2); a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): what a NULL description stands for; a NULL out-pointer validates: S_FALSE; which dimension goes with which resource is from memory",
    ("ID3D11Device", "CreateDepthStencilView"): "creator: S_OK and a new depth-stencil view holding the device (rt_nulldev_new), NdView.resource (held) and desc.dsv as Windows reads it back: format UNKNOWN is the texture's, a count of 0xffffffff is all the rest, a NULL description is derived from the texture (docs/reviews/d3d-contract-check-2.md, F1; unit d3d-fix-2); a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS, the creator's page (read 10 Oct by a subagent): what a NULL description stands for; a NULL out-pointer validates: S_FALSE; which dimension goes with which resource is from memory",
    ("ID3D11Device", "CreateVertexShader"): "creator: S_OK and a new vertex shader holding the device (rt_nulldev_new), NdShader.bytecode_length; a class linkage is a stop; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateVertexShader, CreatePixelShader, CreateInputLayout (read 10 Oct by a subagent): the linkage may be NULL, a NULL out-pointer validates: S_FALSE; nothing is compiled or parsed",
    ("ID3D11Device", "CreateGeometryShader"): "creator: S_OK and a new geometry shader holding the device (rt_nulldev_new), NdShader.bytecode_length; a class linkage is a stop; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateVertexShader, CreatePixelShader, CreateInputLayout (read 10 Oct by a subagent): the linkage may be NULL, a NULL out-pointer validates: S_FALSE; nothing is compiled or parsed",
    ("ID3D11Device", "CreatePixelShader"): "creator: S_OK and a new pixel shader holding the device (rt_nulldev_new), NdShader.bytecode_length; a class linkage is a stop; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateVertexShader, CreatePixelShader, CreateInputLayout (read 10 Oct by a subagent): the linkage may be NULL, a NULL out-pointer validates: S_FALSE; nothing is compiled or parsed",
    ("ID3D11Device", "CreateComputeShader"): "creator: S_OK and a new compute shader holding the device (rt_nulldev_new), NdShader.bytecode_length; a class linkage is a stop; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateVertexShader, CreatePixelShader, CreateInputLayout (read 10 Oct by a subagent): the linkage may be NULL, a NULL out-pointer validates: S_FALSE; nothing is compiled or parsed",
    ("ID3D11Device", "CreateInputLayout"): "creator: S_OK and a new input layout holding the device (rt_nulldev_new), NdInputLayout.nelements; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | MS CreateVertexShader, CreatePixelShader, CreateInputLayout (read 10 Oct by a subagent): the linkage may be NULL, a NULL out-pointer validates: S_FALSE; nothing is compiled or parsed; the elements are kept as a copy",
    ("ID3D11Device", "CreateQuery"): "creator: S_OK and a new query holding the device (rt_nulldev_new), NdQuery.desc; the four kinds of F31 section 5 (event, occlusion, timestamp, timestamp disjoint), any other a stop; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5) | F31 section 5 for the four kinds; a NULL out-pointer validates, S_FALSE: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, point 3)",
    ("ID3D11DeviceContext", "Begin"): "records that the query began (NdQuery.begun); with GetData | MS D3D11_QUERY (read 10 Oct by a subagent): Begin is disabled for event and timestamp queries, a stop here",
    ("ID3D11DeviceContext", "End"): "records that the query ended (NdQuery.ended); with GetData | MS GetData (read 10 Oct by a subagent): some queries need only End; an End without a Begin of the two other kinds is a stop",
    ("ID3D11DeviceContext", "GetData"): "never \"not ready\" (S_FALSE): the game re-queues itself while it is (design 3.2); the result per kind is d3d-resources' stated choice | event TRUE, occlusion 0 samples, timestamp the monotonic clock at End in 100 ns, disjoint 10,000,000 a second and FALSE (the device's stated choice); sizes, E_INVALIDARG and DXGI_ERROR_INVALID_CALL: Wine's d3d11 tests (docs/reviews/d3d-contract-check.md, point 3)",
    ("IDXGIFactory1", "CreateSwapChain"): "a swap chain (rt_nulldev_new) keeping the description, the factory and a back buffer: a texture 2D of the description's size and format | MS CreateSwapChain, DXGI_SWAP_CHAIN_DESC (read 10 Oct by a subagent); the window is not asked: a width or height of 0 is a stop; full screen at creation and a swap effect other than DISCARD are stops",
    ("IDXGISwapChain", "GetBuffer"): "buffer 0 as ID3D11Texture2D with a new reference; any other buffer or IID is a stop (the native renderer serves buffer 0 only: F35) | MS GetBuffer (read 10 Oct by a subagent): DISCARD serves buffer 0",
    ("IDXGISwapChain", "ResizeBuffers"): "a new back buffer of the new size, NdSwapChain.desc updated; needs the texture record's memory rules | MS ResizeBuffers (read 10 Oct by a subagent): count 0 and format UNKNOWN keep; a referenced back buffer: DXGI_ERROR_INVALID_CALL, nothing changes (Wine's dxgi tests, docs/reviews/d3d-contract-check.md, E7); a width or height of 0 is a stop",
    ("IDXGISwapChain", "ResizeTarget"): "the swap chain's own mode, field for field: S_OK and nothing changes; any other mode, a NULL mode, or a window whose client area is not the mode's size: a stop | the planner's decision (brief of gate3-run-3); MS (read 10 Oct): resizes the target window in windowed mode; the recording (F40, calls 5387, 5390): S_OK for exactly this",
    ("ID3D11ShaderReflection", "GetInputParameterDesc"): "element `index` of the input signature D3DReflect parsed from the shader's own bytes (NdReflection), the nine fields of d3dcompiler_46's 40-byte structure; an index past the end or a NULL description: E_INVALIDARG (unit d3d-reflect) | research dxbc-shaders 1.4, 3.5; Wine's reflection tests (read 11 Oct) for E_INVALIDARG; the game's loop at materialsystem_dx11+0x32635 ends on that value",
}

# ---- built by unit d3d-fix-2 (accept lines until then: d3d-resources' decision 4) ----------------------
_COPIES = ("records per destination subresource what it holds afterwards (the game's bytes, the game's bytes the device did not keep, "
           "a renderer's output, nothing) and copies the bytes when both sides keep them; ID3D11DeviceContext::Map reads the record "
           "before a read mapping; returns nothing | MS %s (read 10 Oct by a subagent); docs/trace/d3d-map.md for what the game passes")
BUILT[(CTX, "CopySubresourceRegion")] = _COPIES % "CopySubresourceRegion"
BUILT[(CTX, "CopyResource")] = _COPIES % "CopyResource"
BUILT[(CTX, "UpdateSubresource")] = ("the game's bytes go into the box, or the whole, of the destination subresource: copied where the device keeps the "
                                    "resource's bytes, read once where it does not; immutable and dynamic destinations are stops; returns nothing | "
                                    "MS UpdateSubresource (read 10 Oct by a subagent)")
BUILT[(CTX, "ResolveSubresource")] = ("the destination subresource is recorded as a renderer's output; a source that is not multisampled or a "
                                     "multisampled destination is a stop; returns nothing | MS ResolveSubresource (read 10 Oct by a subagent)")

# ---- accept: lines of runtime/com/accept.def -----------------------------------------------------------
_BIND = "a bind whose Get the game never calls (F31 section 4): nothing reads it back; drawing is not modelled (design 3.1)"
_DRAW = "draws nothing: the device has no pixels (design 3.1); returns nothing"
_COPY = "writes resource contents, which the device does not model (design 3.2: memory read back is zeroes); returns nothing"
ACCEPT = {}
# FORWARD: lines of accept.def until unit pic-seam, body files since (a body and an accept line
# for one method are an error of the generator).  A renderer must see each of them, so each has
# a body that hands the call on through the seam of rt/d3dback.h: two are written (DrawIndexed,
# ClearRenderTargetView), the others do what the accept line did until their back-end row is
# (tools/swarm/back_rows.py).  With no back end the answer is the one below, unchanged.
FORWARD = {}
for _m in ("IASetVertexBuffers IASetIndexBuffer GSSetConstantBuffers VSSetShaderResources VSSetSamplers GSSetShaderResources "
           "GSSetSamplers CSSetShaderResources CSSetUnorderedAccessViews CSSetShader CSSetSamplers CSSetConstantBuffers").split():
    FORWARD[(CTX, _m)] = _BIND
for _m in "DrawIndexed Draw DrawIndexedInstanced DrawInstanced DrawInstancedIndirect Dispatch DispatchIndirect".split():
    FORWARD[(CTX, _m)] = _DRAW
for _m in "ClearRenderTargetView ClearUnorderedAccessViewFloat ClearDepthStencilView".split():
    FORWARD[(CTX, _m)] = _COPY
FORWARD[(CTX, "Flush")] = "nothing is queued, so nothing is sent; returns nothing (MS: \"Sends queued-up commands in the command buffer to the GPU\"; from memory)"
ACCEPT[(DEV, "GetDeviceRemovedReason")] = "S_OK: the device is never removed (MS: \"S_OK\" is the first of the possible results; from memory. F35: the native renderer returned 0)"
ACCEPT[("IDXGIFactory1", "MakeWindowAssociation")] = "S_OK; GetWindowAssociation is not among the 146, so nothing reads the flags back, and DXGI never sees a key or a window message here (MS; from memory)"
ACCEPT[("IDXGIOutput", "SetGammaControl")] = "S_OK; GetGammaControl is not among the 146 and there is no screen whose gamma could change (MS; from memory)"
BUILT[("IDXGISwapChain", "Present")] = "S_OK; with a sync interval of 1 to 4 it returns at the n-th next boundary of the 60 Hz mode the device reports, with 0 at once (RT_D3D_PRESENT=immediate: always at once); counted in the ledger of invented answers; an interval above 4 or any flag is a stop | MS Present through docs/reviews/null-device-redteam.md, R1; unit d3d-fix-2, item 7"

# ---- reserved for unit d3d-resources -----------------------------------------------------------------
_CREATE = "creator: S_OK and a new %s holding the device (rt_nulldev_new), %s; a description it does not understand is a stop, and it never fails with a NULL pointer (design 3.2, 3.5)"
RESOURCES = {
}

# ---- stays a stop ------------------------------------------------------------------------------------
STOPS = {
    ("IDXGIOutput", "GetGammaControlCapabilities"): "would hand the game gamma capabilities (control-point count and positions) that nothing documents for a display we do not have; decide when a run reaches materialsystem_dx11+0x17990",
    ("IDXGIOutput", "GetDisplaySurfaceData"): "screen capture: pixels of a desktop that does not exist; data we cannot make",
}

# ---- outside the 146: called in the recording, built by unit d3d-fix-2 -------------------------------
TRACED = {
    ("ID3D11RenderTargetView", "GetDesc"): "all 20 bytes of the description the creator kept (NdView.desc.rtv), which is the normalised one; returns nothing; a NULL pointer is a stop | MS GetDesc (read 10 Oct); F40 row X: 973 calls from frame 994; what Windows reads back: Wine test_create_rendertarget_view through docs/reviews/d3d-contract-check-2.md, F1",
    ("ID3D11ShaderResourceView", "GetDesc"): "all 24 bytes of the description the creator kept (NdView.desc.srv), which is the normalised one; returns nothing; a NULL pointer is a stop | MS GetDesc (read 10 Oct); F40 row Z: 381 calls from frame 996; Wine test_create_shader_resource_view through the same block F1",
}

# ---- the task rows -----------------------------------------------------------------------------------
# id; methods [(interface, method)]; excerpt: names of rt/nulldev.h (records, functions) the brief
# prints; notes: the exact answer, with its source; model: a written body and test to copy the
# shape from ("Interface/Method"); waits: units with no file yet (the row is held); hold: a reason.

CONTEXT = """\
- State: `NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);` first, in both
  bodies. The record's fields start as zero (nothing bound) and every non-NULL pointer in it
  holds one reference: write a field only with `rt_nulldev_bind`, hand one out only with
  `rt_nulldev_ref`. Never `rt_com_addref` / `rt_com_release` yourself. (Since unit d3d-fix-2
  the reference `rt_nulldev_bind` takes is a hold: `rt_com_refs` counts it, as the test below
  expects, and the game's own AddRef and Release do not report it, as on Windows.)
- These methods return nothing. Source of the reference rules: Microsoft's page of each Set
  method ("The method will hold a reference to the interfaces passed in") and of each Get
  method ("Any returned interfaces will have their reference count incremented by one");
  both from memory, so cite the page in `RT-DOC` and say in your reply if it reads otherwise.
- Test: `rt_nulldev_create(&device, &context)`, objects to bind from
  `rt_nulldev_new(device, &<class>)`, counts with `rt_com_refs`. Check after a Set that the
  object's count went up by one, after a Get that the same pointer came back and the count
  went up by one more, after binding NULL (or another object) that the old one's count went
  down, and that a slot never set reads NULL.
"""
SLOTS = CONTEXT + """\
- Set: for i in [0, {n}): `rt_nulldev_bind(&c->{field}[StartSlot + i],
  rt_nulldev_arg(ctx, {pp}[i], &{cls}, "{pp}"))`; a NULL element unbinds the slot.
  Get: `{pp}[i] = rt_nulldev_ref(c->{field}[StartSlot + i])`.
- Stops (status `partial`), the same in both: `StartSlot + {n}` greater than `{limit}`
  ({limitv}: Microsoft gives StartSlot the range 0 to the slot count - 1 and the count 0 to
  the slot count - StartSlot; that the count is {limitv} is the header's number), written as
  `RT_UNIMPLEMENTED(ctx, "<Method> for slots %u to %u", ...)`. Take the sum in 64 bits,
  `(uint64_t)StartSlot + {n}`: a UINT sum wraps and would pass. This stop applies whatever
  the count is (StartSlot 20 with a count of 0 stops). Then a NULL array with a count above
  0, `RT_UNIMPLEMENTED(ctx, "<Method> with a NULL array")` (Microsoft marks the array
  optional and does not say what NULL does). A count of 0 inside the range does nothing.
- The Set's array is described with `const` elements: pass `(void *){pp}[i]` to
  `rt_nulldev_arg`.
"""
SHADER = CONTEXT + """\
- Set: `rt_nulldev_bind(&c->{field}, rt_nulldev_arg(ctx, {arg}, &{cls}, "{arg}"))`; NULL
  unbinds. `NumClassInstances` other than 0 is a stop,
  `RT_UNIMPLEMENTED(ctx, "{set} with %u class instances", NumClassInstances)`: the game never
  creates a class linkage (finding 31, section 4), so no instance can exist. `ppClassInstances`
  is then not looked at: `(void)ppClassInstances;`.
- Get: `*{out} = rt_nulldev_ref(c->{field})`; if `pNumClassInstances` is not NULL write 0
  through it (no instance is ever bound); `ppClassInstances` is not written. A NULL `{out}` is
  a stop, `RT_UNIMPLEMENTED(ctx, "{get} with a NULL out-pointer")`.
"""
NO_STAGE = CONTEXT + """\
- No class of this device presents `{iface}`: the game never creates such a shader (finding
  31, section 5: no CreateHullShader, no CreateDomainShader; these two methods occur only in
  its state save and restore helper). So `c->{field}` is always NULL.
- Set: a non-NULL `{arg}` is a stop, `RT_UNIMPLEMENTED(ctx, "{set} with a shader (%p)", ...)`:
  no such object can exist. `NumClassInstances` other than 0 is a stop,
  `RT_UNIMPLEMENTED(ctx, "{set} with %u class instances", ...)`. Otherwise nothing happens.
- Get: `*{out} = NULL`; if `pNumClassInstances` is not NULL write 0 through it;
  `ppClassInstances` is not written. A NULL `{out}` is a stop,
  `RT_UNIMPLEMENTED(ctx, "{get} with a NULL out-pointer")`.
- Test: after Set(NULL, NULL, 0), Get gives NULL and a count of 0 where the test had put
  other values; the three stops with `T_TRAPS` (for the first, pass any object, for example
  the device).
"""
ONE = CONTEXT + """\
- Set: `rt_nulldev_bind(&c->{field}, rt_nulldev_arg(ctx, {arg}, &{cls}, "{arg}"))`; NULL unbinds.
- Get: `*{out} = rt_nulldev_ref(c->{field})`. A NULL `{out}` is a stop,
  `RT_UNIMPLEMENTED(ctx, "{get} with a NULL out-pointer")`.
"""


# Added by unit d3d-rows-fix (E3 of docs/reviews/d3d-contract-check.md) for the rows whose
# bindings look at each other; SLOTS and CONTEXT above are as the other rows were told.
VIEW_OF = """\
- In the test, a view is a view OF a resource once its record says so (the creators are
  another unit's): `NdView *v = rt_com_state_of(view, &<the view's class>);
  rt_nulldev_bind(&v->resource, texture);` (a view keeps its resource with a hold, as
  everything `rt_nulldev_bind` writes: unit d3d-fix-2, F3) with `texture` from
  `rt_nulldev_new(device, &rt_nulldev_texture2d_class)`. Leave the rest of the record zero:
  such a view stands for the whole texture. A view whose `resource` is NULL overlaps nothing.
"""
PS_SRV_HAZARD = """\
- **One more rule for this Set** (it replaces the Set line above): a view that is bound for
  output is not bound for reading; its slot gets NULL. For each element:
  `void *v = rt_nulldev_arg(ctx, (void *)ppShaderResourceViews[i], &rt_nulldev_srv_class, "ppShaderResourceViews");
  rt_nulldev_bind(&c->ps_srv[StartSlot + i], rt_nulldev_is_target(ctx, c, v) ? NULL : v);`
  Source: Microsoft's page of PSSetShaderResources, "If an overlapping resource view is
  already bound to an output slot, such as a rendertarget, then this API will fill the
  destination shader resource slot with NULL" (read 10 October by unit d3d-rows-fix), and
  Wine's conformance test d3d11.c, `test_sample_attached_rtv`, which sets such a view and
  reads NULL back. The Get is as above.
""" + VIEW_OF + """\
- Test of the Set, besides the cases above: make a texture, a shader-resource view of it and
  a render-target view of it (`rt_nulldev_new(device, &rt_nulldev_rtv_class)`), and bind the
  target in the record yourself, `rt_nulldev_bind(&c->rtv[0], rtv)` (OMSetRenderTargets is
  another task's). Then a Set of the shader-resource view leaves its slot NULL and its count
  unchanged; a Set of it over a slot that held another view leaves NULL there and that
  view's count went down by one; a view of a second texture binds as usual; after
  `rt_nulldev_bind(&c->rtv[0], NULL)` the first view binds and its count goes up by one.
"""


def _slots(rid, stage, what, field, cls, limit, limitv, n, pp, extra="", names=()):
    return dict(id=rid, methods=[(CTX, stage + "Set" + what), (CTX, stage + "Get" + what)],
                excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new"] + list(names),
                notes=SLOTS.format(field=field, cls=cls, limit=limit, limitv=limitv, n=n, pp=pp) + extra)


def _shader(rid, stage, field, cls, arg):
    s, g = stage + "SetShader", stage + "GetShader"
    return dict(id=rid, methods=[(CTX, s), (CTX, g)],
                excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new"],
                notes=SHADER.format(field=field, cls=cls, arg=arg, out="p" + arg, set=s, get=g))


def _nostage(rid, stage, field, iface, arg):
    s, g = stage + "SetShader", stage + "GetShader"
    return dict(id=rid, methods=[(CTX, s), (CTX, g)], excerpt=["NdContext"],
                notes=NO_STAGE.format(field=field, iface=iface, arg=arg, out="p" + arg, set=s, get=g))


ROWS = [
    # ---- the device's own queries (on the path of steps 19 to 24) ----
    dict(id="device-feature-level", methods=[(DEV, "GetFeatureLevel")], excerpt=["NdDevice"], notes="""\
- Returns `s->level`, with `NdDevice *s = rt_com_state(ctx, This, &rt_nulldev_device_class);`.
  It is `D3D_FEATURE_LEVEL_11_0` (0xb000): the level `D3D11CreateDevice` reported, which is
  what the native renderer gave on this Mac for a null feature-level list (finding 35,
  docs/findings/35-dxmt-build.md section 3). Microsoft: "Gets the feature level of the
  hardware device" (from memory). Status `full`, no stop of its own.
- Test: the value 0xb000 on a device from `rt_nulldev_create`, written as the number.
"""),
    dict(id="device-format-queries", methods=[(DEV, "CheckFormatSupport"), (DEV, "CheckMultisampleQualityLevels")],
         excerpt=["NdDevice", "rt_nulldev_format_support", "ND_MAX_SAMPLE_COUNT"], notes="""\
- Both answers start from what the native renderer reported on this Mac (finding 35,
  docs/findings/35-dxmt-build.md section 3). `rt_nulldev_format_support(Format, &bits)` has
  the 32 formats that were measured; begin both bodies with
  `(void)rt_com_state(ctx, This, &rt_nulldev_device_class);`.
- `CheckFormatSupport`: a measured format: `*pFormatSupport = bits`, `S_OK`. A format that
  was not measured is a stop, `RT_UNIMPLEMENTED(ctx, "CheckFormatSupport for format %u", Format)`.
  What Windows does there, for `RT-DOC`: `E_FAIL` with 0 written for every format the device
  does not support, format 0 and ~0u included (Wine's conformance test d3d11.c,
  `test_format_support`); Microsoft's page says "E_INVALIDARG if the Format parameter is NULL,
  or returns E_FAIL if the described format does not exist", and the test saw no
  `E_INVALIDARG`. Which of the unmeasured formats this Mac lacks is not known, so neither is
  returned. A NULL `pFormatSupport` is a stop,
  `RT_UNIMPLEMENTED(ctx, "CheckFormatSupport with a NULL out-pointer")` (no source says what
  Windows returns).
- `CheckMultisampleQualityLevels`, in this order:
  1. a format that was not measured is a stop, `"CheckMultisampleQualityLevels for format %u"`
     (Windows, same Wine file, `test_check_multisample_quality_levels`: 65536 gives
     `E_INVALIDARG` and writes nothing, format 0 gives `S_OK` and 0; not returned here for the
     reason above);
  2. a NULL `pNumQualityLevels` returns `E_INVALIDARG` (that test: NULL with 0, 1 and 2
     samples, so the pointer is looked at before the count);
  3. a `SampleCount` of 0 or above 32 writes 0 and returns `E_FAIL` (that test: 0, 33, 64);
  4. otherwise the result is `S_OK` and `*pNumQualityLevels` is: 1 for 1 sample; for 2 or 4
     samples, 1 when the format's measured bits contain
     `D3D11_FORMAT_SUPPORT_MULTISAMPLE_RENDERTARGET` (0x200000) and 0 when they do not; 0 for
     every other count up to 32.
  Sources of step 4: the probe's line for five formats, "x1: 1 x2: 1 x4: 1 x8: 0 x16: 0"
  (`ND_MAX_SAMPLE_COUNT`); the native renderer answers by the sample count alone, 1 where the
  GPU has it (its source, third_party/dxmt `src/d3d11/d3d11_device.cpp`,
  `CheckMultisampleQualityLevels1`). Windows answers 0 for a format that cannot be a
  multisampled target (that test: `BC3_UNORM` with 2 samples gives `S_OK` and 0; Microsoft's
  page: an unsupported combination "returns a failure HRESULT code ... or sets
  pNumQualityLevels output parameter to zero, or both"), and the 14 BC formats and `R32_UINT`
  lack that bit in what was measured: there this body answers as Windows does, not as the
  native renderer would.
- Test, with the numbers written out (not through the function): for `CheckFormatSupport`
  format 28 (`R8G8B8A8_UNORM`) gives 0x02fef3f3, 71 (`BC1_UNORM`) gives 0x00d213f0, 45
  (`D24_UNORM_S8_UINT`) gives 0x04f717f0, 42 (`R32_UINT`) gives 0x02d273ff, and format 2
  (`R32G32B32A32_FLOAT`) gives 0x02f6f3fb; a stop for format 1 (`R32G32B32A32_TYPELESS`, not
  measured) and for the NULL pointer. For the other method, the count preset to 0xdeadbeef
  before each call: format 28 with 1, 2, 4 gives `S_OK` and 1, with 8, 16, 3, 15 and 32
  `S_OK` and 0; format 45 with 4 gives 1 and format 40 (`D32_FLOAT`) with 4 gives 1; format 71
  with 2 gives `S_OK` and 0, with 1 gives 1; format 42 with 4 gives `S_OK` and 0; format 28
  with 0 and with 33 gives `E_FAIL` and 0; format 28 with 2 and a NULL pointer, and with 0
  and a NULL pointer, gives `E_INVALIDARG`; a stop for format 1 with 2 samples. Compare
  results with the constants `S_OK`, `E_FAIL`, `E_INVALIDARG` (0, 0x80004005, 0x80070057).
"""),
    # ---- the context's recorded bindings: a Set and the Get that reads it back ----
    _slots("ctx-vs-constant-buffers", "VS", "ConstantBuffers", "vs_cb", "rt_nulldev_buffer_class", "ND_CB_SLOTS", 14, "NumBuffers", "ppConstantBuffers"),
    _slots("ctx-ps-constant-buffers", "PS", "ConstantBuffers", "ps_cb", "rt_nulldev_buffer_class", "ND_CB_SLOTS", 14, "NumBuffers", "ppConstantBuffers"),
    _slots("ctx-ps-shader-resources", "PS", "ShaderResources", "ps_srv", "rt_nulldev_srv_class", "ND_SRV_SLOTS", 128, "NumViews", "ppShaderResourceViews",
           extra=PS_SRV_HAZARD, names=["rt_nulldev_is_target"]),
    _slots("ctx-ps-samplers", "PS", "Samplers", "ps_sampler", "rt_nulldev_sampler_class", "ND_SAMPLER_SLOTS", 16, "NumSamplers", "ppSamplers"),
    _shader("ctx-vs-shader", "VS", "vs", "rt_nulldev_vs_class", "pVertexShader"),
    _shader("ctx-ps-shader", "PS", "ps", "rt_nulldev_ps_class", "pPixelShader"),
    _shader("ctx-gs-shader", "GS", "gs", "rt_nulldev_gs_class", "pShader"),
    _nostage("ctx-hs-shader", "HS", "hs", "ID3D11HullShader", "pHullShader"),
    _nostage("ctx-ds-shader", "DS", "ds", "ID3D11DomainShader", "pDomainShader"),
    dict(id="ctx-input-layout", methods=[(CTX, "IASetInputLayout"), (CTX, "IAGetInputLayout")],
         excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new"],
         notes=ONE.format(field="input_layout", cls="rt_nulldev_inputlayout_class", arg="pInputLayout", out="ppInputLayout", get="IAGetInputLayout")),
    dict(id="ctx-rasterizer-state", methods=[(CTX, "RSSetState"), (CTX, "RSGetState")],
         excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new"],
         notes=ONE.format(field="rasterizer", cls="rt_nulldev_rasterizer_class", arg="pRasterizerState", out="ppRasterizerState", get="RSGetState")),
    dict(id="ctx-topology", methods=[(CTX, "IASetPrimitiveTopology"), (CTX, "IAGetPrimitiveTopology")], excerpt=["NdContext"], notes="""\
- State: `NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);` in both. Both
  return nothing.
- Set: `c->topology = Topology` for the values 0 to 5 and 10 to 13 (undefined, the point,
  line and triangle lists and strips, and their four adjacency forms: `rt/win/d3d11.h`,
  `D3D11_PRIMITIVE_TOPOLOGY_*`). Any other value is a stop,
  `RT_UNIMPLEMENTED(ctx, "IASetPrimitiveTopology with topology %u", Topology)`: the patch
  lists belong to tessellation, which the game never uses (finding 31, section 5), and the
  rest are no enumerators.
- Get: `*pTopology = c->topology`; it reads 0 (`D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED`) before
  any Set (Microsoft's page of ClearState: the topology's default is undefined; from memory).
  A NULL `pTopology` is a stop, `RT_UNIMPLEMENTED(ctx, "IAGetPrimitiveTopology with a NULL out-pointer")`.
- Test: 0 on a new context; set 4, get 4; set 13, get 13; stops for 6, 33 and the NULL pointer.
"""),
    dict(id="ctx-render-targets", methods=[(CTX, "OMSetRenderTargets"), (CTX, "OMGetRenderTargets")],
         excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new",
                  "rt_nulldev_unbind_readers"], notes=CONTEXT + """\
- Set: for i below `NumViews` bind `c->rtv[i]` to `ppRenderTargetViews[i]` (through
  `rt_nulldev_arg` with `&rt_nulldev_rtv_class`); every slot from `NumViews` to 7 is bound
  to NULL (Microsoft: "Any render targets not defined by this call are set to NULL"; read
  10 October by unit d3d-rows-fix); `c->dsv` is bound to `pDepthStencilView`
  (`&rt_nulldev_dsv_class`), NULL included.
  Stops: `NumViews` above `ND_RT_SLOTS` (8), `"OMSetRenderTargets with %u views"`; a NULL
  array with `NumViews` above 0, `"OMSetRenderTargets with a NULL array"`. The array is
  described with `const` elements: pass `(void *)ppRenderTargetViews[i]` to `rt_nulldev_arg`.
- Then, still in the Set: a subresource bound for writing is not also bound for reading. For
  each view the call bound, `rt_nulldev_unbind_readers(ctx, c, c->rtv[i])` for i below
  `NumViews` and `rt_nulldev_unbind_readers(ctx, c, c->dsv)` (NULL does nothing): it binds
  NULL in every pixel-shader resource slot whose view overlaps that target. Source: the same
  page, "If any subresources are also currently bound for reading in a different stage or
  writing (perhaps in a different part of the pipeline), those bind points will be set to
  NULL", and Wine's conformance test d3d11.c, `test_sample_attached_rtv` (a bound view reads
  back NULL once its texture is set as render target).
- Get: when `ppRenderTargetViews` is not NULL, `ppRenderTargetViews[i] = rt_nulldev_ref(c->rtv[i])`
  for i below `NumViews`; when `ppDepthStencilView` is not NULL,
  `*ppDepthStencilView = rt_nulldev_ref(c->dsv)` (Microsoft lets either pointer be NULL when
  that part is not wanted: checked by docs/reviews/d3d-contract-check.md, M5). Stop:
  `NumViews` above 8, `"OMGetRenderTargets with %u views"`.
""" + VIEW_OF + """\
- Test: also that setting one view after two leaves slot 1 NULL and released. And for the
  Set's second rule: with shader-resource views put in the record by the test,
  `rt_nulldev_bind(&c->ps_srv[3], srv)`, a Set whose render-target view is a view of the
  same texture leaves `c->ps_srv[3]` NULL and that view's count down by one, while a view
  of a second texture in `c->ps_srv[4]` stays; the same through the depth-stencil view.
"""),
    dict(id="ctx-render-targets-uavs", methods=[(CTX, "OMSetRenderTargetsAndUnorderedAccessViews")],
         excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_new", "rt_nulldev_is_target",
                  "rt_nulldev_unbind_readers"], notes=CONTEXT + """\
- Source of everything below: the remarks of Microsoft's page of the method (read 10 October
  by unit d3d-rows-fix). Write `keep_rt` for `NumRTVs ==
  D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL` and `keep_uav` for `NumUAVs ==
  D3D11_KEEP_UNORDERED_ACCESS_VIEWS` (both 0xffffffff). `pUAVInitialCounts` is not used: `(void)`.
- Stops come first, in this order, before anything is bound (begin each message with the
  method's name; the page lists 1 to 3 as what "must be true ... for the runtime to pass the
  bind information to the driver"):
  1. not `keep_rt` and `NumRTVs` above 8: `"... with %u views"`;
  2. not `keep_uav` and `(uint64_t)UAVStartSlot + NumUAVs` above
     `D3D11_PS_CS_UAV_REGISTER_COUNT` (8): `"... with unordered-access views in slots %u to %u"`;
  3. neither keep value, and `UAVStartSlot` below `NumRTVs`:
     `"... with UAVs from slot %u and %u render targets"`;
  4. not `keep_rt`, `NumRTVs` above 0 and a NULL `ppRenderTargetViews`: `"... with a NULL array"`;
  5. `keep_rt` and a render target recorded in a slot from `UAVStartSlot` up (some
     `c->rtv[i]` not NULL with `UAVStartSlot <= i < 8`):
     `"... keeping render targets with one bound in slot %u"`. The page says of the keep value
     both that the method "does not modify the currently bound render-target views" and that
     it unbinds "All RTVs in slots >= UAVStartSlot"; no test settles which, so the body
     stops exactly where the two sentences differ.
- Render targets and depth. Not `keep_rt`: as `OMSetRenderTargets` does: `c->rtv[i]` bound to
  `(void *)ppRenderTargetViews[i]` (through `rt_nulldev_arg` with `&rt_nulldev_rtv_class`;
  both arrays are described with `const` elements, hence the cast) for i below `NumRTVs` and to NULL from `NumRTVs` to 7; `c->dsv` bound to `pDepthStencilView`
  (`&rt_nulldev_dsv_class`), NULL included; then `rt_nulldev_unbind_readers(ctx, c, view)`
  for each `c->rtv[i]` below `NumRTVs` and for `c->dsv` (the page: the method "Unbinds all
  currently bound conflicting resources", shader-resource views among them). `keep_rt`:
  `c->rtv` and `c->dsv` stay as they are and `ppRenderTargetViews` and `pDepthStencilView`
  are not looked at (the page: it "ignores ppDepthStencilView, and the current depth-stencil
  view remains bound").
- Unordered-access views are NOT recorded: no Get of them is among the methods the game
  calls. When not `keep_uav` and `ppUnorderedAccessViews` is not NULL, for each of the
  `NumUAVs` elements: `void *u = rt_nulldev_arg(ctx, (void *)p, &rt_nulldev_uav_class,
  "ppUnorderedAccessViews")`, so that a pointer of the wrong kind still stops; then, when
  `keep_rt` and `rt_nulldev_is_target(ctx, c, u)`, a stop
  `"... with an unordered-access view of a kept render target"` (the page unbinds such
  render targets: the disagreement of stop 5 again); otherwise
  `rt_nulldev_unbind_readers(ctx, c, u)`. With `keep_uav`, `UAVStartSlot` and the array are
  not looked at. Conflicts among the views of one call (the page: "There must be no
  conflicts") are not looked for.
""" + VIEW_OF + """\
- There is no Get in this task: the test reads `c->rtv`, `c->dsv` and `c->ps_srv` through
  `rt_com_state_of(context, &rt_nulldev_context_class)` and the counts with `rt_com_refs`;
  it may put a view into `c->ps_srv[0]` with `rt_nulldev_bind`. Cases: two views and a
  depth view with `UAVStartSlot` 2 and no UAVs are recorded, slots 2 to 7 NULL; then the
  keep value with `UAVStartSlot` 2 and one UAV leaves all three bound and no count changed;
  with `keep_uav`, one view is recorded and slot 1 released, whatever `UAVStartSlot` is; a
  shader-resource view in `c->ps_srv[0]` of the texture a UAV of the call is a view of is
  unbound (its count down by one), and so is one of a render-target view's texture, while a
  view of another texture stays. `T_TRAPS` for the six stops (for 5: two views bound, then
  the keep value with `UAVStartSlot` 1; for the sixth: the same two bound, then the keep
  value with `UAVStartSlot` 2 and a UAV of the texture of the view in slot 0) and for a buffer passed as a UAV ("is an object
  of class").
"""),
    dict(id="ctx-blend-state", methods=[(CTX, "OMSetBlendState"), (CTX, "OMGetBlendState")],
         excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new"], notes=CONTEXT + """\
- Set: bind `c->blend` to `pBlendState` (`&rt_nulldev_blend_class`; NULL unbinds);
  `c->blend_factor` = the four floats of `BlendFactor`, or {1, 1, 1, 1} when it is NULL
  (Microsoft: "If you pass NULL, the runtime uses or stores a blend factor equal to
  { 1, 1, 1, 1 }"; from memory); `c->sample_mask = SampleMask`.
- Get: each of the three out-pointers that is not NULL receives its value:
  `*ppBlendState = rt_nulldev_ref(c->blend)`, four floats to `BlendFactor`, `*pSampleMask`.
  (Microsoft lets each be NULL; from memory.) No stop: status `full`.
- A new context reads NULL, {1, 1, 1, 1} and 0xffffffff (`D3D11_DEFAULT_SAMPLE_MASK`): the
  record starts so (`rt_nulldev_context_reset`).
"""),
    dict(id="ctx-depth-stencil-state", methods=[(CTX, "OMSetDepthStencilState"), (CTX, "OMGetDepthStencilState")],
         excerpt=["NdContext", "rt_nulldev_arg", "rt_nulldev_bind", "rt_nulldev_ref", "rt_nulldev_new"], notes=CONTEXT + """\
- Set: bind `c->depth_stencil` to `pDepthStencilState` (`&rt_nulldev_depthstencil_class`);
  `c->stencil_ref = StencilRef`.
- Get: each out-pointer that is not NULL receives its value:
  `*ppDepthStencilState = rt_nulldev_ref(c->depth_stencil)`, `*pStencilRef = c->stencil_ref`.
  No stop: status `full`. A new context reads NULL and 0.
"""),
    dict(id="ctx-viewports", methods=[(CTX, "RSSetViewports"), (CTX, "RSGetViewports")], excerpt=["NdContext"], notes="""\
- State: `NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);` in both. Both
  return nothing. `D3D11_VIEWPORT` is six floats, 24 bytes (`rt/win/d3d11.h`).
- Set: copy `NumViewports` structures to `c->viewport` and set `c->nviewports = NumViewports`
  (Microsoft: "Any viewports not defined by the call are disabled"; from memory). Stops:
  more than `ND_VIEWPORTS` (16), `"RSSetViewports with %u viewports"`; a NULL array with a
  count above 0, `"RSSetViewports with a NULL array"`.
- Get, from Microsoft's page (from memory): `pNumViewports` is in and out. With `pViewports`
  NULL, `*pNumViewports = c->nviewports` and nothing else. Otherwise the caller's count n is
  read from `*pNumViewports`; the first `c->nviewports` elements receive the viewports, the
  elements from there to n are filled with zero bytes ("unused elements of the array
  contain 0"), and `*pNumViewports = c->nviewports`. Stops: a NULL `pNumViewports`,
  `"RSGetViewports with a NULL count"`; n above 16, `"RSGetViewports for %u viewports"`; n
  below `c->nviewports` with a non-NULL array, `"RSGetViewports with room for %u of %u viewports"`
  (the page does not say what the count is then).
- Test: set two viewports with distinct numbers; get with NULL array gives 2; get with room
  for 3 gives 2, the two viewports byte for byte and 24 zero bytes after them, and nothing
  written past the third; a new context gives 0; the stops.
"""),
    dict(id="ctx-clear-state", methods=[(CTX, "ClearState")], excerpt=["NdContext", "rt_nulldev_context_reset", "rt_nulldev_bind", "rt_nulldev_new"], notes="""\
- `rt_nulldev_context_reset(rt_com_state(ctx, This, &rt_nulldev_context_class));` and nothing
  else; returns nothing; status `full`. Microsoft: "Restore all default settings" (the
  page lists every slot, shader and state set to NULL and the topology to undefined; from
  memory).
- Test: there may be no Set method yet, so write the record directly: get it with
  `rt_com_state_of(context, &rt_nulldev_context_class)`, bind a buffer to `vs_cb[3]`, a
  render-target view to `rtv[0]` and a rasterizer state with `rt_nulldev_bind`, set
  `topology` 4, `nviewports` 2, `stencil_ref` 9, `sample_mask` 5 and a blend factor; after
  the call each object's count is back to what it was before the bind, every pointer of the
  record is NULL, `topology`, `nviewports` and `stencil_ref` are 0, `sample_mask` is
  0xffffffff, the blend factor is four times 1.0f, and `child.device` is unchanged.
"""),
    # ---- objects other than the device and the context ----
    dict(id="view-resource", methods=[("ID3D11RenderTargetView", "GetResource"), ("ID3D11DepthStencilView", "GetResource")],
         excerpt=["NdView", "rt_nulldev_ref", "rt_nulldev_new"], notes="""\
- `NdView *v = rt_com_state(ctx, This, &rt_nulldev_rtv_class);` (the depth-stencil body:
  `&rt_nulldev_dsv_class`), then `*ppResource = rt_nulldev_ref(v->resource);`. Returns
  nothing. Microsoft, ID3D11View::GetResource: "This function increments the reference count
  of the resource by one, so it is necessary to call Release on the returned pointer when the
  application is done with it" (from memory). A NULL `ppResource` is a stop,
  `RT_UNIMPLEMENTED(ctx, "GetResource with a NULL out-pointer")`.
- The two bodies are separate files, `runtime/nulldev/ID3D11RenderTargetView/GetResource.c`
  and `runtime/nulldev/ID3D11DepthStencilView/GetResource.c`, each with
  `COM_METHOD(<its interface>, void, GetResource, ...)`.
- Test: a view from `rt_nulldev_new(device, &rt_nulldev_rtv_class)`, a texture from
  `rt_nulldev_new(device, &rt_nulldev_texture2d_class)`; give the view its resource the way
  a creator will: `rt_nulldev_bind(&v->resource, texture)` with `v` from `rt_com_state_of`.
  Then GetResource returns that pointer and the texture's count is one higher; a view whose
  resource is NULL gives NULL; the stop.
"""),
    dict(id="texture2d-desc", methods=[("ID3D11Texture2D", "GetDesc")], excerpt=["NdTexture", "rt_nulldev_new"], notes="""\
- `NdTexture *t = rt_com_state(ctx, This, &rt_nulldev_texture2d_class);` then
  `*pDesc = t->desc.d2;`: the description the texture was created with (Microsoft: "Get the
  properties of the texture resource"; that `MipLevels` comes back as the real count when 0
  was asked is the creator's business and from memory). Returns nothing. A NULL `pDesc` is a
  stop, `RT_UNIMPLEMENTED(ctx, "GetDesc with a NULL description")`.
- Test: a texture from `rt_nulldev_new(device, &rt_nulldev_texture2d_class)`; fill its
  `desc.d2` through `rt_com_state_of` with eleven distinct numbers; GetDesc into a buffer of
  44 + 8 bytes pre-filled with 0xa5 returns those 44 bytes and leaves the 8 after them; the
  stop. (`D3D11_TEXTURE2D_DESC` is 44 bytes: `rt/win/d3d11.h`.)
"""),
    dict(id="swapchain-desc", methods=[("IDXGISwapChain", "GetDesc")], excerpt=["NdSwapChain", "rt_nulldev_new"], notes="""\
- `NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);` then
  `*desc = s->desc;` and `S_OK`: the description the swap chain was created with (the game
  reads the size from it at five sites: design-null-device.md, section 6). A NULL `desc` is a
  stop, `RT_UNIMPLEMENTED(ctx, "GetDesc with a NULL description")` (Microsoft names a failure
  code for it that was not read).
- Test: a swap chain from `rt_nulldev_new(device, &rt_nulldev_swapchain_class)`; fill its
  `desc` through `rt_com_state_of` with distinct numbers in every field; GetDesc into a
  buffer of 72 + 8 bytes pre-filled with 0xa5 returns those 72 bytes and leaves the 8 after
  them; the stop. (`DXGI_SWAP_CHAIN_DESC` is 72 bytes: `rt/win/dxgi.h`.)
"""),
    dict(id="swapchain-fullscreen", methods=[("IDXGISwapChain", "SetFullscreenState"), ("IDXGISwapChain", "GetFullscreenState")],
         excerpt=["NdSwapChain", "rt_nulldev_new"], notes="""\
- This device's swap chain is always windowed: the game creates it windowed (finding 31,
  section 2) and there is no display to take over. State:
  `NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);`, field `fullscreen`,
  which is and stays FALSE.
- `SetFullscreenState(FALSE, NULL)`: `S_OK`, nothing changes (it is windowed already).
  `SetFullscreenState` with `fullscreen` not 0 is a stop,
  `RT_UNIMPLEMENTED(ctx, "SetFullscreenState(TRUE)")`: what a device with no display answers
  has to be decided by the orchestrator first (docs/handoff/d3d-contract.md, open decisions).
  `fullscreen` 0 with a non-NULL `target` is a stop,
  `RT_UNIMPLEMENTED(ctx, "SetFullscreenState(FALSE) with a target output")` (Microsoft: the
  target must be NULL for windowed mode; the page, read by the proof's worker on 10 October,
  names no result for it).
- `GetFullscreenState`: each out-pointer that is not NULL receives its value:
  `*fullscreen = s->fullscreen` (FALSE) and `*target = NULL` (Microsoft: the target is the
  output "when the mode is full screen; otherwise NULL"; both pointers `[out, optional]`;
  confirmed on the page by the proof's worker, 10 October); `S_OK`. No stop of its own.
- Test: a swap chain from `rt_nulldev_new(device, &rt_nulldev_swapchain_class)`; Get writes
  FALSE and NULL over other values and tolerates each pointer being NULL; Set(FALSE, NULL)
  is S_OK and Get still says FALSE; the two stops (for the second pass any object as target).
"""),
    dict(id="swapchain-output", methods=[("IDXGISwapChain", "GetContainingOutput")],
         excerpt=["NdSwapChain", "NdDevice", "rt_nulldev_output_new", "rt_nulldev_new"], notes="""\
- There is one output (design-null-device.md, section 3.4), so it contains every window:
  `*output = rt_nulldev_output_new(d->adapter)` and `S_OK`, where
  `NdSwapChain *s = rt_com_state(ctx, This, &rt_nulldev_swapchain_class);` and
  `NdDevice *d = rt_com_state_of(s->device, &rt_nulldev_device_class);`. A new output object
  per call, with one reference for the caller (Microsoft: the output "that contains the
  majority of the client area of the target window"; from memory). A NULL `output` is a stop,
  `RT_UNIMPLEMENTED(ctx, "GetContainingOutput with a NULL out-pointer")`.
- Test: a swap chain from `rt_nulldev_new(device, &rt_nulldev_swapchain_class)`; the result
  is an object of `rt_nulldev_output_class` with count 1 whose record's `adapter` is the
  device's; two calls give two objects; the stop.
"""),
    dict(id="adapter-interface-support", methods=[("IDXGIAdapter1", "CheckInterfaceSupport")], excerpt=["NdAdapter"], notes="""\
- Microsoft's page (read 2026-10-10): "S_OK indicates that the interface is supported,
  otherwise DXGI_ERROR_UNSUPPORTED is returned", and: "If you try to use
  CheckInterfaceSupport to check whether a Direct3D 11.x and later version interface is
  supported, CheckInterfaceSupport returns DXGI_ERROR_UNSUPPORTED."
- So: begin with `(void)rt_com_state(ctx, This, &rt_nulldev_adapter_class);`. When
  `*guid` equals `rt_iface_ID3D11Device.iid` (compare the 16 bytes with `memcmp`) return
  `DXGI_ERROR_UNSUPPORTED` and do not write `umd_version` ("returned only if the interface
  is supported").
- Every other GUID is a stop,
  `RT_UNIMPLEMENTED(ctx, "CheckInterfaceSupport for {%08x-...}", (unsigned)guid->Data1)`:
  for a Direct3D 10 interface the answer would be S_OK with a driver version number, and
  this device has no driver version to give. A NULL `guid` is a stop,
  `RT_UNIMPLEMENTED(ctx, "CheckInterfaceSupport with a NULL interface")`.
- Test: the adapter is `((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->adapter`;
  the result 0x887a0004 for ID3D11Device's IID with the version variable left as the test
  set it; the two stops (for the first use `rt_iface_IDXGIDevice1.iid`).
"""),
    dict(id="dxgi-frame-latency", methods=[("IDXGIDevice1", "SetMaximumFrameLatency")], excerpt=["NdDevice"], notes="""\
- `NdDevice *s = rt_com_state(ctx, This, &rt_nulldev_device_class);` (the DXGI device is the
  Direct3D device's second face: the same object, the same record). Field
  `max_frame_latency`, which starts at 3.
- Microsoft (from memory): "The value defaults to 3, but can range from 1 to 16. A value of
  0 will reset latency to the default." So 1 to 16 is stored, 0 stores 3, and the result is
  `S_OK`. Above 16 is a stop,
  `RT_UNIMPLEMENTED(ctx, "SetMaximumFrameLatency(%u)", MaxLatency)`: the page names a failure
  code for it that was not read. Nothing presents frames, so the number has no other effect.
- Test: get the interface with `COM_CALL(ID3D11Device, QueryInterface, device,
  &rt_iface_IDXGIDevice1.iid, (void **)&dxgi)`; read the field through
  `rt_com_state_of(device, &rt_nulldev_device_class)`: 3 at first, 1 after 1, 16 after 16, 3
  after 0; the stop for 17.
"""),
    dict(id="output-closest-mode", methods=[("IDXGIOutput", "FindClosestMatchingMode")], excerpt=["NdOutput", "rt_nulldev_output_new"],
         more=[("rt/window.h", ["rt_desktop"])], notes="""\
- The output has ONE display mode, the desktop's (`runtime/nulldev/IDXGIOutput/GetDisplayModeList.c`
  says why), so the closest match to anything is that mode. Begin with
  `(void)rt_com_state(ctx, This, &rt_nulldev_output_class);`, include `"rt/window.h"`, and
  write to `*closest_match`: `Width` and `Height` from `rt_desktop()`, `RefreshRate`
  `refresh_hz` over 1, `Format` = `mode->Format`, `ScanlineOrdering`
  `DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE`, `Scaling` `DXGI_MODE_SCALING_UNSPECIFIED`; `S_OK`.
- Answered only when `mode->Format` is `DXGI_FORMAT_R8G8B8A8_UNORM` (28), the one format the
  mode list answers for. Any other format is a stop,
  `RT_UNIMPLEMENTED(ctx, "FindClosestMatchingMode for format %u", mode->Format)` (for format 0
  Microsoft takes the format from the device, which is not built). A NULL `mode` or
  `closest_match` is a stop, `RT_UNIMPLEMENTED(ctx, "FindClosestMatchingMode with a NULL mode")`.
  `device` is not used: `(void)device;` (Microsoft: it may be NULL when the format is given;
  from memory).
- Test: the output is `rt_nulldev_output_new(((NdDevice *)rt_com_state_of(device,
  &rt_nulldev_device_class))->adapter)`; ask for 640 by 480 at 75 Hz in format 28 and get the
  seven numbers 1920, 1080, 60, 1, 28, 1, 0 (the window contract's desktop; 28 bytes, nothing
  written after them); the stops for format 0 and for a NULL `closest_match`.
"""),
]
# One line per row for the contract table (the brief has the full text).
SUMMARY = {
    "device-feature-level": "returns 0xb000, the level the device was made with. F35; MS (from memory)",
    "device-format-queries": "the measured support bits for 32 formats, S_OK; quality levels 1 for 1 sample, 1 for 2 and 4 on a format whose bits say multisampled target (0 on the 14 BC formats and R32_UINT, as Windows answers), 0 otherwise; 0 or more than 32 samples E_FAIL, a NULL count pointer E_INVALIDARG; an unmeasured format is a stop. F35; Wine d3d11.c test_check_multisample_quality_levels, test_format_support (read 10 Oct)",
    "ctx-vs-constant-buffers": "Set records 14 slots holding a reference each, Get hands them out with one more; out-of-range slots and a NULL array are stops. MS (read by the proof's worker 10 Oct)",
    "ctx-ps-constant-buffers": "as the vertex stage's pair, in its own 14 slots. MS (from memory)",
    "ctx-ps-shader-resources": "the same over 128 slots of shader-resource views, except that a view overlapping a bound render target or written depth target is not bound: its slot reads NULL (rt_nulldev_is_target). MS PSSetShaderResources (read 10 Oct); Wine d3d11.c test_sample_attached_rtv",
    "ctx-ps-samplers": "the same over 16 slots of sampler states. MS (from memory)",
    "ctx-vs-shader": "Set records the shader (one reference), Get hands it out with one more and reports 0 class instances; class instances are a stop. MS (from memory); F31 section 4",
    "ctx-ps-shader": "as the vertex shader's pair. MS (from memory)",
    "ctx-gs-shader": "as the vertex shader's pair. MS (from memory)",
    "ctx-hs-shader": "no hull shader can exist: Set accepts NULL and stops on anything else, Get writes NULL and 0 instances. F31 section 5",
    "ctx-ds-shader": "no domain shader can exist: as the hull stage's pair. F31 section 5",
    "ctx-input-layout": "Set records the layout (one reference), Get hands it out with one more. MS (from memory)",
    "ctx-rasterizer-state": "Set records the state (one reference), Get hands it out with one more. MS (from memory)",
    "ctx-topology": "Set records the value for the ten list, strip and adjacency topologies and stops on any other; Get returns it, 0 before any Set. MS (from memory)",
    "ctx-render-targets": "Set records up to 8 views and the depth view, the rest NULL, and binds NULL in every pixel-shader resource slot whose view overlaps one of them (rt_nulldev_unbind_readers); Get hands them out with one more reference each, either pointer optional. MS OMSetRenderTargets (read 10 Oct); Wine d3d11.c test_sample_attached_rtv",
    "ctx-render-targets-uavs": "render targets as OMSetRenderTargets (with its unbinding of overlapping shader-resource slots), or kept for 0xffffffff; unordered-access views are checked for their class, unbind overlapping shader-resource slots and are not recorded; stops: more than 8 views, UAV slots past 8, UAVStartSlot below NumRTVs, and the keep value with a render target bound from UAVStartSlot up or under a given UAV (the page says both that they are kept and that they are unbound). MS OMSetRenderTargetsAndUnorderedAccessViews, remarks (read 10 Oct)",
    "ctx-blend-state": "Set records state, factor (NULL means four ones) and mask; Get writes each through the out-pointers that are not NULL. MS (from memory)",
    "ctx-depth-stencil-state": "Set records state and stencil reference; Get writes each through the out-pointers that are not NULL. MS (from memory)",
    "ctx-viewports": "Set records up to 16 viewports and their number; Get returns the number, the viewports, zeroes after them; less room than viewports is a stop. MS (from memory)",
    "ctx-clear-state": "everything recorded back to the defaults, every held reference released. MS (from memory)",
    "view-resource": "the resource the view was created on, with one more reference. MS (from memory); model",
    "texture2d-desc": "the 44 bytes of the description the texture was created with. MS; the creator's record",
    "swapchain-desc": "the 72 bytes of the description the swap chain was created with, S_OK. The creator's record; design section 6",
    "swapchain-fullscreen": "Get: FALSE and a NULL target, S_OK; Set(FALSE, NULL): S_OK; Set(TRUE): a stop (open decision D1). MS (read by the proof's worker 10 Oct); F31 section 2",
    "swapchain-output": "a new object of the one output, S_OK. design 3.4; MS (from memory)",
    "adapter-interface-support": "DXGI_ERROR_UNSUPPORTED for ID3D11Device's IID, the version not written; any other GUID is a stop. MS (read 10 Oct)",
    "dxgi-frame-latency": "stores 1 to 16, 0 stores the default 3, S_OK; above 16 is a stop. MS (from memory)",
    "output-closest-mode": "the one mode of the list (the desktop's) for format 28, S_OK; any other format is a stop. The built mode list; MS (from memory)",
}

for _r in ROWS:
    # The proof row (written 10 October from its brief alone) is the model of the other rows
    # that bind into the context's record.
    if _r["id"].startswith("ctx-") and _r["id"] != "ctx-vs-constant-buffers":
        _r.setdefault("model", "ID3D11DeviceContext/VSSetConstantBuffers")
    _r.setdefault("model", "IDXGIAdapter1/EnumOutputs")
    _r.setdefault("needs", ["d3d-contract"])

BRIEF = """# Unit d3d-{id}: {names}

This repository recompiles a 64-bit Windows game for arm64 macOS. Where the game expects a
Direct3D 11 device it gets our own objects, a device that draws nothing. Each method of
those objects is one small C function, a *body*, in a file of its own; a method with no body
stops the run. Write the body and its test for: {names}. **You do not decide what a method
answers: the notes below do, with where each answer comes from. Write exactly that.**

## Files (create exactly these, edit nothing else)

{file_list}

Nothing registers them: the build finds them by their path.

## Read, in this order, and nothing else unless you get stuck

1. `runtime/nulldev/{model}.c` and `runtime/tests/nulldev/{model}.c`: a finished body and
   its test, to copy the shape from (the four `RT-` tags, the `COM_METHOD` line, `TEST`,
   `T_CASE`, `COM_CALL`, `T_TRAPS`).
2. The Microsoft documentation page of each method, if you have a tool that fetches web pages
   (`learn.microsoft.com` only): put its address in `RT-DOC`. If the page contradicts a note
   below, write what the note says and report the difference in your reply. If you cannot
   fetch it, write `RT-DOC` as
   `https://learn.microsoft.com/windows/win32/api/<header>/nf-<header>-<interface>-<method>`
   in lower case (`d3d11` or `dxgi`) and say in your reply that the page was not read.

## The `COM_METHOD` line of each body

Copy it as it is, on ONE line starting in column 1 (the build compares it with the
interface's description; `ctx` and `This` are in scope inside the function):

{com_lines}

## What you use

From `runtime/include/rt/nulldev.h` (include it as `"rt/nulldev.h"`; it brings the Direct3D
structures and constants of `runtime/include/rt/win/d3d11.h` and `dxgi.h`). Do not
re-implement any of it:

```c
{excerpt}
```

Also, from `rt/com.h`: `rt_com_state(ctx, This, &<class>)` gives the record of `This` and
stops unless `This` is a live object of that class (begin every body with it);
`rt_com_state_of(obj, &<class>)` is the same for a test; `rt_com_refs(obj)` is an object's
reference count; `rt_com_class_of(obj)` its class. In a test, `rt_nulldev_create(&device,
&context)` makes a device and its context, and `COM_CALL(<Interface>, <Method>, object,
arguments...)` calls a method as the game does.

If a type, constant or function you need does not exist, do not add it and do not work
around it: stop and say exactly what is missing.

## Rules

- **Never guess.** A case the notes do not settle is `RT_UNIMPLEMENTED(ctx, "...")` (status
  `partial`, listed in `RT-TRAPS`) and goes in your reply.
- `RT-STATUS` is `full` when the body has no `RT_UNIMPLEMENTED` (then `RT-TRAPS: none`),
  otherwise `partial`. Each `RT-TRAPS` entry is a piece of the message of one
  `RT_UNIMPLEMENTED`, entries separated by `;`, and the test proves each with
  `T_TRAPS(<the call>, "<text that contains the entry>")`, placed after the checks of the
  last case (a `T_TRAPS` gets no `T_CASE` of its own).
- Every `T_CASE` text is an `RT-CASES` entry, spelled the same; entries are separated by `;`.
- An argument the body never uses gets `(void)name;`: the build has no warnings.
- The body is thin: no `malloc`, no other method's body, no vtable. The test checks what the
  game can see: return values, every byte written, reference counts. At the top of the test
  say where each expected value comes from; it never comes from the body.
- Three things nobody in this project does: read or run `OriginSDK.dll` or `Titanfall2.exe`;
  touch anything that calls EA's Origin SDK or looks like a licence check; run a Windows
  executable. This task needs nothing under `game/`: do not open it.

Notes for {these}:

{notes}
## Done

Each of these prints eight numbered items and exits 0 when all pass, on a clean, committed
worktree. Run each as you finish that method and commit when it passes (pipe the output
through `tail -20`{family}; a run builds and runs the whole suite, a few minutes). Item 1
compares with the branch your worktree was cut from: only your files may differ. Item 7 wants
every `RT-TRAPS` entry reached by a `T_TRAPS` of the test:

{check_block}
"""


# ---- reading the tree --------------------------------------------------------------------------------

def used():
    """The 146 (interface, method) pairs of finding 31."""
    j = json.load(open(os.path.join(ROOT, "docs", "findings", "31-d3d11-usage.json")))
    return [(i, m["method"]) for i, v in sorted(j["modules"]["materialsystem_dx11"]["interfaces"].items())
            for m in v["called"]]


def descriptions():
    """{(interface, method): (declared by, 'HRESULT Name(params)')} for every described slot."""
    out = {}
    d = os.path.join(ROOT, "runtime", "com", "desc")
    for name in sorted(os.listdir(d)):
        if not name.endswith(".idef"):
            continue
        for line in open(os.path.join(d, name)):
            m = re.match(r"\s*\d+\s+(\w+)\s+(.*?\))\s*(#.*)?$", line)
            if m:
                out[(name[:-5], re.search(r"(\w+)\(", m.group(2)).group(1))] = (m.group(1), m.group(2))
    return out


def com_line(iface, method, desc):
    proto = desc[(iface, method)][1]
    m = re.fullmatch(r"(.+?)\s*\b(\w+)\((.*)\)", proto)
    params = m.group(3).strip()
    ret = m.group(1).strip()
    return "COM_METHOD(%s)" % ", ".join([iface, ret, method] + ([] if params in ("", "void") else [params]))


def accept_lines():
    out = set()
    for raw in open(os.path.join(ROOT, "runtime", "com", "accept.def")):
        line = raw.split("#", 1)[0].strip()
        if line:
            out.add(tuple(line.split("::")))
    return out


def body_exists(iface, method):
    return os.path.exists(os.path.join(ROOT, "runtime", "nulldev", iface, method + ".c"))


def excerpt(path, names):
    """Each name's definition in a header: a struct (to its closing line), a #define, or a
    function with the comment above it."""
    lines = open(os.path.join(ROOT, "runtime", "include", path)).read().split("\n")
    out = []
    for name in names:
        hit = None
        for i, l in enumerate(lines):
            if l.lstrip().startswith(("*", "/*")):
                continue
            if re.match(r"typedef struct %s\b" % re.escape(name), l) or re.match(r"#define\s+%s\b" % re.escape(name), l) \
                    or re.search(r"\b%s\s*\(" % re.escape(name), l):
                hit = i
                break
        if hit is None:
            raise KeyError("runtime/include/%s does not define %s" % (path, name))
        a = hit
        while a > 0 and lines[a - 1].strip() and lines[a - 1].lstrip().startswith(("/*", "*")):
            a -= 1
        b = hit
        if lines[hit].startswith("typedef struct"):
            while not re.match(r"\}\s*%s;" % re.escape(name), lines[b]) and not lines[b].rstrip().endswith("} %s;" % name):
                b += 1
        elif not lines[hit].startswith("#define"):
            while ";" not in lines[b]:
                b += 1
        out.append("\n".join(lines[a:b + 1]))
    return "\n\n".join(out)


def unit_state(uid):
    path = os.path.join(ROOT, "swarm", "units", uid + ".json")
    return json.load(open(path)).get("state", "?") if os.path.exists(path) else None


def pending(r):
    return [w for w in r.get("waits", []) if unit_state(w) is None]


def written(r):
    return all(body_exists(i, m) for i, m in r["methods"])


def kinds():
    """{(interface, method): kind} over the six lists; a pair in two lists is reported by problems()."""
    out = {}
    for pair in used():
        if pair[1] in ("QueryInterface", "AddRef", "Release"):
            out[pair] = "common"
    for name, table in (("built", BUILT), ("accept", ACCEPT), ("accept", FORWARD), ("resources", RESOURCES), ("stop", STOPS)):
        for pair in table:
            out.setdefault(pair, name)
    for r in ROWS:
        for pair in r["methods"]:
            out.setdefault(pair, "row")
    return out


def counts():
    k = kinds()
    n = {name: sum(1 for v in k.values() if v == name) for name in ("common", "built", "accept", "row", "resources", "stop")}
    held = [r for r in ROWS if r.get("hold") or pending(r)]
    done = [r for r in ROWS if written(r)]
    return ("%d methods: %d common, %d accept, %d answer (%d built, %d in %d rows, %d for d3d-resources), %d stop; "
            "rows: %d written, %d held, %d ready" % (
                len(k), n["common"], n["accept"], n["built"] + n["row"] + n["resources"], n["built"], n["row"], len(ROWS),
                n["resources"], n["stop"], len(done), len(held), len(ROWS) - len(done) - len([r for r in held if r not in done])))


def problems():
    out = []
    want = set(used())
    desc = descriptions()
    seen = {}
    lists = [("BUILT", list(BUILT)), ("ACCEPT", list(ACCEPT)), ("FORWARD", list(FORWARD)), ("RESOURCES", list(RESOURCES)), ("STOPS", list(STOPS)),
             ("ROWS", [p for r in ROWS for p in r["methods"]]),
             ("COMMON", [p for p in want if p[1] in ("QueryInterface", "AddRef", "Release")])]
    for name, pairs in lists:
        for p in pairs:
            if p in seen:
                out.append("%s::%s is in %s and in %s" % (p[0], p[1], seen[p], name))
            seen[p] = name
            if p not in want:
                out.append("%s::%s (%s) is not one of finding 31's 146 methods" % (p[0], p[1], name))
            if p not in desc:
                out.append("%s::%s (%s) is not a described method" % (p[0], p[1], name))
    for p in sorted(want - set(seen)):
        out.append("%s::%s is one of the 146 and in no list" % p)
    for p in TRACED:
        if p in want or p in seen:
            out.append("%s::%s is TRACED (outside the 146) and also %s" % (p + ("one of the 146" if p in want else "in " + seen[p],)))
        if p not in desc:
            out.append("%s::%s (TRACED) is not a described method" % p)
        for kind in ("nulldev", "tests/nulldev"):
            if not os.path.exists(os.path.join(ROOT, "runtime", kind, p[0], p[1] + ".c")):
                out.append("%s::%s is TRACED and runtime/%s/%s/%s.c does not exist" % (p + (kind,) + p))
    acc = accept_lines()
    for p in ACCEPT:
        if p not in acc and (desc.get(p, ("",))[0], p[1]) not in acc:
            out.append("%s::%s is ACCEPT and runtime/com/accept.def has no line for it" % p)
    for line in acc:
        if not any(line == p or line == (desc.get(p, ("",))[0], p[1]) for p in ACCEPT):
            out.append("runtime/com/accept.def marks %s::%s, which is not in ACCEPT" % line)
    for p in FORWARD:
        if not body_exists(*p):
            out.append("%s::%s is FORWARD (an accept line until unit pic-seam) and runtime/nulldev/%s/%s.c does not exist" % (p + p))
    for p in BUILT:
        for kind in ("nulldev", "tests/nulldev"):
            if not os.path.exists(os.path.join(ROOT, "runtime", kind, p[0], p[1] + ".c")):
                out.append("%s::%s is BUILT and runtime/%s/%s/%s.c does not exist" % (p + (kind,) + p))
    for p in list(RESOURCES) + list(STOPS):
        if p in STOPS and body_exists(*p):
            out.append("%s::%s is a STOP and has a body file" % p)
    ids = set()
    for r in ROWS:
        row = "row %s" % r["id"]
        if r["id"] in ids:
            out.append("%s: the id is used twice" % row)
        ids.add(r["id"])
        if not 1 <= len(r["methods"]) <= 2:
            out.append("%s: %d methods; a row is one or two" % (row, len(r["methods"])))
        for kind in ("nulldev", "tests/nulldev"):
            if not os.path.exists(os.path.join(ROOT, "runtime", kind, r["model"] + ".c")):
                out.append("%s: the model runtime/%s/%s.c does not exist" % (row, kind, r["model"]))
        for need in r["needs"]:
            if unit_state(need) is None:
                out.append("%s: needs '%s', and there is no swarm/units/%s.json" % (row, need, need))
        try:
            excerpt("rt/nulldev.h", r.get("excerpt", []))
            for header, names in r.get("more", []):
                excerpt(header, names)
        except KeyError as e:
            out.append("%s: %s" % (row, e.args[0]))
        if r["id"] not in SUMMARY:
            out.append("%s: no line in SUMMARY for the contract table" % row)
        for m in re.findall(r"\{[a-z_]+\}", r["notes"]):
            out.append("%s: the notes still have the placeholder %s" % (row, m))
    return out


def build(r):
    desc = descriptions()
    uid = "d3d-" + r["id"]
    names = ["%s/%s" % p for p in r["methods"]]
    cmds, files = [], []
    for n in names:
        others = ",".join(x for x in names if x != n)
        cmds.append("make method-check METHOD=%s%s" % (n, " METHOD_FAMILY=" + others if others else ""))
        files += ["runtime/nulldev/%s.c" % n, "runtime/tests/nulldev/%s.c" % n]
    text = excerpt("rt/nulldev.h", r.get("excerpt", []))
    for header, hnames in r.get("more", []):
        text += '\n\n/* ---- from runtime/include/%s (include it as "%s" too) ---- */\n\n' % (header, header) + excerpt(header, hnames)
    pretty = ", ".join("`%s::%s`" % p for p in r["methods"])
    brief = BRIEF.format(id=r["id"], names=pretty, file_list="\n".join("- `%s`" % f for f in files), model=r["model"],
                         com_lines="\n".join("    " + com_line(i, m, desc) for i, m in r["methods"]), excerpt=text,
                         notes=r["notes"], these="these methods" if len(names) > 1 else "this method",
                         check_block="\n".join("    " + c for c in cmds),
                         family="; `METHOD_FAMILY` names the other method of this task, whose two files may differ "
                                "from the base as well" if len(names) > 1 else "")
    unit = {"id": uid, "title": "Do-nothing device: %s" % ", ".join("%s::%s" % p for p in r["methods"]), "kind": "method-task",
            "issue": r.get("issue", 31), "model": "haiku", "priority": 20,
            "needs": r["needs"] + [w for w in r.get("waits", []) if w not in pending(r)], "check": cmds, "state": "todo"}
    return uid, brief, unit


def show(rid):
    for r in ROWS:
        if rid in (r["id"], "d3d-" + r["id"]):
            return build(r)[1]
    return None


def listing():
    for r in ROWS:
        what = " ".join("%s::%s[%s]" % (i, m, "written" if body_exists(i, m) else "-") for i, m in r["methods"])
        state = "HELD: " + (r.get("hold") or "waits for " + ", ".join(pending(r))) if r.get("hold") or pending(r) else \
            "needs " + ", ".join("%s (%s)" % (n, unit_state(n) or "NO UNIT") for n in r["needs"])
        print("%-28s %-88s %s" % ("d3d-" + r["id"], what, state))


def write():
    """Briefs and unit files for the rows that are not held (make_shim_tasks.py calls this)."""
    for r in ROWS:
        uid, brief, unit = build(r)
        if r.get("hold") or pending(r):
            print("held  %s: %s" % (uid, r.get("hold") or "waits for " + ", ".join(pending(r))))
            continue
        with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
            f.write(brief)
        path = os.path.join(ROOT, "swarm", "units", uid + ".json")
        if os.path.exists(path):
            continue
        if written(r):
            unit["state"] = "held"                      # the proof: written by unit d3d-contract
        with open(path, "w") as f:
            json.dump(unit, f, indent=2)
            f.write("\n")
        print("wrote %s (%d tokens of brief, roughly)" % (uid, len(brief) // 4))


def table():
    """The contract table, one row per method, in finding 31's order."""
    k, desc = kinds(), descriptions()
    row_of = {p: r["id"] for r in ROWS for p in r["methods"]}
    print("| Method | Kind | Answer, and where it comes from |")
    print("|---|---|---|")
    for p in used():
        kind = k.get(p, "?")
        if kind == "common":
            text = "the interface layer's (`rt/com.h`): COM's rules for `IUnknown` (from memory)"
        elif kind == "built":
            text = BUILT[p].replace(" | ", ". Source: ")
        elif kind == "accept":
            text = ACCEPT[p] if p in ACCEPT else FORWARD[p] + " (a body file since unit pic-seam: with a back end the call is handed on, rt/d3dback.h)"
        elif kind == "resources":
            text = RESOURCES[p]
        elif kind == "stop":
            text = STOPS[p]
        else:
            text = "row `d3d-%s`%s: %s" % (row_of[p], " (written)" if body_exists(*p) else "", SUMMARY[row_of[p]])
        label = {"common": "common", "built": "answer, built", "accept": "accept", "row": "answer, row",
                 "resources": "answer, d3d-resources", "stop": "**stop**"}[kind]
        print("| `%s::%s` | %s | %s |" % (p[0], p[1], label, text))
    for p in sorted(TRACED):
        print("| `%s::%s` | answer, built (outside the 146: finding 40) | %s |" % (p[0], p[1], TRACED[p].replace(" | ", ". Source: ")))
    del desc


def main():
    args = sys.argv[1:]
    if args == ["--list"]:
        listing()
    elif args == ["--table"]:
        table()
    elif len(args) == 2 and args[0] == "--show":
        text = show(args[1])
        if text is None:
            sys.exit("no row %s" % args[1])
        print(text)
    elif args == ["--check"]:
        p = problems()
        for line in p:
            print(line)
        print(counts() + "; %d built outside the 146" % len(TRACED) + (": %d problems" % len(p) if p else ": ok"))
        sys.exit(1 if p else 0)
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
