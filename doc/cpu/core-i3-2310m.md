# Core i3-2310M

The measurement log for this CPU. Conclusions that shape the design are summarized in
[resize-performance.md](../resize-performance.md); the detail stays here.

## Machine

| | |
|---|---|
| CPU | Core i3-2310M (Sandy Bridge), 2 cores, 4 threads, 2.1 GHz, no turbo |
| SIMD | AVX; no AVX2, no FMA. It runs the AVX kernels, the SSE4.1 ones when capped; up to ad7966e SSE4.1 was its only level |
| Caches | 32 KB L1D, 256 KB private L2 per core, 3 MB L3 |
| Toolchain | None used on it: it runs binaries built on the 8500T or the 12600K (MSVC 19.51 and clang-cl, Qt 6.11.2) |
| OS | Windows 11 |

## Reading this machine's numbers

- A laptop with weak cooling: runs are kept short. The benchmark's default mix of threaded and single-threaded rows
  does not start its fan.
- A run of the single-threaded display and layout rows takes 20 s, of the threaded rows 17 s.
- Six alternating rounds resolve about 2% on the single-threaded rows.
- The threaded rows need eight rounds: with two, single runs differed by 30-65% on rows that eight rounds show flat.
- A binary built on the N4100 links Qt 6.12.0 and needs its `Qt6Core.dll` and `Qt6Gui.dll` beside it.

## Revisions up to 64af30d

MSVC 19.51 binaries built on the 12600K, Qt 6.11.2. Three alternating rounds of the full benchmark, 10 samples: each
binary's minimum, ms. 8baefb9 is the first revision with SSE4.1 kernels. A run takes 65-74 s.

| Scenario | 8baefb9 | b8892fc | a6c69e7 | 64af30d | QImage | 64af30d / QImage |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 243.5 | 230.5 | 196.6 | 190.6 | 67.8 | 2.81 |
| 4K -> 1080p RGB32 | 110.4 | 103.4 | 83.0 | 79.2 | 28.3 | 2.80 |
| 4K -> 1080p RGBA32 | 123.3 | 118.8 | 109.1 | 104.2 | 67.8 | 1.54 |
| 4K -> 1080p RGB24 | 245.7 | 104.1 | 82.8 | 80.3 | 60.9 | 1.32 |
| 4K -> 1080p Grayscale8 | 103.5 | 48.8 | 42.5 | 39.7 | 74.9 | 0.53 |
| 720p -> 4K RGBA32 | 71.8 | 72.2 | 66.8 | 53.8 | 196.9 | 0.27 |
| 720p -> 4K RGB32 | 63.1 | 62.0 | 56.4 | 51.0 | 84.9 | 0.60 |
| 720p -> 4K RGB24 | 178.9 | 57.7 | 53.3 | 47.5 | 99.9 | 0.48 |
| 720p -> 4K Grayscale8 | 67.4 | 25.6 | 20.9 | 15.6 | 84.5 | 0.18 |
| 1080p -> 1440p | 47.2 | 46.5 | 41.3 | 34.6 | 37.2 | 0.93 |
| 1080p -> 240p | 20.4 | 19.5 | 16.4 | 16.1 | 4.55 | 3.54 |
| 4K -> 64x64 | 56.8 | 55.9 | 52.4 | 51.0 | 23.4 | 2.18 |
| 101 MP -> 720p | 788.9 | 764.6 | 711.8 | 703.3 | 180.7 | 3.89 |
| 24 MP -> 1080p, threads | 123.7 | 115.1 | 92.9 | 90.2 | - | 1.33 |
| 4K -> 1080p RGBA32, threads | 63.1 | 58.6 | 51.2 | 49.6 | - | 0.73 |
| 4K -> 1080p RGB24, threads | 118.6 | 52.0 | 40.3 | 39.6 | - | 0.65 |
| 720p -> 4K RGBA32, threads | 35.3 | 35.6 | 32.5 | 27.3 | - | 0.14 |
| 720p -> 4K RGB24, threads | 77.9 | 28.1 | 25.0 | 23.1 | - | 0.23 |
| 1080p -> 1440p, threads | 21.3 | 20.9 | 18.8 | 16.5 | - | 0.44 |
| 1080p -> 240p, threads | 11.5 | 10.7 | 8.12 | 8.02 | - | 1.76 |
| 4K -> 64x64, threads | 37.0 | 36.3 | 31.3 | 30.9 | - | 1.32 |
| 101 MP -> 720p, threads | 411.8 | 397.8 | 340.5 | 338.8 | - | 1.87 |

- No row slows from one revision to the next: the largest rise is +0.6%. The QImage controls of the four binaries agree
  within 0.8%.
- 8baefb9 -> b8892fc: RGB24 and Grayscale8 2.1-3.1x faster, the 4-byte rows -7% to +0.6%.
- a6c69e7 -> 64af30d: upscales -10% to -25%, downscales -1% to -7%.
- Threads, 4 executors on 2 cores: 2.0-2.1x at 64af30d, 4K -> 64x64 1.65x. Three rounds only; the threaded rows of
  64af30d spread by at most 2.1% over them.
- 1080p at native size takes either 4.37 or 4.80 ms in any round of any binary; `QImage::copy` 4.31 or 4.7.
- The tests pass at 64af30d.

## Accumulators updated in place

Eight alternating rounds against 64af30d: median of same-round ratios / minimum to minimum.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 4K -> 1080p RGBA32 | -9.5% / -11.2% | -0.7% / +0.2% |
| 4K -> 1080p RGBA32, threads | -7.8% / -7.8% | +1.8% / +0.1% |
| 4K -> 1080p RGB32, RGB24, Grayscale8 | +1.5%, +3.5%, +2.3% / -0.1%, +0.0%, +0.4% | -0.6%, -0.4%, +0.2% / -0.8%, -0.2%, -1.3% |
| 720p -> 4K RGBA32, RGB32, RGB24, Grayscale8 | +1.7%, +0.3%, -0.3%, +0.9% / +0.3%, -0.5%, -0.1%, -1.3% | -0.5%, +0.3%, +1.0%, -0.2% / -0.3%, +0.2%, +0.0%, +0.6% |
| 24 MP -> 1080p, 1080p -> 240p, 4K -> 64x64 | +0.1%, -0.2%, +0.5% / +0.1%, -0.9%, +0.0% | +1.3%, +0.1%, -0.6% / +0.3%, +0.0%, +0.1% |
| Other threaded rows | -1.0% to +0.6% / -2.1% to +1.1% | -1.7% to +0.1% / -1.7% to +0.2% |

Every run had "System" at about 1 s of CPU.

## The vertical taps: a list, and the first tap assigned

Six alternating rounds of the single-threaded display and layout rows against 3e0a1ea: median of same-round ratios /
minimum to minimum. "List": the nonzero taps listed per dest row, the first tap assigned. "Assign only": the first tap
assigned, the rows walked as before.

| Scenario | MSVC list, 16-byte | MSVC assign only | clang-cl list, 16-byte | clang-cl assign only |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -10.3% / -10.6% | -6.6% / -6.1% | -7.2% / -6.4% | -4.9% / -4.8% |
| 720p -> 4K RGB32 | -10.6% / -9.8% | -3.4% / -2.9% | -7.4% / -7.6% | -6.3% / -5.8% |
| 720p -> 4K RGB24 | -14.2% / -12.8% | -3.8% / -3.8% | -3.8% / -2.9% | -4.4% / -3.8% |
| 720p -> 4K Grayscale8 | -10.3% / -11.4% | -4.6% / -5.1% | -7.8% / -8.0% | -6.8% / -6.3% |
| 1080p -> 1440p RGB32 | -6.7% / -7.3% | -2.8% / -1.8% | -4.6% / -4.4% | -4.2% / -3.7% |
| Downscales, by minimum | -2.3% to +0.2% | -1.3% to +1.5% | -3.0% to +2.4% | -0.8% to +0.8% |

The list's stored weight, a second run: `float` within a point of 16 bytes on every row under MSVC; under clang-cl 1-2
points behind on two upscales and on the 3-channel downscales. The 32-byte form within two points of 16 bytes.

The committed form: "experiment" is the `float` list written inline in `filterVerticalBlocks`.

| Scenario | MSVC experiment | MSVC committed | clang-cl experiment | clang-cl committed |
|---|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -9.4% / -9.4% | -9.7% / -9.4% | -4.2% / -5.9% | -5.1% / -5.5% |
| 720p -> 4K RGB32 | -9.6% / -9.5% | -10.6% / -11.4% | -6.3% / -6.2% | -6.9% / -6.5% |
| 720p -> 4K RGB24 | -12.9% / -12.7% | -12.6% / -11.7% | -0.2% / -0.6% | -6.0% / -6.1% |
| 720p -> 4K Grayscale8 | -7.3% / -5.0% | -7.8% / -8.7% | -6.0% / -6.3% | -4.9% / -2.4% |
| 1080p -> 1440p RGB32 | -7.0% / -5.5% | -7.1% / -8.5% | -5.1% / -5.6% | -5.4% / -4.9% |
| Downscales, by median | -6.0% to +0.2% | -4.9% to +1.1% | -4.2% to +0.8% | -1.7% to +0.7% |

Threaded rows, six rounds, MSVC, the committed form: 720p -> 4K RGBA32 -9.1% / -9.7%, RGB24 -8.3% / -9.8%,
1080p -> 1440p -3.9% / -5.2%; the downscales -2.7% to +0.2%.

With the list and without `addProduct` (64af30d's accumulators), MSVC's 3-channel downscales were +15% to +20%: the
list changed the kernel's register allocation, and the row pair's 8-tap horizontal loop got the 47-instruction form.

## Standing against Qt, e8d2d15

MSVC 19.51 built on the 12600K, Qt 6.11.2. Minimum ms of eight alternating rounds of the full benchmark.

| Scenario | Resizer | QImage | Ratio | Threads | Ratio | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 189.8 | 67.6 | 2.81 | 89.5 | 1.32 | 2.12x |
| 4K -> 1080p RGB32 | 76.2 | 27.4 | 2.78 | - | - | - |
| 4K -> 1080p RGBA32 | 91.5 | 70.3 | 1.30 | 43.7 | 0.62 | 2.09x |
| 4K -> 1080p RGB24 | 79.5 | 65.3 | 1.22 | 37.7 | 0.58 | 2.11x |
| 4K -> 1080p Grayscale8 | 38.1 | 74.2 | 0.51 | - | - | - |
| 720p -> 4K RGBA32 | 47.7 | 194.4 | 0.25 | 24.0 | 0.12 | 1.99x |
| 720p -> 4K RGB32 | 45.5 | 82.2 | 0.55 | - | - | - |
| 720p -> 4K RGB24 | 40.9 | 97.4 | 0.42 | 20.1 | 0.21 | 2.03x |
| 720p -> 4K Grayscale8 | 13.9 | 84.2 | 0.17 | - | - | - |
| 1080p -> 1440p | 31.5 | 35.9 | 0.88 | 15.1 | 0.42 | 2.09x |
| 1080p -> 240p | 15.9 | 4.55 | 3.49 | 7.96 | 1.75 | 2.00x |
| 4K -> 64x64 | 51.5 | 23.4 | 2.20 | 30.5 | 1.30 | 1.69x |
| 101 MP -> 720p | 704.0 | 180.8 | 3.89 | 333.4 | 1.84 | 2.11x |
| 1080p, native size | 3.86 | 3.79 | 1.02 | - | - | - |

- A noisier session than 64af30d's: a binary's rounds spread by 1-12% on the resizer rows, 1-7% on the controls, 28% on
  both native-size rows.
- QImage's 4K -> 1080p RGB24 and RGBA32 controls: 65.3 and 70.3 ms, against 60.9 and 67.8 in 64af30d's session.

## The SSE4.1 level VEX-encoded (not committed)

e8d2d15 with `/arch:AVX` on every source, through the `CL` variable: the same 128-bit kernels, three-operand encoded.
Register-to-register `movaps` in the binary: 928 -> 336. Eight alternating rounds against the build above: median of
same-round ratios / minimum to minimum.

| Scenario | Single-threaded | Threads |
|---|---:|---:|
| 24 MP -> 1080p | -3.3% / -3.2% | -1.5% / -1.4% |
| 4K -> 1080p RGB32, both of its rows | +4.8%, +3.6% / +3.4%, +2.2% | - |
| 4K -> 1080p RGBA32 | -2.8% / -3.8% | -4.1% / -5.3% |
| 4K -> 1080p RGB24 | +3.3% / +1.0% | -2.5% / -1.0% |
| 4K -> 1080p Grayscale8 | +2.2% / -1.4% | - |
| 720p -> 4K RGBA32 | -3.7% / -3.2% | -1.2% / -3.6% |
| 720p -> 4K RGB32 | -4.3% / -3.2% | - |
| 720p -> 4K RGB24 | -1.8% / -3.3% | -1.5% / -1.6% |
| 720p -> 4K Grayscale8 | -1.4% / -1.8% | - |
| 1080p -> 1440p | -1.6% / -0.5% | -1.4% / +1.7% |
| 1080p -> 240p | -4.2% / -3.5% | -1.4% / -1.5% |
| 4K -> 64x64 | -2.5% / -3.4% | +4.0% / +3.6% |
| 101 MP -> 720p | -4.0% / -5.8% | -1.5% / -1.2% |

- The QImage controls of the two binaries: within 0.8% by minimum.
- The tests pass under the VEX build.

## AVX primitives in the SSE4.1 level's place (prototype, not committed)

e8d2d15 with `/arch:AVX` on every source and a primitives header of 256-bit floats: multiply and add separate, loads,
stores, the weight spread, premultiply and rounding. The byte widening and the output's packing stay 128-bit. 4 horizontal
chains and 2 vertical blocks per step, as at AVX2. Eight alternating rounds with the plain and the VEX builds, against the
plain one: median of same-round ratios / minimum to minimum.

| Scenario | Single-threaded | Threads |
|---|---:|---:|
| 24 MP -> 1080p | -16.3% / -17.1% | -15.5% / -16.6% |
| 4K -> 1080p RGB32, both of its rows | -3.4%, -1.6% / -8.8%, -8.9% | - |
| 4K -> 1080p RGBA32 | -7.5% / -12.6% | -15.8% / -15.5% |
| 4K -> 1080p RGB24 | -1.8% / -9.4% | -10.7% / -9.4% |
| 4K -> 1080p Grayscale8 | -2.7% / -0.7% | - |
| 720p -> 4K RGBA32 | -1.3% / -2.1% | -3.3% / -3.4% |
| 720p -> 4K RGB32 | -1.6% / -4.1% | - |
| 720p -> 4K RGB24 | -0.6% / -1.9% | -1.1% / -2.3% |
| 720p -> 4K Grayscale8 | -0.9% / -1.1% | - |
| 1080p -> 1440p | -3.3% / -1.0% | -3.6% / -0.5% |
| 1080p -> 240p | -17.3% / -18.7% | -15.9% / -16.1% |
| 4K -> 64x64 | -27.7% / -29.2% | -18.8% / -19.8% |
| 101 MP -> 720p | -19.9% / -24.7% | -17.6% / -18.2% |

- Minimum ms, plain -> prototype: 24 MP -> 1080p 189.3 -> 157.0, 4K -> 64x64 51.4 -> 36.4, 101 MP -> 720p 705.4 -> 531.4,
  1080p -> 240p 16.0 -> 13.0.
- Single-threaded 4K -> 1080p of 3- and 4-channel pixels varies by round under the prototype: RGB32 69.2-82.9 ms over
  seven rounds (the eighth, 101.1, was disturbed on all three rows), against 75.9-81.8 for the plain build. Not explained.
- Upscales and Grayscale8: within the session's noise.
- The VEX build in this session: -5% to +4% by minimum, as in the section above.
- The `QImage::scaled` controls of the three binaries: within 2.5% by minimum. The tests pass under the prototype.
- The same three binaries on the 12600K, SSE4.1-capped, one pinned core, minimum of three rounds, prototype against plain:
  a P-core -12% to -32%; an E-core -6% to +18%, the upscales slower.

## Revisions up to 1e18e27

MSVC 19.51 binaries built on the 12600K, Qt 6.11.2, no QImage controls. 25cb0e5 has e8d2d15's SSE4.1 code under MSVC.
Six alternating rounds of the single-threaded display and layout rows: median of same-round ratios / minimum to
minimum, each build against the one before, and 1e18e27 against 25cb0e5. A run takes 19 s.

| Scenario | 25cb0e5, ms | fdd5aec | 5e3c797 | 1e18e27 | 1e18e27 / 25cb0e5 |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 189.7 | +0.3% / +0.7% | -1.3% / -0.8% | -2.7% / -2.6% | -3.3% / -2.7% |
| 4K -> 1080p RGB32 | 76.8 | +0.9% / +1.7% | -1.3% / -1.2% | -3.5% / -2.9% | -3.8% / -2.4% |
| 4K -> 1080p RGBA32 | 92.3 | -1.0% / -0.8% | -3.0% / -1.8% | -5.0% / -5.0% | -9.2% / -7.4% |
| 4K -> 1080p RGB24 | 78.7 | +0.2% / +0.6% | -5.3% / -3.6% | -3.6% / -3.6% | -8.4% / -6.6% |
| 4K -> 1080p Grayscale8 | 39.2 | +1.8% / +1.7% | -2.3% / -5.9% | -13.3% / -10.1% | -14.3% / -13.9% |
| 720p -> 4K RGBA32 | 49.2 | -2.5% / +0.1% | -0.4% / -0.3% | +1.1% / +1.0% | -0.7% / +0.8% |
| 720p -> 4K RGB32 | 46.2 | -0.3% / -0.1% | -0.2% / +0.2% | -3.5% / -3.7% | -4.4% / -3.7% |
| 720p -> 4K RGB24 | 41.2 | -2.0% / -0.2% | -0.2% / -0.2% | -5.2% / -4.4% | -6.2% / -4.9% |
| 720p -> 4K Grayscale8 | 13.8 | -0.3% / +1.0% | +5.9% / +6.4% | -11.5% / -11.2% | -7.9% / -4.6% |
| 1080p -> 1440p | 32.6 | -1.3% / -1.1% | +1.8% / +1.1% | -5.9% / -5.6% | -6.6% / -5.6% |
| 1080p -> 240p | 15.9 | +1.1% / +1.5% | -3.0% / -2.8% | -1.9% / -2.0% | -3.5% / -3.4% |
| 1080p, native size | 4.00 | +0.8% / -1.0% | -4.5% / -0.9% | +4.5% / +2.2% | +9.9% / +0.3% |

- fdd5aec, the ring 16 bytes past a 64-byte boundary (and 2577296's allocations on one): within 2.5% on every row.
  Goldmont Plus gains 2-6% from it.
- fdd5aec -> 5e3c797 also holds cbbd718 and 19bf5da, not separated: 720p -> 4K Grayscale8 +6%.
- 5e3c797 -> 1e18e27, the stepped-pointer passes: the Grayscale8 rows -10% to -13%, 720p -> 4K RGBA32 +1%.
- Native size: medians and minimums disagree by up to 10 points, the row's two levels.
- The display rows' 4K -> 1080p RGB32 agrees with the layout rows' within 1.2 points in every column.
- 1e18e27's thumbnail rows, added at d77c79c: 1080p -> 120 px 13.7 ms, 21 MP -> 175 px 132.7 ms.

Eight alternating rounds of the threaded rows, 4K -> 64x64 and 101 MP -> 720p, 1e18e27 against 25cb0e5. A run takes
28 s.

| Scenario | 25cb0e5, ms | 1e18e27 |
|---|---:|---:|
| 4K -> 64x64 | 51.5 | -0.9% / -0.6% |
| 101 MP -> 720p | 706.2 | -2.1% / -2.2% |
| 24 MP -> 1080p, threads | 90.6 | -5.0% / -5.3% |
| 4K -> 1080p RGBA32, threads | 43.6 | -5.2% / -4.6% |
| 4K -> 1080p RGB24, threads | 38.2 | -4.6% / -6.5% |
| 720p -> 4K RGBA32, threads | 24.7 | -1.7% / -2.1% |
| 720p -> 4K RGB24, threads | 20.4 | -4.9% / -5.7% |
| 1080p -> 1440p, threads | 15.4 | -6.8% / -6.1% |
| 1080p -> 240p, threads | 7.99 | -5.7% / -6.3% |
| 4K -> 64x64, threads | 30.4 | -5.0% / -2.7% |
| 101 MP -> 720p, threads | 351.3 | -4.6% / -4.6% |

- Every run had "System" at 1-1.6 s of CPU.
- The tests pass at 1e18e27.

## AVX primitives in the SSE4.1 level's place, 1e18e27 (not adopted)

1e18e27 with `/arch:AVX` on every source and the 1e18e27 form of `resize/simd_primitives_avx.h` in place of the
SSE4.1 primitives, MSVC 19.51, built on the 12600K:

- 256-bit floats: loads, stores, multiply and add separate, premultiply, the rounding conversion.
- The weight spread: `vperm2f128` puts a 128-bit half in both lanes, `vpermilps` picks each lane's weights.
- The byte widening: two 128-bit widenings, one 256-bit conversion. The output's packing, layout and alpha cap stay
  128-bit.
- 4 horizontal chains and 2 vertical blocks per step, as at AVX2; the ring on a 64-byte boundary.

"vex" is 1e18e27 with `/arch:AVX` alone. Six alternating rounds of the single-threaded display and layout rows: median
of same-round ratios / minimum to minimum.

| Scenario | Plain, ms | AVX, ms | vex / plain | AVX / plain | AVX / vex |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 185.3 | 145.4 | -5.6% / -5.9% | -20.1% / -21.5% | -15.2% / -16.6% |
| 4K -> 1080p RGB32 | 75.0 | 65.5 | -3.9% / -3.7% | -12.6% / -12.7% | -9.2% / -9.3% |
| 4K -> 1080p RGBA32 | 85.2 | 80.5 | -0.1% / +0.2% | -5.8% / -5.5% | -5.8% / -5.7% |
| 4K -> 1080p RGB24 | 73.9 | 64.8 | -0.9% / -0.3% | -12.8% / -12.3% | -11.7% / -12.1% |
| 4K -> 1080p Grayscale8 | 33.9 | 31.0 | -2.8% / -2.1% | -4.7% / -8.5% | -6.6% / -6.5% |
| 1080p -> 240p | 15.4 | 12.4 | -6.5% / -6.2% | -18.5% / -19.2% | -12.0% / -13.9% |
| 1080p -> 120 px | 13.7 | 9.84 | -11.0% / -10.7% | -28.2% / -28.1% | -19.3% / -19.5% |
| 21 MP -> 175 px | 133.1 | 92.3 | -11.8% / -11.9% | -30.2% / -30.6% | -21.0% / -21.2% |
| 720p -> 4K RGBA32 | 49.2 | 48.7 | -4.8% / -3.6% | -2.0% / -0.9% | +3.8% / +2.8% |
| 720p -> 4K RGB32 | 44.3 | 44.5 | -1.8% / -1.8% | +1.3% / +0.4% | +3.6% / +2.2% |
| 720p -> 4K RGB24 | 39.2 | 39.8 | -0.9% / -1.2% | +2.0% / +1.7% | +3.3% / +2.9% |
| 720p -> 4K Grayscale8 | 13.1 | 13.4 | +0.2% / -1.0% | +1.8% / +2.5% | +1.4% / +3.5% |
| 1080p -> 1440p | 30.6 | 31.4 | -0.4% / -0.7% | +3.9% / +2.4% | +5.6% / +3.1% |

Eight alternating rounds of the threaded rows, 4K -> 64x64 and 101 MP -> 720p:

| Scenario | Plain, ms | AVX, ms | AVX / plain |
|---|---:|---:|---:|
| 4K -> 64x64 | 51.3 | 35.6 | -29.9% / -30.6% |
| 101 MP -> 720p | 695.2 | 521.5 | -22.5% / -25.0% |
| 24 MP -> 1080p, threads | 85.8 | 70.8 | -18.0% / -17.5% |
| 4K -> 1080p RGBA32, threads | 42.1 | 35.1 | -15.4% / -16.7% |
| 4K -> 1080p RGB24, threads | 36.2 | 31.2 | -13.3% / -13.9% |
| 720p -> 4K RGBA32, threads | 24.6 | 23.8 | -5.6% / -3.3% |
| 720p -> 4K RGB24, threads | 19.4 | 19.8 | +0.5% / +2.1% |
| 1080p -> 1440p, threads | 14.6 | 14.9 | +3.6% / +2.2% |
| 1080p -> 240p, threads | 7.53 | 6.25 | -17.0% / -16.9% |
| 4K -> 64x64, threads | 29.8 | 23.4 | -22.9% / -21.5% |
| 101 MP -> 720p, threads | 332.8 | 264.8 | -20.7% / -20.4% |

- Minimum ms, the e8d2d15 prototype -> this one: 24 MP -> 1080p 157.0 -> 145.4, 4K -> 64x64 36.4 -> 35.6,
  101 MP -> 720p 531.4 -> 521.5, 1080p -> 240p 13.0 -> 12.4.
- vex: level to -12% on the downscales, where e8d2d15's was -5% to +4% on every row.
- The display rows' 4K -> 1080p RGB32 agrees with the layout rows' within 0.6 points. Native size: within the row's
  two levels.
- 4K -> 1080p RGB32 does not vary by round as under the e8d2d15 prototype: -12.5% median, -12.6% by minimum.
- The tests pass under this build on the i3-2310M, and on the 12600K at both levels.

### Upscale probes

One change each against the build above, through the header's defines, six alternating rounds of the same rows, a second
session: "1 block", one vertical block per step; "ring + 16", the ring 16 bytes past a 64-byte boundary, as at SSE4.1.
Against the build above:

| Scenario | AVX, ms | vex | 1 block | ring + 16 |
|---|---:|---:|---:|---:|
| 24 MP -> 1080p | 145.4 | +15.5% / +20.6% | +2.8% / +4.9% | +2.4% / +4.5% |
| 4K -> 1080p RGB32 | 65.6 | +10.2% / +10.1% | +0.2% / +0.1% | +0.2% / -1.5% |
| 4K -> 1080p RGBA32 | 80.6 | +6.2% / +6.5% | +0.9% / +0.8% | -0.2% / -0.8% |
| 4K -> 1080p RGB24 | 64.9 | +14.1% / +14.7% | +0.4% / +0.3% | -1.4% / -0.9% |
| 4K -> 1080p Grayscale8 | 31.0 | +6.7% / +7.2% | +8.3% / +1.1% | +14.6% / +0.5% |
| 1080p -> 240p | 12.7 | +12.8% / +13.5% | -0.7% / -0.4% | -1.6% / -1.3% |
| 1080p -> 120 px | 9.82 | +24.4% / +24.1% | +2.8% / +2.9% | +0.1% / +0.2% |
| 21 MP -> 175 px | 92.1 | +27.6% / +27.5% | +3.6% / +3.2% | +0.4% / -0.0% |
| 720p -> 4K RGBA32 | 47.0 | -3.2% / -3.4% | -0.7% / +0.6% | -0.7% / -0.4% |
| 720p -> 4K RGB32 | 43.9 | -3.2% / -5.9% | -2.3% / -2.8% | -2.8% / -2.6% |
| 720p -> 4K RGB24 | 39.1 | -0.9% / -2.7% | +3.0% / +2.4% | -1.0% / +0.2% |
| 720p -> 4K Grayscale8 | 13.9 | +0.7% / -1.4% | +1.8% / -2.2% | +0.4% / -0.4% |
| 1080p -> 1440p | 31.6 | -5.0% / -5.9% | -1.9% / -1.9% | -1.6% / -0.5% |

- Neither probe closes the gap to vex on the colour upscales: at most -3%. One block costs the strong downscales 3-5%.
- The build's minimums on these rows agree with the first session's within 3.5%.
- 4K -> 1080p Grayscale8: medians and minimums disagree by up to 14 points.

## AVX primitives in the SSE4.1 level's place, ad7966e (adopted as the AVX level)

The header above ported to ad7966e's outline, MSVC 19.51, built on the 12600K: `interleavedRowPairs` and a
`longestFixedXRun` of 20 as at AVX2, the pixel pairs widened by 128-bit steps. "Not interleaved" is the same build
with `interleavedRowPairs` off.

Single-threaded display and layout rows: six alternating rounds of plain, AVX and not interleaved, median of same-round
ratios / minimum to minimum. vex: a separate session of six rounds against plain. QImage: the best of three
controls-only runs.

| Scenario | QImage, ms | Plain, ms | AVX, ms | Plain / QImage | AVX / QImage | AVX / plain | Not interleaved / plain | vex / plain |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 68.1 | 178.0 | 144.8 | 2.61 | 2.12 | -18.2% / -18.7% | -19.0% / -19.0% | -7.1% / -7.6% |
| 2.8 MP -> 1920x1280 | 33.8 | 32.8 | 28.7 | 0.97 | 0.85 | -12.6% / -12.5% | -4.5% / -4.0% | -2.4% / -3.1% |
| 4K -> 1600x900 | 35.5 | 68.1 | 52.4 | 1.92 | 1.47 | -23.0% / -23.1% | -17.4% / -17.7% | -3.7% / -4.2% |
| 4K -> 720p | 20.1 | 60.6 | 45.0 | 3.01 | 2.23 | -25.5% / -25.8% | -16.7% / -17.4% | -5.5% / -6.2% |
| 4K -> 1080p RGB32 | 28.0 | 64.6 | 57.3 | 2.30 | 2.05 | -10.8% / -11.2% | -5.1% / -4.8% | -3.3% / -2.7% |
| 4K -> 1080p RGBA32 | 67.9 | 88.3 | 68.7 | 1.30 | 1.01 | -22.6% / -22.2% | -13.1% / -12.9% | -11.7% / -11.5% |
| 4K -> 1080p RGB24 | 61.2 | 63.9 | 56.3 | 1.04 | 0.92 | -11.9% / -11.9% | -5.4% / -5.2% | -0.0% / -0.1% |
| 4K -> 1080p Grayscale8 | 75.4 | 27.9 | 25.6 | 0.37 | 0.34 | -8.3% / -8.2% | -8.5% / -7.7% | -8.4% / -8.3% |
| 1080p -> 240p | 4.56 | 15.1 | 12.3 | 3.30 | 2.70 | -17.5% / -18.4% | -17.8% / -18.1% | -7.2% / -6.8% |
| 1080p -> 120 px | 3.67 | 13.6 | 9.34 | 3.69 | 2.55 | -31.1% / -31.1% | -31.2% / -31.1% | -11.0% / -11.3% |
| 21 MP -> 175 px | 41.8 | 132.2 | 86.6 | 3.17 | 2.07 | -34.5% / -34.5% | -34.4% / -34.4% | -11.9% / -11.8% |
| 720p -> 4K RGBA32 | 198.6 | 48.1 | 46.0 | 0.24 | 0.23 | -5.7% / -4.4% | -2.7% / +0.7% | -5.0% / -5.7% |
| 720p -> 4K RGB32 | 84.9 | 41.3 | 40.1 | 0.49 | 0.47 | -3.5% / -2.9% | +1.5% / +2.2% | -3.0% / -2.8% |
| 720p -> 4K RGB24 | 100.2 | 36.9 | 36.0 | 0.37 | 0.36 | -5.5% / -2.5% | -3.6% / +3.0% | -1.6% / -2.3% |
| 720p -> 4K Grayscale8 | 84.6 | 13.5 | 13.5 | 0.16 | 0.16 | +0.2% / -0.4% | -0.9% / -0.3% | +1.5% / +2.1% |

Eight alternating rounds of plain and AVX: the threaded rows, 4K -> 64x64 and 101 MP -> 720p.

| Scenario | QImage, ms | Plain, ms | AVX, ms | Plain / QImage | AVX / QImage | AVX / plain |
|---|---:|---:|---:|---:|---:|---:|
| 4K -> 64x64 | 23.6 | 51.5 | 34.3 | 2.19 | 1.45 | -33.3% / -33.4% |
| 101 MP -> 720p | 182.0 | 679.7 | 514.1 | 3.73 | 2.82 | -22.3% / -24.4% |
| 24 MP -> 1080p, threads | 68.1 | 84.2 | 66.0 | 1.24 | 0.97 | -21.2% / -21.7% |
| 4K -> 1080p RGBA32, threads | 67.9 | 39.9 | 30.2 | 0.59 | 0.44 | -24.4% / -24.5% |
| 4K -> 1080p RGB24, threads | 61.2 | 32.2 | 25.8 | 0.53 | 0.42 | -20.3% / -20.0% |
| 720p -> 4K RGBA32, threads | 198.6 | 24.4 | 22.9 | 0.12 | 0.12 | -6.8% / -6.5% |
| 720p -> 4K RGB24, threads | 100.2 | 19.0 | 18.3 | 0.19 | 0.18 | -4.8% / -3.9% |
| 1080p -> 240p, threads | 4.56 | 7.32 | 5.98 | 1.61 | 1.31 | -18.8% / -18.4% |
| 4K -> 64x64, threads | 23.6 | 29.8 | 21.9 | 1.27 | 0.93 | -26.4% / -26.5% |
| 101 MP -> 720p, threads | 182.0 | 316.5 | 241.9 | 1.74 | 1.33 | -23.5% / -23.6% |

- Interleaving: 3-12% on the rows with padded runs of 4-float pixels; the rows without them read alike either way.
- MSVC's 4-tap column for a row pair, interleaved: 28 instructions, 4 multiplies, 4 `vshufps` from one
  `vbroadcastf128`, no stack references. Not interleaved: indexed loads at 16-byte steps, `vperm2f128` and two
  `vpermilps` for the weights, two `vextractf128` and adds for the halves.
- The controls-only runs spread by at most 2.6%, 1080p -> 240p 12.9%. They agree with e8d2d15's session within about 3%.
- Plain's minimums in the AVX and vex sessions agree within 1.4%, native size aside.
- Plain 4K -> 1080p RGBA32: 88.3 ms, against 85.2 at 1e18e27 in another session; VEX encoding alone takes 11.7% off
  it. Not investigated.
- The tests pass under both builds on the 12600K at both levels.

## x runs three at a time over the interleaved row pairs

AVX's run triples, as AVX2 takes them since 6bb1cec, against 6bb1cec: each compiler against its own build, ten
alternating rounds, median of same-round ratios / minimum to minimum. The SSE4.1 rows run the same code in both builds.

| Scenario | 6bb1cec, MSVC, min ms | MSVC | clang-cl |
|---|---:|---:|---:|
| 960x540 -> 1920x1080 RGB32 | 12.03 | -11.6% / -7.1% | -1.9% / -5.6% |
| 1280x720 -> 1920x810 RGB32 | 11.50 | -13.6% / -9.0% | -1.8% / -8.0% |
| 1280x720 -> 1920x1440 RGB32 | 16.23 | -6.8% / -6.6% | -4.2% / -5.4% |
| 720p -> 4K RGB32 | 36.13 | -5.4% / -8.2% | -3.7% / -4.9% |
| 960x540 -> 1920x1080 RGBA32 | 13.80 | -8.2% / -5.8% | -5.6% / -5.7% |
| 1080p -> 1440p RGBA32 | 29.00 | -6.1% / -6.8% | -5.8% / -5.8% |
| 720p -> 4K RGBA32 | 40.62 | -4.8% / -5.6% | -3.3% / -4.0% |
| 1080p -> 720p RGB32 | 18.03 | -0.0% / +0.0% | -0.2% / -0.2% |
| 4K -> 1080p RGBA32 | 66.15 | +0.1% / +0.0% | +0.0% / +0.0% |
| 1280x720 -> 1920x1080 RGB32, SSE4.1 | 14.72 | -2.5% / -0.3% | -1.4% / -0.4% |
| 1280x720 -> 1920x1080 RGBA32, SSE4.1 | 16.97 | +2.0% / +0.2% | -0.3% / -0.7% |

- The medians spread up to 2.5% on the SSE4.1 rows: minimum to minimum is the reading here.
- Placement control, MSVC, a second session: both builds also with 32 bytes of `__nop()` at the start of
  `resizeRowsWith`. The padding alone moved 6bb1cec's rows 1.4% at most; the triples took the upscales 6.0-9.5% faster
  against the padded build, padded or not, the 1080p -> 720p control level.

## Four y taps held across the dest row

AVX's vertical pass holding a 4-tap dest row's row pointers and weights in registers, as AVX2 and SSE4.1 do since
e70ecc5, against e70ecc5: each compiler against its own build, four alternating rounds of the "Common" sections, median
of same-round ratios / minimum to minimum.

| Scenario, AVX | e70ecc5, MSVC, min ms | Held, MSVC | Held, clang-cl |
|---|---:|---:|---:|
| 720p -> 4K RGBA32 | 43.92 | -5.1% / -5.5% | -17.0% / -14.3% |
| 720p -> 4K RGB32 | 38.53 | -11.0% / -6.7% | -14.4% / -13.6% |
| 720p -> 4K RGB24 | 33.87 | +2.2% / -8.7% | -14.3% / -14.4% |
| 720p -> 4K Grayscale8 | 13.86 | -10.0% / -11.9% | -9.2% / -6.3% |

- MSVC's held RGB24 upscale took 31 ms in two rounds and 39-41 ms in the other two.
- The downscales within 1.6%, but MSVC's 4K -> 1600x900 -2.9%; native size, the same code, -15.8% minimum to minimum
  under MSVC.
- The held loops have no stack references but RGB32's alpha tails, as at AVX2.
- The test executable: MSVC 3.63 -> 3.91 MB, clang-cl 4.69 -> 5.02 MB.

## The premultiplying conversion on Sandy Bridge

4K -> 200x113 RGBA32, straight alpha, against the same row with the source marked premultiplied, which skips the
premultiplying conversion. Probe builds of 67935c2, four alternating rounds, minimum ms.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 4K -> 200x113 RGBA32 | 52.98 | 43.22 |
| The same, premultiplied | 40.33 | 38.83 |
| 4K -> 200x113 RGB32 | 39.45 | 38.39 |
| 4K -> 1080p RGBA32 | 69.13 | 67.39 |
| 4K -> 1080p RGB32 | 57.45 | 56.48 |

- Premultiplying costs MSVC 12.7 ms on this row, clang-cl 4.4. The 2x row, whose padded kernel converts row pairs
  interleaved, shows no such gap.
- The two compilers' premultiplying block loops: the same 22 instructions per 4 pixels, no stack references. MSVC clears
  the upper YMM state before the sliding buffer's `memmove`.
- MSVC with 32 and with 16 bytes of `__nop()` at the start of `resizeRowsWith`: 53.00 and 52.99 ms. The row alone in its
  process: 53.08 ms against clang-cl's 43.07.
- Converting a row pair per loop, both rows' loads, premultiplies and stores per block: MSVC 44.17 ms, premultiplied
  37.78; clang-cl 45.92 and 37.74.

## A row pair converted in one loop

Against 67935c2, four alternating rounds, each compiler against its own build, median of same-round ratios / minimum to
minimum.

| Scenario | 67935c2, MSVC, min ms | MSVC | clang-cl |
|---|---:|---:|---:|
| 24 MP -> 1620x1080 RGB32 | 144.22 | -4.1% / -7.9% | -3.1% / -1.2% |
| 2.8 MP -> 1920x1280 RGB32 | 28.87 | +0.6% / +0.6% | +0.3% / -0.2% |
| 4K -> 1600x900 RGB32 | 52.16 | -1.0% / -1.1% | -0.5% / +0.0% |
| 4K -> 720p RGB32 | 44.98 | +0.5% / +0.5% | -0.7% / -0.2% |
| 720p -> 4K RGBA32 | 41.10 | +0.8% / -0.2% | -0.7% / -0.6% |
| 720p -> 4K RGB32 | 35.98 | +0.5% / +0.3% | -0.0% / +0.2% |
| 1080p -> 240p RGB32 | 12.04 | -3.8% / -3.8% | -4.2% / -3.7% |
| 1080p -> 120x68 RGB32 | 9.16 | -5.2% / -5.9% | -2.6% / -2.7% |
| 21 MP -> 175x117 RGB32 | 83.82 | -5.6% / -5.8% | -1.8% / -1.6% |
| 4K -> 200x113 RGB32 | 39.45 | -5.3% / -5.4% | -2.9% / -2.6% |
| 4K -> 200x113 RGBA32 | 52.93 | -15.7% / -15.9% | +5.9% / +6.0% |
| 1080p at native size RGB32 | 4.81 | -9.0% / -9.6% | +6.6% / +0.2% |
| 4K -> 1080p Grayscale8 | 25.90 | -8.1% / -2.2% | -1.6% / -1.2% |
| 4K -> 1080p RGB24 | 57.17 | -0.5% / -0.6% | +0.7% / +0.6% |
| 4K -> 1080p RGB32 | 57.00 | +0.5% / +0.7% | -1.5% / -1.7% |
| 4K -> 1080p RGBA32 | 68.73 | +0.6% / +0.6% | -0.6% / -0.4% |
| 720p -> 4K Grayscale8 | 12.46 | +0.3% / -0.2% | +1.7% / -2.1% |
| 720p -> 4K RGB24 | 31.69 | -1.5% / -1.6% | -1.3% / -0.3% |

## 87bebbc to 54b7a6b: the conversion, the tap steps, the block loop

54b7a6b against e7f3800 at AVX, each compiler against its own build: clang-cl three rounds; MSVC two, in a session
where one run shared the machine with 26 s of `svchost`.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 24 MP -> 1620x1080 RGB32 | -8.1% / -3.6% | +0.9% / -0.4% |
| 4K -> 1600x900 RGB32 | +2.1% / +2.3% | -0.1% / -0.4% |
| 4K -> 720p RGB32 | +0.4% / -0.2% | -0.1% / -0.1% |
| 720p -> 4K RGBA32 | -3.3% / -1.4% | +0.8% / +0.9% |
| 720p -> 4K RGB32 | +0.2% / +1.5% | +1.7% / +1.7% |
| 720p -> 4K RGB24 | +3.3% / +3.1% | +0.3% / +0.7% |
| 1080p -> 240p RGB32 | +1.1% / +1.4% | -2.5% / -6.2% |
| 1080p -> 120x68 RGB32 | -4.9% / -3.3% | -7.5% / -7.6% |
| 21 MP -> 175x117 RGB32 | -5.2% / -5.2% | -8.0% / -8.3% |
| 4K -> 200x113 RGB32 | -2.7% / -2.7% | -5.2% / -5.1% |
| 4K -> 200x113 RGBA32 | -3.7% / -3.2% | -4.3% / -4.7% |
| 4K -> 1080p, 3 and 4 channels | -1.6% to +0.8% | -0.8% to +0.4% |
| 4K -> 64x64 RGB32 | -1.9% / -3.4% | -5.7% / -6.2% |
| 101 MP -> 720p RGB32 | +0.0% / +1.3% | +2.2% / +2.2% |

- clang-cl by commit, the display and layout scenarios, three rounds: 87bebbc and cf1340b within 1.6% on the thumbnails,
  54b7a6b 3.5-8% faster there; the upscales within 2% at each.
- The disturbed session read clang-cl's upscales 10-16% slower; neither later session repeated it.
- One run of the whole benchmark: about 35 s.
