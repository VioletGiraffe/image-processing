# x64 CI runners (ubuntu-latest, windows-latest)

The measurement log for these runners. Sections carry the headings of [resize-performance.md](../resize-performance.md),
which holds each one's rationale.

## Machines

| | |
|---|---|
| CPU | 2 cores, 4 threads, varying by run. Seen: AMD EPYC 9V45 (Zen 5), 9V74 (Zen 4), 7763 (Zen 3); Xeon Platinum 8573C, 8370C, Xeon 6973P-C |
| Caches | Private L2 per core: 1 MB on Zen 4 and 5, 512 KB on Zen 3, 2 MB on the Xeons (1.25 MB on the 8370C) |
| Toolchain | GCC and Clang on ubuntu-latest; MSVC and clang-cl on windows-latest |

- Run-to-run noise on windows-latest: most rows within 3%, some 10%.
- A row names its CPU where the comparison depended on it. The EPYC 7763 is the model most runs landed on.

## Per-instruction-set primitives, not SIMDe (963b54b)

The AVX2 instructions are unchanged under clang-cl and MSVC. MSVC's code layout moved, and its RGBA32 downscales on the
EPYC 7763 are 6-9% slower.

## Source rows converted to float once (d436d41)

Resizer / QImage single-threaded, f435b80 -> d436d41:

| Job | 24 MP -> 1080p | 4K -> 1080p | 4K -> 64x64 | 101 MP -> 720p |
|---|---|---|---|---|
| x64 GCC | 2.71 -> 2.77 | 3.10 -> 3.00 | 3.55 -> 4.04 | 3.79 -> 3.96 |
| MSVC | 2.27 -> 2.44 | 2.09 -> 2.18 | 1.84 -> 2.66 | 2.49 -> 2.54 |

## The run lookup holds its array pointers by value (f2f4569, 03113a6)

GCC on the EPYC 7763, AVX2 ms, three runs per commit:

| Scenario | 8baefb9 | f04db84 | f2f4569 |
|---|---:|---:|---:|
| 720p -> 4K RGB32 | 12.77 | 13.21 | 12.56 |
| 1080p -> 1440p | 10.36 | 10.88 | 10.09 |
| 4K -> 1080p RGB32 | 27.22 | 27.85 | 25.82 |
| 4K -> 1080p RGBA32 | 26.79 | 27.31 | 25.84 |
| 720p -> 4K RGBA32 | 12.95 | 13.16 | 13.08 |
| 4K -> 1080p Grayscale8 | - | 12.91 | 11.89 |
| 720p -> 4K RGB24 | - | 12.90 | 11.86 |

- The 3-channel AVX2 kernel at e5ea70d ran 4-5% slower than at 8baefb9, with the same instructions in every hot loop.
- GCC: the SSE4.1 kernels gain 2-8% as well. 4K -> 64x64 does not gain. 03113a6 is within 2% of f2f4569.
- Clang and clang-cl: within 2%.
- MSVC, EPYC 7763: `runFor` and the `std::span` constructor called per output pixel cost 4-18% on the SSE4.1 upscales.
- `flatten` under GCC on the EPYC 7763, six runs against four: -0.9% on average. AVX2 720p -> 4K Grayscale8 7.42 -> 7.75 ms,
  its SSE4.1 run 10.42 -> 9.80.
- Call-site forcing under MSVC on the EPYC 7763, 03113a6 against 15f240e and 9811e5b, three runs each, by minimums: AVX2
  -3.3%, SSE4.1 -2.8% on average.

## Jumps kept off 32-byte boundaries under MSVC (14e731e)

- Placement shows at most a few percent on the upscale rows, and not consistently: CI cannot check it.
- EPYC 7763 (same toolset as the PC), 03113a6 -> 14e731e, three runs each: AVX2 rows within 1%. SSE4.1 RGB32 upscales
  slower in every run: 720p -> 4K 22.8-23.4 -> 26.6-28.1 ms, 1080p -> 1440p 16.2-17.5 -> 19.0-19.3 ms. The other SSE4.1
  upscales: within 4%.
- With the switch on the AVX2 source alone (15f240e, 9811e5b; three runs): 23.1-23.9 and 16.8-19.0.

## One-channel upscales filter four columns at a time (0bd90e4)

720p -> 4K Grayscale8 at 0bd90e4 against the three commits before it, EPYC 7763:

| Compiler | Default level | SSE4.1 |
|---|---:|---:|
| MSVC | -29% | -30% |
| Clang | -28% | -27% |
| GCC | -8% | -10% |

## Column strips (b9fea7e)

Without the pre-touch. EPYC 7763, resizer / QImage, 2b371a2 -> 29b97e9, 2-4 samples per side:
- MSVC and clang-cl: SIMD 720p -> 4K +53-62%, 4K -> 1080p RGB32 +9-14%; scalar upscales +4-20%.
- With threads: MSVC -8% to +7%, clang-cl gains 3-11%.
- Clang on Linux, same CPU model: within 4%.

With the pre-touch. EPYC 7763, single-threaded against 2b371a2, 6b6bf7e:
- MSVC and clang-cl upscales -3% to +3% (strips alone: +53-62%), 4K -> 1080p +1-7%.
- The Linux jobs held within 5% of strips alone.

## CI, 0bd90e4 to 8c9e194 (2026-10-04, 2026-10-05)

EPYC 7763 throughout. Median ms per commit, with the sample count in the header. "-": no run of that commit landed on this CPU.
- Baseline: 0bd90e4, b8892fc and cc975cf, the same code.
- 84bbe28: two chains at SSE4.1, and the first block assigned at every level.
- 16cdfbd: runs a period apart share their weights.
- 1eb77df: the 8-float step for every pixel layout.
- 93775b0: the pass's steps looped over the rows.
- bdf65f9: `storeTempPixelWithTaps` called per row.
- 8c9e194: `[[likely]]` on the leftover-tap steps.

GCC 14.2, AVX2:

| Scenario | Baseline (3) | 84bbe28 (1) | 16cdfbd (0) | 1eb77df (1) | 93775b0 (2) | bdf65f9 (1) | 8c9e194 (2) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 58.22 | 56.42 | - | 52.94 | 52.56 | 52.47 | 52.55 |
| 4K -> 1080p RGB32 | 25.51 | 23.71 | - | 22.78 | 22.23 | 22.71 | 23.05 |
| 4K -> 1080p RGBA32 | 25.93 | 23.80 | - | 22.58 | 22.82 | 23.36 | 22.77 |
| 4K -> 1080p RGB24 | 25.30 | 23.59 | - | 22.70 | 22.99 | 23.18 | 22.82 |
| 4K -> 1080p Grayscale8 | 12.25 | 11.90 | - | 10.88 | 15.65 | 13.74 | 10.77 |
| 1080p -> 240p | 4.85 | 4.65 | - | 4.09 | 4.09 | 4.11 | 4.08 |
| 4K -> 64x64 | 14.54 | 14.39 | - | 13.78 | 13.57 | 13.67 | 13.34 |
| 101 MP -> 720p | 194.7 | 193.6 | - | 188.4 | 187.9 | 188.1 | 187.8 |
| 720p -> 4K RGB32 | 12.52 | 12.22 | - | 12.51 | 12.73 | 12.67 | 12.82 |
| 720p -> 4K RGBA32 | 12.70 | 12.93 | - | 12.68 | 12.75 | 12.83 | 12.68 |
| 720p -> 4K RGB24 | 11.90 | 11.53 | - | 11.77 | 11.82 | 11.83 | 12.26 |
| 720p -> 4K Grayscale8 | 7.11 | 7.44 | - | 7.34 | 6.88 | 7.21 | 6.87 |
| 1080p -> 1440p | 10.06 | 9.76 | - | 9.77 | 9.99 | 10.05 | 10.17 |
| 24 MP -> 1080p, threads | 26.06 | 25.06 | - | 22.74 | 22.81 | 22.73 | 22.55 |
| 4K -> 1080p RGBA32, threads | 12.94 | 12.19 | - | 11.43 | 11.59 | 11.48 | 11.10 |
| 4K -> 1080p RGB24, threads | 11.91 | 10.81 | - | 9.95 | 9.93 | 10.04 | 10.47 |
| 1080p -> 240p, threads | 2.56 | 2.55 | - | 2.10 | 2.58 | 2.09 | 2.11 |
| 4K -> 64x64, threads | 8.62 | 8.67 | - | 7.87 | 7.84 | 7.82 | 7.75 |

GCC 14.2, SSE4.1:

| Scenario | Baseline (3) | 84bbe28 (1) | 16cdfbd (0) | 1eb77df (1) | 93775b0 (2) | bdf65f9 (1) | 8c9e194 (2) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 86.23 | 77.49 | - | 75.51 | 75.30 | 75.07 | 74.38 |
| 4K -> 1080p RGB32 | 35.86 | 31.49 | - | 30.60 | 29.89 | 30.43 | 29.99 |
| 4K -> 1080p RGBA32 | 40.79 | 35.84 | - | 35.09 | 34.16 | 34.12 | 33.42 |
| 4K -> 1080p RGB24 | 36.32 | 31.00 | - | 30.26 | 29.98 | 30.78 | 30.09 |
| 4K -> 1080p Grayscale8 | 15.97 | 15.14 | - | 14.35 | 21.50 | 14.46 | 14.02 |
| 1080p -> 240p | 7.27 | 6.47 | - | 6.12 | 6.07 | 6.14 | 6.14 |
| 4K -> 64x64 | 20.55 | 19.50 | - | 18.50 | 17.93 | 17.89 | 17.73 |
| 101 MP -> 720p | 282.9 | 264.4 | - | 262.2 | 253.4 | 252.3 | 251.8 |
| 720p -> 4K RGB32 | 17.06 | 17.41 | - | 17.46 | 17.44 | 16.68 | 16.57 |
| 720p -> 4K RGBA32 | 20.91 | 21.08 | - | 20.91 | 20.82 | 20.70 | 20.85 |
| 720p -> 4K RGB24 | 16.16 | 16.35 | - | 16.32 | 15.93 | 15.90 | 16.05 |
| 720p -> 4K Grayscale8 | 8.79 | 8.46 | - | 8.35 | 8.46 | 8.48 | 8.34 |
| 1080p -> 1440p | 13.25 | 13.68 | - | 13.65 | 12.93 | 13.04 | 12.53 |
| 24 MP -> 1080p, threads | 40.84 | 36.57 | - | 35.17 | 34.78 | 34.85 | 34.37 |
| 4K -> 1080p RGBA32, threads | 20.84 | 18.25 | - | 17.41 | 17.30 | 17.31 | 17.44 |
| 4K -> 1080p RGB24, threads | 18.41 | 15.86 | - | 14.71 | 14.58 | 14.77 | 14.92 |
| 1080p -> 240p, threads | 3.87 | 3.54 | - | 3.19 | 3.10 | 3.10 | 3.06 |
| 4K -> 64x64, threads | 12.67 | 12.85 | - | 12.00 | 11.42 | 11.50 | 11.30 |

Clang 18.1.3, AVX2:

| Scenario | Baseline (2) | 84bbe28 (2) | 16cdfbd (2) | 1eb77df (2) | 93775b0 (2) | bdf65f9 (2) | 8c9e194 (0) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 58.19 | 57.08 | 54.54 | 51.35 | 51.30 | 51.26 | - |
| 4K -> 1080p RGB32 | 24.55 | 23.27 | 21.55 | 21.88 | 22.58 | 22.74 | - |
| 4K -> 1080p RGBA32 | 26.67 | 25.69 | 24.07 | 24.02 | 24.23 | 24.43 | - |
| 4K -> 1080p RGB24 | 25.01 | 23.68 | 22.31 | 22.26 | 22.86 | 23.06 | - |
| 4K -> 1080p Grayscale8 | 13.41 | 13.71 | 12.53 | 12.54 | 12.59 | 13.12 | - |
| 1080p -> 240p | 4.79 | 4.66 | 4.20 | 4.05 | 4.08 | 4.06 | - |
| 4K -> 64x64 | 13.53 | 13.48 | 12.39 | 12.40 | 12.34 | 12.39 | - |
| 101 MP -> 720p | 191.2 | 190.0 | 188.8 | 178.8 | 179.4 | 179.3 | - |
| 720p -> 4K RGB32 | 12.48 | 12.52 | 12.15 | 12.08 | 12.70 | 12.36 | - |
| 720p -> 4K RGBA32 | 13.77 | 13.84 | 12.98 | 12.90 | 13.03 | 13.28 | - |
| 720p -> 4K RGB24 | 12.10 | 12.27 | 11.52 | 11.68 | 12.36 | 11.62 | - |
| 720p -> 4K Grayscale8 | 6.36 | 6.45 | 6.13 | 6.18 | 6.12 | 6.29 | - |
| 1080p -> 1440p | 9.89 | 9.90 | 9.74 | 9.70 | 10.22 | 9.77 | - |
| 24 MP -> 1080p, threads | 26.87 | 26.56 | 24.62 | 23.77 | 23.92 | 23.51 | - |
| 4K -> 1080p RGBA32, threads | 13.02 | 13.07 | 11.47 | 11.76 | 11.65 | 11.63 | - |
| 4K -> 1080p RGB24, threads | 11.94 | 11.44 | 10.10 | 10.16 | 10.53 | 10.12 | - |
| 1080p -> 240p, threads | 2.67 | 2.62 | 2.21 | 2.15 | 2.22 | 2.18 | - |
| 4K -> 64x64, threads | 8.19 | 8.36 | 7.46 | 7.43 | 7.38 | 7.54 | - |

Clang 18.1.3, SSE4.1:

| Scenario | Baseline (2) | 84bbe28 (2) | 16cdfbd (2) | 1eb77df (2) | 93775b0 (2) | bdf65f9 (2) | 8c9e194 (0) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 92.71 | 76.68 | 74.26 | 74.54 | 74.15 | 74.69 | - |
| 4K -> 1080p RGB32 | 38.31 | 32.65 | 31.13 | 31.30 | 31.45 | 31.63 | - |
| 4K -> 1080p RGBA32 | 43.79 | 37.38 | 36.00 | 36.12 | 36.48 | 36.43 | - |
| 4K -> 1080p RGB24 | 39.74 | 33.19 | 31.18 | 31.23 | 31.24 | 31.45 | - |
| 4K -> 1080p Grayscale8 | 16.47 | 16.41 | 15.27 | 15.32 | 15.27 | 15.77 | - |
| 1080p -> 240p | 7.76 | 6.55 | 6.12 | 6.08 | 6.12 | 6.09 | - |
| 4K -> 64x64 | 20.91 | 20.18 | 18.74 | 18.52 | 18.63 | 18.71 | - |
| 101 MP -> 720p | 300.1 | 270.4 | 267.4 | 267.3 | 267.0 | 267.0 | - |
| 720p -> 4K RGB32 | 18.80 | 17.86 | 17.29 | 17.26 | 17.26 | 17.41 | - |
| 720p -> 4K RGBA32 | 22.71 | 21.56 | 21.89 | 21.91 | 22.00 | 22.06 | - |
| 720p -> 4K RGB24 | 18.00 | 17.70 | 16.64 | 16.66 | 16.59 | 16.62 | - |
| 720p -> 4K Grayscale8 | 8.09 | 8.08 | 7.94 | 7.96 | 7.94 | 7.98 | - |
| 1080p -> 1440p | 14.56 | 13.49 | 13.16 | 13.19 | 13.18 | 13.25 | - |
| 24 MP -> 1080p, threads | 43.83 | 38.06 | 36.02 | 37.42 | 36.66 | 37.37 | - |
| 4K -> 1080p RGBA32, threads | 22.02 | 19.14 | 18.41 | 17.92 | 17.91 | 17.87 | - |
| 4K -> 1080p RGB24, threads | 19.54 | 17.11 | 15.43 | 15.39 | 15.09 | 15.23 | - |
| 1080p -> 240p, threads | 4.13 | 3.67 | 3.33 | 3.20 | 3.21 | 3.21 | - |
| 4K -> 64x64, threads | 13.24 | 13.52 | 12.19 | 11.85 | 12.04 | 12.19 | - |

MSVC 19.51, AVX2. Its one run of bdf65f9 on this CPU is left out: the QImage control for 720p -> 4K RGB32 read 42.3 ms
against 33.5-36.3 in the other runs.

| Scenario | Baseline (2) | 84bbe28 (0) | 16cdfbd (1) | 1eb77df (2) | 93775b0 (2) | bdf65f9 (0) | 8c9e194 (3) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 58.26 | - | 56.89 | 53.34 | 54.18 | - | 54.16 |
| 4K -> 1080p RGB32 | 25.45 | - | 24.98 | 26.04 | 23.37 | - | 25.03 |
| 4K -> 1080p RGBA32 | 28.87 | - | 27.20 | 28.41 | 26.46 | - | 27.60 |
| 4K -> 1080p RGB24 | 25.86 | - | 25.56 | 25.66 | 26.11 | - | 24.96 |
| 4K -> 1080p Grayscale8 | 17.10 | - | 16.34 | 16.70 | 16.75 | - | 16.07 |
| 1080p -> 240p | 4.84 | - | 4.51 | 4.22 | 4.21 | - | 4.24 |
| 4K -> 64x64 | 15.07 | - | 14.71 | 13.83 | 14.36 | - | 13.61 |
| 101 MP -> 720p | 199.1 | - | 185.9 | 186.1 | 184.0 | - | 182.2 |
| 720p -> 4K RGB32 | 17.62 | - | 17.92 | 17.70 | 18.05 | - | 17.63 |
| 720p -> 4K RGBA32 | 18.67 | - | 18.49 | 19.50 | 20.22 | - | 20.04 |
| 720p -> 4K RGB24 | 15.54 | - | 16.04 | 17.04 | 16.46 | - | 16.08 |
| 720p -> 4K Grayscale8 | 8.83 | - | 8.52 | 8.78 | 9.15 | - | 8.48 |
| 1080p -> 1440p | 12.45 | - | 12.38 | 12.86 | 12.74 | - | 12.57 |
| 24 MP -> 1080p, threads | 27.29 | - | 25.77 | 26.15 | 31.20 | - | 26.49 |
| 4K -> 1080p RGBA32, threads | 14.44 | - | 12.68 | 13.29 | 16.10 | - | 14.49 |
| 4K -> 1080p RGB24, threads | 12.58 | - | 11.49 | 11.54 | 14.15 | - | 11.32 |
| 1080p -> 240p, threads | 3.07 | - | 2.22 | 2.70 | 2.26 | - | 2.17 |
| 4K -> 64x64, threads | 8.81 | - | 8.29 | 8.33 | 9.09 | - | 8.52 |

MSVC 19.51, SSE4.1:

| Scenario | Baseline (2) | 84bbe28 (0) | 16cdfbd (1) | 1eb77df (2) | 93775b0 (2) | bdf65f9 (0) | 8c9e194 (3) |
|---|---:|---:|---:|---:|---:|---:|---:|
| 24 MP -> 1080p | 92.42 | - | 79.22 | 77.99 | 77.91 | - | 76.53 |
| 4K -> 1080p RGB32 | 40.67 | - | 32.54 | 33.38 | 32.95 | - | 32.15 |
| 4K -> 1080p RGBA32 | 45.26 | - | 40.91 | 42.31 | 41.15 | - | 41.68 |
| 4K -> 1080p RGB24 | 41.58 | - | 32.84 | 33.61 | 33.29 | - | 32.78 |
| 4K -> 1080p Grayscale8 | 19.40 | - | 19.90 | 18.64 | 18.59 | - | 17.88 |
| 1080p -> 240p | 7.73 | - | 6.72 | 6.31 | 6.09 | - | 6.05 |
| 4K -> 64x64 | 22.00 | - | 19.62 | 19.81 | 19.68 | - | 18.97 |
| 101 MP -> 720p | 310.5 | - | 276.3 | 286.0 | 279.2 | - | 271.3 |
| 720p -> 4K RGB32 | 22.87 | - | 24.00 | 24.08 | 23.50 | - | 22.93 |
| 720p -> 4K RGBA32 | 27.90 | - | 27.10 | 27.88 | 28.01 | - | 27.25 |
| 720p -> 4K RGB24 | 22.35 | - | 23.78 | 26.05 | 25.40 | - | 20.64 |
| 720p -> 4K Grayscale8 | 10.95 | - | 10.55 | 10.45 | 10.25 | - | 9.93 |
| 1080p -> 1440p | 16.61 | - | 16.90 | 17.02 | 15.96 | - | 15.44 |
| 24 MP -> 1080p, threads | 48.66 | - | 39.52 | 40.18 | 46.79 | - | 36.69 |
| 4K -> 1080p RGBA32, threads | 24.30 | - | 21.76 | 21.49 | 25.19 | - | 20.41 |
| 4K -> 1080p RGB24, threads | 21.27 | - | 16.41 | 16.62 | 20.67 | - | 17.78 |
| 1080p -> 240p, threads | 4.09 | - | 3.30 | 4.21 | 3.29 | - | 3.16 |
| 4K -> 64x64, threads | 13.58 | - | 12.47 | 13.77 | 13.42 | - | 11.90 |

- Two chains (84bbe28) at SSE4.1: downscales -10% to -17% under Clang and GCC, not only under MSVC, whose stack spills
  it was aimed at. MSVC on the EPYC 9V45, two runs a side: -12% to -22%.
- The assigned first block (84bbe28) at AVX2: downscales -2% to -8% under Clang and GCC.
- Shared weights (16cdfbd): Clang's AVX2 downscales reach -6% to -13% against the baseline, the threaded ones more.
- The 8-float step (1eb77df): within noise.
- The row loops (93775b0) under GCC: 4K -> 1080p Grayscale8 +43% at AVX2 and +50% at SSE4.1, the rolled loop around
  `storeTempPixelWithTaps`. bdf65f9 restores SSE4.1 and leaves AVX2 at 13.74 ms against 10.88; on the EPYC 9V45, 6.92
  against 5.75.
- `[[likely]]` (8c9e194) restores AVX2: 10.74 and 10.81 ms, and 5.68 on the EPYC 9V45. GCC had placed the three
  leftover-tap steps out of line once their bodies were loops over the rows: the same instructions, with a taken branch
  into each step and a jump back. A Golden Cove VM with the same GCC shows no cost for that placement.
- The 3- and 4-channel kernels under GCC: about 3% more instructions since 93775b0, mostly integer moves. No row shows it.
- MSVC's 720p -> 4K RGB24 at SSE4.1 reads +17% at 1eb77df and -8% at 8c9e194 against the baseline, with that level's
  kernels unchanged by either commit: code placement.
- Clang had no run of 8c9e194 on this CPU.
