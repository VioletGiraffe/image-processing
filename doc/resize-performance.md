# Resizer performance

The measurements behind the resizer's design, and the experiments that lost. Benchmarks live in
`tests/cimageresizer_benchmarks.cpp`; `scripts/run_tests --benchmark` runs them and `tests/report_benchmark_ratios.py` prints
the table. Every table names the commit it was measured at: re-measure after changing the resizer.

## Reading the numbers

- The resizer / QImage ratio is the only figure that compares across runs, machines and CI jobs.
- Absolute ms compare only on one machine, and only while the rows the change cannot affect agree.
- Noise gauges: Grayscale8 and RGB24 have no SIMD kernel, and the scalar rows ignore SIMD changes.
- Run-to-run noise:
  - PC: 5-8% on the few-ms rows, 1-2% elsewhere.
  - Pi: 5-10%.
  - windows-latest runner: scalar times swing up to 30%.
  - macos-latest runner: too noisy to read per scenario.
- `QT_NO_GUI_THREADPOOL=1` keeps the Qt control serial; `run_tests` sets it.
- The serial benchmarks include destination allocation, as the Qt control does.
- Windows throttles an unfocused console process onto E-cores: such runs came out 2-4x slow. The test runner opts out
  through `runCatchSession` (cpp-template-utils).
- A Pi 4 without cooling throttles under sustained load: `vcgencmd get_throttled` must print `0x0` after a run.
- A/B rounds alternate the builds.
- A CI label's runner CPU varies between runs, and the ratios with it: Qt's SSE code and the AVX2 kernel do not scale
  alike. A job's numbers compare across runs only when its "Show the CPU" step reports the same model.

## Machines

| | CPU | Caches | Toolchain |
|---|---|---|---|
| PC | Core i5-12600K, P-cores | 48 KB L1D, 1.25 MB private L2 per P-core | MSVC 2022, Qt 6.11.2 |
| Raspberry Pi 4 | 4x Cortex-A72 | 32 KB L1D, 1 MB L2 shared by all cores | Clang 22 unless noted |
| ubuntu-24.04-arm | Cobalt 100 (Neoverse N2), 4 cores | 1 MB private L2 per core | GCC and Clang jobs |
| ubuntu-latest, windows-latest | 2 cores, 4 threads, varying by run. Seen: AMD EPYC 9V74 (Zen 4), EPYC 7763 (Zen 3), Xeon Platinum 8573C | 1 MB, 512 KB, 2 MB private L2 per core | GCC and Clang; MSVC and clang-cl |
| macos-latest | Apple M1 (virtual), 3 cores | 12 MB L2 | Apple Clang |

## Standing against Qt

Resizer / QImage, lower is better. PC at 9d86565 (mean of three rounds; 4K -> 1080p RGB24 with threads: one run), Pi at
6fac81f (4K -> 1080p RGB24 with threads: 2b371a2). All three have the same resizer code, from before column strips. "-": no
such benchmark.

| Scenario | PC | PC, threads | Pi | Pi, threads |
|---|---|---|---|---|
| 24 MP -> 1080p | 1.77 | 0.51 | 2.95 | 1.51 |
| 4K -> 1080p RGB32 | 1.99 | - | 4.88 | - |
| 4K -> 1080p RGBA32 | 0.94 | 0.28 | 2.58 | 1.82 |
| 720p -> 4K RGB32 | 0.59 | - | 1.07 | - |
| 720p -> 4K RGBA32 | 0.31 | 0.07 | 0.33 | 0.26 |
| 1080p -> 1440p | 0.87 | 0.21 | 1.84 | 0.59 |
| 1080p -> 240p | 2.36 | 0.95 | 3.73 | 1.29 |
| 4K -> 64x64 | 2.56 | 1.05 | 3.42 | 1.31 |
| 101 MP -> 720p | 2.22 | 0.64 | 4.25 | 1.59 |
| 1080p, native size | 1.12 | - | 1.06 | - |
| 4K -> 1080p Grayscale8 (scalar) | 1.06 | - | 1.16 | - |
| 720p -> 4K Grayscale8 (scalar) | 0.90 | - | 0.56 | - |
| 4K -> 1080p RGB24 (scalar) | 2.93 | 0.73 | 3.64 | 2.31 |
| 720p -> 4K RGB24 (scalar) | 2.09 | 0.53 | 1.06 | 0.54 |

- PC: upscales and straight-alpha images beat Qt; Qt premultiplies alpha in a separate pass.
- Pi: straight-alpha upscales beat Qt single-threaded, and the opaque 720p -> 4K about matches it. Every upscale beats it
  with threads.
- Opaque downscales stay about 2x behind Qt single-threaded on the PC and 3-5x on the Pi.
- RGB24 is the weak spot: no SIMD kernel, and 2-3.6x behind Qt on downscales. Grayscale8 about matches Qt on downscales
  and beats it on upscales.

## What the design rests on

### Native AVX2 under GCC and Clang (7919eaf)

Before it, SIMDe emulated every AVX2 intrinsic in plain C on x64 GCC and Clang: the per-function target attribute does not
tell SIMDe that AVX2 exists. `simd_support.h` now wraps the SIMDe include in an AVX2 target region.

CI at e67a9dc, SIMD / scalar ms within one job:

| Scenario | ubuntu-latest, GCC | windows-latest, MSVC |
|---|---|---|
| 4K -> 1080p | 152.9 / 131.6 | 25.7 / 122.5 |
| 24 MP -> 1080p | 320.1 / 351.8 | 60.0 / 318.1 |
| 101 MP -> 720p | 1208.6 / 1388.5 | 195.8 / 1217.9 |
| 64x64 -> 4K | 102.0 / 45.4 | 13.5 / 57.1 |

### ARM builds need Clang

On AArch64, GCC keeps SIMDe's 256-bit type in memory: every `simde_mm256_*` operation copies through the stack. Clang keeps
both 128-bit halves in NEON registers. Resizer / QImage on the ARM runners, 24 MP -> 1080p single-threaded:

- f435b80: GCC 6.46, Clang 5.14.
- d436d41: GCC 4.95, Clang 2.30. Pre-conversion removed the work that hid GCC's overhead.

A struct of two `simde__m128` would let GCC keep the halves in registers; not done.

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
- 256-bit packs work per 128-bit lane. Joining the lanes takes a lane-crossing `permutevar8x32`, which SIMDe emulates
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

### The scalar path's temp-row ring and whole-row conversion

The scalar path used to run two passes through a whole-image float temp, and converted and premultiplied a source pixel at
every tap. It now shares `TempRowRing` with the SIMD kernel, and converts each source row once:
- Whole rows, not the sliding buffer: on ARM the scalar path only serves layouts without a kernel, 1-3 floats per pixel in
  one row. The whole rows that overflowed the Pi's L2 held 8: two rows of 4.
- Tight packing and RGB32 get a compile-time pixel stride. With a runtime one, MSVC calls `memcpy` for every pixel's tail
  bytes, and the conversion loop stays generic.

PC, MSVC, scalar / QImage, 8ca3148 -> the next commit. Three alternating rounds, averaged; the SIMD rows held within 1%,
64x64 within 6%. The threaded rows spread up to 21% between rounds.

| Scenario | Single-threaded | Threads |
|---|---|---|
| 24 MP -> 1080p | 8.39 -> 6.33 | 2.38 -> 1.77 |
| 4K -> 1080p RGB32 | 8.24 -> 7.25 | - |
| 4K -> 1080p RGBA32 | 6.09 -> 4.48 | 1.74 -> 1.24 |
| 4K -> 1080p Grayscale8 | 1.51 -> 1.06 | - |
| 4K -> 1080p RGB24 | 3.45 -> 2.93 | - |
| 720p -> 4K RGB32 | 1.81 -> 1.99 | - |
| 720p -> 4K RGBA32 | 1.62 -> 1.40 | 0.43 -> 0.37 |
| 720p -> 4K Grayscale8 | 1.17 -> 0.90 | - |
| 720p -> 4K RGB24 | 1.86 -> 2.09 | 0.59 -> 0.53 |
| 1080p -> 1440p | 2.80 -> 3.30 | 0.88 -> 0.84 |
| 1080p -> 240p | 11.65 -> 8.26 | 3.48 -> 2.46 |
| 4K -> 64x64 | 15.83 -> 11.65 | 4.69 -> 4.08 |
| 101 MP -> 720p | 12.61 -> 9.46 | 3.52 -> 2.57 |

- RGBA32 gains the most: the premultiply ran at every tap. The same upscale gains 13% as RGBA32 and loses 7% as RGB32.
- Three-channel upscales lose 7-13% single-threaded and gain with threads: open lead 3. The Grayscale8 upscale gains 23%.

CI, scalar / QImage single-threaded, 8ca3148 -> 9d86565, in the jobs whose SIMD rows held:
- ARM gains the most: Clang 35-65%, GCC 28-60%. ARM GCC's RGB24 and Grayscale8 upscales gain only 5-7%.
- Only MSVC gains nothing on upscales: 720p -> 4K RGB32 -2%, RGB24 +2%, 1080p -> 1440p +5%. On the same rows clang-cl gains
  23-31%, and x64 Clang 15-28%, overstated by about 15%: its SIMD rows drifted faster.
- x64 GCC ran on a slower machine after. Against 6fac81f's EPYC 9V74, whose SIMD times match the before run's, its
  scalar / SIMD ratio fell 16-44%.
- clang-cl's scalar path runs about 23% ahead of MSVC's on 24 MP -> 1080p at 6fac81f: Zen 3 against Zen 4, with their Qt
  and SIMD times within 2%.

Pi, scalar ms, 8ca3148 -> 6fac81f. The Qt controls held within 2%, except 4K -> 1080p RGBA32: +6%, its SIMD row +9%.
RGB24 4K -> 1080p with threads: 8ca3148 with 2b371a2's benchmark file -> 2b371a2; the other threaded rows matched within 1%.

| Scenario | Single-threaded | Threads | Thread speedup |
|---|---|---|---|
| 4K -> 1080p RGB24 | 372 -> 247 | 136 -> 157 | 2.73x -> 1.58x |
| 4K -> 1080p Grayscale8 | 143 -> 121 | - | - |
| 720p -> 4K RGB24 | 189 -> 118 | 62.7 -> 59.9 | 3.01x -> 1.98x |
| 720p -> 4K Grayscale8 | 76.4 -> 52.2 | - | - |
| 720p -> 4K RGB32 | 265 -> 118 | - | - |
| 720p -> 4K RGBA32 | 322 -> 155 | 127 -> 143 | 2.54x -> 1.09x |
| 1080p -> 1440p | 128 -> 87 | 38.4 -> 36.7 | 3.34x -> 2.36x |
| 24 MP -> 1080p | 819 -> 611 | 305 -> 310 | 2.69x -> 1.97x |
| 4K -> 1080p RGBA32 | 507 -> 321 | 215 -> 231 | 2.36x -> 1.39x |
| 1080p -> 240p | 59.7 -> 51.8 | 16.7 -> 16.7 | 3.57x -> 3.11x |
| 4K -> 64x64 | 172 -> 166 | 47.1 -> 58.1 | 3.65x -> 2.86x |
| 101 MP -> 720p | 2569 -> 2277 | 841 -> 1032 | 3.06x -> 2.21x |

- RGB24 and Grayscale8, the layouts ARM runs scalar, gain 15-37% single-threaded. With threads the RGB24 upscale gains 4%,
  and the RGB24 downscale loses 15%: the column strips section.
- No upscale loses: open lead 3 is MSVC's.
- With threads the scalar path now scales like the SIMD path, which uses the same ring. Four RGB32 and RGBA32 rows lose
  7-24%: the column strips section.
- 4K -> 64x64 and 101 MP gain only 3% and 11% single-threaded. Likely cause: their float rows, 46 and 139 KB, exceed the
  A72's 32 KB L1, and their long x runs read each pixel many times.

### Column strips (b9fea7e)

Each thread fills its ring for one column strip of the destination at a time. `stripWidthFor` bounds a ring to 128 KB, half
of a four-core share of the Pi's 1 MB L2. The SIMD path converts only the strip's source span (29b97e9).

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
- CI shows only the cost: the runners' L2 is private, 512 KB or 1 MB per core, and full-width rings already fit.

Strips cost x64 Windows single-threaded, the SIMD upscales most. CI, SIMD time, 2b371a2 -> b9fea7e: MSVC and clang-cl on
EPYC 7763 +57-67% on 720p -> 4K and +15-17% on 4K -> 1080p RGB32; x64 GCC on the same CPU model 0-6%; ARM 0-6%.
With threads every job stayed within -12% to +6%.

PC, MSVC, SIMD, mean ms of five alternating rounds.

| Scenario | 2b371a2 | b9fea7e | 29b97e9 (cap) |
|---|---:|---:|---:|
| 4K -> 1080p RGB32 | 15.42 | 18.28 (+19%) | 17.81 (+15%) |
| 4K -> 1080p RGB32 [reused dest] | 14.55 | 15.97 (+10%) | 15.34 (+5%) |
| 720p -> 4K RGBA32 | 12.75 | 18.74 (+47%) | 18.13 (+42%) |
| 720p -> 4K RGB32 | 11.94 | 17.35 (+45%) | 16.78 (+41%) |
| 24 MP -> 1080p | 34.87 | 37.37 (+7%) | 37.10 (+6%) |
| 1080p -> 1440p (one strip) | 8.39 | 8.48 | 8.30 |

- Most of the cost is first-touch page faults on the freshly allocated destination. The fault zeroes a page into the
  cache; a strip fills only part of it, and the page is written back before the later strips fill the rest.
- The fresh-destination premium on 4K -> 1080p RGB32 grew from 0.87-0.95 ms to 2.3-2.7 ms in each of three runs. An
  upscale writes 9 times its source in destination, so it pays the most.
- Linux on the same CPU does not pay it. Unverified explanation: glibc's dynamic `mmap` threshold keeps a freed large
  block mapped, so the next allocation reuses faulted pages.
- The scalar path pays 3-10% on the PC with several strips; it already converted only the strip's span.

## Experiments that lost

1. **Weight broadcasts instead of the lane permute on ARM** (8429a19, reverted).
   - What: the horizontal pass spreads weights with `permutevar8x32_ps`, which SIMDe emulates element by element on NEON.
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

## Open leads

1. **x64 GCC runners, 4K -> 64x64:** resizer / QImage 3.55 at f435b80, 4.04 at d436d41, 3.95 at 1ecd630. The sliding buffer
   fixed the same regression on MSVC: 1.84, 2.66, 1.76.
2. **Native size on the x64 runners:** resizer / `QImage::copy` 7.59 (GCC) and 7.26 (Clang) at d436d41, about 1.0 on ARM.
   Not investigated.
3. **Pre-conversion costs MSVC's upscales 7-17%**, on both paths: SIMD 1080p -> 1440p 7.25 -> 8.5-8.7 ms on the PC
   (experiment 6), scalar three-channel upscales 7-13% single-threaded on the PC and up to 5% on the MSVC runner. The other
   compilers gain on the same scalar rows (the scalar ring section). Scalar 1080p -> 1440p phase timings on the PC,
   Mcycles per resize:
   - The horizontal filter reading floats takes 68-69, against 55-56 reading bytes, despite fewer instructions per tap.
   - Skipping the vertical pass leaves it at 68-69, so the fused loop's other phases do not evict its data.
   - Conversion takes 4.6; the ring saved 3-4 on the vertical pass.
   - Unexplained. A loss confined to MSVC points at its code for `filterHorizontalRow`, not at memory traffic: comparing
     it with clang-cl's is the next step.
4. **CPUs without AVX2 take the scalar path:** there is no SSE4.1 kernel yet. Baselines on a Sandy Bridge laptop and a
   Celeron N4100 come first.
5. **Strips cost x64 Windows up to 42% on the PC's single-threaded SIMD upscales** (the column strips section), mostly in
   first-touch faults on a fresh destination.
   - Candidate: row blocks. Each thread finishes all strips of a block of destination rows before the next, so every
     destination page fills while cached. The temp rows under the window at each block boundary get recomputed.
   - A runtime L2-share budget does not remove it: a CI Windows runner's 512 KB L2, shared by two threads, yields the same
     128 KB budget.
6. **4K -> 64x64 with threads on the Pi:** scalar 59 ms against 47 for the whole-image temp (8ca3148).
