#!/usr/bin/env python3
"""The back-end seam of the do-nothing Direct3D device as task rows (unit pic-seam).

runtime/include/rt/d3dback.h is the seam: a table of entries a back end (a renderer) fills, and
the macros a method body hands its call on with.  Unit pic-seam built the seam and one worked
example of every shape of method; every other method is a ROW here: one or two methods, for a
small worker, with a pass/fail check.

    .venv/bin/python tools/swarm/back_rows.py --check      every entry is done or in exactly one row; print the counts
    .venv/bin/python tools/swarm/back_rows.py --list       one line per row, with its state
    .venv/bin/python tools/swarm/back_rows.py --show ID    one brief
    .venv/bin/python tools/swarm/back_rows.py --write      swarm/briefs/back-<id>.md and swarm/units/back-<id>.json
                                                           (the planner's; a unit never writes under swarm/)

A row touches only its own files: the body `runtime/nulldev/<Interface>/<Method>.c` (which
exists: a body to which the forward is added, or a SKELETON that does what the accept list did
and which the row replaces) and the new test `runtime/tests/back/<Interface>/<Method>.c`.  It
never edits rt/d3dback.h, the recording back end or a makefile.  Its check is
`make back-check ROW=<id>` (runtime/tools/check_back.py).

A row is DONE when each of its bodies uses its entry and each of its tests exists; `--write`
gives such a row state "held".  `make d3d-replay-back` is the measure of the whole: it lists
every entry whose method the recording calls and whose body does not hand it on yet.
"""

import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3d_rows                                                     # noqa: E402

CTX, DEV, SC, FAC = "ID3D11DeviceContext", "ID3D11Device", "IDXGISwapChain", "IDXGIFactory1"

# ---- done by unit pic-seam: the worked examples, by shape ----------------------------------------------
DONE = {
    (DEV, "CreateBuffer"): "a creator with a description and initial data",
    (DEV, "CreateShaderResourceView"): "a creator of a view of another object",
    (DEV, "CreateBlendState"): "a state-object creator (an object that exists is the front's alone)",
    (CTX, "VSSetConstantBuffers"): "a bind of an array of objects with a start slot",
    (CTX, "VSSetShader"): "a bind of one object",
    (CTX, "DrawIndexed"): "a draw",
    (CTX, "ClearRenderTargetView"): "a clear",
    (CTX, "CopyResource"): "a copy between two resources",
    (CTX, "UpdateSubresource"): "an update from the game's bytes",
    (CTX, "Map"): "Map: the back end's memory",
    (CTX, "Unmap"): "Unmap",
    (CTX, "Begin"): "a query's Begin",
    (CTX, "End"): "a query's End",
    (CTX, "GetData"): "a query's GetData: the back end's answer",
    (SC, "Present"): "Present: the back end's answer",
    (FAC, "CreateSwapChain"): "the swap chain's creator, and its back buffer (entry SwapChainGetBuffer)",
    (SC, "ResizeBuffers"): "the swap chain's resize: the old back buffer first",
}
SEAM_OWN = ("CreateDevice", "Release", "SwapChainGetBuffer")        # entries of no front method: runtime/nulldev/objects.c, creators.c

# ---- the notes of each method ----------------------------------------------------------------------------
# body: what to write, exactly.  test: what the test makes and calls beyond the model's shape.
# skeleton: the body file is a skeleton (it only checks `this`): the row writes the whole body.

def _array(method, arr, count, cls, limit, limit_name, what, skeleton):
    body = []
    if skeleton:
        body.append("""The file is a SKELETON: replace its function body (keep the `COM_METHOD` line; rewrite the head
comment: `RT-STATUS: partial`, the cases you test in `RT-CASES`, a piece of each stop's message in
`RT-TRAPS`, and delete the "THE SKELETON OF A BACK-END ROW" comment). The body, in this order:

    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if ((uint64_t)StartSlot + %(count)s > %(limit_name)s)
        RT_UNIMPLEMENTED(ctx, "%(method)s for slots %%u to %%u", StartSlot, StartSlot + %(count)s - 1);
    if (%(count)s > 0 && !%(arr)s)
        RT_UNIMPLEMENTED(ctx, "%(method)s with a NULL array");
    for (UINT i = 0; i < %(count)s; i++)
        (void)rt_nulldev_arg(ctx, (void *)%(arr)s[i], &rt_nulldev_%(cls)s_class, "%(arr)s");

(`%(limit_name)s` is %(limit)d. The sum is taken in 64 bits: in `UINT`, 0xffffffff + 2 wraps to 1.
Nothing is recorded in `NdContext`: no Get of this bind exists, as before.) Then the forward:""")
    else:
        body.append("The body exists and keeps every line it has. After its last statement (after every check and "
                    "every `rt_nulldev_bind`), add the forward:")
    body.append("""
    if (rt_d3dback()) {
        void *back[%(limit_name)s];
        for (UINT i = 0; i < %(count)s; i++) back[i] = rt_d3dback_of((void *)%(arr)s[i]);
        RT_BACK(ctx, %(method)s, c->child.back, StartSlot, %(count)s, %(arr)s ? back : NULL);
    }

Each %(what)s crosses as its back object, NULL as NULL, in an array rebuilt on the stack; the
game's own array is never passed on, and what crosses is what THE GAME passed (not what the
front decided to record in its place).""")
    test = ("Objects: `T_BACK_NEW(device, &rt_nulldev_%(cls)s_class)`, two of them, and one of another class for the "
            "wrong-class stop. Cases as in the model test of `VSSetConstantBuffers`: three slots with a NULL in the "
            "middle (count 1; arguments 1 and 2 the slot and the count; `T_BACK_ELEMENT(%(method)s, 3, i)` each back "
            "object or 0; argument 3 not the game's array); a count of 0 with a NULL array (argument 3 is 0); the last "
            "slot (%(last)d). Stops, each with `T_BACK_STOPS_BEFORE`: a range past slot %(last)d; a NULL array with a "
            "count; an object of the wrong class.")
    d = dict(method=method, arr=arr, count=count, cls=cls, limit=limit, limit_name=limit_name, what=what, last=limit - 1)
    return {"body": "\n".join(body) % d, "test": test % d, "skeleton": skeleton, "model": (CTX, "VSSetConstantBuffers")}


def _one(method, arg, cls, skeleton=False, extra_args="", extra_test="", stops_note=""):
    if skeleton:
        body = ("""The file is a SKELETON: replace its function body (keep the `COM_METHOD` line; rewrite the head
comment: `RT-STATUS: partial`, your cases in `RT-CASES`, a piece of each stop's message in `RT-TRAPS`, and
delete the "THE SKELETON OF A BACK-END ROW" comment). The body, in this order:

    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
%(stops)s    (void)rt_nulldev_arg(ctx, %(arg)s, &rt_nulldev_%(cls)s_class, "%(arg)s");
    RT_BACK(ctx, %(method)s, c->child.back, rt_d3dback_of(%(arg)s)%(extra)s);
""")
    else:
        body = ("""The body exists and keeps every line it has. After its last statement (after every check and the
`rt_nulldev_bind`), add:

    RT_BACK(ctx, %(method)s, c->child.back, rt_d3dback_of(%(arg)s)%(extra)s);
""")
    body += "The object crosses as its back object, NULL as NULL; every other argument as the game passed it."
    test = ("Object: `T_BACK_NEW(device, &rt_nulldev_%(cls)s_class)`. Cases as in the model test of `VSSetShader`: the "
            "object (count 1, argument 0 the context's back object, argument 1 the object's); NULL (argument 1 is 0)."
            "%(extra_test)s Stops, each with `T_BACK_STOPS_BEFORE`: every `RT_UNIMPLEMENTED` of the body, and an object "
            "of the wrong class (a `T_BACK_NEW` of another class, cast).")
    d = dict(method=method, arg=arg, cls=cls, extra=extra_args, extra_test=" " + extra_test if extra_test else "", stops=stops_note)
    return {"body": body % d, "test": test % d, "skeleton": skeleton, "model": (CTX, "VSSetShader")}


def _creator(method, call, made, model, test):
    body = ("""The body exists and keeps every line it has. It is a creator: the front makes its object first, then
the back end is asked for its own (rule 3). %s

`RT_BACK_CREATE(ctx, made, Entry, arguments...)` takes the entry's arguments WITHOUT the last (the
out-pointer): it asks the back end only for an object that has no back object yet, stops when the
back end's result is not S_OK, and attaches the object. `made` is the front object the creator
hands out, or NULL when the call only validated (then the front answers alone, which the macro
counts).""" % call)
    return {"body": body, "test": test + " A call that only validates (out-pointer NULL, `S_FALSE`) leaves the count and adds "
            "one to `rt_d3dback_alone(RT_BACK_%s)`. Releasing the object (`COM_CALL(<its interface>, Release, object)` "
            "returning 0) raises `T_BACK_COUNT(Release)` by one. Rule 4, inside `if (T_BACK)`: "
            "`rt_d3dback_record_answer(RT_BACK_%s, E_FAIL)`, then `T_TRAPS(<a create>, \"the back end (record) answered "
            "0x80004005 to ID3D11Device::%s\")`, then the answer back to `S_OK`. Stops with `T_BACK_STOPS_BEFORE`: the stops a "
            "call can reach simply, of the body or of the helper it calls (`rt_nulldev_view_create`, `_state_create`, "
            "`_shader_create`, `rt_nulldev_texture_new` in `runtime/nulldev/creators.c` and `memory.c`): a NULL description, a "
            "NULL resource, NULL bytes, whichever the method has." % (made, made, made),
            "skeleton": False, "model": model}


def _plain(method, stmt, model, test, skeleton, stops=""):
    if skeleton:
        body = ("""The file is a SKELETON: replace its function body (keep the `COM_METHOD` line; rewrite the head
comment: `RT-STATUS: %s`, your cases in `RT-CASES`, %s in `RT-TRAPS`, and delete the "THE SKELETON OF A
BACK-END ROW" comment). The body, in this order:

    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
%s    %s
""" % ("partial" if stops else "full", "a piece of each stop's message" if stops else "`none`", stops, stmt))
    else:
        body = ("The body exists and keeps every line it has. After its last statement (after every check), add:\n\n    %s\n" % stmt)
    return {"body": body, "test": test, "skeleton": skeleton, "model": model}


_VIEW_TEST = ("A resource: a texture made as the model test of `CreateShaderResourceView` makes its own (so that it has a "
              "back object), with the bind flag this kind of view needs; a valid view description is in "
              "`runtime/tests/nulldev/ID3D11Device/%s.c`. Cases as in the model "
              "test of `CreateShaderResourceView`: a view (count 1; argument 0 the device's back object, 1 the "
              "resource's, 2 the description pointer or 0 for NULL, 3 the view's back object, not 0).")
_STATE_TEST = ("Copy a valid description from `runtime/tests/nulldev/ID3D11Device/%s.c`. Cases as in the model test of "
               "`CreateBlendState`: a new object (count 1, argument 1 the description pointer, argument 2 its back "
               "object); the SAME description again gives the same pointer, the count stays and "
               "`rt_d3dback_alone(RT_BACK_%s)` is 1 (rule 7); a different description gives a new object and count 2.")
_SHADER_TEST = ("Copy from `runtime/tests/nulldev/ID3D11Device/%s.c` the bytes it passes as bytecode. A shader (count 1; "
                "argument 0 the device's back object, 1 the bytecode pointer, 2 the length, 3 the shader's back object).")
_TEX_TEST = ("Copy a valid description (and initial data, if it has some) from `runtime/tests/nulldev/ID3D11Device/%s.c`. "
             "Cases as in the model test of `CreateBuffer`: a texture (count 1; argument 0 the device's back object, 1 "
             "the description pointer, 2 the initial-data pointer or 0, 3 the texture's back object).")

M = {}
for _m, _lim, _name in (("PSSetConstantBuffers", 14, "ND_CB_SLOTS"), ("GSSetConstantBuffers", 14, "ND_CB_SLOTS"), ("CSSetConstantBuffers", 14, "ND_CB_SLOTS")):
    M[(CTX, _m)] = _array(_m, "ppConstantBuffers", "NumBuffers", "buffer", _lim, _name, "buffer", _m != "PSSetConstantBuffers")
for _m in ("PSSetShaderResources", "VSSetShaderResources", "GSSetShaderResources", "CSSetShaderResources"):
    M[(CTX, _m)] = _array(_m, "ppShaderResourceViews", "NumViews", "srv", 128, "ND_SRV_SLOTS", "view", _m != "PSSetShaderResources")
M[(CTX, "PSSetShaderResources")]["test"] += (" One more case: a view of a texture that is bound as a render target is "
    "recorded by the front as NULL in its slot (`c->ps_srv[slot] == NULL`, as the nulldev test shows) and still CROSSES as "
    "its back object: copy that case's setup from `runtime/tests/nulldev/ID3D11DeviceContext/PSSetShaderResources.c`, and "
    "give each object it makes a back object with `T_BACK_ADOPT(...)`.")
for _m in ("PSSetSamplers", "VSSetSamplers", "GSSetSamplers", "CSSetSamplers"):
    M[(CTX, _m)] = _array(_m, "ppSamplers", "NumSamplers", "sampler", 16, "ND_SAMPLER_SLOTS", "sampler state", _m != "PSSetSamplers")

M[(CTX, "IASetVertexBuffers")] = {"skeleton": True, "model": (CTX, "VSSetConstantBuffers"), "body": """The file is a SKELETON: replace its function body (keep the `COM_METHOD` line; rewrite the head
comment: `RT-STATUS: partial`, your cases in `RT-CASES`, a piece of each stop's message in `RT-TRAPS`, and
delete the "THE SKELETON OF A BACK-END ROW" comment). The body, in this order:

    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if ((uint64_t)StartSlot + NumBuffers > D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT)
        RT_UNIMPLEMENTED(ctx, "IASetVertexBuffers for slots %u to %u", StartSlot, StartSlot + NumBuffers - 1);
    if (NumBuffers > 0 && (!ppVertexBuffers || !pStrides || !pOffsets))
        RT_UNIMPLEMENTED(ctx, "IASetVertexBuffers with a NULL array (buffers %p, strides %p, offsets %p)",
                         (const void *)ppVertexBuffers, (const void *)pStrides, (const void *)pOffsets);
    for (UINT i = 0; i < NumBuffers; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppVertexBuffers[i], &rt_nulldev_buffer_class, "ppVertexBuffers");
    if (rt_d3dback()) {
        void *back[D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT];
        for (UINT i = 0; i < NumBuffers; i++) back[i] = rt_d3dback_of((void *)ppVertexBuffers[i]);
        RT_BACK(ctx, IASetVertexBuffers, c->child.back, StartSlot, NumBuffers, ppVertexBuffers ? back : NULL, pStrides, pOffsets);
    }

(The slot count is 32. The buffers cross as back objects in an array rebuilt on the stack; the strides
and the offsets are the game's arrays, by pointer, unchanged. Nothing is recorded in `NdContext`.)""",
    "test": "Objects: two `T_BACK_NEW(device, &rt_nulldev_buffer_class)` and one of another class. `UINT strides[3] = "
            "{ 16, 20, 24 }, offsets[3] = { 0, 4, 8 };` Cases as the model's: three slots with a NULL in the middle "
            "(arguments 1, 2; elements of argument 3; argument 4 == `T_BACK_PTR(strides)`, argument 5 == "
            "`T_BACK_PTR(offsets)`); a count of 0 with three NULL arrays (arguments 3, 4, 5 are 0); the last slot (31). "
            "Stops with `T_BACK_STOPS_BEFORE`: a range past slot 31; a NULL buffer array with a count; NULL strides with "
            "a count; an object of the wrong class."}

M[(CTX, "CSSetUnorderedAccessViews")] = {"skeleton": True, "model": (CTX, "VSSetConstantBuffers"), "body": """The file is a SKELETON: replace its function body (keep the `COM_METHOD` line; rewrite the head
comment: `RT-STATUS: partial`, your cases in `RT-CASES`, a piece of each stop's message in `RT-TRAPS`, and
delete the "THE SKELETON OF A BACK-END ROW" comment). The body, in this order:

    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);
    if ((uint64_t)StartSlot + NumUAVs > D3D11_PS_CS_UAV_REGISTER_COUNT)
        RT_UNIMPLEMENTED(ctx, "CSSetUnorderedAccessViews for slots %u to %u", StartSlot, StartSlot + NumUAVs - 1);
    if (NumUAVs > 0 && !ppUnorderedAccessViews)
        RT_UNIMPLEMENTED(ctx, "CSSetUnorderedAccessViews with a NULL array");
    for (UINT i = 0; i < NumUAVs; i++)
        (void)rt_nulldev_arg(ctx, (void *)ppUnorderedAccessViews[i], &rt_nulldev_uav_class, "ppUnorderedAccessViews");
    if (rt_d3dback()) {
        void *back[D3D11_PS_CS_UAV_REGISTER_COUNT];
        for (UINT i = 0; i < NumUAVs; i++) back[i] = rt_d3dback_of((void *)ppUnorderedAccessViews[i]);
        RT_BACK(ctx, CSSetUnorderedAccessViews, c->child.back, StartSlot, NumUAVs, ppUnorderedAccessViews ? back : NULL, pUAVInitialCounts);
    }

(The slot count is 8. The initial counts are the game's array, by pointer, unchanged; NULL is allowed
and crosses as NULL. Nothing is recorded in `NdContext`.)""",
    "test": "Objects: two `T_BACK_NEW(device, &rt_nulldev_uav_class)` and one of another class. Cases as the model's: two "
            "slots with `UINT counts[2] = { 0, 0xffffffffu };` (argument 4 == `T_BACK_PTR(counts)`), then with NULL counts "
            "(argument 4 is 0); a count of 0 with a NULL array; the last slot (7). Stops with `T_BACK_STOPS_BEFORE`: a "
            "range past slot 7; a NULL array with a count; an object of the wrong class."}

M[(CTX, "PSSetShader")] = _one("PSSetShader", "pPixelShader", "ps", extra_test="")
M[(CTX, "GSSetShader")] = _one("GSSetShader", "pShader", "gs")
M[(CTX, "CSSetShader")] = _one("CSSetShader", "pComputeShader", "cs", skeleton=True,
                               stops_note="    if (NumClassInstances != 0) RT_UNIMPLEMENTED(ctx, \"CSSetShader with %u class instances\", NumClassInstances);\n"
                                          "    (void)ppClassInstances;\n")
for _m, _arg in (("HSSetShader", "pHullShader"), ("DSSetShader", "pDomainShader")):
    M[(CTX, _m)] = {"skeleton": False, "model": (CTX, "VSSetShader"),
                    "body": "The body exists and keeps every line it has. It stops on any shader (no hull or domain shader can "
                            "exist) and on class instances; what is left is the call with NULL. After its last check add:\n\n"
                            "    RT_BACK(ctx, %s, c->child.back, NULL);\n\nand remove the line `(void)c;` (c is used now)." % _m,
                    "test": "The only call that returns is `COM_CALL(ID3D11DeviceContext, %s, context, NULL, NULL, 0)`: count 1, "
                            "argument 0 the context's back object, argument 1 is 0; twice gives count 2. Stops with "
                            "`T_BACK_STOPS_BEFORE`: a shader that is not NULL (any `T_BACK_NEW` object, cast); one class "
                            "instance." % _m}
M[(CTX, "IASetInputLayout")] = _one("IASetInputLayout", "pInputLayout", "inputlayout")
M[(CTX, "RSSetState")] = _one("RSSetState", "pRasterizerState", "rasterizer")
M[(CTX, "IASetIndexBuffer")] = _one("IASetIndexBuffer", "pIndexBuffer", "buffer", skeleton=True, extra_args=", Format, Offset",
                                    extra_test="Arguments 2 and 3 are the format and the offset as passed (use "
                                               "DXGI_FORMAT_R16_UINT and 12).")
M[(CTX, "OMSetBlendState")] = _one("OMSetBlendState", "pBlendState", "blend", extra_args=", BlendFactor, SampleMask",
                                   extra_test="Argument 2 is the game's factor pointer (`T_BACK_PTR(factor)` for a `FLOAT "
                                              "factor[4]`, and 0 when the game passes NULL: the front's four ones are NOT "
                                              "substituted), argument 3 the mask.")
M[(CTX, "OMSetDepthStencilState")] = _one("OMSetDepthStencilState", "pDepthStencilState", "depthstencil", extra_args=", StencilRef",
                                          extra_test="Argument 2 is the stencil reference.")

M[(CTX, "OMSetRenderTargets")] = {"skeleton": False, "model": (CTX, "VSSetConstantBuffers"), "body": """The body exists and keeps every line it has. After its last statement add:

    if (rt_d3dback()) {
        void *back[ND_RT_SLOTS];
        for (UINT i = 0; i < NumViews; i++) back[i] = rt_d3dback_of((void *)ppRenderTargetViews[i]);
        RT_BACK(ctx, OMSetRenderTargets, c->child.back, NumViews, ppRenderTargetViews ? back : NULL, rt_d3dback_of(pDepthStencilView));
    }

The views cross as the game passed them (NumViews of them, not the eight slots the front fills).""",
    "test": "Objects: two `T_BACK_NEW(device, &rt_nulldev_rtv_class)`, one `T_BACK_NEW(device, &rt_nulldev_dsv_class)`. "
            "(`rt_nulldev_unbind_readers` looks at each view's resource: a view from `T_BACK_NEW` has none, which is "
            "fine as long as no shader-resource view is bound; bind none.) Cases: two views and the depth view (count 1; "
            "argument 1 is 2; `T_BACK_ELEMENT(OMSetRenderTargets, 2, i)` the back objects; argument 3 the depth view's); "
            "no view and no depth view (`0, NULL, NULL`: arguments 2 and 3 are 0). Stops with `T_BACK_STOPS_BEFORE`: "
            "nine views; a NULL array with a count; a view of the wrong class."}

M[(CTX, "OMSetRenderTargetsAndUnorderedAccessViews")] = {"skeleton": False, "model": (CTX, "VSSetConstantBuffers"), "body": """The body exists and keeps every line it has. Two changes.

1. Among its checks, after the stop "with a NULL array", add one (a count of views with no array is a
   call a renderer cannot be handed):

    if (!keep_uav && NumUAVs > 0 && !ppUnorderedAccessViews)
        RT_UNIMPLEMENTED(ctx, "OMSetRenderTargetsAndUnorderedAccessViews with a NULL array of unordered-access views");

   and add a piece of that message to `RT-TRAPS` in the head comment.
2. As the LAST statement of the function (after the loop over the unordered-access views, which
   still holds a stop), the forward; remove `(void)pUAVInitialCounts;`:

    if (rt_d3dback()) {
        void *rtvs[ND_RT_SLOTS], *uavs[D3D11_PS_CS_UAV_REGISTER_COUNT];
        for (UINT i = 0; !keep_rt && i < NumRTVs; i++) rtvs[i] = rt_d3dback_of((void *)ppRenderTargetViews[i]);
        for (UINT i = 0; !keep_uav && i < NumUAVs; i++) uavs[i] = rt_d3dback_of((void *)ppUnorderedAccessViews[i]);
        RT_BACK(ctx, OMSetRenderTargetsAndUnorderedAccessViews, c->child.back, NumRTVs,
                !keep_rt && ppRenderTargetViews ? rtvs : NULL, keep_rt ? NULL : rt_d3dback_of(pDepthStencilView),
                UAVStartSlot, NumUAVs, !keep_uav && ppUnorderedAccessViews ? uavs : NULL, pUAVInitialCounts);
    }

A count of 0xffffffff means "keep what is bound": Direct3D does not read that array then, so it
crosses as NULL (and the depth view with the render targets); the two counts cross as passed.""",
    "test": "Objects: one `T_BACK_NEW(device, &rt_nulldev_rtv_class)`, one `..._dsv_class`, one `..._uav_class`. Cases: one "
            "render target in slot 0, the depth view, one unordered-access view from slot 1 (`1, rtvs, dsv, 1, 1, uavs, "
            "NULL`): count 1; argument 1 is 1; `T_BACK_ELEMENT(..., 2, 0)` the render target's back object; argument 3 "
            "the depth view's; arguments 4 and 5 are 1; `T_BACK_ELEMENT(..., 6, 0)` the unordered-access view's; argument "
            "7 is 0. Then keep the render targets (`0xffffffff, NULL, NULL, 1, 1, uavs, counts` with `UINT counts[1] = "
            "{ 0 }`; first unbind slot 1 and above is already empty): argument 1 is 0xffffffff, arguments 2 and 3 are "
            "0, argument 7 == `T_BACK_PTR(counts)`. Stops with `T_BACK_STOPS_BEFORE`: nine render targets; a NULL "
            "render-target array with a count; unordered-access views with a count and a NULL array; a view of the "
            "wrong class."}

M[(CTX, "IASetPrimitiveTopology")] = _plain("IASetPrimitiveTopology", "RT_BACK(ctx, IASetPrimitiveTopology, c->child.back, Topology);",
    (CTX, "DrawIndexed"), "Cases: topology 4 (a triangle list): count 1, argument 0 the context's back object, argument 1 "
    "is 4; topology 0. Stop with `T_BACK_STOPS_BEFORE`: topology 7.", False)
M[(CTX, "RSSetViewports")] = _plain("RSSetViewports", "RT_BACK(ctx, RSSetViewports, c->child.back, NumViewports, pViewports);",
    (CTX, "DrawIndexed"), "Cases: two viewports (`D3D11_VIEWPORT v[2]` with any numbers): count 1, argument 1 is 2, argument 2 "
    "== `T_BACK_PTR(v)` (the game's array, by pointer); none (`0, NULL`): argument 2 is 0. Stops with "
    "`T_BACK_STOPS_BEFORE`: 17 viewports; a NULL array with a count.", False)
M[(CTX, "ClearState")] = {"skeleton": False, "model": (CTX, "DrawIndexed"),
    "body": "The body is one statement. Write it as two, the forward last:\n\n"
            "    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);\n    rt_nulldev_context_reset(c);\n"
            "    RT_BACK(ctx, ClearState, c->child.back);\n\n(`rt_nulldev_context_reset` keeps the context's back object: "
            "it is not state.)",
    "test": "Cases: bind a buffer first (`T_BACK_NEW`, through `VSSetConstantBuffers`), then `ClearState`: in both modes the "
            "slot reads NULL afterwards (`c->vs_cb[0] == NULL`) and the buffer's `rt_com_refs` is 1 again; with the back end "
            "count 1, argument 0 the context's back object, and `T_BACK_OF(context)` is the same before and after."}
M[(CTX, "Flush")] = _plain("Flush", "RT_BACK(ctx, Flush, c->child.back);", (CTX, "DrawIndexed"),
    "Cases: one call (count 1, argument 0 the context's back object); a second (count 2). The method has no stop.", True)
M[(CTX, "Draw")] = _plain("Draw", "RT_BACK(ctx, Draw, c->child.back, VertexCount, StartVertexLocation);", (CTX, "DrawIndexed"),
    "Cases as the model's: `(3, 7)`: count 1, arguments 1 and 2; zeroes; 0xffffffff. No stop.", True)
M[(CTX, "DrawInstanced")] = _plain("DrawInstanced", "RT_BACK(ctx, DrawInstanced, c->child.back, VertexCountPerInstance, InstanceCount, "
    "StartVertexLocation, StartInstanceLocation);", (CTX, "DrawIndexed"), "Cases as the model's: `(6, 10, 2, 3)`: count 1, "
    "arguments 1 to 4; zeroes. No stop.", True)
M[(CTX, "DrawIndexedInstanced")] = _plain("DrawIndexedInstanced", "RT_BACK(ctx, DrawIndexedInstanced, c->child.back, IndexCountPerInstance, "
    "InstanceCount, StartIndexLocation, BaseVertexLocation, StartInstanceLocation);", (CTX, "DrawIndexed"),
    "Cases as the model's: `(36, 4, 6, -2, 1)`: count 1, arguments 1 to 5, the signed one as `(uint64_t)(int64_t)-2`; "
    "zeroes. No stop.", True)
M[(CTX, "Dispatch")] = _plain("Dispatch", "RT_BACK(ctx, Dispatch, c->child.back, ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);",
    (CTX, "DrawIndexed"), "Cases as the model's: `(8, 4, 1)`: count 1, arguments 1 to 3; zeroes. No stop.", True)
for _m in ("DrawInstancedIndirect", "DispatchIndirect"):
    M[(CTX, _m)] = _plain(_m, "RT_BACK(ctx, %s, c->child.back, rt_d3dback_of(pBufferForArgs), AlignedByteOffsetForArgs);" % _m,
        (CTX, "ClearRenderTargetView"), "Object: `T_BACK_NEW(device, &rt_nulldev_buffer_class)`. Cases: the buffer and offset 16: "
        "count 1, argument 1 the buffer's back object, argument 2 is 16. Stops with `T_BACK_STOPS_BEFORE`: a NULL buffer; an "
        "object of the wrong class.", True,
        "    if (!pBufferForArgs) RT_UNIMPLEMENTED(ctx, \"%s with a NULL buffer\");\n"
        "    (void)rt_nulldev_arg(ctx, pBufferForArgs, &rt_nulldev_buffer_class, \"pBufferForArgs\");\n" % _m)
M[(CTX, "ClearDepthStencilView")] = _plain("ClearDepthStencilView",
    "RT_BACK(ctx, ClearDepthStencilView, c->child.back, rt_d3dback_of(pDepthStencilView), ClearFlags, Depth, Stencil);",
    (CTX, "ClearRenderTargetView"), "Object: `T_BACK_NEW(device, &rt_nulldev_dsv_class)`. Cases: `(view, D3D11_CLEAR_DEPTH | "
    "D3D11_CLEAR_STENCIL, 0.5f, 7)`: count 1, argument 1 the view's back object, argument 2 is 3, argument 3 is the bits of "
    "0.5f (0x3f000000: the recording back end keeps a FLOAT's bits), argument 4 is 7. Stops with `T_BACK_STOPS_BEFORE`: a "
    "NULL view; flags 4; a view of the wrong class.", True,
    "    if (!pDepthStencilView) RT_UNIMPLEMENTED(ctx, \"ClearDepthStencilView of a NULL view\");\n"
    "    if (ClearFlags & ~(D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL)) RT_UNIMPLEMENTED(ctx, \"ClearDepthStencilView with flags 0x%x\", (unsigned)ClearFlags);\n"
    "    (void)rt_nulldev_arg(ctx, pDepthStencilView, &rt_nulldev_dsv_class, \"pDepthStencilView\");\n")
M[(CTX, "ClearUnorderedAccessViewFloat")] = _plain("ClearUnorderedAccessViewFloat",
    "RT_BACK(ctx, ClearUnorderedAccessViewFloat, c->child.back, rt_d3dback_of(pUnorderedAccessView), Values);",
    (CTX, "ClearRenderTargetView"), "Object: `T_BACK_NEW(device, &rt_nulldev_uav_class)`. Cases: the view and `FLOAT v[4]`: "
    "count 1, argument 1 the view's back object, argument 2 == `T_BACK_PTR(v)`. Stops with `T_BACK_STOPS_BEFORE`: a NULL "
    "view; NULL values; a view of the wrong class.", True,
    "    if (!pUnorderedAccessView) RT_UNIMPLEMENTED(ctx, \"ClearUnorderedAccessViewFloat of a NULL view\");\n"
    "    if (!Values) RT_UNIMPLEMENTED(ctx, \"ClearUnorderedAccessViewFloat with NULL values\");\n"
    "    (void)rt_nulldev_arg(ctx, pUnorderedAccessView, &rt_nulldev_uav_class, \"pUnorderedAccessView\");\n")
M[(CTX, "CopySubresourceRegion")] = {"skeleton": False, "model": (CTX, "CopyResource"),
    "body": "The body is two statements; `rt_nulldev_copy` holds every check and stop of the front. Change the first to keep "
            "the context's record and add the forward last:\n\n"
            "    NdContext *c = rt_com_state(ctx, This, &rt_nulldev_context_class);\n"
            "    rt_nulldev_copy(ctx, pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);\n"
            "    RT_BACK(ctx, CopySubresourceRegion, c->child.back, rt_d3dback_of(pDstResource), DstSubresource, DstX, DstY, DstZ,\n"
            "            rt_d3dback_of(pSrcResource), SrcSubresource, pSrcBox);\n",
    "test": "Two buffers of 64 bytes from the real creator (as the model test of `CopyResource` makes them). Cases: a box "
            "(`D3D11_BOX box = { .left = 0, .top = 0, .front = 0, .right = 16, .bottom = 1, .back = 1 };` to x 8): count 1; "
            "argument 1 the destination's back object, 2 is 0, 3 is 8, 4 and 5 are 0, 6 the source's back object, 7 is 0, 8 "
            "== `T_BACK_PTR(&box)`; a NULL box (argument 8 is 0). Stops with `T_BACK_STOPS_BEFORE`: a NULL destination; a "
            "NULL source; a box that reaches past the source (`.right = 128`)."}
M[(CTX, "ResolveSubresource")] = {"skeleton": False, "model": (CTX, "CopyResource"),
    "body": "The body exists and keeps every line it has, except two: change `(void)rt_com_state(...)` to `NdContext *c = "
            "rt_com_state(ctx, This, &rt_nulldev_context_class);` and remove `(void)Format;`. After its last statement add:\n\n"
            "    RT_BACK(ctx, ResolveSubresource, c->child.back, rt_d3dback_of(pDstResource), DstSubresource, rt_d3dback_of(pSrcResource), SrcSubresource, Format);\n",
    "test": "Copy from `runtime/tests/nulldev/ID3D11DeviceContext/ResolveSubresource.c` how it makes a multisampled source "
            "and a plain destination, and give each a back object with `T_BACK_ADOPT(...)`. Cases: the resolve: count 1; "
            "argument 1 the destination's back object, 2 is 0, 3 the source's, 4 is 0, 5 the format passed. Stops with "
            "`T_BACK_STOPS_BEFORE`: a NULL resource; a source that is not multisampled; a multisampled destination."}

for _n, _dim in (("1D", "TEXTURE1D"), ("2D", "TEXTURE2D"), ("3D", "TEXTURE3D")):
    M[(DEV, "CreateTexture" + _n)] = _creator("CreateTexture" + _n,
        "Two additions, as `runtime/nulldev/ID3D11Device/CreateBuffer.c` has them: `RT_BACK_ALONE(ctx, CreateTexture%s);` "
        "before `return S_FALSE;`, and before `*ppTexture%s = texture;`:\n\n"
        "    RT_BACK_CREATE(ctx, texture, CreateTexture%s, rt_d3dback_of(This), pDesc, pInitialData);" % (_n, _n, _n),
        "CreateTexture" + _n, (DEV, "CreateBuffer"), _TEX_TEST % ("CreateTexture" + _n))
for _m, _out in (("CreateUnorderedAccessView", "ppUAView"), ("CreateRenderTargetView", "ppRTView"), ("CreateDepthStencilView", "ppDepthStencilView")):
    M[(DEV, _m)] = _creator(_m, "The body is one `return` of `rt_nulldev_view_create(...)`. Write it as "
        "`runtime/nulldev/ID3D11Device/CreateShaderResourceView.c` is written:\n\n"
        "    HRESULT hr = rt_nulldev_view_create(...the same arguments as now...);\n"
        "    RT_BACK_CREATE(ctx, %s ? *%s : NULL, %s, rt_d3dback_of(This), rt_d3dback_of(pResource), pDesc);\n    return hr;" % (_out, _out, _m),
        _m, (DEV, "CreateShaderResourceView"), _VIEW_TEST % _m)
for _m, _arg, _out in (("CreateDepthStencilState", "pDepthStencilDesc", "ppDepthStencilState"), ("CreateRasterizerState", "pRasterizerDesc", "ppRasterizerState"),
                       ("CreateSamplerState", "pSamplerDesc", "ppSamplerState")):
    M[(DEV, _m)] = _creator(_m, "The body is one `return` of `rt_nulldev_state_create(...)`. Write it as "
        "`runtime/nulldev/ID3D11Device/CreateBlendState.c` is written:\n\n"
        "    HRESULT hr = rt_nulldev_state_create(...the same arguments as now...);\n"
        "    RT_BACK_CREATE(ctx, *%s, %s, rt_d3dback_of(This), %s);\n    return hr;\n\n"
        "(A state object with the same description as a live one is that same object: it has its back object, and the "
        "macro counts the call as answered by the front alone. `rt_nulldev_state_create` stops on a NULL out-pointer, so "
        "`*%s` is safe.)" % (_out, _m, _arg, _out), _m, (DEV, "CreateBlendState"), _STATE_TEST % (_m, _m))
    M[(DEV, _m)]["test"] = M[(DEV, _m)]["test"].replace(" A call that only validates (out-pointer NULL, `S_FALSE`) leaves the count and adds "
        "one to `rt_d3dback_alone(RT_BACK_%s)`." % _m, "")
for _m, _out in (("CreateVertexShader", "ppVertexShader"), ("CreateGeometryShader", "ppGeometryShader"), ("CreatePixelShader", "ppPixelShader"),
                 ("CreateComputeShader", "ppComputeShader")):
    M[(DEV, _m)] = _creator(_m, "The body is one `return` of `rt_nulldev_shader_create(...)`. Write it in the shape of "
        "`runtime/nulldev/ID3D11Device/CreateShaderResourceView.c`:\n\n"
        "    HRESULT hr = rt_nulldev_shader_create(...the same arguments as now...);\n"
        "    RT_BACK_CREATE(ctx, %s ? *%s : NULL, %s, rt_d3dback_of(This), pShaderBytecode, BytecodeLength);\n    return hr;\n\n"
        "(The class linkage does not cross: the front has stopped on one.)" % (_out, _out, _m), _m, (DEV, "CreateShaderResourceView"),
        _SHADER_TEST % _m)
M[(DEV, "CreateInputLayout")] = _creator("CreateInputLayout",
    "Two additions: `RT_BACK_ALONE(ctx, CreateInputLayout);` before `return S_FALSE;` (write the `if` with braces), and before "
    "`*ppInputLayout = layout;`:\n\n    RT_BACK_CREATE(ctx, layout, CreateInputLayout, rt_d3dback_of(This), pInputElementDescs, NumElements,\n"
    "                   pShaderBytecodeWithInputSignature, BytecodeLength);", "CreateInputLayout", (DEV, "CreateBuffer"),
    "Copy from `runtime/tests/nulldev/ID3D11Device/CreateInputLayout.c` its element descriptions and bytecode. A layout "
    "(count 1; argument 0 the device's back object, 1 the elements pointer, 2 their number, 3 the bytecode pointer, 4 the "
    "length, 5 the layout's back object).")
M[(DEV, "CreateQuery")] = _creator("CreateQuery",
    "Two additions: `RT_BACK_ALONE(ctx, CreateQuery);` before `return S_FALSE;` (write the `if` with braces), and before "
    "`*ppQuery = query;`:\n\n    RT_BACK_CREATE(ctx, query, CreateQuery, rt_d3dback_of(This), pQueryDesc);", "CreateQuery", (DEV, "CreateBuffer"),
    "`D3D11_QUERY_DESC d = { .Query = D3D11_QUERY_OCCLUSION };` A query (count 1; argument 0 the device's back object, 1 "
    "== `T_BACK_PTR(&d)`, 2 the query's back object); an event query makes a second (count 2).")
M[("IDXGIDevice1", "SetMaximumFrameLatency")] = {"skeleton": False, "model": (CTX, "DrawIndexed"),
    "body": "The body exists and keeps every line it has. Before `return S_OK;` add (the front's answer is S_OK; any other "
            "result of the back end is a stop: rule 4; the number crosses as the game passed it, 0 included):\n\n"
            "    RT_BACK_SAME(ctx, S_OK, SetMaximumFrameLatency, s->back, MaxLatency);\n",
    "test": "The method is the device's second face: `IDXGIDevice1 *dx = rt_com_face(device, &rt_iface_IDXGIDevice1);` and "
            "`COM_CALL(IDXGIDevice1, SetMaximumFrameLatency, dx, 2)` returns S_OK: count 1, argument 0 the DEVICE's back "
            "object (`T_BACK_OF(device)`), argument 1 is 2; with 0 argument 1 is 0 while the front records 3 "
            "(`((NdDevice *)rt_com_state_of(device, &rt_nulldev_device_class))->max_frame_latency == 3`). Rule 4, inside "
            "`if (T_BACK)`: answer `E_FAIL`, `T_TRAPS(..., \"the back end (record) answered 0x80004005 to "
            "IDXGIDevice1::SetMaximumFrameLatency\")`, answer back to `S_OK`. Stop with `T_BACK_STOPS_BEFORE`: a latency of 17."}

# ---- the rows --------------------------------------------------------------------------------------------
ROWS = [
    ("tex-1d-2d", "creator", [(DEV, "CreateTexture1D"), (DEV, "CreateTexture2D")]),
    ("tex-3d", "creator", [(DEV, "CreateTexture3D")]),
    ("view-uav-rtv", "view creator", [(DEV, "CreateUnorderedAccessView"), (DEV, "CreateRenderTargetView")]),
    ("view-dsv", "view creator", [(DEV, "CreateDepthStencilView")]),
    ("state-ds-rs", "state creator", [(DEV, "CreateDepthStencilState"), (DEV, "CreateRasterizerState")]),
    ("state-sampler", "state creator", [(DEV, "CreateSamplerState")]),
    ("shader-vs-ps", "shader creator", [(DEV, "CreateVertexShader"), (DEV, "CreatePixelShader")]),
    ("shader-gs-cs", "shader creator", [(DEV, "CreateGeometryShader"), (DEV, "CreateComputeShader")]),
    ("input-layout", "creator", [(DEV, "CreateInputLayout")]),
    ("query", "creator", [(DEV, "CreateQuery")]),
    ("frame-latency", "state without an object", [("IDXGIDevice1", "SetMaximumFrameLatency")]),
    ("cb-ps-gs", "array bind", [(CTX, "PSSetConstantBuffers"), (CTX, "GSSetConstantBuffers")]),
    ("cb-cs", "array bind", [(CTX, "CSSetConstantBuffers")]),
    ("srv-ps", "array bind", [(CTX, "PSSetShaderResources")]),
    ("srv-vs-gs", "array bind", [(CTX, "VSSetShaderResources"), (CTX, "GSSetShaderResources")]),
    ("srv-cs", "array bind", [(CTX, "CSSetShaderResources")]),
    ("samplers-ps-vs", "array bind", [(CTX, "PSSetSamplers"), (CTX, "VSSetSamplers")]),
    ("samplers-gs-cs", "array bind", [(CTX, "GSSetSamplers"), (CTX, "CSSetSamplers")]),
    ("ia-vertex-buffers", "array bind", [(CTX, "IASetVertexBuffers")]),
    ("cs-uavs", "array bind", [(CTX, "CSSetUnorderedAccessViews")]),
    ("shader-ps-gs", "bind of one object", [(CTX, "PSSetShader"), (CTX, "GSSetShader")]),
    ("shader-cs", "bind of one object", [(CTX, "CSSetShader")]),
    ("shader-hs-ds", "bind of one object", [(CTX, "HSSetShader"), (CTX, "DSSetShader")]),
    ("ia-layout-index", "bind of one object", [(CTX, "IASetInputLayout"), (CTX, "IASetIndexBuffer")]),
    ("rs-state-viewports", "bind of one object; state without an object", [(CTX, "RSSetState"), (CTX, "RSSetViewports")]),
    ("om-blend-depth", "bind of one object", [(CTX, "OMSetBlendState"), (CTX, "OMSetDepthStencilState")]),
    ("om-targets", "array bind", [(CTX, "OMSetRenderTargets")]),
    ("om-targets-uavs", "array bind", [(CTX, "OMSetRenderTargetsAndUnorderedAccessViews")]),
    ("topology", "state without an object", [(CTX, "IASetPrimitiveTopology")]),
    ("draw", "draw", [(CTX, "Draw"), (CTX, "DrawInstanced")]),
    ("draw-indexed-instanced", "draw", [(CTX, "DrawIndexedInstanced")]),
    ("draw-indirect", "draw with a buffer", [(CTX, "DrawInstancedIndirect"), (CTX, "DispatchIndirect")]),
    ("dispatch", "draw", [(CTX, "Dispatch")]),
    ("clear-dsv-uav", "clear", [(CTX, "ClearDepthStencilView"), (CTX, "ClearUnorderedAccessViewFloat")]),
    ("copy-region-resolve", "copy", [(CTX, "CopySubresourceRegion"), (CTX, "ResolveSubresource")]),
    ("clear-state-flush", "state without an object", [(CTX, "ClearState"), (CTX, "Flush")]),
]

BRIEF = """\
# Unit back-{id}: hand {names} on to the back end

This repository recompiles a 64-bit Windows game for arm64 macOS. Where the game expects a
Direct3D 11 device it gets our own objects: a device that draws nothing, the *front*. Each
method is one small C function, a *body*. A renderer will stand behind the front as a *back
end*: after a body's checks have passed it hands the call on through one table of functions
(`runtime/include/rt/d3dback.h`). Your task: make {these} hand {its} call on, and prove it with a
test against the *recording* back end, which counts every call and keeps its arguments.
**You do not decide what crosses: the notes below do. Write exactly that.**

## Files (edit or create exactly these, nothing else)

{file_list}

Nothing registers them: the build finds them by their path. Do NOT edit `rt/d3dback.h`,
`back.c`, `back_record.c`, a makefile, another body or another test.

## Read, in this order, and nothing else unless you get stuck

1. `runtime/include/rt/test_back.h`, whole: how a back test is written. Its body runs twice,
   with the recording back end (`T_BACK` true) and with none.
2. The model of this shape, body and test, to copy from:
{models}
3. The head comment of `runtime/include/rt/d3dback.h`, to the line "THE ENTRIES": the rules.
4. In `runtime/nulldev/back_record.c`, search for the name of each of your methods: the
   function `r_<Method>`, or the one line that makes it from a macro (`BIND_ARRAY`,
   `BIND_ONE`, `VIEW`, `STATE`, `SHADER`, `TEXTURE`: read the macro above it). It shows what
   the recording back end keeps as argument 0, 1, 2 ... (`O` an object, `V` a value, `P` a
   pointer, `A` an array of objects, `F` a FLOAT's bits).

## The `COM_METHOD` line of each body (it stays as it is)

{com_lines}

## What to write
{notes}
## Rules

- **After the checks.** The forward comes after every check and stop of the body, never
  before one: `T_BACK_STOPS_BEFORE(<a call that stops>)` in the test proves it for each stop.
  Where the method takes an object as an ARGUMENT (not `This`: a call through the wrong
  object's slots is not this method at all), an object of the wrong class is a stop too
  (`rt_nulldev_arg` makes it; its message reads
  "... is an object of class ..., where ... is expected", which is the piece for `RT-TRAPS`
  when you write that tag): pass a `T_BACK_NEW` object of any other class, cast, for example
  `(void *)T_BACK_NEW(device, &rt_nulldev_query_class)`.
- **Exactly once.** Every path on which the body returns passes one of the macros exactly
  once; the test's wrapper sums the counts after your test and fails otherwise.
- An object crosses as `rt_d3dback_of(object)` (NULL gives NULL); the context's own back
  object is `c->child.back`. Never pass a game pointer to an object on.
- With no back end nothing changes: the macros do nothing then, and everything the test
  checks outside `if (T_BACK)` holds in both halves.
- The test says at its top where its expected values come from (the rules of
  `rt/d3dback.h`, by number); never from the body. If a stop listed above does not stop, or
  a value differs, do not change what the notes prescribe: say so in your reply.
- An argument the body never uses gets `(void)name;`: the build has no warnings.
- If a type, constant or function you need does not exist, do not add it: stop and say
  exactly what is missing.
- Three things nobody in this project does: read or run `OriginSDK.dll` or `Titanfall2.exe`;
  touch anything that calls EA's Origin SDK or looks like a licence check; run a Windows
  executable. This task needs nothing under `game/`: do not open it.

## Done

    make back-check ROW={id}

It checks that only your files changed, that each body uses its entry and no other, that the
test has a `T_BACK_STOPS_BEFORE` for every stop of the body, builds, and runs your tests in
both halves and the method's older test where there is one. Under two minutes.
"""


def entries():
    """[(entry, interface or '-')] from rt/d3dback.h, in the table's order."""
    text = open(os.path.join(ROOT, "runtime", "include", "rt", "d3dback.h")).read()
    block = text[text.index("#define RT_D3DBACK_ENTRIES(X)"):text.index("#define RT_D3DBACK_ENUM_")]
    return re.findall(r"X\((\w+), ([\w-]+)\)", block)


def body_path(p):
    return "runtime/nulldev/%s/%s.c" % p


def test_path(p):
    return "runtime/tests/back/%s/%s.c" % p


def read(rel):
    path = os.path.join(ROOT, rel)
    return open(path).read() if os.path.exists(path) else None


def forwards(p):
    """True when the body of method p uses its entry in one of the seam's macros."""
    text = read(body_path(p))
    return bool(text and re.search(r"\bRT_BACK(?:_SAME|_CREATE|_ANSWER|_ALONE)?\s*\((?:[^;]*?,\s*)?%s\b" % re.escape(p[1]), text))


def done(p):
    return forwards(p) and read(test_path(p)) is not None


def row_done(r):
    return all(done(p) for p in r[2])


def problems():
    out = []
    ents = entries()
    desc = d3d_rows.descriptions()
    in_rows = {}
    ids = set()
    for rid, shape, methods in ROWS:
        if rid in ids:
            out.append("row %s: the id is used twice" % rid)
        ids.add(rid)
        if not 1 <= len(methods) <= 2:
            out.append("row %s: %d methods; a row is one or two" % (rid, len(methods)))
        for p in methods:
            if p in in_rows:
                out.append("%s::%s is in row %s and in row %s" % (p + (in_rows[p], rid)))
            in_rows[p] = rid
            if p in DONE:
                out.append("%s::%s is in row %s and is DONE by unit pic-seam" % (p + (rid,)))
            if p not in M:
                out.append("row %s: no notes for %s::%s" % ((rid,) + p))
                continue
            if p not in desc:
                out.append("row %s: %s::%s is not a described method" % ((rid,) + p))
            body = read(body_path(p))
            if body is None:
                out.append("row %s: %s does not exist (a row edits a body or replaces a skeleton)" % (rid, body_path(p)))
            elif M[p]["skeleton"] != ("THE SKELETON OF A BACK-END ROW" in body) and not done(p):
                out.append("row %s: %s is %sa skeleton, and the notes say otherwise" % (rid, body_path(p), "" if M[p]["skeleton"] else "not "))
            model = M[p]["model"]
            for path in (body_path(model), test_path(model)):
                if read(path) is None:
                    out.append("row %s: the model %s does not exist" % (rid, path))
            if model not in DONE:
                out.append("row %s: the model %s::%s is not a worked example of unit pic-seam" % ((rid,) + model))
    for p in DONE:
        if not forwards(p):
            out.append("%s::%s is DONE and %s does not hand its entry on" % (p + (body_path(p),)))
        if read(test_path(p)) is None:
            out.append("%s::%s is DONE and %s does not exist" % (p + (test_path(p),)))
    for entry, iface in ents:
        if iface == "-":
            if entry not in SEAM_OWN:
                out.append("entry %s serves no method and is not one of the seam's own" % entry)
            continue
        p = (iface, entry)
        if p not in DONE and p not in in_rows:
            out.append("entry %s (%s::%s) is neither done nor in a row" % (entry, iface, entry))
    served = {(i, e) for e, i in ents if i != "-"}
    for p in list(DONE) + list(in_rows):
        if p not in served:
            out.append("%s::%s has no entry in rt/d3dback.h" % p)
    return out


def counts():
    ents = entries()
    left = [r for r in ROWS if not row_done(r)]
    by_shape = {}
    for rid, shape, methods in left:
        by_shape[shape] = by_shape.get(shape, 0) + 1
    return ("%d entries: %d of the seam's own, %d methods done by pic-seam, %d methods in %d rows; rows: %d done, %d to do (%s)" % (
        len(ents), len(SEAM_OWN), len(DONE), sum(len(r[2]) for r in ROWS), len(ROWS), len(ROWS) - len(left), len(left),
        "; ".join("%s %d" % kv for kv in sorted(by_shape.items())) or "none"))


def build(r):
    rid, shape, methods = r
    desc = d3d_rows.descriptions()
    uid = "back-" + rid
    files, notes, models = [], [], []
    for p in methods:
        m = M[p]
        files.append("- `%s` (%s)" % (body_path(p), "a SKELETON: you replace its body" if m["skeleton"] else "exists: you add to it"))
        files.append("- `%s` (new)" % test_path(p))
        line = "   `%s` and `%s`" % (body_path(m["model"]), test_path(m["model"]))
        if line not in models:
            models.append(line)
        notes.append("\n### `%s::%s`\n\n**The body.** %s\n\n**The test** (`BACK_TEST(%s, %s)`). %s\n" % (p[0], p[1], m["body"], p[0], p[1], m["test"]))
    pretty = ", ".join("`%s::%s`" % p for p in methods)
    brief = BRIEF.format(id=rid, names=pretty, these="these methods" if len(methods) > 1 else "this method",
                         its="their" if len(methods) > 1 else "its", file_list="\n".join(files), models="\n".join(models),
                         com_lines="\n".join("    " + d3d_rows.com_line(i, m, desc) for i, m in methods), notes="".join(notes))
    unit = {"id": uid, "title": "Back-end seam: %s" % ", ".join("%s::%s" % p for p in methods), "kind": "back-task", "issue": 35,
            "model": "haiku", "priority": 20, "needs": ["pic-seam"], "check": ["make back-check ROW=%s" % rid], "state": "todo"}
    return uid, brief, unit


def row(rid):
    for r in ROWS:
        if rid in (r[0], "back-" + r[0]):
            return r
    return None


def main():
    args = sys.argv[1:]
    if args == ["--check"]:
        p = problems()
        for line in p:
            print(line)
        print(counts() + (": %d problems" % len(p) if p else ": ok"))
        sys.exit(1 if p else 0)
    elif args == ["--list"]:
        for r in ROWS:
            print("%-28s %-44s %s" % ("back-" + r[0], r[1], " ".join("%s::%s[%s]" % (i, m, "done" if done((i, m)) else "skeleton" if M[(i, m)]["skeleton"] else "body")
                                                                         for i, m in r[2])))
    elif len(args) == 2 and args[0] == "--show":
        r = row(args[1])
        if r is None:
            sys.exit("no row %s" % args[1])
        print(build(r)[1])
    elif args == ["--write"]:
        for r in ROWS:
            uid, brief, unit = build(r)
            with open(os.path.join(ROOT, "swarm", "briefs", uid + ".md"), "w") as f:
                f.write(brief)
            path = os.path.join(ROOT, "swarm", "units", uid + ".json")
            if os.path.exists(path):
                continue
            if row_done(r):
                unit["state"] = "held"
            with open(path, "w") as f:
                json.dump(unit, f, indent=2)
                f.write("\n")
            print("wrote %s (%d lines of brief)" % (uid, brief.count("\n")))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
