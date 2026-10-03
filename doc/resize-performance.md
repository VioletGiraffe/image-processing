# Resizer performance

The measurements behind the resizer's design, and the experiments that lost. Benchmarks live in
`tests/cimageresizer_benchmarks.cpp`; `scripts/run_tests --benchmark` runs them and `tests/report_benchmark_ratios.py` prints
the table. Every table names the commit it was measured at: re-measure after changing the resizer.

## Reading the numbers

- The resizer / QImage ratio is the only figure that compares across runs, machines and CI jobs.
- Absolute ms compare only on one machine, and only while the rows the change cannot affect agree.
- Noise gauges: the QImage controls. Every resizer row runs a SIMD kernel. Older reports' Grayscale8, RGB24 and `[scalar]`
  rows ran a scalar path, since removed.
- Run-to-run noise:
  - PC: 5-8% on the few-ms rows, 1-2% elsewhere.
  - Pi: 5-10%.
  - windows-latest runner: most rows within 3%, some 10%.
  - ubuntu-24.04-arm runner, Clang: about 1%.
  - macos-latest runner: up to 2x, too noisy to read per scenario.
  - A real M1, same-session A/B: about 1%.
  - An Ubuntu VM on the PC: 10-20% between runs of one binary, whole runs drifting together. Fifteen alternating rounds,
    read by each build's minimum and by the median of back-to-back pairs, resolve about 3%.
- A CI comparison needs several samples per side: re-running the old commit's run alongside the new one gives same-time
  pairs. A re-run is a new attempt of the same run, with its own logs.
- `QT_NO_GUI_THREADPOOL=1` keeps the Qt control serial; `run_tests` sets it.
- Every benchmark allocates a fresh destination inside the timing, as the Qt control does. Threaded rows measured up to
  29b97e9 reused one destination.
- Windows throttles an unfocused console process onto E-cores: such runs came out 2-4x slow. The test runner opts out
  through `runCatchSession` (cpp-template-utils).
- A Pi 4 without cooling throttles under sustained load: `vcgencmd get_throttled` must print `0x0` after a run.
- A/B rounds alternate the builds.
- Under MSVC on the PC, the 3x upscale rows move 5-10% with where the kernel's loops land in memory, with no change to
  their instructions (the section on jumps and 32-byte boundaries). An MSVC A/B on those rows needs a placement control:
  the same build with `__nop()` padding at the kernel's start, in 16-byte steps.
- An A/B build in a second worktree starts from deleted object files: after a checkout there, MSBuild recompiled the AVX2
  kernels and left the other sources' objects stale.
- A CI label's runner CPU varies between runs, and the ratios with it: Qt's SSE code and the AVX2 kernel do not scale
  alike. A job's numbers compare across runs only when the CPU line in its report header names the same model.
- The report header names the commit, CPU, compiler and cache sizing.

## Reading the generated code

- The release builds use link-time optimization on every compiler, so the object files hold no machine code. Disassemble
  the executable: `objdump -d -C`, or `dumpbin /disasm` with the PDB next to it.
- Under MSVC the two kernel levels are generated differently: the SSE4.1 source at link time with the rest of the
  project, the AVX2 source by its own compile rule. An inlining decision can differ between them.
- Two builds' kernels compare by instruction sequence with the `nop`s and jump targets removed.
- A variant build takes its define through the `CL` environment variable, set for the msbuild step only: set before
  qmake, it breaks qmake's compiler probe. From Git Bash the value starts with `-D`, as a leading `/` is rewritten into a path.
- VTune's hardware events need an elevated prompt. With about ten events each is counted throughout; the
  `uarch-exploration` preset rotates some 190, and its totals came out inconsistent. `-report hw-events -group-by function`
  separates instantiations of one template.

## Machines

| | CPU | Caches | Toolchain |
|---|---|---|---|
| PC | Core i5-12600K, P-cores | 48 KB L1D, 1.25 MB private L2 per P-core | MSVC 19.51 (toolset v145), Qt 6.11.2 |
| Raspberry Pi 4 | 4x Cortex-A72 | 32 KB L1D, 1 MB L2 shared by all cores | Clang 22 unless noted |
| ubuntu-24.04-arm | Cobalt 100 (Neoverse N2), 4 cores | 1 MB private L2 per core | GCC and Clang jobs |
| ubuntu-latest, windows-latest | 2 cores, 4 threads, varying by run. Seen: AMD EPYC 9V45 (Zen 5), 9V74 (Zen 4), 7763 (Zen 3); Xeon Platinum 8573C, 8370C, Xeon 6973P-C | Private L2 per core: 1 MB on Zen 4 and 5, 512 KB on Zen 3, 2 MB on the Xeons (1.25 MB on the 8370C) | GCC and Clang; MSVC and clang-cl |
| macos-latest | Apple M1 (virtual), 3 cores | 12 MB L2 | Apple Clang |

## Standing against Qt

Resizer / QImage, lower is better. PC at 9d86565, before column strips (mean of three rounds). Pi at 6b6bf7e. Grayscale8
and RGB24 on both at e5ea70d. "-": no such benchmark.

| Scenario | PC | PC, threads | Pi | Pi, threads |
|---|---|---|---|---|
| 24 MP -> 1080p | 1.77 | 0.51 | 2.90 | 0.84 |
| 4K -> 1080p RGB32 | 1.99 | - | 4.97 | - |
| 4K -> 1080p RGBA32 | 0.94 | 0.28 | 2.33 | 0.72 |
| 720p -> 4K RGB32 | 0.59 | - | 1.08 | - |
| 720p -> 4K RGBA32 | 0.31 | 0.07 | 0.32 | 0.10 |
| 1080p -> 1440p | 0.87 | 0.21 | 1.84 | 0.63 |
| 1080p -> 240p | 2.36 | 0.95 | 3.66 | 1.18 |
| 4K -> 64x64 | 2.56 | 1.05 | 3.52 | 1.37 |
| 101 MP -> 720p | 2.22 | 0.64 | 3.91 | 1.17 |
| 1080p, native size | 1.12 | - | 1.00 | - |
| 4K -> 1080p Grayscale8 | 0.50 | - | 0.74 | - |
| 720p -> 4K Grayscale8 | 0.48 | - | 0.44 | - |
| 4K -> 1080p RGB24 | 0.81 | 0.27 | 2.75 | 0.80 |
| 720p -> 4K RGB24 | 0.56 | 0.20 | 0.77 | 0.29 |

- PC: upscales, straight-alpha images, Grayscale8 and RGB24 beat Qt; Qt premultiplies alpha in a separate pass.
- Pi: straight-alpha upscales, Grayscale8 and the RGB24 upscale beat Qt single-threaded, and the opaque 720p -> 4K about
  matches it. With threads every upscale beats it, and the 24 MP, RGBA32 and RGB24 downscales too.
- Opaque 4-byte downscales stay about 2x behind Qt single-threaded on the PC and 3-5x on the Pi.

## What the design rests on

### Per-instruction-set primitives, not SIMDe

The kernel outline is written against primitives that state what the kernel needs, such as spreading a weight pair or
packing 8 floats to words. Each instruction set implements them natively. SIMDe, translating AVX2 intrinsics op by op,
falls short both ways:
- AVX2 code lowered to SSE4.1 falls back to scalar loops in the hot paths: `permutevar8x32_ps`, `cvttps_epi32` and the
  256-bit `shuffle_ps`, plus `cvtepu8_epi32` under MSVC, which lacks vector extensions.
- On AArch64, GCC kept SIMDe's 256-bit type in memory, copying it through the stack at every operation. Resizer / QImage
  on the ARM runners, 24 MP -> 1080p single-threaded, at d436d41: GCC 4.95, Clang 2.30.

CI, 9c5c6ac and 4b5fdf8 -> 963b54b, SIMD rows:
- ARM GCC: -40% to -55% everywhere. It is now within 2-22% of Clang, down from about 2x.
- ARM Clang: upscales -12% to -23%, downscales within 3%.
- Pi, Clang, 6b6bf7e -> 3049bb0: upscales -6% to -13%, downscales -4% to -9%; the scalar rows within 4%.
- M1, Apple Clang, three alternating rounds of 4b5fdf8 and 963b54b: -2% to -12%, the scalar rows within 1%.
- x64: the AVX2 instructions are unchanged under clang-cl and MSVC. MSVC's code layout moved, and its RGBA32 downscales
  on the EPYC 7763 runner are 6-9% slower, against neutral on the PC.

### Source rows converted to float once (d436d41)

The horizontal pass used to widen bytes to floats at every tap; each pixel is now converted once. x64 widens as part of
the load, NEON needs separate instructions. So the change pays on ARM and is about neutral on x64.

CI, resizer / QImage single-threaded, f435b80 -> d436d41:

| Job | 24 MP -> 1080p | 4K -> 1080p | 4K -> 64x64 | 101 MP -> 720p |
|---|---|---|---|---|
| ARM Clang | 5.14 -> 2.30 | 7.42 -> 3.41 | 6.49 -> 2.47 | 7.05 -> 2.89 |
| ARM GCC | 6.46 -> 4.95 | 9.78 -> 7.32 | 7.07 -> 5.47 | 8.91 -> 6.06 |
| macOS | 3.91 -> 2.50 | 3.72 -> 2.90 | 3.87 -> 1.61 | 6.14 -> 1.86 |
| x64 GCC | 2.71 -> 2.77 | 3.10 -> 3.00 | 3.55 -> 4.04 | 3.79 -> 3.96 |
| MSVC | 2.27 -> 2.44 | 2.09 -> 2.18 | 1.84 -> 2.66 | 2.49 -> 2.54 |

### A sliding source buffer, not whole converted rows (a9756cb)

Whole converted rows are 4x the byte rows. On the Pi, four threads' float rows overflow the shared 1 MB L2, and threading
stopped paying. `SlidingSourceFloats` converts into a buffer of a few KB per row that slides along the row.

Pi, SIMD ms: 7919eaf / whole rows (d436d41) / sliding (a9756cb, chunk 64):

| Scenario | Single-threaded | Threads |
|---|---|---|
| 24 MP -> 1080p | 542 / 427 / 476 | 246 / 462 / 232 |
| 4K -> 1080p | 220 / 172 / 200 | 124 / 220 / 125 |
| 101 MP -> 720p | 2023 / 1534 / 1680 | 639 / 1600 / 587 |
| 1080p -> 1440p | 81.1 / 69.1 / 78.0 | 22.6 / 44.1 / 23.9 |
| 1080p -> 240p | 45.1 / 31.7 / 37.7 | 15.3 / 15.9 / 13.2 |
| 4K -> 64x64 | 129 / 84 / 114 | 45.6 / 41.4 / 43.1 |

- Whole rows: the threaded times doubled wherever the float rows are large. Sliding brings them back to 7919eaf's.
- Single-threaded, sliding keeps a third to two thirds of the whole-row gain.
- The PC has private L2 per core, so there all three stay within about 10% of each other.
- Capacity is two of the longest runs, plus a back margin and a conversion chunk. The first version held one run plus
  72 pixels. For 4K -> 64x64 (360-pixel windows), that copied every source pixel about 5 extra times per sweep.

CI runners with large private L2 prefer whole rows single-threaded. Resizer / QImage on ARM Clang, whole rows (d436d41) ->
sliding (1ecd630):

| Scenario | Whole rows -> sliding |
|---|---|
| 4K -> 1080p | 3.41 -> 3.64 |
| 1080p -> 1440p | 1.60 -> 1.79 |
| 4K -> 64x64 | 2.47 -> 3.09 |

Threads were even on those runners. The Pi decides: it is the target hardware, and its gain is far larger.

### Conversion chunk of 128 pixels (1ecd630)

The chunk sets how far conversion runs ahead of the current run:

- A larger chunk copies less when the buffer slides.
- A smaller chunk keeps the buffer small in L1.

Estimates for 24 MP -> 1080p:

| Chunk | Buffer per row | Copied share of float stores |
|---|---|---|
| 256 | 4.9 KB | 10% |
| 128 | 2.8 KB | 19% |
| 64 | 1.8 KB | 34% |
| 32 | 1.3 KB | 55% |
| 16 | 1.0 KB | 78% |

Pi, SIMD ms, chunk 64 (2 runs) / 256 (2 runs) / 128, single-threaded:

| Scenario | 64 | 256 | 128 |
|---|---|---|---|
| 24 MP -> 1080p | 476, 487 | 467, 460 | 458 |
| 4K -> 1080p | 200, 203 | 225, 198 | 196 |
| 1080p -> 1440p | 78.0, 80.9 | 78.4, 77.2 | 76.7 |
| 4K -> 64x64 | 114, 118 | 105, 104 | 110 |
| 101 MP -> 720p | 1680, 1713 | 1684, 1664 | 1683 |

Pi, the same with threads:

| Scenario | 64 | 256 | 128 |
|---|---|---|---|
| 24 MP -> 1080p | 232, 230 | 251, 242 | 236, 235 |
| 4K -> 1080p | 125, 123 | 133, 132 | 128, 127 |
| 1080p -> 1440p | 23.9, 23.2 | 26.0, 25.9 | 24.0, 22.6 |
| 4K -> 64x64 | 43.1, 41.3 | 40.6, 41.7 | 39.6, 39.5 |
| 101 MP -> 720p | 587, 585 | 595, 587 | 558, 583 |

- 256 costs the Pi 5-10% with threads, likely from L1 pressure: more buffer next to a 23 KB temp row in a 32 KB L1.
- 128 matches 64 with threads, and matches or beats 256 single-threaded.
- PC, 64 -> 256: upscales gain 3-6% single-threaded, and 101 MP with threads gains 4-5%; nothing loses outside noise.

Net on the Pi, 7919eaf -> 1ecd630, ms:

| Scenario | Single-threaded | Threads |
|---|---|---|
| 24 MP -> 1080p | 542 -> 458 | 246 -> 235 |
| 4K -> 1080p | 220 -> 196 | 124 -> 125 |
| 101 MP -> 720p | 2023 -> 1683 | 639 -> 578 |
| 4K -> 64x64 | 129 -> 110 | 45.6 -> 42.7 |
| 1080p -> 1440p | 81 -> 77 | 22.6 -> 22.6-25.3 |

### Straight alpha premultiplied in the kernel (003d5e6)

Qt converts straight alpha to `ARGB32_Premultiplied` before scaling. That pass is the difference between its RGBA32 and
RGB32 4K -> 1080p controls: 18.7 against 8.1 ms on the PC, 87.9 against 40.2 on the Pi. The same conversion in our Qt bridge
would add a pass and a 33 MB temporary per 4K frame. Premultiplying while converting to float costs nothing measurable on
the SIMD path on the PC: at 1ecd630 the 4K -> 1080p RGBA32 ratio was 0.858 and 0.797 in two runs, and at 003d5e6 it was
0.885. The untouched RGB32 row moved by as much in the same runs. On the Pi it costs about 5% single-threaded: 2.36 at
1ecd630, 2.47 at 1199012.

### 128-bit packs for the output bytes (e16c6c4)

The vertical pass rounds floats to bytes:
- 256-bit packs work per 128-bit lane. Joining the lanes takes a lane-crossing `permutevar8x32`, which SIMDe emulated
  element by element on NEON.
- 128-bit packs keep element order, so the permute is not needed.

Resizer / QImage, before -> after. On the ARM runner and the Pi, both builds called `prepareRun` out of line (see the next
section).

| Scenario | ARM Clang CI | Pi | PC |
|---|---|---|---|
| 720p -> 4K RGBA32 | 0.31 -> 0.28 | 0.39 -> 0.37 | 0.31 -> 0.31 |
| 720p -> 4K RGBA32, threads | 0.081 -> 0.072 | 0.31 -> 0.28 | 0.074 -> 0.068 |
| 1080p -> 1440p | 2.07 -> 1.95 | 2.10 -> 2.01 | 0.92 -> 0.87 |

Downscales stayed within about 4% on all three.

### Forced inlining on every platform (1199012)

The kernel's helpers run per pixel or per output column, so `IMAGE_PROCESSING_SIMD_INLINE` forces inlining on every
platform:
- On x64 the forcing came with the AVX2 target attribute.
- On ARM the macro was plain `inline`. Once 003d5e6 enlarged `prepareRun`, Clang called it out of line, once per output
  column.
- A per-column cost weighs most on upscales, which have the most columns per source pixel.

ARM Clang runner, 1ecd630 -> 003d5e6: 24 MP 2.32 -> 2.58, 4K -> 1080p 3.64 -> 4.09, 1080p -> 1440p 1.79 -> 2.07,
4K -> 64x64 3.09 -> 3.26.

Pi, single-threaded. Columns: 1ecd630 / f116725 (out-of-line call) / 1199012 (inlined, with the 128-bit packs):

| Scenario | Resizer / QImage |
|---|---|
| 24 MP -> 1080p | 2.94 / 3.11 / 2.97 |
| 4K -> 1080p RGB32 | 4.96 / 5.24 / 4.83 |
| 1080p -> 1440p | 1.92 / 2.10 / 1.85 |
| 4K -> 64x64 | 3.62 / 3.63 / 3.41 |
| 101 MP -> 720p | 4.32 / 4.47 / 4.29 |

### One outline for every pixel layout (e5ea70d)

The SIMD outline serves 1-4 channels at any pixel stride.
- The sliding source buffer holds 1, 2 or 4 floats per pixel. RGB gets a 4th float, 0, and runs the 4-channel code.
- The horizontal pass works in blocks of 32 source floats through four FMA chains: 8 taps of 4-float pixels, 16 of 2-float,
  32 of 1-float. 1- and 2-float pixels end with a horizontal sum per output pixel.
- The vertical pass writes blocks of 32 floats, 24 for RGB.
- Tight packing and RGB32 get a compile-time stride, with whole-vector conversion and writes. Other strides convert and
  store pixel by pixel.

PC, ms, 42a4330 -> e5ea70d, three alternating rounds:

| Scenario | MSVC | clang-cl |
|---|---|---|
| 4K -> 1080p Grayscale8 | 22.5 -> 10.6 | 16.8 -> 8.7 |
| 720p -> 4K Grayscale8 | 14.1 -> 7.3 | 9.4 -> 7.0 |
| 4K -> 1080p RGB24 | 59.1 -> 16.5 | 39.9 -> 17.7 |
| 720p -> 4K RGB24 | 41.3 -> 10.9 | 25.0 -> 10.9 |
| 4K -> 1080p RGB24, threads | 16.5 -> 5.2 | 11.7 -> 5.8 |
| 720p -> 4K RGB24, threads | 12.4 -> 3.9 | 7.5 -> 3.9 |

- RGB24 now costs what RGB32 does: 16.5 against 16.6 ms on 4K -> 1080p under MSVC.
- The 4-byte rows held within 4% under MSVC; clang-cl's upscales gained 4-11%.
- SSE4.1 / AVX2 time under MSVC: Grayscale8 1.1x, RGB24 1.25-1.7x.
- Pi, Clang, resizer / QImage, 6b6bf7e -> e5ea70d: Grayscale8 1.19 -> 0.74 and 0.59 -> 0.44, RGB24 3.76 -> 2.75 and
  1.09 -> 0.77.
- Pi, GCC against Clang at e5ea70d: 7-17% slower on downscales.

### The run lookup holds its array pointers by value (f2f4569, 03113a6)

The horizontal pass looks up each output pixel's run through `AxisWeights::RunLookup`, a copy of the two array pointers.
Through the `AxisWeights` reference the compiler reloads both per pixel: the loop's stores may alias the object.

- The reload sits ahead of the load that yields the pixel's tap count, so every branch on that count resolves later.
- x64 GCC kept the reference itself on the stack in the 3-channel AVX2 kernel at e5ea70d, a third load in that chain. That
  kernel ran 4-5% slower than at 8baefb9, with the same instructions in every hot loop.

CI, x64 GCC on the EPYC 7763, AVX2 ms, three runs per commit:

| Scenario | 8baefb9 | f04db84 | f2f4569 |
|---|---:|---:|---:|
| 720p -> 4K RGB32 | 12.77 | 13.21 | 12.56 |
| 1080p -> 1440p | 10.36 | 10.88 | 10.09 |
| 4K -> 1080p RGB32 | 27.22 | 27.85 | 25.82 |
| 4K -> 1080p RGBA32 | 26.79 | 27.31 | 25.84 |
| 720p -> 4K RGBA32 | 12.95 | 13.16 | 13.08 |
| 4K -> 1080p Grayscale8 | - | 12.91 | 11.89 |
| 720p -> 4K RGB24 | - | 12.90 | 11.86 |

- x64 GCC: the SSE4.1 kernels gain 2-8% as well. 4K -> 64x64 does not gain. 03113a6 is within 2% of f2f4569.
- ARM GCC on Neoverse-N2, f04db84 -> 03113a6, 6 and 4 runs: 4-byte rows -1% to -5%, Grayscale8 -8% and -5%,
  720p -> 4K RGB24 -16%.
- Clang on ARM and x64, and clang-cl: within 2%.
- PC, MSVC, AVX2, ten alternating rounds: downscales -3% to -6%.

`RunLookup::runFor` is force-inlined. MSVC's link-time code generation otherwise calls it once per output pixel in the
SSE4.1 kernels, and did the same to a `std::span` constructor written directly in the loop (f2f4569): 4-6% on the SSE4.1
upscales on the PC, 4-18% on the EPYC 7763 runner.

Calls written in the hot loop bodies are forced inline at the call site as well (`IMAGE_PROCESSING_FORCE_INLINE_CALLS`),
which covers standard library calls that no function attribute of ours can reach.
- MSVC: `[[msvc::forceinline_calls]]` takes effect on a block. Ahead of a `for` statement it left the calls in the loop's
  body alone. `[[msvc::flatten]]` on the kernel left `writePixelBytes` out of line.
- clang-cl: `[[clang::always_inline]]` on the block. Inside a template it crashes Clang 18.1.3 and Apple clang 21 (Xcode 26.6)
  in `Sema::CheckAlwaysInlineAttr`; 20.1.8 and 22.1.3 compile it.
- GCC has no per-block form: the kernel's entry function is `flatten`ed. Clang off Windows takes the same form.
- `std::min` over an initializer list is a library call under MSVC whatever the attributes: the kernels use the
  two-argument overload.

The check is the disassembly. Under MSVC a kernel's only calls are `memmove`, `memcpy` on the runtime-stride paths,
allocation, the ring's constructor and `clearAvxUpperState`.

### Jumps kept off 32-byte boundaries under MSVC (14e731e)

MSVC aligns loops to 16 bytes. On the PC's P-cores the kernels' upscale path runs at two speeds depending on where its
loops land within a 64-byte line, with identical instructions. Any edit that moves the kernels' code flips it.

PC, MSVC, 03113a6 with 0-56 `nop` bytes at each kernel's start, median ms of six rounds. The eight builds fall into two
placements:

| Scenario | Placement A | Placement B |
|---|---|---|
| AVX2 720p -> 4K Grayscale8 | 7.70 - 8.05 | 7.37 - 7.44 |

- Each kernel has its own placements: AVX2 720p -> 4K RGB24 moves 6% and SSE4.1 720p -> 4K Grayscale8 12-14%, at other
  paddings than the row above.
- Downscales do not react: 2-5% spread, the QImage controls' own.
- The EPYC runners show at most a few percent on these rows, and not consistently: CI cannot check this.

A reduced loop, `tests/msvc_loop_placement_repro.cpp` (two rows per iteration, a never-entered 32-tap block, a 4-tap
step, run lengths 4, 1, 4), shows 7-28% between placements. VTune on it, per iteration, fast against slow placements:
- Instructions, retired micro-ops, fused pairs and taken branches: equal.
- Mispredictions and legacy-decoder micro-ops: none. The loop stream detector delivers nothing.
- Cycles in which the micro-op cache delivers a partial group: 1.4-1.9 against 3.6-4.0.

`/QIntel-jcc-erratum` pads so that no jump crosses or ends on a 32-byte boundary. Only the AVX2 source is compiled with
it. The measurements below are of 14e731e, which applied it to both levels. PC, MSVC, median ms of six rounds; the plain
build at its two placements against eight padded builds with the switch:

| Scenario | Plain, slow | Plain, fast | With the switch |
|---|---:|---:|---|
| AVX2 720p -> 4K Grayscale8 | 8.12 | 7.56 | 7.41 - 7.80 |
| AVX2 720p -> 4K RGB24 | 12.18 | 11.74 | 11.56 - 11.94 |
| SSE4.1 720p -> 4K Grayscale8 | 9.32 | 8.32 | 8.28 - 8.89 |

- Downscales with the switch: within -3% to 0% of the plain build.
- Conditional jumps crossing or ending on a 32-byte boundary went from 135 and 152 in the nine kernels per level to 1 each.
  The link-time-generated level takes the switch from the compile flags too.
- The same reduced loop under clang-cl 22 runs within 1% at every placement.
- EPYC 7763 (CI, same toolset as the PC), 03113a6 -> 14e731e, three runs each: AVX2 rows within 1%. SSE4.1 RGB32 upscales
  slower in every run: 720p -> 4K 22.8-23.4 -> 26.6-28.1 ms, 1080p -> 1440p 16.2-17.5 -> 19.0-19.3 ms. The same two rows
  on the PC: within 1.5% with and without the switch. The other SSE4.1 upscales on the EPYC: within 4%.
- The PC's E-cores (Gracemont), MSVC, 03113a6 -> 14e731e, minimum of two rounds: AVX2 rows within noise. SSE4.1 RGB32
  downscales slower: 101 MP -> 720p 314 -> 415 ms, 4K -> 64x64 22.4 -> 30.2, 4K -> 1080p 42.4 -> 49.5, 24 MP -> 1080p 95 -> 111.
  Cause not investigated. With the switch on the AVX2 source alone: 311, 22.1, 40.7, 92.5.

### Column strips (b9fea7e)

Each thread fills its ring for one column strip of the destination at a time. `stripWidthFor` bounds a ring to half the
smallest L2 share per logical processor, detected at runtime; an undetected L2 counts as the Pi's. The SIMD path converts
only the strip's source span (29b97e9).

| Machine | L2 share per logical processor | Ring budget |
|---|---:|---:|
| Pi 4 | 256 KB (1 MB, four cores) | 128 KB |
| PC | 512 KB (E-core cluster: 2 MB, four cores) | 256 KB |

The tests pass the Pi's budget on every machine, so they cover the same strip layouts everywhere.

Full-width rings overflowed the Pi's shared L2 with four threads. Per thread, ring plus float row plus accumulator row came
to about 350 KB for 720p -> 4K RGBA32, 390 KB for 4K -> 1080p RGB24, 500 KB for 24 MP -> 1080p, 1.2 MB for 101 MP -> 720p.
Pi PMU counters, whole process, "Parallel resize" reduced to 720p -> 4K RGBA32, 8ca3148 (two passes through a whole-image
temp) -> 2b371a2 (full-width ring):

| Event | 8ca3148 | 2b371a2 |
|---|---:|---:|
| L2 read refills (0x52) | 85.8 M | 103.0 M |
| L2 write refills (0x53) | 4.6 M | 10.3 M |
| Bus reads (0x60) | 359 M | 450 M |
| Bus writes (0x61) | 144 M | 177 M |
| Cycles (0x11) | 19.0 G | 22.2 G |
| Kernel time | 1.48 s | 0.50 s |

- DRAM traffic grew both ways despite less work: evicted dirty ring rows are written out and read back.
- The 3.2 G extra cycles over the 17.2 M extra read refills come to about 186 cycles each, a full DRAM latency.
- The whole-image temp caused few write refills: the A72 stops allocating on long sequential store runs. Its kernel time
  is the temp's page faults on every call.
- The A72 does not count backend stalls (0x24).

Pi, ms with threads, 2b371a2 -> b9fea7e. Scalar also against 8ca3148. Speedup: single-threaded / threaded time, with strips.

| Scenario | SIMD | Scalar: 8ca3148 / 2b371a2 -> b9fea7e | Speedup: SIMD, scalar |
|---|---|---|---|
| 24 MP -> 1080p | 236.8 -> 136.4 | 304.7 / 308.8 -> 171.8 | 3.36x, 3.52x |
| 4K -> 1080p RGBA32 | 167.3 -> 69.9 | 214.6 / 228.7 -> 91.4 | 3.13x, 3.23x |
| 4K -> 1080p RGB24 | - | 136.4 / 156.6 -> 74.2 | -, 3.42x |
| 720p -> 4K RGBA32 | 88.6 -> 36.5 | 127.0 / 144.5 -> 77.2 | 3.08x, 2.04x |
| 720p -> 4K RGB24 | - | 62.7 / 60.5 -> 38.7 | -, 3.12x |
| 101 MP -> 720p | 624.8 -> 481.0 | 840.8 / 1034.8 -> 614.9 | 3.11x, 3.62x |
| 1080p -> 240p | 13.0 -> 11.9 | 16.7 / 16.7 -> 16.8 | - |
| 1080p -> 1440p (one strip) | 23.0 -> 24.7 | 38.4 / 34.2 -> 38.2 | - |
| 4K -> 64x64 | 42.6 -> 44.3 | 47.1 / 58.6 -> 59.2 | 2.46x, 2.85x |

- Both paths scale 3.1-3.6x on four cores; before strips the SIMD path scaled 1.3-3.1x, the scalar one 1.1-2.9x.
- Weak rows: 4K -> 64x64, whose two strips each re-convert about 360 shared source pixels, and scalar 720p -> 4K RGBA32.
- Single-threaded the Pi is about neutral: 101 MP SIMD gains 9-12% (its ring overflowed the L2 even alone), 4K -> 64x64 SIMD
  loses 8%.
- Where full-width rings already fit, only the overhead shows. ARM Clang on Neoverse-N2 (1 MB private L2), CI,
  2b371a2 -> 29b97e9, 3-4 samples per side:
  - SIMD +1-5% single-threaded.
  - Scalar downscales +5-11%, scalar upscales -5-6%.
  - With threads within 4%, except scalar 24 MP and 101 MP: +8-9%.

Without the pre-touch below, strips cost x64 Windows single-threaded, the SIMD upscales most. CI, EPYC 7763, resizer / QImage, 2b371a2 -> 29b97e9,
2-4 samples per side:
- MSVC and clang-cl: SIMD 720p -> 4K +53-62%, 4K -> 1080p RGB32 +9-14%; scalar upscales +4-20%.
- With threads: MSVC -8% to +7%, clang-cl gains 3-11%.
- x64 Clang on Linux, same CPU model: within 4%.

The cost is Windows' demand-zero page faults on a fresh destination:
- A process-fault-counter probe on the PC: one fault per page with and without strips, but about 1.1-1.2 us each when strips
  first write the pages out of address order, against 0.45 us in order.
- With a reused destination strips cost the upscales -4% to +6%.
- Finishing each page while cached does not help: see experiment 8.
- `touchDestPagesInOrder` writes one byte per page in address order before a band's strips run.
- Linux on the same CPU model shows no cost; why is unmeasured.

PC, MSVC, mean ms of five alternating rounds; threads: a probe with fresh destinations, three runs.

| Scenario | 2b371a2 | 29b97e9 (strips, cap) | Pre-touch |
|---|---:|---:|---:|
| 720p -> 4K RGBA32 | 13.65 | 19.59 (+43%) | 13.93 (+2%) |
| 720p -> 4K RGB32 | 12.72 | 18.56 (+46%) | 13.29 (+4%) |
| 4K -> 1080p RGB32 | 15.89 | 18.53 (+17%) | 16.73 (+5%) |
| 24 MP -> 1080p | 35.84 | 38.64 (+8%) | 36.93 (+3%) |
| 720p -> 4K RGB32, scalar | 44.24 | 48.67 (+10%) | 44.85 (+1%) |
| 720p -> 4K RGBA32, threads | 5.35 | 7.57 (+41%) | 5.47 (+2%) |

- The remaining 3-5% on downscales is strip overhead: it remains with a reused destination.
- Pre-touching a reused destination costs nothing measurable, with or without threads.
- CI, EPYC 7763, single-threaded against 2b371a2, 6b6bf7e: MSVC and clang-cl upscales -3% to +3% (strips alone: +53-62%),
  4K -> 1080p +1-7%. The Linux and ARM jobs held within 5% of strips alone.
- The cap (29b97e9) took 4-5 points off the SIMD cost before the pre-touch.

## Experiments that lost

1. **Weight broadcasts instead of the lane permute on ARM** (8429a19, reverted).
   - What: the horizontal pass spread weights with `permutevar8x32_ps`, which SIMDe emulated element by element on NEON.
     Two broadcasts replaced it outside x64.
   - Result: ARM GCC SIMD speedup 0.86-1.25x before, 0.84-1.34x after.
   - The cost was GCC's 256-bit type going through memory, not the permute.
2. **An early return and register-held span state in `prepareRun`** (e71b61c, reverted by d8172a0).
   - Aimed at MSVC, which lacks type-based alias analysis and may reload the buffer's fields after every float store.
   - PC, MSVC, three rounds each, ms. Columns: sliding, + early return, + both.

     | Scenario | Sliding | + early return | + both |
     |---|---|---|---|
     | 1080p -> 1440p | 8.55, 8.89, 8.92 | 8.08, 8.15, 8.47 | 8.70, 8.84, 8.97 |
     | 720p -> 1080p | 4.53, 4.63, 4.66 | 4.33, 4.35, 4.58 | 4.64, 4.65, 4.70 |
     | 4K -> 1080p | 15.75, 15.99, 16.35 | 15.41, 15.45, 16.06 | 14.92, 15.28, 15.42 |
     | 24 MP -> 1080p | 35.46, 35.50, 35.75 | 34.81, 35.31, 35.66 | 35.22, 35.31, 36.06 |
   - Pi: both together cost 2-5% single-threaded and nothing with threads. Clang already keeps the fields in registers,
     so the locals only add register pressure.
3. **Chunk 256** (97b96ed, replaced by 128): the Pi's thread cost above.
4. **Chunks of 32 and 16:** not measured.
   - They save about 0.5 KB of L1 per row against 64.
   - The table above shows the copying they add, and the Pi's single-threaded numbers already favour less copying.
   - The Pi's noise is 5-10%, so telling them apart from 64 would take several runs of each.
5. **Four runs of buffer capacity:** not built. It would cut the copying for 4K -> 64x64 to a third, but a row pair would
   need about 46 KB, more than the Pi's L1.
6. **The old direct-load path for windows of up to 4 taps on x64:** not built.
   - It would win back the x64 upscale cost of pre-conversion: in one PC session, 1080p -> 1440p took 7.25 ms at 7919eaf
     and 8.5-8.7 ms with the sliding buffer.
   - It would add a second horizontal pass selected by platform and window size.
7. **A byte-to-float lookup table in the scalar conversion** (not committed).
   - Aimed at a suspected dependency chain through `cvtsi2ss`. MSVC already breaks it with `xorps`.
   - PC: scalar downscales 2-5% faster, 1080p -> 1440p unchanged, across sessions.
   - Dropped: unmeasured on ARM, where a table lookup blocks vectorizing the conversion.

8. **Row blocks for the Windows cost of strips** (not committed).
   - What: each thread ran all strips over a block of about 2.7 MB of destination rows before the next block, so every
     destination page filled while cached.
   - Result: no change against strips alone, on fresh and reused destinations. The cost is in the faults (the column
     strips section).

## Open leads

1. **x64 GCC runners, 4K -> 64x64:** resizer / QImage 3.55 at f435b80, 4.04 at d436d41, 3.95 at 1ecd630. The sliding buffer
   fixed the same regression on MSVC: 1.84, 2.66, 1.76.
2. **Native size on the x64 runners:** resizer / `QImage::copy` 7.59 (GCC) and 7.26 (Clang) at d436d41, about 1.0 on ARM.
   Not investigated.
3. **Pre-conversion costs MSVC's upscales:** 1080p -> 1440p 7.25 -> 8.5-8.7 ms on the PC (experiment 6). The scalar path
   lost 7-13% on its three-channel upscales under MSVC alone, and its phase timings put the loss in the horizontal filter's
   code, not in memory traffic. Comparing MSVC's code for `filterHorizontalRowGroup` with clang-cl's is the next step.
4. **The SSE4.1 kernels are untuned:** 1.3-1.7x the AVX2 time on the PC under MSVC. The horizontal pass's paired-row
   block needs 16 XMM accumulators, x64's whole register file: MSVC keeps eight of them on the stack. Single rows or fewer
   chains at this level are the candidates, to be judged on the Celeron N4100.
5. **Strips cost where the L2 is large:** 1-5% on Neoverse-N2's SIMD rows, 3-8% on the PC's downscales (the column strips
   section). A budget from the runtime L2 share would skip strips there.
6. **Placement still moves MSVC's AVX2 upscale rows about 5%** with `/QIntel-jcc-erratum`, and the SSE4.1 ones, built
   without it, up to 12-14%. Which loop of the upscale path reacts is unidentified: padding at the kernel's start shifts
   them all together. MSVC has no loop alignment control; clang-cl's code for the reduced loop does not react.
7. **`/QIntel-jcc-erratum` on the SSE4.1 level is untested where that level runs:** it cost RGB32 upscales 13-17% on the
   EPYC 7763 and RGB32 downscales 17-35% on the PC's E-cores, and nothing on its P-cores (the section on jumps and 32-byte
   boundaries). To be measured on the Celeron N4100 and a Sandy Bridge.
