# Resizer performance

The measurements behind the resizer's design, and the experiments that lost. Benchmarks live in
`tests/cimageresizer_benchmarks.cpp`; `scripts/run_tests --benchmark` runs them and `tests/report_benchmark_ratios.py` prints
the table. Every table names the commit it was measured at: re-measure after changing the resizer.

This document holds what the design rests on, with headline figures. The full tables are in the per-CPU logs in `doc/cpu/`,
under the same section headings.

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
  - Celeron N4100: 0.5-1%.
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
| [PC](cpu/core-i5-12600k.md) | Core i5-12600K, P-cores | 48 KB L1D, 1.25 MB private L2 per P-core | MSVC 19.51 (toolset v145), Qt 6.11.2 |
| [Raspberry Pi 4](cpu/raspberry-pi-4.md) | 4x Cortex-A72, 1.8 GHz | 32 KB L1D, 1 MB L2 shared by all cores | Clang 22 unless noted |
| [Celeron N4100](cpu/celeron-n4100.md) | 4x Goldmont Plus, no AVX: SSE4.1 is its only level | 24 KB L1D, 4 MB L2 shared by all cores | MSVC 19.51, Qt 6.12.0 |
| [ubuntu-24.04-arm](cpu/neoverse-n2.md) | Cobalt 100 (Neoverse N2), 4 cores | 1 MB private L2 per core | GCC and Clang jobs |
| [ubuntu-latest, windows-latest](cpu/x64-ci-runners.md) | 2 cores, 4 threads, varying by run. Seen: AMD EPYC 9V45 (Zen 5), 9V74 (Zen 4), 7763 (Zen 3); Xeon Platinum 8573C, 8370C, Xeon 6973P-C | Private L2 per core: 1 MB on Zen 4 and 5, 512 KB on Zen 3, 2 MB on the Xeons (1.25 MB on the 8370C) | GCC and Clang; MSVC and clang-cl |
| [macos-latest](cpu/apple-m1.md) | Apple M1 (virtual), 3 cores | 12 MB L2 | Apple Clang |

## Standing against Qt

Resizer / QImage, lower is better. "T": with threads. "-": no such benchmark.
- PC at 9d86565, before column strips (mean of three rounds); its Grayscale8 and RGB24 at e5ea70d.
- Pi, Clang 22, at 6b6bf7e; its Grayscale8 and RGB24 at e5ea70d.
- Pi, GCC 14.2, at bdf65f9 (one run).
- N4100 at a6c69e7 (minimum of three rounds).
- The PC's and the Pi's Clang columns predate the two-chain block's assigned first block and the shared weights.

| Scenario | PC | PC, T | Pi, Clang | Pi, Clang, T | Pi, GCC | Pi, GCC, T | N4100 | N4100, T |
|---|---|---|---|---|---|---|---|---|
| 24 MP -> 1080p | 1.77 | 0.51 | 2.90 | 0.84 | 2.61 | 0.73 | 2.91 | 0.82 |
| 4K -> 1080p RGB32 | 1.99 | - | 4.97 | - | 4.53 | - | 3.04 | - |
| 4K -> 1080p RGBA32 | 0.94 | 0.28 | 2.33 | 0.72 | 2.10 | 0.61 | 1.49 | 0.43 |
| 720p -> 4K RGB32 | 0.59 | - | 1.08 | - | 1.00 | - | 0.72 | - |
| 720p -> 4K RGBA32 | 0.31 | 0.07 | 0.32 | 0.10 | 0.28 | 0.10 | 0.36 | 0.12 |
| 1080p -> 1440p | 0.87 | 0.21 | 1.84 | 0.63 | 1.77 | 0.52 | 1.15 | 0.38 |
| 1080p -> 240p | 2.36 | 0.95 | 3.66 | 1.18 | 3.43 | 0.99 | 3.79 | 1.16 |
| 4K -> 64x64 | 2.56 | 1.05 | 3.52 | 1.37 | 3.70 | 1.31 | 3.08 | 1.12 |
| 101 MP -> 720p | 2.22 | 0.64 | 3.91 | 1.17 | 3.72 | 1.16 | 3.82 | 1.14 |
| 1080p, native size | 1.12 | - | 1.00 | - | 1.00 | - | 0.91 | - |
| 4K -> 1080p Grayscale8 | 0.50 | - | 0.74 | - | 0.72 | - | 0.56 | - |
| 720p -> 4K Grayscale8 | 0.48 | - | 0.44 | - | 0.39 | - | 0.28 | - |
| 4K -> 1080p RGB24 | 0.81 | 0.27 | 2.75 | 0.80 | 2.72 | 0.74 | 1.26 | 0.36 |
| 720p -> 4K RGB24 | 0.56 | 0.20 | 0.77 | 0.29 | 0.78 | 0.22 | 0.51 | 0.18 |

- PC: upscales, straight-alpha images, Grayscale8 and RGB24 beat Qt; Qt premultiplies alpha in a separate pass.
- Pi: straight-alpha upscales, Grayscale8 and the RGB24 upscale beat Qt single-threaded, and the opaque 720p -> 4K about
  matches it. With threads every upscale beats it, and the 24 MP, RGBA32 and RGB24 downscales too; under GCC at
  bdf65f9 1080p -> 240p matches it as well.
- N4100: the PC's pattern. In absolute time it runs 1.4-2.0x faster than the Pi on downscales, 1.2-1.6x on
  upscales.
- Opaque 4-byte downscales stay behind Qt single-threaded: about 2x on the PC, 2.6-5x on the Pi, 2.9-3.8x on the N4100.

## What the design rests on

### Per-instruction-set primitives, not SIMDe (963b54b)

The kernel outline is written against primitives that state what the kernel needs, such as spreading a weight pair or
packing 8 floats to words. Each instruction set implements them natively. SIMDe, translating AVX2 intrinsics op by op,
falls short both ways:
- AVX2 code lowered to SSE4.1 falls back to scalar loops in the hot paths: `permutevar8x32_ps`, `cvttps_epi32` and the
  256-bit `shuffle_ps`, plus `cvtepu8_epi32` under MSVC, which lacks vector extensions.
- On AArch64, GCC kept SIMDe's 256-bit type in memory, copying it through the stack at every operation.

Against SIMDe, SIMD rows:
- ARM GCC on Neoverse-N2: -40% to -55% everywhere, now within 2-22% of Clang, down from about 2x.
- ARM Clang, the Pi and the M1: -2% to -23%, upscales most.
- x64: the AVX2 instructions are unchanged.

### Source rows converted to float once (d436d41)

The horizontal pass used to widen bytes to floats at every tap; each pixel is now converted once. x64 widens as part of
the load, NEON needs separate instructions. So the change pays on ARM and is about neutral on x64.

CI, resizer / QImage, 24 MP -> 1080p single-threaded: ARM Clang 5.14 -> 2.30, ARM GCC 6.46 -> 4.95, macOS 3.91 -> 2.50,
x64 GCC 2.71 -> 2.77, MSVC 2.27 -> 2.44.

### A sliding source buffer, not whole converted rows (a9756cb)

Whole converted rows are 4x the byte rows. On the Pi, four threads' float rows overflow the shared 1 MB L2, and threading
stopped paying. `SlidingSourceFloats` converts into a buffer of a few KB per row that slides along the row.

- Pi, 24 MP -> 1080p with threads, ms: 246 before, 462 with whole rows, 232 sliding. Single-threaded: 542, 427, 476.
- Single-threaded, sliding keeps a third to two thirds of the whole-row gain.
- Capacity is two of the longest runs, plus a back margin and a conversion chunk. The first version held one run plus
  72 pixels. For 4K -> 64x64 (360-pixel windows), that copied every source pixel about 5 extra times per sweep.
- CI runners with large private L2 prefer whole rows single-threaded: ARM Clang 4K -> 1080p 3.41 -> 3.64. The Pi decides:
  it is the target hardware, and its gain is far larger.

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

- 256 costs the Pi 5-10% with threads, likely from L1 pressure: more buffer next to a 23 KB temp row in a 32 KB L1.
- 128 matches 64 with threads, and matches or beats 256 single-threaded.
- Net on the Pi, 7919eaf -> 1ecd630, 24 MP -> 1080p: 542 -> 458 ms single-threaded, 246 -> 235 with threads.

### Straight alpha premultiplied in the kernel (003d5e6)

Qt converts straight alpha to `ARGB32_Premultiplied` before scaling. That pass is the difference between its RGBA32 and
RGB32 4K -> 1080p controls: 18.7 against 8.1 ms on the PC, 87.9 against 40.2 on the Pi. The same conversion in our Qt bridge
would add a pass and a 33 MB temporary per 4K frame. Premultiplying while converting to float costs nothing measurable on
the PC, and about 5% single-threaded on the Pi.

### 128-bit packs for the output bytes (e16c6c4)

The vertical pass rounds floats to bytes:
- 256-bit packs work per 128-bit lane. Joining the lanes takes a lane-crossing `permutevar8x32`, which SIMDe emulated
  element by element on NEON.
- 128-bit packs keep element order, so the permute is not needed.

Resizer / QImage, 720p -> 4K RGBA32: ARM Clang CI 0.31 -> 0.28, Pi 0.39 -> 0.37, PC 0.31 -> 0.31. Downscales stayed within
about 4% on all three.

### Forced inlining on every platform (1199012)

The kernel's helpers run per pixel or per output column, so `IMAGE_PROCESSING_SIMD_INLINE` forces inlining on every
platform:
- On x64 the forcing came with the AVX2 target attribute.
- On ARM the macro was plain `inline`. Once 003d5e6 enlarged `prepareRun`, Clang called it out of line, once per output
  column.
- A per-column cost weighs most on upscales, which have the most columns per source pixel.

Resizer / QImage, 1080p -> 1440p: ARM Clang runner 1.79 -> 2.07 with the call; Pi 1.92 before it, 2.10 with it, 1.85 inlined.

### One outline for every pixel layout (e5ea70d)

The SIMD outline serves 1-4 channels at any pixel stride.
- The sliding source buffer holds 1, 2 or 4 floats per pixel. RGB gets a 4th float, 0, and runs the 4-channel code.
- The horizontal pass works in blocks of 8 source floats per chain. AVX2 and NEON run four chains: 8 taps of 4-float
  pixels, 16 of 2-float, 32 of 1-float. SSE4.1 runs two (the section on two chains). 1- and 2-float pixels end with a
  horizontal sum per output pixel.
- The vertical pass writes blocks of 32 floats, 24 for RGB.
- Tight packing and RGB32 get a compile-time stride, with whole-vector conversion and writes. Other strides convert and
  store pixel by pixel.

Against the scalar path it replaced:
- PC, MSVC, ms: 4K -> 1080p RGB24 59.1 -> 16.5, 4K -> 1080p Grayscale8 22.5 -> 10.6. RGB24 now costs what RGB32 does.
- Pi, Clang, resizer / QImage: 4K -> 1080p RGB24 3.76 -> 2.75, Grayscale8 1.19 -> 0.74.
- SSE4.1 / AVX2 time under MSVC on the PC: Grayscale8 1.1x, RGB24 1.25-1.7x.

### The run lookup holds its array pointers by value (f2f4569, 03113a6)

The horizontal pass looks up each output pixel's run through `AxisWeights::RunLookup`, a copy of the two array pointers.
Through the `AxisWeights` reference the compiler reloads both per pixel: the loop's stores may alias the object.

- The reload sits ahead of the load that yields the pixel's tap count, so every branch on that count resolves later.
- x64 GCC kept the reference itself on the stack in the 3-channel AVX2 kernel at e5ea70d, a third load in that chain.

Gains: x64 GCC on the EPYC 7763 5-8% on most AVX2 rows (4K -> 1080p RGB32 27.85 -> 25.82 ms), ARM GCC 1-16%, MSVC's AVX2
downscales on the PC 3-6%. Clang: within 2% everywhere.

`RunLookup::runFor` is force-inlined. MSVC's link-time code generation otherwise calls it once per output pixel in the
SSE4.1 kernels, and did the same to a `std::span` constructor written directly in the loop (f2f4569): 4-6% on the SSE4.1
upscales on the PC, 4-18% on the EPYC 7763 runner, nothing on the N4100.

Calls written in the hot loop bodies are forced inline at the call site as well (`IMAGE_PROCESSING_FORCE_INLINE_CALLS`),
which covers standard library calls that no function attribute of ours can reach.
- MSVC: `[[msvc::forceinline_calls]]` takes effect on a block. Ahead of a `for` statement it left the calls in the loop's
  body alone. `[[msvc::flatten]]` on the kernel left `writePixelBytes` out of line.
- clang-cl: `[[clang::always_inline]]` on the block. Inside a template it crashes Clang 18.1.3 and Apple clang 21 (Xcode 26.6)
  in `Sema::CheckAlwaysInlineAttr`; 20.1.8 and 22.1.3 compile it.
- GCC has no per-block form: the kernel's entry function is `flatten`ed. Clang off Windows takes the same form.
- `std::min` over an initializer list is a library call under MSVC whatever the attributes: the kernels use the
  two-argument overload.

Gains:
- `flatten` under GCC on Neoverse-N2: 24 MP -> 1080p 92.4 -> 78.3 ms, level with Clang. On the Pi: 480 -> 451 ms.
  On the EPYC 7763: -0.9% on average.
- Call-site forcing under MSVC: about 3% on the EPYC 7763. On the N4100, the span that holds it took 7-9% off the
  4-byte and RGB24 downscales.

The check is the disassembly. Under MSVC a kernel's only calls are `memmove`, `memcpy` on the runtime-stride paths,
allocation, the ring's constructor and `clearAvxUpperState`.

### Jumps kept off 32-byte boundaries under MSVC (14e731e)

MSVC aligns loops to 16 bytes. On the PC's P-cores the kernels' upscale path runs at two speeds depending on where its
loops land within a 64-byte line, with identical instructions. Any edit that moves the kernels' code flips it.

- Each kernel has its own placements. Downscales do not react.
- A reduced loop, `tests/msvc_loop_placement_repro.cpp`, shows 7-28% between placements. VTune finds equal instruction,
  micro-op and branch counts and no mispredictions; the slow placement has the micro-op cache deliver partial groups in
  3.6-4.0 cycles per iteration against 1.4-1.9.
- The same loop under clang-cl 22 runs within 1% at every placement.
- The EPYC runners show at most a few percent, and not consistently: CI cannot check this.

`/QIntel-jcc-erratum` pads so that no jump crosses or ends on a 32-byte boundary. Only the AVX2 source is compiled with it.
- PC, AVX2 720p -> 4K Grayscale8, median ms: 8.12 and 7.56 at the plain build's two placements, 7.41-7.80 with the switch.
- On the SSE4.1 level the switch costs wherever that level was measured, the PC's P-cores aside:
  - EPYC 7763: RGB32 upscales 13-17%.
  - The PC's E-cores (Gracemont): RGB32 downscales 17-35%. Cause not investigated.
  - N4100: up to 5.6%, no row gaining.

### One-channel upscales filter four columns at a time (0bd90e4)

Where no x run exceeds 4 taps, the one-channel horizontal pass (`filterHorizontalShortRuns`) multiplies four columns' runs
and reduces them together: one reduction and one store per four columns, no branch on the run's length. The general
pass reduces and stores each column on its own.

- Each run loads 4 source floats and 4 weights whatever its length: the lanes past the run are masked out of the source,
  and the weights array's slack keeps the load in bounds.
- 720p -> 4K Grayscale8: PC, MSVC, AVX2 7.13 -> 5.45 ms, SSE4.1 8.34 -> 6.12. MSVC and Clang on the EPYC 7763 -27% to -30%.
  N4100 -20%. Clang on Neoverse-N2 -14%.
- GCC gains a third or less of what the others do, on both architectures: -8% to -10% on the EPYC 7763, -2% on
  Neoverse-N2. Not investigated.
- Two-channel pixels still take the general pass.

### Two chains at SSE4.1, and a first block that assigns

A `Floats8` is two XMM registers at SSE4.1, so a row pair's four chains were 16 accumulators, x64's whole register file:
MSVC kept eight on the stack. A 12-tap run, a 2x Lanczos3 downscale, ran the four-chain block once: its chains never
overlapped, and their zeroing and reduction were paid per column.

- Each primitives header states its level's chain count: 2 at SSE4.1, 4 at AVX2 and NEON.
- A run's first block starts the chains with its products: no zeroed accumulators, no add to zero. With FMA the result
  is bit-identical.
- N4100, MSVC: opaque 4-byte and RGB24 downscales -7% to -14% (4K -> 1080p RGB32 115.6 -> 102.1 ms), 4K -> 64x64 -2%,
  upscales within 3%.
- Two chains without the assigned first block cost 1080p -> 1440p 8%: a 4-tap run then pays a block's zeroing and reduction.
- One row per sweep, the other way to fit the registers, loses 9-41%: paired rows share the column's lookup, weights and loop.
- The chains are separate variables, and the block's helper takes the two rows' by name. Held in an array, or indexed by
  row, MSVC moved one row's chains through a second register every block at SSE4.1: 101 MP -> 720p 907 ms against 851.
- CI, EPYC 7763: two chains take 10-17% off the SSE4.1 downscales under Clang and GCC as well. The assigned first block
  takes 2-8% off the AVX2 downscales under Clang and GCC, and 4-9% off GCC's on Neoverse-N2; Clang there is within 3%.

### Runs a period apart share their weights

Dest coordinates `dstSize / gcd(srcSize, dstSize)` apart sample the source at the same sub-pixel phase, a whole number of
pixels apart. Such a run takes the earlier run's weights with its source position shifted, unless border clamping
touched either window.

- An exact-ratio scale's weights then stay in L1 through a row sweep: a 2:1 downscale has one run's weights where it had
  92 KB for 1920 columns, swept out of L2 once per row pair.
- The builder evaluates the kernel for one period and the borders, which shortens the serial part of a threaded resize.
- A shared run's weights are those computed for the earlier coordinate: the same in exact arithmetic, a rounding step
  apart in double.
- N4100, ms: 4K -> 1080p RGB32 102.3 -> 94.5, Grayscale8 59.2 -> 52.6, 24 MP -> 1080p 231.3 -> 215.0, 4K -> 64x64
  68.9 -> 61.8. With threads: 24 MP -> 1080p 68.0 -> 59.9, 1080p -> 240p 7.02 -> 5.71. Upscales -1% to -3%.
- 101 MP -> 720p does not gain: its period is 160 columns, 35 KB of weights against a 24 KB L1.
- CI: downscales -5% to -10% on Neoverse-N2 under GCC and Clang, threaded ones up to -17%; Clang's AVX2 downscales on
  the EPYC 7763 -4% to -10%.

### Output bytes packed without a clamp

The vertical pass capped its sums at 255 before converting them: `packus_epi16` reads its input as signed, so a word past
32767 would have packed to 0. The first pack is now the signed one, `packs_epi32`, which carries anything below 0 or past
255 to `packus_epi16` as such. NEON's narrowing instructions saturate both ways and needed no cap.

- One `min` fewer per 4 floats, at every level. The output is unchanged.
- N4100: upscales -6% to -8% under MSVC, -2% to -6% under clang-cl. Pi: upscales -2% to -8% under GCC and Clang.
- Pi, GCC: 4K -> 64x64 up to 6% slower and 101 MP -> 720p 1.5%. The NEON block for two rows fills the register file,
  and without the clamp's constant GCC's allocation spills one pixel vector in the block loop (open lead 12).

### The horizontal passes' shape is part of their tuning

`filterHorizontalRowGroup` writes each step once, in a loop over the rows, with three exceptions; two of its neighbours
carry one more each. Each answers one compiler, and each is commented where it stands:

| Compiler | Behaviour | In the code | Cost without it |
|---|---|---|---|
| MSVC | Moves chains held in an array, or indexed by row, through a second register every block | `RowChains` with named fields; the block helper takes the two rows by name | SSE4.1 downscales +3% to +8% on the N4100 |
| GCC | Leaves a loop over the rows rolled when its body holds a loop, with the rows' state in memory | `storeTempPixelWithTaps` called per row | 4K -> 1080p Grayscale8 +31% to +50% on the Pi, Neoverse-N2 and the EPYC 7763 |
| GCC | Places a step whose body is a loop over the rows out of line | `[[likely]]` on the three leftover-tap steps | 4K -> 1080p Grayscale8 +20% to +26% at AVX2 on the EPYC 7763 and 9V45; nothing on Golden Cove |
| MSVC | Takes a small array of sums through the stack: stored as a vector, reloaded as a scalar, reloaded again to copy out | `storeTempPixelWithTaps` holds the sums as scalars taken out of the vector | 4K -> 1080p Grayscale8 +11% on the N4100 |
| MSVC | Leaves a four-iteration loop with a large body rolled, its results in stack arrays | `filterHorizontalShortRuns` calls `multiplyShortRun` once per column, into named products | 720p -> 4K Grayscale8 +7% on the N4100; clang-cl +4%; GCC on the Pi up to +27% |

- The last two were found by reading MSVC's listing of the one-channel kernels beside clang-cl's.
- SSE4.1 conversion: each widening loads its own 4 bytes. MSVC otherwise copied and shifted one 16-byte load three times:
  4-byte downscales -3% to -5% on the N4100 under MSVC, nothing under clang-cl.

- A change to this function is compared by listing under MSVC, GCC and Clang against the commit before it.
- The 3- and 4-channel kernels under GCC carry about 3% more instructions with the row loops than without. No row shows it.

### Column strips (b9fea7e)

Each thread fills its ring for one column strip of the destination at a time. `stripWidthFor` bounds a ring to half the
smallest L2 share per logical processor, detected at runtime; an undetected L2 counts as the Pi's. The SIMD path converts
only the strip's source span (29b97e9).

| Machine | L2 share per logical processor | Ring budget |
|---|---:|---:|
| Pi 4 | 256 KB (1 MB, four cores) | 128 KB |
| PC | 512 KB (E-core cluster: 2 MB, four cores) | 256 KB |
| N4100 | 1024 KB (4 MB, four cores) | 512 KB |

The tests pass the Pi's budget on every machine, so they cover the same strip layouts everywhere.

Full-width rings overflowed the Pi's shared L2 with four threads: evicted dirty ring rows are written out to DRAM and
read back.
- Pi, ms with threads: 24 MP -> 1080p 236.8 -> 136.4, 4K -> 1080p RGBA32 167.3 -> 69.9.
- Both paths scale 3.1-3.6x on the Pi's four cores; before strips the SIMD path scaled 1.3-3.1x.
- Single-threaded the Pi is about neutral.
- Where full-width rings already fit, only the overhead shows: SIMD +1-5% single-threaded on Neoverse-N2, 3-5% on the
  PC's downscales.

Without the pre-touch below, strips cost x64 Windows single-threaded, the SIMD upscales most: 720p -> 4K +53-62% on the
EPYC 7763 under MSVC and clang-cl, +43-46% on the PC. The cost is Windows' demand-zero page faults on a fresh destination:
- One fault per page with and without strips, but about 1.1-1.2 us each when strips first write the pages out of address
  order, against 0.45 us in order.
- Finishing each page while cached does not help: see experiment 8.
- `touchDestPagesInOrder` writes one byte per page in address order before a band's strips run.
- Linux on the same CPU model shows no cost; why is unmeasured.
- PC, 720p -> 4K RGBA32, ms: 13.65 before strips, 19.59 with them, 13.93 with the pre-touch.
- Pre-touching a reused destination costs nothing measurable, with or without threads.

## Experiments that lost

1. **Weight broadcasts instead of the lane permute on ARM** (8429a19, reverted).
   - What: the horizontal pass spread weights with `permutevar8x32_ps`, which SIMDe emulated element by element on NEON.
     Two broadcasts replaced it outside x64.
   - Result: ARM GCC SIMD speedup 0.86-1.25x before, 0.84-1.34x after.
   - The cost was GCC's 256-bit type going through memory, not the permute.
2. **An early return and register-held span state in `prepareRun`** (e71b61c, reverted by d8172a0).
   - Aimed at MSVC, which lacks type-based alias analysis and may reload the buffer's fields after every float store.
   - PC, MSVC: within 5% either way, no row gaining from both together on upscales.
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
9. **Streaming stores to the destination** (N4100, not committed): within 1% under MSVC, upscales -3% to -4% under
   clang-cl. A block that is not 16-aligned, as RGB24's, mixes them with ordinary stores: +85%.
10. **The x weights pre-spread per channel in an aligned table** (N4100, not committed), so that the multiply takes them
    as its memory operand: 4-byte downscales +2% to +5% under MSVC. The table is 4x the weights, and built per call.
11. **A prefetch on each vertical tap's row** (N4100, not committed): within noise under both compilers.
12. **One row per horizontal sweep at SSE4.1** (N4100, not committed): 9-41% slower than paired rows.

## Open leads

1. **x64 GCC runners, 4K -> 64x64:** resizer / QImage 3.55 at f435b80, 4.04 at d436d41, 3.95 at 1ecd630. The sliding buffer
   fixed the same regression on MSVC: 1.84, 2.66, 1.76.
2. **Native size on the x64 runners:** resizer / `QImage::copy` 7.59 (GCC) and 7.26 (Clang) at d436d41, about 1.0 on ARM.
   Not investigated.
3. **Pre-conversion costs MSVC's upscales:** 1080p -> 1440p 7.25 -> 8.5-8.7 ms on the PC (experiment 6). The scalar path
   lost 7-13% on its three-channel upscales under MSVC alone, and its phase timings put the loss in the horizontal filter's
   code, not in memory traffic. Comparing MSVC's code for `filterHorizontalRowGroup` with clang-cl's is the next step.
4. **The SSE4.1 kernels on the N4100:** opaque downscales stay 3x and more behind Qt after the two-chain block. Its log
   has where the time goes: the horizontal taps are 53-70% of a downscale, the vertical pass 46-72% of an upscale.
   - Where the weights' period is long they are still swept out of L2 once per row pair: 101 MP -> 720p would gain 8% from
     weights in L1.
   - Otherwise the taps are bound by instruction count on a core that decodes 3 per cycle. 16-bit fixed-point kernels
     at this level would halve the loads and multiplies per tap and drop the float conversion.
   - A ring small enough for L1 gains upscales 3-7% and costs downscales up to 25%.
5. **Strips cost where the L2 is large:** 1-5% on Neoverse-N2's SIMD rows, 3-8% on the PC's downscales (the column strips
   section). A budget from the runtime L2 share would skip strips there.
6. **Placement still moves MSVC's AVX2 upscale rows about 5%** with `/QIntel-jcc-erratum`, and the SSE4.1 ones, built
   without it, up to 12-14%. Which loop of the upscale path reacts is unidentified: padding at the kernel's start shifts
   them all together. MSVC has no loop alignment control; clang-cl's code for the reduced loop does not react.
7. **`/QIntel-jcc-erratum` on the SSE4.1 level is untested on a Sandy Bridge.** Every CPU measured so far loses with it or
   is indifferent (the section on jumps and 32-byte boundaries).
8. **GCC's 720p -> 4K RGBA32 on Neoverse-N2 moves 10% with the binary's layout** (the call-site attribute notes): 24.1 or
   21.9 ms for the same kernel source. Code placement or the buffers' addresses, undetermined. The Pi does not react.
9. **The PC has not run anything since 0bd90e4:** the assigned first block, the shared weights, the 8-float step for
   4-float pixels and the row loops are measured on CI, the Pi and the N4100 only. Its MSVC upscale rows are the
   placement-sensitive ones.
10. **The vertical pass with its taps listed once per dest row:** the nonzero taps' row pointers and broadcast weights in
    a list, the first tap assigned. N4100: upscales -5% to -8% under clang-cl; under MSVC nothing, or downscales +2% to
    +4%, in three shapes of the loop (its log). Untried under GCC and at NEON.
11. **MSVC against clang-cl on the N4100, after the one-channel fixes:** clang-cl ahead by 12% on the Grayscale8
    downscale and 5% on the RGBA32 downscale, MSVC by 4-5% on the RGB32 and RGB24 downscales. The listings of those
    kernels have not been compared.
12. **Four chains at NEON leave GCC no spare register:** 16 accumulators, 2 weight registers and the block's 16 pixel
    vectors loaded ahead of the multiplies. On the Pi two chains gain GCC 4-6% on downscales, and cost Clang 8% on
    4K -> 64x64 and 3% on 101 MP -> 720p (its log). Not adopted. Neoverse-N2 and Apple Silicon are unmeasured.
