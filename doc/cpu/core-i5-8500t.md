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

### Rounds on this machine

- One binary's 4K -> 1080p rows range 13-19% over twenty rounds, with an interquartile range of 6-8%; 24 MP and 101 MP
  range 5%. Qt's rows range as much: the spread is the process's, not the kernel's.
- Six rounds of ten samples gave -0.6% and -6.1% for one pair of binaries on 4K -> 1080p RGB24, an hour apart.
- 8K -> 4K, four times the pixels of 4K -> 1080p, ranges 10-21%: a longer job does not average it out.
