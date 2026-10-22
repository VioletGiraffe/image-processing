# Raspberry Pi 4

The measurement log for this machine. Sections carry the headings of [resize-performance.md](../resize-performance.md),
which holds each one's rationale.

## Machine

| | |
|---|---|
| CPU | 4x Cortex-A72, 1.8 GHz |
| Caches | 32 KB L1D, 1 MB L2 shared by all cores |
| Toolchain | Clang 22 unless noted; GCC 14.2 where named |

- Run-to-run noise: 5-10%.
- Without cooling it throttles under sustained load: `vcgencmd get_throttled` must print `0x0` after a run.

## Standing against Qt, 4f25dd1

Minimum ms of three alternating rounds of the Clang 22 and GCC 14.2 builds, each ratio against its own run's QImage.

| Scenario | Clang | QImage | Ratio | Threads | Ratio | GCC | Ratio | Threads | Ratio |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 312.6 | 155.6 | 2.01 | 94.5 | 0.61 | 337.4 | 2.16 | 99.5 | 0.64 |
| 4K -> 1080p RGB32 | 133.2 | 39.2 | 3.39 | - | - | 137.9 | 3.52 | - | - |
| 4K -> 1080p RGBA32 | 162.5 | 92.0 | 1.77 | 47.0 | 0.51 | 163.8 | 1.78 | 48.0 | 0.52 |
| 4K -> 1080p RGB24 | 135.0 | 67.9 | 1.99 | 37.6 | 0.55 | 143.2 | 2.12 | 40.0 | 0.59 |
| 4K -> 1080p Grayscale8 | 56.4 | 102.1 | 0.55 | - | - | 55.0 | 0.53 | - | - |
| 720p -> 4K RGBA32 | 84.8 | 346.7 | 0.24 | 29.2 | 0.08 | 86.6 | 0.25 | 41.5 | 0.12 |
| 720p -> 4K RGB32 | 65.1 | 89.4 | 0.73 | - | - | 74.8 | 0.84 | - | - |
| 720p -> 4K RGB24 | 64.2 | 109.6 | 0.59 | 23.8 | 0.22 | 72.7 | 0.66 | 37.6 | 0.34 |
| 720p -> 4K Grayscale8 | 22.5 | 93.1 | 0.24 | - | - | 23.7 | 0.25 | - | - |
| 1080p -> 240p | 26.7 | 10.0 | 2.66 | 7.97 | 0.80 | 28.4 | 2.83 | 8.69 | 0.87 |
| 4K -> 64x64 | 87.5 | 30.4 | 2.88 | 31.5 | 1.04 | 97.6 | 3.20 | 35.2 | 1.16 |
| 101 MP -> 720p | 1174.9 | 398.9 | 2.95 | 394.5 | 0.99 | 1310.2 | 3.29 | 431.2 | 1.08 |
| 1080p, native size | 3.19 | 3.21 | 1.00 | - | - | 3.18 | 0.99 | - | - |

- GCC is 1-15% slower than Clang single-threaded, 2% faster on 4K -> 1080p Grayscale8, and 42-58% slower on the
  threaded RGBA32 and RGB24 upscales. Not investigated.
- Against GCC at bdf65f9: 24 MP -> 1080p 409 -> 337 ms, 4K -> 1080p RGB32 180.9 -> 137.9, 101 MP -> 720p
  1451 -> 1310, 720p -> 4K RGB32 89.6 -> 74.8.
- A benchmark run takes 133 s.

## 2026-10-05, after 74c02b3: four changes, and two chains at NEON

- Before: 74c02b3. After: the output packed without the clamp, `storeTempPixelWithTaps` with scalar sums, the short-run
  pass's columns by name. (The fourth change, the SSE4.1 conversion loads, does not exist at NEON.)
- Two chains: the same with `horizontalChainCount` 2 at NEON.
- Minimum ms of three alternating rounds per compiler. The tests pass in all four changed builds.

| Scenario | GCC before | GCC after | GCC, two chains | Clang before | Clang after | Clang, two chains |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 410.9 | 404.4 | 386.4 | 385.9 | 382.9 | 380.1 |
| 4K -> 1080p RGB32 | 178.0 | 177.8 | 167.8 | 163.9 | 163.3 | 163.1 |
| 4K -> 1080p RGBA32 | 191.0 | 191.8 | 182.5 | 198.6 | 197.6 | 199.0 |
| 4K -> 1080p RGB24 | 181.4 | 182.4 | 173.2 | 164.1 | 163.8 | 164.3 |
| 4K -> 1080p Grayscale8 | 74.4 | 69.4 | 68.1 | 77.4 | 70.3 | 69.7 |
| 720p -> 4K RGBA32 | 97.2 | 89.2 | 88.9 | 100.5 | 96.3 | 97.7 |
| 720p -> 4K RGB32 | 89.5 | 84.4 | 83.5 | 87.3 | 82.7 | 83.7 |
| 720p -> 4K RGB24 | 85.2 | 79.8 | 81.5 | 84.9 | 80.6 | 81.7 |
| 720p -> 4K Grayscale8 | 36.3 | 28.7 | 28.5 | 39.6 | 29.9 | 30.1 |
| 1080p -> 1440p | 69.5 | 67.1 | 67.4 | 66.1 | 64.5 | 66.3 |
| 1080p -> 240p | 34.3 | 35.2 | 33.2 | 32.4 | 31.1 | 31.7 |
| 4K -> 64x64 | 107.1 | 107.1 | 109.7 | 93.2 | 94.6 | 102.0 |
| 101 MP -> 720p | 1427.6 | 1450.1 | 1391.3 | 1344.2 | 1332.8 | 1370.7 |
| 24 MP -> 1080p, threads | 116.7 | 119.7 | 111.0 | 108.3 | 111.1 | 106.9 |
| 4K -> 64x64, threads | 37.9 | 39.3 | 37.5 | 34.7 | 35.0 | 37.1 |
| 101 MP -> 720p, threads | 438.4 | 453.2 | 435.0 | 405.9 | 410.8 | 426.2 |

The three changes:
- Upscales -2% to -8% under both compilers; 720p -> 4K Grayscale8 -21% (GCC) and -25% (Clang); 4K -> 1080p Grayscale8
  -7% and -9%.
- GCC, 4K -> 64x64: +3.3% over five rounds of an earlier session, +6.4% in another, 0 in this one; 101 MP -> 720p +1.4%
  to +1.8% in all three. A build that kept the NEON clamp, alone of the three changes, read +1.4% and +1.0%, and lost
  the upscales' gain.
- The cause is in GCC's listing. The four-chain block for two rows is 16 accumulators and 2 weight registers, and GCC
  loads all 16 pixel vectors of a block before multiplying: 32 registers. With the clamp's constant gone the allocation
  around the kernel shifts, and the block loop stores one pixel vector to the stack and reloads it: 31 instructions
  against 29.

Two chains at NEON:
- GCC: downscales -4% to -6% against the current code (4K -> 1080p RGB32 177.8 -> 167.8), 4K -> 64x64 +2.4%.
- Clang: downscales within 1%, 4K -> 64x64 +8%, 101 MP -> 720p +3%, their threaded rows +4% to +6%.
- Not adopted: Clang, the faster compiler here on downscales, loses its long runs.

## 2026-10-22, 44dd1ce: where an upscale's time goes

Clang 22, RGB32, a loop of resizes outside Catch2. `perf record` at 10 kHz, samples summed per kernel loop
(`extract_kernels.py --loops`); event totals from `perf stat`. `vcgencmd get_throttled` 0x0 after every run.

### Fixed and per-row cost

Each series holds the dest width and the source height, so its horizontal pass makes the same temp pixels in every row.
Minimum ms of three runs, fitted as fixed + per output MP:

| Series | Temp pixels | Fixed, ms | Per output MP, ms | Largest residual, ms |
|---|---:|---:|---:|---:|
| 960x540 -> 1920 x 720, 1080, 1440 | 1.04 M | 8.7 | 6.8 | 0.2 |
| 1280x720 -> 1920 x 810, 1080, 1440, 2160 | 1.38 M | 15.9 | 5.2 | 1.5 |
| 1920x1080 -> 2560 x 1200, 1440, 2160, 2880 | 2.76 M | 28.9 | 6.2 | 0.8 |
| 1280x720 -> 3840 x 900, 1440, 2160 | 2.76 M | 29.6 | 4.9 | 2.4 |

- The 2.76 M series ran as Catch2 benchmark sections, 10 samples a run, with QImage's control beside each row.
- QImage's smooth upscale: 10.8 ns per output pixel on every row, its fixed part within 0.3 ms of zero.
- The fits overstate the fixed part: the vertical tap loop skips zero weights, and the larger the factor, the more dest
  rows fall on a source row (weights 0, 1, 0, 0).
- Destinations over 32 MiB cost both 4.4 ns more per pixel ("Reading the numbers"): left out of the fits.

### 1280x720 -> 1920x1080, per kernel loop

| Part | ms | Per unit | Unit | Instructions per cycle |
|---|---:|---:|---|---:|
| Horizontal taps | 9.1 | 6.6 ns | temp pixel | 1.21 |
| Conversion | 4.2 | 4.55 ns | source pixel | 0.48 |
| Vertical pass | 14.1 | 6.8 ns | dest pixel | 1.20 |
| Weights, allocation, the rest | 0.2 | | | |

- Horizontal: 6.5-6.7 ns per temp pixel whether the source comes from DRAM or L2. Its samples fall on the loads (run
  index, weights, four 32-byte `ldp`) and the two 16-byte stores, almost none on the multiply-adds.
- Every temp line the horizontal pass writes is an L1 write refill: 0.25 M per resize.
- The horizontal loop's bound: [throughput-ceiling.md](../throughput-ceiling.md), "An upscale's 4-tap runs at NEON".
- Conversion: 60% of its samples on the source load, and 93% of the kernel's L2 refills, one per source line. With
  source and destination in L2 (240x136 -> 360x204, 14 L2 refills per resize) it costs 2.7 ns per source pixel.
- 1920x2160 out: the vertical pass 5.6 ns per dest pixel, a third of the rows on a single tap.

## The next chunk's source prefetched

Clang 22, the loop of resizes from the section above, five alternating rounds, median of same-round ratios / minimum to
minimum, against 44dd1ce.
- Next chunk: the kernel's form.
- Next row pair too: also the next row pair's first chunk, when a pair converts its last. Not committed.

| Scenario | 44dd1ce, min ms | Next chunk | Next chunk and row pair |
|---|---:|---:|---:|
| 960x540 -> 1920x1080 | 23.08 | -4.3% / -4.5% | -4.6% / -4.5% |
| 1280x720 -> 1920x810 | 23.25 | -5.4% / -4.8% | -5.3% / -5.0% |
| 1280x720 -> 1920x1080 | 26.79 | -4.8% / -5.2% | -4.9% / -4.6% |
| 1280x720 -> 1920x1440 | 31.54 | -5.2% / -4.3% | -5.1% / -4.3% |
| 1080p -> 1440p | 50.32 | -5.2% / -4.9% | -4.5% / -4.8% |
| 720p -> 4K | 70.00 | -3.5% / -3.4% | -3.3% / -3.2% |
| 1080p -> 720p | 40.97 | -5.3% / -3.7% | -4.9% / -3.4% |
| 1080p -> 240p | 28.44 | -9.3% / -7.9% | -8.8% / -6.8% |
| 4K -> 1080p | 137.76 | -4.4% / -2.9% | -5.0% / -3.6% |

- A first session of five rounds, the next chunk alone: -3.4% to -6.4% on the same rows.
- At 720p -> 1080p the next chunk takes 1.3-1.4 ms off the conversion's 1.7 ms over its L2-resident rate (the section
  above).
- The next row pair adds nothing: within 1.2% of the next chunk alone on every row.

Grayscale8, the next chunk against 44dd1ce in a later session, with an RGB32 row as its cross-check:

| Scenario | 44dd1ce, min ms | Next chunk |
|---|---:|---:|
| 960x540 -> 1920x1080 | 7.40 | +0.0% / -0.1% |
| 1280x720 -> 1920x1080 | 9.27 | -4.7% / -4.7% |
| 720p -> 4K | 22.39 | -3.6% / -0.1% |
| 1080p -> 720p | 18.15 | -6.5% / -6.6% |
| 4K -> 1080p | 58.16 | -5.6% / -5.0% |
| 1280x720 -> 1920x1080 RGB32 | 26.79 | -4.4% / -5.2% |

The committed form, the full benchmark, three alternating rounds against 44dd1ce, `vcgencmd get_throttled` 0x0 after:
- Every filtered row faster, the threaded ones included: -1.4% to -9.9% by median, -1.4% to -7.7% minimum to minimum.
  24 MP -> 1080p -5.9%, 101 MP -> 720p -8.1%, 4K -> 64x64 -9.9%, 720p -> 4K RGB32 -3.7%.
- Nearest neighbor, which converts nothing: +1.9% to -0.9%, the threaded rows +2.5% to -0.4%.

## 4-tap runs of 4-float pixels in one chain per row

Clang 22, the loop of resizes from "where an upscale's time goes", against 901d72e: five alternating rounds for RGB32,
three for RGBA32. Median of same-round ratios / minimum to minimum. `vcgencmd get_throttled` 0x0 after both.

| Scenario | 901d72e, min ms | One chain |
|---|---:|---:|
| 960x540 -> 1920x1080 | 22.07 | -4.1% / -4.9% |
| 1280x720 -> 1920x810 | 22.12 | -5.5% / -5.5% |
| 1280x720 -> 1920x1080 | 25.60 | -5.6% / -5.7% |
| 1280x720 -> 1920x1440 | 30.19 | -3.9% / -3.8% |
| 1080p -> 1440p | 48.02 | -5.5% / -5.5% |
| 720p -> 4K | 67.62 | -3.5% / -3.7% |
| 1080p -> 720p | 39.38 | +0.2% / +0.3% |
| 1080p -> 240p | 26.47 | +2.8% / +3.3% |
| 4K -> 1080p | 133.62 | -0.1% / 0.0% |
| 1280x720 -> 1920x1080 RGBA32 | 32.11 | -3.5% / -2.5% |
| 1080p -> 1440p RGBA32 | 64.94 | -4.7% / -4.4% |
| 720p -> 4K RGBA32 | 86.26 | -0.3% / +0.3% |

- The downscales do not run the changed code. 1080p -> 240p's 901d72e rounds split between 26.5 and 27.7-28.1 ms; the
  one-chain build's held 27.3-27.6.

## Per-instruction-set primitives, not SIMDe (963b54b)

Clang, 6b6bf7e -> 3049bb0: upscales -6% to -13%, downscales -4% to -9%; the scalar rows within 4%.

## A sliding source buffer, not whole converted rows (a9756cb)

SIMD ms: 7919eaf / whole rows (d436d41) / sliding (a9756cb, chunk 64):

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

## Conversion chunk of 128 pixels (1ecd630)

SIMD ms, chunk 64 (2 runs) / 256 (2 runs) / 128, single-threaded:

| Scenario | 64 | 256 | 128 |
|---|---|---|---|
| 24 MP -> 1080p | 476, 487 | 467, 460 | 458 |
| 4K -> 1080p | 200, 203 | 225, 198 | 196 |
| 1080p -> 1440p | 78.0, 80.9 | 78.4, 77.2 | 76.7 |
| 4K -> 64x64 | 114, 118 | 105, 104 | 110 |
| 101 MP -> 720p | 1680, 1713 | 1684, 1664 | 1683 |

The same with threads:

| Scenario | 64 | 256 | 128 |
|---|---|---|---|
| 24 MP -> 1080p | 232, 230 | 251, 242 | 236, 235 |
| 4K -> 1080p | 125, 123 | 133, 132 | 128, 127 |
| 1080p -> 1440p | 23.9, 23.2 | 26.0, 25.9 | 24.0, 22.6 |
| 4K -> 64x64 | 43.1, 41.3 | 40.6, 41.7 | 39.6, 39.5 |
| 101 MP -> 720p | 587, 585 | 595, 587 | 558, 583 |

- 256 costs 5-10% with threads, likely from L1 pressure: more buffer next to a 23 KB temp row in a 32 KB L1.
- 128 matches 64 with threads, and matches or beats 256 single-threaded.

Net, 7919eaf -> 1ecd630, ms:

| Scenario | Single-threaded | Threads |
|---|---|---|
| 24 MP -> 1080p | 542 -> 458 | 246 -> 235 |
| 4K -> 1080p | 220 -> 196 | 124 -> 125 |
| 101 MP -> 720p | 2023 -> 1683 | 639 -> 578 |
| 4K -> 64x64 | 129 -> 110 | 45.6 -> 42.7 |
| 1080p -> 1440p | 81 -> 77 | 22.6 -> 22.6-25.3 |

## Straight alpha premultiplied in the kernel (003d5e6)

- Qt's 4K -> 1080p controls: RGBA32 87.9 ms, RGB32 40.2 ms.
- Premultiplying in the kernel costs about 5% single-threaded: 4K -> 1080p RGBA32 ratio 2.36 at 1ecd630, 2.47 at 1199012.

## 128-bit packs for the output bytes (e16c6c4)

Resizer / QImage, before -> after. Both builds called `prepareRun` out of line.

| Scenario | |
|---|---|
| 720p -> 4K RGBA32 | 0.39 -> 0.37 |
| 720p -> 4K RGBA32, threads | 0.31 -> 0.28 |
| 1080p -> 1440p | 2.10 -> 2.01 |

Downscales stayed within about 4%.

## Forced inlining on every platform (1199012)

Single-threaded resizer / QImage. Columns: 1ecd630 / f116725 (out-of-line call) / 1199012 (inlined, with the 128-bit packs):

| Scenario | Resizer / QImage |
|---|---|
| 24 MP -> 1080p | 2.94 / 3.11 / 2.97 |
| 4K -> 1080p RGB32 | 4.96 / 5.24 / 4.83 |
| 1080p -> 1440p | 1.92 / 2.10 / 1.85 |
| 4K -> 64x64 | 3.62 / 3.63 / 3.41 |
| 101 MP -> 720p | 4.32 / 4.47 / 4.29 |

## One outline for every pixel layout (e5ea70d)

- Clang, resizer / QImage, 6b6bf7e -> e5ea70d: Grayscale8 1.19 -> 0.74 and 0.59 -> 0.44, RGB24 3.76 -> 2.75 and
  1.09 -> 0.77.
- GCC against Clang at e5ea70d: 7-17% slower on downscales.

## The run lookup holds its array pointers by value (f2f4569, 03113a6)

`flatten` under GCC 14.2, 14e731e against 74b015c's kernels, one run of 30 samples each: 24 MP -> 1080p 480 -> 451 ms,
1080p -> 240p 40.1 -> 38.0. 720p -> 4K RGBA32 unchanged, 98.4 -> 97.7-99.2. Other rows within 1.5%.

## Column strips (b9fea7e)

- L2 share per logical processor 256 KB (1 MB, four cores), ring budget 128 KB.
- Full-width rings overflowed the shared L2 with four threads. Per thread, ring plus float row plus accumulator row came
  to about 350 KB for 720p -> 4K RGBA32, 390 KB for 4K -> 1080p RGB24, 500 KB for 24 MP -> 1080p, 1.2 MB for 101 MP -> 720p.

PMU counters, whole process, "Parallel resize" reduced to 720p -> 4K RGBA32, 8ca3148 (two passes through a whole-image
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

ms with threads, 2b371a2 -> b9fea7e. Scalar also against 8ca3148. Speedup: single-threaded / threaded time, with strips.

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
- Single-threaded about neutral: 101 MP SIMD gains 9-12% (its ring overflowed the L2 even alone), 4K -> 64x64 SIMD
  loses 8%.

## The pass's steps looped over the rows (93775b0)

GCC 14.2, 4K -> 1080p Grayscale8, one run each; the QImage control 106.8-109.5 ms throughout.

| | ms |
|---|---:|
| 1eb77df, each step written per row | 78.4 |
| 93775b0, each step in a loop over the rows | 103.5 |
| 93775b0 with `storeTempPixelWithTaps` called per row | 78.3 |

- GCC unrolled every row loop but the one around `storeTempPixelWithTaps`, whose body holds the scalar tap loop: an outer
  loop. It kept both rows' sums, source pointers and temp-row pointers in memory for it.
- Only 1- and 2-float pixels take that step. Clang and MSVC unrolled it.
- The CI's GCC jobs showed the same at 93775b0: +31% on Neoverse-N2, +43% at AVX2 and +50% at SSE4.1 on the EPYC 7763.

## Two vertical blocks per step at AVX2 (b8bdd30)

NEON keeps one block. GCC 14.2 and Clang 22: the nine kernels' sizes unchanged, every row within 2.2% over three
rounds, 4K -> 64x64 with threads under Clang (-6.7%) aside.

## The NEON kernel file without LTO, and a floor on the strip width (b8bdd30)

Three alternating rounds each.
- Without LTO the kernels are 0.2-5% smaller under GCC and within 2% under Clang, and call `TempRowRing`'s constructor
  where LTO inlines it. GCC's rows stay within 1.7%, 4K -> 1080p RGBA32 at +2.7%; Clang's within 3%, 4K -> 64x64 at
  +6.1% with its QImage control 3.6% apart.
- A floor of 64 dest columns per strip: 4K -> 64x64 -7.4% under GCC and -6.0% with threads, +0.2% and +3.7% under
  Clang. A floor of 32 under GCC: -5.7% and -3.6%. No other benchmark has strips that narrow.

## The horizontal filter's batched loop, and the destination's stores

Ten alternating rounds, median of same-round ratios, against 93b1b79. With one column per preparation, as NEON
shipped first, the sources were within 2.5% under GCC, 1080p -> 1440p with threads at +3.8%, and within 1.5% under Clang.

The batched loop, as AVX2 and SSE4.1 have it:

| Scenario | GCC | Clang | GCC, `STNP` output stores | Clang, `STNP` output stores |
|---|---:|---:|---:|---:|
| 3- and 4-channel 4K downscales | -0.5% to -1.6% | -1.7% to +0.8% | -0.6% to -2.9% | -1.4% to +0.9% |
| 720p -> 4K RGBA32 | +2.9% | -4.7% | +3.2% | -2.3% |
| 720p -> 4K RGB32 | +4.1% | -2.8% | -1.0% | -2.5% |
| 1080p -> 1440p RGB32 | +1.9% | -2.9% | -1.6% | -3.1% |
| 720p -> 4K RGBA32, threads | +72% | -2.7% | +6.3% | +4.1% |
| 1080p -> 1440p RGB32, threads | +33% | -6.5% | +1.4% | +3.5% |
| 720p -> 4K RGB24, threads | +37% | -2.1% | +38% | -0.3% |

`perf stat` over 720p -> 4K RGBA32 single-threaded under GCC, a run of 17 resizes, in million lines:

| Build | L2 refills | L2 write-backs |
|---|---:|---:|
| One column per preparation | 17-19 | 8-9.5 |
| Batched loop | 28-29 | 18-19 |
| One column per preparation, no vertical pass | 17.8 | 7.6 |
| Batched loop, no vertical pass | 18.3 | 8.9 |
| Batched loop, `STNP` output stores | 15.7 | 6.3 |

- A run writes 8.8 million destination lines. The vertical pass adds 1-2 million refills and write-backs to the first
  build and 9-10 million to the batched one: the first build's destination stores bypass L2.
- The two builds' vertical loops store with the same instructions, and their horizontal passes read and write the same
  addresses in the same order.
- With the bypass lost, the write-backs sampled inside the kernel fall on the vertical pass's temp-row loads.
- A one-column loop that went through `filterBufferedRuns` lost the bypass as well: 1080p -> 1440p RGB32 with threads
  +41% under GCC.
- `STNP` here covers the 32-byte stores of RGBA32 and RGB32 only. Single-threaded 720p -> 4K RGBA32 under GCC: 106 ms
  with it, 99 ms where the bypass engages without it.

The short-run pass's groups filtered from the range by value, ten rounds of the pixel-layout rows against 1917611:

| | GCC | Clang |
|---|---:|---:|
| 720p -> 4K Grayscale8 | -8.9% | -11.8% |
| 4K -> 1080p Grayscale8 | -0.1% | +2.7% |
| L2 refills, write-backs on 720p -> 4K Grayscale8 | 15.6 M -> 15.1 M, 7.5 M -> 7.1 M | 16.7 M -> 15.8 M, 8.8 M -> 7.7 M |

The bypass is kept, single-threaded; no threaded one-channel row was measured.

### `STNP` on every block store, and the batched loop with it

Eight alternating rounds against 1ed9965. "Stores": `STNP` with the per-column loop. "Both": with the batched loop.

| Scenario | GCC, stores | GCC, both | Clang, stores | Clang, both |
|---|---:|---:|---:|---:|
| 4K -> 1080p RGB32 | -0.3% | -2.0% | -0.6% | -0.6% |
| 4K -> 1080p RGBA32 | +0.7% | -1.2% | -1.8% | -1.6% |
| 4K -> 1080p Grayscale8 | -0.4% | -4.2% | +0.2% | -3.0% |
| 24 MP -> 1080p | -1.6% | -3.1% | +1.2% | -0.7% |
| 720p -> 4K RGBA32 | +2.6% | +2.7% | +1.8% | -1.1% |
| 720p -> 4K RGB32 | +0.2% | +0.4% | -0.5% | -2.3% |
| 1080p -> 1440p RGB32 | -2.2% | -2.8% | -0.9% | -3.5% |
| 720p -> 4K RGBA32, threads | -0.2% | +6.4% | +3.0% | +0.3% |
| 1080p -> 1440p RGB32, threads | +7.3% | +5.3% | +5.0% | +3.0% |
| 4K -> 1080p RGBA32, threads | -3.3% | -3.2% | +0.3% | +0.4% |

RGB24, whose 24-byte block is 16 + 8 bytes. "16": `STNP` on the 16 only. "24": on both, the 8 as two 4-byte halves.
Both with the batched loop; the second pair of columns from a second run of eight rounds.

| Scenario | GCC, 16 | GCC, 24 | Clang, 16 | Clang, 24 |
|---|---:|---:|---:|---:|
| 720p -> 4K RGB24 | +18.0% | +2.9% | -2.7% | -1.9% |
| 720p -> 4K RGB24, threads | +86.8% | +11.6% | -23.6% | -17.0% |
| 4K -> 1080p RGB24 | +0.8% | +0.1% | -0.3% | -0.9% |
| 4K -> 1080p RGB24, threads | +2.1% | +1.2% | -0.3% | +1.1% |

- Threaded 720p -> 4K RGB24 at 1ed9965: 23.7 ms under GCC, 30.8 under Clang, whose build lacks the bypass there.
- The committed form was not timed: it differs from "both" with "24" in the odd-stride path's plain stores, which no
  benchmark row takes.

## Short x runs padded to 4 taps, and their passes

Eight alternating rounds against eeb1af6. "Loop": the 4-float pass with its row pair in a loop. "Named": the committed
sources, the rows by name.

| Scenario | GCC, loop | GCC, named | Clang, loop | Clang, named |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -2.4% | -1.9% | -5.6% | -7.7% |
| 720p -> 4K RGB32 | +6.4% | -6.3% | -6.3% | -5.6% |
| 720p -> 4K RGB24 | +6.7% | -7.1% | -6.7% | -7.9% |
| 1080p -> 1440p RGB32 | +6.1% | -9.4% | -10.1% | -11.3% |
| 720p -> 4K Grayscale8 | +0.3% | -9.4% | -0.3% | -10.0% |
| 720p -> 4K RGBA32, threads | -0.7% | +1.4% | +3.2% | -1.4% |
| 720p -> 4K RGB24, threads | +9.2% | -2.3% | -3.6% | -4.9% |
| 1080p -> 1440p RGB32, threads | +1.2% | -10.0% | -4.6% | -4.5% |
| Downscale rows | -4.5% to +0.4% | -0.8% to +1.3% | -5.3% to +3.3% | -5.5% to +2.3% |

- "Loop" kept the one-channel pass's masking and padded every x run to a multiple of 4.
- GCC's loop over the row pair stays rolled: both row pointers and both temp pointers reloaded from the stack per row.
- Either way a 3-float temp pixel goes through the stack: a 16-byte store, then 8 and 4 bytes loaded and stored.

The committed sources, the kernel compiled once per kind of x run, in a second run of eight rounds beside "named":

| Scenario | GCC, named | GCC, committed | Clang, named | Clang, committed |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32, RGB32, RGB24 | -2.8%, -6.2%, -6.8% | -2.8%, -6.0%, -7.0% | -7.3%, -5.7%, -7.7% | -7.2%, -5.7%, -6.0% |
| 1080p -> 1440p RGB32 | -9.8% | -10.0% | -10.6% | -11.1% |
| 720p -> 4K Grayscale8 | -8.9% | -8.3% | -11.5% | -11.3% |
| 720p -> 4K RGBA32, RGB24, 1080p -> 1440p with threads | +8.9%, -4.2%, -0.2% | +6.9%, -4.0%, -4.5% | -4.2%, -5.2%, -6.4% | -5.2%, -5.2%, -7.4% |
| Downscale rows | -0.6% to +4.1% | -2.3% to +2.9% | -4.3% to +1.7% | -4.5% to +2.4% |

GCC's threaded 720p -> 4K RGBA32 read +1.4% for "named" in the first run and +8.9% in this one.

## Output bytes capped at alpha after packing

Eight alternating rounds against e60816d: median of same-round ratios / minimum to minimum. Only rows with alpha run
changed code.

| Scenario | GCC | Clang |
|---|---:|---:|
| 720p -> 4K RGBA32 | -1.9% / -2.5% | +0.4% / -1.1% |
| 720p -> 4K RGBA32, threads | -3.4% / -3.6% | -2.3% / +1.7% |
| 4K -> 1080p RGBA32 | +0.2% / -0.0% | -0.8% / +0.8% |
| 4K -> 1080p RGBA32, threads | +2.6% / +0.4% | +1.1% / +1.8% |
| 1080p -> 1440p RGB32, threads (code unchanged) | +6.5% / +3.5% | +6.3% / -0.1% |
| Other rows (code unchanged) | -1.4% to +1.1% | -4.7% to +1.1% |

## Accumulators updated in place

Eight alternating rounds against 64af30d, `vcgencmd get_throttled` 0x0 after them. No row moves by both measures:

| | GCC | Clang |
|---|---:|---:|
| Median of same-round ratios | -2.4% to +3.8% | -5.2% to +4.3% |
| Minimum to minimum | -1.6% to +2.7% | -3.1% to +1.1% |

The largest medians: GCC's 4K -> 64x64 +3.8% (minimum +0.5%); Clang's threaded 4K -> 1080p RGBA32 -5.2% (+0.5%) and
720p -> 4K RGBA32 +4.3% (+1.1%).

## The vertical taps: a list, and the first tap assigned

Single-threaded display and layout rows against 3e0a1ea, median of same-round ratios. "List": the nonzero taps listed
per dest row, the first tap assigned. "Assign only": the first tap assigned, the rows walked as before. Eight rounds;
the 32-byte column four. `vcgencmd get_throttled` 0x0 after every round.

| Scenario | GCC list, 32-byte | GCC list, 16-byte | GCC list, `float` | GCC assign only | Clang list, 32-byte | Clang list, 16-byte | Clang list, `float` | Clang assign only |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | +3.3% | -3.7% | -2.9% | -0.8% | +5.5% | +4.5% | +7.6% | -2.0% |
| 720p -> 4K RGB32 | +18.0% | -3.4% | -3.9% | +2.9% | +5.0% | +3.7% | +4.3% | -4.3% |
| 720p -> 4K RGB24 | +19.2% | -0.2% | -0.3% | +5.5% | +2.7% | +4.6% | +4.8% | -3.1% |
| 720p -> 4K Grayscale8 | +7.4% | -2.6% | -2.1% | +0.2% | +5.5% | +4.1% | +6.7% | -1.3% |
| 1080p -> 1440p RGB32 | +13.9% | +0.9% | +2.7% | -0.4% | +4.4% | +5.0% | +6.0% | -2.4% |
| 4K -> 1080p RGB32 | +7.6% | +1.6% | +0.5% | +0.7% | +2.3% | +5.3% | +3.5% | -0.6% |
| 4K -> 1080p RGB24 | +7.3% | +1.6% | +1.3% | +0.5% | +3.9% | +3.2% | +3.7% | -0.8% |
| 4K -> 1080p RGBA32 | +7.4% | +1.9% | +0.5% | +0.4% | +2.6% | +2.1% | +4.5% | -0.1% |
| 24 MP -> 1080p | +8.6% | +1.2% | +0.9% | +0.1% | +2.3% | +5.9% | +3.3% | +0.4% |

- Two forms were timed twice, four rounds then eight: within a point on every row.
- The committed form, eight rounds, equals "assign only" within a point: GCC's 720p -> 4K RGB32 +3.4% and RGB24 +5.5%,
  Clang's upscales -3.0% to -4.6%, every downscale within 1.7%.

The 3-channel tap loop, per tap, from the listings:

| Build | Instructions | Loads | Multiply-adds | Branches |
|---|---:|---:|---:|---:|
| Clang, 3e0a1ea | 16 | weight, 3 `ldp` | 6, by the weight's lane | 3 |
| Clang, `float` list | 14 | row pointer, weight by `ld1r`, 3 `ldp` | 6, by vector | 1 |
| Clang, 16-byte list | 13 | row pointer, weight, 3 `ldp` | 6, by vector | 1 |
| GCC, 3e0a1ea | 16 | weight, 3 `ldp` | 6, by the weight's lane | 2 |
| GCC, `float` list | 16 | row pointer, weight, 2 `ldr` and 2 `ldp` | 6, by the weight's lane | 1 |

An `ldp` of two Q registers is two loads: 7 per tap without a list, 8 with one. Clang inlines both kernels of a pixel
layout into its `resizeRows`.

## The conversion's tail loop kept scalar under Clang

Clang, eight alternating rounds without the controls against 17d2fa0: median of same-round ratios / minimum to minimum.

| Scenario | |
|---|---:|
| 1080p -> 240p RGB32 | -5.7% / -4.8% |
| 24 MP -> 1080p, 4K -> 64x64, 101 MP -> 720p | -2.3%, -2.8%, -2.9% / -1.8%, -2.2%, -1.2% |
| 4K -> 1080p RGB32, two sections | -2.7%, -1.6% / -2.0%, -1.1% |
| 4K -> 1080p RGB24, RGBA32, Grayscale8 | -0.6%, -0.1%, 0.0% / -0.9%, +0.1%, +0.1% |
| 720p -> 4K RGBA32, RGB32, RGB24, Grayscale8 | -0.7%, 0.0%, +0.8%, +0.1% / -0.9%, -1.2%, +1.9%, 0.0% |
| 1080p -> 1440p RGB32 | -0.1% / -1.2% |
| Threaded rows | -3.3% to +3.2% / -5.1% to +2.0% |

The kernels' instructions, all nine functions: 29184 -> 19660. `vcgencmd get_throttled` printed `0x0` after the run.

## `filterRun`'s last taps as explicit steps (against cbbd718, not committed)

GCC 14.2, five alternating rounds pinned to one core, 81 s a run: median of same-round ratios / minimum to minimum.

| Scenario | cbbd718, min ms | Explicit steps |
|---|---:|---:|
| 4K -> 1080p Grayscale8 | 67.65 | +6.5% / +6.4% |
| 1080p -> 240p RGB32 | 34.69 | +3.2% / +2.4% |
| 24 MP -> 1080p RGB32 | 399.81 | +1.1% / +2.3% |
| 4K -> 64x64 RGB32 | 108.39 | +0.4% / -3.7% |
| The other eleven rows | | within 1% |

Both builds pass the tests.

## Experiments that lost

**2. An early return and register-held span state in `prepareRun`.** Both together cost 2-5% single-threaded and nothing
with threads. Clang already keeps the fields in registers, so the locals only add register pressure.

**4. Chunks of 32 and 16.** Not measured: with 5-10% noise, telling them apart from 64 would take several runs of each.
