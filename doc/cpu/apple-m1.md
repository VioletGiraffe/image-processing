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
