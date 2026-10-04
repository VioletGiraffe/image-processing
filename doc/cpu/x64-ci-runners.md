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
