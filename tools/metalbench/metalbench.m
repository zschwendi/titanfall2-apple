// metalbench: offscreen Metal measurements of this GPU's own rates (tools/metalbench/README.md).
// Never opens a window, shows anything or plays a sound: every pass draws into offscreen textures.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kShaderSource =
    "#include <metal_stdlib>\n"
    "using namespace metal;\n"
    "struct VOut { float4 pos [[position]]; };\n"
    "vertex VOut fst_vertex(uint vid [[vertex_id]]) {\n"
    "  float2 p = float2(float((vid << 1) & 2u), float(vid & 2u));\n"
    "  VOut o; o.pos = float4(p * 2.0 - 1.0, 0.0, 1.0); return o;\n"
    "}\n"
    "struct MRT3 { half4 a [[color(0)]]; half4 b [[color(1)]]; half4 c [[color(2)]]; };\n"
    "fragment MRT3 fill_mrt3_frag() {\n"
    "  MRT3 o;\n"
    "  o.a = half4(0.25h, 0.5h, 0.75h, 1.0h);\n"
    "  o.b = half4(0.25h, 0.5h, 0.75h, 1.0h);\n"
    "  o.c = half4(0.25h, 0.5h, 0.75h, 1.0h);\n"
    "  return o;\n"
    "}\n"
    "fragment float4 alu_float_frag(float4 fp [[position]]) {\n"
    "  float4 v = float4(fp.xy * 0.001f, fp.x * fp.y * 1e-6f, 0.5f);\n"
    "  const float4 a = float4(1.01f, 0.99f, 1.03f, 0.97f);\n"
    "  const float4 b = float4(0.01f, -0.02f, 0.03f, -0.01f);\n"
    "  for (int i = 0; i < 64; i++) {\n"
    "    v = fma(v, a, b); v = fma(v, a, b); v = fma(v, a, b);\n"
    "    v = sin(v);\n"
    "  }\n"
    "  return v;\n"
    "}\n"
    "fragment half4 alu_half_frag(float4 fp [[position]]) {\n"
    "  half4 v = half4(float4(fp.xy * 0.001f, fp.x * fp.y * 1e-6f, 0.5f));\n"
    "  const half4 a = half4(1.01h, 0.99h, 1.03h, 0.97h);\n"
    "  const half4 b = half4(0.01h, -0.02h, 0.03h, -0.01h);\n"
    "  for (int i = 0; i < 64; i++) {\n"
    "    v = fma(v, a, b); v = fma(v, a, b); v = fma(v, a, b);\n"
    "    v = sin(v);\n"
    "  }\n"
    "  return v;\n"
    "}\n"
    "fragment half4 sample_frag(float4 fp [[position]],\n"
    "                           texture2d<float> tex [[texture(0)]],\n"
    "                           sampler smp [[sampler(0)]]) {\n"
    "  float2 base = float2(fp.x * 0.9f + fp.y * 0.1f, fp.y * 0.9f + fp.x * 0.05f) / 1024.0f;\n"
    "  float4 acc = float4(0.0f);\n"
    "  for (int k = 0; k < 8; k++) {\n"
    "    float2 uv = base + float2(float(k) * 0.0137f, float(k) * 0.0071f);\n"
    "    acc += tex.sample(smp, uv);\n"
    "  }\n"
    "  return half4(acc * 0.125f);\n"
    "}\n";

static id<MTLDevice> gDev;
static id<MTLCommandQueue> gQ;
static id<MTLLibrary> gLib;
static NSUInteger gW = 3024, gH = 1890, gN = 60;
static id<MTLTexture> gTgt[3], gDepth, gOne, gTex;
static id<MTLRenderPipelineState> gPipeMRT, gPipeMRTBlend, gPipeAluF, gPipeAluH, gPipeSample;
static id<MTLDepthStencilState> gDSOff;
static id<MTLSamplerState> gSmpBil, gSmpAniso;

static void die(const char *msg) {
    fprintf(stderr, "metalbench: %s\n", msg);
    exit(1);
}

static NSString *jsonEscape(NSString *s) {
    NSMutableString *m = [NSMutableString stringWithString:s];
    [m replaceOccurrencesOfString:@"\\" withString:@"\\\\" options:0 range:NSMakeRange(0, m.length)];
    [m replaceOccurrencesOfString:@"\"" withString:@"\\\"" options:0 range:NSMakeRange(0, m.length)];
    return m;
}

static id<MTLTexture> makeTarget(MTLPixelFormat fmt, NSUInteger w, NSUInteger h) {
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt
                                                                                  width:w
                                                                                 height:h
                                                                              mipmapped:NO];
    td.usage = MTLTextureUsageRenderTarget;
    td.storageMode = MTLStorageModePrivate;
    id<MTLTexture> t = [gDev newTextureWithDescriptor:td];
    if (!t) die("cannot create render target");
    return t;
}

static id<MTLRenderPipelineState> makePipe(NSString *fragName, NSUInteger nColors, BOOL blend, BOOL withDepth) {
    MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
    pd.vertexFunction = [gLib newFunctionWithName:@"fst_vertex"];
    pd.fragmentFunction = [gLib newFunctionWithName:fragName];
    if (!pd.vertexFunction || !pd.fragmentFunction) die("shader function missing");
    for (NSUInteger i = 0; i < nColors; i++) {
        MTLRenderPipelineColorAttachmentDescriptor *c = pd.colorAttachments[i];
        c.pixelFormat = MTLPixelFormatRGBA16Float;
        if (blend) {
            c.blendingEnabled = YES;
            c.rgbBlendOperation = MTLBlendOperationAdd;
            c.alphaBlendOperation = MTLBlendOperationAdd;
            c.sourceRGBBlendFactor = MTLBlendFactorOne;
            c.destinationRGBBlendFactor = MTLBlendFactorOne;
            c.sourceAlphaBlendFactor = MTLBlendFactorOne;
            c.destinationAlphaBlendFactor = MTLBlendFactorOne;
        }
    }
    if (withDepth) {
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        pd.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    }
    NSError *err = nil;
    id<MTLRenderPipelineState> p = [gDev newRenderPipelineStateWithDescriptor:pd error:&err];
    if (!p) {
        fprintf(stderr, "metalbench: pipeline %s: %s\n", fragName.UTF8String, err.localizedDescription.UTF8String);
        exit(1);
    }
    return p;
}

static void setupResources(void) {
    NSError *err = nil;
    gLib = [gDev newLibraryWithSource:[NSString stringWithUTF8String:kShaderSource] options:nil error:&err];
    if (!gLib) {
        fprintf(stderr, "metalbench: shader compile: %s\n", err.localizedDescription.UTF8String);
        exit(1);
    }
    for (int i = 0; i < 3; i++) gTgt[i] = makeTarget(MTLPixelFormatRGBA16Float, gW, gH);
    gOne = makeTarget(MTLPixelFormatRGBA16Float, gW, gH);
    {
        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                                                       width:gW
                                                                                      height:gH
                                                                                   mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget;
        dd.storageMode = MTLStorageModePrivate;
        gDepth = [gDev newTextureWithDescriptor:dd];
        if (!gDepth) die("cannot create depth target");
    }
    gPipeMRT = makePipe(@"fill_mrt3_frag", 3, NO, YES);
    gPipeMRTBlend = makePipe(@"fill_mrt3_frag", 3, YES, YES);
    gPipeAluF = makePipe(@"alu_float_frag", 1, NO, NO);
    gPipeAluH = makePipe(@"alu_half_frag", 1, NO, NO);
    gPipeSample = makePipe(@"sample_frag", 1, NO, NO);

    MTLDepthStencilDescriptor *dsd = [MTLDepthStencilDescriptor new];
    dsd.depthCompareFunction = MTLCompareFunctionAlways;
    dsd.depthWriteEnabled = NO;
    gDSOff = [gDev newDepthStencilStateWithDescriptor:dsd];

    // The 2048 x 2048 RGBA8 texture with full mip chain, filled from a staging buffer.
    const NSUInteger T = 2048;
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                                  width:T
                                                                                 height:T
                                                                              mipmapped:YES];
    td.usage = MTLTextureUsageShaderRead;
    td.storageMode = MTLStorageModePrivate;
    gTex = [gDev newTextureWithDescriptor:td];
    if (!gTex) die("cannot create sample texture");
    id<MTLBuffer> stage = [gDev newBufferWithLength:T * T * 4 options:MTLResourceStorageModeShared];
    uint8_t *px = (uint8_t *)stage.contents;
    for (NSUInteger y = 0; y < T; y++)
        for (NSUInteger x = 0; x < T; x++) {
            uint8_t *p = px + (y * T + x) * 4;
            p[0] = (uint8_t)(x ^ y);
            p[1] = (uint8_t)(x * 3 + y);
            p[2] = (uint8_t)((x + y) * 5);
            p[3] = 255;
        }
    id<MTLCommandBuffer> cb = [gQ commandBuffer];
    id<MTLBlitCommandEncoder> be = [cb blitCommandEncoder];
    [be copyFromBuffer:stage sourceOffset:0 sourceBytesPerRow:T * 4 sourceBytesPerImage:T * T * 4
            sourceSize:MTLSizeMake(T, T, 1) toTexture:gTex destinationSlice:0 destinationLevel:0
     destinationOrigin:MTLOriginMake(0, 0, 0)];
    [be generateMipmapsForTexture:gTex];
    [be endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.status == MTLCommandBufferStatusError) die("texture upload failed");

    MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new];
    sd.minFilter = MTLSamplerMinMagFilterLinear;
    sd.magFilter = MTLSamplerMinMagFilterLinear;
    sd.mipFilter = MTLSamplerMipFilterNearest;   // bilinear within a mip, nearest mip
    sd.sAddressMode = MTLSamplerAddressModeRepeat;
    sd.tAddressMode = MTLSamplerAddressModeRepeat;
    sd.maxAnisotropy = 1;
    gSmpBil = [gDev newSamplerStateWithDescriptor:sd];
    sd.mipFilter = MTLSamplerMipFilterLinear;     // trilinear, 16x anisotropy
    sd.maxAnisotropy = 16;
    gSmpAniso = [gDev newSamplerStateWithDescriptor:sd];
}

static MTLRenderPassDescriptor *mrtPass(MTLLoadAction la) {
    MTLRenderPassDescriptor *d = [MTLRenderPassDescriptor renderPassDescriptor];
    for (int i = 0; i < 3; i++) {
        d.colorAttachments[i].texture = gTgt[i];
        d.colorAttachments[i].loadAction = la;
        d.colorAttachments[i].storeAction = MTLStoreActionStore;
        d.colorAttachments[i].clearColor = MTLClearColorMake(0, 0, 0, 0);
    }
    d.depthAttachment.texture = gDepth;
    d.depthAttachment.loadAction = la;
    d.depthAttachment.storeAction = MTLStoreActionStore;
    d.depthAttachment.clearDepth = 1.0;
    d.stencilAttachment.texture = gDepth;
    d.stencilAttachment.loadAction = la;
    d.stencilAttachment.storeAction = MTLStoreActionStore;
    d.stencilAttachment.clearStencil = 0;
    return d;
}

static MTLRenderPassDescriptor *oneTargetPass(void) {
    MTLRenderPassDescriptor *d = [MTLRenderPassDescriptor renderPassDescriptor];
    d.colorAttachments[0].texture = gOne;
    d.colorAttachments[0].loadAction = MTLLoadActionClear;
    d.colorAttachments[0].storeAction = MTLStoreActionStore;
    d.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    return d;
}

typedef void (^PassBody)(id<MTLRenderCommandEncoder> e);

// One command buffer per pass: N+3 buffers, the first 3 skipped as warm-up.
// Returns the mean GPU time in seconds over the N timed buffers.
static double timePass(MTLRenderPassDescriptor *d, PassBody body) {
    double sum = 0;
    NSUInteger counted = 0;
    for (NSUInteger i = 0; i < gN + 3; i++) {
        id<MTLCommandBuffer> cb = [gQ commandBuffer];
        id<MTLRenderCommandEncoder> e = [cb renderCommandEncoderWithDescriptor:d];
        if (body) body(e);
        [e endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status == MTLCommandBufferStatusError) die("command buffer failed");
        if (i >= 3) {
            double t = cb.GPUEndTime - cb.GPUStartTime;
            if (!(t > 0)) die("no GPU timestamps");
            sum += t;
            counted++;
        }
    }
    return sum / (double)counted;
}

static void report(NSString *test, double secs, double pixelsPerPass, NSString *notes) {
    double ms = secs * 1000.0;
    double gpix = pixelsPerPass / secs / 1e9;
    NSString *line = [NSString stringWithFormat:
        @"{\"test\": \"%@\", \"ms_per_pass\": %.6f, \"gpix_per_s\": %.4f, \"notes\": \"%@\"}",
        test, ms, gpix, jsonEscape(notes)];
    printf("%s\n", line.UTF8String);
    fflush(stdout);
}

static void fullTriangle(id<MTLRenderCommandEncoder> e) {
    [e drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
}

int main(int argc, const char *argv[]) {
    @autoreleasepool {
        NSUInteger quickN = 0;
        BOOL quick = NO;
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "--width") && i + 1 < argc) gW = (NSUInteger)atol(argv[++i]);
            else if (!strcmp(argv[i], "--height") && i + 1 < argc) gH = (NSUInteger)atol(argv[++i]);
            else if (!strcmp(argv[i], "--frames") && i + 1 < argc) gN = (NSUInteger)atol(argv[++i]);
            else if (!strcmp(argv[i], "--quick")) quick = YES;
            else {
                fprintf(stderr, "usage: metalbench [--width W] [--height H] [--frames N] [--quick]\n");
                return 2;
            }
        }
        if (quick) { quickN = 5; gN = quickN; }
        if (gW == 0 || gH == 0 || gN == 0) {
            fprintf(stderr, "usage: metalbench [--width W] [--height H] [--frames N] [--quick]\n");
            return 2;
        }

        gDev = MTLCreateSystemDefaultDevice();
        if (!gDev) die("no Metal device");
        gQ = [gDev newCommandQueue];
        if (!gQ) die("no command queue");

        setupResources();

        printf("{\"device\": \"%s\", \"width\": %lu, \"height\": %lu, \"frames\": %lu}\n",
               jsonEscape(gDev.name).UTF8String, (unsigned long)gW, (unsigned long)gH, (unsigned long)gN);
        fflush(stdout);

        double px = (double)gW * (double)gH;
        NSString *mrtNote = [NSString stringWithFormat:
            @"3 x RGBA16Float + Depth32Float_Stencil8 at %lux%lu; mean of %lu passes after 3 warm-up",
            (unsigned long)gW, (unsigned long)gH, (unsigned long)gN];

        // 1. clear_store_mrt3: no draws.
        {
            double s = timePass(mrtPass(MTLLoadActionClear), nil);
            report(@"clear_store_mrt3", s, px, [mrtNote stringByAppendingString:@"; Clear load, Store store, no draws; pixels = W*H"]);
        }
        // 2. fill_mrt3
        {
            double s = timePass(mrtPass(MTLLoadActionClear), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeMRT];
                [e setDepthStencilState:gDSOff];
                fullTriangle(e);
            });
            report(@"fill_mrt3", s, px, [mrtNote stringByAppendingString:@"; one full-screen triangle, constant half4 to 3 targets, depth test off; pixels = W*H"]);
        }
        // 3. reload_mrt3
        {
            double s = timePass(mrtPass(MTLLoadActionLoad), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeMRT];
                [e setDepthStencilState:gDSOff];
                fullTriangle(e);
            });
            report(@"reload_mrt3", s, px, [mrtNote stringByAppendingString:@"; as fill_mrt3 with Load load action (previous content kept); pixels = W*H"]);
        }
        // 4. overdraw8_blend_mrt3
        {
            double s = timePass(mrtPass(MTLLoadActionClear), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeMRTBlend];
                [e setDepthStencilState:gDSOff];
                for (int k = 0; k < 8; k++) fullTriangle(e);
            });
            report(@"overdraw8_blend_mrt3", s, px * 8.0, [mrtNote stringByAppendingString:@"; 8 full-screen triangles, additive blend on all 3 targets; pixels = 8*W*H shaded"]);
        }
        NSString *oneNote = [NSString stringWithFormat:
            @"1 x RGBA16Float at %lux%lu; mean of %lu passes after 3 warm-up; Clear load, Store store",
            (unsigned long)gW, (unsigned long)gH, (unsigned long)gN];
        // 5. alu_float
        {
            double s = timePass(oneTargetPass(), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeAluF];
                fullTriangle(e);
            });
            report(@"alu_float", s, px, [oneNote stringByAppendingString:@"; 64 iterations of 3 fma + sin in float4; pixels = W*H"]);
        }
        // 6. alu_half
        {
            double s = timePass(oneTargetPass(), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeAluH];
                fullTriangle(e);
            });
            report(@"alu_half", s, px, [oneNote stringByAppendingString:@"; 64 iterations of 3 fma + sin in half4; pixels = W*H"]);
        }
        // 7. sample_bilinear
        {
            double s = timePass(oneTargetPass(), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeSample];
                [e setFragmentTexture:gTex atIndex:0];
                [e setFragmentSamplerState:gSmpBil atIndex:0];
                fullTriangle(e);
            });
            report(@"sample_bilinear", s, px, [oneNote stringByAppendingString:@"; 8 samples per pixel of a 2048x2048 RGBA8 texture with mips; linear min/mag, nearest mip, max aniso 1; pixels = W*H"]);
        }
        // 8. sample_aniso16
        {
            double s = timePass(oneTargetPass(), ^(id<MTLRenderCommandEncoder> e) {
                [e setRenderPipelineState:gPipeSample];
                [e setFragmentTexture:gTex atIndex:0];
                [e setFragmentSamplerState:gSmpAniso atIndex:0];
                fullTriangle(e);
            });
            report(@"sample_aniso16", s, px, [oneNote stringByAppendingString:@"; 8 samples per pixel of a 2048x2048 RGBA8 texture with mips; linear min/mag, linear mip, max aniso 16; pixels = W*H"]);
        }
    }
    return 0;
}
