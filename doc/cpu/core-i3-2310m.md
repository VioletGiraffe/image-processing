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
