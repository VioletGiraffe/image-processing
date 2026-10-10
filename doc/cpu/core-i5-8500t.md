# Core i5-8500T

The measurement log for this CPU. Conclusions that shape the design are summarized in
[resize-performance.md](../resize-performance.md); the detail stays here.

## Machine

| | |
|---|---|
| CPU | Core i5-8500T (Coffee Lake), 6 cores, 6 threads, 2.1 GHz nominal, 3.5 GHz turbo |
| SIMD | AVX2 and FMA |
| Caches | 32 KB L1D, 256 KB private L2 per core, 9 MB L3 |
| Detected sizing | 256 KB of L2 per logical processor, ring budget 128 KB |
| RAM | 32 GB |
| Toolchain | MSVC 19.51 (toolset 14.51.36231), clang-cl of Visual Studio 18.10.3, Qt 6.11.2 |
| OS | Windows 10 |

## Reading this machine's numbers

- QImage controls agree within 1% between binaries on most rows; the 4K -> 1080p RGBA32, RGB24 and Grayscale8 controls
  ranged 4-6% in one session.
- A full benchmark run takes 65 s, a build 25-30 s.
- A ring near the L2's size varies by run: see the ring budget section.

## Standing against Qt, a6c69e7

Minimum ms of three alternating rounds of the two binaries.

| Scenario | MSVC | QImage | Ratio | Threads | Ratio | Speedup | clang-cl | clang-cl / MSVC |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 57.7 | 37.1 | 1.56 | 16.7 | 0.45 | 3.45x | 64.8 | +12.3% |
| 4K -> 1080p RGB32 | 27.1 | 14.9 | 1.81 | - | - | - | 30.2 | +11.7% |
| 4K -> 1080p RGBA32 | 32.2 | 29.5 | 1.09 | 9.17 | 0.31 | 3.52x | 32.6 | +1.0% |
| 4K -> 1080p RGB24 | 27.7 | 32.3 | 0.86 | 8.04 | 0.25 | 3.44x | 30.4 | +9.8% |
| 4K -> 1080p Grayscale8 | 16.0 | 36.7 | 0.44 | - | - | - | 15.5 | -3.3% |
| 720p -> 4K RGBA32 | 23.6 | 77.9 | 0.30 | 8.75 | 0.11 | 2.70x | 24.1 | +2.1% |
| 720p -> 4K RGB32 | 20.7 | 45.6 | 0.45 | - | - | - | 21.9 | +5.7% |
| 720p -> 4K RGB24 | 18.7 | 36.7 | 0.51 | 6.55 | 0.18 | 2.86x | 20.1 | +7.8% |
| 720p -> 4K Grayscale8 | 7.95 | 26.5 | 0.30 | - | - | - | 7.42 | -6.7% |
| 1080p -> 1440p | 15.5 | 20.0 | 0.77 | 4.94 | 0.25 | 3.13x | 16.3 | +5.3% |
| 1080p -> 240p | 4.50 | 2.39 | 1.88 | 1.41 | 0.59 | 3.19x | 5.01 | +11.3% |
| 4K -> 64x64 | 12.3 | 6.55 | 1.87 | 4.39 | 0.67 | 2.80x | 12.7 | +3.5% |
| 101 MP -> 720p | 184.6 | 94.8 | 1.95 | 51.6 | 0.54 | 3.57x | 205.3 | +11.3% |
| 1080p, native size | 1.81 | 1.51 | 1.19 | - | - | - | 1.83 | +1.4% |

- Opaque 4-byte downscales stay 1.6-2.0x behind Qt single-threaded; every other row beats it, and every threaded row.
- MSVC leads clang-cl by 5-12% on the 3-channel rows. clang-cl leads on Grayscale8 by 3-7%.
- The work runs in at most 4 bands on 6 cores.

## Where the time goes at AVX2

MSVC, a6c69e7, minimum ms of two rounds. Four builds: whole; without the vertical pass; with the horizontal filter
replaced by zeroing its temp rows; with conversion only (each run's `prepareRun`, the temp rows zeroed, no vertical
pass). The parts below are differences of those. "Fixed": weight tables, allocation, the destination's page faults.

| Scenario | Whole | Conversion | Horizontal | Vertical | Fixed |
|---|---:|---:|---:|---:|---:|
| 4K -> 1080p RGB32 | 26.9 | 8.7 | 11-12 | 5.6 | about 1 |
| 24 MP -> 1080p | 56.2 | about 20 | about 28 | 6.8 | about 1 |
| 101 MP -> 720p | 185.2 | about 68 | about 103 | 13.5 | about 1 |
| 720p -> 4K RGBA32 | 21.6 | 2.0 | about 6.5 | 8.3 | about 5 |

- "Conversion" is a third of a downscale. It includes reading the source from RAM (33 MB for 4K, 404 MB for 101 MP) and
  each run's lookup and `prepareRun` check, about 20 instructions per dest pixel; the conversion loop itself is 15
  instructions per 4 pixels.
- Per multiply-add the two passes cost the same on 4K -> 1080p RGB32: 5.6 ms for the vertical pass's 75 million, 11-12 ms
  for the horizontal pass's 149 million. Filtering vertically first would move work between equally priced kernels.
- The vertical pass spends about 2 cycles per multiply-add instruction.
- A fresh 4K destination's page faults are a quarter of the RGBA32 upscale.

## Ring budget

MSVC, one binary, the budget overridden per run; minimum of two rounds, against the detected 128 KB.

| Scenario | 16 KB | 32 KB | 64 KB | 400 KB | 8 MB |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | +15.9% | +8.5% | +1.9% | +1.8% | +0.8% |
| 4K -> 1080p RGB32 | +10.7% | +18.5% | +3.0% | +14.0% | +2.4% |
| 4K -> 1080p RGBA32 | +19.1% | +14.9% | +2.9% | +12.1% | +6.8% |
| 720p -> 4K RGBA32 | +3.3% | +9.4% | -0.7% | +1.9% | +2.6% |
| 1080p -> 1440p | +6.3% | +7.0% | +0.9% | 0.0% | +4.7% |
| 4K -> 64x64 | +14.6% | -1.2% | +2.2% | -6.9% | -8.7% |
| 101 MP -> 720p | +21.6% | +8.9% | +3.7% | -0.7% | -0.9% |

- A ring small enough for L1 loses on every row. Whether the vertical pass alone gains from it is not separated here.
- 400 KB and 8 MB both run 4K -> 1080p as one strip with a 276 KB ring, and read 10 points apart: a ring just past the
  256 KB L2 varies by run.
- 4K -> 64x64 gains 7-9% without strips: at 128 KB its 64 columns are split into three strips.

## Band count

MSVC, a6c69e7 with the cap of `forEachRowBand` overridden per run; threaded rows, minimum of two rounds. The benchmark's
pool has 5 workers beside the calling thread.

| Scenario | 4 bands, ms | 5 | 6 | 8 | 12 | 24 | 48 | 96 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 16.6 | -19% | -28% | -1% | -27% | -23% | -18% | -7% |
| 4K -> 1080p RGBA32 | 9.56 | -15% | -28% | -2% | -27% | -21% | -21% | -14% |
| 720p -> 4K RGBA32 | 8.71 | -14% | -24% | -7% | -25% | -24% | -25% | -20% |
| 1080p -> 1440p | 4.88 | -14% | -26% | -7% | -28% | -22% | -14% | +4% |
| 1080p -> 240p | 1.45 | -17% | -23% | +1% | -19% | -5% | +24% | +27% |
| 4K -> 64x64 | 4.51 | -13% | -18% | +11% | +6% | +15% | +14% | +13% |
| 101 MP -> 720p | 54.4 | -17% | -29% | -2% | -28% | -22% | -14% | +7% |

- 24, 48 and 96 were measured against 6 in a second session; the columns restate them against 4.
- A count that is not a multiple of the thread count leaves bands for a second pass: 8 runs like 4.
- Bands past twice the thread count cost on every row, upscales included: each band pays more than its window of rows.

## Two vertical blocks per step (b8bdd30)

Minimum of three alternating rounds, against the same source with one block per step.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 720p -> 4K RGB32 | -9.7% | -14.1% |
| 720p -> 4K RGB24 | -4.9% | -9.6% |
| 720p -> 4K RGBA32 | -6.2% | -3.6% |
| 720p -> 4K Grayscale8 | -2.2% | +1.3% |
| 1080p -> 1440p | -6.5% | -13.5% |
| 4K -> 1080p RGBA32 | -5.8% | -6.3% |
| 4K -> 1080p RGB24 | -2.8% | -8.0% |
| 4K -> 1080p RGB32, two benchmarks | -0.8%, -1.7% | -10.2%, -7.7% |
| 4K -> 1080p Grayscale8 | -0.9% | -3.1% |
| 24 MP -> 1080p | -0.9% | -0.7% |
| 1080p -> 240p | -4.0% | +0.7% |
| 4K -> 64x64 | -0.2% | -3.1% |
| 101 MP -> 720p | -2.2% | -0.4% |

- MSVC's tap loop for two blocks: 15 instructions with 6 multiply-adds for 3 channels, 17 with 8 for 4, no stack
  references.
- The two 4K -> 1080p RGB32 benchmarks of one binary read up to 5 points apart within a session.
- A software prefetch of the next row pair's source segment, tried beside it: rows between -7% and +4%, no pattern.

## WSL 2 on this machine: GCC 14.3 and Clang 22 (b8bdd30)

Ubuntu 22.04. Minimum of three alternating rounds, two blocks per step against one. The SSE4.1 rows, whose code did not
change, stay within 2.7%.

| Scenario, AVX2 | GCC | Clang |
|---|---:|---:|
| 720p -> 4K RGB32 | -10.3% | -10.9% |
| 720p -> 4K RGB24 | -10.6% | -18.5% |
| 720p -> 4K RGBA32 | +3.7% | +1.2% |
| 720p -> 4K Grayscale8 | -0.9% | -5.2% |
| 1080p -> 1440p | -6.3% | -7.4% |
| 4K -> 1080p RGBA32 | -5.1% | -4.7% |
| 4K -> 1080p RGB24 | -1.2% | -3.6% |
| 4K -> 1080p RGB32, two benchmarks | +5.5%, +4.3% | -2.2%, -2.0% |
| 4K -> 1080p Grayscale8 | +4.1% | -1.2% |
| 24 MP -> 1080p | -2.0% | -2.3% |
| 101 MP -> 720p | -1.4% | -1.6% |

- GCC's tap loop for two blocks: 16 instructions with 8 multiply-adds, no stack references. Its register and stack
  assignment changed across the whole kernel, the horizontal pass included.
- GCC's slower rows are placement: the next section.

## Jumps on 32-byte boundaries under GCC

This CPU has the JCC erratum: a jump that crosses or ends on a 32-byte boundary is not held in the decoded cache.
GCC 14.3 in WSL, minimum of three rounds per session, all against the build of a6c69e7 with default flags.

| Scenario | b8bdd30 | a6c69e7, branches within 32 bytes | b8bdd30, branches within 32 bytes |
|---|---:|---:|---:|
| 720p -> 4K RGB32 | -10.8% | -7.4% | -19.1% |
| 720p -> 4K RGB24 | -10.9% | -6.8% | -16.3% |
| 720p -> 4K RGBA32 | +3.1% | -4.3% | -0.1% |
| 1080p -> 1440p | -6.8% | -10.5% | -19.8% |
| 4K -> 1080p RGB32, two benchmarks | +6.1%, +6.9% | -1.8%, -1.8% | -5.9%, -5.9% |
| 4K -> 1080p RGB24 | -1.0% | -2.9% | -5.8% |
| 4K -> 1080p RGBA32 | -5.0% | -5.7% | -7.6% |
| 4K -> 1080p Grayscale8 | +3.6% | -4.5% | -4.5% |
| 24 MP -> 1080p, 101 MP -> 720p | within 1.5% | within 1.5% | -1.8%, -1.9% |

- "Branches within 32 bytes": `-Wa,-mbranches-within-32B-boundaries` on the compile and the link.
- Jumps of the AVX2 RGB32 kernel that cross or end on a boundary: 21 of 113 at a6c69e7 and 13 of 121 at b8bdd30,
  1 and 3 with the option. Clang's builds: 27 of 190 and 22 of 207.
- `-falign-loops=32 -falign-functions=64` alone moves a6c69e7's rows by up to 8%: 4K -> 1080p Grayscale8 +8.3%,
  1080p -> 1440p +5.6%.
- The vertical pass kept out of line alone makes a6c69e7's 4K -> 1080p RGB32 6.7-6.9% slower.

## The erratum option per file, and LTO (b8bdd30)

WSL, Qt 6.11.2, minimum of three rounds. The AVX2 kernel file compiled four ways, against the project's build of that
session, whose AVX2 rows ran close to the padded builds of the session before.

| | GCC | Clang |
|---|---|---|
| Boundary jumps in the RGB32 and RGBA32 AVX2 kernels, plain | 14, 14 | 22, 37 |
| The option on that file, LTO kept | 14, 24 | 22, 37 |
| The option on that file, no LTO | 3, 3 | 1, 0 |
| No LTO only | 16, 19 | 22, 37 |
| AVX2 rows, the option without LTO | 0% to -7.3% | +2.7% to -4.1% |
| AVX2 rows, no LTO only | within 2.4%, Grayscale8 +4.8% and -6.4% | +4.0% to -4.6% |

- Without LTO the AVX2 kernels are 0.5-2% smaller under GCC and 1-4% under Clang, and call `TempRowRing`'s constructor
  where LTO inlines it. No other call differs.
- The SSE4.1 rows, their code under LTO in every build, range -16% to +10% between these builds.

## Three blocks per step, and the taps listed per dest row

Windows, minimum of three rounds against b8bdd30.

| Scenario | Three blocks for 3 channels, MSVC | Tap list, MSVC | Tap list, clang-cl |
|---|---:|---:|---:|
| 720p -> 4K RGBA32 | +0.1% | -6.2% | -3.3% |
| 720p -> 4K RGB32 | +0.8% | -5.1% | -2.3% |
| 720p -> 4K RGB24 | +1.4% | -4.6% | +6.1% |
| 720p -> 4K Grayscale8 | +0.6% | -3.3% | -4.2% |
| 1080p -> 1440p | -1.9% | -4.5% | +3.3% |
| 4K -> 1080p RGB32, two benchmarks | +0.9%, +2.1% | -3.9%, -2.5% | -4.5%, +2.5% |
| 4K -> 1080p RGB24 | +0.3% | -4.2% | -0.8% |
| 4K -> 1080p RGBA32 | +5.4% | -0.5% | +2.8% |
| 24 MP -> 1080p | +4.1% | +1.3% | -3.2% |
| 101 MP -> 720p | +1.5% | +2.7% | -1.3% |

- The tap list in WSL at AVX2: GCC -2.3% to +2.1% with 720p -> 4K RGB24 at -6.4%, Clang -3.5% to +3.7%.

## MSVC against clang-cl by parts (8d72f9b)

Windows, minimum ms of two rounds, MSVC / clang-cl. The parts as in "Where the time goes at AVX2"; the build without
the vertical pass lets the temp rows' addresses escape, or clang-cl removes the horizontal filter with it.

| Scenario | Whole | Conversion-only build | Horizontal | Vertical |
|---|---|---|---|---|
| 4K -> 1080p RGB32 | 24.7 / 28.3 | 10.7 / 11.7 | 10.9 / 12.9 | 3.1 / 3.7 |
| 4K -> 1080p RGB24 | 25.9 / 28.6 | 10.9 / 8.1 | 11.3 / 16.1 | 3.7 / 4.4 |
| 4K -> 1080p RGBA32 | 28.4 / 30.2 | 13.3 / 10.8 | 10.6 / 13.9 | 4.4 / 5.5 |
| 4K -> 1080p Grayscale8 | 15.7 / 14.8 | 5.0 / 4.3 | 9.2 / 8.6 | 1.5 / 1.9 |
| 24 MP -> 1080p | 55.4 / 63.9 | 22.1 / 24.9 | 28.0 / 30.5 | 5.3 / 8.4 |
| 101 MP -> 720p | 181.7 / 204.5 | 70.6 / 85.2 | 105.3 / 101.1 | 5.8 / 18.2 |
| 720p -> 4K RGBA32 | 20.9 / 22.3 | 9.1 / 7.8 | 3.0 / 6.1 | 8.8 / 8.3 |
| 720p -> 4K RGB32 | 18.9 / 19.2 | 8.5 / 8.6 | 3.8 / 4.8 | 6.6 / 5.9 |

- The conversion-only build and the horizontal part do not separate cleanly: their sums differ by 0.8-2.5 ms on the
  4K -> 1080p rows, clang-cl the slower.
- The conversion-only column does not compare the conversion loops. The RGB24 loop is the same 4 shuffles and widenings
  per 4 pixels under both compilers; clang-cl's RGB32 and RGBA32 loops have one fewer than MSVC's.
- Fewer shuffles in MSVC's conversion loops, confirmed in its listings, gained nothing measurable (experiment 15 in the main doc).

The conversion-only build by parts, minimum ms of three rounds, MSVC / clang-cl. Fetch: one byte read per cache line of
the source. Convert: the strip's pixels converted in 128-pixel chunks to one place, less the fetch. Stepping: the build's
loop over dest pixels with `prepareRun`, less the chunked conversion. clang-cl removes the fetch loop, its results unused.

| Scenario | Whole resize | Fetch | Convert | Stepping |
|---|---|---|---|---|
| 4K -> 1080p RGB32 | 24.7 / 28.3 | 1.8 / - | 1.7 / 4.2 with the fetch | 5.0 / 6.3 |
| 4K -> 1080p RGB24 | 25.9 / 28.6 | 1.4 / - | 2.6 / 4.9 | 5.1 / 2.4 |
| 4K -> 1080p RGBA32 | 28.4 / 30.2 | 1.8 / - | 3.6 / 5.5 | 5.3 / 4.0 |
| 4K -> 1080p Grayscale8 | 15.7 / 14.8 | 0.3 / - | 0.4 / 0.8 | 4.0 / 3.5 |
| 24 MP -> 1080p | 55.4 / 63.9 | 5.7 / - | 5.4 / 12.0 | 8.4 / 11.8 |
| 101 MP -> 720p | 181.7 / 204.5 | 26.3 / - | 24.0 / 53.9 | 16.9 / 28.9 |

- The fetch and the conversion together are 14-19% of a 3- or 4-channel 4K downscale under MSVC and 28% of 101 MP -> 720p.
- The stepping figure is 1.2-3 ns per dest pixel for one source logic, by layout and compiler: it measures that loop's
  code, and bounds the stepping's cost inside the filter loop from above only.

## The source buffers' range by value in the horizontal filter

Median of same-round ratios over twenty alternating rounds (sixteen in WSL), against 93b1b79 with the same benchmark
file. The Qt and SSE4.1 rows were switched off in these builds, which takes a run from 65 s to 27 s.

| Scenario | MSVC | clang-cl | GCC (WSL) | Clang (WSL) |
|---|---:|---:|---:|---:|
| 4K -> 1080p RGB32 | -8.4%, -7.9% | -5.2%, -6.4% | -0.4%, 0.0% | -6.5%, -6.8% |
| 4K -> 1080p RGB24 | -6.5% | -7.3% | -1.0% | -7.9% |
| 4K -> 1080p RGBA32 | -5.7% | -5.6% | -2.8% | -8.5% |
| 4K -> 1080p Grayscale8 | -9.2% | -8.9% | -4.6% | -11.5% |
| 24 MP -> 1080p | -5.3% | -6.7% | -2.9% | -5.5% |
| 101 MP -> 720p | -3.4% | -3.4% | -3.9% | -2.7% |
| 720p -> 4K RGBA32 | -2.1% | -7.2% | -4.4% | -8.7% |
| 720p -> 4K RGB32 | -3.4% | -6.2% | -3.1% | -6.6% |
| 720p -> 4K RGB24 | -5.9% | -3.0% | -2.2% | -6.9% |
| 1080p -> 1440p RGB32 | -5.1% | -6.9% | -2.0% | -6.8% |
| 4K -> 64x64 | +0.3% | +1.0% | -3.1% | 0.0% |
| 720p -> 4K Grayscale8 | +0.2% | +0.1% | +0.1% | -0.3% |

- With threads: -2.5% to -6.3% under MSVC, 0% to -7.5% under clang-cl, 0% to -4% under GCC, -3% to -10% under Clang.
- The Windows columns are the committed sources. The WSL columns are the same loop before `filterRun` was split from
  it; under MSVC and clang-cl the two forms measured alike in one run.
- The loop without a check, its batch end found by a scan: MSVC -3% to -6% on the 4K downscales; clang-cl +7.1% on
  4K -> 1080p Grayscale8, +4.4% on 1080p -> 1440p, +2% to +3% on 720p -> 4K RGB32 and RGB24; GCC within 2-4% either way.

### The short-run pass, and the temp pixel's store

Twenty alternating rounds against 1917611, one binary per compiler carrying both changes: they touch disjoint rows.

| Rows | MSVC | clang-cl |
|---|---:|---:|
| 720p -> 4K Grayscale8 (short-run groups from the range by value) | -16.6% | -20.5% |
| RGB24 and RGB32 rows (one 16-byte store per temp pixel, not committed) | -3.3% to +0.4% | -4.1% to +4.3% |
| RGBA32 rows, touched by neither | -0.9% to +1.1% | -0.2% to +0.6% |
| 4K -> 1080p Grayscale8, touched by neither | +2.4% | +3.0% |

### VTune on the committed MSVC build (1917611)

Hardware-sampled hotspots, 2024.3, 4K -> 1080p RGB32, samples attributed to the kernel's loops by address.

| Part of `resizeRows<3, 4>` | Share |
|---|---:|
| Per-column filter loop, row pair | 64.9% |
| Conversion to floats | 17.6% |
| Vertical taps | 11.2% |
| Vertical output | 5.1% |

- The filter loop retires about 3.3 instructions per cycle.
- 12-tap runs take the 8-tap block and the 4-tap block once each: no inner loop iterates.
- VTune 2025.4 and later do not recognize this processor.

### Two chains at AVX2, and 12-byte run table entries (neither committed)

Twenty alternating rounds against 1ed9965.

| Scenario | Two chains, MSVC | Two chains, clang-cl | 12-byte entries, MSVC | 12-byte entries, clang-cl |
|---|---:|---:|---:|---:|
| 4K -> 1080p RGB32, RGB24, RGBA32 | -0.1%, -2.3%, +0.1% | -2.9%, -2.2%, -3.4% | +0.7%, -0.5%, -0.3% | -1.9%, +2.2%, -2.8% |
| 720p -> 4K RGBA32, RGB32, RGB24 | -1.4%, +0.8%, -1.0% | -2.4%, -2.7%, -6.8% | +0.6%, +1.6%, -0.4% | +4.9%, +3.5%, +8.4% |
| 24 MP -> 1080p | +5.0% | -2.3% | +1.5% | -0.2% |
| 101 MP -> 720p | +7.8% | -1.9% | +0.2% | +0.3% |
| 4K -> 64x64 | +12.3% | +5.3% | +0.3% | +1.4% |
| 4K -> 1080p Grayscale8 | -1.3% | -12.1% | -0.1% | -9.7% |

clang-cl's Grayscale8 row gains in both builds, neither of which changes what it runs.

### Where an upscale's time goes (eeb1af6, VTune, MSVC, 720p -> 4K RGBA32)

| Part of `resizeRows<4, 4>` | Share |
|---|---:|
| Per-column filter loop, row pair | 38.0% |
| Conversion to floats | 4.9% |
| Vertical taps | 25.5% |
| Vertical output | 26.9% |
| Vertical step setup | 3.6% |

- The whole run: the kernel 6.5 s, `touchDestPagesInOrder` 0.8 s, ntoskrnl 1.5 s.
- A 3x bicubic upscale's runs alternate 4 taps and 1: at the phase where the kernel's zeros fall on pixels only the centre
  tap is left after trimming.

### Short x runs padded to 4 taps, and their passes

Twenty alternating rounds against eeb1af6. "Flag": the choice of pass carried through the kernel as a `bool`. "Variants":
the committed sources, the kernel compiled once per choice.

| Scenario | MSVC, flag | MSVC, variants | clang-cl, flag | clang-cl, variants |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -10.3% | -7.7% | -11.2% | -8.2% |
| 720p -> 4K RGB32 | -7.0% | -6.9% | -9.9% | -7.5% |
| 720p -> 4K RGB24 | -8.8% | -7.6% | -14.3% | -13.8% |
| 1080p -> 1440p RGB32 | -12.8% | -11.7% | -14.9% | -12.8% |
| 720p -> 4K Grayscale8 | -11.9% | -10.0% | -11.3% | -10.1% |
| 720p -> 4K RGBA32, RGB24, 1080p -> 1440p with threads | -7.8%, -5.1%, -8.0% | -8.8%, -5.6%, -8.4% | -7.7%, -8.5%, -10.4% | -5.7%, -9.8%, -11.2% |
| 4K -> 1080p Grayscale8 | +6.1% | -0.1% | -11.9% | -8.3% |
| 4K -> 1080p RGB32 (two rows) | +3.6%, +3.3% | +2.9%, +0.5% | -1.5%, -1.2% | +0.5%, +1.0% |
| 4K -> 1080p RGBA32 | +2.9% | +0.9% | -1.5% | -0.4% |
| 8K -> 4K RGB32 | +1.7% | +0.8% | -1.0% | -0.5% |
| 24 MP -> 1080p | +2.0% | +0.2% | -0.7% | +0.1% |

- Placement control for the flag form's downscale cost under MSVC: eeb1af6 with 16, 32 and 48 bytes of `__nop()` at each
  kernel's start stays within 1.8% on these rows; the flag form with 32 bytes reads +3.4% on 4K -> 1080p Grayscale8 and
  +3.6% on 8K -> 4K.
- The flag form's general pass in the listing: the RGB32 pair loop 141 -> 142 instructions and 5 -> 6 stack accesses,
  the one-channel one 152 -> 155 and 7 -> 8.
- The flag read from `AxisWeights` in `filterHorizontal`, not passed in: MSVC's 4K -> 1080p RGB32 +3% to +7%.
- Every x run padded to a multiple of 4 taps, with the 4-float pass alone: the thumbnail +3% to +5%.

### Output blocks packed whole, and capped at alpha as bytes

Twenty alternating rounds against e60816d: median of same-round ratios / minimum to minimum.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 720p -> 4K RGBA32 | -12.8% / -14.4% | -13.2% / -13.3% |
| 720p -> 4K RGB32 | -5.0% / -9.8% | -6.1% / -6.9% |
| 720p -> 4K Grayscale8 | -4.7% / -5.7% | -6.7% / -6.8% |
| 720p -> 4K RGB24 | -0.9% / -3.3% | -3.3% / -3.7% |
| 1080p -> 1440p RGB32 | -4.9% / -5.1% | -3.5% / -3.5% |
| 720p -> 4K RGBA32, threads | -6.8% / -7.4% | -6.4% / -6.5% |
| 1080p -> 1440p RGB32, threads | -3.1% / -3.2% | -2.6% / -1.4% |
| 4K -> 1080p RGBA32 | -2.2% / -6.4% | -2.7% / -0.3% |
| Other downscale rows | -1.7% to +3.5% / -5.4% to +1.5% | -3.2% to +0.7% / -6.5% to +2.8% |

The vertical pass of the 720p -> 4K RGBA32 row before the change (VTune, MSVC, per step of 16 pixels):
- The taps: 3 on average, 8 `vfmadd231ps` each, about 45 instructions.
- The output: 56 instructions, 28 of them on port 5 (8 `vshufps`, 8 `vextracti128`, 8 `vpackssdw`, 4 `vpackuswb`).
- Each took about 20 cycles.

### Vertical accumulators started at 0.5 (not committed)

Twenty alternating rounds against 64af30d: median of same-round ratios / minimum to minimum.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 720p -> 4K RGBA32 | -1.3% / -2.3% | -2.2% / -2.9% |
| 720p -> 4K RGB32 | -1.1% / -1.8% | -1.7% / -1.0% |
| 720p -> 4K Grayscale8 | -2.8% / -1.5% | -0.8% / -1.6% |
| 720p -> 4K RGB24 | -1.4% / -2.2% | +1.5% / -0.2% |
| 1080p -> 1440p RGB32 | -0.3% / -0.8% | +0.0% / +0.4% |
| 4K -> 1080p RGB32 | +2.8% / -0.2% | +3.5% / +2.1% |
| 4K -> 1080p RGBA32 | +2.6% / +1.2% | +4.3% / +0.1% |
| 4K -> 1080p RGB24 | +1.9% / +0.4% | +0.7% / -2.1% |
| 4K -> 1080p Grayscale8 | +1.4% / +0.0% | +1.2% / +0.7% |
| Threaded rows | -1.1% to +0.9% | -1.3% to +1.1% |

MSVC's general-pass kernels in the two builds, from the listings:
- The horizontal filter and conversion loops are identical. The vertical tap loops differ in register names only.
- Each kernel is 60-65 bytes shorter and starts elsewhere: several loops sit at another offset in their 64-byte line.
- Jumps on a 32-byte boundary inside the inner loops: the same count in both.

clang-cl's listings were not compared.

### Accumulators updated in place

Twenty alternating rounds against 64af30d. Every row within -1.8% to +2.2% under MSVC and -3.1% to +1.1% under
clang-cl by median of same-round ratios; by minimum, -3.7% to +2.5% and -1.1% to +2.4%.

MSVC's AVX2 kernels in the two builds, from the listings:
- 375 of 376 inner loops have the same instruction count, multiplies and stack accesses. The exception gains one
  instruction: the group loop of the one-channel four-tap kernel for a runtime pixel stride.
- 8 kernels differ in register names only, 3 by one or two instructions of per-strip setup, 3 not at all.
- The two one-channel four-tap kernels address their frame through `rbp` where it was `rsp`.

### The vertical taps: a list, and the first tap assigned

Against 3e0a1ea, median of same-round ratios / minimum to minimum. "List": the nonzero taps listed per dest row, the
first tap assigned. "Assign only": the first tap assigned, the rows walked as before.

Twenty rounds, the list with 32-byte stored weights:

| Scenario | MSVC list | MSVC assign only | clang-cl list | clang-cl assign only |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -3.6% / -3.2% | +0.4% / -0.8% | -4.6% / -1.8% | -3.6% / -4.9% |
| 720p -> 4K RGB32 | -5.0% / -3.5% | +1.5% / -0.3% | -1.6% / -2.2% | -1.4% / -2.0% |
| 720p -> 4K RGB24 | -5.2% / -4.2% | -0.1% / -0.6% | -1.1% / -1.9% | -4.0% / -3.3% |
| 720p -> 4K Grayscale8 | -4.4% / -2.3% | -1.7% / -0.2% | +0.8% / +0.8% | -1.1% / -0.9% |
| 4K -> 1080p RGB32, RGB24, RGBA32 | +1.6%, +0.7%, +0.5% | +2.0%, +1.8%, +1.9% | -1.2%, +0.5%, -0.0% | -1.5%, +0.2%, -1.7% |

The stored weight, ten rounds: `float`, 16 bytes and 32 bytes within noise of one another, upscales -1% to -4% under
MSVC. Six processes each of 720p -> 4K RGBA32 (600 samples), mean ms:

| | Per-process means | Best |
|---|---|---:|
| No list | 18.03 18.16 16.72 16.73 17.99 17.95 | 16.72 |
| `float` | 16.28 16.76 17.86 16.67 16.23 16.17 | 16.17 |
| 16 bytes | 17.69 17.44 17.39 17.39 16.25 16.27 | 16.25 |
| 32 bytes | 16.47 16.36 16.32 16.32 16.29 16.18 | 16.18 |

VTune, MSVC, 720p -> 4K RGBA32, instructions per iteration of the horizontal four-tap loop: the kernel 78.6 without a
list and 63.1 to 63.3 with each of the three; the code after that loop, the vertical pass, 46.0 and 30.5 to 30.7.

The committed form, twenty rounds: "experiment" is the `float` list written inline in `filterVerticalBlocks`.

| Scenario | MSVC experiment | MSVC committed | clang-cl experiment | clang-cl committed |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -3.0% / -2.2% | -2.4% / -2.4% | -9.4% / -5.5% | -3.2% / -3.3% |
| 720p -> 4K RGB32 | -1.3% / -2.6% | -3.1% / -3.6% | -0.1% / +2.9% | -3.6% / -2.3% |
| 720p -> 4K RGB24 | -3.3% / -3.7% | -2.5% / -4.4% | -3.9% / -3.5% | -3.1% / -3.0% |
| 720p -> 4K Grayscale8 | -1.4% / -2.0% | -0.8% / -1.4% | -0.1% / +0.1% | +1.5% / +1.6% |
| 1080p -> 1440p RGB32 | -0.6% / -0.0% | -1.6% / -1.4% | +2.0% / +4.1% | -2.4% / -0.1% |
| 4K -> 1080p RGBA32 | +1.9% / +1.4% | +3.9% / +4.6% | -3.3% / -0.3% | -0.7% / -0.4% |
| 4K -> 1080p Grayscale8 | +0.3% / +0.6% | +1.4% / +0.3% | -1.1% / -1.0% | +6.4% / +6.0% |
| Threaded rows | -1.4% to +1.7% | -2.0% to +1.0% | -1.4% to +3.5% | -1.6% to +1.1% |

The two rows that stand out, six processes each, per-process mean ms:

| | 3e0a1ea | Experiment | Committed |
|---|---|---|---|
| MSVC 4K -> 1080p RGBA32 | 25.3 to 28.5, median 27.78 | 26.9 to 29.6, median 27.43 | 26.9 to 29.3, median 27.18 |
| clang-cl 4K -> 1080p Grayscale8 | 13.18 to 13.74 | 13.32 to 13.62 | 14.15 to 14.54 |

MSVC's tap loops in the committed build equal the experiment's by instruction and multiply count, in every kernel.

### clang-cl's 4K -> 1080p Grayscale8 with the list: jump placement

The committed list read +6% on this row against 3e0a1ea, and the experiment's inline form did not. Per-process mean ms,
six processes of 500 samples each unless noted:

| clang-cl build | Range | Median |
|---|---|---:|
| 3e0a1ea | 13.15 to 13.95 | 13.50 to 13.75 by session |
| e8d2d15 | 14.12 to 14.81 | 14.24 to 14.36 by session |
| e8d2d15, the list's arrays allocated after the source floats | 14.79 to 15.18 | 14.97 |
| e8d2d15, the source floats' addresses printed | 14.68 to 15.18 | 14.72 |
| 3e0a1ea, jumps kept off 32-byte boundaries by the linker | 13.39 to 14.27 | 13.65 |
| e8d2d15, jumps kept off 32-byte boundaries by the linker | 13.16 to 13.93 | 13.49 |

- VTune, two runs of 1200 samples per build: the vertical pass 1.4 s at 3e0a1ea and 1.5 to 2.0 s at e8d2d15; conversion
  and the horizontal pass 14.1 to 14.3 s and 15.5 s, with fewer instructions retired at e8d2d15. The per-column loop of
  the 12-tap row pair is the same instructions in both builds, and takes 23.8 s and 26.8 s over the two runs.
- Jumps of that loop that cross or end on a 32-byte boundary: two at 3e0a1ea and in the experiment's build, on paths
  taken rarely; three at e8d2d15, two of them taken per output column.
- Not the buffers' placement: the source floats put at 13 offsets within a page, and the second row's buffer at 6
  distances from the first, gave 14.6 to 15.2 ms at each, three processes apiece. The addresses a process got by
  itself do not follow its time.
- Each change to the kernel source moved every kernel's code: the build with the arrays allocated four lines later
  differs from e8d2d15 in 127 to 2379 instructions per kernel.
- `/clang:-mbranches-within-32B-boundaries` on the compile changes nothing in a build with LTO: the executable's code
  is byte-identical. The linker takes it as `/mllvm:-x86-branches-within-32B-boundaries`.

The AVX2 kernel file through its own rule, padded and without LTO, against e8d2d15 as built before: eight rounds,
median of same-round ratios / minimum to minimum. "Linker" is e8d2d15 with LTO and the linker's option.

| Scenario | Linker | Own rule |
|---|---:|---:|
| 4K -> 1080p Grayscale8 | -7.6% / -6.7% | -9.0% / -8.1% |
| 4K -> 1080p RGBA32, RGB32, RGB24 | +0.6%, +0.6%, -0.1% / -2.4%, -0.7%, -5.4% | -3.2%, -2.4%, -0.5% / +0.9%, +2.1%, -2.3% |
| 720p -> 4K RGBA32, RGB32, RGB24, Grayscale8 | +0.4%, +0.8%, -1.3%, -0.8% / -0.9%, +0.1%, -0.8%, -2.2% | -3.6%, -0.9%, -2.4%, -3.8% / -2.2%, -0.9%, -0.6%, -1.8% |
| 1080p -> 1440p RGB32 | -1.5% / +1.9% | -3.4% / -0.8% |
| 24 MP -> 1080p, 1080p -> 240p, 4K -> 64x64, 101 MP -> 720p, 8K -> 4K | -2.4% to +0.3% / -1.7% to +1.4% | -2.5% to +0.3% / -2.0% to +0.2% |
| Threaded rows | -4.9% to +6.5% / -1.8% to +3.1% | -4.7% to -0.2% / -5.2% to +2.6% |

### The vertical taps' list at SSE4.1 on this machine

e8d2d15 against 3e0a1ea, the SSE4.1 rows, six rounds: median of same-round ratios / minimum to minimum.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 720p -> 4K RGBA32, RGB32 | -9.6%, -9.2% / -8.1%, -6.8% | -6.5%, -5.6% / -3.1%, -4.7% |
| 720p -> 4K Grayscale8, RGB24 | -11.8%, -11.1% / -10.5%, -10.9% | -4.2%, -1.0% / -5.9%, -2.1% |
| 1080p -> 1440p RGB32 | -8.1% / -5.6% | +0.9% / +0.3% |
| 4K -> 1080p Grayscale8 | +5.7% / +5.3% | +1.7% / +0.2% |
| 4K -> 1080p RGBA32, RGB32, RGB24 | +2.2%, +0.8%, -3.9% / +1.7%, +0.9%, -3.1% | +1.0%, +2.7%, -5.0% / +0.9%, +2.5%, -5.2% |
| Other downscales | -0.4% to +1.7% | -0.5% to +2.6% |

MSVC's 4K -> 1080p Grayscale8 alone, six processes of 300 samples: 17.89 to 18.07 ms at 3e0a1ea, 19.10 to 19.32 at
e8d2d15. clang-cl's: 15.87 to 16.40 and 15.83 to 16.21.

### The weight spread's indices hidden from Clang

4K -> 1080p RGB32, VTune, 600 samples, one process per build: the RGB32 kernel 14.4 s under MSVC, 15.8 s under clang-cl
and 14.5 s with the indices hidden; its per-column loop 8.9, 9.9 and 9.2 s; instructions retired 136.6, 148.8 and 142.5 G.

Eight alternating rounds without the controls: median of same-round ratios / minimum to minimum.

| Scenario | Hidden against plain clang-cl | Plain clang-cl against MSVC | Hidden against MSVC |
|---|---:|---:|---:|
| 101 MP -> 720p RGB32 | -8.4% / -8.6% | +10.0% / +9.8% | +0.1% / +0.4% |
| 4K -> 64x64 RGB32 | -7.6% / -11.0% | +1.8% / +3.1% | -4.0% / -8.2% |
| 1080p -> 240p RGB32 | -6.7% / -7.2% | +11.0% / +11.6% | +3.1% / +3.6% |
| 24 MP -> 1080p RGB32 | -4.4% / -5.7% | +6.9% / +10.4% | +2.3% / +4.1% |
| 4K -> 1080p RGB32, the display section's | -5.4% / -4.5% | +4.7% / +7.8% | -0.2% / +2.9% |
| 4K -> 1080p RGB32, the layouts section's | +0.1% / -2.1% | +4.8% / +7.5% | +6.5% / +5.3% |
| 4K -> 1080p RGB24 | -0.7% / -4.9% | +3.3% / +9.5% | +5.0% / +4.2% |
| 4K -> 1080p RGBA32 | +2.3% / -0.8% | -2.1% / +4.2% | +2.0% / +3.3% |
| 720p -> 4K RGBA32, RGB32, RGB24 | +0.9%, -3.1%, -0.2% / -0.1%, -2.8%, -1.3% | -1.1%, -2.5%, -5.0% / +0.3%, -3.0%, -1.0% | -0.6%, -8.5%, -4.9% / +0.2%, -5.8%, -2.3% |
| 1080p -> 1440p RGB32 | -1.5% / -1.1% | -0.7% / +0.1% | -1.5% / -1.1% |
| 4K -> 1080p and 720p -> 4K Grayscale8 | +0.8%, -0.4% / +0.9%, -0.5% | -6.5%, -5.9% / -6.7%, -7.5% | -6.0%, -7.8% / -5.9%, -7.9% |
| Threaded rows | -8.0% to -0.5% / -6.2% to -1.7% | -1.6% to +10.9% | -9.6% to +4.8% |

- The indices read from a writable global, in place of the assembly statement: about half the gain on most rows.
- The two sections' 4K -> 1080p RGB32 are one job.
- MSVC's AVX2 kernels with `spreadIndices`: the same instructions, 25 of 24685 differing in the order of a commutative
  operation's operands or in a register's name.

### Rounds on this machine

- One binary's 4K -> 1080p rows range 13-19% over twenty rounds, with an interquartile range of 6-8%; 24 MP and 101 MP
  range 5%. Qt's rows range as much: the spread is the process's, not the kernel's.
- Six rounds of ten samples gave -0.6% and -6.1% for one pair of binaries on 4K -> 1080p RGB24, an hour apart.
- 8K -> 4K, four times the pixels of 4K -> 1080p, ranges 10-21%: a longer job does not average it out.
