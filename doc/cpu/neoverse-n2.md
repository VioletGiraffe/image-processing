# Neoverse N2 (the ubuntu-24.04-arm runner)

The measurement log for this CPU. Sections carry the headings of [resize-performance.md](../resize-performance.md), which
holds each one's rationale.

## Machine

| | |
|---|---|
| CPU | Cobalt 100 (Neoverse N2), 4 cores |
| Caches | 1 MB private L2 per core |
| Toolchain | GCC and Clang CI jobs |

Run-to-run noise under Clang: about 1%.

## Per-instruction-set primitives, not SIMDe (963b54b)

- With SIMDe, resizer / QImage, 24 MP -> 1080p single-threaded, at d436d41: GCC 4.95, Clang 2.30.
- 9c5c6ac and 4b5fdf8 -> 963b54b, SIMD rows:
  - GCC: -40% to -55% everywhere. It is now within 2-22% of Clang, down from about 2x.
  - Clang: upscales -12% to -23%, downscales within 3%.

## Source rows converted to float once (d436d41)

Resizer / QImage single-threaded, f435b80 -> d436d41:

| Job | 24 MP -> 1080p | 4K -> 1080p | 4K -> 64x64 | 101 MP -> 720p |
|---|---|---|---|---|
| Clang | 5.14 -> 2.30 | 7.42 -> 3.41 | 6.49 -> 2.47 | 7.05 -> 2.89 |
| GCC | 6.46 -> 4.95 | 9.78 -> 7.32 | 7.07 -> 5.47 | 8.91 -> 6.06 |

## A sliding source buffer, not whole converted rows (a9756cb)

Resizer / QImage single-threaded under Clang, whole rows (d436d41) -> sliding (1ecd630):

| Scenario | Whole rows -> sliding |
|---|---|
| 4K -> 1080p | 3.41 -> 3.64 |
| 1080p -> 1440p | 1.60 -> 1.79 |
| 4K -> 64x64 | 2.47 -> 3.09 |

Threads were even.

## 128-bit packs for the output bytes (e16c6c4)

Clang, resizer / QImage, before -> after. Both builds called `prepareRun` out of line.

| Scenario | |
|---|---|
| 720p -> 4K RGBA32 | 0.31 -> 0.28 |
| 720p -> 4K RGBA32, threads | 0.081 -> 0.072 |
| 1080p -> 1440p | 2.07 -> 1.95 |

Downscales stayed within about 4%.

## Forced inlining on every platform (1199012)

Clang, 1ecd630 -> 003d5e6, once `prepareRun` went out of line: 24 MP 2.32 -> 2.58, 4K -> 1080p 3.64 -> 4.09,
1080p -> 1440p 1.79 -> 2.07, 4K -> 64x64 3.09 -> 3.26.

## The run lookup holds its array pointers by value (f2f4569, 03113a6)

- GCC, f04db84 -> 03113a6, 6 and 4 runs: 4-byte rows -1% to -5%, Grayscale8 -8% and -5%, 720p -> 4K RGB24 -16%.
- Clang: within 2%.
- `flatten` under GCC (74b015c and later, ten runs against seven): 24 MP -> 1080p 92.4 -> 78.3 ms, 1080p -> 240p
  8.00 -> 6.60, 101 MP -> 720p 288 -> 261, level with Clang. Other rows within 5%, 720p -> 4K RGBA32 aside.
  Under Clang: within 2.5%.
- 720p -> 4K RGBA32 under GCC in the same runs: 22.2 ms before, 24.0-24.3 at 74b015c and 15f240e (seven runs), 21.9 at
  9811e5b (three runs), which changed only the benchmark's source. The row follows the binary's layout, not the kernel's
  code.

## One-channel upscales filter four columns at a time (0bd90e4)

720p -> 4K Grayscale8 at 0bd90e4 against the three commits before it: Clang -14%, GCC -2%.

## Column strips (b9fea7e)

Full-width rings already fit the L2, so only the strips' overhead shows. Clang, 2b371a2 -> 29b97e9, 3-4 samples per side:
- SIMD +1-5% single-threaded.
- Scalar downscales +5-11%, scalar upscales -5-6%.
- With threads within 4%, except scalar 24 MP and 101 MP: +8-9%.
- With the pre-touch (6b6bf7e): within 5% of strips alone.

## CI, 0bd90e4 to 8c9e194 (2026-10-04, 2026-10-05)

Median ms per commit, with the sample count in the header. "-": no run of that commit landed on this CPU.
- Baseline: 0bd90e4, b8892fc and cc975cf, the same code.
- 84bbe28: two chains at SSE4.1, and the first block assigned at every level.
- 16cdfbd: runs a period apart share their weights.
- 1eb77df: the 8-float step for every pixel layout.
- 93775b0: the pass's steps looped over the rows.
- bdf65f9: `storeTempPixelWithTaps` called per row.
- 8c9e194: `[[likely]]` on the leftover-tap steps.

GCC 14.2:

| Scenario | Baseline (5) | 84bbe28 (2) | 16cdfbd (3) | 1eb77df (2) | 93775b0 (2) | bdf65f9 (2) | 8c9e194 (3) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 78.81 | 75.64 | 73.22 | 71.00 | 69.36 | 69.53 | 69.40 |
| 4K -> 1080p RGB32 | 35.87 | 32.74 | 30.50 | 30.33 | 29.99 | 29.98 | 30.50 |
| 4K -> 1080p RGBA32 | 41.46 | 37.65 | 35.75 | 35.95 | 35.71 | 35.95 | 35.56 |
| 4K -> 1080p RGB24 | 35.57 | 33.15 | 30.84 | 30.86 | 30.51 | 30.57 | 30.57 |
| 4K -> 1080p Grayscale8 | 15.14 | 14.99 | 13.86 | 13.75 | 18.00 | 13.88 | 13.76 |
| 1080p -> 240p | 6.61 | 6.39 | 6.07 | 6.10 | 5.93 | 5.92 | 5.92 |
| 4K -> 64x64 | 20.39 | 19.38 | 17.48 | 16.94 | 17.27 | 17.21 | 17.33 |
| 101 MP -> 720p | 260.5 | 252.2 | 250.7 | 250.9 | 248.6 | 248.8 | 247.3 |
| 720p -> 4K RGB32 | 19.07 | 18.96 | 18.83 | 19.09 | 18.71 | 18.82 | 18.80 |
| 720p -> 4K RGBA32 | 22.05 | 21.95 | 22.27 | 22.42 | 22.19 | 22.23 | 22.19 |
| 720p -> 4K RGB24 | 17.07 | 17.11 | 17.11 | 17.20 | 17.19 | 17.15 | 17.04 |
| 720p -> 4K Grayscale8 | 8.39 | 8.43 | 8.30 | 8.27 | 8.24 | 8.27 | 8.22 |
| 1080p -> 1440p | 14.66 | 14.43 | 14.70 | 14.51 | 14.52 | 14.59 | 14.38 |
| 24 MP -> 1080p, threads | 21.36 | 20.47 | 19.16 | 18.31 | 17.98 | 18.15 | 18.05 |
| 4K -> 1080p RGBA32, threads | 11.38 | 10.16 | 9.11 | 9.17 | 9.26 | 9.23 | 9.12 |
| 4K -> 1080p RGB24, threads | 9.77 | 8.87 | 8.00 | 7.87 | 7.81 | 7.89 | 7.79 |
| 1080p -> 240p, threads | 2.13 | 2.07 | 1.70 | 1.71 | 1.74 | 1.67 | 1.65 |
| 4K -> 64x64, threads | 6.90 | 6.49 | 5.67 | 5.54 | 5.57 | 5.59 | 5.39 |

Clang 18.1.3:

| Scenario | Baseline (5) | 84bbe28 (2) | 16cdfbd (3) | 1eb77df (2) | 93775b0 (2) | bdf65f9 (2) | 8c9e194 (3) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 75.10 | 76.89 | 74.19 | 72.15 | 72.03 | 72.11 | 71.79 |
| 4K -> 1080p RGB32 | 33.15 | 32.85 | 29.89 | 29.67 | 29.78 | 29.68 | 29.89 |
| 4K -> 1080p RGBA32 | 40.00 | 39.06 | 36.76 | 36.64 | 36.47 | 36.21 | 36.65 |
| 4K -> 1080p RGB24 | 34.82 | 34.67 | 30.87 | 31.06 | 30.66 | 30.73 | 30.93 |
| 4K -> 1080p Grayscale8 | 16.00 | 16.10 | 14.37 | 14.36 | 14.30 | 14.36 | 14.37 |
| 1080p -> 240p | 6.44 | 6.53 | 6.08 | 6.05 | 6.09 | 6.22 | 6.21 |
| 4K -> 64x64 | 20.48 | 21.11 | 18.34 | 18.68 | 18.28 | 18.45 | 19.62 |
| 101 MP -> 720p | 258.2 | 259.1 | 258.1 | 255.6 | 255.9 | 254.7 | 255.6 |
| 720p -> 4K RGB32 | 18.18 | 18.48 | 18.16 | 18.28 | 18.22 | 18.31 | 18.42 |
| 720p -> 4K RGBA32 | 22.57 | 22.84 | 22.03 | 22.44 | 22.22 | 22.30 | 22.46 |
| 720p -> 4K RGB24 | 16.98 | 17.36 | 16.95 | 17.31 | 17.17 | 17.24 | 17.43 |
| 720p -> 4K Grayscale8 | 7.54 | 7.94 | 7.73 | 7.72 | 7.74 | 7.74 | 7.75 |
| 1080p -> 1440p | 13.73 | 13.85 | 13.63 | 13.77 | 13.44 | 13.46 | 13.72 |
| 24 MP -> 1080p, threads | 20.46 | 20.84 | 19.04 | 18.69 | 18.68 | 18.67 | 18.47 |
| 4K -> 1080p RGBA32, threads | 10.91 | 11.27 | 9.43 | 9.35 | 9.32 | 9.64 | 9.81 |
| 4K -> 1080p RGB24, threads | 9.42 | 9.28 | 7.80 | 7.86 | 7.87 | 7.93 | 7.97 |
| 1080p -> 240p, threads | 2.11 | 2.12 | 1.72 | 1.71 | 1.72 | 1.72 | 1.74 |
| 4K -> 64x64, threads | 6.85 | 6.85 | 5.88 | 5.80 | 5.75 | 6.07 | 6.42 |

- The assigned first block (84bbe28): GCC's downscales -4% to -9%, Clang within 3%.
- Shared weights (16cdfbd): downscales a further -5% to -10% under both compilers, the threaded ones up to -17%.
  101 MP -> 720p, with its long period, within 4%.
- The 8-float step (1eb77df): within noise.
- The row loops (93775b0) under GCC: 4K -> 1080p Grayscale8 +31%. GCC left the loop around `storeTempPixelWithTaps`
  rolled, its body holding a loop, and kept the rows' state in memory (the Pi's log has the listing's detail).
  bdf65f9 restores it. Clang did not react.
- `[[likely]]` (8c9e194): no change here.
- Clang's 720p -> 4K Grayscale8 is 5% slower at 84bbe28 and 2.5% after it; that commit does not touch the short-run pass.
  Unexplained.

## Experiments that lost

**1. Weight broadcasts instead of the lane permute on ARM** (8429a19, reverted). GCC SIMD speedup 0.86-1.25x before,
0.84-1.34x after.
