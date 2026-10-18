# metalbench

Offscreen Metal measurements of this GPU (Apple M3 Pro, 14 cores) for the planner. It never opens a window, shows anything or plays a sound. Usage: `build/metalbench/metalbench [--width W] [--height H] [--frames N] [--quick]` (defaults 3024, 1890, 60; `--quick` is N = 5). The first line is the device; then one JSON line per measurement. Each measurement is one command buffer per pass, committed and waited on, with its GPU start and end times read; the first 3 are warm-up and the mean of the next N is printed.

- **clear_store_mrt3**: a render pass with three RGBA16Float targets and a Depth32Float_Stencil8 depth target, Clear load and Store store, no draws.
- **fill_mrt3**: the same targets, Clear and Store, one full-screen triangle writing a constant half4 to each target, depth test off.
- **reload_mrt3**: as fill_mrt3 but with the Load load action, so the previous content is read back.
- **overdraw8_blend_mrt3**: as fill_mrt3 with 8 full-screen triangles and additive blending on all three targets, which defeats hidden-surface removal.
- **alu_float** and **alu_half**: one RGBA16Float target; a loop of 64 iterations of three multiply-adds and one sin on a 4-component vector seeded from the pixel position, in float4 and in half4.
- **sample_bilinear** and **sample_aniso16**: one RGBA16Float target; a full-screen triangle taking 8 samples per pixel from a 2048 by 2048 RGBA8 texture with full mips, bilinear (linear filter, nearest mip, no anisotropy) and with max anisotropy 16 (linear mip).

`gpix_per_s` is shaded pixels per second: W times H per pass, times 8 for the overdraw test. `ms_per_pass` is the mean GPU time of one pass.
