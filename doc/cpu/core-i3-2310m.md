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
