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
- A full benchmark run takes 89 s, a build 25-30 s.
- A ring near the L2's size varies by run: see the ring budget section.

## Standing against Qt, 4f25dd1

Minimum ms of three rounds, MSVC. clang-cl against MSVC: the sections "clang-cl 22 against MSVC" below.

| Scenario | MSVC | QImage | Ratio | Threads | Ratio | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 45.1 | 37.1 | 1.22 | 13.1 | 0.35 | 3.45x |
| 4K -> 1080p RGB32 | 18.0 | 15.0 | 1.20 | - | - | - |
| 4K -> 1080p RGBA32 | 21.3 | 28.6 | 0.74 | 6.48 | 0.23 | 3.28x |
| 4K -> 1080p RGB24 | 18.5 | 31.8 | 0.58 | 5.53 | 0.17 | 3.34x |
| 4K -> 1080p Grayscale8 | 9.04 | 35.6 | 0.25 | - | - | - |
| 720p -> 4K RGBA32 | 13.9 | 78.3 | 0.18 | 6.38 | 0.08 | 2.18x |
| 720p -> 4K RGB32 | 12.6 | 45.1 | 0.28 | - | - | - |
| 720p -> 4K RGB24 | 11.3 | 36.8 | 0.31 | 4.84 | 0.13 | 2.34x |
| 720p -> 4K Grayscale8 | 4.53 | 26.5 | 0.17 | - | - | - |
| 1080p -> 240p | 3.60 | 2.44 | 1.48 | 1.18 | 0.49 | 3.05x |
| 4K -> 64x64 | 9.93 | 6.54 | 1.52 | 3.61 | 0.55 | 2.75x |
| 101 MP -> 720p | 155.2 | 94.6 | 1.64 | 45.7 | 0.48 | 3.40x |
| 1080p, native size | 1.83 | 1.82 | 1.01 | - | - | - |

- Opaque 4-byte downscales stay 1.2-1.6x behind Qt single-threaded; every other row beats it, and every threaded row.
- The work runs in at most 4 bands on 6 cores.
- Against a6c69e7: 24 MP -> 1080p 57.7 -> 45.1 ms, 4K -> 1080p RGB32 27.1 -> 18.0, 101 MP -> 720p 184.6 -> 155.2,
  720p -> 4K RGB32 20.7 -> 12.6.

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

## The conversion's block loop with stepped pointers (against a8dd837)

MSVC, eight alternating rounds pinned to core 2, single-threaded rows: median of same-round ratios / minimum to minimum.

| Scenario | a8dd837, min ms | Stepped pointers |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 52.84 | -2.9% / -2.9% |
| 4K image -> 1080p RGB32 | 24.43 | -4.1% / -6.1% |
| 4K -> 1080p RGB32 | 23.98 | -3.4% / -3.5% |
| 1080p -> 240p RGB32 | 4.19 | -2.1% / -1.9% |
| 101 MP -> 720p RGB32 | 177.30 | -0.9% / -1.1% |
| 4K -> 64x64 RGB32 | 11.69 | -0.3% / -0.4% |
| 4K -> 1080p RGBA32 | 25.72 | +1.0% / 0.0% |
| 4K -> 1080p RGB24 | 23.06 | 0.0% / -0.3% |
| 4K -> 1080p Grayscale8 | 14.09 | +0.7% / +0.4% |
| 1080p at native size RGB32 | 1.80 | +1.9% / +3.2% |
| The five upscales | | -1.3% to +0.5% |

## `filterRun`'s last taps as explicit steps (against cbbd718, not committed)

MSVC, eight alternating rounds pinned to core 2, single-threaded rows: median of same-round ratios / minimum to minimum.

| Scenario | cbbd718, min ms | Explicit steps |
|---|---:|---:|
| 4K image -> 1080p RGB32 | 23.13 | -3.6% / -1.0% |
| 1080p -> 240p RGB32 | 4.11 | -3.6% / -3.0% |
| 1080p at native size RGB32 | 1.83 | -3.0% / -0.4% |
| 101 MP -> 720p RGB32 | 175.49 | -1.6% / -1.6% |
| 4K -> 1080p RGB24 | 22.99 | +6.0% / 0.0% |
| 4K -> 1080p RGB32 | 23.22 | +5.7% / -1.2% |
| 4K -> 1080p RGBA32 | 25.75 | +3.4% / -0.4% |
| The other eight rows | | within 1% |

- The three 4K -> 1080p rows have two levels in both builds, 23.3 and 24.7 ms for RGB32. Round 1 is on the other
  level from rounds 2 to 8 in both: cbbd718 26.36 then 23.22-23.46, the steps 22.95 then 24.47-25.03.
- "4K image -> 1080p RGB32" is the same resize in another test case of the same process: the steps are faster there in
  seven rounds of eight.

4K -> 1080p RGB32 under VTune, hardware-sampled hotspots, three launches each: 23.86-24.02 ms and 25.34-25.47 ms.

| | cbbd718 | Explicit steps |
|---|---:|---:|
| Kernel | 8.96-9.02 s, 93.0-93.1 G instructions | 9.36-9.45 s, 90.4-90.6 G |
| Horizontal column loop | 5.81-5.88 s, 70.5-70.8 G | 6.32-6.35 s, 68.5-69.1 G |
| Conversion loops, vertical pass | 1.40-1.42 s, 1.41-1.44 s | 1.30-1.34 s, 1.40-1.44 s |

The column loop retires 3% fewer instructions and takes 9% longer: the level of these launches, not the steps.

The same binary on both levels, microarchitecture exploration (VTune 2024.3: it ran here without a crash), the
explicit-steps build, 400 samples, over the column loop:

| | Fast, two launches | Slow |
|---|---:|---:|
| The resize, ms | 23.87, 23.96 | 25.63 |
| Cycles | 20.8 G, 20.9 G | 23.1 G |
| Instructions retired | 71.6 G, 72.3 G | 72.9 G |
| Cycles with 1 or 2 ports busy | 2.7 G, 2.7 G | 5.0 G |
| Split loads at the 8-tap block's weight load | 0 | 984 M |
| Split loads at the 4-tap step's weight load | 851 M, 910 M | 67 M |
| Loads blocked by a 4K alias | 775 M, 790 M | 756 M |
| Mispredicted branches, machine clears, loads blocked on a store | 0 | 0 |
| Uops not delivered by the front end | 1.6 G | 1.5 G |

- About one weight load per column spans two cache lines on either level; on the slow one it is the load at the head
  of the column's chain.
- Unprofiled afterwards, both builds fast: 40 launches of 23.2-23.7 ms, memory allocated and freed between half of
  them; 16 fresh copies of the two binaries, each at its own load address, three launches each, 23.2-23.6 ms.
- About one load per column is blocked by a 4K alias on both levels: not followed.

## clang-cl 22 against MSVC at 3d1717b

Eight alternating rounds, clang-cl's time against MSVC's, median of the rounds' ratios. The thumbnail rows, 4K -> 64x64
and the display rows from one session, the rest from another.

| Scenario | clang-cl / MSVC |
|---|---:|
| 24 MP photo -> 1080p viewport | +0.5% |
| 4K image -> 1080p RGB32 | +1.2% |
| 720p image -> 4K RGBA32, RGB32 | +0.5%, -0.2% |
| 1080p -> 1440p | -1.6% |
| 1080p -> 240p thumbnail | +0.7% |
| 1080p -> 120 px wide thumbnail (16x) | -11.6% |
| 21 MP photo -> 175 px wide thumbnail (32x) | -11.6% |
| 4K -> 64x64 | -12.1% |
| 101 MP -> 720p | -4% |
| 4K -> 1080p Grayscale8, RGB24, RGB32, RGBA32 | -0.7%, +2.9%, +0.4%, -1.6% |
| 720p -> 4K Grayscale8, RGB24 | -9%, -1.7% |

- MSVC with the block loop's pointers stepped by hand reaches clang-cl on the three 16x-60x rows (-11% to -14%)
  and loses 4-7% on 2x to 4.5x (`resize-performance.md`, experiment 27).
- The block loop stepped where a level has four chains (its design entry) closes the gap on those three rows with
  no row lost: MSVC's 32x thumbnail 26.5 ms against clang-cl's 26.4.
- On the 12600K the two compilers are within 3% on those rows, on both core types.

## clang-cl 22 against MSVC at 3c33ab0

12 alternating rounds pinned to core 2: minimum ms, and clang-cl's time against MSVC's as median of same-round ratios /
minimum to minimum. Both built on the 12600K; MSVC's binary is 560ca96's: 3c33ab0's barrier is empty under MSVC.

| Scenario | MSVC | clang-cl | clang-cl / MSVC |
|---|---:|---:|---:|
| 24 MP photo -> 1080p viewport | 46.13 | 44.38 | -3.9% / -3.8% |
| 2.8 MP photo -> 1920x1280 viewport | 9.07 | 9.25 | +2.4% / +2.0% |
| 4K image -> 1600x900 | 16.49 | 16.41 | -0.6% / -0.5% |
| 4K image -> 720p | 14.44 | 14.50 | -0.1% / +0.4% |
| 720p image -> 4K RGBA32 | 13.95 | 13.88 | -0.5% / -0.5% |
| 720p image -> 4K RGB32 | 12.63 | 12.90 | +2.3% / +2.1% |
| 1080p -> 240p thumbnail | 3.63 | 3.58 | -1.5% / -1.4% |
| 1080p -> 120 px wide thumbnail (16x) | 2.61 | 2.49 | -4.3% / -4.7% |
| 21 MP photo -> 175 px wide thumbnail (32x) | 25.86 | 25.06 | -3.1% / -3.1% |
| 1080p image at native size | 1.80 | 1.82 | -1.2% / +0.9% |
| 4K -> 1080p Grayscale8 | 9.07 | 9.15 | +0.9% / +0.9% |
| 4K -> 1080p RGB24 | 18.37 | 18.37 | +0.5% / -0.0% |
| 4K -> 1080p RGB32 | 18.16 | 18.20 | -0.1% / +0.2% |
| 4K -> 1080p RGBA32 | 21.49 | 21.17 | -1.9% / -1.5% |
| 720p -> 4K Grayscale8 | 4.50 | 4.60 | +1.1% / +2.3% |
| 720p -> 4K RGB24 | 11.37 | 11.39 | +0.4% / +0.2% |
| 4K -> 64x64 | 10.22 | 9.89 | -3.3% / -3.2% |

- clang-cl faster by 3-5% on the 16x-60x rows and 24 MP -> 1080p; MSVC by about 2% on 2.8 MP -> 1920x1280 and the
  RGB32 and Grayscale8 upscales; the rest within 2%.
- The 12600K, both core types: its log's section of this name.

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

### The conversion's tail loop kept scalar under Clang

clang-cl, against 17d2fa0: median of same-round ratios / minimum to minimum. AVX2: eight rounds without the controls.
SSE4.1: six rounds with them.

| Scenario | AVX2 | SSE4.1 |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | -2.2% / -5.8% | -0.6% / -0.1% |
| 4K -> 1080p RGB32, the display section's | -2.9% / -0.2% | -0.6% / -0.1% |
| 1080p -> 240p RGB32 | -2.5% / -1.4% | -0.4% / -0.6% |
| 4K -> 64x64, 101 MP -> 720p | -2.6%, -1.4% / -0.3%, -1.5% | -0.2%, 0.0% / -0.5%, +0.1% |
| 4K -> 1080p RGB32, RGB24, RGBA32 | +1.5%, -2.2%, 0.0% / +0.3%, -0.3%, +2.5% | -0.8%, -3.9%, -0.3% / -0.4%, -3.8%, -0.7% |
| 4K -> 1080p Grayscale8 | +0.7% / +0.4%; eight processes of 300 samples -2.7% / -1.3% | -1.3% / -1.9% |
| 720p -> 4K Grayscale8 | +2.8% / +3.0%; eight processes of 300 samples +2.5% / +3.5% | -2.1% / -2.5% |
| 720p -> 4K RGBA32, RGB32, RGB24 | -0.2%, -1.5%, -2.0% / -3.3%, -3.9%, -0.6% | +0.3%, +2.5%, +0.1% / -0.4%, -0.5%, +1.3% |
| 1080p -> 1440p RGB32 | -0.4% / -2.3% | +0.9% / +1.7% |
| Threaded rows | -2.5% to +3.0% / -4.6% to +1.3% | -4.5% to +0.8% / -3.9% to +1.4% |

- Against MSVC in the AVX2 session: 24 MP -> 1080p -0.3% / +0.5%, 4K -> 1080p RGB32 +0.3% / -1.5% and -1.2% / +1.2%,
  1080p -> 240p -0.8% / +0.2%, 101 MP -> 720p -0.2% / -0.2%, 4K -> 64x64 -8.3% / -8.6%.
- The RGB32 kernel, 4K -> 1080p, VTune: constant loads executed 11 -> 5, instructions retired 142.5 -> 140.8 G.
- Kernel sizes: the RGB32 one 2123 -> 1884 instructions, the one-channel four-tap one 3485 -> 2441.
- 720p -> 4K Grayscale8, VTune, 2000 samples: the horizontal four-column loop 55 instructions with 5 stack accesses and
  3.92 s before, 61 with 7 and 4.32 s after; the kernel 8.85 and 9.06 s.
- MSVC's AVX2 kernels: identical. Its rows in the same rounds, one build against the other: within 2% by minimum.

### MSVC against clang-cl at SSE4.1, and the SSE4.1 kernels padded

b1395f9. Six alternating rounds with the controls, the SSE4.1 rows, all against the plain MSVC build: median of
same-round ratios / minimum to minimum. "Padded": jumps kept off 32-byte boundaries in the SSE4.1 kernels too, MSVC
by `/QIntel-jcc-erratum` on every source, clang-cl by the linker's `/mllvm:-x86-branches-within-32B-boundaries`.

| Scenario | MSVC, ms | MSVC padded | clang-cl | clang-cl padded |
|---|---:|---:|---:|---:|
| 4K -> 1080p RGB24 | 37.75 | -1.6% / -1.9% | +10.0% / +8.8% | +5.7% / +5.2% |
| 4K -> 1080p RGB32 | 36.97 | -2.5% / -2.8% | +3.4% / +4.0% | +3.2% / +3.9% |
| 4K -> 1080p RGB32, the display section's | 36.88 | -2.8% / -2.6% | +3.8% / +3.4% | +4.0% / +4.0% |
| 4K -> 1080p RGBA32 | 43.58 | -4.5% / -4.7% | -2.1% / -2.0% | -1.8% / -2.7% |
| 4K -> 1080p Grayscale8 | 20.21 | -14.2% / -15.3% | -19.4% / -21.0% | -20.7% / -20.7% |
| 24 MP -> 1080p, 101 MP -> 720p | 87.40, 326.18 | -0.6%, -0.8% / +0.1%, -0.9% | +1.3%, +1.6% / +1.3%, +1.8% | +0.8%, +0.5% / +1.3%, +0.7% |
| 1080p -> 240p, 4K -> 64x64 | 7.17, 23.67 | -0.3%, -1.1% / -0.2%, -1.2% | +1.1%, +0.6% / +1.4%, +0.1% | +1.4%, -0.4% / +1.6%, -0.4% |
| 720p -> 4K RGBA32, RGB32, RGB24 | 20.95, 19.53, 18.03 | +2.3%, +4.5%, -1.3% / +1.0%, -0.2%, -1.4% | +5.9%, +2.4%, +2.0% / +3.1%, -1.6%, +2.3% | +5.0%, +1.3%, -0.6% / +3.4%, -1.7%, +0.1% |
| 720p -> 4K Grayscale8 | 6.35 | -1.0% / -1.0% | -6.3% / -6.6% | -7.1% / -6.5% |
| 1080p -> 1440p RGB32 | 13.79 | -0.5% / -0.9% | -2.6% / -2.6% | -2.1% / -1.9% |

The padded builds' RGB32 kernel on 4K -> 1080p, VTune, 300 samples, two runs each, MSVC / clang-cl:

| | Seconds | Instructions retired |
|---|---|---|
| Kernel | 10.68, 10.73 / 11.39, 11.24 | 137.7 G / 141.4 G |
| Per-column loop | 6.88, 6.91 / 7.26, 7.35 | 97 G / 100 G |
| Its tap loop: 39 and 38 instructions, the same operations | 3.52, 3.62 / 3.46, 3.53 | 48.8 G / 47.7 G |
| Vertical step | 2.21, 2.17 / 2.24, 2.09 | 25-26 G / 24.8 G |

- clang-cl's deficit is the per-column code around the tap loop, 0.45 s, and the conversion, 0.2 s.
- Per column, outside the tap loop: clang-cl loads 9 values from the stack and copies two accumulators and two
  general registers; MSVC loads 5.
- The unpadded RGB24 kernels: 19 of 107 conditional jumps on a boundary under clang-cl, 23 of 87 under MSVC.

### Rounds on this machine

- One binary's 4K -> 1080p rows range 13-19% over twenty rounds, with an interquartile range of 6-8%; 24 MP and 101 MP
  range 5%. Qt's rows range as much: the spread is the process's, not the kernel's.
- Six rounds of ten samples gave -0.6% and -6.1% for one pair of binaries on 4K -> 1080p RGB24, an hour apart.
- 8K -> 4K, four times the pixels of 4K -> 1080p, ranges 10-21%: a longer job does not average it out.

### The process states on 720p -> 4K, and where the float buffers start

An experiment at 4edb9e5, MSVC, not committed: one binary that places the temp ring's and the source span's starts by
the executable's name. 720p -> 4K RGB32 without a pool, per-process mean ms of 20 samples unless noted.

Placement:

- The allocations' own starts vary by process: the ring and the span each at 0, 16, 32 or 48 bytes modulo 64 over 12
  processes. They stay put within a process that runs this row alone; a full benchmark run sees all four.
- Both on a 64-byte boundary: 14.3-14.6. Both 16 bytes past one: 15.3-15.5, +6.5%.
- The ring on a 32-byte boundary, the span anywhere: 14.3-14.4. On this row the ring's start decides.

The whole benchmark, 15 rounds, against both starts on a 64-byte boundary. A cell is the median of same-round ratios /
minimum to minimum:

| Scenario | On a 64-byte boundary, ms | Both 16 bytes past one | The allocations' own starts |
|---|---:|---:|---:|
| 24 MP -> 1080p RGB32 | 51.01 | +3.4% / +4.8% | +2.5% / +0.1% |
| 4K -> 1080p RGB32, its two rows | 22.89, 22.88 | +9.2% / +10.9%, +11.2% / +11.4% | +5.8% / +0.6%, +7.3% / +3.1% |
| 4K -> 1080p RGB24 | 23.09 | +11.9% / +12.0% | +6.1% / +4.7% |
| 4K -> 1080p RGBA32 | 25.40 | +13.6% / +12.4% | +7.2% / +5.2% |
| 4K -> 1080p Grayscale8 | 14.00 | +3.4% / +3.4% | +1.9% / -0.1% |
| 720p -> 4K RGBA32 | 14.65 | +7.7% / +9.6% | +5.0% / +0.8% |
| 720p -> 4K RGB32 | 14.29 | +5.8% / +6.6% | +0.4% / +0.2% |
| 720p -> 4K RGB24 | 13.21 | +6.6% / +7.1% | +1.4% / -0.2% |
| 720p -> 4K Grayscale8 | 5.09 | +8.0% / +7.9% | +1.7% / -0.3% |
| 1080p -> 1440p RGB32 | 10.09 | +5.5% / +5.9% | +4.3% / +0.1% |
| 1080p -> 240p RGB32 | 4.11 | +4.4% / +4.5% | +1.8% / +0.2% |
| 4K -> 64x64 RGB32 | 11.67 | +4.2% / +4.3% | +1.6% / +0.2% |
| 101 MP -> 720p RGB32 | 174.72 | +2.9% / +2.8% | +0.8% / +0.4% |
| Native size, the control: no filter runs | 1.82 | -1.4% / -1.7% | +2.2% / -1.4% |
| 4K -> 1080p RGBA32, RGB24, with a pool | 7.70, 6.90 | +9.7% / +10.3%, +7.7% / +9.2% | +7.5% / +6.5%, +6.6% / +6.4% |

- The allocations' own starts land on a boundary in some rounds: their minimum matches the 64-byte boundary's on most
  rows, their median does not.

4K -> 1080p RGBA32 alone, both starts 16 bytes past a 64-byte boundary against both on it: ten pinned processes of 20
samples each way, alternating, median / minimum of the per-process means, ms. The N4100 gains 5-6% from the 16 bytes
on this row at SSE4.1 (its log).

| | On the boundary | 16 bytes past | |
|---|---:|---:|---:|
| AVX2 | 25.92 / 25.44 | 29.08 / 28.58 | +12.2% / +12.3% |
| SSE4.1 | 42.79 / 42.66 | 42.75 / 42.56 | -0.1% / -0.2% |
| QImage, the control | 28.93 / 28.58 | 29.42 / 28.74 | +1.7% / +0.6% |

A second state that placement does not explain: 9 of 24 processes with both buffers on a 64-byte boundary gave 15.2-16.8.

| Tested against it | Result |
|---|---|
| The ring's and the span's page offsets | No pattern over those 24 processes |
| The source's and the destination's page offsets | The same in every process |
| The core: six processes pinned to each | Both states on every core |
| The clock: a 2-cycle dependent loop before and after each of 16 processes | 3.47-3.49 GHz every time |
| 15 s of idle before a process, HWiNFO64 polling | No faster than back to back |
| Other processes | 3.0 s of System and 1.9 s of pwsh over 121 s |

- 40 pinned processes of 3 samples: 18 at 14.2-14.6, 10 at 15.6-15.7, 12 at 17.5-25.5, the first 7 among them.
- Pinned processes of 60 samples: 15.0-15.5 each, between the two levels. Not explained.
- The first six processes after a sustained load, one per core: 18.3-20.4.
- With HWiNFO64 polling, 18 processes of 60 samples gave 15.3-17.2. Its plot of the clock: 3.3-3.4 GHz.

The second state's cause is not found.

## The next chunk's source prefetched

Against 44dd1ce, ten alternating rounds, median of same-round ratios / minimum to minimum. "Padded": a placement
control, 44dd1ce with 32 bytes of `__nop()` at the start of `resizeRowsWith`. AVX2 binaries built here, the SSE4.1 ones
on the 12600K. The prefetch measured here covered one-channel rows too.

| Scenario | 44dd1ce AVX2, MSVC, min ms | AVX2, MSVC | AVX2, MSVC, padded | AVX2, clang-cl | SSE4.1, MSVC | SSE4.1, MSVC, padded |
|---|---:|---:|---:|---:|---:|---:|
| 4K -> 1080p Grayscale8 | 9.10 | -1.3% / +0.4% | -2.5% / -0.4% | +1.0% / +0.6% | -0.1% / +0.2% | -0.3% / -0.3% |
| 4K -> 1080p RGB24 | 18.41 | -0.6% / -0.7% | +0.0% / -0.3% | -0.5% / -0.7% | -0.1% / -0.3% | +0.3% / +0.2% |
| 4K -> 1080p RGB32 | 18.13 | -1.6% / -0.8% | -0.1% / +0.1% | +0.3% / +0.3% | -0.5% / -0.8% | +0.1% / -0.1% |
| 4K -> 1080p RGBA32 | 21.44 | -0.5% / -0.5% | +0.4% / +0.4% | -1.4% / -1.6% | -0.2% / -0.2% | +0.3% / +0.1% |
| 720p -> 4K Grayscale8 | 4.60 | +0.2% / +0.2% | -0.1% / +0.5% | +0.3% / +0.0% | +0.4% / +0.9% | -0.1% / -0.1% |
| 720p -> 4K RGB24 | 11.42 | -4.1% / +0.2% | +1.8% / -0.0% | +0.4% / +0.2% | -0.0% / +0.1% | +0.2% / +0.1% |
| 960x540 -> 1920x1080 RGB32 | 3.80 | +0.4% / +0.8% | +0.7% / +0.6% | -0.8% / +0.4% | +0.0% / +0.0% | +0.1% / -0.0% |
| 1280x720 -> 1920x810 RGB32 | 3.76 | +0.2% / +0.2% | +0.4% / -0.1% | -0.3% / -0.1% | -0.3% / -0.3% | -0.4% / -0.3% |
| 1280x720 -> 1920x1080 RGB32 | 4.37 | +0.4% / +0.2% | +0.4% / +0.5% | -0.3% / -0.1% | -0.3% / -0.2% | -0.0% / +0.1% |
| 1280x720 -> 1920x1080 RGBA32 | 5.03 | +0.4% / -0.0% | +0.3% / -0.3% | -0.2% / +0.7% | +0.2% / -0.0% | +0.3% / -0.3% |
| 1280x720 -> 1920x1440 RGB32 | 5.25 | -3.4% / -0.0% | +1.2% / -0.2% | -0.1% / -0.2% | -0.2% / -0.2% | +0.1% / +0.3% |
| 1080p -> 1440p RGB32 | 8.43 | +1.7% / +0.3% | +0.6% / +0.6% | -0.8% / -0.4% | -0.4% / -1.2% | -0.5% / -0.2% |

- An earlier MSVC session at AVX2, without the control: 960x540 -> 1920x1080 +10.0% / +0.7%, 1280x720 -> 1920x1440
  +9.3% / +0.3%, the prefetch build in a slower process state in most rounds. In the session above 44dd1ce and the
  padded build land in those states as often.

## 4-tap runs of 4-float pixels in one chain per row, at SSE4.1 (not committed)

The NEON kernel's one chain per row enabled at SSE4.1 as well, against the working tree's NEON-only form, which compiles
the x64 kernels as 901d72e does. Standalone drivers built with MSVC on the 12600K, the kernels' per-file flags; fifteen
alternating rounds of 30 resizes each. Median of same-round ratios / minimum to minimum. The AVX2 rows run the same code
in both builds: the control.

| Scenario | Two chains, min ms | One chain |
|---|---:|---:|
| 960x540 -> 1920x1080 RGB32 | 5.50 | -1.2% / -1.5% |
| 1280x720 -> 1920x810 RGB32 | 5.34 | -1.1% / -0.9% |
| 1280x720 -> 1920x1080 RGB32 | 6.22 | -1.0% / -1.1% |
| 1280x720 -> 1920x1440 RGB32 | 7.40 | -1.6% / -1.1% |
| 1080p -> 1440p RGB32 | 11.88 | -0.8% / -0.8% |
| 720p -> 4K RGB32 | 17.27 | -1.1% / -1.1% |
| 1280x720 -> 1920x1080 RGBA32 | 8.16 | +0.5% / +0.6% |
| 1080p -> 1440p RGBA32 | 15.58 | +0.5% / +0.4% |
| 1280x720 -> 1920x1080 RGB32, AVX2 | 4.32 | -1.4% / -0.5% |
| 1080p -> 1440p RGB32, AVX2 | 8.35 | +0.8% / -0.2% |

- Within the control's spread. At SSE4.1 a multiply-add is a multiply and an add, so one chain is the same 7
  instructions per row as two: only the sums' order changes, a tree of depth 3 to a chain of 4.
- A first session of five rounds: medians up to 6% either way, the control's too; its minimums agree with these.

## x runs three at a time, at SSE4.1 (not committed)

NEON's run triples enabled at SSE4.1 as well, against the working tree with them at NEON only, which compiles the x64
kernels as 9bae4c1 does. Standalone drivers built with MSVC on the 12600K, the kernels' per-file flags; ten alternating
rounds of 30 resizes each. Median of same-round ratios / minimum to minimum. The AVX2 rows run the same code in both
builds: the control.

| Scenario | Runs one at a time, min ms | Triples |
|---|---:|---:|
| 960x540 -> 1920x1080 RGB32 | 5.49 | +1.6% / +1.5% |
| 1280x720 -> 1920x810 RGB32 | 5.35 | +4.5% / +4.1% |
| 1280x720 -> 1920x1080 RGB32 | 6.22 | +4.1% / +4.0% |
| 1280x720 -> 1920x1440 RGB32 | 7.44 | +1.6% / +2.6% |
| 1080p -> 1440p RGB32 | 11.90 | +2.4% / +2.4% |
| 720p -> 4K RGB32 | 17.29 | +0.2% / +0.3% |
| 1280x720 -> 1920x1080 RGBA32 | 8.16 | -7.6% / -5.8% |
| 1080p -> 1440p RGBA32 | 15.56 | -5.6% / -5.5% |
| 1280x720 -> 1920x1080 RGB32, AVX2 | 4.33 | +0.8% / +0.2% |
| 1080p -> 1440p RGB32, AVX2 | 8.35 | +0.2% / +0.2% |

- MSVC's listing of the pattern bodies keeps part of a triple's window and broadcast weights on the stack: three
  columns' 12 weights and up to 6 pixels outgrow the 16 registers, every SSE multiply needing a copy of its operand.

clang-cl, the same builds' sources, ten alternating rounds:

| Scenario | Runs one at a time, min ms | Triples |
|---|---:|---:|
| 960x540 -> 1920x1080 RGB32 | 5.33 | +6.1% / +3.0% |
| 1280x720 -> 1920x810 RGB32 | 5.19 | +6.5% / +3.5% |
| 1280x720 -> 1920x1080 RGB32 | 6.05 | +4.8% / +3.3% |
| 1280x720 -> 1920x1440 RGB32 | 7.21 | +1.2% / +2.6% |
| 1080p -> 1440p RGB32 | 11.56 | +1.9% / +1.8% |
| 720p -> 4K RGB32 | 16.73 | +1.8% / +2.0% |
| 1280x720 -> 1920x1080 RGBA32 | 7.29 | -1.6% / +2.3% |
| 1080p -> 1440p RGBA32 | 13.98 | +3.1% / +3.1% |
| 1280x720 -> 1920x1080 RGB32, AVX2 | 4.14 | +0.0% / +0.2% |
| 1080p -> 1440p RGB32, AVX2 | 8.01 | -0.0% / +0.4% |

- MSVC's RGBA32 gain is not the triples': with them on, MSVC happened to compile the vertical tap loop without the copies
  of the next section. With that loop fixed, triples cost MSVC's RGBA32 upscales 9.7-11.2% (7.47 -> 8.31 ms, 14.37 ->
  15.76) and its RGB32 upscales up to 3.3%, ten alternating rounds.

## MSVC's vertical tap loop: accumulators in locals

clang-cl's RGBA32 upscales at SSE4.1 were 11-12% faster than MSVC's: 7.29 against 8.16 ms. VTune hotspots on 300
resizes of 1280x720 -> 1920x1080 RGBA32 at SSE4.1, split by the kernel's two call lines and by loop address ranges:

| Part | MSVC, s | clang-cl, s |
|---|---:|---:|
| Horizontal pass, conversion included | 0.964 | 0.864 |
| Vertical pass and output | 1.357 | 1.155 |
| of which the tap loop | 0.806 | 0.582 |

- The tap loop held three quarters of the gap: 45 instructions per tap under MSVC for clang-cl's 30, the same loads,
  multiplies and adds. MSVC computed each sum in a second register, copied it back, and kept one of the 8 accumulators
  on the stack. Its AVX2 tap loop: 29 instructions for 13.
- The horizontal pass's 0.1 s has no codegen difference behind it: the conversion loops match instruction for instruction.
- Accumulators in local blocks with the products written out compile to clang-cl's loop at every x64 level. Local
  blocks through `addTapToBlock`, or the products written out on the caller's blocks, kept the copies.

MSVC, the change against the tree before it, two sets of ten alternating rounds, median of same-round ratios / minimum
to minimum:

| Scenario | Before, min ms | After |
|---|---:|---:|
| 960x540 -> 1920x1080 RGB32 | 5.50 | +0.8% / +0.7% |
| 1280x720 -> 1920x810 RGB32 | 5.34 | -1.4% / +0.7% |
| 1280x720 -> 1920x1080 RGB32 | 6.23 | +2.2% / +0.6% |
| 1280x720 -> 1920x1440 RGB32 | 7.69 | -5.1% / -2.7% |
| 1080p -> 1440p RGB32 | 11.89 | +0.5% / +0.5% |
| 720p -> 4K RGB32 | 17.28 | +0.8% / +1.0% |
| 1280x720 -> 1920x1080 RGBA32 | 8.20 | -12.1% / -9.1% |
| 1080p -> 1440p RGBA32 | 15.59 | -7.9% / -8.0% |
| 1080p -> 720p RGB32 | 8.05 | +0.7% / +0.5% |
| 4K -> 1080p RGB32 | 28.70 | -1.0% / -1.1% |
| 1080p -> 720p RGBA32 | 10.57 | -7.5% / -7.6% |
| 4K -> 1080p RGBA32 | 34.88 | -0.3% / -0.4% |
| 1280x720 -> 1920x1080 RGB32, AVX2 | 4.34 | -3.1% / -6.0% |
| 1080p -> 1440p RGB32, AVX2 | 8.37 | -2.7% / -5.4% |
| 1280x720 -> 1920x1080 RGBA32, AVX2 | 5.04 | -6.7% / -6.9% |
| 1080p -> 1440p RGBA32, AVX2 | 9.69 | -7.0% / -6.5% |
| 1080p -> 720p RGB32, AVX2 | 5.09 | -0.2% / -0.4% |
| 4K -> 1080p RGB32, AVX2 | 17.89 | -1.6% / -1.6% |
| 4K -> 1080p RGBA32, AVX2 | 21.48 | -2.4% / -2.3% |

- Rows without AVX2 run at SSE4.1. The 960x540 row came out +7.0% / +1.1% in a first set, the change at 5.9 ms in nine
  of ten rounds; it did not repeat.
- MSVC's RGBA32 upscales at SSE4.1 now trail clang-cl's by 2%: 7.45 against 7.29-7.33 ms.

## x runs three at a time over the interleaved row pairs

AVX2's run triples against eb92f4a, ten alternating rounds each, median of same-round ratios / minimum to minimum. The
placement control is eb92f4a with 32 bytes of `__nop()` at the start of `resizeRowsWith`, MSVC.

| Scenario, AVX2 | eb92f4a, MSVC, min ms | Triples, MSVC | Triples, clang-cl | Padding alone | Both padded |
|---|---:|---:|---:|---:|---:|
| 960x540 -> 1920x1080 RGB32 | 3.52 | -3.8% / -4.0% | -3.6% / -3.6% | +8.5% / +8.5% | -11.7% / -12.0% |
| 1280x720 -> 1920x810 RGB32 | 3.51 | -6.2% / -6.0% | -5.0% / -5.0% | +6.2% / +6.3% | -11.2% / -11.3% |
| 1280x720 -> 1920x1440 RGB32 | 4.81 | -5.1% / -5.0% | -3.9% / -3.9% | +8.2% / +8.4% | -12.1% / -11.9% |
| 720p -> 4K RGB32 | 11.46 | -3.8% / -3.9% | -3.6% / -3.7% | +7.0% / +7.2% | -10.2% / -10.3% |
| 960x540 -> 1920x1080 RGBA32 | 4.05 | -3.7% / -4.9% | -4.3% / -4.5% | +0.1% / +0.5% | -5.5% / -5.4% |
| 1080p -> 1440p RGBA32 | 9.06 | -4.1% / -4.3% | -3.8% / -3.6% | +0.2% / +0.0% | -4.1% / -4.0% |
| 720p -> 4K RGBA32 | 12.78 | -4.5% / -4.5% | -4.4% / -4.4% | +0.1% / -0.1% | -4.8% / -4.8% |
| 1080p -> 720p RGB32 | 5.07 | +0.1% / -0.2% | +0.3% / +0.0% | -0.1% / +0.0% | +0.0% / +0.2% |
| 4K -> 1080p RGBA32 | 21.00 | +0.0% / -0.1% | +0.1% / +0.0% | -0.0% / +0.1% | +0.1% / -0.0% |

- clang-cl against its own build of eb92f4a. Both padded: the triples build padded against eb92f4a padded.
- The padding moves eb92f4a's RGB32 upscales 6-8.5%; the triples build stays within 0.4% of its unpadded times. Its
  RGB32 gain is 4-6% against eb92f4a's faster placement and 10-12% against the slower one.
- The standard rounds: the SSE4.1 rows, the same code in both builds, within 0.4%; 1280x720 -> 1920x1080 and 1080p ->
  1440p RGB32 at AVX2 -4.9% and -6.8% under MSVC, -3.9% and -5.6% under clang-cl.

## MSVC against clang-cl, and the placement control, at 6ab4ba0

Eight alternating rounds, every build against MSVC, median of same-round ratios / minimum to minimum. "Padded": 32 bytes
of `__nop()` at the start of `resizeRowsWith`. The QImage controls read within 1% across the four builds.

| Scenario, AVX2 | MSVC, min ms | MSVC padded | clang-cl | clang-cl padded |
|---|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 45.25 | +0.2% / +0.4% | -5.3% / -5.1% | -4.7% / -5.5% |
| 2.8 MP -> 1920x1280 RGB32 | 9.08 | +0.4% / +0.5% | +1.7% / +1.9% | +1.8% / +1.9% |
| 4K -> 1600x900 RGB32 | 16.30 | +0.3% / +0.3% | -0.0% / +0.1% | -0.7% / -0.0% |
| 4K -> 720p RGB32 | 14.37 | -0.3% / +0.2% | -0.3% / -0.1% | -0.2% / +0.1% |
| 1080p -> 240p RGB32 | 3.60 | -0.1% / -0.2% | -3.0% / -2.9% | -2.5% / -3.0% |
| 1080p -> 120x68 RGB32 | 2.62 | +0.4% / +0.5% | -6.3% / -6.5% | -6.1% / -6.4% |
| 21 MP -> 175x117 RGB32 | 25.27 | -0.3% / +0.1% | -7.7% / -7.3% | -8.1% / -7.7% |
| 4K -> 200x113 RGB32 | 11.75 | -0.2% / +0.0% | -5.4% / -5.7% | -5.8% / -5.6% |
| 4K -> 200x113 RGBA32 | 14.26 | -0.0% / +0.1% | -6.1% / -5.3% | -6.4% / -5.6% |
| 4K -> 64x64 RGB32 | 9.93 | +0.3% / +0.4% | -5.0% / -5.0% | -4.9% / -4.9% |
| 4K -> 1080p Grayscale8 | 9.00 | -2.1% / +0.1% | -1.0% / +0.7% | -0.4% / +0.9% |
| 4K -> 1080p RGB24 | 18.21 | +0.1% / +0.2% | -0.2% / -0.2% | +0.1% / -0.4% |
| 4K -> 1080p RGB32 | 17.89 | +0.1% / +0.3% | +0.2% / +1.6% | +1.3% / +1.9% |
| 4K -> 1080p RGBA32 | 21.37 | -0.2% / -0.3% | -3.1% / -3.0% | -2.8% / -2.9% |
| 720p -> 4K RGBA32 | 13.37 | +0.5% / -0.1% | -0.7% / -0.4% | -0.5% / -0.6% |
| 720p -> 4K RGB32 | 12.13 | +0.4% / -0.1% | +2.7% / +2.9% | +2.1% / +2.5% |
| 720p -> 4K RGB24 | 10.86 | -0.2% / -0.1% | +10.9% / +1.7% | +1.1% / +0.7% |
| 720p -> 4K Grayscale8 | 4.50 | -1.0% / -0.0% | +0.5% / +0.4% | -0.7% / +0.4% |
| 1080p at native size RGB32 | 1.83 | -2.2% / -2.7% | -1.3% / -0.8% | -0.8% / -1.3% |

| Scenario, SSE4.1 | MSVC, min ms | MSVC padded | clang-cl | clang-cl padded |
|---|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 80.43 | +0.2% / +0.1% | -5.1% / -5.4% | +4.0% / +3.9% |
| 2.8 MP -> 1920x1280 RGB32 | 13.98 | +0.1% / -0.1% | -2.4% / -2.3% | -2.0% / -2.1% |
| 4K -> 1600x900 RGB32 | 30.55 | +0.3% / +0.1% | -3.0% / -3.3% | +2.8% / +3.1% |
| 4K -> 720p RGB32 | 27.67 | -0.1% / -0.0% | -1.4% / -1.2% | +4.5% / +4.8% |
| 1080p -> 240p RGB32 | 6.55 | +0.1% / +0.3% | -4.2% / -4.2% | +4.8% / +5.1% |
| 1080p -> 120x68 RGB32 | 5.90 | -0.2% / -0.3% | -6.8% / -6.8% | +4.8% / +4.8% |
| 21 MP -> 175x117 RGB32 | 59.35 | -0.1% / -0.1% | -7.3% / -7.4% | +3.9% / +3.8% |
| 4K -> 200x113 RGB32 | 24.56 | +0.1% / -0.2% | -7.8% / -7.6% | +2.9% / +2.9% |
| 4K -> 200x113 RGBA32 | 29.14 | +0.9% / +0.9% | -8.4% / -8.7% | -7.8% / -8.0% |
| 4K -> 64x64 RGB32 | 22.91 | +0.0% / -0.0% | -7.3% / -7.9% | +2.9% / +2.9% |
| 4K -> 1080p Grayscale8 | 10.72 | -0.8% / -0.3% | +11.9% / +12.0% | +11.2% / +11.8% |
| 4K -> 1080p RGB24 | 30.24 | +0.3% / +0.3% | -4.2% / -4.0% | -4.6% / -4.3% |
| 4K -> 1080p RGB32 | 28.62 | -0.1% / +0.2% | -4.3% / -4.5% | -5.1% / -4.2% |
| 4K -> 1080p RGBA32 | 34.76 | +0.0% / +0.0% | -4.2% / -4.4% | -4.5% / -4.7% |
| 720p -> 4K RGBA32 | 21.00 | +0.3% / +0.5% | -1.9% / -1.2% | -2.1% / -1.3% |
| 720p -> 4K RGB32 | 18.46 | -1.3% / -1.5% | -4.1% / -3.4% | -3.5% / -3.4% |
| 720p -> 4K RGB24 | 16.33 | +0.3% / +0.3% | +15.2% / +15.4% | +15.1% / +15.1% |
| 720p -> 4K Grayscale8 | 5.72 | -1.2% / -0.9% | +3.1% / +2.6% | +1.6% / +2.7% |

- MSVC padded: the kernels' instructions are identical to the plain build's (`extract_kernels.py`), so the column is
  placement alone. No row moves over 1.5% minimum to minimum, the two read by launch (below) aside.
- clang-cl padded is no placement control: `__nop()` is inline assembly there and changes the code of 69 of 355 kernels.
  Of the SSE4.1 kernels only `<2,2,0>` changes, which no row runs.
- SSE4.1 under clang-cl: `resizeRowsWith<3,4,0>`, the RGB32 kernel for x runs over 12 taps, has the same code in both
  builds, 16 bytes apart modulo 32. The padded build has jumps on 32-byte boundaries in two more multiply loops, of 34
  and 45 instructions. The rows it runs, the RGB32 downscales of 2.4:1 and more, lose 6-12%; 21 MP -> 175x117 reads
  55.0-55.6 ms plain and 61.6-62.2 ms padded in all eight rounds. The rows on the per-length kernels do not move.
- Two AVX2 rows read two levels by launch, minimum to minimum the reading there: 720p -> 4K RGB24 10.9-11.1 or
  12.6-12.9 ms in three of the four builds, native size 1.8-1.9 or 2.1-2.2 ms under MSVC.

## Two dest rows per step on downscales

AVX2's vertical pass filtering two dest rows over their windows' union, against a2d03fc. Eight alternating rounds,
median of same-round ratios / minimum to minimum. "Padded": both builds with 32 bytes of `__nop()` at the start of
`resizeRowsWith`, the pair against the padded a2d03fc. clang-cl against its own build of a2d03fc. The builds paired every
resize; the kernel pairs downscales of 2x and more only, and the rows below the line take the single-row path.

| Scenario, AVX2 | a2d03fc, MSVC, min ms | Paired, MSVC | Both padded | Paired, clang-cl |
|---|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 45.31 | -1.8% / -2.1% | -2.1% / -2.2% | -3.1% / -3.4% |
| 4K -> 1600x900 RGB32 | 16.30 | -3.9% / -3.9% | -4.2% / -4.2% | -5.7% / -5.5% |
| 4K -> 720p RGB32 | 14.41 | -4.2% / -3.3% | -3.3% / -3.2% | -3.8% / -3.8% |
| 1080p -> 240p RGB32 | 3.58 | -1.6% / -1.1% | -1.3% / -1.4% | -4.0% / -3.6% |
| 1080p -> 120x68 RGB32 | 2.61 | -0.2% / +0.4% | -0.3% / -0.4% | -2.1% / -1.2% |
| 21 MP -> 175x117 RGB32 | 25.30 | -1.0% / -1.1% | -0.8% / -1.0% | -0.5% / -0.8% |
| 4K -> 200x113 RGB32 | 11.71 | +0.3% / +0.2% | +0.0% / -0.2% | +0.5% / -0.8% |
| 4K -> 200x113 RGBA32 | 14.28 | -0.1% / -0.2% | +0.1% / +0.0% | -1.4% / -1.5% |
| 4K -> 64x64 RGB32 | 9.92 | +0.0% / -0.4% | -0.8% / -0.8% | -1.0% / -2.1% |
| 4K -> 1080p Grayscale8 | 9.00 | -3.8% / -2.6% | -3.4% / -2.8% | -2.4% / -3.0% |
| 4K -> 1080p RGB24 | 18.19 | -4.3% / -4.3% | -5.0% / -4.2% | -4.7% / -4.7% |
| 4K -> 1080p RGB32 | 17.93 | -4.1% / -4.3% | -4.9% / -4.4% | -5.0% / -6.7% |
| 4K -> 1080p RGBA32 | 21.33 | -4.6% / -4.6% | -4.7% / -4.5% | -4.5% / -4.2% |
| 2.8 MP -> 1920x1280 RGB32 | 9.10 | -3.1% / -2.5% | -2.6% / -3.2% | -3.3% / -3.6% |
| 720p -> 4K RGBA32 | 13.35 | +0.1% / -0.6% | -0.9% / +0.0% | -1.8% / -0.5% |
| 720p -> 4K RGB32 | 12.11 | +0.5% / +0.4% | +0.8% / +0.8% | -0.7% / -0.8% |
| 720p -> 4K RGB24 | 10.85 | +1.7% / +1.6% | +1.0% / +1.3% | -1.3% / -0.9% |
| 720p -> 4K Grayscale8 | 4.53 | -0.1% / -0.2% | -0.5% / -0.6% | +0.0% / -0.9% |

- The rows the horizontal pass dominates gain least: the 16x-32x thumbnails and 4K -> 64x64 within 1-2%.
- The SSE4.1 rows, the same code in both builds: within 1.6%, but 4K -> 1600x900 at +2.1%.

## Four y taps held across the dest row

AVX2's vertical pass holding a 4-tap dest row's row pointers and weights in registers, against 9685f09: eight
alternating rounds, median of same-round ratios / minimum to minimum, clang-cl against its own build of 9685f09.

| Scenario, AVX2 | 9685f09, MSVC, min ms | Held, MSVC | Held, clang-cl |
|---|---:|---:|---:|
| 720p -> 4K RGBA32 | 13.45 | -2.0% / -1.7% | -4.1% / -4.0% |
| 720p -> 4K RGB32 | 12.32 | -18.4% / -3.2% | -4.0% / -3.5% |
| 720p -> 4K RGB24 | 10.86 | -2.3% / -2.1% | +7.2% / -3.7% |
| 720p -> 4K Grayscale8 | 4.54 | -3.1% / -3.0% | -3.8% / -3.3% |

- Two rows read two levels by launch (open lead 19), minimum to minimum the reading there: MSVC's 9685f09 RGB32 upscale
  at 12.3 or 13.8-15.1 ms, the held build at 11.9-12.2 ms in every round; clang-cl's held RGB24 upscale at 10.6 or
  11.3-12.6 ms. Native size, the same code in both builds, reads -14.2% / -2.0% under MSVC the same way.
- The downscales within 1%, but MSVC's 24 MP -> 1620x1080 -2.9% / -1.2% and clang-cl's 4K -> 1080p RGBA32 -2.2% / -1.8%.
  The SSE4.1 rows within 1.3%.
- The test executable: MSVC 3.29 -> 3.54 MB, clang-cl 4.27 -> 4.59 MB.

At SSE4.1 too, against the AVX2-held build, whose SSE4.1 code is 9685f09's: eight rounds, each compiler against its own
build.

| Scenario, SSE4.1 | AVX2 held, MSVC, min ms | Held, MSVC | Held, clang-cl |
|---|---:|---:|---:|
| 720p -> 4K RGBA32 | 21.07 | -5.1% / -4.3% | -8.0% / -7.9% |
| 720p -> 4K RGB32 | 18.21 | -3.3% / -4.2% | -7.0% / -6.9% |
| 720p -> 4K RGB24 | 16.41 | -1.0% / -0.7% | -17.0% / -16.9% |
| 720p -> 4K Grayscale8 | 5.73 | -4.5% / -4.6% | -6.2% / -5.8% |

- MSVC: the SSE4.1 downscales within 1%, 4K -> 1600x900 +2.8% / +2.4% aside; the AVX2 rows, the same code, within 1.5%.
- clang-cl: the SSE4.1 downscales, which take the listed loop in both builds, moved by -11% to +12%, steady across the
  rounds (21 MP -> 175x117 -10.8%, 4K -> 200x113 RGBA32 +11.5%): placement in its unpadded kernels. Its upscale rows
  are not attributable. Its AVX2 720p -> 4K RGB32, the same code, read two levels by launch: +10.5% / +8.1%.
- The test executable: MSVC 3.54 -> 3.63 MB, clang-cl 4.59 -> 4.69 MB.

## A row pair converted in one loop

Against 67935c2, six alternating rounds, each compiler against its own build, median of same-round ratios / minimum to
minimum.

| Scenario | 67935c2, MSVC AVX2, min ms | MSVC, AVX2 | MSVC, SSE4.1 | clang-cl, AVX2 | clang-cl, SSE4.1 |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 44.32 | -2.7% / -2.0% | -0.1% / -0.5% | -2.0% / -1.7% | -3.2% / -3.2% |
| 2.8 MP -> 1920x1280 RGB32 | 9.11 | +0.3% / +0.2% | -1.2% / -1.0% | -0.2% / -0.2% | -0.3% / -0.4% |
| 4K -> 1600x900 RGB32 | 15.65 | -0.3% / +0.0% | +1.8% / +1.9% | -0.2% / +0.2% | -3.1% / -2.8% |
| 4K -> 720p RGB32 | 13.92 | +0.1% / -0.2% | -0.3% / -0.2% | +0.2% / +0.1% | -0.7% / -0.4% |
| 720p -> 4K RGBA32 | 13.22 | +0.0% / -0.0% | -0.1% / -0.7% | -0.5% / -0.4% | -0.2% / -0.8% |
| 720p -> 4K RGB32 | 11.89 | -0.3% / +0.2% | -1.3% / -0.5% | -0.7% / -0.2% | +0.3% / +0.0% |
| 1080p -> 240p RGB32 | 3.51 | -1.0% / -0.6% | +0.3% / +0.2% | -1.3% / -0.6% | -2.5% / -2.1% |
| 1080p -> 120x68 RGB32 | 2.62 | -2.1% / -1.7% | -0.8% / -1.1% | -1.9% / -2.2% | -1.9% / -1.7% |
| 21 MP -> 175x117 RGB32 | 25.13 | -2.8% / -2.9% | -0.7% / -0.6% | -0.8% / -1.0% | -2.2% / -2.0% |
| 4K -> 200x113 RGB32 | 11.76 | -3.1% / -3.2% | -1.8% / -1.3% | -2.3% / -2.8% | -1.4% / -0.9% |
| 4K -> 200x113 RGBA32 | 14.31 | -4.9% / -4.3% | -1.7% / -1.7% | -4.8% / -4.5% | -3.3% / -3.2% |
| 1080p at native size RGB32 | 1.85 | -14.6% / -1.9% | - | -2.2% / +1.8% | - |
| 4K -> 1080p Grayscale8 | 8.82 | -0.5% / -0.9% | -1.8% / -2.3% | -2.5% / -1.7% | +0.8% / +1.3% |
| 4K -> 1080p RGB24 | 17.43 | -0.7% / -0.3% | -0.8% / -1.0% | -1.1% / -0.3% | -2.9% / -2.9% |
| 4K -> 1080p RGB32 | 17.18 | -4.6% / -0.2% | -1.4% / -1.3% | +1.7% / +0.3% | +2.8% / +3.3% |
| 4K -> 1080p RGBA32 | 20.37 | +0.2% / +0.1% | -1.2% / -1.6% | -0.7% / -0.1% | -3.3% / -3.0% |
| 720p -> 4K Grayscale8 | 4.38 | -0.7% / -0.5% | -0.0% / +0.2% | -1.1% / -1.3% | -0.5% / -0.4% |
| 720p -> 4K RGB24 | 10.64 | +0.5% / -0.3% | -0.4% / -0.6% | -0.1% / -0.8% | +4.1% / +1.5% |

- clang-cl's SSE4.1 rows move with placement (open lead 5): its +3% and +4% are within that.
- Native size, the same code in both builds, reads two levels by launch under MSVC.

The AVX level, an AVX2 build capped at it, 4K -> 200x113, probe builds of 67935c2, six rounds, minimum ms:

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| RGBA32 | 18.02 | 16.87 |
| RGBA32, the source marked premultiplied | 16.23 | 15.22 |
| RGB32 | 16.20 | 15.01 |
| RGBA32 at SSE4.1 | 29.44 | 29.71 |

- clang-cl is ahead by 6-7% in every layout, as at AVX2. Premultiplying costs both compilers 1.6-1.8 ms.

## MSVC's source prefetch and conversion loads (open lead 11)

clang-cl against MSVC at e7f3800, each level against MSVC's at that level: four rounds with controls, median of
same-round ratios / minimum to minimum.

| Scenario | MSVC AVX2, min ms | clang-cl, AVX2 | clang-cl, SSE4.1 |
|---|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 43.28 | -4.2% / -5.8% | -7.5% / -7.0% |
| 2.8 MP -> 1920x1280 RGB32 | 9.13 | +0.9% / +1.7% | -1.6% / -2.3% |
| 4K -> 1600x900 RGB32 | 15.71 | -1.1% / -1.2% | -9.8% / -9.8% |
| 4K -> 720p RGB32 | 13.95 | -0.5% / -0.4% | -0.9% / -0.7% |
| 720p -> 4K RGBA32 | 13.27 | -2.6% / -3.0% | -1.7% / -4.0% |
| 720p -> 4K RGB32 | 11.93 | +1.0% / +1.2% | -5.8% / -5.9% |
| 1080p -> 240p RGB32 | 3.48 | -1.8% / -5.2% | -6.9% / -6.9% |
| 1080p -> 120x68 RGB32 | 2.56 | -7.2% / -7.2% | -7.9% / -7.1% |
| 21 MP -> 175x117 RGB32 | 24.38 | -6.7% / -6.6% | -8.4% / -8.2% |
| 4K -> 200x113 RGB32 | 11.45 | -4.6% / -5.1% | -8.1% / -8.0% |
| 4K -> 200x113 RGBA32 | 13.75 | -7.1% / -7.7% | -6.9% / -7.0% |
| 1080p at native size RGB32 | 1.79 | +3.5% / +3.6% | - |
| 4K -> 1080p Grayscale8 | 8.72 | +0.7% / -1.0% | +14.8% / +14.7% |
| 4K -> 1080p RGB24 | 17.43 | -0.6% / -0.7% | -1.4% / -1.8% |
| 4K -> 1080p RGB32 | 17.18 | +4.9% / -1.3% | +0.3% / +0.1% |
| 4K -> 1080p RGBA32 | 20.37 | -2.7% / -2.7% | -4.9% / -5.2% |
| 720p -> 4K Grayscale8 | 4.35 | +7.7% / +0.5% | -0.1% / -0.4% |
| 720p -> 4K RGB24 | 10.67 | +7.2% / -0.7% | +4.9% / +2.4% |
| 1080p -> 4K NN RGB32 | 6.48 | +8.4% / -0.0% | - |
| 1080p -> 4K NN RGBA32 | 6.99 | +10.1% / +24.0% | - |
| 640x480 -> 1080p NN RGB32 | 1.60 | +1.0% / -0.0% | - |
| 640x480 -> 1080p NN RGBA32 | 1.69 | +2.5% / +3.5% | - |
| 4K -> 64x64 RGB32 | 9.92 | -3.3% / -3.5% | -8.9% / -8.5% |
| 101 MP -> 720p RGB32 | 149.30 | -6.1% / -6.6% | -7.6% / -7.7% |

VTune, 21 MP -> 175x117 RGB32 at AVX2, 400 resizes, `resizeRowsWith<3,4,0>`:

| Loop | MSVC | clang-cl |
|---|---:|---:|
| Whole kernel | 9.35 s | 8.80 s |
| The row pair's conversion | 2.54 s, 24.2 G instructions | 1.99 s, 19.0 G |
| The block loop, 8 taps a step | 4.79 s | 4.87 s |
| The rest | 2.02 s | 1.94 s |

The conversion loop's hardware events, counted throughout:

| | MSVC | MSVC, 8-byte loads | clang-cl |
|---|---:|---:|---:|
| Cycles | 9.26 G | 8.74 G | 7.04 G |
| Instructions per cycle | 2.73 | 2.28 | 2.68 |
| Stall cycles on an L3 miss | 1.34 G | 1.41 G | 0.64 G |
| Loads retired that missed L3 | 13.5 M | 13.4 M | 9.2 M |
| Loads blocked by a 4K alias | 385 M | 872 M | 967 M |
| Store buffer full | 1% | 2% | 4% |

- MSVC's `prefetchPixels` requested the first row's lines alone in every row-pair path, at all three levels: from a
  loop over the rows it keeps the first iteration. clang-cl requested both. 342 `prefetcht0` in MSVC's executable
  against clang-cl's 618; 618 with the rows written out.
- MSVC's AVX2 conversion widened the 16 loaded bytes' high half after a `vpunpckhqdq`; clang-cl loads each 8 bytes into
  its `vpmovzxbd`. Loading them so took MSVC's loop from 22 to 18 instructions and left its time: row B still waited
  on DRAM.
- With both, on 1080p -> 120x68 the conversion loop takes 0.24 s against clang-cl's 0.23 s, the block loop 0.55 s
  against 0.56 s. What remains is the column loop's code around the block loop, 0.26 s against 0.20 s, spread over
  its instructions.

Both against e7f3800, each compiler against its own build: three rounds with controls.

| Scenario | e7f3800, MSVC AVX2, min ms | MSVC, AVX2 | MSVC, SSE4.1 | clang-cl, AVX2 | clang-cl, SSE4.1 |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 43.10 | -1.7% / -1.4% | -1.4% / -1.0% | -0.3% / +0.1% | +9.9% / +9.9% |
| 2.8 MP -> 1920x1280 RGB32 | 9.15 | -5.4% / -0.3% | +0.1% / +0.1% | -0.3% / -0.6% | +1.9% / +0.2% |
| 4K -> 1600x900 RGB32 | 15.70 | -1.1% / -1.1% | -2.7% / -2.7% | -0.2% / +0.0% | +6.1% / +5.8% |
| 4K -> 720p RGB32 | 13.93 | -1.2% / -0.7% | -1.2% / -1.2% | -0.7% / +0.0% | +3.4% / +3.4% |
| 720p -> 4K RGBA32 | 13.22 | +1.4% / +0.0% | +2.0% / +0.8% | -0.1% / -0.3% | +0.2% / +0.6% |
| 720p -> 4K RGB32 | 11.94 | -0.1% / -0.1% | +4.8% / +0.4% | -8.2% / -0.3% | -0.7% / +0.8% |
| 1080p -> 240p RGB32 | 3.47 | -1.5% / -0.6% | -1.5% / -1.7% | -0.1% / +0.1% | +9.4% / +9.4% |
| 1080p -> 120x68 RGB32 | 2.57 | -1.5% / -2.4% | -0.7% / -0.7% | -0.3% / -0.3% | +12.3% / +12.4% |
| 21 MP -> 175x117 RGB32 | 24.37 | -4.1% / -3.7% | -1.3% / -1.8% | -0.4% / -0.0% | +13.5% / +13.5% |
| 4K -> 200x113 RGB32 | 11.43 | -2.9% / -2.9% | -1.0% / -0.9% | +0.7% / +0.4% | +11.3% / +11.7% |
| 4K -> 200x113 RGBA32 | 13.72 | -4.7% / -4.9% | -1.6% / -1.6% | +0.7% / +0.3% | -3.4% / -3.5% |
| 1080p at native size RGB32 | 1.80 | +0.2% / +1.0% | - | +0.0% / -0.2% | - |
| 4K -> 1080p Grayscale8 | 8.84 | -0.7% / -1.2% | +0.4% / +0.8% | -0.5% / +0.6% | -1.3% / -1.1% |
| 4K -> 1080p RGB24 | 17.39 | +0.0% / -0.2% | +0.7% / +0.5% | -0.5% / +0.3% | -0.4% / -0.7% |
| 4K -> 1080p RGB32 | 17.15 | -0.4% / -0.8% | -0.3% / -0.3% | +4.8% / +4.9% | -1.0% / -1.1% |
| 4K -> 1080p RGBA32 | 20.35 | -0.5% / -0.3% | -0.1% / -0.1% | -0.4% / -0.1% | +0.3% / +0.9% |
| 720p -> 4K Grayscale8 | 4.39 | +29.1% / +2.6% | -0.6% / -0.3% | +7.4% / +2.8% | +0.0% / +0.0% |
| 720p -> 4K RGB24 | 10.88 | -2.4% / -2.7% | -0.2% / -0.2% | -1.8% / +0.4% | -1.8% / -2.5% |
| 1080p -> 4K NN RGB32 | 6.47 | +22.1% / +1.1% | - | +0.7% / +1.1% | - |
| 1080p -> 4K NN RGBA32 | 8.43 | -17.2% / -16.7% | - | +0.6% / -0.7% | - |
| 640x480 -> 1080p NN RGB32 | 1.62 | -17.5% / -1.1% | - | +1.0% / +2.0% | - |
| 640x480 -> 1080p NN RGBA32 | 2.00 | -10.5% / -14.8% | - | -0.3% / -0.1% | - |
| 4K -> 64x64 RGB32 | 9.95 | -2.2% / -2.2% | -0.8% / +0.2% | +0.6% / +0.1% | +11.3% / +11.7% |
| 101 MP -> 720p RGB32 | 149.19 | -1.9% / -1.6% | -1.1% / -1.1% | +0.0% / +0.6% | +11.2% / +11.2% |

- MSVC: the RGB32 and RGBA32 downscales of 3.7:1 and more 1.5-4.9% faster at AVX2 by their medians, 0.7-1.8% at
  SSE4.1, 4K -> 1600x900 2.7%. Nearest neighbor, whose code neither change touches, reads -17% to +22% at AVX2: this
  session's noise, as 720p -> 4K Grayscale8's +29% median.
- clang-cl's SSE4.1 downscales of 2.4:1 and more, 3-13.5% slower: its inner loops are the same instructions, a block of
  the kernel moved (open lead 5). Its AVX2 rows within 1% minimum to minimum, 4K -> 1080p RGB32 +4.9% and 720p -> 4K
  Grayscale8 +2.8% aside.
- The prefetch alone, without the loads: MSVC's strong downscales 0.3-2.5% faster at AVX2 (four rounds).
- On top of both, the spread indices hidden from MSVC (experiment 29 again): within 1% of the fix on every filtered row.

## MSVC's per-column branches and the slide check (open lead 11)

At 87bebbc. MSVC's executable with clang-cl's AVX2 kernel object linked in place of its own, against both builds: four
rounds without controls, median of same-round ratios / minimum to minimum.

| Scenario | MSVC, min ms | Hybrid | clang-cl |
|---|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 42.33 | -4.2% / -4.1% | -3.6% / -4.0% |
| 2.8 MP -> 1920x1280 RGB32 | 9.09 | +1.8% / +1.8% | +1.9% / +2.0% |
| 4K -> 1600x900 RGB32 | 15.50 | +0.1% / -0.3% | -0.6% / -0.2% |
| 4K -> 720p RGB32 | 13.87 | +0.5% / -0.3% | -0.3% / -0.4% |
| 720p -> 4K RGBA32 | 13.24 | -2.2% / -2.5% | -2.2% / -2.3% |
| 720p -> 4K RGB32 | 11.91 | +1.0% / +1.0% | +1.1% / +1.0% |
| 1080p -> 240p RGB32 | 3.47 | -3.6% / -4.3% | -4.1% / -4.1% |
| 1080p -> 120x68 RGB32 | 2.52 | -4.9% / -6.0% | -6.4% / -5.9% |
| 21 MP -> 175x117 RGB32 | 23.49 | -3.4% / -3.5% | -2.8% / -3.5% |
| 4K -> 200x113 RGB32 | 11.14 | -2.1% / -2.6% | -3.5% / -3.1% |
| 4K -> 200x113 RGBA32 | 13.07 | -2.1% / -2.1% | -1.9% / -2.3% |
| 1080p at native size RGB32 | 1.80 | +0.5% / +2.3% | -0.1% / +0.1% |
| 4K -> 1080p Grayscale8 | 8.79 | +0.9% / +0.8% | -0.9% / -0.2% |
| 4K -> 1080p RGB24 | 17.41 | -0.3% / -0.4% | -0.5% / -0.5% |
| 4K -> 1080p RGB32 | 17.14 | -1.1% / -0.6% | -0.8% / -0.6% |
| 4K -> 1080p RGBA32 | 20.35 | -1.6% / -1.9% | -2.2% / -2.3% |
| 720p -> 4K Grayscale8 | 4.40 | +0.5% / +0.6% | +12.2% / -0.5% |
| 720p -> 4K RGB24 | 10.64 | +0.5% / +0.0% | +0.1% / -0.1% |
| 1080p -> 4K NN RGB32 | 6.48 | +0.3% / +0.0% | -5.4% / -0.3% |
| 1080p -> 4K NN RGBA32 | 7.05 | +0.7% / +0.2% | +0.5% / +0.2% |
| 640x480 -> 1080p NN RGB32 | 1.60 | -0.7% / -0.9% | +3.3% / +0.4% |
| 640x480 -> 1080p NN RGBA32 | 1.66 | +1.5% / +0.6% | +1.8% / -0.0% |
| 4K -> 64x64 RGB32 | 9.72 | -0.7% / -0.8% | -1.0% / -1.3% |
| 101 MP -> 720p RGB32 | 145.59 | -3.3% / -3.8% | -4.0% / -3.8% |

- The hybrid runs as clang-cl on every row: the gap is the kernels' code.

1080p -> 120x68 RGB32, 2000 resizes, `resizeRowsWith<3,4,0>`, M cycles (M micro-ops issued):

| Part | MSVC | clang-cl |
|---|---:|---:|
| Kernel | 16858 | 15896 |
| The block loop | 7764 | 7762 |
| The column loop's code around it | 3456 (12290) | 2804 (10106) |
| The row pair's conversion | 3498 | 3062 |

- The code around the block loop runs at 3.6 micro-ops a cycle in both, with no stalls on either end: issue-bound.
- The conversion: clang-cl unrolls it twice, 5 loop instructions per two blocks against MSVC's per one; 6.07 cycles a
  block against 5.24.
- Kernel mispredicts, precise events: MSVC 22.8 M, clang-cl 16.4 M. The difference is one branch, `prepareRun`'s slide
  check, 6.4-7.2 M under MSVC and none under clang-cl; no front-end resteers there in either.

What the slide check sees:
- It alternates every other chunk, 7 slides a row pair: a slide follows a chunk of 7 columns, none a chunk of 8 (a
  traced build).
- After the block loop a 96-tap column took 4 taken branches under MSVC, a jump over each tap step it skips and the
  loop's, and 3 under clang-cl, whose code after the steps is duplicated: 14 and 13 a column with the block loop's 10.
- The chunk's start is about 7 x 14 + 1 = 99 taken branches back under MSVC, 92 under clang-cl; Skylake's history holds
  about 93.

The mispredicts at the check, M, in builds that move one thing:

| Build | Slide check | Kernel |
|---|---:|---:|
| MSVC | 6.4-7.2 | 22.8-25.2 |
| MSVC, 32 and 48 bytes of padding at the kernel's start | 7.2, 7.2 | 22.0, 24.8 |
| MSVC, chunks of 64 pixels: 3-4 columns | 1.2 | 16.0 |
| MSVC, no source prefetch: 14 taken branches less a chunk | 4.8 | 20.4 |
| clang-cl | 0 | 14.8-16.4 |
| clang-cl, the conversion loop not unrolled: 16 more | 0 | 15.2 |
| clang-cl, the prefetch over 256 pixels: 16 more | 0 | 21.6 |
| clang-cl, chunks of 256 pixels: 15-16 columns | 4.0 | 19.6 |

- Branches added ahead of the chunk's columns do not reach the check; the columns' count and their branches do. Removing
  the prefetch helps MSVC partly: unexplained.

An early return for runs of whole blocks, ahead of the steps: the check's mispredicts gone, 1080p -> 120x68 -1.7% in
kernel cycles. Four rounds, each compiler against MSVC's 87bebbc and its own:

| Scenario | 87bebbc MSVC, min ms | Early return | clang-cl, 87bebbc | clang-cl, early return |
|---|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 42.69 | +0.9% / +0.6% | -3.4% / -4.3% | +2.4% / +2.6% |
| 2.8 MP -> 1920x1280 RGB32 | 9.09 | +4.7% / +5.1% | +6.5% / +1.9% | -1.2% / +0.2% |
| 4K -> 1600x900 RGB32 | 15.48 | +1.5% / +1.6% | +0.5% / +0.1% | -0.8% / -0.0% |
| 4K -> 720p RGB32 | 13.84 | -0.7% / +0.0% | -1.0% / -0.1% | +1.7% / +0.5% |
| 720p -> 4K RGBA32 | 14.62 | -4.3% / -4.3% | -5.2% / -5.5% | +2.3% / -0.3% |
| 720p -> 4K RGB32 | 12.96 | -1.9% / -1.6% | +1.9% / +3.7% | -2.6% / -4.4% |
| 1080p -> 240p RGB32 | 3.47 | +0.7% / +0.5% | -4.2% / -4.0% | +1.0% / +1.4% |
| 1080p -> 120x68 RGB32 | 2.52 | -1.7% / -1.8% | -5.1% / -5.6% | -2.0% / -1.9% |
| 21 MP -> 175x117 RGB32 | 23.46 | -0.1% / -0.1% | -3.5% / -3.7% | -1.4% / -1.4% |
| 4K -> 200x113 RGB32 | 11.08 | -0.1% / +0.6% | -2.1% / -2.4% | +1.5% / +1.7% |
| 4K -> 200x113 RGBA32 | 13.07 | +1.6% / +1.4% | -0.6% / -2.2% | -0.0% / +1.7% |
| 1080p at native size RGB32 | 1.98 | -0.9% / -8.3% | +1.7% / -7.0% | -8.0% / +0.9% |
| 4K -> 1080p Grayscale8 | 8.76 | +1.2% / +0.4% | +0.5% / +0.4% | +1.2% / +1.5% |
| 4K -> 1080p RGB24 | 17.40 | -0.7% / +0.0% | +0.8% / -0.3% | -0.6% / -0.2% |
| 4K -> 1080p RGB32 | 17.07 | -1.2% / +0.1% | +0.8% / +2.0% | -1.1% / -1.8% |
| 4K -> 1080p RGBA32 | 20.54 | -1.8% / -0.8% | -1.6% / -0.9% | -2.0% / -2.0% |
| 720p -> 4K Grayscale8 | 4.82 | -9.4% / -9.6% | -2.8% / -8.9% | -3.1% / +0.1% |
| 720p -> 4K RGB24 | 11.21 | -2.3% / -1.0% | -3.8% / -1.4% | +6.1% / +0.6% |
| 1080p -> 4K NN RGB32 | 7.30 | +5.0% / +9.1% | +1.1% / +1.2% | +0.2% / +0.3% |
| 1080p -> 4K NN RGBA32 | 7.77 | +8.9% / +9.3% | +9.5% / +9.5% | -6.5% / -6.4% |
| 640x480 -> 1080p NN RGB32 | 1.85 | -17.4% / -13.9% | -15.2% / -14.5% | +10.4% / +1.1% |
| 640x480 -> 1080p NN RGBA32 | 1.65 | +1.0% / -0.2% | +1.0% / +6.9% | -3.6% / -6.0% |
| 4K -> 64x64 RGB32 | 9.82 | +0.2% / -0.2% | -1.8% / -2.1% | -0.5% / -0.4% |
| 101 MP -> 720p RGB32 | 146.65 | -0.7% / -0.4% | -4.2% / -3.7% | +0.6% / +0.0% |

- MSVC put the early return on the fall-through path: a run with taps left now jumps to its steps, one more taken branch.
  1080p -> 240p and 4K -> 200x113 RGBA32 +1% to +2%.
- 2.8 MP -> 1920x1280 takes a kernel of 7-tap runs, the same instructions in both builds: placement.

The committed form, every step behind one test and one end for all runs, against 87bebbc: 2 taken branches after the
block loop for a run of whole blocks, a run with taps left as before. Precise events: the slide check's mispredicts gone,
kernel mispredicts 25.2 M to 16.0 M, cycles -2.1% under MSVC and -2.5% under clang-cl. Four rounds, the padded pair a
placement control:

| Scenario | 87bebbc MSVC, min ms | MSVC | MSVC, padded | clang-cl | clang-cl, 87bebbc |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 42.73 | +0.2% / +0.7% | -1.5% / +0.1% | +0.1% / -0.1% | -4.5% / -4.4% |
| 2.8 MP -> 1920x1280 RGB32 | 9.09 | +2.4% / +0.4% | +1.3% / +2.0% | -1.9% / -4.2% | +6.7% / +6.8% |
| 4K -> 1600x900 RGB32 | 15.55 | -0.4% / -0.3% | +0.2% / +0.2% | -0.6% / -0.1% | -0.4% / -0.6% |
| 4K -> 720p RGB32 | 13.84 | -0.2% / +0.2% | -0.5% / -0.0% | +1.2% / +0.4% | -1.4% / -0.2% |
| 720p -> 4K RGBA32 | 14.22 | +1.5% / +0.4% | +0.8% / +1.3% | +1.9% / +4.3% | -2.5% / -2.5% |
| 720p -> 4K RGB32 | 12.93 | +2.4% / +1.7% | +3.1% / -0.2% | -1.7% / -2.2% | +3.4% / +3.5% |
| 1080p -> 240p RGB32 | 3.47 | -0.6% / -0.3% | +0.5% / +0.6% | +1.1% / +0.9% | -3.9% / -4.0% |
| 1080p -> 120x68 RGB32 | 2.51 | -1.4% / -1.4% | -2.2% / -1.9% | -1.9% / -1.9% | -5.7% / -5.7% |
| 21 MP -> 175x117 RGB32 | 23.52 | -0.2% / -0.2% | +0.1% / -0.3% | -1.7% / -1.7% | -3.5% / -3.6% |
| 4K -> 200x113 RGB32 | 11.10 | -0.2% / -0.2% | -0.8% / -0.7% | +2.3% / +1.7% | -2.7% / -2.6% |
| 4K -> 200x113 RGBA32 | 13.07 | +0.6% / +0.4% | +0.6% / +0.7% | +0.9% / +0.8% | -2.0% / -2.2% |
| 1080p at native size RGB32 | 1.86 | +9.3% / +17.0% | +14.5% / +1.2% | +8.9% / +0.9% | -7.5% / -2.0% |
| 4K -> 1080p Grayscale8 | 8.80 | -0.2% / -0.6% | -0.5% / -0.4% | +0.8% / +0.2% | +0.8% / -0.5% |
| 4K -> 1080p RGB24 | 17.62 | -1.0% / -1.3% | -1.3% / -0.2% | -1.2% / -1.3% | -0.0% / -0.3% |
| 4K -> 1080p RGB32 | 17.12 | -1.2% / -0.3% | -0.1% / +0.1% | -1.2% / -1.3% | -0.2% / +0.9% |
| 4K -> 1080p RGBA32 | 20.36 | -0.3% / -0.1% | +0.7% / +0.1% | -1.4% / -0.2% | -1.7% / -1.6% |
| 720p -> 4K Grayscale8 | 4.45 | +5.2% / -0.7% | +4.7% / +7.2% | +1.8% / +0.6% | -3.5% / -2.9% |
| 720p -> 4K RGB24 | 11.24 | +1.0% / +4.7% | +2.1% / -0.6% | +3.3% / +4.3% | -1.7% / -0.2% |
| 1080p -> 4K NN RGB32 | 7.43 | -0.2% / +1.7% | +1.3% / +2.2% | -0.8% / -4.7% | +2.1% / +6.9% |
| 1080p -> 4K NN RGBA32 | 8.42 | -4.3% / -4.2% | -5.1% / -5.0% | -3.0% / -0.8% | +0.6% / -3.1% |
| 640x480 -> 1080p NN RGB32 | 1.62 | +0.9% / +10.6% | +12.8% / +1.2% | +23.6% / +1.3% | -10.6% / -2.2% |
| 640x480 -> 1080p NN RGBA32 | 1.66 | +21.3% / +22.5% | +22.0% / +2.1% | +0.4% / +22.0% | +10.0% / -0.2% |
| 4K -> 64x64 RGB32 | 9.73 | +0.9% / +0.5% | +0.3% / +0.3% | +0.0% / +0.1% | -1.4% / -1.8% |
| 101 MP -> 720p RGB32 | 147.73 | -0.6% / -0.6% | -0.1% / -0.2% | +0.1% / +0.4% | -5.0% / -5.2% |

- MSVC: 1080p -> 120x68 -1.4% to -2.2%, the other downscales within 1% in both pairs.
- 2.8 MP -> 1920x1280 and the upscales take kernels of fixed run lengths, the same instructions in both builds, and move
  up to 7%. Nearest neighbor, also unchanged, swings up to 22%.
- VTune's precise events stopped the machine once, bugcheck 0xD1 in its sampling driver; the same collection then ran
  cleanly five times.

## The block loop to an end pointer, the steps from its pointers (open lead 11)

At cf1340b, 1080p -> 120x68 RGB32, 2000 resizes, `resizeRowsWith<3,4,0>`, M cycles:

| Part | MSVC | clang-cl | MSVC, end pointer | clang-cl, end pointer |
|---|---:|---:|---:|---:|
| Kernel | 16678 | 15706 | 15686 | 14840 |
| The block loop | 7740 | 7738 | 6742 | 6520 |
| The column loop's code around it | 3254 | 2842 | 3738 | 3282 |
| The row pair's conversion | | | 3098 | 3042 |

- The block loop: 20 micro-ops a block in both builds, 5.3 cycles; MSVC's bookkeeping a `tap` and a `tap + 8` counter
  and a copy, beside three stepped pointers. To an end pointer: 17, and the loop's 9 loads bind it.
- The two right columns also convert with one offset into all four streams ("The conversion's block loop steps one
  offset"). Per 96-tap column then:
  MSVC 245 issue slots and 117 loads, 75.1 cycles; clang-cl 239 and 113, 70.7. MSVC's extra: the four spread indices
  reloaded, both sums zeroed, two values reloaded from the stack after the block loop.
- clang-cl without its conversion unroll: 3130 M cycles in that loop against 3126 M unrolled. MSVC's loop stepped four
  pointers, 18 instructions a block against clang-cl's 15.

Variants against cf1340b, each compiler against its own build; four rounds without controls, median of same-round
ratios / minimum to minimum. The last three rows are against the end pointer build.

| Variant | 1080p -> 120x68 | 21 MP -> 175x117 | 4K -> 64x64 | 4K -> 200x113 RGB32 | 4K -> 200x113 RGBA32 |
|---|---:|---:|---:|---:|---:|
| One conversion offset, MSVC | -2.0% / -0.6% | -1.5% / -1.1% | +0.4% / +0.6% | -3.4% / -1.2% | -1.6% / -1.6% |
| With the block loop's end pointer, MSVC | -5.9% / -4.4% | -5.8% / -5.9% | -5.0% / -4.7% | -4.2% / -2.4% | -3.8% / -3.5% |
| With the block loop's end pointer, clang-cl | -4.8% / -4.7% | -6.9% / -7.4% | -8.8% / -8.0% | -2.0% / -2.0% | -2.0% / -1.8% |
| Spread indices hidden (experiment 29), MSVC | +0.7% / +0.7% | +1.0% / +0.5% | +0.6% / +0.7% | +0.3% / +0.3% | +0.2% / +0.1% |
| And sums zeroed on short runs only (experiment 39), MSVC | +1.9% / +1.8% | -0.2% / +0.1% | +0.6% / +0.6% | +1.3% / +1.1% | +1.0% / +1.3% |
| And the steps from the block loop's pointers, MSVC | -1.8% / -2.3% | -4.9% / -4.6% | -5.2% / -5.1% | -1.1% / -1.1% | -1.0% / -0.4% |

- The two that lose, padded: within 1.3% of their padded base.
- The steps from the loop's pointers under MSVC: no stack reference left in the AVX2 column loop. Under clang-cl, which had begun to spill with the end
  pointer: none either, and its rows 1-2% faster.

54b7a6b (the end pointer and the steps from the loop's pointers) against cf1340b, four rounds with controls; MSVC padded too:

| Scenario | MSVC AVX2 | MSVC AVX2, padded | MSVC SSE4.1 | clang-cl AVX2 | clang-cl SSE4.1 |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | +1.4% / +1.4% | +0.9% / +1.2% | +0.5% / +0.6% | +0.5% / +1.2% | -5.5% / -5.7% |
| 4K -> 1600x900 RGB32 | -0.8% / +0.5% | -0.5% / -0.2% | +1.6% / +1.2% | +2.3% / +1.7% | -0.5% / -0.5% |
| 4K -> 720p RGB32 | -0.3% / +0.3% | -0.2% / +0.2% | -0.1% / -0.4% | -0.9% / -0.1% | -4.8% / -4.4% |
| 720p -> 4K RGB32 | -0.8% / -0.1% | -0.5% / +0.6% | +0.5% / -1.4% | +3.2% / +3.4% | +3.6% / +3.7% |
| 1080p -> 240p RGB32 | -0.6% / -0.7% | +0.0% / -1.1% | +0.5% / +0.7% | -0.4% / -0.3% | -4.7% / -4.7% |
| 1080p -> 120x68 RGB32 | -5.0% / -5.5% | -5.4% / -5.5% | -0.2% / -0.2% | -4.4% / -4.7% | -10.2% / -10.6% |
| 21 MP -> 175x117 RGB32 | -9.0% / -9.3% | -9.3% / -9.4% | -0.7% / -0.5% | -7.6% / -7.2% | -10.7% / -10.8% |
| 4K -> 200x113 RGB32 | -2.3% / -1.5% | -2.0% / -2.1% | -0.2% / -0.4% | -5.9% / -4.8% | -10.2% / -10.2% |
| 4K -> 200x113 RGBA32 | -1.3% / -1.1% | -1.8% / -0.9% | +3.1% / +2.5% | -2.7% / -3.8% | -3.8% / -3.9% |
| 4K -> 1080p, 3 and 4 channels | -0.2% to +0.4% | -1.3% to +1.1% | -0.8% to +0.7% | -1.4% to +1.1% | -1.1% to +1.0% |
| 4K -> 64x64 RGB32 | -9.9% / -9.9% | -9.8% / -10.0% | -1.1% / -1.0% | -8.0% / -8.0% | -11.5% / -11.4% |
| 101 MP -> 720p RGB32 | -1.8% / -2.1% | -2.2% / -2.2% | +0.3% / -0.0% | -0.9% / -1.4% | -8.9% / -8.8% |

- MSVC SSE4.1, padded: 4K -> 200x113 RGBA32 +2.8% / +2.8%, 24 MP +1.9% / +1.8%: runs with taps left after the blocks.
  At SSE4.1 the steps' pointers are formed from the index after the loop: the column loop holds 136 instructions and
  reloads its bound from the stack, against 130 and none.
- The steps' first form held their pointers in the run's pointer array. MSVC's SSE4.1 kernels, all of them, then kept
  the array on the stack and added the offset to both pointers with one `paddq`: two 8-byte stores reloaded as 16
  bytes, every column. 2.8 MP -> 1920x1280 +85%, the other downscales +3% to +8%. Written out, the two adds still
  formed the vector; two variables do not.
- clang-cl's padded control is not run: `__nop()` changes its code.

clang-cl against MSVC at 54b7a6b, from the same rounds: 1080p -> 120x68 -6.8% / -5.5% (-7.0% / -6.3% at cf1340b),
21 MP -> 175x117 -3.2% / -2.8% (-4.8% / -5.0%), 4K -> 200x113 RGB32 -3.3% / -3.0% (+0.5% / +0.3%), 24 MP -5.6% / -4.4%
(-4.1% / -4.3%), 4K -> 64x64 -0.3% / -0.3% (-2.4% / -2.3%); MSVC ahead on 720p -> 4K RGB32, +3.1% / +2.6%. At SSE4.1
clang-cl leads every 3- and 4-channel row by 1% to 12%, where at cf1340b MSVC led the thumbnails by 3-4%.

### The steps' weights set ahead, the ring's 32-bit divide, the conversion's one offset

1080p -> 120x68 at 54b7a6b, precise events: MSVC's kernel mispredicted 21.2 M times, clang-cl's 16.0 M; 4.0 M of
MSVC's at `prepareRun`'s slide check, none of clang-cl's. MSVC's 96-tap column took 13 taken branches, clang-cl's 11.
The temp ring's `row()` divided in 64 bits, about 90 M cycles at the two divides; clang-cl tests both operands and
divides in 32 bits.

With the steps' weights set ahead of the paths and the ring's divide in 32 bits (ddcfe9d): MSVC's kernel mispredicts
14 M, none at the slide check; 12 taken branches a column; 34 M cycles at the divides. The kernel's cycles level,
about 15500 M. Against 54b7a6b, three rounds without controls:

| Scenario | MSVC | MSVC, padded | clang-cl |
|---|---:|---:|---:|
| 1080p -> 120x68 RGB32 | +0.5% / -1.0% | -0.9% / -1.0% | +0.5% / +0.4% |
| 21 MP -> 175x117 RGB32 | +0.3% / +0.2% | +0.3% / +0.3% | -0.0% / -0.0% |
| 4K -> 200x113 RGB32 | -1.2% / -0.6% | +1.1% / -0.6% | +0.3% / +0.2% |
| 4K -> 64x64 RGB32 | +1.7% / +0.7% | +0.1% / +0.1% | -2.0% / -0.2% |

The conversion's one offset on top, against ddcfe9d, three rounds:

| Scenario | MSVC | MSVC, padded | clang-cl | clang-cl / MSVC after |
|---|---:|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | -0.4% / -0.4% | -0.1% / -0.3% | -1.1% / -0.9% | -3.8% / -3.4% |
| 1080p -> 240p RGB32 | -2.1% / -1.0% | -1.3% / -0.4% | -0.5% / -0.5% | -4.5% / -4.3% |
| 1080p -> 120x68 RGB32 | -2.6% / -2.0% | -2.6% / -2.7% | -0.6% / -0.6% | -3.2% / -3.2% |
| 21 MP -> 175x117 RGB32 | -0.9% / -1.0% | -0.9% / -0.9% | -0.9% / -0.4% | -2.0% / -2.1% |
| 4K -> 200x113 RGB32 | -0.8% / -1.0% | -1.2% / -0.7% | +0.4% / +0.3% | -0.7% / -1.1% |
| 4K -> 200x113 RGBA32 | -1.4% / -2.1% | -3.0% / -2.8% | +1.0% / +1.0% | -1.4% / -0.9% |
| 4K -> 1080p, 3 and 4 channels | -2.2% to -0.2% | -0.8% to +0.6% | -1.8% to +0.8% | -1.1% to +1.6% |
| 101 MP -> 720p RGB32 | -1.3% / -0.6% | -1.2% / -1.2% | -0.2% / -0.2% | -2.2% / -2.5% |

- One profile's regions vary by 100 M cycles and more between runs: the vertical tap loop read MSVC +124 M in one
  and -18 M in another. Region gaps here are averages of three.

## MSVC's column on ports 1 and 5 (open lead 11)

1080p -> 240p RGB32 at 9b6d783, `resizeRowsWith<3,4,0>` at AVX2, 1000 resizes, mean of three profiles, M cycles:

| Part of the column loop | MSVC | clang-cl | Micro-ops issued, MSVC - clang-cl |
|---|---:|---:|---:|
| Run record to pointers | 1242 | 802 | +1674 |
| First block | 1538 | 1170 | +950 |
| Block loop | 2071 | 2210 | -211 |
| Chains summed | 645 | 479 | +347 |
| Steps | 1349 | 1557 | -1199 |
| Reduction and stores | 1904 | 1889 | -188 |
| Kernel | 12339 | 11879 | +881 |

- The column runs from the micro-op cache in both builds; no store-forwarding blocks; 4K aliasing 172 M against 150 M.
- Ports per column pair: MSVC 20.7, 22.1, 27.1, 26.4 on ports 0, 1, 5, 6, loads 43.5; clang-cl 20.8, 20.4, 22.8,
  29.9, loads 39.6. Cycles 37.8 against 34.6. Four micro-ops or more executed in 60% of cycles against 67%.
- MSVC's extra in the first two parts: 10 `lea`s against 3, the four spread indices loaded, both sums zeroed.

Variants, MSVC, each against 9b6d783 in its own rounds, three each; ranges over two or three sets:

| Variant | Column | 1080p -> 240p | 24 MP | 1080p -> 120x68 |
|---|---|---:|---:|---:|
| Indices hidden (experiment 29) | 105 instructions, kernel cycles level, 2% fewer micro-ops | profiled only | | |
| And the sums zeroed in their own branch (experiment 39) | a `jmp` per column | kernel cycles level | | |
| Zero-started chains, every block in the loop, indices hidden (experiment 40) | 92 instructions, 5 `lea`s | -1.9% to -6.0% | -0.7% to +0.1% | +0.5% to +1.2% |
| The same, MSVC only, sums from the chains | 90, 5 `lea`s | -2.4% / -2.2% | +0.1% | +1.2% |
| And the pixels from a byte offset | 90, 3 `lea`s | -3.2% to -3.6% | -0.7% to +0.3% | +0.3% to +1.4% |
| The pixels from a byte offset, first block peeled, indices hidden | 104, 7 `lea`s | -3.7% / -5.0% | -2.5% / -2.4% | -1.0% / -1.3% |
| The pixels from a byte offset alone | 108, 7 `lea`s | -5.0% / -4.7% | -3.4% / -1.7% | -0.3% / -0.5% |

- Zero-started chains without the indices hidden: MSVC loads them inside the block loop, the strong downscales 12-20% slower.
- The indices built once per row group instead of per chunk: clang-cl spills them across `prepareRun`'s `memmove` and
  reloads them every column.
- The byte offset under clang-cl: registers renamed, its AVX2 rows within 1%, its SSE4.1 downscales 3-10% slower, a
  jump moved onto a 32-byte boundary (open lead 5).
- 24 MP, zero-started chains and the byte offset: the column 8% above clang-cl's in instructions and in cycles, port 5
  its surplus.
- 1080p -> 120x68, zero-started chains and the byte offset: the block loop's cycles per block level with clang-cl's, the column within 1%;
  the rest of the gap in the code per chunk (open lead 11).

## Without link-time code generation (open lead 34)

MSVC at 5a4d2c3 without against with link-time code generation, three rounds with controls, median of ratios / min to
min. AVX2, the same code: every row within 1.7%. SSE4.1: 2.8 MP -> 1920x1280 +4.4% / +4.4%, 720p -> 4K RGB24
+2.2% / +2.6%, 720p -> 4K Grayscale8 +2.0% / +5.3%, 4K -> 1080p RGB24 +1.5% / +1.2%, the other rows within 1.1%.
