# Apple M1

The measurement log for this CPU. Sections carry the headings of [resize-performance.md](../resize-performance.md), which
holds each one's rationale.

## Machines

| | |
|---|---|
| macos-latest runner | Apple M1 (virtual), 3 cores, 12 MB L2, Apple Clang |
| A real M1 | Apple Clang |

Run-to-run noise: up to 2x on the runner, too noisy to read per scenario. About 1% on the real M1 in a same-session A/B.

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
