# The kernels against the hardware's multiply-add ceiling

How far the AVX2 kernels run from the CPU's peak multiply-add rate, what the distance consists of, and which
horizontal structure each core prefers. Measured at b1395f9 on 4K -> 1080p RGB32. The design's measurements are in
[resize-performance.md](resize-performance.md).

## Method

- **Peak:** two 256-bit FMA units give 16 float multiply-adds per cycle. 50 G/s on the 8500T at 3.15 GHz, 75 G/s on a
  12600K P-core at 4.69 GHz. The 12600K's E-cores ran at 3.59 GHz; their unit count for 256-bit work was not established.
- **Work in one resize:** 274 M float multiply-adds, 5.4 ms at the 8500T's peak.
  - Horizontal: 2160 source rows x 1920 columns x 12 taps x 4 floats = 199 M.
  - Vertical: 1080 rows x 1920 pixels x 3 floats x 12 taps = 75 M.
- **The kernel's rates:** VTune's time for the per-column loop and for the vertical pass over 600 resizes (8500T, MSVC),
  a few percent high for the profiler's overhead.
- **The ceilings:** `scripts/perf/multiply_add_ceiling.cpp`, the passes' arithmetic alone at the real sizes: the same
  loads, chains, reductions and stores, with a fixed 12 taps, no run lookup, no range checks, no conversion. Best of 30
  repeats; two runs agree within 1-3%.
- Rates are G float multiply-adds per second, MSVC / clang-cl. A column pair is 96 of them.

## The 8500T's 4x

| Part | Time per resize | Rate | Share of peak |
|---|---:|---:|---:|
| Horizontal per-column loop | 14.8 ms | 13.4 G/s | 27% |
| Vertical pass, output packing included | 4.9 ms | 15.3 G/s | 30% |
| Conversion and the rest of the kernel | 4.3 ms | no multiply-adds | |
| Whole resize | about 23 ms | 11.9 G/s | 24% |

| Step | Both passes | Factor |
|---|---:|---:|
| Peak multiply-add rate | 5.4 ms | |
| The passes' arithmetic with no bookkeeping | 13.1 ms | 2.4x |
| The kernel's two passes | 19.7 ms | 1.5x |
| With conversion and the rest | about 23 ms | 1.2x |

- **The arithmetic's own structure costs more than the bookkeeping.** A column pair's 12 multiply-adds come with 8
  additions, the chains' sums and the halves' sums, and on the Skylake family additions run on the two FMA units: 20
  operations where the peak counts 12. The weights' load and spread take another quarter.
- **The kernel runs at about two thirds of that practical ceiling in each pass.** With every bookkeeping instruction gone a resize
  would take about 17 ms.
- **The horizontal loop is at the core's width.** 81 instructions per column pair in about 22.5 cycles, 3.6 per cycle on
  a core that retires 4: the same instructions cannot run meaningfully faster.
- **The vertical pass is not bound by memory.** The ring in L2 costs 15-20% against L1. Packing the sums to bytes costs
  0-3%.

## The horizontal pass's forms, by core

All with the weights loaded per column, except the first line.

| Form | 8500T | 12600K P-core | 12600K E-core |
|---|---:|---:|---:|
| Arithmetic alone, weights in registers | 25.7 / 26.7 | 54.1 / 56.5 | 14.9 / 14.9 |
| The kernel's: four chains, weights spread over a pixel pair, halves summed | 19.9 / 18.7 | 41.5 / 38.0 | 10.0 / 10.4 |
| The kernel's with two chains | 23.0 / 22.4 | 45.7 / 41.5 | 11.1 / 11.1 |
| The kernel's with one chain | 22.6 / 21.6 | 47.9 / 42.3 | 9.3 / 10.9 |
| A tap per multiply-add, the row pair assembled by an insert, weights permuted | 14.8 / 17.0 | 32.1 / 34.2 | 10.7 / 13.5 |
| Same, each weight broadcast from memory | 15.6 / 17.0 | 34.1 / 34.2 | 12.5 / 13.5 |
| A tap per multiply-add, the rows' pixels interleaved in one buffer, weights permuted | 20.6 / 21.9 | 39.0 / 49.1 | 15.1 / 16.7 |
| Same, each weight broadcast from memory | 20.4 / 21.9 | 48.7 / 49.2 | 17.3 / 16.7 |

"A tap per multiply-add": row A's pixel in the low lane and row B's in the high one, each tap's weight in all 8 floats.
No spread across pixels and no sum of halves; three chains, two additions per column pair.

Cycles per column pair; the multiply-adds alone need 6:

| Form | 8500T | P-core | E-core |
|---|---:|---:|---:|
| Arithmetic alone | 11.8 / 11.3 | 8.3 / 8.0 | 23.1 / 23.1 |
| The kernel's | 15.2 / 16.2 | 10.9 / 11.9 | 34.5 / 33.1 |
| Rows interleaved, weights from memory | 14.8 / 13.8 | 9.3 / 9.2 | 19.9 / 20.6 |

- **The interleaved form beats the kernel's on every core:** +3% / +17% on the 8500T, +17% / +29% on the P-core,
  +73% / +61% on the E-core.
- **The kernel's form is specifically costly on Gracemont:** the E-core takes 2.8-3.2x the P-core's cycles for it and
  2.1-2.2x for the interleaved form. It rests on a cross-lane permute per chain and the halves' sum, and Gracemont runs
  256-bit operations as two halves.
- **The pair assembled by an insert loses on both big cores** and gains on the E-core: it needs a load per row and per
  weight for every tap.
- **Fewer chains gain here and not in the kernel:** two chains were level on the 12-tap rows and 5-12% worse on long
  runs there (experiment 19 of the main doc). The additions saved are 4 of the real loop's 81 instructions.
- **The P-core is 1.4x the 8500T per cycle on the arithmetic alone** and reaches 72-75% of its peak against 51-53%. It
  is wider, has a third load port and separate adders; which of those gives the 1.4x is not separated. 2 cycles above
  the multiply-adds' 6 remain.

## The vertical pass, by core

| Ring | 8500T | P-core | E-core |
|---|---:|---:|---:|
| 7 KB, in L1, sums stored as floats | 28.4 / 31.4 | 51.6 / 59.6 | 19.1 / 21.8 |
| 7 KB, packed to bytes | 25.7 / 28.4 | 47.1 / 52.5 | 18.7 / 19.0 |
| 105 KB, in L2 as the kernel's strips, sums stored as floats | 24.0 / 24.8 | 38.9 / 44.6 | 13.9 / 12.2 |
| 105 KB, packed to bytes | 24.0 / 25.8 | 37.7 / 43.3 | 13.8 / 12.2 |
| 315 KB, whole rows | 20.9 / 21.6 | 39.8 / 45.1 | 14.1 / 11.8 |

- Leaving L1 costs 15-20% on the 8500T, 25% on the P-core and 27-44% on the E-core.
- 315 KB is past the 8500T's L2 and inside the 12600K's.
- The kernel's vertical pass on the 8500T, 15.3 G/s, has the tap list's construction, RGB32's byte layout and the strip
  loop on top of this arithmetic.

## What follows

- More multiply-add throughput, wider vectors or more chains, would gain little on the Skylake family: its FMA units
  are idle three quarters of the time.
- The room is in the work per multiply-add. Trimming the per-column bookkeeping has not gained (experiments 18 to 20, 22 and 23
  of the main doc); the horizontal form itself is the larger term.
- **Untried in the kernel: the interleaved form** for the 4-float layouts' row pairs. The conversion would write a row
  pair's pixels alternately into one buffer. The figures above bound its gain from above: a third of the real loop is
  bookkeeping that it keeps, and the conversion gains work.

## Limits of these figures

- One scenario: a 2x downscale with 12-tap runs. An upscale's 4-tap runs have their own passes.
- The kernel's rates are MSVC's on the 8500T only. No kernel profile exists for the 12600K.
- The program's vertical variant packs 64 floats to 64 bytes, not RGB32's 3 floats to 4 bytes.
- SSE4.1 and NEON are not covered: their peak is 8 floats per cycle on a core with two 128-bit units.
