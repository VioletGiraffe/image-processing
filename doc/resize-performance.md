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

## Machines

| | CPU | Caches | Toolchain |
|---|---|---|---|
| PC | Core i5-12600K, P-cores | 48 KB L1D, 1.25 MB private L2 per P-core | MSVC 2022, Qt 6.11.2 |
| Raspberry Pi 4 | 4x Cortex-A72 | 32 KB L1D, 1 MB L2 shared by all cores | Clang 22 unless noted |
| ubuntu-24.04-arm | Cobalt 100 (Neoverse N2) | large private L2 | GCC and Clang jobs |
| ubuntu-latest | x64 | | GCC and Clang jobs |
| windows-latest, macos-latest | x64; Apple silicon | | MSVC; Apple Clang |

## Standing against Qt

Resizer / QImage, lower is better. PC at 003d5e6, Pi at 1ecd630. "-": not measured at that commit.

| Scenario | PC | PC, threads | Pi | Pi, threads |
|---|---|---|---|---|
| 24 MP -> 1080p | 1.77 | 0.57 | 2.94 | 1.51 |
| 4K -> 1080p RGB32 | 2.01 | - | 4.84 | 3.16 |
| 4K -> 1080p RGBA32 | 0.89 | 0.31 | - | - |
| 720p -> 4K RGBA32 | 0.32 | 0.07 | - | - |
| 1080p -> 1440p | 0.93 | 0.26 | 1.92 | 0.63 |
| 1080p -> 240p | 2.57 | 1.41 | 3.63 | 1.42 |
| 4K -> 64x64 | 2.36 | 1.13 | 3.62 | 1.40 |
| 101 MP -> 720p | 2.24 | 0.66 | 4.32 | 1.48 |
| 1080p, native size | 1.02 | - | 1.00 | - |
| 4K -> 1080p Grayscale8 (scalar) | 1.28 | - | 1.39 | - |
| 4K -> 1080p RGB24 (scalar) | 3.13 | - | 5.49 | - |

- Upscales and straight-alpha images beat Qt; Qt premultiplies alpha in a separate pass.
- Opaque downscales stay about 2x behind Qt single-threaded on the PC and 3-5x on the Pi.
- RGB24 and Grayscale8 are the weak spots: they have no SIMD kernel.

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
the SIMD path: at 1ecd630 the 4K -> 1080p RGBA32 ratio was 0.858 and 0.797 in two runs, and at 003d5e6 it was 0.885. The
untouched RGB32 row moved by as much in the same runs.

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

## Open leads

1. **x64 GCC runners, 4K -> 64x64:** resizer / QImage 3.55 at f435b80, 4.04 at d436d41, 3.95 at 1ecd630. The sliding buffer
   fixed the same regression on MSVC: 1.84, 2.66, 1.76.
2. **`packEightFloatsToBytes` on ARM:** SIMDe emulates its `permutevar8x32_epi32` on NEON, once per 8 output pixels. The
   ARM runners' upscale ratio lags: 64x64 -> 4K was 0.92 on ARM Clang against 0.40 on x64 Clang at d436d41.
3. **Native size on the x64 runners:** resizer / `QImage::copy` 7.59 (GCC) and 7.26 (Clang) at d436d41, about 1.0 on ARM.
   Not investigated.
4. **Scalar straight alpha:** after 003d5e6, the PC's 4K -> 1080p RGBA32 scalar / SIMD ratio rose from 5.6-5.7x to 7.6x,
   while the SIMD row held steady against Qt. The scalar path premultiplies at every tap: about 6 times per source pixel
   in a 2x Lanczos downscale. Converting each row once, as the SIMD path does, would fix it.
5. **CPUs without AVX2 take the scalar path:** there is no SSE4.1 kernel yet. Baselines on a Sandy Bridge laptop and a
   Celeron N4100 come first.
