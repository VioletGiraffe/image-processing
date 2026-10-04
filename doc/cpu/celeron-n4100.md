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

### The generated code, RGB32 paired-row horizontal pass

MSVC's code for `filterHorizontalRowGroup` with two rows of 4-float pixels, read from the b8892fc binary:

- A `Floats8` is two XMM registers at this level, so the block's four chains per row are 16 accumulators. Eight of them
  live on the stack: each is loaded, added to and stored back every block.
- Entering the block costs 15 stores of zero to those slots, eight of them to one slot.
- Every first add of a chain adds to zero: 16 of the 28 adds a 12-tap column's block and reduction take.
- A 2x Lanczos3 downscale has 12 taps: the block loop runs once per column, then the half block. The four chains never
  overlap in time, and their setup and reduction are paid per column.
- The column loop reloads seven of its own pointers and counters from the stack per column.
- Estimated from the listing: about 170 micro-ops per column pair, of which 72 are the taps' loads, multiplies and adds.
