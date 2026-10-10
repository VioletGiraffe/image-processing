# Core i3-2310M

The measurement log for this CPU. Conclusions that shape the design are summarized in
[resize-performance.md](../resize-performance.md); the detail stays here.

## Machine

| | |
|---|---|
| CPU | Core i3-2310M (Sandy Bridge), 2 cores, 4 threads, 2.1 GHz, no turbo |
| SIMD | AVX; no AVX2, no FMA. The SSE4.1 kernels are the only level it runs |
| Caches | 32 KB L1D, 256 KB private L2 per core, 3 MB L3 |
| Toolchain | None used on it: it runs binaries built on the 8500T (MSVC 19.51 and clang-cl, Qt 6.11.2) |
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
