# Apple M1

The measurement log for this CPU. Sections carry the headings of [resize-performance.md](../resize-performance.md), which
holds each one's rationale.

## Machines

| | |
|---|---|
| macos-latest runner | Apple M1 (virtual), 3 cores, 12 MB L2, Apple Clang |
| Mac mini (Macmini9,1), the real M1 | 4 P-cores: 128 KB L1D, 12 MB L2 shared; 4 E-cores: 4 MB L2; 128-byte cache lines. macOS 27, Apple Clang 21.0, Qt 6.11.2 |

- ISA beyond NEON: FP16, FHM, DotProd, RDM; no BF16, no I8MM (`sysctl hw.optional.arm.FEAT_*`).
- `sysctl hw.l1dcachesize` reads 64 KB, the E-cores'. A core type's own sizes are under `hw.perflevel<n>.`, 0 the P-cores.

Run-to-run noise: up to 2x on the runner, too noisy to read per scenario. The Mac mini: below.

## Measuring on the Mac mini

- A serial run lands on a P-core at the default QoS. `taskpolicy -b` moves it to the E-cores: 4K -> 1080p RGB32 14.6
  -> 107 ms. macOS has no pinning: the threaded rows' pool spans both core types.
- One run: serial rows of 2 ms or more within 4%, most within 2%; threaded rows 12-15%; rows under 1 ms 10-50%. Three
  runs of one binary at the start, middle and end of a 40-minute pass: no drift.
- Ten alternating rounds resolve about 1%: two builds differing only in the store hint spread 1.3% at most (the
  `STNP` section).
- Native size lands in one of two levels per process, 0.22-0.26 or 0.30-0.33 ms, at any revision.
- A system process (ScriptMonitor) takes about 12% of a core throughout; `run_benchmark_rounds.py` reports it per run.
- `mediaanalysisd`, the media analysis daemon, starts on its own and took 62 s of CPU over a 32 s run: such a run is
  rerun.
- `taskpolicy -c utility` leaves a run on a P-core: 14.6 ms.
- Apple Clang 21 crashes on 5cc07e4's NEON kernel file; 74b015c works around it. Every other revision from 963b54b
  builds against the current cpputils, cpp-template-utils and qtutils.
- A revision builds from `git archive` into a tree of its own, the three dependencies symlinked beside it: 27 s. Its
  test binary copied out of the `.app` runs: it finds Qt through an absolute rpath.
- Benchmark files before 25cb0e5 ignore `IMAGE_PROCESSING_BENCHMARK_NO_CONTROLS`: their runs time the QImage controls
  too, 33 s a run; 963b54b's its `[scalar]` rows as well, 52 s. Before 9811e5b no scenario has a `-c` section.

## 963b54b to e3520f9, one run per revision (2026-10-23)

The 60 commits that change the NEON build, all but 5cc07e4, one run each with its own benchmark file: the timed region
is the same in all. A step within the single-run noise is not read. ms, 963b54b -> e3520f9:

| Scenario | Serial | Threads, noise 15% |
|---|---|---|
| 24 MP -> 1080p | 43.1 -> 37.2 (-14%) | 12.5 -> 10.4 |
| 4K -> 1080p RGB32 | 19.2 -> 14.9 (-23%) | - |
| 4K -> 1080p RGBA32 | 21.3 -> 18.1 (-15%) | 6.6 -> 5.0 |
| 4K -> 1080p RGB24 | 35.8 -> 14.5 (-59%) | 10.3 -> 4.2 |
| 4K -> 1080p Grayscale8 | 16.7 -> 5.8 (-65%) | - |
| 720p -> 4K RGBA32 | 12.1 -> 7.9 (-35%) | 3.5 -> 2.3 |
| 720p -> 4K RGB32 | 10.7 -> 6.6 (-38%) | - |
| 720p -> 4K RGB24 | 17.5 -> 6.6 (-62%) | 5.1 -> 1.9 |
| 720p -> 4K Grayscale8 | 8.8 -> 2.6 (-71%) | - |
| 1080p -> 240p | 3.7 -> 3.1 (-18%) | 1.3 -> 1.0 |
| 4K -> 64x64 | 9.9 -> 9.7 (-2%) | 3.8 -> 3.4 |
| 101 MP -> 720p | 145.3 -> 135.6 (-7%) | 42.5 -> 37.1 |

The steps beyond the noise, serial:
- e5ea70d: RGB24 and Grayscale8 onto the kernels, -32% to -45%.
- 0bd90e4, a6c69e7, 1ed9965: 720p -> 4K Grayscale8 -19%, -18%, -20%; a6c69e7 4K -> 1080p Grayscale8 -12%.
- 16cdfbd: downscales -4.5% to -10%.
- eeb1af6: downscales -5% to -9%.
- e60816d: upscales -7% to -20%. e8d2d15: upscales and Grayscale8 -5% to -6%.
- 963e6bc: 4K -> 1080p -6%, its Grayscale8 -25%. 560ca96: RGB32 upscale -6.5%.
- 411a90d, 05f8105: nearest-neighbor upscales -45%, then -58%.
- ae01521: upscales -11% to -13%.

Slowdowns:
- 901d72e: RGB32 and RGBA32 downscales +1.3% to +3.5%, kept to e3520f9 (its section).
- 32b0a2f: the strongest downscales +2.7% to +3.5%, upscales -4% to -6.6% (its section).
- 1eb77df: 720p -> 4K RGB24 about +4% in single runs until e60816d replaced its pass. Not confirmed by rounds.

Reading the scan:
- Adjacent single runs flip by 5-8% on the upscale rows: 594a17b +6.4% on 2.8 MP -> 1920x1280, then db0e2df, with no
  kernel change, -6.2%.
- A step is read from the median of the three revisions on each side, then confirmed by rounds.
- HEAD against the fastest of 59 single runs reads 2-4% slow from the noise alone.
- The scan's control: 93b1b79 and 8e68f80 change only x64 build flags, and move the serial median +0.4% and -0.3%.

Every revision, serial, one run each, ms. Down: 4K -> 1080p; Up: 720p -> 4K.

| Commit | 24 MP | Down RGB32 | Down RGBA32 | Down RGB24 | Down Gray8 | Up RGBA32 | Up RGB32 | Up RGB24 | Up Gray8 | 240p | 64x64 | 101 MP |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 963b54b | 43.08 | 19.17 | 21.32 | 35.77 | 16.67 | 12.10 | 10.66 | 17.46 | 8.80 | 3.74 | 9.85 | 145.31 |
| 8baefb9 | 43.00 | 19.23 | 21.47 | 35.45 | 16.44 | 12.14 | 10.50 | 17.45 | 8.79 | 3.72 | 10.05 | 145.31 |
| 42a4330 | 43.32 | 19.38 | 21.59 | 35.67 | 16.73 | 12.04 | 10.63 | 17.81 | 8.77 | 3.70 | 9.87 | 145.63 |
| e5ea70d | 43.12 | 19.30 | 22.53 | 19.49 | 9.36 | 12.24 | 10.30 | 10.15 | 5.93 | 3.75 | 9.89 | 146.66 |
| f04db84 | 42.77 | 19.49 | 22.32 | 19.78 | 9.12 | 12.25 | 10.15 | 10.16 | 5.84 | 3.73 | 9.88 | 147.38 |
| f2f4569 | 43.26 | 19.19 | 22.89 | 19.54 | 9.11 | 12.21 | 10.50 | 10.01 | 5.86 | 3.69 | 9.88 | 144.97 |
| 03113a6 | 43.09 | 19.23 | 22.47 | 19.71 | 9.02 | 12.27 | 10.35 | 10.00 | 5.84 | 3.74 | 10.03 | 147.64 |
| 14e731e | 43.57 | 19.61 | 22.46 | 19.80 | 9.06 | 12.35 | 10.37 | 9.84 | 5.86 | 3.78 | 10.20 | 146.82 |
| 74b015c | 43.02 | 19.75 | 22.39 | 19.82 | 9.02 | 12.66 | 10.27 | 10.15 | 5.98 | 3.76 | 10.22 | 146.93 |
| 15f240e | 43.22 | 19.50 | 22.52 | 19.68 | 9.01 | 12.45 | 10.26 | 10.20 | 5.97 | 3.73 | 9.87 | 148.53 |
| 0bd90e4 | 43.17 | 19.22 | 23.10 | 19.84 | 9.36 | 12.29 | 10.11 | 9.84 | 4.83 | 3.81 | 9.89 | 147.07 |
| 84bbe28 | 41.90 | 18.46 | 21.69 | 20.34 | 9.76 | 12.21 | 10.13 | 10.00 | 4.89 | 3.76 | 10.19 | 145.86 |
| 16cdfbd | 41.24 | 17.62 | 21.12 | 19.73 | 9.54 | 11.93 | 9.86 | 9.82 | 4.74 | 3.46 | 9.16 | 141.69 |
| 1eb77df | 42.96 | 17.98 | 21.35 | 19.87 | 9.54 | 12.02 | 10.44 | 10.44 | 4.72 | 3.41 | 9.16 | 144.31 |
| 93775b0 | 42.58 | 17.69 | 21.05 | 19.95 | 9.50 | 11.92 | 10.03 | 10.02 | 4.68 | 3.43 | 9.16 | 144.55 |
| bdf65f9 | 41.82 | 17.66 | 21.30 | 19.86 | 9.58 | 12.38 | 10.21 | 10.43 | 4.74 | 3.44 | 9.16 | 144.44 |
| 8c9e194 | 42.03 | 18.06 | 21.08 | 20.15 | 9.24 | 12.68 | 10.29 | 10.62 | 4.67 | 3.45 | 9.18 | 145.66 |
| 680a5e5 | 41.88 | 17.63 | 21.14 | 19.90 | 9.40 | 11.87 | 10.02 | 10.33 | 4.58 | 3.44 | 9.33 | 144.82 |
| a6c69e7 | 42.28 | 17.69 | 21.30 | 19.99 | 8.30 | 11.62 | 9.98 | 10.12 | 3.77 | 3.40 | 9.22 | 146.21 |
| b8bdd30 | 42.32 | 17.63 | 21.08 | 19.84 | 8.43 | 12.01 | 9.70 | 10.44 | 3.89 | 3.44 | 9.14 | 144.44 |
| 93b1b79 | 42.07 | 17.53 | 21.64 | 20.14 | 8.34 | 11.75 | 10.06 | 10.54 | 3.82 | 3.44 | 9.28 | 145.49 |
| 8d72f9b | 41.99 | 18.05 | 21.24 | 19.91 | 8.30 | 11.67 | 9.71 | 10.35 | 3.79 | 3.43 | 9.27 | 144.42 |
| 1917611 | 41.89 | 17.92 | 21.42 | 20.25 | 8.32 | 11.66 | 9.82 | 10.45 | 3.77 | 3.46 | 9.15 | 144.46 |
| 1ed9965 | 41.86 | 17.72 | 21.12 | 19.92 | 8.62 | 11.61 | 10.21 | 10.02 | 3.01 | 3.58 | 9.17 | 145.08 |
| eeb1af6 | 39.23 | 17.91 | 20.38 | 18.19 | 8.17 | 10.83 | 10.07 | 10.72 | 3.11 | 3.31 | 9.22 | 137.23 |
| e60816d | 38.37 | 17.72 | 20.15 | 18.22 | 8.03 | 10.10 | 8.32 | 8.62 | 2.73 | 3.33 | 9.28 | 136.79 |
| 64af30d | 39.12 | 17.43 | 19.79 | 18.18 | 8.21 | 9.16 | 8.58 | 8.61 | 2.69 | 3.38 | 9.31 | 137.35 |
| 3e0a1ea | 38.99 | 17.53 | 20.15 | 18.13 | 8.36 | 9.32 | 8.35 | 8.71 | 2.77 | 3.31 | 9.45 | 137.02 |
| e8d2d15 | 39.07 | 17.65 | 19.98 | 17.88 | 7.87 | 8.81 | 7.95 | 8.20 | 2.59 | 3.36 | 9.17 | 136.48 |
| 8e68f80 | 38.98 | 17.48 | 19.70 | 17.85 | 7.88 | 8.94 | 7.84 | 8.18 | 2.66 | 3.29 | 9.16 | 137.10 |
| b1395f9 | 38.11 | 17.42 | 19.76 | 17.74 | 7.83 | 8.81 | 7.76 | 7.62 | 2.65 | 3.25 | 9.52 | 136.52 |
| 2577296 | 38.56 | 17.39 | 20.03 | 17.62 | 7.86 | 9.04 | 7.78 | 7.71 | 2.56 | 3.50 | 9.25 | 139.96 |
| fdd5aec | 38.81 | 17.63 | 20.00 | 17.64 | 7.85 | 8.90 | 7.78 | 8.12 | 2.57 | 3.29 | 9.29 | 140.58 |
| cbbd718 | 38.76 | 17.39 | 19.77 | 18.14 | 7.75 | 8.96 | 8.13 | 7.84 | 2.60 | 3.27 | 9.50 | 137.19 |
| 19bf5da | 38.93 | 17.62 | 20.31 | 17.62 | 7.82 | 8.85 | 7.96 | 7.62 | 2.63 | 3.24 | 9.15 | 135.63 |
| 5e3c797 | 37.95 | 17.71 | 19.55 | 17.77 | 7.80 | 8.90 | 7.88 | 8.25 | 2.60 | 3.24 | 9.14 | 135.09 |
| 3d1717b | 38.48 | 17.39 | 19.71 | 17.95 | 7.81 | 9.02 | 7.79 | 7.83 | 2.61 | 3.25 | 9.43 | 135.63 |
| 7a873c8 | 38.52 | 16.70 | 19.43 | 17.06 | 8.13 | 8.90 | 7.84 | 7.79 | 2.65 | 3.27 | 9.22 | 134.95 |
| 02510a2 | 37.65 | 16.98 | 18.91 | 16.73 | 7.73 | 8.87 | 8.28 | 7.86 | 2.55 | 3.21 | 9.16 | 132.28 |
| 1e18e27 | 38.27 | 16.75 | 18.91 | 16.92 | 7.86 | 8.94 | 8.19 | 7.86 | 2.59 | 3.27 | 9.19 | 133.31 |
| 2e606ac | 37.93 | 16.74 | 18.93 | 16.71 | 7.73 | 8.94 | 7.84 | 8.17 | 2.62 | 3.33 | 9.18 | 132.62 |
| d5da600 | 37.00 | 16.28 | 18.79 | 16.71 | 7.49 | 9.41 | 8.22 | 8.21 | 2.63 | 3.38 | 9.15 | 131.67 |
| 32b0a2f | 36.77 | 16.23 | 18.64 | 16.38 | 7.54 | 9.11 | 7.95 | 7.91 | 2.51 | 3.10 | 9.38 | 131.65 |
| 963e6bc | 37.07 | 15.18 | 17.58 | 15.42 | 5.69 | 8.95 | 7.76 | 7.62 | 2.56 | 3.07 | 9.44 | 133.32 |
| 594a17b | 36.46 | 15.41 | 17.57 | 15.95 | 5.68 | 8.92 | 7.89 | 7.71 | 2.50 | 3.07 | 9.48 | 132.64 |
| db0e2df | 37.13 | 15.34 | 17.52 | 15.43 | 5.68 | 8.96 | 8.04 | 7.49 | 2.52 | 3.12 | 9.47 | 131.13 |
| 560ca96 | 36.63 | 14.82 | 17.81 | 14.86 | 5.76 | 8.92 | 7.52 | 7.18 | 2.50 | 3.04 | 9.33 | 132.65 |
| 3c33ab0 | 36.52 | 14.57 | 17.59 | 14.78 | 5.71 | 8.95 | 7.47 | 7.54 | 2.59 | 3.02 | 9.45 | 130.81 |
| f49a714 | 36.62 | 14.33 | 18.01 | 14.78 | 5.75 | 9.03 | 7.45 | 7.34 | 2.50 | 3.07 | 9.31 | 131.49 |
| 9b88d0a | 36.45 | 14.52 | 17.79 | 14.78 | 5.92 | 8.80 | 7.55 | 7.45 | 2.54 | 3.04 | 9.35 | 131.83 |
| 0ae60df | 36.12 | 14.55 | 17.90 | 14.84 | 5.79 | 9.19 | 7.52 | 7.29 | 2.69 | 3.05 | 9.30 | 132.52 |
| 411a90d | 36.13 | 14.38 | 17.56 | 14.62 | 5.68 | 9.17 | 7.53 | 7.47 | 2.55 | 3.02 | 9.62 | 131.19 |
| 05f8105 | 36.27 | 14.50 | 17.56 | 14.79 | 5.88 | 8.84 | 7.50 | 7.27 | 2.55 | 3.06 | 9.42 | 132.07 |
| 901d72e | 37.84 | 14.66 | 18.00 | 14.73 | 6.05 | 9.16 | 7.36 | 7.42 | 2.57 | 3.06 | 9.57 | 135.22 |
| b8468a7 | 37.53 | 14.77 | 18.06 | 14.52 | 6.07 | 9.03 | 7.49 | 7.40 | 2.59 | 3.08 | 9.56 | 134.49 |
| ae01521 | 37.54 | 14.79 | 17.99 | 14.76 | 5.91 | 8.04 | 6.66 | 6.43 | 2.52 | 3.05 | 9.59 | 135.01 |
| eb92f4a | 37.57 | 14.57 | 17.87 | 14.60 | 5.91 | 7.96 | 6.67 | 6.61 | 2.56 | 3.12 | 9.66 | 136.95 |
| 6bb1cec | 37.54 | 14.58 | 18.26 | 14.62 | 5.80 | 8.08 | 6.77 | 6.61 | 2.57 | 3.09 | 9.63 | 135.12 |
| e3520f9 | 37.15 | 14.85 | 18.12 | 14.53 | 5.77 | 7.86 | 6.64 | 6.60 | 2.56 | 3.07 | 9.70 | 135.60 |

## The next chunk's source prefetched

Ten alternating rounds, 901d72e against 05f8105, median of same-round ratios / minimum to minimum. Rows under 0.1 ms
are left out here and below.

| Scenario | 05f8105, min ms | 901d72e |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 36.18 | +2.4% / +2.7% |
| 2.8 MP -> 1920x1280 RGB32 | 6.75 | +0.4% / +0.8% |
| 4K -> 1600x900 RGB32 | 13.96 | +1.8% / +2.1% |
| 4K -> 720p RGB32 | 12.63 | +3.5% / +2.4% |
| 720p -> 4K RGBA32 | 8.80 | -1.1% / -0.5% |
| 720p -> 4K RGB32 | 7.34 | -0.5% / +0.0% |
| 1080p -> 240p RGB32 | 3.01 | +1.6% / +1.8% |
| 1080p -> 120 px RGB32 | 2.42 | +0.2% / +1.3% |
| 21 MP -> 175 px RGB32 | 23.91 | +2.7% / +2.7% |
| Native size | 0.22 | -4.8% / +2.4% |
| 4K -> 1080p Grayscale8 | 5.68 | +1.9% / +2.0% |
| 4K -> 1080p RGB24 | 14.58 | -0.4% / -0.8% |
| 4K -> 1080p RGB32 | 14.31 | +1.3% / +1.4% |
| 4K -> 1080p RGBA32 | 17.53 | +1.4% / +1.7% |
| 720p -> 4K Grayscale8 | 2.50 | +2.4% / +2.0% |
| 720p -> 4K RGB24 | 7.17 | +0.1% / +0.9% |
| 1080p -> 4K NN RGB32 | 1.60 | +2.0% / +3.7% |
| 1080p -> 4K NN RGBA32 | 1.78 | -0.4% / -1.8% |
| 640x480 -> 1080p NN RGB32 | 0.25 | -3.7% / -2.1% |
| 640x480 -> 1080p NN RGBA32 | 0.29 | -0.2% / -1.4% |
| 4K -> 64x64 RGB32 | 9.26 | +3.3% / +3.5% |
| 101 MP -> 720p RGB32 | 130.48 | +3.1% / +2.9% |
| 24 MP -> 1080p RGB32, threads | 10.07 | +2.8% / +2.7% |
| 4K -> 1080p RGBA32, threads | 4.93 | -11.2% / +2.3% |
| 4K -> 1080p RGB24, threads | 4.15 | -1.0% / -0.8% |
| 720p -> 4K RGBA32, threads | 2.57 | -0.1% / -0.1% |
| 720p -> 4K RGB24, threads | 2.15 | +1.4% / +1.0% |
| 1080p -> 240p RGB32, threads | 0.95 | +1.0% / +1.2% |
| 4K -> 64x64 RGB32, threads | 3.34 | +2.9% / +2.9% |
| 101 MP -> 720p RGB32, threads | 36.18 | +2.2% / +2.4% |
| 1080p -> 4K NN RGB32, threads | 1.02 | -1.8% / -0.6% |
| 1080p -> 4K NN RGBA32, threads | 1.01 | -3.4% / -0.9% |
- RGB32 and RGBA32 downscales +1.3% to +3.5%, the threaded ones too. Grayscale8 +2%. RGB24 and the colour upscales level.

e3520f9 with the prefetch call removed, and with 128-byte steps, the M1's line size; ten alternating rounds:

| Scenario | e3520f9, min ms | No prefetch | 128-byte steps |
|---|---:|---:|---:|
| 24 MP -> 1080p RGB32 | 37.12 | -2.7% / -2.9% | -2.0% / -1.9% |
| 2.8 MP -> 1920x1280 RGB32 | 6.78 | -0.2% / -0.3% | +0.2% / -0.0% |
| 4K -> 1600x900 RGB32 | 14.21 | -1.3% / -1.8% | -1.7% / -2.0% |
| 4K -> 720p RGB32 | 12.93 | -2.7% / -2.3% | -2.0% / -2.1% |
| 720p -> 4K RGBA32 | 7.86 | -1.3% / -0.4% | -2.1% / -0.6% |
| 720p -> 4K RGB32 | 6.56 | -0.6% / -1.0% | -0.2% / -1.4% |
| 1080p -> 240p RGB32 | 3.07 | -4.4% / -4.1% | -1.2% / -1.8% |
| 1080p -> 120 px RGB32 | 2.44 | -1.6% / -1.1% | -1.6% / -1.0% |
| 21 MP -> 175 px RGB32 | 24.51 | -0.7% / -1.7% | -1.8% / -1.8% |
| 4K -> 200 px RGB32 | 9.89 | -3.0% / -2.9% | -2.2% / -2.7% |
| 4K -> 200 px RGBA32 | 12.09 | -2.2% / -2.1% | -2.0% / -1.9% |
| Native size | 0.22 | +5.8% / +2.9% | +10.7% / +10.5% |
| 4K -> 1080p Grayscale8 | 5.77 | -1.5% / -1.7% | -0.4% / -0.2% |
| 4K -> 1080p RGB24 | 14.47 | +0.2% / +1.0% | -0.1% / +0.2% |
| 4K -> 1080p RGB32 | 14.59 | -1.6% / -1.8% | -1.6% / -2.1% |
| 4K -> 1080p RGBA32 | 17.80 | -1.6% / -1.5% | -1.5% / -1.6% |
| 720p -> 4K Grayscale8 | 2.52 | -1.2% / -0.2% | +0.7% / +0.6% |
| 720p -> 4K RGB24 | 6.54 | -3.3% / -3.7% | -1.3% / -1.9% |
| 1080p -> 4K NN RGB32 | 1.60 | -0.3% / +4.1% | -0.1% / +5.6% |
| 1080p -> 4K NN RGBA32 | 1.81 | +0.3% / +1.1% | -0.5% / -3.0% |
| 640x480 -> 1080p NN RGB32 | 0.25 | -0.2% / -1.9% | -2.0% / -1.1% |
| 640x480 -> 1080p NN RGBA32 | 0.30 | -1.4% / -2.3% | -0.2% / -1.3% |
| 4K -> 64x64 RGB32 | 9.55 | -2.2% / -2.4% | -1.9% / -1.7% |
| 101 MP -> 720p RGB32 | 134.25 | -1.9% / -2.3% | -1.4% / -1.3% |
| 24 MP -> 1080p RGB32, threads | 10.32 | -2.7% / -2.5% | -1.8% / -1.7% |
| 4K -> 1080p RGBA32, threads | 5.03 | -2.4% / -2.2% | -2.2% / -1.8% |
| 4K -> 1080p RGB24, threads | 4.13 | +0.2% / +0.2% | -0.4% / -0.4% |
| 720p -> 4K RGBA32, threads | 2.31 | +0.2% / +0.1% | -0.3% / -0.9% |
| 720p -> 4K RGB24, threads | 1.93 | -2.0% / -2.0% | -0.1% / -1.1% |
| 1080p -> 240p RGB32, threads | 0.96 | -2.8% / -2.3% | -0.8% / -0.3% |
| 4K -> 64x64 RGB32, threads | 3.43 | -2.3% / -2.2% | -2.3% / -2.3% |
| 101 MP -> 720p RGB32, threads | 36.94 | -1.1% / -1.6% | -1.3% / -1.2% |
| 1080p -> 4K NN RGB32, threads | 1.00 | -0.4% / +0.2% | +1.7% / +1.4% |
| 1080p -> 4K NN RGBA32, threads | 1.01 | +2.2% / +3.7% | -1.3% / -0.9% |
| 4K -> 200 px RGB32, threads | 3.26 | -3.0% / -3.0% | -2.9% / -2.7% |
| 4K -> 200 px RGBA32, threads | 3.91 | -2.5% / -2.1% | -2.5% / -1.8% |
- No row slower beyond the noise: on the M1 the prefetch only costs.
- 128-byte steps recover about three quarters of it on most downscales; a quarter on 1080p -> 240p, two fifths on
  720p -> 4K RGB24.

## A run holds its weights' address, and the x runs start in order

Ten alternating rounds, 32b0a2f against d5da600, in the session of 901d72e's:

| Scenario | d5da600, min ms | 32b0a2f |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 36.55 | -0.0% / +0.1% |
| 4K -> 1080p RGB32 | 16.23 | -1.9% / -1.8% |
| 720p -> 4K RGBA32 | 9.41 | -6.6% / -5.6% |
| 720p -> 4K RGB32 | 8.19 | -5.4% / -5.8% |
| 1080p -> 1440p RGB32 | 5.93 | -2.2% / -2.3% |
| 1080p -> 240p RGB32 | 3.14 | -1.3% / -2.2% |
| 1080p -> 120 px RGB32 | 2.36 | +2.7% / +2.3% |
| 21 MP -> 175 px RGB32 | 23.16 | +2.8% / +4.0% |
| Native size | 0.24 | -20.2% / -7.8% |
| 4K -> 1080p Grayscale8 | 7.49 | +1.4% / +0.6% |
| 4K -> 1080p RGB24 | 16.55 | -1.6% / -1.6% |
| 4K -> 1080p RGB32 | 16.23 | -1.4% / -1.4% |
| 4K -> 1080p RGBA32 | 18.51 | -1.2% / -1.3% |
| 720p -> 4K Grayscale8 | 2.63 | -4.1% / -4.8% |
| 720p -> 4K RGB24 | 8.20 | -5.7% / -4.8% |
| 4K -> 64x64 RGB32 | 9.05 | +3.5% / +2.7% |
| 101 MP -> 720p RGB32 | 130.97 | -0.5% / +0.1% |
| 24 MP -> 1080p RGB32, threads | 10.20 | -0.1% / -0.3% |
| 4K -> 1080p RGBA32, threads | 5.17 | -1.5% / -1.5% |
| 4K -> 1080p RGB24, threads | 4.65 | -1.9% / -2.0% |
| 720p -> 4K RGBA32, threads | 2.75 | -5.3% / -5.5% |
| 720p -> 4K RGB24, threads | 2.40 | -4.8% / -4.5% |
| 1080p -> 1440p RGB32, threads | 1.73 | -32.6% / -1.6% |
| 1080p -> 240p RGB32, threads | 0.99 | -1.9% / -1.9% |
| 4K -> 64x64 RGB32, threads | 3.24 | +3.1% / +2.5% |
| 101 MP -> 720p RGB32, threads | 36.55 | -0.2% / -0.5% |
- 4K -> 64x64 +3.5%, threaded +3.1%; 21 MP -> 175 px wide +2.8%; 1080p -> 120 px wide +2.7%.
- Upscales -4.1% to -6.6%, threaded -5%.
- The rest within 2%.

## NEON stores its output blocks with `STNP`

e3520f9 with `STP` for `STNP`, the same instructions otherwise: 151 `STNP` in e3520f9, none in the variant. Ten
alternating rounds:

| Scenario | e3520f9, min ms | `STP` |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 37.14 | -0.3% / +0.0% |
| 2.8 MP -> 1920x1280 RGB32 | 6.80 | +0.7% / +0.7% |
| 4K -> 1600x900 RGB32 | 14.26 | -0.4% / +0.1% |
| 4K -> 720p RGB32 | 12.92 | +0.4% / +0.5% |
| 720p -> 4K RGBA32 | 7.86 | -0.2% / +0.0% |
| 720p -> 4K RGB32 | 6.55 | -1.3% / -0.8% |
| 1080p -> 240p RGB32 | 3.07 | -0.8% / -0.7% |
| 1080p -> 120 px RGB32 | 2.44 | +1.0% / +0.6% |
| 21 MP -> 175 px RGB32 | 24.52 | +0.0% / +0.0% |
| 4K -> 200 px RGB32 | 9.89 | -0.4% / -0.3% |
| 4K -> 200 px RGBA32 | 12.09 | -0.1% / -0.1% |
| Native size | 0.24 | -1.5% / -2.1% |
| 4K -> 1080p Grayscale8 | 5.77 | +0.4% / +0.6% |
| 4K -> 1080p RGB24 | 14.49 | +0.9% / +0.5% |
| 4K -> 1080p RGB32 | 14.52 | -0.1% / +0.7% |
| 4K -> 1080p RGBA32 | 17.80 | +0.3% / +0.1% |
| 720p -> 4K Grayscale8 | 2.53 | -0.6% / +0.3% |
| 720p -> 4K RGB24 | 6.51 | -0.8% / -0.5% |
| 1080p -> 4K NN RGB32 | 1.67 | +0.3% / +0.7% |
| 1080p -> 4K NN RGBA32 | 1.81 | -0.1% / +0.1% |
| 640x480 -> 1080p NN RGB32 | 0.24 | -0.4% / -1.0% |
| 640x480 -> 1080p NN RGBA32 | 0.29 | +1.9% / +0.1% |
| 4K -> 64x64 RGB32 | 9.57 | +0.7% / -0.1% |
| 101 MP -> 720p RGB32 | 133.29 | -0.3% / +0.6% |
| 24 MP -> 1080p RGB32, threads | 10.33 | +0.1% / +0.3% |
| 4K -> 1080p RGBA32, threads | 5.02 | +0.6% / +0.6% |
| 4K -> 1080p RGB24, threads | 4.14 | +0.5% / +0.4% |
| 720p -> 4K RGBA32, threads | 2.31 | +0.4% / -0.3% |
| 720p -> 4K RGB24, threads | 1.94 | +1.0% / -0.4% |
| 1080p -> 240p RGB32, threads | 0.96 | -0.3% / +0.2% |
| 4K -> 64x64 RGB32, threads | 3.42 | +0.3% / +0.3% |
| 101 MP -> 720p RGB32, threads | 36.99 | -0.0% / +0.0% |
| 1080p -> 4K NN RGB32, threads | 1.04 | -0.9% / -1.9% |
| 1080p -> 4K NN RGBA32, threads | 1.02 | -7.0% / -0.4% |
| 4K -> 200 px RGB32, threads | 3.26 | -0.2% / -0.3% |
| 4K -> 200 px RGBA32, threads | 3.91 | -0.1% / +0.5% |
- Every row within 1.3%, the threaded upscales included: the hint neither costs nor pays here.

## Column strips (b9fea7e)

The ring budget is half the smallest L2 share: the E-cores' 1 MB, so 512 KB. A P-core's share is 3 MB.

A sweep at a1f1e22, single-threaded, through `Detail::resizeWithRingBudget`: the budgets alternating, ten rounds of three
resizes each, a fresh destination per resize. Median ms, the strip count in parentheses; min and median within 1%.

| Scenario | y run | 256 KB | 512 KB | 1 MB | 1.5 MB | 64 MB |
|---|---:|---:|---:|---:|---:|---:|
| 101 MP -> 1280x720 RGB32 | 73 | 134.18 (5) | 132.00 (3) | 131.57 (2) | 129.20 (1) | 129.07 (1) |
| 101 MP -> 1920x1440 RGB32 | 37 | 141.79 (4) | 140.29 (2) | 139.20 (1) | 139.22 (1) | 139.13 (1) |
| 101 MP -> 3840x2880 RGB32 | 19 | 169.32 (4) | 164.61 (2) | 163.29 (1) | 162.09 (1) | 162.19 (1) |
| 50 MP -> 3840x2880 RGBA32 | 13 | 110.79 (4) | 107.98 (2) | 106.44 (1) | 106.28 (1) | 106.65 (1) |

- One strip against 512 KB's 2-3: 0.8-2.3% faster. Fewer strips that are still several gain little: 0.3% from 3 to 2.
- A single-strip ring here is 0.8-1.4 MB: past 1.5 MB the budget changes nothing.

The benchmark, a1f1e22 with the budget fixed at 1.5 MB, ten alternating rounds. 101 MP -> 720p is the only row 512 KB
splits; the rest of 2 ms and more within 1%, 720p -> 4K Grayscale8 aside (+2.0% / +0.9%).

| Scenario | a1f1e22, min ms | 1.5 MB |
|---|---:|---:|
| 101 MP -> 720p RGB32 | 132.31 | -1.7% / -1.5% |
| 101 MP -> 720p RGB32, threads | 37.00 | -2.1% / -2.4% |

## A row pair converted in one loop

a8253c4 against 67935c2, ten alternating rounds, median of same-round ratios / minimum to minimum. Rows under 0.1 ms are
left out. Nearest neighbor converts nothing: its rows are the control.

| Scenario | 67935c2, min ms | a8253c4 |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 37.13 | -0.6% / -0.9% |
| 2.8 MP -> 1920x1280 RGB32 | 6.80 | -1.0% / -0.4% |
| 4K -> 1600x900 RGB32 | 14.24 | -1.1% / -1.2% |
| 4K -> 720p RGB32 | 12.95 | -0.9% / -1.1% |
| 720p -> 4K RGBA32 | 7.86 | -1.4% / +0.5% |
| 720p -> 4K RGB32 | 6.60 | +0.6% / -1.8% |
| 1080p -> 240p RGB32 | 3.07 | -0.9% / -0.3% |
| 1080p -> 120 px RGB32 | 2.44 | +0.2% / +0.7% |
| 21 MP -> 175 px RGB32 | 24.50 | -0.2% / -0.5% |
| 4K -> 200 px RGB32 | 9.85 | -0.2% / -0.2% |
| 4K -> 200 px RGBA32 | 12.07 | -1.4% / -1.1% |
| Native size | 0.22 | -1.7% / +11.5% |
| 4K -> 1080p Grayscale8 | 5.76 | -1.7% / -2.2% |
| 4K -> 1080p RGB24 | 14.50 | -0.1% / -0.1% |
| 4K -> 1080p RGB32 | 14.48 | -1.2% / -0.9% |
| 4K -> 1080p RGBA32 | 17.78 | -0.4% / -0.6% |
| 720p -> 4K Grayscale8 | 2.52 | -0.2% / -0.4% |
| 720p -> 4K RGB24 | 6.51 | -1.6% / -2.2% |
| 1080p -> 4K NN RGB32 | 1.61 | +1.3% / -0.4% |
| 1080p -> 4K NN RGBA32 | 1.75 | -0.5% / +0.2% |
| 640x480 -> 1080p NN RGB32 | 0.24 | +0.3% / +0.8% |
| 640x480 -> 1080p NN RGBA32 | 0.29 | +0.2% / -2.3% |
| 4K -> 64x64 RGB32 | 9.54 | -1.2% / -1.3% |
| 101 MP -> 720p RGB32 | 134.16 | -0.7% / -1.1% |
| 24 MP -> 1080p RGB32, threads | 10.35 | -0.7% / -0.9% |
| 4K -> 1080p RGBA32, threads | 5.01 | -0.8% / -0.8% |
| 4K -> 1080p RGB24, threads | 4.13 | -0.0% / +0.3% |
| 720p -> 4K RGBA32, threads | 2.31 | +0.5% / +0.2% |
| 720p -> 4K RGB24, threads | 1.94 | -1.3% / -2.1% |
| 1080p -> 240p RGB32, threads | 0.96 | -0.6% / -0.4% |
| 4K -> 64x64 RGB32, threads | 3.42 | -1.3% / -0.8% |
| 101 MP -> 720p RGB32, threads | 36.97 | -0.1% / -0.2% |
| 1080p -> 4K NN RGB32, threads | 1.01 | -0.5% / +1.6% |
| 1080p -> 4K NN RGBA32, threads | 1.03 | +1.5% / -3.3% |
| 4K -> 200 px RGB32, threads | 3.25 | -0.9% / -0.5% |
| 4K -> 200 px RGBA32, threads | 3.94 | -1.6% / -1.5% |
- Level to 2% faster: the filtered rows +0.7% to -2.2%, none slower beyond the control's spread of 1.5%. The Pi's
  Clang build was level too.
- Clang's NEON kernels 12 KB smaller.

## Row pairs interleaved at AVX2, at padded lengths

8bba85d with `interleavedRowPairs` true at NEON, against 8bba85d: ten alternating rounds. Its tests pass. The 12-tap
RGB32 column loop takes 56 instructions for its 24 multiply-adds, against 64.

| Scenario | 8bba85d, min ms | Interleaved |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 37.12 | -0.5% / -0.8% |
| 2.8 MP -> 1920x1280 RGB32 | 6.79 | -1.4% / -1.4% |
| 4K -> 1600x900 RGB32 | 14.23 | -2.3% / -2.3% |
| 4K -> 720p RGB32 | 12.96 | -3.6% / -3.9% |
| 720p -> 4K RGBA32 | 7.88 | +7.3% / +7.7% |
| 720p -> 4K RGB32 | 6.53 | +5.2% / +5.5% |
| 1080p -> 240p RGB32 | 3.08 | +0.9% / +0.3% |
| 1080p -> 120 px RGB32 | 2.44 | +0.5% / -0.1% |
| 21 MP -> 175 px RGB32 | 24.50 | +0.9% / +0.1% |
| 4K -> 200 px RGB32 | 9.88 | +0.2% / -0.0% |
| 4K -> 200 px RGBA32 | 12.10 | -0.0% / -0.2% |
| Native size | 0.23 | +2.0% / +17.1% |
| 4K -> 1080p Grayscale8 | 5.78 | -0.1% / -0.1% |
| 4K -> 1080p RGB24 | 14.49 | +0.8% / +0.8% |
| 4K -> 1080p RGB32 | 14.53 | -0.4% / -0.0% |
| 4K -> 1080p RGBA32 | 17.78 | +0.3% / +0.3% |
| 720p -> 4K Grayscale8 | 2.52 | +0.0% / -0.0% |
| 720p -> 4K RGB24 | 6.48 | +3.3% / +4.8% |
| 1080p -> 4K NN RGB32 | 1.60 | +0.5% / +6.8% |
| 1080p -> 4K NN RGBA32 | 1.84 | +0.4% / +0.6% |
| 640x480 -> 1080p NN RGB32 | 0.25 | -1.1% / -3.1% |
| 640x480 -> 1080p NN RGBA32 | 0.29 | +0.4% / +0.2% |
| 4K -> 64x64 RGB32 | 9.53 | -0.1% / +0.1% |
| 101 MP -> 720p RGB32 | 133.33 | -0.2% / -0.1% |
| 24 MP -> 1080p RGB32, threads | 10.34 | -1.5% / -1.5% |
| 4K -> 1080p RGBA32, threads | 5.02 | +0.6% / +0.8% |
| 4K -> 1080p RGB24, threads | 4.14 | +2.6% / +2.2% |
| 720p -> 4K RGBA32, threads | 2.31 | +4.0% / +4.3% |
| 720p -> 4K RGB24, threads | 1.93 | +2.1% / +2.1% |
| 1080p -> 240p RGB32, threads | 0.96 | -0.0% / +0.2% |
| 4K -> 64x64 RGB32, threads | 3.43 | -0.0% / -0.2% |
| 101 MP -> 720p RGB32, threads | 36.93 | +0.6% / +0.1% |
| 1080p -> 4K NN RGB32, threads | 1.01 | -0.1% / +1.2% |
| 1080p -> 4K NN RGBA32, threads | 1.04 | +0.0% / -2.0% |
| 4K -> 200 px RGB32, threads | 3.24 | +0.3% / +1.0% |
| 4K -> 200 px RGBA32, threads | 3.95 | +0.1% / -0.8% |
- The downscales whose x runs are padded gain by run length: 4K -> 720p (17 taps) -3.6%, 4K -> 1600x900 (14-15) -2.3%,
  2.8 MP -> 1920x1280 (6-7) -1.4%; 4K -> 1080p (12) level.
- The upscales lose, their 4-tap runs filtered three at a time over pixel pairs: 720p -> 4K RGBA32 +7.3%, RGB32 +5.2%,
  RGB24 +3.3%.
- Grayscale8, the runs read per run (over 32 taps) and nearest neighbor: within 1%.

The same against a1f1e22, a8253c4's conversion in both: ten alternating rounds. The variant's tests pass.

| Scenario | a1f1e22, min ms | Interleaved |
|---|---:|---:|
| 24 MP -> 1080p RGB32 | 36.85 | +0.3% / +0.2% |
| 2.8 MP -> 1920x1280 RGB32 | 6.79 | -1.2% / -1.3% |
| 4K -> 1600x900 RGB32 | 14.11 | -1.7% / -1.2% |
| 4K -> 720p RGB32 | 12.81 | -2.8% / -2.7% |
| 720p -> 4K RGBA32 | 7.88 | +8.0% / +8.1% |
| 720p -> 4K RGB32 | 6.48 | +5.6% / +6.0% |
| 1080p -> 240p RGB32 | 3.05 | +1.9% / +1.4% |
| 1080p -> 120 px RGB32 | 2.45 | +0.6% / +0.8% |
| 21 MP -> 175 px RGB32 | 24.34 | +1.3% / +0.2% |
| 4K -> 200 px RGB32 | 9.82 | +0.4% / +0.1% |
| 4K -> 200 px RGBA32 | 11.95 | +0.1% / +0.0% |
| Native size | 0.30 | -1.1% / -23.2% |
| 4K -> 1080p Grayscale8 | 5.63 | +0.1% / +0.2% |
| 4K -> 1080p RGB24 | 14.48 | +0.4% / +0.8% |
| 4K -> 1080p RGB32 | 14.37 | +0.7% / +1.0% |
| 4K -> 1080p RGBA32 | 17.64 | +0.9% / +0.9% |
| 720p -> 4K Grayscale8 | 2.51 | +0.3% / +0.2% |
| 720p -> 4K RGB24 | 6.35 | +5.9% / +6.6% |
| 1080p -> 4K NN RGB32 | 1.60 | -2.7% / +0.5% |
| 1080p -> 4K NN RGBA32 | 1.84 | +0.3% / +0.2% |
| 640x480 -> 1080p NN RGB32 | 0.25 | -4.0% / -1.7% |
| 640x480 -> 1080p NN RGBA32 | 0.30 | -0.5% / -0.6% |
| 4K -> 64x64 RGB32 | 9.42 | +0.4% / +0.0% |
| 101 MP -> 720p RGB32 | 132.78 | -0.5% / -0.1% |
| 24 MP -> 1080p RGB32, threads | 10.28 | -0.8% / -0.8% |
| 4K -> 1080p RGBA32, threads | 4.96 | +1.8% / +2.0% |
| 4K -> 1080p RGB24, threads | 4.14 | +2.6% / +2.1% |
| 720p -> 4K RGBA32, threads | 2.30 | +3.4% / +4.3% |
| 720p -> 4K RGB24, threads | 1.89 | +3.4% / +4.1% |
| 1080p -> 240p RGB32, threads | 0.95 | +1.1% / +1.2% |
| 4K -> 64x64 RGB32, threads | 3.40 | -0.2% / -0.1% |
| 101 MP -> 720p RGB32, threads | 36.92 | -0.5% / -0.6% |
| 1080p -> 4K NN RGB32, threads | 1.01 | +0.8% / +2.5% |
| 1080p -> 4K NN RGBA32, threads | 1.01 | +2.2% / +4.9% |
| 4K -> 200 px RGB32, threads | 3.23 | +0.3% / +0.3% |
| 4K -> 200 px RGBA32, threads | 3.87 | +0.1% / +0.5% |
- a8253c4 took up to 2% off the per-row conversion and nothing off the pair conversion the variant takes: the downscale
  gains shrink, 4K -> 720p -2.8%, 4K -> 1600x900 -1.7%, 2.8 MP -> 1920x1280 -1.2%.
- The 12- and 27-tap rows turn slower: 4K -> 1080p RGB32 +0.7%, RGBA32 +0.9% (threaded +1.8%), 1080p -> 240p +1.9%.
- The upscales +5.6% to +8.0%, threaded +3.4%.

## The multiply-add ceiling

Sections N, S and U of `multiply_add_ceiling.cpp` at e3520f9, `clang++ -O2 -std=c++20`: the tables are in
[throughput-ceiling.md](../throughput-ceiling.md). The core's figures behind them:
- Clock: 3.20 GHz, from a chain of 16 dependent integer adds per iteration, one cycle each; 3.198 and 3.199 GHz.
- Peak: 51.3 G/s by U12, 16 floats per cycle: four 128-bit multiply-adds.
- Loads, 12 independent `ldr q` per iteration from L1: 3.00 per cycle at 16-byte steps, aligned or 4 bytes off; 2.49
  at 64-byte steps; 1.82 at 128-byte steps, a line each. `ldp` of two q registers: 3.00 q registers per cycle.
- Section U's lines take about 1 ms. At the default 30 repeats two runs differ by up to 12% on a line; at 200, within
  2%, U11 4%, U13's lines 5.6%. A run of 3 repeats read U0 at twice its time: the first repeats run slow.

## Per-instruction-set primitives, not SIMDe (963b54b)

Real M1, Apple Clang, three alternating rounds of 4b5fdf8 and 963b54b: -2% to -12%, the scalar rows within 1%.

## Source rows converted to float once (d436d41)

macos-latest, resizer / QImage single-threaded, f435b80 -> d436d41:

| 24 MP -> 1080p | 4K -> 1080p | 4K -> 64x64 | 101 MP -> 720p |
|---|---|---|---|
| 3.91 -> 2.50 | 3.72 -> 2.90 | 3.87 -> 1.61 | 6.14 -> 1.86 |

## CI, 8c9e194 to eeb1af6 (2026-10-05 to 2026-10-08)

Three attempts per commit, two of 8c9e194's and 680a5e5's on an M2 Pro. No step is readable: one commit's three samples
of a row span up to 2x (720p -> 4K RGBA32 with threads at eeb1af6: 6.75, 10.79, 17.37 ms), and the resizer / QImage
ratios move 20-40% between adjacent commits in both directions.

`STNP` (eeb1af6) shows no collapse: each row's best sample is at or below 1ed9965's on the upscales, threaded or not.

## CI, eeb1af6 to 3c33ab0 (2026-10-08 to 2026-10-16)

Three attempts per commit; the one on an M2 Pro is left out. No step is readable. Medians, 3c33ab0 against eeb1af6:
- Upscales -25% to -40%, 2x downscales -22% to -45%.
- 24 MP -> 1080p -24%, 101 MP -> 720p -12%, 1080p -> 240p -8%, 4K -> 64x64 0%.
- 1080p at native size: +24%, against +3% and 0% on the Neoverse N2.
