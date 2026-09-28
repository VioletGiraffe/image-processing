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
  - windows-latest runner: most rows within 3%, some 10%; scalar times swing up to 30%.
  - ubuntu-24.04-arm runner, Clang: about 1%.
  - macos-latest runner: up to 2x, too noisy to read per scenario.
- A CI comparison needs several samples per side: re-running the old commit's run alongside the new one gives same-time
  pairs. A re-run is a new attempt of the same run, with its own logs.
- `QT_NO_GUI_THREADPOOL=1` keeps the Qt control serial; `run_tests` sets it.
- Every benchmark allocates a fresh destination inside the timing, as the Qt control does. Threaded rows measured up to
  29b97e9 reused one destination.
- Windows throttles an unfocused console process onto E-cores: such runs came out 2-4x slow. The test runner opts out
  through `runCatchSession` (cpp-template-utils).
- A Pi 4 without cooling throttles under sustained load: `vcgencmd get_throttled` must print `0x0` after a run.
- A/B rounds alternate the builds.
- A CI label's runner CPU varies between runs, and the ratios with it: Qt's SSE code and the AVX2 kernel do not scale
  alike. A job's numbers compare across runs only when the CPU line above its table names the same model.

## Machines

| | CPU | Caches | Toolchain |
|---|---|---|---|
| PC | Core i5-12600K, P-cores | 48 KB L1D, 1.25 MB private L2 per P-core | MSVC 2022, Qt 6.11.2 |
| Raspberry Pi 4 | 4x Cortex-A72 | 32 KB L1D, 1 MB L2 shared by all cores | Clang 22 unless noted |
| ubuntu-24.04-arm | Cobalt 100 (Neoverse N2), 4 cores | 1 MB private L2 per core | GCC and Clang jobs |
| ubuntu-latest, windows-latest | 2 cores, 4 threads, varying by run. Seen: AMD EPYC 9V45 (Zen 5), 9V74 (Zen 4), 7763 (Zen 3); Xeon Platinum 8573C, Xeon 6973P-C | Private L2 per core: 1 MB on Zen 4 and 5, 512 KB on Zen 3, 2 MB on the Xeons | GCC and Clang; MSVC and clang-cl |
| macos-latest | Apple M1 (virtual), 3 cores | 12 MB L2 | Apple Clang |

## Standing against Qt

Resizer / QImage, lower is better. PC at 9d86565, before column strips (mean of three rounds; 4K -> 1080p RGB24 with
threads: 2b371a2, one run). Pi at 6b6bf7e. "-": no such benchmark.

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
| 4K -> 1080p Grayscale8 (scalar) | 1.06 | - | 1.19 | - |
| 720p -> 4K Grayscale8 (scalar) | 0.90 | - | 0.59 | - |
| 4K -> 1080p RGB24 (scalar) | 2.93 | 0.73 | 3.76 | 1.05 |
| 720p -> 4K RGB24 (scalar) | 2.09 | 0.53 | 1.09 | 0.35 |

- PC: upscales and straight-alpha images beat Qt; Qt premultiplies alpha in a separate pass.
- Pi: straight-alpha upscales beat Qt single-threaded, and the opaque 720p -> 4K about matches it. With threads every
  upscale beats it, 24 MP and RGBA32 downscales too, and RGB24 matches it.
- Opaque downscales stay about 2x behind Qt single-threaded on the PC and 3-5x on the Pi.
- RGB24 is the weak spot: no SIMD kernel, and 2.9-3.8x behind Qt on single-threaded downscales. Grayscale8 about matches Qt on downscales
  and beats it on upscales.

## What the design rests on

### Per-instruction-set primitives, not SIMDe

The kernel outline is written against primitives that state what the kernel needs, such as spreading a weight pair or
packing 8 floats to words. Each instruction set implements them natively. SIMDe, translating AVX2 intrinsics op by op,
falls short both ways:
- AVX2 code lowered to SSE4.1 falls back to scalar loops in the hot paths: `permutevar8x32_ps`, `cvttps_epi32` and the
  256-bit `shuffle_ps`, plus `cvtepu8_epi32` under MSVC, which lacks vector extensions.
- On AArch64, GCC kept SIMDe's 256-bit type in memory, copying it through the stack at every operation. Resizer / QImage
  on the ARM runners, 24 MP -> 1080p single-threaded, at d436d41: GCC 4.95, Clang 2.30.

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
5. **4K -> 64x64 with threads on the Pi:** scalar 59 ms against 47 for the whole-image temp (8ca3148).
6. **Strips cost up to 11% where the L2 is large:** Neoverse-N2's scalar downscales, 3-8% on the PC's (the column strips
   section). A budget from the runtime L2 share would skip strips there.
7. **GCC against Clang on AArch64 with the NEON primitives:** GCC was about 2x behind with SIMDe (the primitives
   section). The NEON `Floats8` is a struct of two `float32x4_t`, with no 256-bit type left to spill.
