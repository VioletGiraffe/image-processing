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

## The interleaved form closer to the kernel

The program's sections T, F, C, P and N, at 4edb9e5. MSVC 19.51 and clang-cl 22.1 on the x64 machines, pinned to one
core. Interleaved: a tap per multiply-add over the row pair's pixels in one buffer, as in the table above.

### A tap count read per column

G/s, MSVC. Kernel's: `filterRun`'s steps of 8, 4, 2 and 1 taps. The interleaved columns take the same steps, with each
weight broadcast from memory, or four weights loaded into both lanes and permuted in-lane.

| Taps | 8500T: kernel's | broadcast | lanes | P-core: kernel's | broadcast | lanes | E-core: kernel's | broadcast | lanes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | 8.1 | 9.2 | 9.2 | 16.5 | 19.4 | 18.7 | 7.5 | 7.9 | 7.7 |
| 5 | 7.7 | 11.5 | 11.4 | 16.5 | 23.3 | 23.1 | 8.0 | 9.3 | 8.9 |
| 8 | 12.6 | 13.6 | 13.1 | 26.3 | 30.9 | 29.4 | 9.0 | 11.2 | 10.7 |
| 12 | 16.1 | 15.0 | 16.6 | 33.0 | 36.9 | 35.6 | 9.8 | 13.5 | 12.7 |
| 16 | 17.8 | 16.6 | 18.9 | 35.2 | 40.2 | 37.9 | 10.0 | 15.2 | 14.2 |
| 24 | 21.8 | 18.1 | 22.3 | 40.7 | 38.9 | 38.0 | 10.6 | 15.7 | 16.0 |
| 48 | 24.4 | 20.6 | 25.1 | 43.8 | 43.7 | 38.6 | 11.0 | 17.2 | 17.0 |
| 12 or 11, unpredictably | 14.5 | 14.6 | 15.7 | 30.0 | 34.2 | 32.8 | 9.7 | 11.0 | 11.9 |

- **The fixed 12 taps overstated the gain:** +16% on the P-core under MSVC with the count fixed, +12% with it read.
- **Neither weight source wins on every core.** Broadcast loses 7-17% to the kernel's form on the 8500T from 12 taps
  up, two loads per tap on a core with two load ports. Lanes are level or ahead there, and lose 7-12% on the P-core
  from 24 taps up.
- **The E-core gains with either,** 30-56% from 12 taps up.
- **A plain loop of 4 chains, broadcast,** in place of the steps: 16.3 and 20.9 on the 8500T at 12 and 48 taps, 35.5
  and 44.1 on the P-core, 13.1 and 17.2 on the E-core.
- **clang-cl** at 12 taps, kernel's against interleaved in steps: 14.9 and 17.0 on the 8500T, 29.6 and 37.5 on the
  P-core, 9.3 and 13.8 on the E-core. It compiles both weight sources to the same code.
- **A tap indexed from the run's start costs MSVC a shift per tap,** a pixel pair being 32 bytes: the loops of 3 and 4
  chains read 26-29 G/s at 12 taps on the P-core, against 35 with pointers stepped per tap.

### The whole pass

12 taps at a step of 2 over a 4K RGB32 image: conversion in chunks of 128 pixels into the sliding span, then the
columns' runs, without the run lookup. ms, MSVC / clang-cl.

| Form | 8500T | P-core | E-core |
|---|---:|---:|---:|
| The kernel's | 17.63 / 18.15 | 8.92 / 8.65 | 26.04 / 25.80 |
| Interleaved, a loop of 4 chains, broadcast | 16.98 / 17.19 | 7.72 / 8.24 | 20.23 / 20.10 |
| Interleaved, in steps, broadcast | 18.74 / 15.24 | 8.07 / 6.88 | 22.22 / 18.44 |
| Interleaved, a loop of 4 chains, lanes | 18.04 / 17.14 | 8.07 / 8.21 | 21.15 / 20.10 |
| Interleaved, in steps, lanes | 17.52 / 15.29 | 8.11 / 6.82 | 20.85 / 18.47 |
| The kernel's, the span 16 bytes off a 32-byte boundary | 18.95 / 18.91 | 9.47 / 8.86 | 26.09 / 25.91 |
| Interleaved, 3 chains, the span on a boundary and 16 bytes off | 17.51, 18.20 / 17.67, 17.80 | 7.86, 8.81 / 9.04, 9.12 | 20.53, 20.68 / 20.89, 20.92 |

- Under MSVC the best interleaved variant takes 4% off the pass on the 8500T, 13% on the P-core, 22% on the E-core.
- The P-core's figures move 5-13% with the placement of code and buffers: the H lines read 13% lower after the other
  sections than alone, and clang-cl's ranking of the loop and the steps reversed between two builds.

### The conversion that interleaves

A 4K RGB32 image in chunks of 128 pixels, ms, MSVC / clang-cl.

| Conversion | 8500T | P-core | E-core |
|---|---:|---:|---:|
| The kernel's: each row to its own floats | 2.99 / 2.33 | 1.25 / 1.03 | 3.54 / 2.75 |
| The rows' bytes unpacked into pixel pairs, then widened | 3.04 / 3.04 | 1.13 / 1.12 | 3.06 / 2.74 |
| Each row widened, lanes permuted into pixel pairs | 3.54 / 2.99 | 1.80 / 1.78 | 4.88 / 3.75 |
| Each row widened, each pixel stored as 16 bytes | 3.29 / 3.06 | 1.46 / 1.78 | 3.82 / 3.75 |

Unpacking the bytes costs MSVC 2% at most. On the 8500T clang-cl's interleaving conversions are 28-31% slower than its
code for the kernel's.

### Four-tap runs

An upscale's arithmetic, 720p -> 4K, ms, MSVC / clang-cl.

| Form | 8500T | P-core | E-core |
|---|---:|---:|---:|
| `filterFourTapRun`'s: weights spread over a pixel pair, halves summed | 3.63 / 3.11 | 2.02 / 1.53 | 4.01 / 4.50 |
| Interleaved, one chain, broadcast | 3.21 / 2.83 | 1.78 / 1.30 | 3.36 / 2.88 |
| Interleaved, two chains, broadcast | 3.24 / 2.83 | 1.72 / 1.31 | 3.55 / 3.07 |
| Interleaved, two chains, lanes | 3.59 / 2.83 | 1.71 / 1.31 | 3.74 / 3.07 |

### The 128-bit levels

At SSE4.1 and NEON the kernel's form is already a tap per multiply-add: a register is one pixel, and the spread gives
each tap's weight to one register. Section N is that form at the level's chain count, a tap count read per column, with
the rows in a buffer each or interleaved, and each weight from its lane or broadcast from memory. G/s at 12 and 48 taps.

| | A buffer per row, lanes: the level's form | A buffer per row, broadcast | Interleaved, lanes | Interleaved, broadcast |
|---|---:|---:|---:|---:|
| 12600K P-core, MSVC | 18.9, 21.6 | 18.6, 19.7 | 18.7, 21.3 | 18.8, 19.6 |
| 12600K P-core, clang-cl | 19.2, 22.0 | 19.0, 21.8 | 19.3, 22.0 | 19.3, 21.9 |
| 12600K E-core, MSVC | 8.9, 10.5 | 8.9, 10.5 | 8.8, 10.4 | 9.0, 10.5 |
| 12600K E-core, clang-cl | 9.2, 11.2 | 9.1, 11.2 | 9.2, 11.2 | 9.2, 11.2 |
| N4100, MSVC | 4.1, 4.8 | 4.0, 4.7 | 4.1, 4.8 | 4.0, 4.8 |
| N4100, clang-cl | 4.1, 4.7 | 3.8, 4.6 | 3.9, 4.7 | 4.0, 4.7 |
| Pi 4, GCC 14.2 | 2.7, 3.9 | 2.7, 3.5 | 2.6, 3.8 | 2.7, 3.5 |
| Pi 4, Clang 22 | 2.7, 3.9 | 2.5, 3.6 | 2.7, 3.8 | 2.6, 3.6 |

- Interleaving the buffer changes nothing at 128 bits: within 3% of the level's form, the N4100 under clang-cl aside (5%).
- Broadcast weights cost the Pi 8-10% at 48 taps and the P-core 9% under MSVC.

## What follows

- More multiply-add throughput, wider vectors or more chains, would gain little on the Skylake family: its FMA units
  are idle three quarters of the time.
- The room is in the work per multiply-add. Trimming the per-column bookkeeping has not gained (experiments 18 to 20, 22 and 23
  of the main doc); the horizontal form itself is the larger term.
- **Untried in the kernel: the interleaved form** for the 4-float layouts' row pairs. The figures above bound its gain
  from above: the real loop keeps its run lookup.
  - One outline could serve every level: the span interleaved everywhere, a row pair as one register at AVX2 and two at
    128 bits, the weight source chosen per level.
  - Every 32-byte load of it is aligned once the span is: a pixel pair is 32 bytes. The kernel's form is 16 bytes off
    at every odd pixel offset.
  - The 1- and 2-float layouts keep the kernel's form: a pixel pair would fill 2 or 4 of a register's 8 floats.

## Limits of these figures

- One scenario: a 2x downscale with 12-tap runs. An upscale's 4-tap runs have their own passes.
- The kernel's rates are MSVC's on the 8500T only. No kernel profile exists for the 12600K.
- The program's vertical variant packs 64 floats to 64 bytes, not RGB32's 3 floats to 4 bytes.
- At SSE4.1 and NEON only the horizontal forms of section N are measured, not the levels' ceilings: their peak is 8
  floats per cycle on a core with two 128-bit units.
- The interleaved form's sections have no run lookup, no straight alpha and no RGB24 source.
