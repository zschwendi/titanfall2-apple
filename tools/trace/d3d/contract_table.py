#!/usr/bin/env python3
"""contract_table.py: hold the session's census (a summary's JSON from d3dtrace.py) against the
device contract's table of 146 methods (docs/handoff/d3d-contract.md, section 7).

    contract_table.py build/trace/summary.json docs/handoff/d3d-contract.md used      the methods the session called
    contract_table.py build/trace/summary.json docs/handoff/d3d-contract.md unused    the contract's methods it never called
    contract_table.py build/trace/summary.json docs/handoff/d3d-contract.md outside   what it called outside the 146

The kind comes from the contract's table; the last column of `used` is this unit's reading of the
session's arguments against the contract's row (NOTES below, written by hand from the summary's
tables), "inside" where nothing was found.
"""
import sys, json, re

NOTES = {
    'ID3D11Device::QueryInterface': 'IID_IDXGIDevice1 once: inside. The second (a private IID, call 13) is not the game\'s [reading]',
    'ID3D11DeviceContext::Release': 'call 14, to 1: not the game\'s [reading]',
    'ID3D11Texture2D::QueryInterface': 'always IID_ID3D11Texture2D, on what a view\'s GetResource returned: inside',
    'IDXGIAdapter1::GetDesc': 'inside; the real answer is that machine\'s card, not ours to copy',
    'IDXGIOutput::GetDesc': 'inside; the real desktop is 5120 by 2880, rotation "unspecified", monitor handle 1',
    'IDXGIOutput::GetDisplayModeList': 'R8G8B8A8_UNORM, flags 0, count then list: inside. Real list: 46 modes, all 60 Hz, 1920x1080 among them (D4)',
    'IDXGIAdapter1::EnumOutputs': 'index 0, then 1 = DXGI_ERROR_NOT_FOUND: inside',
    'IDXGIFactory1::EnumAdapters1': 'index 0, then 1 = DXGI_ERROR_NOT_FOUND: inside',
    'IDXGIFactory1::MakeWindowAssociation': 'flags NO_WINDOW_CHANGES and NO_ALT_ENTER: inside',
    'IDXGIFactory1::CreateSwapChain': 'explicit 1920x1080, R8G8B8A8_UNORM_SRGB, 3 buffers, usage SHADER_INPUT with RENDER_TARGET_OUTPUT and BACK_BUFFER, windowed, DISCARD, flag ALLOW_MODE_SWITCH: see stops, row 7',
    'IDXGISwapChain::ResizeTarget': '**outside: a stop.** Twice at the start (stops, rows 8 and 11)',
    'IDXGISwapChain::SetFullscreenState': '**TRUE is outside: a stop** (stops, row 9); (FALSE, NULL) once at the end: inside',
    'IDXGISwapChain::GetFullscreenState': 'the arguments are inside (target pointer NULL), **the answer is not what the real device wrote** (TRUE, 4 of 4): stops, row 10',
    'IDXGISwapChain::ResizeBuffers': 'BufferCount 0, 1920x1080, R8G8B8A8_UNORM_SRGB, ALLOW_MODE_SWITCH, before any GetBuffer: for `d3d-resources` (0 buffers means "as many as before": Microsoft, from memory)',
    'IDXGISwapChain::GetBuffer': 'buffer 0, IID_ID3D11Texture2D, once: inside. The buffer is then given a render-target view AND a shader-resource view, and its description is read (stops, row 13)',
    'IDXGISwapChain::GetContainingOutput': 'inside; 3 calls, each followed by the gamma pair',
    'IDXGISwapChain::Present': 'SyncInterval 1, flags 0 always: inside. The real device answered DXGI_STATUS_OCCLUDED 72 times at the end; ours never does',
    'IDXGIOutput::GetGammaControlCapabilities': '**outside: a stop** (stops, rows 15 and 19)',
    'IDXGIOutput::SetGammaControl': 'inside (accepted); a 1,024-point curve, scale 1, offset 0',
    'ID3D11Device::GetFeatureLevel': '26 calls, real answer 11_0: inside',
    'ID3D11Device::CreateBuffer': '16 kinds of description, all four usages: `docs/trace/d3d-creators.md`',
    'ID3D11Device::CreateTexture2D': '38 kinds, 22 formats incl. block-compressed and typeless, arrays to 102 slices, cubes, MipLevels never 0: `d3d-creators`',
    'ID3D11Device::CreateTexture3D': 'one kind, 32x32x32: `d3d-creators`',
    'ID3D11Device::CreateShaderResourceView': 'always with a description; typed views of typeless textures, parts of buffers, cube arrays; also on the swap chain\'s buffer: `d3d-creators`',
    'ID3D11Device::CreateUnorderedAccessView': 'on textures, single slices of an array, and buffers: `d3d-creators`',
    'ID3D11Device::CreateRenderTargetView': 'always with a description, mip 0: `d3d-creators`',
    'ID3D11Device::CreateDepthStencilView': 'flags 0 and the two READ_ONLY flags: `d3d-creators`',
    'ID3D11Device::CreateInputLayout': '39 layouts, up to 9 elements, per-instance data in slot 1: `d3d-creators`',
    'ID3D11Device::CreateVertexShader': 'vs_4_0 and vs_5_0, linkage always NULL: inside',
    'ID3D11Device::CreatePixelShader': 'ps_5_0 and ps_4_0, linkage always NULL: inside',
    'ID3D11Device::CreateGeometryShader': 'gs_4_0 and gs_5_0, linkage always NULL: inside',
    'ID3D11Device::CreateComputeShader': 'cs_5_0, linkage always NULL: inside',
    'ID3D11Device::CreateBlendState': 'fields of switched-off targets hold values outside the enumerations; equal descriptions got the same object (stops, row 3; D6)',
    'ID3D11Device::CreateDepthStencilState': '14 descriptions, 8 objects on the real device (D6)',
    'ID3D11Device::CreateRasterizerState': '10 descriptions, 10 objects',
    'ID3D11Device::CreateSamplerState': '498 descriptions in 1,171 calls; MaxAnisotropy 0 in 24 calls; equal descriptions got the same object 673 of 673 times (stops, row 2; D6)',
    'ID3D11Device::CreateQuery': 'OCCLUSION 4,096 (never used afterwards), TIMESTAMP 32, EVENT 4, TIMESTAMP_DISJOINT 2; misc flags 0: `d3d-queries`',
    'ID3D11DeviceContext::Map': 'five map types, flags always 0, subresource always 0; 28 maps stay open over every Present; one READ of a staging texture: `d3d-map`, stops rows 4 and 23',
    'ID3D11DeviceContext::Unmap': 'with Map',
    'ID3D11DeviceContext::Begin': 'only TIMESTAMP_DISJOINT: `d3d-queries`',
    'ID3D11DeviceContext::End': 'EVENT, TIMESTAMP, TIMESTAMP_DISJOINT; an EVENT query is ended without a Begin (Microsoft: so it is meant; from memory): `d3d-queries`',
    'ID3D11DeviceContext::GetData': 'EVENT with a NULL buffer of size 0; TIMESTAMP 8 bytes; TIMESTAMP_DISJOINT 16 bytes; never OCCLUSION: stops rows 16 and 21',
    'ID3D11DeviceContext::VSSetConstantBuffers': 'slots 0 to 3, one buffer a call, never a NULL array: inside the 14 slots',
    'ID3D11DeviceContext::PSSetConstantBuffers': 'slots 0 to 3, one buffer a call, never a NULL array: inside the 14 slots',
    'ID3D11DeviceContext::PSSetShaderResources': 'slots 0 to 42, up to 16 a call, arrays of NULLs, never a NULL array: inside the 128 slots',
    'ID3D11DeviceContext::PSSetSamplers': 'from slot 0, 1 to 16 a call, arrays with NULLs: inside the 16 slots',
    'ID3D11DeviceContext::VSSetShader': 'class instances always NULL and 0; a NULL shader 324 times: inside if NULL unbinds',
    'ID3D11DeviceContext::PSSetShader': 'class instances always NULL and 0; a NULL shader 27,646 times: inside if NULL unbinds',
    'ID3D11DeviceContext::GSSetShader': 'class instances always NULL and 0; a NULL shader 4,830 times: inside if NULL unbinds',
    'ID3D11DeviceContext::IASetInputLayout': 'NULL 324 times: inside if NULL unbinds',
    'ID3D11DeviceContext::IASetPrimitiveTopology': 'TRIANGLELIST, POINTLIST, TRIANGLESTRIP: inside the ten',
    'ID3D11DeviceContext::OMSetRenderTargets': '0 to 3 views. **Check the row against two forms**: 0 views with an array pointer and a depth view (485 calls), and 3 views of which some are NULL (484)',
    'ID3D11DeviceContext::OMSetRenderTargetsAndUnorderedAccessViews': '1 view, a depth view, 1 unordered-access view at slot 2, initial counts NULL (81 calls): inside',
    'ID3D11DeviceContext::OMSetBlendState': 'factor always NULL, mask 0xffffffff; once (NULL, NULL, 0): inside',
    'ID3D11DeviceContext::OMSetDepthStencilState': 'reference always 0; NULL once: inside',
    'ID3D11DeviceContext::RSSetState': 'NULL once: inside',
    'ID3D11DeviceContext::RSSetViewports': 'always one viewport; depth range 0 to 1, 0.1 or 0.05: inside',
    'ID3D11DeviceContext::RSGetViewports': 'count 1, room for one: inside. **The only Get method of the session** (D7)',
    'ID3D11DeviceContext::IASetVertexBuffers': 'slots 0 and 1; once per mission frame 0 buffers with NULL strides: inside (accepted)',
    'ID3D11DeviceContext::IASetIndexBuffer': 'always R16_UINT, offset 0; NULL buffer 7,467 times: inside (accepted)',
    'ID3D11DeviceContext::CSSetShaderResources': 'incl. 0 views and 128 NULLs: inside (accepted)',
    'ID3D11DeviceContext::CSSetUnorderedAccessViews': '1 or 8 views from slot 0, initial counts NULL: inside (accepted)',
    'ID3D11DeviceContext::UpdateSubresource': 'buffers and textures of usage DEFAULT, mostly with a box: inside (accepted): `d3d-map`',
    'ID3D11DeviceContext::CopySubresourceRegion': 'mip by mip between block-compressed textures; staging buffer to structured buffer: inside (accepted)',
    'ID3D11DeviceContext::CopyResource': 'incl. a render target into the staging texture that is then mapped for reading (stops, row 23)',
    'ID3D11DeviceContext::Dispatch': 'inside (accepted); never DispatchIndirect',
    'ID3D11RenderTargetView::GetResource': '9,065 calls: inside',
    'ID3D11Texture2D::GetDesc': '8,092 calls, on render targets and on the swap chain\'s buffer: inside once that buffer\'s record has a description (stops, row 13)',
}


def contract_rows(path):
    rows = {}
    for line in open(path, encoding='utf-8'):
        m = re.match(r'\| `([A-Za-z0-9_]+::[A-Za-z0-9_]+)` \| ([^|]+) \|', line)
        if m:
            rows[m.group(1)] = m.group(2).strip().replace('**', '')
    return rows


def main(argv):
    S = json.load(open(argv[1]))
    C = contract_rows(argv[2])
    T = S['census']
    what = argv[3]
    if what == 'used':
        print('| Method | calls | first call | kind in our contract | the session\'s arguments against the row |')
        print('|---|---|---|---|---|')
        for m in sorted((m for m in T if m in C), key=lambda m: T[m][0]):
            print('| `%s` | %d | %d | %s | %s |' % (m, T[m][2], T[m][0], C[m], NOTES.get(m, 'inside')))
    elif what == 'unused':
        by = {}
        for m in C:
            if m not in T:
                by.setdefault(C[m], []).append(m)
        print('| kind in our contract | never called in this session | methods |')
        print('|---|---|---|')
        for k in sorted(by, key=lambda k: -len(by[k])):
            print('| %s | %d | %s |' % (k, len(by[k]), ', '.join('`%s`' % m for m in sorted(by[k]))))
    elif what == 'outside':
        print('| Function | calls | first call | frame |')
        print('|---|---|---|---|')
        for m in sorted((m for m in T if m not in C), key=lambda m: T[m][0]):
            print('| `%s` | %d | %d | %d |' % (m, T[m][2], T[m][0], T[m][1]))
    else:
        sys.stderr.write(__doc__)
        return 2
    used = [m for m in T if m in C]
    sys.stderr.write('%d functions in the session, %d of them among the contract\'s %d; %d of the %d never called\n' % (
        len(T), len(used), len(C), len(C) - len(used), len(C)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
