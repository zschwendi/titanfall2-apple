# 42: every shader of the recording through DXMT's standalone converter

12 October 2026. The recording of the real game's
Direct3D calls holds each shader's bytecode beside its text; our own reader of the
trace file took them out. The session creates **11,043 shaders,
which are 7,821 distinct containers**: 4,713 vertex (6,523 creations), 3,053 pixel (4,413), 26
geometry (74), 29 compute (33). No hull shader, no domain shader.

**In one paragraph.** DXMT's standalone `airconv` **converts every vertex, pixel and compute
container of the recording (7,795 of 7,795) and prints `unhandled dxbc instruction` for none of
them**: there is no opcode of this session that the converter does not know. Geometry shaders
cannot be given to the tool alone; with the vertex shader the recording drew them with they
convert too (7 pairs, both halves of each). **22 of the 26 geometry containers are created and
never drawn with** in the session, so the tool was not able to take them (its own message, not
a fault of the shader) and a replay of this recording will never ask DXMT for them. Nothing
was refused for a reason inside a shader. This says the converter accepts the shaders; it does
not say that Metal accepts what comes out or that the picture is right (no Metal device in
this study).

Only counts, stages, opcode numbers and the tool's own messages are here. The converted
output went to a scratch directory and was deleted after each run.

## 1. The release build (`third_party/dxmt/build-native`, the build that exists since finding 35)

7,831 runs in 28 s, six at once (median 0.02 s a shader, longest 0.15 s).

| stage | runs | converted, no `unhandled` line | converted with `unhandled` lines | refused | died | timeout |
|---|---|---|---|---|---|---|
| compute | 29 | 29 | 0 | 0 | 0 | 0 |
| pixel | 3,053 | 3,053 | 0 | 0 | 0 | 0 |
| vertex | 4,713 | 4,713 | 0 | 0 | 0 | 0 |
| geometry, with its vertex shader (`--vertex-before-geometry`) | 7 | 7 | 0 | 0 | 0 | 0 |
| vertex, before its geometry shader (`--geometry-after-vertex`) | 7 | 7 | 0 | 0 | 0 | 0 |
| geometry, alone (never drawn with in the session) | 22 | 0 | 0 | 22 | 0 | 0 |
| all | 7,831 | 7,809 | 0 | 22 | 0 | 0 |

- `unhandled dxbc instruction N`: no line, for any N.
- The 22 refusals are one message class: `Geometry shader cannot be independently converted.`
- No other line on the tool's standard output; every converted run wrote an output file.

**The geometry shaders.** A geometry shader is converted together with the vertex shader that
feeds it (the tool's `--vertex-before-geometry`, and `--geometry-after-vertex` for the vertex
half). Which pairs the session uses was read from the trace by walking `VSSetShader`,
`GSSetShader` and the draws: **7 pairs, made of 4 geometry containers, in 6,762 draws**. The
other 22 geometry containers are created (and may be bound) but no draw happens while one of
them is bound with a vertex shader.

## 2. The assert build (`third_party/dxmt/build-native-assert`)

Run after the release build and with `--skip-flagged release`, the project's rule: the assert
build aborts where the release build prints its `unhandled` line, and every abort leaves a
crash report of the system's that may carry bytes of the process. The release build flagged
nothing, so nothing was left out.

**The same table, run for run**: 7,831 runs in 34 s; 7,809 converted (29 compute, 3,053 pixel,
4,713 vertex, 7 geometry with their vertex shader, 7 vertex before their geometry shader), 22
geometry containers refused alone with the same message, **no run died** (an abort at any
assertion of the converter would be a death by signal 6), no `unhandled` line, no timeout.
The sweep was run twice on this build (00:27 and 00:40) with the same result.

**Crash reports.** The system's crash-report folder held 3 files `airconv-*.ips` before the
sweep and 3 after: **the runs left none**. The three predate the sweeps. Not opened, not removed.

What was run: the binary of 27,781,144 bytes dated 12 October 00:06, sha256 beginning
`1320464e8e7c2502`, the same before and after the sweeps. That it asserts was checked on the
binary (it holds the text `0 && "unhandled dxbc instruction"`; the release build's does not)
and is what the earlier check reports from a made-up shader with
an unknown opcode: exit 0 and the line in the release build, `SIGABRT` in this one.

## 3. What this does not cover

- The pipeline variants DXMT compiles at a draw (input layout, render-target formats, the
  tessellation and geometry paths inside the device) are not what the standalone tool runs:
  it knows no input layout and no pipeline. The warm-up of step 1 is the
  first time those run.
- Metal's own compile of the result.
- Shaders of other sessions: another level, other settings, multiplayer.

## 4. Reproducing

    M=<checkout>/build/trace          # the main checkout's; DATA = $M/data
    .venv/bin/python tools/trace/d3d/tracefile.py extract $M/data/mission1.trace $M/data      # blobs/, shaders.tsv (82 s)
    .venv/bin/python tools/trace/d3d/sweep.py pairs $M/data/mission1.trace $M/data            # gs-pairs.tsv (one pass)
    .venv/bin/python tools/trace/d3d/sweep.py run third_party/dxmt/build-native/src/airconv/darwin/airconv $M/data release
    .venv/bin/python tools/trace/d3d/sweep.py run third_party/dxmt/build-native-assert/src/airconv/darwin/airconv $M/data assert --skip-flagged release
    .venv/bin/python tools/trace/d3d/sweep.py table $M/data release                           # the table again

Per run the script keeps (in `$M/data/sweep-<name>.tsv`, not in git): the container's hash,
its stage, the partner's hash, how the tool ended, the message class, the opcode numbers, the
time. The head of `tools/trace/d3d/sweep.py` says how each stage is run.
