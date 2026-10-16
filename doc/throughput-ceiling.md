# The kernels against the hardware's multiply-add ceiling

How far the AVX2 kernels run from the CPU's peak multiply-add rate, what the distance consists of, and which
horizontal structure each core prefers. Measured at b1395f9 on 4K -> 1080p RGB32, the interleaved form's sections
against the kernel's form of 32b0a2f. The design's measurements are in
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
| A tap per multiply-add, the rows' pixels interleaved in one buffer, weights permuted | 20.6 / 21.9 | 44.8 / 49.1 | 15.1 / 16.7 |
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

The program's sections T, F, C and R, pinned to one core, with the kernel's form as the kernel has it at 32b0a2f: the
block loop and the conversion on stepped pointers, the last tap a step, every run's weights at a multiple of 8 floats,
no margin behind the span. MSVC 19.51 and clang-cl 22.1. Interleaved: a tap per multiply-add over the row pair's pixels
in one buffer, as in the table above.

- Built with the kernels' jump-alignment options (`/QIntel-jcc-erratum`, `-mbranches-within-32B-boundaries`). Without
  them single lines differ by up to 14% between two builds of one source on the 8500T, and by nothing on the 12600K.
- Every figure is the better of two runs, the compilers' builds alternated. A run on the 12600K's P-core now and then
  reads 10-25% slow in most lines; the E-core and the 8500T repeat within 3%.
- The whole pass, section R, and the four-tap runs read their runs from a run table. Section T is arithmetic with
  each column's position and weights computed in registers, and was measured before the pass got the table.
- Section N, the 128-bit levels with the count read, is as measured at a8dd837, before these changes to the kernel's
  form. Section S, the same levels over a run table, is of this source.

### What the compilers make of the forms

From both listings:

- **The tap itself is the same code:** one weight instruction and one multiply-add with a memory operand.
- **A weight picked from its lane must be written as a shuffle.** MSVC emits `vpermilps` for `_mm256_permute_ps` and
  `vshufps` for `_mm256_shuffle_ps` of a register with itself; Clang emits `vshufps` for either. The 12600K's P-core
  runs `vpermilps` at one per cycle: lane weights stopped at 37 G/s from 16 taps up, a tap per cycle, and reach 48
  with `vshufps`. The 8500T and the E-core time the two alike. The figures below are `vshufps`.
- **Clang rewrites three things once it knows the values.** The program hides each behind an empty `asm` statement, as
  the kernels' `spreadIndices` does; `-DNO_CLANG_BARRIERS` builds without.
  - A `vpermps` with known indices becomes `vshufps` + `vpermpd`: the kernel's form is 6% slower in the pass and 15%
    slower at 48 taps on the 8500T.
  - Lane permutes of a loaded weight block become broadcasts from memory: no lane weights at all.
  - A pixel pointer and a weight pointer stepped together become one index scaled by 8: every multiply-add of the loop
    is indexed. The kernel's form's block loop is not hidden, and Clang indexes it: 45 G/s at 48 taps on the P-core
    against MSVC's 55.
- **MSVC keeps a second counter for a bound written `w + n <= end`.** With the loop's end pointer computed before it,
  the bookkeeping is 3 instructions.
- **MSVC does not unroll a loop over a known count; Clang does.** A fixed count must be written out, by template
  recursion over the taps or blocks. Left as a loop, the kernel's form at a fixed 18 taps was 10% slower under MSVC than
  written out; its column at 24 taps was 104 instructions against Clang's 65 (VTune on the 8500T: 11.4 G instructions
  retired in the column loop against 6.7 G, 0.91 s against 0.72 s).
- **At 24 taps MSVC's written-out kernel's form is short of registers:** per column both output pointers go through
  the stack, and one chain is spilled and reloaded. Clang's column has one stack reference, and gains 11-15% where
  MSVC's gains nothing.
- **Broadcast weights pay for every other load in the column; lane weights do not.** With the pass's step, weight
  stride and column count read from the stack per column, as MSVC had them before the run table, fixed broadcast at
  12 taps was 13-15% slower on all three cores than with those loads gone, and matched Clang's without them. Fixed
  lanes and the kernel's form moved 0-4%. VTune on the 8500T: the same 44 instructions per column, 1.22 s against
  Clang's 0.99 s. Which resource the loads exhaust is not established: the P-core has three load ports.

### A tap count read per column

G/s. Kernel's: `filterRun`'s steps of 8, 4, 2 and 1 taps. The interleaved columns take the same steps, with each weight
broadcast from memory, or four weights loaded into both lanes and shuffled in-lane.

MSVC:

| Taps | 8500T: kernel's | broadcast | lanes | P-core: kernel's | broadcast | lanes | E-core: kernel's | broadcast | lanes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | 8.8 | 10.0 | 10.0 | 17.1 | 19.9 | 20.3 | 7.9 | 8.4 | 7.9 |
| 5 | 9.8 | 12.4 | 11.9 | 18.5 | 24.8 | 24.1 | 9.2 | 9.8 | 9.3 |
| 8 | 12.1 | 14.7 | 13.8 | 26.2 | 31.0 | 30.2 | 9.1 | 11.5 | 10.8 |
| 12 | 16.1 | 16.4 | 16.7 | 32.5 | 37.9 | 37.4 | 9.8 | 13.7 | 12.8 |
| 16 | 17.3 | 19.4 | 19.3 | 38.1 | 43.7 | 42.6 | 10.4 | 15.7 | 14.5 |
| 24 | 22.1 | 20.2 | 22.0 | 46.6 | 42.9 | 47.6 | 10.6 | 17.9 | 15.9 |
| 48 | 26.1 | 21.2 | 25.2 | 55.3 | 45.1 | 48.6 | 11.0 | 17.2 | 17.0 |
| 12 or 11, unpredictably | 15.1 | 16.2 | 16.1 | 30.3 | 36.5 | 34.6 | 9.8 | 12.9 | 12.2 |

clang-cl:

| Taps | 8500T: kernel's | broadcast | lanes | P-core: kernel's | broadcast | lanes | E-core: kernel's | broadcast | lanes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 4 | 8.8 | 10.0 | 10.0 | 17.3 | 20.7 | 21.4 | 7.8 | 5.7 | 8.2 |
| 5 | 9.5 | 11.7 | 11.9 | 18.5 | 24.4 | 24.0 | 8.8 | 7.2 | 9.3 |
| 8 | 13.1 | 15.4 | 14.9 | 27.9 | 32.5 | 30.7 | 9.3 | 11.3 | 10.9 |
| 12 | 16.7 | 17.7 | 17.7 | 36.6 | 39.3 | 36.6 | 9.8 | 13.9 | 13.3 |
| 16 | 17.7 | 21.0 | 20.8 | 37.1 | 40.2 | 41.9 | 10.6 | 15.2 | 14.4 |
| 24 | 22.6 | 21.1 | 23.7 | 40.8 | 43.8 | 47.9 | 11.0 | 17.6 | 16.2 |
| 48 | 25.0 | 22.3 | 25.0 | 44.9 | 47.6 | 49.1 | 11.5 | 18.5 | 17.0 |
| 12 or 11, unpredictably | 15.7 | 17.3 | 16.7 | 32.4 | 36.8 | 34.8 | 7.9 | 13.0 | 12.4 |

- **Up to 16 taps the interleaved columns lead on every core,** with either weight source.
- **From 24 taps up the kernel's form leads on the 8500T and, under MSVC, on the P-core:** 55 against 49 G/s at 48 taps
  there. Its stepped block loop is a pixel load and half a permute per tap; a lane weight is a shuffle per tap, a
  broadcast a second load.
- **On the 8500T broadcast falls behind from 24 taps up** under both compilers: two loads per tap on a core with two
  load ports. Lanes stay within 4% of the kernel's form.
- **The E-core gains with either weight source,** 30-70% from 12 taps up; broadcast is up to 13% ahead of lanes.
- **A plain loop of 4 chains** in place of the steps, broadcast and lanes, at 12 and 48 taps. MSVC: 17.8, 17.2 and 22.0,
  24.8 on the 8500T; 37.5, 37.8 and 45.2, 47.8 on the P-core; 13.6, 13.1 and 17.2, 17.1 on the E-core. clang-cl: 18.7,
  16.7 and 22.7, 24.5; 38.3, 32.1 and 46.4, 48.3; 13.6, 12.1 and 18.5, 17.0.
- **A tap indexed from the run's start costs MSVC a shift per tap,** a pixel pair being 32 bytes: the columns step
  pointers per tap.

### The whole pass

A 4K RGB32 image: conversion in chunks of 128 pixels into the sliding span, then the columns' runs. Each column's run
is read from a run table as the kernel's: its first source pixel, its weights' address, its tap count.

- Six run lengths: a 6-lobe-wide filter's at 1.17x, 1.5x, 2x, 3x and 4x, and a 4-lobe-wide one's at 1.25x. 7 taps take
  all of the kernel's form's 4-, 2- and 1-tap steps and no block.
- At 3x every other run starts at an odd pixel offset, at 1.5x two of every four: the kernel's form is 16 bytes off a
  32-byte boundary there, the interleaved form never.
- Fixed: the tap count is a template parameter of the pass, and the column is written out, per block for the kernel's
  form and per tap for the interleaved ones, in 4 chains. The loop of 4 chains and the steps have one fixed form per
  weight source.
- ms, with the change against the same compiler's kernel's form, its count read. Bold: the row's best.

| Ratio | Machine, compiler | Kernel's | Kernel's, fixed | 4 ch., broadcast | Steps, broadcast | Broadcast, fixed | 4 ch., lanes | Steps, lanes | Lanes, fixed |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1.25x, 5 taps | 8500T, MSVC | 16.56 | 13.15 (-21%) | 14.61 (-12%) | 13.24 (-20%) | **12.29 (-26%)** | 14.76 (-11%) | 13.29 (-20%) | 12.75 (-23%) |
| | 8500T, clang-cl | 17.39 | 12.76 (-27%) | 14.65 (-16%) | 14.03 (-19%) | **12.34 (-29%)** | 15.08 (-13%) | 13.90 (-20%) | 12.76 (-27%) |
| | P-core, MSVC | 8.54 | 7.71 (-10%) | 6.84 (-20%) | 6.94 (-19%) | 6.89 (-19%) | 6.82 (-20%) | **6.36 (-26%)** | 6.61 (-23%) |
| | P-core, clang-cl | 9.21 | 7.53 (-18%) | 6.90 (-25%) | 6.74 (-27%) | 6.83 (-26%) | 6.94 (-25%) | **6.54 (-29%)** | 6.61 (-28%) |
| | E-core, MSVC | 18.68 | 17.10 (-8%) | 18.13 (-3%) | 16.35 (-12%) | **13.80 (-26%)** | 18.40 (-1%) | 16.69 (-11%) | 14.69 (-21%) |
| | E-core, clang-cl | 19.38 | 16.90 (-13%) | 17.98 (-7%) | 17.28 (-11%) | **13.56 (-30%)** | 18.61 (-4%) | 17.33 (-11%) | 14.59 (-25%) |
| 1.17x, 7 taps | 8500T, MSVC | 19.50 | 15.99 (-18%) | 18.48 (-5%) | 16.76 (-14%) | **15.11 (-23%)** | 18.33 (-6%) | 16.33 (-16%) | 15.21 (-22%) |
| | 8500T, clang-cl | 20.44 | 15.12 (-26%) | 18.97 (-7%) | 17.00 (-17%) | 15.23 (-25%) | 18.92 (-7%) | 17.19 (-16%) | **14.68 (-28%)** |
| | P-core, MSVC | 10.13 | 7.62 (-25%) | 8.40 (-17%) | 7.72 (-24%) | 8.02 (-21%) | 8.32 (-18%) | **7.24 (-29%)** | 7.33 (-28%) |
| | P-core, clang-cl | 10.55 | 8.03 (-24%) | 8.90 (-16%) | 8.31 (-21%) | 7.87 (-25%) | 8.60 (-18%) | 7.60 (-28%) | **7.30 (-31%)** |
| | E-core, MSVC | 23.14 | 25.56 (+10%) | 23.32 (+1%) | 20.28 (-12%) | **18.41 (-20%)** | 23.45 (+1%) | 21.20 (-8%) | 18.43 (-20%) |
| | E-core, clang-cl | 24.30 | 24.80 (+2%) | 23.25 (-4%) | 21.17 (-13%) | **18.32 (-25%)** | 23.76 (-2%) | 21.68 (-11%) | 18.51 (-24%) |
| 1.5x, 9 taps | 8500T, MSVC | 18.46 | 16.69 (-10%) | 17.09 (-7%) | 17.39 (-6%) | 16.00 (-13%) | 16.74 (-9%) | 16.92 (-8%) | **14.91 (-19%)** |
| | 8500T, clang-cl | 18.16 | 15.79 (-13%) | 17.53 (-3%) | 17.85 (-2%) | 16.03 (-12%) | 17.32 (-5%) | 16.94 (-7%) | **14.74 (-19%)** |
| | P-core, MSVC | 8.64 | 7.41 (-14%) | 7.20 (-17%) | 7.21 (-17%) | 7.41 (-14%) | 7.02 (-19%) | 6.79 (-21%) | **6.53 (-24%)** |
| | P-core, clang-cl | 8.10 | 7.38 (-9%) | 7.38 (-9%) | 7.45 (-8%) | 7.21 (-11%) | 7.24 (-11%) | 7.43 (-8%) | **6.52 (-20%)** |
| | E-core, MSVC | 25.52 | 25.80 (+1%) | 19.78 (-22%) | 19.41 (-24%) | **17.47 (-32%)** | 19.94 (-22%) | 19.82 (-22%) | 18.05 (-29%) |
| | E-core, clang-cl | 24.79 | 24.66 (-1%) | 20.24 (-18%) | 20.20 (-19%) | **17.38 (-30%)** | 20.69 (-17%) | 20.62 (-17%) | 17.81 (-28%) |
| 2x, 12 taps | 8500T, MSVC | 15.21 | 13.99 (-8%) | 15.89 (+4%) | 16.33 (+7%) | 14.91 (-2%) | 14.67 (-4%) | 14.71 (-3%) | **13.30 (-13%)** |
| | 8500T, clang-cl | 14.19 | 13.15 (-7%) | 15.95 (+12%) | 16.01 (+13%) | 15.01 (+6%) | 14.82 (+4%) | 14.90 (+5%) | **12.82 (-10%)** |
| | P-core, MSVC | 7.28 | 6.39 (-12%) | 6.59 (-9%) | 6.96 (-4%) | 7.10 (-2%) | 6.24 (-14%) | 6.82 (-6%) | **6.13 (-16%)** |
| | P-core, clang-cl | 6.71 | 6.37 (-5%) | 6.81 (+1%) | 6.88 (+3%) | 7.00 (+4%) | 6.53 (-3%) | 6.43 (-4%) | **6.12 (-9%)** |
| | E-core, MSVC | 24.60 | 24.41 (-1%) | 17.71 (-28%) | 17.99 (-27%) | **16.41 (-33%)** | 18.02 (-27%) | 18.58 (-24%) | 17.40 (-29%) |
| | E-core, clang-cl | 24.73 | 23.84 (-4%) | 17.90 (-28%) | 18.10 (-27%) | **16.31 (-34%)** | 18.47 (-25%) | 18.64 (-25%) | 16.86 (-32%) |
| 3x, 18 taps | 8500T, MSVC | 13.73 | 12.86 (-6%) | 14.53 (+6%) | 14.83 (+8%) | 13.99 (+2%) | 13.33 (-3%) | **12.81 (-7%)** | 13.02 (-5%) |
| | 8500T, clang-cl | 13.74 | **12.37 (-10%)** | 14.53 (+6%) | 14.45 (+5%) | 13.84 (+1%) | 13.51 (-2%) | 12.90 (-6%) | 12.53 (-9%) |
| | P-core, MSVC | 6.59 | 6.17 (-6%) | 6.69 (+2%) | 7.26 (+10%) | 6.99 (+6%) | 6.11 (-7%) | 6.12 (-7%) | **5.80 (-12%)** |
| | P-core, clang-cl | 6.67 | 6.04 (-9%) | 7.00 (+5%) | 7.00 (+5%) | 7.05 (+6%) | 6.20 (-7%) | 6.21 (-7%) | **5.72 (-14%)** |
| | E-core, MSVC | 23.43 | 22.71 (-3%) | 16.92 (-28%) | 16.60 (-29%) | **16.13 (-31%)** | 17.41 (-26%) | 17.49 (-25%) | 17.67 (-25%) |
| | E-core, clang-cl | 23.39 | 22.48 (-4%) | 16.94 (-28%) | 16.58 (-29%) | **15.73 (-33%)** | 17.60 (-25%) | 17.54 (-25%) | 16.43 (-30%) |
| 4x, 24 taps | 8500T, MSVC | 11.63 | **11.45 (-2%)** | 13.18 (+13%) | 13.50 (+16%) | 13.43 (+15%) | 11.93 (+3%) | 11.50 (-1%) | 12.46 (+7%) |
| | 8500T, clang-cl | 11.79 | **9.99 (-15%)** | 13.11 (+11%) | 13.40 (+14%) | 12.70 (+8%) | 11.72 (-1%) | 11.63 (-1%) | 11.65 (-1%) |
| | P-core, MSVC | **5.41** | 5.57 (+3%) | 6.21 (+15%) | 6.71 (+24%) | 6.60 (+22%) | **5.41 (0%)** | 5.62 (+4%) | 5.55 (+3%) |
| | P-core, clang-cl | 5.90 | **5.27 (-11%)** | 6.47 (+10%) | 6.47 (+10%) | 6.53 (+11%) | 5.59 (-5%) | 5.58 (-5%) | 5.58 (-5%) |
| | E-core, MSVC | 22.22 | 22.98 (+3%) | 15.55 (-30%) | **15.54 (-30%)** | 15.85 (-29%) | 15.91 (-28%) | 16.10 (-28%) | 17.50 (-21%) |
| | E-core, clang-cl | 21.51 | 21.77 (+1%) | 15.43 (-28%) | 15.48 (-28%) | **15.10 (-30%)** | 16.06 (-25%) | 16.06 (-25%) | 15.72 (-27%) |

- **A count known to the compiler is worth as much as the form.** The kernel's own form gains 8-25% from it up to 12
  taps under MSVC on the two big cores, and 6% at 18.
- **Lane weights with a fixed count are the best form for the big cores:** 13-28% off the kernel's form up to 12 taps
  under MSVC, 5-12% at 18. Against the kernel's form with a fixed count they are 2-9 points ahead on the 8500T and 3-13
  on the P-core.
- **Broadcast needs a column with no other loads.** Its fixed form is 7-21% behind fixed lanes on the big cores from 9
  taps up, and loses to the kernel's form from 18. Without the run table's two loads per column it led under clang-cl
  on every core (the notes on the compilers).
- **The E-core prefers broadcast,** fixed lanes 0-10% behind, and gains 20-34% from either.
- **At 24 taps nothing gains more than 2% on the kernel's form under MSVC** on the big cores. Under clang-cl its fixed form gains 11-15%
  there: MSVC's runs out of registers.
- **With the count read, the steps with lane weights hold up best at short runs:** 16-29% off at 5 and 7 taps on both
  big cores, where the loop of 4 chains leaves its last taps to a serial tail.
- **The span 16 bytes off a 32-byte boundary,** 2x, count read, MSVC: the kernel's form +5% on the 8500T and +6% on
  the P-core; the interleaved loop of 3 chains +7% and +15%. Nothing on the E-core.

### Runs of two lengths

A fixed count pads a resize's shorter runs with a zero weight. The fixed pass against the count read over runs of n or
n-1 taps, unpredictably: the padding's cost is in the figure. ms, the fixed forms with their change against the same
form's mixed runs, lanes against the better of its two.

| Ratio | Machine, compiler | Kernel's: mixed | fixed | Lanes: 4 ch. mixed | steps mixed | fixed | Broadcast: 4 ch. mixed | fixed |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1.25x, 5 taps | 8500T, MSVC | 16.21 | 13.15 (-19%) | 14.39 | 13.30 | 12.75 (-4%) | 14.20 | 12.29 (-13%) |
| | 8500T, clang-cl | 16.79 | 12.76 (-24%) | 14.92 | 13.79 | 12.76 (-7%) | 14.19 | 12.34 (-13%) |
| | P-core, MSVC | 8.35 | 7.71 (-8%) | 6.62 | 6.53 | 6.61 (+1%) | 6.80 | 6.89 (+1%) |
| | P-core, clang-cl | 8.60 | 7.53 (-12%) | 6.95 | 6.66 | 6.61 (-1%) | 6.83 | 6.83 (0%) |
| | E-core, MSVC | 19.45 | 17.10 (-12%) | 17.91 | 17.15 | 14.69 (-14%) | 17.63 | 13.80 (-22%) |
| | E-core, clang-cl | 19.37 | 16.90 (-13%) | 18.71 | 17.20 | 14.59 (-15%) | 18.00 | 13.56 (-25%) |
| 1.17x, 7 taps | 8500T, MSVC | 19.43 | 15.99 (-18%) | 18.13 | 16.31 | 15.21 (-7%) | 18.16 | 15.11 (-17%) |
| | 8500T, clang-cl | 19.65 | 15.12 (-23%) | 18.62 | 16.86 | 14.68 (-13%) | 18.19 | 15.23 (-16%) |
| | P-core, MSVC | 9.70 | 7.62 (-21%) | 8.18 | 7.32 | 7.33 (0%) | 8.63 | 8.02 (-7%) |
| | P-core, clang-cl | 9.93 | 8.03 (-19%) | 8.80 | 7.60 | 7.30 (-4%) | 9.76 | 7.87 (-19%) |
| | E-core, MSVC | 24.32 | 25.56 (+5%) | 23.58 | 21.97 | 18.43 (-16%) | 23.34 | 18.41 (-21%) |
| | E-core, clang-cl | 25.48 | 24.80 (-3%) | 23.60 | 22.04 | 18.51 (-16%) | 22.75 | 18.32 (-19%) |
| 1.5x, 9 taps | 8500T, MSVC | 18.54 | 16.69 (-10%) | 16.60 | 16.79 | 14.91 (-10%) | 16.93 | 16.00 (-5%) |
| | 8500T, clang-cl | 17.83 | 15.79 (-11%) | 17.17 | 16.94 | 14.74 (-13%) | 17.24 | 16.03 (-7%) |
| | P-core, MSVC | 8.46 | 7.41 (-12%) | 6.47 | 6.97 | 6.53 (+1%) | 6.83 | 7.41 (+8%) |
| | P-core, clang-cl | 7.90 | 7.38 (-7%) | 7.17 | 7.13 | 6.52 (-9%) | 7.22 | 7.21 (0%) |
| | E-core, MSVC | 26.38 | 25.80 (-2%) | 19.94 | 20.01 | 18.05 (-9%) | 19.81 | 17.47 (-12%) |
| | E-core, clang-cl | 24.62 | 24.66 (0%) | 20.84 | 20.56 | 17.81 (-13%) | 19.87 | 17.38 (-13%) |
| 2x, 12 taps | 8500T, MSVC | 15.26 | 13.99 (-8%) | 14.74 | 14.46 | 13.30 (-8%) | 15.52 | 14.91 (-4%) |
| | 8500T, clang-cl | 14.55 | 13.15 (-10%) | 14.98 | 14.92 | 12.82 (-14%) | 15.59 | 15.01 (-4%) |
| | P-core, MSVC | 7.58 | 6.39 (-16%) | 6.42 | 6.59 | 6.13 (-5%) | 6.79 | 7.10 (+5%) |
| | P-core, clang-cl | 7.03 | 6.37 (-9%) | 6.68 | 6.72 | 6.12 (-8%) | 7.07 | 7.00 (-1%) |
| | E-core, MSVC | 23.56 | 24.41 (+4%) | 18.64 | 18.48 | 17.40 (-6%) | 18.24 | 16.41 (-10%) |
| | E-core, clang-cl | 23.44 | 23.84 (+2%) | 19.03 | 18.73 | 16.86 (-10%) | 18.37 | 16.31 (-11%) |
| 3x, 18 taps | 8500T, MSVC | 13.56 | 12.86 (-5%) | 13.11 | 12.65 | 13.02 (+3%) | 14.29 | 13.99 (-2%) |
| | 8500T, clang-cl | 13.58 | 12.37 (-9%) | 13.70 | 12.79 | 12.53 (-2%) | 14.22 | 13.84 (-3%) |
| | P-core, MSVC | 6.35 | 6.17 (-3%) | 5.84 | 5.85 | 5.80 (-1%) | 6.62 | 6.99 (+6%) |
| | P-core, clang-cl | 6.55 | 6.04 (-8%) | 6.15 | 5.92 | 5.72 (-3%) | 6.84 | 7.05 (+3%) |
| | E-core, MSVC | 22.84 | 22.71 (-1%) | 17.28 | 17.12 | 17.67 (+3%) | 16.78 | 16.13 (-4%) |
| | E-core, clang-cl | 22.75 | 22.48 (-1%) | 18.26 | 17.18 | 16.43 (-4%) | 16.61 | 15.73 (-5%) |
| 4x, 24 taps | 8500T, MSVC | 12.07 | 11.45 (-5%) | 12.21 | 11.70 | 12.46 (+6%) | 13.23 | 13.43 (+2%) |
| | 8500T, clang-cl | 11.84 | 9.99 (-16%) | 12.22 | 11.72 | 11.65 (-1%) | 13.23 | 12.70 (-4%) |
| | P-core, MSVC | 5.55 | 5.57 (0%) | 5.63 | 5.57 | 5.55 (0%) | 6.35 | 6.60 (+4%) |
| | P-core, clang-cl | 6.02 | 5.27 (-12%) | 5.78 | 5.59 | 5.58 (0%) | 6.46 | 6.53 (+1%) |
| | E-core, MSVC | 23.06 | 22.98 (0%) | 16.58 | 16.85 | 17.50 (+6%) | 15.80 | 15.85 (0%) |
| | E-core, clang-cl | 21.52 | 21.77 (+1%) | 16.51 | 16.51 | 15.72 (-5%) | 15.73 | 15.10 (-4%) |

- **The kernel's form keeps its gain:** 8-21% up to 12 taps under MSVC on the big cores.
- **Interleaved lanes keep 4-10% on the 8500T up to 12 taps and 0-5% on the P-core** under MSVC: the count-read
  steps are already near the fixed form there, and the padded tap takes the rest.
- **The E-core keeps 6-16% for lanes and 10-22% for broadcast** up to 12 taps.

### The conversion that interleaves

A 4K RGB32 image in chunks of 128 pixels, ms, MSVC / clang-cl.

| Conversion | 8500T | P-core | E-core |
|---|---:|---:|---:|
| The kernel's: each row to its own floats | 2.61 / 2.33 | 1.09 / 1.02 | 2.86 / 2.75 |
| The rows' bytes unpacked into pixel pairs, then widened | 3.04 / 3.04 | 1.13 / 1.11 | 2.77 / 2.68 |
| Each row widened, lanes permuted into pixel pairs | 3.54 / 3.09 | 1.81 / 1.77 | 4.80 / 3.64 |
| Each row widened, each pixel stored as 16 bytes | 3.28 / 3.09 | 1.37 / 1.76 | 3.69 / 3.65 |

Unpacking the bytes costs the 8500T 16% under MSVC and 30% under clang-cl, the P-core 4% and 9%, the E-core nothing: two
shuffles more per four pixel pairs. The whole pass's figures include it.

### Four-tap runs

An upscale's arithmetic, 720p -> 4K: 3840 columns of exactly 4 taps, no count to read. Each column's first pixel and
weights' address are read from a run table, with a test of the pixel against the buffer's end, as the kernel's pass at
a fixed 4 taps has them; runs a period of 3 columns apart share their weights. ms, the better of three runs, with the
change against the same compiler's kernel's form.

| Form | 8500T: MSVC | clang-cl | E-core: MSVC | clang-cl |
|---|---:|---:|---:|---:|
| The kernel's form: weights spread over a pixel pair, halves summed | 2.92 | 2.73 | 4.01 | 4.01 |
| Interleaved, one chain, broadcast | 2.73 (-7%) | 2.82 (+3%) | 3.20 (-20%) | 3.19 (-20%) |
| Interleaved, two chains, broadcast | 2.70 (-8%) | 2.82 (+3%) | 3.20 (-20%) | 3.21 (-20%) |
| Interleaved, two chains, lanes | 2.83 (-3%) | 2.53 (-7%) | 3.22 (-20%) | 3.08 (-23%) |
| Interleaved, one chain, lanes | 2.81 (-4%) | 2.50 (-8%) | 3.03 (-24%) | 2.91 (-27%) |
| Interleaved, four chains, lanes: the fixed-count column at 4 taps | 2.89 (-1%) | 2.86 (+5%) | 3.62 (-10%) | 3.46 (-14%) |

- **Lane weights in one or two chains gain 3-8% on the 8500T and 20-27% on the E-core.** One chain and two are level.
- **Four chains give up most of that:** four multiplies and three adds, where one chain is a multiply and three multiply-adds.
- **Broadcast weights gain 7-8% on the 8500T under MSVC only.**
- **The P-core's lines do not repeat across sessions.** Each form and the kernel's take one of two levels about 15-20%
  apart, set for hours and sometimes per process, and their order changes with it: within 1.47-1.85 ms for all of
  them. The whole pass's lines and the register-only ones held within 4% meanwhile. VTune finds no blocked store
  forwarding, machine clears or mispredicts; these loops wait partly on L1 misses and on their output stores.
- **Without the run table the ranking is the reverse.** With each column's pixels and weights at a constant step, and
  a block of weights per column, broadcast gained 11% on the 8500T under MSVC, and the kernel's form took 3.59 ms:
  its 32-byte weight loads went through 240 KB of weights where the kernel's shared ones stay in L1. The program
  still times those lines.

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

Section S is the level's form over a run table as the kernel's, at the whole pass's six run lengths, with the count
read, fixed, and read over runs of n or n-1 taps. Arithmetic and the run lookup, no conversion. ms, the fixed count
with its change against the count read, and against the runs of two lengths: the padding paid.

| Taps | N4100, MSVC: read | fixed | padding paid | Pi 4, GCC 14.2: read | fixed | padding paid | Pi 4, Clang 22: read | fixed | padding paid |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 5 | 37.09 | 33.80 (-9%) | -6% | 57.58 | 51.61 (-10%) | -3% | 54.79 | 47.08 (-14%) | -11% |
| 7 | 50.19 | 47.70 (-5%) | -2% | 74.69 | 68.34 (-9%) | -3% | 74.43 | 63.26 (-15%) | -12% |
| 9 | 53.79 | 47.52 (-12%) | -9% | 86.63 | 69.33 (-20%) | -19% | 78.94 | 73.23 (-7%) | -5% |
| 12 | 51.41 | 44.48 (-13%) | -9% | 75.99 | 60.05 (-21%) | -19% | 70.58 | 62.80 (-11%) | -9% |
| 18 | 47.44 | 47.73 (+1%) | +2% | 67.13 | 61.86 (-8%) | -6% | 64.52 | 60.06 (-7%) | -4% |
| 24 | 46.68 | 49.05 (+5%) | +4% | 60.73 | 56.17 (-8%) | -8% | 57.80 | 54.82 (-5%) | -5% |

- **A fixed count gains at both levels:** 5-13% up to 12 taps at SSE4.1, 5-21% at every length at NEON.
- **At SSE4.1 it loses from 18 taps up.** MSVC's written-out 24-tap column has 66 stack references: 16 registers, and
  no three-operand instructions.
- The interleaved buffer and broadcast weights gain nothing here with a fixed count either.
- The N4100 ran two passes, the second 4% slower on every line, the machine warming up; the fixed count's change
  against the count read is the same in both within a point. The Pi's passes agree within 3%.

## What follows

- More multiply-add throughput, wider vectors or more chains, would gain little on the Skylake family: its FMA units
  are idle three quarters of the time.
- The room is in the work per multiply-add. Stepped pointers in the passes and the block loop took 3-7% of it (the
  main doc's sections on them); the horizontal form itself is the larger term.
- **The interleaved form is in the kernel** for the 4-float layouts' row pairs at padded lengths, with lane weights
  ([resize-performance.md](resize-performance.md), "Row pairs interleaved at AVX2, at padded lengths"). Whole rows: the
  8500T up to 7% faster, the P-core up to 12%, the E-core 5-23%: less than this program's figures on the big cores.
  - On runs read per run it lost to the kernel's form on both big cores, up to 36% at 96-360 taps (that doc's
    experiment 30), as this program found from 24 taps up.
  - The 1- and 2-float layouts keep the kernel's form: a pixel pair would fill 2 or 4 of a register's 8 floats.
- **A pass per x run length, every x run padded to the resize's longest,** is in the kernel for its own form
  ([resize-performance.md](resize-performance.md), "The x runs padded to one length, and a kernel per length"): whole
  rows 3-10% faster at AVX2 and NEON, 8-16% at SSE4.1.
  - Here it gains the kernel's form 8-21% up to 12 taps under MSVC on the big cores, the padding paid.
  - On top of interleaved lanes it adds 4-10% on the 8500T up to 12 taps and 0-5% on the P-core, the padding paid.
  - Every length is another copy of the kernel per pixel layout. Above about 20 taps there is nothing to gain at AVX2
    under MSVC; NEON gains up to 32, the longest measured.
  - At the 128-bit levels the arithmetic with its run lookup gains 2-9% up to 12 taps at SSE4.1 on the N4100 and
    3-19% up to 24 at NEON on the Pi, the padding paid.

## Limits of these figures

- The ceilings and the 8500T's breakdown are one scenario: a 2x downscale with 12-tap runs.
- The kernel's rates are MSVC's on the 8500T only. No kernel profile exists for the 12600K.
- The program's vertical variant packs 64 floats to 64 bytes, not RGB32's 3 floats to 4 bytes.
- At SSE4.1 and NEON only the horizontal forms of sections N and S are measured, without the conversion, not the
  levels' ceilings: their peak is 8 floats per cycle on a core with two 128-bit units.
- The interleaved form's sections have no straight alpha and no RGB24 source, and section T has no run table.
- The four-tap runs are arithmetic and the run lookup: no conversion, no sliding span.
- The whole pass's runs within one resize are all of one length, or of two in the padding table; the kernel's are
  shorter at the image's edges too.
- GCC is not measured at this source. At a8dd837 its kernel's form was the fastest of the three compilers' on the 8500T
  at 2x, 3x and 1.5x, and its best interleaved pass 1-11% ahead of it.
