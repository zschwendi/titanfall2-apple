// hlslcc2msl IN.dxbc OUT.metal: translate one Direct3D shader container to Metal source with
// Unity's HLSLcc (third_party/hlslcc, built into build/hlslcc/libhlslcc.a by tools/shaderxc/build.sh).
// Prints nothing of the shader; exit status 0 when the translation succeeded.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "hlslcc.h"

int main(int argc, char **argv) {
    if (argc != 3) { std::fprintf(stderr, "usage: hlslcc2msl IN.dxbc OUT.metal\n"); return 2; }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.empty()) { std::fprintf(stderr, "hlslcc2msl: cannot read %s\n", argv[1]); return 2; }
    GlExtensions ext = {};
    GLSLCrossDependencyData deps;
    HLSLccSamplerPrecisionInfo precisions;
    HLSLccReflection reflection;
    GLSLShader result;
    unsigned flags = getenv("HLSLCC_FLAGS") ? (unsigned)strtoul(getenv("HLSLCC_FLAGS"), 0, 0) : 0x1;  // uniform buffer objects (cbuffers as structs), as Unity's Metal path
    int ok = TranslateHLSLFromMem(data.data(), flags, LANG_METAL, &ext, &deps, precisions, reflection, &result);
    if (!ok || result.sourceCode.empty()) { std::fprintf(stderr, "hlslcc2msl: translation failed\n"); return 1; }
    std::ofstream out(argv[2], std::ios::binary);
    out << result.sourceCode;
    return out.good() ? 0 : 2;
}
