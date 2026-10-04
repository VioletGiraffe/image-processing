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

## Experiments that lost

**1. Weight broadcasts instead of the lane permute on ARM** (8429a19, reverted). GCC SIMD speedup 0.86-1.25x before,
0.84-1.34x after.
