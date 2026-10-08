# Celeron N4100

The measurement log for this CPU. Conclusions that shape the design are summarized in
[resize-performance.md](../resize-performance.md); the detail stays here.

## Machine

| | |
|---|---|
| CPU | Celeron N4100 (Goldmont Plus), 4 cores, 4 threads, 1.1 GHz nominal, 2.4 GHz burst |
| SIMD | SSE4.2; no AVX, no FMA. The SSE4.1 kernels are the only level it runs |
| Caches | 24 KB L1D, 4 MB L2 shared by all cores |
| Detected sizing | 1024 KB of L2 per logical processor, ring budget 512 KB |
| RAM | 3.9 GB. During the runs about 1 GB free, and a further 770 MB of standby cache. The 101 MP source alone is 404 MB |
| Toolchain | MSVC 19.51.36260, Qt 6.12.0 |
| OS | Windows 11 |

## Reading this machine's numbers

- Run-to-run noise: 0.5-1% on a quiet machine, QImage controls included.
- A full benchmark run takes 70-75 s. A run of 85 s or more was disturbed.
- The first benchmark of a session can run far slow once: 24 MP -> 1080p took 434 ms in one round against 261 in the
  three after it. Read that row by its minimum.
- A remote-desktop session inflates everything: 4K -> 1080p RGB32 measured 198 ms with one attached, 115 without.
- Builds take about 100 s per binary.

## Standing against Qt, a6c69e7

MSVC, minimum ms of three rounds. Since b8892fc: two chains with the assigned first block, shared weights, the 8-float
step for every layout, the pass's steps looped over the rows, and the four changes of the next section.

| Scenario | Resizer | QImage | Ratio | Threads | Ratio | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 206.8 | 71.1 | 2.91 | 58.5 | 0.82 | 3.54x |
| 4K -> 1080p RGB32 | 90.8 | 29.9 | 3.04 | - | - | - |
| 4K -> 1080p RGBA32 | 118.8 | 79.7 | 1.49 | 34.1 | 0.43 | 3.49x |
| 4K -> 1080p RGB24 | 92.4 | 73.5 | 1.26 | 26.2 | 0.36 | 3.52x |
| 4K -> 1080p Grayscale8 | 47.1 | 84.0 | 0.56 | - | - | - |
| 720p -> 4K RGBA32 | 77.6 | 213.0 | 0.36 | 25.7 | 0.12 | 3.02x |
| 720p -> 4K RGB32 | 60.3 | 83.3 | 0.72 | - | - | - |
| 720p -> 4K RGB24 | 54.9 | 107.5 | 0.51 | 19.3 | 0.18 | 2.85x |
| 720p -> 4K Grayscale8 | 26.4 | 93.8 | 0.28 | - | - | - |
| 1080p -> 1440p | 43.3 | 37.7 | 1.15 | 14.3 | 0.38 | 3.04x |
| 1080p -> 240p | 17.9 | 4.73 | 3.79 | 5.46 | 1.16 | 3.28x |
| 4K -> 64x64 | 59.9 | 19.5 | 3.08 | 21.9 | 1.12 | 2.74x |
| 101 MP -> 720p | 813.8 | 213.3 | 3.82 | 242.8 | 1.14 | 3.35x |
| 1080p, native size | 5.26 | 5.77 | 0.91 | - | - | - |

- Against b8892fc below, ms: 4K -> 1080p RGB32 115.2 -> 90.8, 24 MP -> 1080p 260.5 -> 206.8, 1080p -> 240p 23.0 -> 17.9,
  4K -> 64x64 70.4 -> 59.9, 101 MP -> 720p 913 -> 814, 4K -> 1080p Grayscale8 59.5 -> 47.1. With threads: 24 MP -> 1080p
  74.3 -> 58.5, 1080p -> 240p 7.7 -> 5.46.
- Upscales: 720p -> 4K RGB32 66.3 -> 60.3, 1080p -> 1440p 47.3 -> 43.3.
- With threads, the 24 MP downscale beats Qt; 1080p -> 240p, 4K -> 64x64 and 101 MP stay behind it.
- 101 MP -> 720p with threads varies by session: one binary's minimum ranged 284-338 ms between the sessions at 93775b0.

## 2026-10-05, after 74c02b3: clang-cl, and four changes

clang-cl 22.1.3 (the Visual Studio install's) beside MSVC 19.51. "Before" is 74c02b3, "after" the tree with the four
changes below. Minimum ms of three alternating rounds of the four binaries.

| Scenario | MSVC before | MSVC after | clang-cl before | clang-cl after | QImage |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 213.3 | 206.8 | 215.7 | 213.9 | 71.0 |
| 4K -> 1080p RGB32 | 94.7 | 90.8 | 96.6 | 95.7 | 29.6 |
| 4K -> 1080p RGBA32 | 125.1 | 118.8 | 118.4 | 112.4 | 79.4 |
| 4K -> 1080p RGB24 | 93.8 | 92.4 | 97.8 | 96.0 | 72.8 |
| 4K -> 1080p Grayscale8 | 52.7 | 47.1 | 47.2 | 41.4 | 82.9 |
| 720p -> 4K RGBA32 | 83.3 | 77.6 | 80.7 | 75.7 | 213.0 |
| 720p -> 4K RGB32 | 63.3 | 60.3 | 63.9 | 60.7 | 83.3 |
| 720p -> 4K RGB24 | 58.4 | 54.9 | 58.7 | 56.0 | 107.1 |
| 720p -> 4K Grayscale8 | 29.3 | 26.4 | 26.5 | 24.6 | 93.5 |
| 1080p -> 1440p | 45.4 | 43.3 | 45.0 | 43.9 | 37.7 |
| 1080p -> 240p | 19.0 | 17.9 | 18.8 | 18.5 | 4.71 |
| 4K -> 64x64 | 62.3 | 59.9 | 60.6 | 59.3 | 19.2 |
| 101 MP -> 720p | 844.2 | 813.7 | 835.8 | 819.9 | 213.0 |
| 24 MP -> 1080p, threads | 59.5 | 58.5 | 60.2 | 58.6 | - |
| 4K -> 1080p RGBA32, threads | 35.9 | 34.1 | 33.1 | 32.3 | - |
| 4K -> 1080p RGB24, threads | 26.1 | 26.2 | 28.1 | 27.9 | - |
| 720p -> 4K RGBA32, threads | 26.7 | 25.7 | 26.8 | 26.3 | - |
| 1080p -> 240p, threads | 5.65 | 5.46 | 5.76 | 5.55 | - |
| 4K -> 64x64, threads | 22.6 | 21.8 | 22.0 | 22.0 | - |

- The compilers before: within 2-4% on the 3-channel rows; clang-cl ahead by 10% on both Grayscale8 rows and 5% on the
  RGBA32 downscale. Upscale rows ran about 10% slower in this session than in the day's first for both compilers; same-session
  figures only.
- After: MSVC ahead by 4-5% on the RGB32 and RGB24 downscales; clang-cl ahead by 12% on the Grayscale8 downscale, 7% on
  the Grayscale8 upscale and 5% on the RGBA32 downscale. Neither gap is explained.
- The tests pass under both.

The four changes, each measured alone against its compiler's baseline, minimum of two rounds:

| Change | MSVC | clang-cl |
|---|---|---|
| Signed pack to words, no clamp to 255 | Upscales -6% to -8%, downscales -1% to -4% | Upscales -2% to -6% |
| SSE4.1 conversion: each widening loads its 4 bytes | 4-byte downscales -3% to -5% | Within 2%, either way |
| `storeTempPixelWithTaps`: the sums as scalars | 4K -> 1080p Grayscale8 53.1 -> 48.0 | No change |
| Short-run pass: the four columns by name | 720p -> 4K Grayscale8 28.9 -> 27.0 | 26.3 -> 25.3 |

- The last two came from the listings. In the one-channel kernels MSVC stored each column's sums to the stack, reloaded
  them as a scalar and again as an integer to copy them out; and it ran the short-run pass's four columns as a loop, with
  the products in stack arrays. clang-cl did neither.

Variants that lost, same method:

| Variant | MSVC | clang-cl |
|---|---|---|
| Vertical pass: the row's nonzero taps listed once per dest row with their weights broadcast, the first tap assigned | Upscales about 0, downscales +2% to +4%: six register moves per tap. With the accumulators zeroed, or passed to a helper by reference: no better than the tree | Upscales -5% to -8% |
| Streaming stores to the destination | Within 1% | RGB32 and RGBA32 upscales -3% to -4%; RGB24, whose blocks are not 16-aligned, +85% |
| The x weights pre-spread 4 per tap in a 16-aligned table, as the multiply's memory operand | 4-byte downscales +2% to +5%, 101 MP +8% | -2% to -4% on some downscales, 101 MP +5%, threaded small rows +20% (the table's construction per call) |
| A prefetch 256 bytes ahead on each vertical tap's row | Within 2% | Within 4%, either way |

- MSVC's vertical tap loop at 74c02b3 is 26 instructions per tap for a 3-channel block, 18 of them the loads, multiplies
  and adds: the rewritten loop is no shorter under MSVC.
- The upscale's vertical pass is not waiting on the destination's memory: streaming stores change nothing under MSVC.

## Two vertical blocks per step at AVX2 (b8bdd30)

SSE4.1 keeps one block. Minimum of three rounds against the build before it:
- MSVC: within 1%, except 4K -> 1080p RGB24 +2.1% and RGB32 +0.5% to +1.4%. Its kernels differ by a few instructions.
- clang-cl: within 1%, except 4K -> 1080p Grayscale8 +4.9%. Its kernels are identical, loop addresses included.

## The vertical taps listed per dest row (not committed)

MSVC, SSE4.1, minimum of three rounds against b8bdd30: every row slower. Upscales +7.3% to +9.3%, 4K -> 1080p RGB32,
RGB24 and RGBA32 +6.4% to +9.3%, 24 MP -> 1080p +4.7%, 101 MP -> 720p +2.2%, 4K -> 1080p Grayscale8 +1.8%.

## The source buffers' range by value in the horizontal filter

SSE4.1, ten alternating rounds, median of same-round ratios, against 93b1b79.

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 4K -> 1080p RGB32 | -3.8%, -3.9% | -7.1%, -7.6% |
| 4K -> 1080p RGB24 | -3.5% | -7.2% |
| 4K -> 1080p RGBA32 | -5.2% | -1.7% |
| 4K -> 1080p Grayscale8 | -6.6% | -4.6% |
| 24 MP -> 1080p | -2.7% | -5.1% |
| 720p -> 4K RGBA32, RGB32, RGB24 | -5.4%, -4.2%, -4.6% | -5.6%, -5.2%, -4.9% |
| 1080p -> 1440p RGB32 | -4.8% | -6.0% |
| 101 MP -> 720p, 4K -> 64x64 | -0.4%, -0.1% | -1.5%, -0.4% |

With threads: -2% to -8%. Measured on the loop before `filterRun` was split from it.

The short-run pass's groups filtered the same way, ten rounds of the pixel-layout rows against 1917611:

| Scenario | MSVC | clang-cl |
|---|---:|---:|
| 720p -> 4K Grayscale8 | -19.3% | -16.8% |
| 4K -> 1080p Grayscale8 | -10.5% (minimum to minimum -1.9%) | +4.5% |
| 3- and 4-channel rows | within 1.4% | within 1.2% |

## 2026-10-04, b8892fc

### Method

- Five binaries, built from deleted outputs: b8892fc; b8892fc with `/QIntel-jcc-erratum` on every source (through the `CL`
  variable); f04db84; 03113a6; 15f240e.
- Four alternating rounds, 10 samples per benchmark. Figures are each binary's minimum of the four rounds, ms.
- An earlier session of four rounds is discarded: its first two rounds ran 20-60% slow on every row, controls included.
  Cause not identified.
- The tests pass at b8892fc: the first run of the SSE4.1 kernels on a CPU with no higher level.

### Standing against Qt

| Scenario | Resizer | QImage | Ratio | Threads | Ratio | Speedup |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 260.5 | 71.1 | 3.66 | 74.3 | 1.04 | 3.51x |
| 4K -> 1080p RGB32 | 115.2 | 29.5 | 3.91 | - | - | - |
| 4K -> 1080p RGBA32 | 137.2 | 80.6 | 1.70 | 40.8 | 0.51 | 3.37x |
| 4K -> 1080p RGB24 | 115.8 | 73.9 | 1.57 | 34.6 | 0.47 | 3.35x |
| 4K -> 1080p Grayscale8 | 59.5 | 83.9 | 0.71 | - | - | - |
| 720p -> 4K RGBA32 | 82.1 | 213.3 | 0.39 | 27.8 | 0.13 | 2.96x |
| 720p -> 4K RGB32 | 66.3 | 82.9 | 0.80 | - | - | - |
| 720p -> 4K RGB24 | 61.4 | 108.9 | 0.56 | 20.9 | 0.19 | 2.93x |
| 720p -> 4K Grayscale8 | 31.6 | 94.8 | 0.33 | - | - | - |
| 1080p -> 1440p | 47.3 | 37.3 | 1.27 | 15.8 | 0.42 | 2.99x |
| 1080p -> 240p | 23.0 | 4.67 | 4.92 | 7.7 | 1.65 | 2.98x |
| 4K -> 64x64 | 70.4 | 19.5 | 3.60 | 27.7 | 1.42 | 2.54x |
| 101 MP -> 720p | 913 | 213.9 | 4.27 | 289 | 1.35 | 3.16x |
| 1080p, native size | 5.23 | 5.26 | 0.99 | - | - | - |

- Opaque 4-byte downscales: 3.6-4.9x behind Qt single-threaded, 1.0-1.65x with four threads. The PC is about 2x behind
  single-threaded, the Pi 3-5x.
- Upscales, Grayscale8, and RGB24 and RGBA32 with threads beat Qt, as on the PC.
- 4K -> 1080p costs 59.5 ms for Grayscale8 and 115 ms for RGB32: a quarter of the floats for half the time.
- Against the Pi 4's figures in the main doc, measured at older commits: 1.6-1.8x faster (24 MP 458 -> 261,
  4K -> 1080p 196 -> 115, 101 MP 1683 -> 913).

### `/QIntel-jcc-erratum` on the SSE4.1 level

| Scenario | Without | With | |
|---|---:|---:|---:|
| 24 MP -> 1080p | 260.5 | 269.0 | +3.2% |
| 4K -> 1080p RGB32 | 115.2 | 121.3 | +5.2% |
| 4K -> 1080p RGBA32 | 137.2 | 138.7 | +1.1% |
| 4K -> 1080p RGB24 | 115.8 | 117.6 | +1.6% |
| 4K -> 1080p Grayscale8 | 59.5 | 60.9 | +2.3% |
| 720p -> 4K RGBA32 | 82.1 | 84.6 | +3.0% |
| 720p -> 4K RGB32 | 66.3 | 67.5 | +1.8% |
| 720p -> 4K RGB24 | 61.4 | 62.3 | +1.5% |
| 720p -> 4K Grayscale8 | 31.6 | 31.3 | -1.2% |
| 1080p -> 1440p | 47.3 | 50.0 | +5.6% |
| 1080p -> 240p | 23.0 | 23.7 | +3.3% |
| 4K -> 64x64 | 70.4 | 71.1 | +1.1% |
| 101 MP -> 720p | 913 | 940 | +2.9% |

The switch gains nothing here and costs up to 5.6%. The QImage controls of the two binaries agree within 1.2%.

### Earlier revisions

| Scenario | f04db84 | 03113a6 | 15f240e | b8892fc |
|---|---:|---:|---:|---:|
| 24 MP -> 1080p | 277.8 | 276.1 | 258.5 | 260.5 |
| 4K -> 1080p RGB32 | 126.9 | 126.9 | 115.7 | 115.2 |
| 4K -> 1080p RGBA32 | 147.3 | 148.0 | 136.8 | 137.2 |
| 4K -> 1080p RGB24 | 127.9 | 126.8 | 115.9 | 115.8 |
| 4K -> 1080p Grayscale8 | 61.5 | 59.9 | 61.8 | 59.5 |
| 720p -> 4K RGBA32 | 83.3 | 82.4 | 82.3 | 82.1 |
| 720p -> 4K RGB32 | 65.4 | 64.6 | 64.4 | 66.3 |
| 720p -> 4K RGB24 | 61.2 | 61.9 | 61.0 | 61.4 |
| 720p -> 4K Grayscale8 | 38.8 | 38.7 | 39.3 | 31.6 |
| 1080p -> 1440p | 47.8 | 47.7 | 47.1 | 47.3 |
| 1080p -> 240p | 23.9 | 24.3 | 22.7 | 23.0 |
| 4K -> 64x64 | 71.7 | 70.9 | 70.3 | 70.4 |
| 101 MP -> 720p | 945.9 | 946.0 | 912.2 | 913.0 |
| 24 MP -> 1080p, threads | 78.3 | 79.4 | 74.9 | 74.3 |
| 4K -> 1080p RGBA32, threads | 43.4 | 43.0 | 40.4 | 40.8 |
| 4K -> 1080p RGB24, threads | 37.4 | 37.3 | 34.9 | 34.6 |

- f04db84 -> 03113a6 (run lookup by value, `runFor` force-inlined): within 1%. The 4-6% this recovered on the PC's
  SSE4.1 upscales does not appear here.
- 03113a6 -> 15f240e (call-site forced inlining; 14e731e's switch on this level, in between, was not built): 4-byte and
  RGB24 downscales -7% to -9%, 24 MP -6%, 101 MP -3.6%. Upscales within 1%.
- 15f240e -> b8892fc (the one-channel short-run pass): 720p -> 4K Grayscale8 -20%, 4K -> 1080p Grayscale8 -4%.
  720p -> 4K RGB32 +2.8%, which that change does not touch: unexplained.

### Where the time goes

Subtractive builds of b8892fc, minimum ms of three alternating rounds. Each removes one part; the split is by differences,
so it is approximate.
- Taps: the horizontal pass's arithmetic, from the block to the store.
- Conversion: `prepareRun`, with the byte-to-float conversion and the buffer's slides.
- Vertical: `filterVerticalDestRow`.
- Rest: the column loop's skeleton, the weights, the destination's allocation and page faults.

| Scenario | Total | Taps | Conversion | Vertical | Rest |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 260.2 | 161.8 | 50.5 | 35.7 | 12.2 |
| 4K -> 1080p RGB32 | 114.8 | 60.7 | 20.3 | 25.4 | 8.5 |
| 4K -> 1080p RGBA32 | 136.0 | 57.9 | 35.3 | 35.1 | 7.7 |
| 4K -> 1080p Grayscale8 | 59.7 | 32.3 | 11.9 | 8.2 | 7.3 |
| 1080p -> 240p | 23.2 | 13.9 | 4.7 | 3.0 | 1.6 |
| 4K -> 64x64 | 70.7 | 49.7 | 17.4 | 1.9 | 1.8 |
| 101 MP -> 720p | 912.7 | 625.0 | 200.5 | 62.6 | 24.6 |
| 720p -> 4K RGB32 | 66.4 | 13.2 | 5.9 | 44.4 | 2.9 |
| 720p -> 4K RGBA32 | 83.1 | 11.4 | 9.2 | 60.2 | 2.3 |
| 720p -> 4K RGB24 | 61.7 | 13.0 | 5.2 | 38.8 | 4.7 |
| 1080p -> 1440p | 46.9 | 13.6 | 6.6 | 21.5 | 5.2 |

- Downscales: the taps are 53-70%, conversion 18-26%.
- Upscales: the vertical pass is 46-72%.
- Grayscale8's taps cost half of RGB32's for a quarter of the floats.

### Horizontal pass variants

Experiments on b8892fc, not committed. Minimum ms of three alternating rounds; the unpatched build of the same worktree
matched b8892fc within 2% on the single-threaded rows.
- Two chains: a block of 16 source floats through two chains per row, then one vector of 8.
- Assigned first block: the first block's products initialize the accumulators, with no add to zero.
- Single rows: the paired-row sweep replaced by one row per sweep.

| Scenario | b8892fc | Two chains | + assigned first block | Single rows | Single rows, two chains, assigned |
|---|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 260.2 | 246.5 | 230.6 | 312.8 | 296.3 |
| 4K -> 1080p RGB32 | 114.8 | 109.7 | 102.5 | 136.4 | 130.1 |
| 4K -> 1080p RGB24 | 115.6 | 109.4 | 101.9 | 136.8 | 128.4 |
| 4K -> 1080p RGBA32 | 136.0 | 129.7 | 132.5 | 156.6 | 150.2 |
| 4K -> 1080p Grayscale8 | 59.7 | 59.2 | 58.9 | 84.0 | 83.6 |
| 1080p -> 240p | 23.2 | 21.5 | 20.3 | 26.4 | 26.0 |
| 4K -> 64x64 | 70.7 | 70.5 | 69.3 | 77.2 | 83.1 |
| 101 MP -> 720p | 912.7 | 893.5 | 848.3 | 1092.5 | 1093.4 |
| 720p -> 4K RGB32 | 66.4 | 66.6 | 65.2 | 76.7 | 76.8 |
| 720p -> 4K RGBA32 | 83.1 | 83.9 | 82.9 | 92.1 | 91.9 |
| 720p -> 4K RGB24 | 61.7 | 63.7 | 60.2 | 72.7 | 72.4 |
| 720p -> 4K Grayscale8 | 31.6 | 31.2 | 31.8 | 40.2 | 40.3 |
| 1080p -> 1440p | 46.9 | 50.7 | 46.8 | 58.9 | 57.9 |
| 24 MP -> 1080p, threads | 74.5 | 73.3 | 68.1 | 90.5 | 85.0 |
| 4K -> 1080p RGB24, threads | 34.7 | 33.6 | 30.8 | 40.9 | 38.3 |

- Two chains with the assigned first block: opaque 4-byte and RGB24 downscales -7% to -12%, 4K -> 64x64 aside (-2%).
  Upscales within 2.5%. The tests pass.
- 4K -> 1080p RGBA32 gains 2.5% with both and 4.6% with two chains alone. Not explained.
- Two chains alone cost 1080p -> 1440p 8%: a 4-tap run then pays a block's zeroing and reduction.
- Single rows lose 9-41% everywhere: paired rows share the column's lookup, weights and loop.

The tree's implementation after b8892fc: two chains at this level, the assigned first block at every level. Minimum ms
of two alternating rounds against b8892fc and the experiment's binary:

| Scenario | b8892fc | Experiment | Implementation |
|---|---:|---:|---:|
| 24 MP -> 1080p | 260.4 | 231.0 | 231.0 |
| 4K -> 1080p RGB32 | 115.6 | 103.1 | 102.1 |
| 4K -> 1080p RGB24 | 117.3 | 101.5 | 101.3 |
| 4K -> 1080p RGBA32 | 137.4 | 132.7 | 134.7 |
| 1080p -> 240p | 22.9 | 20.7 | 20.7 |
| 4K -> 64x64 | 70.8 | 69.4 | 69.1 |
| 101 MP -> 720p | 916.5 | 850.6 | 851.0 |
| 720p -> 4K RGB32 | 66.5 | 64.9 | 64.7 |
| 1080p -> 1440p | 48.1 | 46.1 | 46.7 |
| 24 MP -> 1080p, threads | 76.5 | 68.3 | 69.0 |

- A first version held the chains in an array passed to the block's helper. MSVC kept them in registers but moved one
  row's four through a second register every block: 101 MP -> 720p 907.4 ms, 4K -> 64x64 73.6, 4K -> 1080p RGB32 105.8.
- Adding the product to the accumulator operand, in place of the reverse, did not remove those moves.
- With the chains as separate variables the block loop matches the experiment's.
- A later rewrite of the pass with every step in a loop over the rows indexed the chains by row, as an array of the
  per-row struct. The SSE4.1 3- and 4-channel kernels gained 12 register moves each: 101 MP -> 720p 852.8 -> 907.9 ms,
  4K -> 64x64 62.6 -> 67.9, 4K -> 1080p RGB24 94.2 -> 98.4. The AVX2 kernels did not change.
- With the block's chains kept as two named variables and the other steps looped over the rows: within 1.5% of the
  code before it on every single-threaded row, the 3- and 4-channel kernels within 2 instructions.

### Ring budget sweep (84bbe28)

The detected budget, 512 KB, never splits these scenarios into strips: the ring is 276 KB for 4K -> 1080p RGB32 and lives
in L2. An override of the budget, minimum ms of two alternating rounds:

| Scenario | 512 KB | 128 KB | 64 KB | 32 KB | 16 KB | 8 KB |
|---|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 234.4 | 236.2 | 240.1 | 252.5 | 269.1 | 292.5 |
| 4K -> 1080p RGB32 | 103.4 | 103.1 | 107.5 | 108.9 | 114.7 | 119.4 |
| 4K -> 1080p RGBA32 | 133.9 | 134.9 | 134.4 | 138.6 | 144.5 | 147.7 |
| 4K -> 64x64 | 69.4 | 71.1 | 72.2 | 73.5 | 79.7 | 80.3 |
| 101 MP -> 720p | 855.5 | 867.7 | 896.7 | 933.2 | 1016.3 | 1072.9 |
| 720p -> 4K RGBA32 | 83.5 | 83.4 | 79.8 | 78.0 | 79.6 | 82.7 |
| 720p -> 4K RGB32 | 66.9 | 65.3 | 65.7 | 65.0 | 64.3 | 66.2 |
| 720p -> 4K RGB24 | 60.6 | 59.3 | 59.2 | 60.0 | 58.2 | 60.7 |
| 1080p -> 1440p | 48.7 | 48.4 | 47.1 | 48.2 | 48.5 | 49.8 |
| 24 MP -> 1080p, threads | 69.4 | 69.7 | 69.7 | 73.0 | 75.0 | 83.6 |
| 720p -> 4K RGBA32, threads | 29.0 | 28.9 | 27.5 | 27.7 | 27.9 | 27.8 |

- Downscales lose steadily below 128 KB: up to 25% at 8 KB.
- Upscales gain 3-7% with a ring of 16-64 KB, RGBA32 most. The vertical pass reading its rows from L2 is a small part of
  its cost.

### What bounds the horizontal taps (84bbe28)

- The clock under a single-threaded benchmark: 2.33 GHz, 212% of nominal on every core.
- The 12-tap path of the RGB32 paired-row pass is about 148 instructions per column pair, 113 of them the taps: a first
  block of 21, two more of 37, the reductions and the store.
- The taps take 23 ns per column pair: 54 cycles, 2.1 instructions per cycle. A column pair has 27 loads and 24
  multiplies: no single port accounts for 54 cycles.
- With every column filtering through one column's weights, so that the weights stay in L1, minimum ms of two rounds:

| Scenario | 84bbe28 | Weights in L1 |
|---|---:|---:|
| 24 MP -> 1080p | 234.4 | 214.4 |
| 4K -> 1080p RGB32 | 103.4 | 95.1 |
| 4K -> 1080p RGBA32 | 133.9 | 130.5 |
| 4K -> 1080p Grayscale8 | 60.4 | 53.6 |
| 1080p -> 240p | 20.9 | 19.5 |
| 4K -> 64x64 | 69.4 | 65.1 |
| 101 MP -> 720p | 855.5 | 784.0 |
| 1080p -> 1440p | 48.7 | 44.8 |
| 720p -> 4K RGB32 | 66.9 | 64.4 |

- The weights of 1920 columns of 12 taps are 92 KB, swept once per row pair out of L2: that costs downscales 6-11%, RGBA32 3%.
- With the weights in L1 the taps run at about 2.5 instructions per cycle on a core that decodes 3. What remains is
  the instruction count.

### Runs a period apart share their weights (after 84bbe28)

Minimum ms of two alternating rounds: 84bbe28, the weights-in-L1 experiment above, and the builder sharing weights.

| Scenario | 84bbe28 | Weights in L1 | Shared weights |
|---|---:|---:|---:|
| 24 MP -> 1080p | 231.3 | 212.9 | 215.0 |
| 4K -> 1080p RGB32 | 102.3 | 95.1 | 94.5 |
| 4K -> 1080p RGB24 | 101.1 | 94.6 | 93.7 |
| 4K -> 1080p RGBA32 | 132.1 | 123.3 | 124.8 |
| 4K -> 1080p Grayscale8 | 59.2 | 53.7 | 52.6 |
| 1080p -> 240p | 20.6 | 19.4 | 19.2 |
| 4K -> 64x64 | 68.9 | 64.9 | 61.8 |
| 101 MP -> 720p | 852.1 | 783.4 | 847.1 |
| 720p -> 4K RGB32 | 65.2 | 64.5 | 63.4 |
| 720p -> 4K RGBA32 | 83.5 | 81.9 | 82.7 |
| 720p -> 4K Grayscale8 | 31.5 | 31.4 | 30.5 |
| 1080p -> 1440p | 46.1 | 45.5 | 45.8 |
| 24 MP -> 1080p, threads | 68.0 | 63.0 | 59.9 |
| 4K -> 1080p RGBA32, threads | 40.3 | 37.1 | 36.4 |
| 4K -> 1080p RGB24, threads | 30.8 | 29.2 | 27.3 |
| 1080p -> 240p, threads | 7.02 | 6.61 | 5.71 |
| 4K -> 64x64, threads | 27.3 | 25.1 | 22.4 |
| 101 MP -> 720p, threads | 284.2 | 249.7 | 285.1 |

- Sharing reaches the experiment's gain wherever the period is short, and passes it where the weights' construction
  shows: the builder evaluates the kernel for one period only, and that is serial time in a threaded resize.
- 101 MP -> 720p: 11608 -> 1280 has a period of 160 columns of about 55 taps, 35 KB. It does not fit L1 and does not gain.

### The generated code, RGB32 paired-row horizontal pass (b8892fc)

MSVC's code for `filterHorizontalRowGroup` with two rows of 4-float pixels, read from the b8892fc binary:

- A `Floats8` is two XMM registers at this level, so the block's four chains per row are 16 accumulators. Eight of them
  live on the stack: each is loaded, added to and stored back every block.
- Entering the block costs 15 stores of zero to those slots, eight of them to one slot.
- Every first add of a chain adds to zero: 16 of the 28 adds a 12-tap column's block and reduction take.
- A 2x Lanczos3 downscale has 12 taps: the block loop runs once per column, then the half block. The four chains never
  overlap in time, and their setup and reduction are paid per column.
- The column loop reloads seven of its own pointers and counters from the stack per column.
- Estimated from the listing: about 170 micro-ops per column pair, of which 72 are the taps' loads, multiplies and adds.
