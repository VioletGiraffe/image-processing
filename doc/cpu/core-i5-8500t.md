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

- Conversion is a third of a downscale. It includes reading the source from RAM: 33 MB for 4K, 404 MB for 101 MP.
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
