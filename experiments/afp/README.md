# x86-compatible floating point on Apple Silicon (2026-10-08)

Question: x86 SSE and plain ARM give different bits for NaN results, for min/max with a NaN
or two zeros, and for the upper lanes after a scalar operation. Does translated float code
have to emulate x86 in software, or can the CPU do it?

Machine: M3 Pro, macOS 27.0.1. `hw.optional.arm.FEAT_AFP = 1`.

`afp_probe.c` sets bits in the FPCR register from an ordinary process (no entitlement) and
runs single native instructions:

| Case | Plain ARM | FPCR.AH | FPCR.AH + NEP | x86 SSE |
|---|---|---|---|---|
| inf - inf | 7fc00000 | ffc00000 | ffc00000 | ffc00000 |
| quiet NaN (1) * signalling NaN (2) | 7fc22222 | 7fc11111 | 7fc11111 | 7fc11111 |
| min(NaN, 1.0) | NaN | 1.0 | 1.0 | 1.0 |
| min(1.0, NaN) | NaN | NaN | NaN | NaN |
| min(+0, -0) | -0 | -0 | -0 | -0 |
| scalar add, upper three lanes | zeroed | zeroed | kept | kept |

All three bits (FIZ bit 0, AH bit 1, NEP bit 2) can be set and read back.

## Reading

- With AH and NEP set, native `fadd/fsub/fmul/fmin/fmax` give x86 SSE results in every case
  tried, so scalar float instructions can be translated one for one with no software fix-ups.
- Caveat: these bits change how every float instruction on the thread behaves, including
  code clang generated for ordinary C and anything in system libraries. The mode has to be
  on only while lifted code runs and off across calls into host code, or host code must be
  shown not to care. That is a design decision for issue #11, not settled here.
- Only seven cases were tried. Division, square root, conversions, double precision,
  denormals with FIZ, and rounding modes are untested.
