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

The program's sections T, F, C, P and N, pinned to one core. MSVC 19.51 and clang-cl 22.1 on the x64 machines; GCC 14.3
on the 8500T, under WSL 2. Interleaved: a tap per multiply-add over the row pair's pixels in one buffer, as in the table
above.

- Sections T and P are built with the kernels' jump-alignment options (`/QIntel-jcc-erratum`,
  `-mbranches-within-32B-boundaries`), GCC's build aside. Without them single lines differ by up to 14% between two
  builds of one source on the 8500T, and by nothing on the 12600K.
- The 8500T ran T and P with the hypervisor on: within 2% of its lines measured without.
- clang-cl on Windows and Clang in WSL agree within 1% on the same source, so GCC's figures compare with the others.

### What the compilers make of the forms

From the listings of all three:

- **The tap itself is the same code everywhere:** one weight instruction and one multiply-add with a memory operand.
- **Clang rewrites three things once it knows the values.** The program hides each behind an empty `asm` statement, as
  the kernels' `spreadIndices` does; `-DNO_CLANG_BARRIERS` builds without.
  - A `vpermps` with known indices becomes `vshufps` + `vpermpd`: the kernel's form is 6% slower in the pass and 15%
    slower at 48 taps on the 8500T.
  - Lane permutes of a loaded weight block become broadcasts from memory: no lane weights at all. 21.7 against 24.4 G/s
    at 48 taps on the 8500T.
  - A pixel pointer and a weight pointer stepped together become one index scaled by 8: every multiply-add of the loop
    is indexed, with 7 bookkeeping instructions an iteration.
- **MSVC keeps a second counter for a bound written `w + n <= end`.** With the loop's end pointer computed before it,
  the bookkeeping is 3 instructions and the interleaved loops gain 9-20% at 12 to 24 taps on the 8500T.
- **GCC derives the end pointer and steps the per-column pointers by itself,** at about 10 instructions per column.

### A tap count read per column

G/s. Kernel's: `filterRun`'s steps of 8, 4, 2 and 1 taps. The interleaved columns take the same steps, with each weight
broadcast from memory, or four weights loaded into both lanes and permuted in-lane.

MSVC:

| Taps | 8500T: kernel's | broadcast | lanes | P-core: kernel's | broadcast | lanes | E-core: kernel's | broadcast | lanes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | 8.8 | 10.0 | 9.7 | 16.5 | 20.5 | 20.6 | 7.9 | 8.4 | 7.2 |
| 5 | 8.9 | 12.4 | 12.2 | 16.9 | 24.8 | 25.1 | 8.4 | 9.7 | 9.3 |
| 8 | 13.3 | 14.6 | 13.8 | 27.9 | 31.0 | 31.0 | 9.2 | 11.5 | 10.9 |
| 12 | 16.8 | 16.5 | 16.9 | 35.5 | 37.8 | 36.2 | 9.8 | 13.8 | 13.0 |
| 16 | 18.2 | 19.4 | 19.3 | 37.7 | 43.6 | 38.6 | 10.0 | 15.7 | 14.7 |
| 24 | 22.1 | 20.2 | 21.8 | 41.2 | 43.2 | 38.8 | 10.6 | 15.8 | 16.1 |
| 48 | 24.2 | 21.1 | 25.3 | 45.3 | 45.5 | 38.6 | 11.0 | 17.2 | 17.0 |
| 12 or 11, unpredictably | 15.4 | 16.3 | 16.3 | 32.6 | 37.3 | 35.2 | 9.8 | 12.9 | 12.3 |

clang-cl, and GCC on the 8500T:

| Taps | 8500T: kernel's | broadcast | lanes | P-core: kernel's | broadcast | lanes | E-core: kernel's | broadcast | lanes | GCC: kernel's | broadcast | lanes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | 8.7 | 10.5 | 10.0 | 17.2 | 21.8 | 20.6 | 7.8 | 8.2 | 5.7 | 6.5 | 10.0 | 9.4 |
| 5 | 8.6 | 12.2 | 11.7 | 16.3 | 25.6 | 24.2 | 7.8 | 9.6 | 7.2 | 7.3 | 12.5 | 10.7 |
| 8 | 13.1 | 15.6 | 14.7 | 27.9 | 32.1 | 30.5 | 9.3 | 11.5 | 10.8 | 11.7 | 14.6 | 13.9 |
| 12 | 16.5 | 17.9 | 17.9 | 36.0 | 40.2 | 37.0 | 9.8 | 14.2 | 13.2 | 15.1 | 17.0 | 17.3 |
| 16 | 17.6 | 21.1 | 20.2 | 36.9 | 42.4 | 42.0 | 10.7 | 15.3 | 14.2 | 17.7 | 20.1 | 19.1 |
| 24 | 22.6 | 21.4 | 23.1 | 41.4 | 43.7 | 47.7 | 11.1 | 17.5 | 16.1 | 22.1 | 20.9 | 22.5 |
| 48 | 25.1 | 22.0 | 24.4 | 46.3 | 47.5 | 48.7 | 11.5 | 18.5 | 17.0 | 26.3 | 22.2 | 24.7 |
| 12 or 11, unpredictably | 15.1 | 17.5 | 17.0 | 31.2 | 37.9 | 35.2 | 9.9 | 13.2 | 12.3 | 14.3 | 16.0 | 16.4 |

- **The E-core gains with either weight source,** 33-61% from 12 taps up. Under clang-cl broadcast is 8-9% ahead of lanes there.
- **On the 8500T broadcast falls behind from 24 taps up** under every compiler, below the kernel's form: two loads per
  tap on a core with two load ports. Lanes stay level with the kernel's form or ahead.
- **On the P-core MSVC's lanes stop at 38.6 G/s from 16 taps up; clang-cl's reach 48.7,** the best of any form there.
- **A plain loop of 4 chains** in place of the steps, broadcast and lanes, at 12 and 48 taps. MSVC: 18.0, 17.6 and 21.9,
  24.8 on the 8500T; 38.3, 37.7 and 45.7, 38.7 on the P-core; 13.6, 13.1 and 17.3, 17.2 on the E-core. clang-cl: 18.8,
  16.8 and 22.3, 24.0; 38.7, 33.1 and 46.0, 48.3; 13.6, 12.1 and 18.5, 17.1.
- **A tap indexed from the run's start costs MSVC a shift per tap,** a pixel pair being 32 bytes: the loops of 3 and 4
  chains read 26-29 G/s at 12 taps on the P-core, against 35 with pointers stepped per tap.

### The whole pass

A 4K RGB32 image: conversion in chunks of 128 pixels into the sliding span, then the columns' runs, without the run
lookup. 2x: 1920 columns of 12 taps. 3x: 1280 of 18. 1.5x: 2560 of 9. 4x: 958 of 24. At 3x every other run starts at
an odd pixel offset, at 1.5x two of every four: the kernel's form is 16 bytes off a 32-byte boundary there, the
interleaved form never. ms.

| Ratio | Form | 8500T: MSVC | clang-cl | GCC | P-core: MSVC | clang-cl | E-core: MSVC | clang-cl |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 2x | The kernel's | 17.02 | 16.59 | 15.81 | 7.81 | 7.79 | 25.05 | 24.87 |
| | A loop of 4 chains, broadcast | 17.02 | 15.15 | 15.58 | 7.67 | 6.89 | 20.01 | 19.03 |
| | In steps, broadcast | 18.94 | 15.22 | 15.95 | 7.91 | 6.56 | 20.16 | 18.69 |
| | A loop of 4 chains, lanes | 16.17 | 14.96 | 16.44 | 7.40 | 6.96 | 20.79 | 19.25 |
| | In steps, lanes | 16.65 | 14.63 | 18.75 | 7.68 | 6.82 | 20.83 | 19.47 |
| 3x | The kernel's | 15.46 | 15.51 | 15.19 | 7.23 | 7.38 | 24.94 | 24.00 |
| | A loop of 4 chains, broadcast | 16.42 | 14.37 | 14.95 | 7.50 | 6.67 | 18.48 | 17.72 |
| | In steps, broadcast | 16.63 | 14.27 | 15.54 | 7.78 | 6.29 | 18.01 | 16.91 |
| | A loop of 4 chains, lanes | 14.87 | 14.04 | 15.49 | 7.07 | 6.62 | 19.26 | 18.30 |
| | In steps, lanes | 14.48 | 13.38 | 15.86 | 7.15 | 6.23 | 18.79 | 18.21 |
| 1.5x | The kernel's | 20.71 | 20.91 | 19.84 | 9.63 | 10.12 | 27.04 | 26.77 |
| | A loop of 4 chains, broadcast | 19.10 | 16.88 | 17.68 | 8.62 | 7.98 | 23.30 | 21.82 |
| | In steps, broadcast | 20.06 | 16.28 | 18.92 | 8.50 | 7.43 | 23.85 | 21.01 |
| | A loop of 4 chains, lanes | 18.83 | 17.29 | 18.49 | 8.66 | 8.07 | 24.08 | 22.02 |
| | In steps, lanes | 18.22 | 16.49 | 20.66 | 8.33 | 7.83 | 23.13 | 21.91 |
| 4x | The kernel's | 13.11 | 13.16 | 13.46 | 6.58 | 6.59 | 23.08 | 21.97 |
| | A loop of 4 chains, broadcast | 14.95 | 13.02 | 13.81 | 6.95 | 6.08 | 16.48 | 15.73 |
| | In steps, broadcast | 15.04 | 13.05 | 13.38 | 6.91 | 5.96 | 16.78 | 15.24 |
| | A loop of 4 chains, lanes | 13.12 | 12.30 | 14.28 | 6.85 | 5.84 | 17.12 | 16.56 |
| | In steps, lanes | 12.79 | 11.88 | 14.12 | 6.74 | 5.54 | 16.59 | 16.25 |

The best interleaved form against the same compiler's kernel's form:

| Ratio | 8500T: MSVC | clang-cl | GCC | P-core: MSVC | clang-cl | E-core: MSVC | clang-cl |
|---|---:|---:|---:|---:|---:|---:|---:|
| 2x | -5% | -12% | -1% | -5% | -16% | -20% | -25% |
| 3x | -6% | -14% | -2% | -2% | -16% | -28% | -30% |
| 1.5x | -12% | -22% | -11% | -13% | -27% | -14% | -22% |
| 4x | -2% | -10% | -1% | +2% | -16% | -29% | -31% |

- **clang-cl's interleaved forms are the fastest code in every row.** Of the kernel's forms GCC's is the fastest on the
  8500T at 2x, 3x and 1.5x.
- **Under clang-cl the steps are the shape, and the weight source divides the cores:**
  - E-core: broadcast at every ratio, lanes 4-8% behind.
  - P-core: broadcast at 2x and 1.5x, level at 3x, lanes 7% ahead at 4x.
  - 8500T: lanes at 2x, 3x and 4x by 4-10%, broadcast 1% ahead at 1.5x.
- **MSVC's interleaved passes are 6-24% behind clang-cl's on the 8500T and the P-core** with the same tap code, and
  section T within 6%. The difference is outside the taps, in code only this program has: the interleaving conversion
  loop, with indexed stores, and the pass function's per-column bookkeeping, about 10 reloads from the stack.
- **The span 16 bytes off a 32-byte boundary,** 2x, MSVC: the kernel's form +6% on the 8500T and +9% on the P-core; the
  interleaved loop of 3 chains +14% and +10%. Nothing on the E-core.
- **The program's overhead per column weighs on the forms unequally.** A multiply by the column index per run, which
  the kernel does not have, cost MSVC's kernel's form 11% on the P-core and its interleaved forms 2%.

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
  - The steps are the shape. No weight source is best on all three cores: broadcast suits the E-core, lanes the 8500T,
    and the P-core changes sides with the run length.
  - The E-core gains under every compiler. On the 8500T and the P-core the gain is clang-cl's; MSVC's is small in this
    program, in code the kernel does not share.
- **Untried in the kernel: stepped pointers in `filterRun`.** It indexes pixels by tap, and 86% of the multiply-adds'
  memory operands in MSVC's listing of the AVX2 kernel are indexed, 81% in Clang's, 30% in GCC's.

## Limits of these figures

- One scenario: a 2x downscale with 12-tap runs. An upscale's 4-tap runs have their own passes.
- The kernel's rates are MSVC's on the 8500T only. No kernel profile exists for the 12600K.
- The program's vertical variant packs 64 floats to 64 bytes, not RGB32's 3 floats to 4 bytes.
- At SSE4.1 and NEON only the horizontal forms of section N are measured, not the levels' ceilings: their peak is 8
  floats per cycle on a core with two 128-bit units.
- The interleaved form's sections have no run lookup, no straight alpha and no RGB24 source.
