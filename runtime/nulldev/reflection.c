/* The shader reflection object of the do-nothing device: what D3DReflect hands out (unit
 * d3d-reflect; rt/nulldev.h, NdReflection).
 *
 * WHAT IT KEEPS.  Not the shader's bytes: the input signature, parsed once by D3DReflect, as
 * the descriptions GetInputParameterDesc copies out, followed by the semantic names, in one
 * block of host memory that the object owns.  So every check of the container is made while
 * D3DReflect is the function being called (a container that does not parse stops there, with
 * that caller), no method of the object can meet a malformed byte, and the caller may change
 * or free its bytes as soon as D3DReflect has returned.
 *
 * HOW LONG A SEMANTIC NAME'S POINTER IS VALID.  Until the object is destroyed (its last
 * Release).  Microsoft's pages of D3DReflect, GetInputParameterDesc and
 * D3D11_SIGNATURE_PARAMETER_DESC (read 2026-10-11) state no lifetime for the string, and no
 * other source read states one.  Whether Windows' own string points into the caller's bytes,
 * and so would outlive the object, was not determined; the game's one user of the interface
 * (materialsystem_dx11+0x323b0) reads each name before its next call and releases the object
 * after the last.
 *
 * WHICH CHUNK.  The input signature is the container's ISGN chunk or its ISG1 chunk (research
 * 1.3, 1.4).  Exactly one of the two must be present: the implementations read disagree on
 * which wins when a container has both or two of one (Microsoft's parser ISG1, vkd3d the
 * first, DXVK the last), and what d3dcompiler_46 answers for a container with none is not
 * settled, so both cases are refused. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "rt/log.h"
#include "rt/nulldev.h"

ID3D11ShaderReflection *rt_nulldev_reflection_new(const void *bytes, SIZE_T size, char *why) {
    RtDxbc c;
    if (!rt_dxbc_open(bytes, size, &c, why)) return NULL;

    uint32_t tag = 0, len = 0, found = 0, n = 0;
    const uint8_t *data = NULL;
    char chunks[160] = "";
    size_t used = 0;
    for (uint32_t i = 0; i < c.nchunks; i++) {
        uint32_t t, l;
        const uint8_t *d;
        char text[5];
        rt_dxbc_chunk(&c, i, &t, &d, &l);
        rt_dxbc_tag_text(t, text);
        if (used < sizeof chunks) used += (size_t)snprintf(chunks + used, sizeof chunks - used, " %s:%u", text, l);
        if (t != RT_DXBC_ISGN && t != RT_DXBC_ISG1) continue;
        if (!found++) { tag = t; data = d; len = l; }
    }
    if (found != 1) {
        snprintf(why, RT_DXBC_WHY, "it has %u input-signature chunks (ISGN or ISG1) among its %u chunks; exactly one is read", found, c.nchunks);
        return NULL;
    }
    if (!rt_dxbc_signature_count(tag, data, len, &n, why)) return NULL;

    /* First pass: every element is checked and the names are measured. */
    size_t names = 0;
    RtDxbcElement e;
    for (uint32_t i = 0; i < n; i++) {
        if (!rt_dxbc_signature_element(tag, data, len, i, &e, why)) return NULL;
        names += (size_t)e.name_length + 1;
    }
    D3D11_SIGNATURE_PARAMETER_DESC *inputs = calloc(1, (size_t)n * sizeof *inputs + names + 1);
    if (!inputs) rt_fatal("rt_nulldev: no host memory for a reflection's %u input parameters", (unsigned)n);
    char *text = (char *)(inputs + n);
    for (uint32_t i = 0; i < n; i++) {
        (void)rt_dxbc_signature_element(tag, data, len, i, &e, why);    /* it passed above */
        memcpy(text, e.name, (size_t)e.name_length + 1);
        inputs[i].SemanticName = text;
        inputs[i].SemanticIndex = e.semantic_index;
        inputs[i].Register = e.reg;
        inputs[i].SystemValueType = e.system_value;
        inputs[i].ComponentType = e.component_type;
        inputs[i].Mask = e.mask;
        inputs[i].ReadWriteMask = e.rw_mask;
        inputs[i].Stream = e.stream;
        inputs[i].MinPrecision = e.min_precision;
        text += e.name_length + 1;
    }

    ID3D11ShaderReflection *obj = rt_com_new(&rt_nulldev_reflection_class);
    NdReflection *r = rt_com_state_of(obj, &rt_nulldev_reflection_class);
    r->bytecode_length = size;
    r->ninputs = n;
    r->inputs = inputs;

    /* The run's record of what was answered (docs/handoff/d3d-reflect.md compares it with the
     * input layouts the game then makes): chunk names and sizes, and the signature. */
    char tagtext[5];
    rt_dxbc_tag_text(tag, tagtext);
    rt_log(RT_CH_COM, RT_LL_INFO, "reflection #%llu: a container of %u bytes, checksum right, chunks%s; %s with %u input parameters",
           (unsigned long long)rt_com_serial(obj), c.size, chunks, tagtext, (unsigned)n);
    for (uint32_t i = 0; i < n; i++)
        rt_log(RT_CH_COM, RT_LL_INFO, "reflection #%llu: input %u: %s%u register %u system value %u component type %u mask %#x read %#x stream %u precision %u",
               (unsigned long long)rt_com_serial(obj), i, inputs[i].SemanticName, (unsigned)inputs[i].SemanticIndex,
               (unsigned)inputs[i].Register, (unsigned)inputs[i].SystemValueType, (unsigned)inputs[i].ComponentType,
               (unsigned)inputs[i].Mask, (unsigned)inputs[i].ReadWriteMask, (unsigned)inputs[i].Stream, (unsigned)inputs[i].MinPrecision);
    return obj;
}

void rt_nulldev_reflection_free(NdReflection *r) {
    free(r->inputs);
    r->inputs = NULL;
    r->ninputs = 0;
}

/* The run's record of an input layout, for the same comparison: the elements the game gave
 * CreateInputLayout and, beside them, the input signature in the bytes it passed with them
 * (the blob D3DGetBlobPart made, or a whole shader).  It only logs: what CreateInputLayout
 * accepts is its body's matter.  A semantic name is read here, at the call, as Windows must
 * read it to match the signature; at most 40 characters of it. */
void rt_nulldev_inputlayout_log(ID3D11InputLayout *layout, const D3D11_INPUT_ELEMENT_DESC *elements, UINT n,
                                const void *bytecode, SIZE_T length) {
    unsigned long long id = (unsigned long long)rt_com_serial(layout);
    rt_log(RT_CH_COM, RT_LL_INFO, "input layout #%llu: %u elements, with %llu bytes of signature", id, (unsigned)n, (unsigned long long)length);
    for (UINT i = 0; i < n; i++)
        rt_log(RT_CH_COM, RT_LL_INFO, "input layout #%llu: element %u: %.40s%u format %u slot %u offset %u class %u rate %u", id, (unsigned)i,
               elements[i].SemanticName ? elements[i].SemanticName : "(null)", (unsigned)elements[i].SemanticIndex,
               (unsigned)elements[i].Format, (unsigned)elements[i].InputSlot, (unsigned)elements[i].AlignedByteOffset,
               (unsigned)elements[i].InputSlotClass, (unsigned)elements[i].InstanceDataStepRate);
    RtDxbc c;
    char why[RT_DXBC_WHY];
    uint32_t count = 0, tag = 0, len = 0, found = 0;
    const uint8_t *data = NULL;
    if (!rt_dxbc_open(bytecode, length, &c, why)) {
        rt_log(RT_CH_COM, RT_LL_WARN, "input layout #%llu: the bytes passed do not open as a container: %s", id, why);
        return;
    }
    for (uint32_t i = 0; i < c.nchunks; i++) {
        uint32_t t, l;
        const uint8_t *d;
        rt_dxbc_chunk(&c, i, &t, &d, &l);
        if ((t == RT_DXBC_ISGN || t == RT_DXBC_ISG1) && !found++) { tag = t; data = d; len = l; }
    }
    if (found != 1 || !rt_dxbc_signature_count(tag, data, len, &count, why)) {
        rt_log(RT_CH_COM, RT_LL_WARN, "input layout #%llu: no single input signature in the bytes passed (%u found)", id, found);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        RtDxbcElement e;
        if (!rt_dxbc_signature_element(tag, data, len, i, &e, why)) {
            rt_log(RT_CH_COM, RT_LL_WARN, "input layout #%llu: %s", id, why);
            return;
        }
        rt_log(RT_CH_COM, RT_LL_INFO, "input layout #%llu: signature %u: %s%u register %u component type %u mask %#x read %#x", id, (unsigned)i,
               e.name, (unsigned)e.semantic_index, (unsigned)e.reg, (unsigned)e.component_type, (unsigned)e.mask, (unsigned)e.rw_mask);
    }
}
