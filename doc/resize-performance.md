# Resizer performance

The measurements behind the resizer's design, and the experiments that lost. Benchmarks live in
`tests/cimageresizer_benchmarks.cpp`; `scripts/run_tests --benchmark` runs them and `tests/report_benchmark_ratios.py` prints
the table. Every table names the commit it was measured at: re-measure after changing the resizer.

This document holds what the design rests on, with headline figures. The full tables are in the per-CPU logs in `doc/cpu/`,
under the same section headings. How far the kernels run from the hardware's multiply-add peak, and what the distance
consists of, is in [throughput-ceiling.md](throughput-ceiling.md).

## Reading the numbers

- The resizer / QImage ratio is the only figure that compares across runs, machines and CI jobs.
- Absolute ms compare only on one machine, and only while the rows the change cannot affect agree.
- Noise gauges: the QImage controls. Every resizer row runs a SIMD kernel. Older reports' Grayscale8, RGB24 and `[scalar]`
  rows ran a scalar path, since removed.
- Run-to-run noise:
  - PC: 5-8% on the few-ms rows, 1-2% elsewhere.
  - Pi: 5-10%.
  - windows-latest runner: most rows within 3%, some 10%.
  - ubuntu-24.04-arm runner, Clang: about 1%.
  - macos-latest runner: up to 2x, too noisy to read per scenario.
  - A real M1, same-session A/B: about 1%.
  - Celeron N4100: 0.5-1%.
  - Core i5-8500T: 5-8% between processes on the 4K -> 1080p rows. Twenty alternating rounds resolve about 2%; six do
    not resolve 5% (its log).
  - An Ubuntu VM on the PC: 10-20% between runs of one binary, whole runs drifting together. Fifteen alternating rounds,
    read by each build's minimum and by the median of back-to-back pairs, resolve about 3%.
- A CI comparison needs several samples per side: re-running the old commit's run alongside the new one gives same-time
  pairs. A re-run is a new attempt of the same run, with its own logs.
- An attempt's logs: `gh api repos/<repo>/actions/runs/<run>/attempts/<n>/jobs` for its job ids, then
  `gh api --allow-escape-sequences repos/<repo>/actions/jobs/<id>/logs`. `gh run view --attempt <n>` lists the latest
  attempt's jobs whatever n is, and without the flag `gh api` prints nothing for a log.
- `QT_NO_GUI_THREADPOOL=1` keeps the Qt control serial; `run_tests` sets it.
- Every benchmark allocates a fresh destination inside the timing, as the Qt control does. Threaded rows measured up to
  29b97e9 reused one destination.
- Windows throttles an unfocused console process onto E-cores: such runs came out 2-4x slow. The test runner opts out
  through `runCatchSession` (cpp-template-utils).
- A Pi 4 without cooling throttles under sustained load: `vcgencmd get_throttled` must print `0x0` after a run.
- A/B rounds alternate the builds.
- Under MSVC on the PC, the 3x upscale rows move 5-10% with where the kernel's loops land in memory, with no change to
  their instructions (the section on jumps and 32-byte boundaries). An MSVC A/B on those rows needs a placement control:
  the same build with `__nop()` padding at the kernel's start, in 16-byte steps.
- An A/B build in a second worktree starts from deleted object files: after a checkout there, MSBuild recompiled the AVX2
  kernels and left the other sources' objects stale.
- A second clang-cl variant built by jom in one shell session reused the first's objects, the outputs deleted in
  between: its build took 10 s. One session per variant, with the kernel sources' timestamps touched.
- An A/B table's rows that the change cannot reach are its control: a "gain" no larger than their spread is not one.
- A row can move with code it does not run, where the change moves its jumps (the sections on jumps and 32-byte
  boundaries). A change read from one row needs that row to run the changed code.
- clang-cl's AVX2 kernels were built without that padding up to e8d2d15: on the 8500T each of their rows up to that
  commit carries several percent of jump placement.
- The SSE4.1 kernels are not padded, and the 8500T runs them only when the benchmark caps the level: its SSE4.1 rows
  carry jump placement, up to 14% on one row. An SSE4.1 change is judged on the i3-2310M or the N4100.
- A process on the 8500T lands in one of two states about 8% apart, on an upscale's vertical pass: six processes of one
  binary gave per-process means of 16.2-16.7 or 17.4-18.2 ms. A single run of each variant can show either.
  - It is not the float buffers' placement, the core, the clock or other load (its log). Its cause is not known.
- A VTune run's instruction counts per region repeat within a few percent; its times are one process's, and need
  repeats as a timing run does. Regions are compared per unit of work: a loop the change does not touch, counted by
  its instructions, gives the unit.
- A CI label's runner CPU varies between runs, and the ratios with it: Qt's SSE code and the AVX2 kernel do not scale
  alike. A job's numbers compare across runs only when the CPU line in its report header names the same model.
- The report header names the commit, CPU, compiler and cache sizing.

## Reading the generated code

- The release builds use link-time optimization on every compiler, so the object files hold no machine code. Disassemble
  the executable: `objdump -d -C`, or `dumpbin /disasm` with the PDB next to it.
- Under MSVC the two kernel levels are generated differently: the SSE4.1 source at link time with the rest of the
  project, the AVX2 source by its own compile rule. An inlining decision can differ between them.
- Two builds' kernels compare by instruction sequence with the `nop`s and jump targets removed: `extract_kernels.py`.
- A variant build takes its define through the `CL` environment variable, set for the msbuild step only: set before
  qmake, it breaks qmake's compiler probe. From Git Bash the value starts with `-D`, as a leading `/` is rewritten into a path.
- VTune's hardware events need an elevated prompt. With about ten events each is counted throughout; the
  `uarch-exploration` preset rotates some 190, and its totals came out inconsistent. `-report hw-events -group-by function`
  separates instantiations of one template.
- `-collect uarch-exploration` stopped the 8500T with bugcheck 0xD1 in VTune's sampling driver, `sepdrv5.sys`, during
  its first run. Hotspots in hardware mode has not.
- Where a kernel's time goes: `-collect hotspots -knob sampling-mode=hw` on one benchmark section, then
  `-report hotspots -group-by address`, the samples summed over each loop's address range in the `dumpbin` listing
  (`annotate_vtune_hotspots.py`, the full commands in its header).
  Grouped by function or source line, the forced-inline primitives each get their own row and a loop's share is lost.
- A benchmark section's VTune totals include Catch2's clock calibration (`RtlQueryPerformanceCounter`, about 1 s).
- Instructions retired per cycle tells an instruction-bound loop (above 3 on Coffee Lake) from a stalled one.

## Tools

In `scripts/perf/`, each with `--help`:

| Script | Purpose |
|---|---|
| `run_benchmark_rounds.py` | Runs several builds' test executables in alternating rounds, one result file per run, and prints the other processes' CPU time over each run. `--no-controls` leaves out the QImage and SSE4.1 runs, about half of a round. `--pinned NAME=MASK`, repeatable, runs each build once per named series at the same time, each pinned to a hexadecimal affinity mask and at high priority on Windows, with results in a directory per series: a hybrid CPU's two core types in one run. |
| `compare_benchmark_rounds.py` | Per benchmark, each build against a reference build: the median of same-round ratios and the minimum-to-minimum ratio, the two figures of the per-CPU logs' A/B tables. `--rounds` prints each build's time in each round instead: a row that reads two levels by launch shows there. |
| `count_boundary_jumps.py` | Counts the kernels' jumps that cross or end on a 32-byte boundary in a `dumpbin` or x86 `objdump` listing. A padded build counts under 1%, a plain one 15-25%. `--loops` prints the loops holding such jumps. |
| `extract_kernels.py` | Prints a listing's kernels in a form two builds diff in: padding dropped, a jump's target as its distance in instructions. `--loops` numbers each kernel's loops and prints their sizes, multiplies, calls and stack references, `--stack-references` the referencing instructions too. `--loop N` prints one loop's instructions. Takes ARM64 `objdump` listings as well. |
| `annotate_vtune_hotspots.py` | Sums a VTune by-address hotspots report over the kernels' loops of a listing, or over each instruction. |
| `multiply_add_ceiling.cpp` | A standalone program: the two passes' multiply-adds without the kernel's bookkeeping, and other forms of the horizontal pass, by section. Builds for AVX2, plain x64 and ARM64. Its header has the build commands. |

The builds to compare are made by hand, the toolchain paths differing per machine:

1. A second worktree beside the checkout, `git worktree add ../image-processing-ab <commit>`: the tests reach the
   sibling repositories by relative path.
2. Per variant: delete `tests/build` and `tests/bin`, build, and copy `image-processing-tests` out under the variant's
   name, the `.pdb` with it. The name becomes the build's label in the two scripts.
3. A variant that differs by a define takes it through `CL` (the notes on reading the generated code).
4. A listing with `dumpbin /disasm` needs the `.pdb` beside the executable under the name the executable records,
   `image-processing-tests.pdb`: one directory per variant.
5. A clang-cl build goes into a directory of its own, from a `vcvars64` prompt with Visual Studio's `VC\Tools\Llvm\x64\bin`
   and Qt's `bin` on `PATH`. The AVX2 source's object path is set apart from `OBJECTS_DIR`:
   `qmake -spec win32-clang-msvc CONFIG+=release CONFIG-=debug <tree>\tests\image-processing-tests.pro -after "OBJECTS_DIR=<dir>\obj"
   "MOC_DIR=<dir>\moc" "DESTDIR=<dir>\bin" "avx2Compiler.output=<dir>\obj\cimageresizer_simd_avx2.obj"`, then `nmake release`.

## Machines

| | CPU | Caches | Toolchain |
|---|---|---|---|
| [PC](cpu/core-i5-12600k.md) | Core i5-12600K, P-cores | 48 KB L1D, 1.25 MB private L2 per P-core | MSVC 19.51 (toolset v145), Qt 6.11.2 |
| [Raspberry Pi 4](cpu/raspberry-pi-4.md) | 4x Cortex-A72, 1.8 GHz | 32 KB L1D, 1 MB L2 shared by all cores | Clang 22 unless noted |
| [Celeron N4100](cpu/celeron-n4100.md) | 4x Goldmont Plus, no AVX: SSE4.1 is its only level | 24 KB L1D, 4 MB L2 shared by all cores | MSVC 19.51, Qt 6.12.0 |
| [Core i3-2310M](cpu/core-i3-2310m.md) | 2x Sandy Bridge, 4 threads, no AVX2: SSE4.1 is its only level | 32 KB L1D, 256 KB private L2 per core, 3 MB L3 | Runs the 8500T's builds |
| [Core i5-8500T](cpu/core-i5-8500t.md) | 6x Coffee Lake | 32 KB L1D, 256 KB private L2 per core, 9 MB L3 | MSVC 19.51, Qt 6.11.2; clang-cl where noted |
| [ubuntu-24.04-arm](cpu/neoverse-n2.md) | Cobalt 100 (Neoverse N2), 4 cores | 1 MB private L2 per core | GCC and Clang jobs |
| [ubuntu-latest, windows-latest](cpu/x64-ci-runners.md) | 2 cores, 4 threads, varying by run. Seen: AMD EPYC 9V45 (Zen 5), 9V74 (Zen 4), 7763 (Zen 3); Xeon Platinum 8573C, 8370C, Xeon 6973P-C | Private L2 per core: 1 MB on Zen 4 and 5, 512 KB on Zen 3, 2 MB on the Xeons (1.25 MB on the 8370C) | GCC and Clang; MSVC and clang-cl |
| [macos-latest](cpu/apple-m1.md) | Apple M1 (virtual), 3 cores | 12 MB L2 | Apple Clang |

## Standing against Qt

Resizer / QImage, lower is better. "T": with threads. "-": no such benchmark.
- PC at 9d86565, before column strips (mean of three rounds); its Grayscale8 and RGB24 at e5ea70d.
- Pi, Clang 22, at 6b6bf7e; its Grayscale8 and RGB24 at e5ea70d.
- Pi, GCC 14.2, at bdf65f9 (one run).
- N4100 at a6c69e7 (minimum of three rounds).
- 8500T (MSVC) at a6c69e7, minimum of three rounds: its [log](cpu/core-i5-8500t.md) has the table. Opaque 4-byte
  downscales 1.6-2.0x behind Qt (4K -> 1080p RGB32 1.81, 101 MP -> 720p 1.95), every other row ahead: 4K -> 1080p RGBA32
  1.09, 720p -> 4K RGB32 0.45, 24 MP -> 1080p with threads 0.45.
- The PC's and the Pi's Clang columns predate the two-chain block's assigned first block and the shared weights.

| Scenario | PC | PC, T | Pi, Clang | Pi, Clang, T | Pi, GCC | Pi, GCC, T | N4100 | N4100, T |
|---|---|---|---|---|---|---|---|---|
| 24 MP -> 1080p | 1.77 | 0.51 | 2.90 | 0.84 | 2.61 | 0.73 | 2.91 | 0.82 |
| 4K -> 1080p RGB32 | 1.99 | - | 4.97 | - | 4.53 | - | 3.04 | - |
| 4K -> 1080p RGBA32 | 0.94 | 0.28 | 2.33 | 0.72 | 2.10 | 0.61 | 1.49 | 0.43 |
| 720p -> 4K RGB32 | 0.59 | - | 1.08 | - | 1.00 | - | 0.72 | - |
| 720p -> 4K RGBA32 | 0.31 | 0.07 | 0.32 | 0.10 | 0.28 | 0.10 | 0.36 | 0.12 |
| 1080p -> 1440p | 0.87 | 0.21 | 1.84 | 0.63 | 1.77 | 0.52 | 1.15 | 0.38 |
| 1080p -> 240p | 2.36 | 0.95 | 3.66 | 1.18 | 3.43 | 0.99 | 3.79 | 1.16 |
| 4K -> 64x64 | 2.56 | 1.05 | 3.52 | 1.37 | 3.70 | 1.31 | 3.08 | 1.12 |
| 101 MP -> 720p | 2.22 | 0.64 | 3.91 | 1.17 | 3.72 | 1.16 | 3.82 | 1.14 |
| 1080p, native size | 1.12 | - | 1.00 | - | 1.00 | - | 0.91 | - |
| 4K -> 1080p Grayscale8 | 0.50 | - | 0.74 | - | 0.72 | - | 0.56 | - |
| 720p -> 4K Grayscale8 | 0.48 | - | 0.44 | - | 0.39 | - | 0.28 | - |
| 4K -> 1080p RGB24 | 0.81 | 0.27 | 2.75 | 0.80 | 2.72 | 0.74 | 1.26 | 0.36 |
| 720p -> 4K RGB24 | 0.56 | 0.20 | 0.77 | 0.29 | 0.78 | 0.22 | 0.51 | 0.18 |

- PC: upscales, straight-alpha images, Grayscale8 and RGB24 beat Qt; Qt premultiplies alpha in a separate pass.
- Pi: straight-alpha upscales, Grayscale8 and the RGB24 upscale beat Qt single-threaded, and the opaque 720p -> 4K about
  matches it. With threads every upscale beats it, and the 24 MP, RGBA32 and RGB24 downscales too; under GCC at
  bdf65f9 1080p -> 240p matches it as well.
- N4100: the PC's pattern. In absolute time it runs 1.4-2.0x faster than the Pi on downscales, 1.2-1.6x on
  upscales.
- Opaque 4-byte downscales stay behind Qt single-threaded: about 2x on the PC, 2.6-5x on the Pi, 2.9-3.8x on the N4100.

## What the design rests on

### Per-instruction-set primitives, not SIMDe (963b54b)

The kernel outline is written against primitives that state what the kernel needs, such as spreading a weight pair or
packing 8 floats to words. Each instruction set implements them natively. SIMDe, translating AVX2 intrinsics op by op,
falls short both ways:
- AVX2 code lowered to SSE4.1 falls back to scalar loops in the hot paths: `permutevar8x32_ps`, `cvttps_epi32` and the
  256-bit `shuffle_ps`, plus `cvtepu8_epi32` under MSVC, which lacks vector extensions.
- On AArch64, GCC kept SIMDe's 256-bit type in memory, copying it through the stack at every operation.

Against SIMDe, SIMD rows:
- ARM GCC on Neoverse-N2: -40% to -55% everywhere, now within 2-22% of Clang, down from about 2x.
- ARM Clang, the Pi and the M1: -2% to -23%, upscales most.
- x64: the AVX2 instructions are unchanged.

### Source rows converted to float once (d436d41)

The horizontal pass used to widen bytes to floats at every tap; each pixel is now converted once. x64 widens as part of
the load, NEON needs separate instructions. So the change pays on ARM and is about neutral on x64.

CI, resizer / QImage, 24 MP -> 1080p single-threaded: ARM Clang 5.14 -> 2.30, ARM GCC 6.46 -> 4.95, macOS 3.91 -> 2.50,
x64 GCC 2.71 -> 2.77, MSVC 2.27 -> 2.44.

### A sliding source buffer, not whole converted rows (a9756cb)

Whole converted rows are 4x the byte rows. On the Pi, four threads' float rows overflow the shared 1 MB L2, and threading
stopped paying. `SlidingSourceFloats` converts into a buffer of a few KB per row that slides along the row.

- Pi, 24 MP -> 1080p with threads, ms: 246 before, 462 with whole rows, 232 sliding. Single-threaded: 542, 427, 476.
- Single-threaded, sliding keeps a third to two thirds of the whole-row gain.
- Capacity is two of the longest runs plus a conversion chunk. The first version held one run plus
  72 pixels. For 4K -> 64x64 (360-pixel windows), that copied every source pixel about 5 extra times per sweep.
- CI runners with large private L2 prefer whole rows single-threaded: ARM Clang 4K -> 1080p 3.41 -> 3.64. The Pi decides:
  it is the target hardware, and its gain is far larger.

### Conversion chunk of 128 pixels (1ecd630)

The chunk sets how far conversion runs ahead of the current run:

- A larger chunk copies less when the buffer slides.
- A smaller chunk keeps the buffer small in L1.

Estimates for 24 MP -> 1080p:

| Chunk | Buffer per row | Copied share of float stores |
|---|---|---|
| 256 | 4.9 KB | 10% |
| 128 | 2.8 KB | 19% |
| 64 | 1.8 KB | 34% |
| 32 | 1.3 KB | 55% |
| 16 | 1.0 KB | 78% |

- 256 costs the Pi 5-10% with threads, likely from L1 pressure: more buffer next to a 23 KB temp row in a 32 KB L1.
- 128 matches 64 with threads, and matches or beats 256 single-threaded.
- Net on the Pi, 7919eaf -> 1ecd630, 24 MP -> 1080p: 542 -> 458 ms single-threaded, 246 -> 235 with threads.

### Straight alpha premultiplied in the kernel (003d5e6)

Qt converts straight alpha to `ARGB32_Premultiplied` before scaling. That pass is the difference between its RGBA32 and
RGB32 4K -> 1080p controls: 18.7 against 8.1 ms on the PC, 87.9 against 40.2 on the Pi. The same conversion in our Qt bridge
would add a pass and a 33 MB temporary per 4K frame. Premultiplying while converting to float costs nothing measurable on
the PC, and about 5% single-threaded on the Pi.

### Output blocks packed whole at AVX2, and capped at alpha as bytes

The vertical pass rounds a block of floats to bytes. In a 3x RGBA32 upscale that step cost as much as the taps (27% and
25% of the kernel, 8500T, VTune): its shuffles, extracts and packs all need port 5.

- AVX2 packs a block's 32 floats with 256-bit packs. They work per 128-bit lane, so one dword permute per block puts
  the bytes in order, and one store writes them. Packing each vector at 128 bits takes an extract per vector.
- Color is capped at alpha on the packed bytes, at every level: one shuffle and one minimum per 16 or 32 bytes, not one
  of each per float vector. Rounding and saturation keep order, so the bytes equal those of capping the floats.
- Port-5 operations per 32 RGBA floats at AVX2: 14 -> 5. Operations in all: 28 -> 15.

| Row, against e60816d | 8500T MSVC | 8500T clang-cl | N4100 MSVC | N4100 clang-cl | Pi GCC | Pi Clang |
|---|---:|---:|---:|---:|---:|---:|
| 720p -> 4K RGBA32 | -12.8% | -13.2% | -5.6% | -7.7% | -1.9% | +0.4% |
| 720p -> 4K RGB32 | -5.0% | -6.1% | +1.2% | -0.3% | -0.1% | -0.5% |
| 720p -> 4K Grayscale8 | -4.7% | -6.7% | -0.1% | +0.1% | -0.1% | -0.3% |
| 720p -> 4K RGBA32, threads | -6.8% | -6.4% | -3.1% | -7.7% | -3.4% | -2.3% |

SSE4.1 and NEON change only where there is alpha. Downscale rows stayed within the noise at every level.

### Forced inlining on every platform (1199012)

The kernel's helpers run per pixel or per output column, so `IMAGE_PROCESSING_SIMD_INLINE` forces inlining on every
platform:
- On x64 the forcing came with the AVX2 target attribute.
- On ARM the macro was plain `inline`. Once 003d5e6 enlarged `prepareRun`, Clang called it out of line, once per output
  column.
- A per-column cost weighs most on upscales, which have the most columns per source pixel.

Resizer / QImage, 1080p -> 1440p: ARM Clang runner 1.79 -> 2.07 with the call; Pi 1.92 before it, 2.10 with it, 1.85 inlined.

### One outline for every pixel layout (e5ea70d)

The SIMD outline serves 1-4 channels at any pixel stride.
- The sliding source buffer holds 1, 2 or 4 floats per pixel. RGB gets a 4th float, 0, and runs the 4-channel code.
- The horizontal pass works in blocks of 8 source floats per chain. AVX2 and NEON run four chains: 8 taps of 4-float
  pixels, 16 of 2-float, 32 of 1-float. SSE4.1 runs two (the section on two chains). 1- and 2-float pixels end with a
  horizontal sum per output pixel.
- The vertical pass writes blocks of 32 floats, 24 for RGB.
- Tight packing and RGB32 get a compile-time stride, with whole-vector conversion and writes. Other strides convert and
  store pixel by pixel.

Against the scalar path it replaced:
- PC, MSVC, ms: 4K -> 1080p RGB24 59.1 -> 16.5, 4K -> 1080p Grayscale8 22.5 -> 10.6. RGB24 now costs what RGB32 does.
- Pi, Clang, resizer / QImage: 4K -> 1080p RGB24 3.76 -> 2.75, Grayscale8 1.19 -> 0.74.
- SSE4.1 / AVX2 time under MSVC on the PC: Grayscale8 1.1x, RGB24 1.25-1.7x.

### The horizontal passes read the runs' array pointer once (f2f4569, 03113a6)

The horizontal passes take `AxisWeights::runs`' pointer into a local ahead of their loops.
Through the `AxisWeights` reference the compiler reloads it per pixel: the loop's stores may alias the object.

- The reload sits ahead of the load that yields the pixel's tap count, so every branch on that count resolves later.
- x64 GCC kept the reference itself on the stack in the 3-channel AVX2 kernel at e5ea70d, a third load in that chain.

Gains: x64 GCC on the EPYC 7763 5-8% on most AVX2 rows (4K -> 1080p RGB32 27.85 -> 25.82 ms), ARM GCC 1-16%, MSVC's AVX2
downscales on the PC 3-6%. Clang: within 2% everywhere.

`AxisWeights::runFor` is force-inlined. MSVC's link-time code generation called a lookup of its kind once per output
pixel in the SSE4.1 kernels, and did the same to a `std::span` constructor written directly in the loop (f2f4569): 4-6% on the SSE4.1
upscales on the PC, 4-18% on the EPYC 7763 runner, nothing on the N4100.

Calls written in the hot loop bodies are forced inline at the call site as well (`IMAGE_PROCESSING_FORCE_INLINE_CALLS`),
which covers standard library calls that no function attribute of ours can reach.
- MSVC: `[[msvc::forceinline_calls]]` takes effect on a block. Ahead of a `for` statement it left the calls in the loop's
  body alone. `[[msvc::flatten]]` on the kernel left `writePixelBytes` out of line.
- clang-cl: `[[clang::always_inline]]` on the block. Inside a template it crashes Clang 18.1.3 and Apple clang 21 (Xcode 26.6)
  in `Sema::CheckAlwaysInlineAttr`; 20.1.8 and 22.1.3 compile it.
- GCC has no per-block form: the kernel's entry function is `flatten`ed. Clang off Windows takes the same form.
- `std::min` over an initializer list is a library call under MSVC whatever the attributes: the kernels use the
  two-argument overload.

Gains:
- `flatten` under GCC on Neoverse-N2: 24 MP -> 1080p 92.4 -> 78.3 ms, level with Clang. On the Pi: 480 -> 451 ms.
  On the EPYC 7763: -0.9% on average.
- Call-site forcing under MSVC: about 3% on the EPYC 7763. On the N4100, the span that holds it took 7-9% off the
  4-byte and RGB24 downscales.

The check is the disassembly. Under MSVC a kernel's only calls are `memmove`, `memcpy` on the runtime-stride paths,
allocation, the ring's constructor and `clearAvxUpperState`.

### Jumps kept off 32-byte boundaries under MSVC (14e731e)

MSVC aligns loops to 16 bytes. On the PC's P-cores the kernels' upscale path runs at two speeds depending on where its
loops land within a 64-byte line, with identical instructions. Any edit that moves the kernels' code flips it.

- Each kernel has its own placements. Downscales do not react.
- A reduced loop, `tests/msvc_loop_placement_repro.cpp`, shows 7-28% between placements. VTune finds equal instruction,
  micro-op and branch counts and no mispredictions; the slow placement has the micro-op cache deliver partial groups in
  3.6-4.0 cycles per iteration against 1.4-1.9.
- The same loop under clang-cl 22 runs within 1% at every placement.
- The EPYC runners show at most a few percent, and not consistently: CI cannot check this.

`/QIntel-jcc-erratum` pads so that no jump crosses or ends on a 32-byte boundary. Only the AVX2 source is compiled with it.
- PC, AVX2 720p -> 4K Grayscale8, median ms: 8.12 and 7.56 at the plain build's two placements, 7.41-7.80 with the switch.
- On the SSE4.1 level the switch costs wherever that level was measured, the PC's P-cores aside:
  - EPYC 7763: RGB32 upscales 13-17%.
  - The PC's E-cores (Gracemont): RGB32 downscales 17-35%. Cause not investigated.
  - N4100: up to 5.6%, no row gaining.

### Jumps kept off 32-byte boundaries under GCC and Clang

The Skylake family (through Comet Lake) has the JCC erratum: its microcode keeps a jump that crosses or ends on a 32-byte
boundary out of the decoded cache. There the kernels' speed follows where their jumps fall, and any change to a kernel
moves them: on the 8500T two GCC builds of one kernel differ by 5-8%, and one source change read as +6% and -6% on the
same row at two placements.

On x64 Linux, and under clang-cl, the AVX2 kernel file compiles through its own rule with the assembler's
`-mbranches-within-32B-boundaries`.
- 8500T, GCC: the padded build is 2-11% faster than a plain build whose jumps fell badly, 0-7% faster than one whose
  jumps fell well. Clang: within +3% to -4% of a plain build that fell well.
- 8500T, clang-cl: 4K -> 1080p Grayscale8 -9% against a plain build with two such jumps on its per-column path, the
  other rows 0% to -4%. The AVX2 kernels' jumps and calls on a boundary: 755 of 4419 plain, 101 of 4535 padded.
- PC (Alder Lake, GCC in the VM, 14 rounds): the padding costs 5% on the RGB32 upscales, 2-4% on the 4K -> 1080p RGB32
  and Grayscale8 downscales and 101 MP -> 720p, and gains 2% on the RGBA32 upscale. The older CPUs' gain was preferred.
  AMD is unmeasured.
- The rule drops LTO for that file: the assembler runs at the link under LTO, and a per-file option is then ignored
  without a diagnostic. LTO's only effect on a kernel, x64 and NEON alike: `TempRowRing`'s constructor, run once per
  band and strip, is inlined into it.
- The SSE4.1 kernels are left alone, as under MSVC: a CPU with the erratum takes the AVX2 ones.

### One-channel upscales filter four columns at a time (0bd90e4)

Where every x run has 4 taps (the next section), the one-channel horizontal pass (`filterHorizontalFourTapRunGroups`)
multiplies four columns' runs and reduces them together: one reduction and one store per four columns, no branch on the
run's length. The general pass reduces and stores each column on its own.

- 720p -> 4K Grayscale8: PC, MSVC, AVX2 7.13 -> 5.45 ms, SSE4.1 8.34 -> 6.12. MSVC and Clang on the EPYC 7763 -27% to -30%.
  N4100 -20%. Clang on Neoverse-N2 -14%.
- GCC gains a third or less of what the others do, on both architectures: -8% to -10% on the EPYC 7763, -2% on
  Neoverse-N2. Not investigated.
- Two-channel pixels still take the general pass.
- Whole groups whose columns lie in the source buffers are filtered from the buffers' range held by value
  (`filterBufferedFourTapRunGroups`), as in the general pass below. 720p -> 4K Grayscale8: 8500T, AVX2 -17% under MSVC,
  -20% under clang-cl; N4100, SSE4.1 -19% and -17%; Pi, NEON -9% under GCC, -12% under Clang.
- With that, clang-cl's 4K -> 1080p Grayscale8 is 3-4% slower on both machines; MSVC +2% on the 8500T. The row does not run
  the changed loop, and its own loops are the same size in the listing: open lead 18.

### The x runs padded to one length, and a kernel per length

Where no x run exceeds the level's limit, every x run is padded to the longest, and the horizontal pass is compiled
for that length: no count to read, no step to test. An upscale's runs are 4 taps; a Lanczos3 downscale's are 6-7 at
the gentlest ratio, 12 at 2x, 17 at 3x, 24 at 4x, 32 at 5.3x. With the count read, a 4-tap column pair spent about
60 instructions on 10 of arithmetic: zeroed accumulators, the steps' checks, the reduction. VTune on the 8500T, MSVC, 720p -> 4K RGBA32 then:
the horizontal filter loop 38% of the kernel, the conversion 5%, the vertical taps 25%, the vertical output 27%.

- `padRunsToOneLength` pads every x run to the longest, 4 taps at least: zero weights after the run, or before it where
  it would pass the source's end, so that all its source pixels exist. A 3x upscale's runs alternate 4 and 1 taps
  before it. The y axis is not padded: a zero tap there costs a row sweep.
- The limit, `longestPaddedXRun`: 1- and 4-float pixels up to the level's `longestFixedXRun`, 20 taps at AVX2, 32 at
  NEON and 12 at SSE4.1. Past 12 taps at SSE4.1 MSVC's 4-float column runs out of registers; past 20 at AVX2 the
  interleaved columns of the next section lose the 8500T 3-7% under MSVC (experiment 31); NEON gains up to 32, the
  longest measured. 2-float pixels, a rare layout, read the count at every length.
- `filterRun` takes the length as a template parameter, 0 where it reads the run's. Its blocks are written out by
  `filterBlocksFrom` (MSVC keeps a loop over a known count as a loop), and its steps fold away. A run shorter than a
  block starts its sums with its first step's products.
- At 4 taps of 4-float pixels at the 128-bit levels that is a weight load, two spreads, and per row a multiply, a
  multiply-add, the reduction and the store; at AVX2 see the next section. One-channel pixels at 4 taps take the pass of
  the section above.
- The kernel is compiled per pixel layout and run length, `resizeRowsWith`, and `resizeRows` takes one from a table per
  call: 128 kernels per level at AVX2, 212 at NEON and 72 at SSE4.1, from 18. MSVC's objects with 4-float pixels alone
  at their limits: AVX2 2.0 -> 8.2 MB, SSE4.1 1.8 -> 4.9 MB; clang-cl's 1.0 -> 5.3 and 0.8 -> 2.5 MB. The four build in
  about 30 s. MSVC's test binary with link-time code generation: the 1-float lengths add 0.22 MB to 1.72. The Pi's test binary: GCC 1.11 -> 2.58 MB, Clang 1.12 -> 2.09 MB; of that the 1-float lengths 0.40 and
  0.28 MB, NEON's 4-float lengths from 24 to 32 0.27 and 0.21 MB.
- MSVC at SSE4.1 fences each written-out block (`std::atomic_signal_fence` in `filterBlocksFrom`). Without it MSVC
  moves all of a column's weight broadcasts ahead of its products, and row A's run ahead of row B's. With it each
  block's broadcasts sit beside both rows' products, as in the count-read loop. clang-cl orders the blocks so already.
- A template parameter, not a flag: carried through the kernel, the choice of pass cost MSVC a stack reload per column
  in the general one: 4K -> 1080p Grayscale8 +3% to +6%, 8K -> 4K and 4K -> 1080p RGB32 +2% to +4% (the 8500T's log).
- Inlined, not a function of its own: split out, the general pass lost up to 22% (experiment 28). A split pass that
  takes the source buffers by reference keeps the caller's in memory under MSVC, a load and a store per column.
- Output bytes can differ from the unpadded runs' in the last bit: a tap lands in another accumulation chain.

The column loop, RGBA32, row pair, instructions and stack references. With the count read, MSVC's AVX2 column loop is
105 instructions and 2 stack references around a 21-instruction block loop. AVX2: the interleaved column of the next
section, the kernel's form in brackets.

| Taps | MSVC, AVX2 | clang-cl, AVX2 | MSVC, SSE4.1 | clang-cl, SSE4.1 |
|---|---|---|---|---|
| 4 | 21, 0 (25) | 21, 0 (23) | 42, 0 | 38, 0 |
| 8 | 33, 0 (39) | 33, 0 (35) | 74, 0 | 66, 0 |
| 12 | 42, 0 (46) | 42, 0 (42) | 106, 0 (108, 5 unfenced) | 94, 0 |
| 16 | 51, 0 (52) | 51, 0 (48) | - | - |
| 20 | 60, 0 (59) | 60, 0 (55) | - | - |

Against the count read and the separate four-tap pass at 0cfe29f: the median of same-round ratios, three rounds of
both builds alternated, the default level. The row's x run lengths in brackets.

| Row | 8500T, MSVC | 8500T, clang-cl | E-core, MSVC | E-core, clang-cl | Pi, GCC | Pi, Clang |
|---|---:|---:|---:|---:|---:|---:|
| 2.8 MP -> 1920x1280 (6-7) | -9.3% | -9.4% | -3.8% | +0.9% | -8.6% | -11.0% |
| 4K -> 1080p RGB32 (12) | -5.2% | -3.2% | -3.3% | -2.8% | -8.7% | -4.8% |
| 4K -> 1080p RGBA32 (12) | -3.1% | -8.4% | -0.7% | -3.0% | -8.5% | -6.2% |
| 4K -> 1080p RGB24 (12) | -4.6% | -6.7% | -2.8% | -1.1% | -9.8% | -4.7% |
| 4K -> 1600x900 (14-15) | +0.1% | -0.6% | +1.2% | -2.2% | -9.7% | -4.4% |
| 4K -> 720p (17) | -3.0% | -7.9% | -2.5% | -6.1% | -6.4% | -5.9% |

SSE4.1, where the 14-15 and 17 tap rows keep the count read:

| Row | 8500T, MSVC | 8500T, clang-cl | E-core, MSVC | E-core, clang-cl |
|---|---:|---:|---:|---:|
| 2.8 MP -> 1920x1280 (6-7) | -7.8% | -5.9% | -9.8% | -7.4% |
| 4K -> 1080p RGB32 (12) | -9.1% | -13.6% | -16.2% | -13.9% |
| 4K -> 1080p RGBA32 (12) | -9.5% | -12.2% | -12.4% | -15.1% |
| 4K -> 1080p RGB24 (12) | -8.5% | -13.0% | -13.4% | -11.3% |

- Rows that keep the count read moved within 1-2%: Grayscale8 2x, the thumbnails, 24 MP -> 1080p, 4K -> 64x64,
  101 MP -> 720p, the SSE4.1 14-15 and 17 tap rows.
- Upscales within noise. clang-cl's 720p -> 4K RGBA32 and RGB32 read +6% and +8.5% on the 8500T: per process the row
  takes 14.4 or 15.3-15.9 ms in either build (open lead 24). Run alone, the fixed column loop takes 9% fewer
  instructions and cycles, and the kernel 1% less time.
- Padding against the gain: the 6-7 tap row pads about 60% of its runs and gains, the 14-15 tap row pads 40% and gains
  nothing on x64. The Pi gains on both.
- The 12600K's P-core is left out: in that session rows with unchanged code read +3% to +22%.
- Pi, GCC, 720p -> 4K RGB32 +2%: not the column loop, which is the same 39 instructions in both builds, the pair's rows
  written out though the 16-float step loops over them.

Scratch run-length rows, RGB32 from 3840x2160, against the count read, without the fence. SSE4.1 past 12 taps is a
build with the limit at 20:

| Level, taps | 8500T, MSVC | 8500T, clang-cl | E-core, MSVC | E-core, clang-cl | N4100, MSVC | N4100, clang-cl |
|---|---:|---:|---:|---:|---:|---:|
| AVX2, 9 | -6.8% | -10.0% | +0.2% | -0.7% | | |
| AVX2, 10-11 | -2.3% | -5.9% | -6.1% | -1.2% | | |
| SSE4.1, 9 | -11.9% | -14.1% | -8.8% | -14.7% | +3.0% | -3.0% |
| SSE4.1, 10-11 | -6.6% | -10.5% | -13.1% | -9.3% | +9.2% | -3.9% |
| SSE4.1, 14-15, limit 20 | -2.5% | -8.0% | -11.3% | -7.6% | +8.2% | |
| SSE4.1, 17, limit 20 | -2.8% | -13.5% | -13.6% | -10.1% | +2.0% | |

- The N4100's column loop is at its multiply floor either way (VTune): a 4-float multiply per 2.24-2.25 cycles with
  the count read, against 2.0 at the ceiling ([throughput-ceiling.md](throughput-ceiling.md), 48 taps). The fixed loop
  retires 24% fewer instructions: the count read's extra ones ran beside the multiplies.
- Cycles per multiply at 9, 10-11 and 12 taps: MSVC unfenced 2.41, 2.44, 2.10; fenced 2.35, 2.18, 2.06; clang-cl 2.12,
  1.97, 1.99.

The fence against the unfenced build, MSVC. The N4100 two rounds unthrottled, min to min; the others three rounds,
median:

| Taps | N4100, SSE4.1 | 8500T, SSE4.1 | E-core, SSE4.1 | 8500T, AVX2 | E-core, AVX2 |
|---|---:|---:|---:|---:|---:|
| 9 | +0.4% | -1.4% | 0.0% | -2.2% | -3.4% |
| 10-11 | -6.4% | -4.2% | -0.1% | -0.3% | +1.9% |
| 12, RGB32 | +1.6% | -3.5% | -0.2% | +1.9% | -0.3% |

- The N4100's column loop in cycles (VTune): -2%, -10%, -2%. Untouched rows moved within 1% at SSE4.1 on x64, 2.5-3%
  at AVX2, 4% on the N4100.
- clang-cl fenced: within 2.6% everywhere, the 8500T's 12-tap SSE4.1 rows 0.4-2% slower.

NEON, a build with the limit at 32, the Pi, against the count read:

| Taps | 9 | 10-11 | 14-15 | 17 | 18-20 | 20-21 | 22-23 | 24 | 25-26 | 27-28 | 29-30 | 32 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GCC 14.2 | -10% | -13% | -14% | -6% | -9% | -10% | -6% | -3% | -7% | -8% | -10% | -3% |
| Clang 22 | -5% | -7% | -6% | -4% | -3% | -4% | -6% | -6% | -6% | -6% | -5% | -3% |

- Whole blocks, 24 and 32 taps, gain least under GCC: the count read has no step to test there either.
- A build with the limit at 24 agreed within 1-4 points up to 24 taps.

Grayscale8, the same scratch rows, against the count read. x64: a build with the limit at 20, unfenced:

| Level, taps | 8500T, MSVC | 8500T, clang-cl | E-core, MSVC | E-core, clang-cl | N4100, MSVC |
|---|---:|---:|---:|---:|---:|
| AVX2, 6-7 | -20.1% | -21.1% | -26.4% | -34.1% | |
| AVX2, 9 | -30.0% | -29.2% | -28.3% | -31.7% | |
| AVX2, 12 | -22.1% | -21.2% | -20.7% | -20.0% | |
| AVX2, 17 | -25.5% | -25.6% | -27.9% | -38.2% | |
| SSE4.1, 6-7 | -25.5% | -27.8% | -27.4% | -28.3% | -20.6% |
| SSE4.1, 9 | -35.7% | -23.4% | -34.1% | -33.9% | -24.5% |
| SSE4.1, 12 | -35.5% | -15.7% | -33.4% | -22.8% | -16.9% |
| SSE4.1, 17, limit 20 | -28.2% | -29.5% | -32.0% | -29.5% | -16.7% |

| NEON, the Pi, taps | 6-7 | 9 | 12 | 17 | 18-20 | 20-21 | 22-23 | 24 | 25-26 | 27-28 | 29-30 | 32 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| GCC 14.2 | -22% | -14% | -10% | -11% | -19% | -12% | -8% | -13% | -7% | -18% | -8% | -13% |
| Clang 22 | -23% | -17% | -8% | -15% | -22% | -18% | -10% | -15% | -7% | -20% | -8% | -8% |

- A 1-float run shorter than a block, 32 taps at AVX2 and NEON and 16 at SSE4.1, is all steps: the fixed form drops
  their tests.
- The gain swings with the length at NEON, 7% at 25-26 and 18-20% at 27-28 taps: unexamined.

The four-tap pass against the count read, when it came in:

| Machine, level | Compiler | Colour upscales | 720p -> 4K Grayscale8 |
|---|---|---:|---:|
| 8500T, AVX2 | MSVC | -7% to -12% | -10% |
| 8500T, AVX2 | clang-cl | -8% to -14% | -10% |
| N4100, SSE4.1 | MSVC | -4% to -8% | -6% |
| N4100, SSE4.1 | clang-cl | -6% to -10% | -5% |
| Pi, NEON | GCC | -3% to -10% | -8% |
| Pi, NEON | Clang | -6% to -11% | -11% |

- With threads: -6% to -9% under MSVC, -6% to -11% under clang-cl; on the Pi -4% to +7% under GCC, -5% to -7% under Clang.
- The N4100's rows are the flag form's.
- Padding every x run to a multiple of 4 taps, with the count still read, gains nothing and costs the thumbnail 3-5%
  (experiment 22).
- That pass named a pair's rows: with a loop over them GCC left the loop rolled at NEON, the row pointers reloaded from
  the stack per row, and its 3-channel upscales lost 6-7% where Clang's gained.

### Row pairs interleaved at AVX2, at padded lengths

Where the x runs are padded, a row pair of 4-float pixels is converted into one buffer, the rows' pixels alternating:
a `Floats8` holds one pixel of each row. A tap is one multiply-add of that pair by the tap's weight, picked from 4
loaded weights by an in-lane shuffle; the sum's halves are the two rows' temp pixels. The kernel's form spreads a
weight over two pixels of one row with a cross-lane permute, and sums each row's halves at the end.

- AVX2 only (`interleavedRowPairs`): at 128 bits a register is one pixel either way, and the micro-benchmark found the
  form within 3% ([throughput-ceiling.md](throughput-ceiling.md)). Every level has the pair primitives: GCC and Clang
  look up the outline's non-dependent names in discarded branches too.
- The conversion unpacks the two rows' 16 loaded bytes into pixel pairs, then widens them (`convertPixelPairsToFloats`).
- The column is written out per tap in 4 chains, 1 at 4 taps: 4 chains there take 4 multiplies and 3 adds.
- The odd last row is filtered as a pair with itself.
- Runs read per run keep the kernel's form: interleaved, the big cores lose up to 36% on long runs (experiment 30).

Against 963e6bc, the median of same-round ratios: the 8500T 10 rounds, the 12600K's P-core 10 rounds of MSVC, its
E-core 6 rounds. The rows the change does not reach (Grayscale8, native size, runs read per run) moved within 1% on the
8500T and about 3% on the 12600K, minimum to minimum.

| Row | 8500T, MSVC | 8500T, clang-cl | P-core, MSVC | E-core, MSVC | E-core, clang-cl |
|---|---:|---:|---:|---:|---:|
| 2.8 MP -> 1920x1280 (6-7) | -6.5% | -0.7% | -1.8% | -12.1% | -11.9% |
| 4K -> 1080p RGB32 (12) | -3.9% | -0.9% | -4.3% | -20.7% | -19.7% |
| 4K -> 1080p RGBA32 (12) | -2.0% | -0.6% | -2.7% | -17.6% | -18.9% |
| 4K -> 1080p RGB24 (12) | -1.7% | -0.2% | -2.9% | -20.2% | -20.2% |
| 4K -> 1600x900 (14-15) | -4.8% | -6.7% | -9.9% | -22.8% | -20.3% |
| 4K -> 720p (17) | -0.5% | -1.2% | -11.9% | -13.4% | -18.9% |
| 720p -> 4K RGB32 (4) | -2.5% | -1.9% | -3.6% | -11.5% | -10.5% |
| 720p -> 4K RGBA32 (4) | +1.7% | +1.3% | +1.4% | -6.3% | -4.7% |
| 720p -> 4K RGB24 (4) | -1.2% | -3.8% | -2.0% | -12.1% | -12.6% |

- The 8500T gains least: it runs every shuffle on one port, and the form trades half a cross-lane permute per tap for a
  whole in-lane shuffle. 720p -> 4K RGBA32 reads -1.5% minimum to minimum there, +1.7% by the median.
- In the micro-benchmark the E-core took 2.8-3.2x the P-core's cycles for the kernel's form, 2.1-2.2x for this one.
- In the P-core's 7-round clang-cl series the padded rows read -8% to +1% minimum to minimum; its medians spread too
  wide to read.

### 3-channel temp pixels stored whole, and the interleaved column's pixel pairs stepped

- A 3-channel temp pixel is one 16-byte store, its fourth float the next pixel's first: the element stores and the
  extract cost the 8500T's column loop more than any other part (experiment 35).
- The temp rows end in 16 floats of slack, a cache line: with 1, the strides lose their 64-byte alignment and the
  vertical pass's two loops took 48% more cycles on the 8500T, the column loop's gain undone.
- The interleaved column steps its pixel-pair pointer from run to run (`keepInRegister` hides it from Clang and GCC):
  from an origin, each compiler folds the offset into every load as an index, and Skylake splits an indexed
  multiply-add in two uops. Alone it gained nothing (experiment 34); with the whole stores the 8500T's column loop took
  16.4 cycles per column pair against 17.6 without it, 19.0 before both.

Against 594a17b, the median of same-round ratios: the 8500T 12 rounds, the 12600K's cores 7 rounds each at once, the
Pi 6 rounds of GCC and 12 of Clang. Minimum to minimum agrees within about 1 point on the rows below unless noted.

| Row | 8500T, MSVC | 8500T, clang-cl | P-core, MSVC | P-core, clang-cl | E-core, MSVC | E-core, clang-cl | Pi, GCC | Pi, Clang |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 2.8 MP -> 1920x1280 | -6.7% | -9.4% | -5.6% | -8.4% | -2.5% | -2.0% | -9.1% | -1.9% |
| 4K -> 1080p RGB32 | -6.5% | -5.0% | -3.1% | -7.0% | -2.0% | -2.1% | -8.9% | -3.1% |
| 4K -> 1080p RGB24 | -6.3% | -5.1% | -3.4% | -7.8% | -2.2% | -2.5% | -8.0% | -2.5% |
| 4K -> 1080p RGBA32 | -2.1% | -0.6% | +1.0% | -1.6% | +0.7% | +1.4% | -0.5% | +0.3% |
| 4K -> 1600x900 | -10.6% | -7.9% | -3.7% | -6.5% | -4.1% | -2.7% | -6.5% | -0.6% |
| 4K -> 720p | -11.0% | -8.6% | -4.8% | -6.3% | -4.6% | -2.7% | -7.6% | -0.4% |
| 720p -> 4K RGB32 | -5.8% | -12.1% | +0.2% | -0.4% | -0.1% | +0.9% | -5.6% | +2.5% |
| 720p -> 4K RGBA32 | -0.1% | -3.9% | -0.3% | +0.7% | +1.6% | +1.3% | +0.5% | +1.6% |
| 720p -> 4K RGB24 | -3.2% | -1.1% | -1.0% | +3.2% | -0.2% | +0.5% | -3.2% | +1.4% |

- RGBA32 has only the stepped pointer. The E-core loses 0.7-1.6% on it, every round of both compilers.
- At NEON only the whole stores apply. Clang's RGBA32 rows move as much as its RGB32 upscale, without a change to
  their code.
- 8500T clang-cl 720p -> 4K: RGB32 -9.9% and RGB24 -6.2% minimum to minimum. P-core clang-cl 720p -> 4K RGB24: +2.2%.
- The N4100 at SSE4.1, the pixel-layout rows, MSVC 8 rounds / clang-cl 4: 4K -> 1080p RGB32 -5.8% / -1.9%, RGB24
  -5.7% / -6.4% (-2.6% minimum to minimum), 720p -> 4K RGB24 -5.6% to -7.7% / -2.4%; RGBA32 and Grayscale8 within 1.5%.

### Two chains at SSE4.1, and a first block that assigns

A `Floats8` is two XMM registers at SSE4.1, so a row pair's four chains were 16 accumulators, x64's whole register file:
MSVC kept eight on the stack. A 12-tap run, a 2x Lanczos3 downscale, ran the four-chain block once: its chains never
overlapped, and their zeroing and reduction were paid per column.

- Each primitives header states its level's chain count: 2 at SSE4.1, 4 at AVX2 and NEON.
- The accumulators are updated in place, by `addProduct`. Written `chain = mulAdd(a, b, chain)`, the sum comes back as
  a pair of registers, and MSVC does not always merge that pair with the accumulator: the row pair's 8-tap loop then
  computes each sum in a second register, copies it back, and keeps two accumulators on the stack (47 instructions
  for 39). Which kernels got that form changed with unrelated code elsewhere in the kernel: RGBA32 and Grayscale8 had
  it, and a change to the vertical pass gave it to the 3-channel kernels, at +15% to +20% on their downscales.
- In place: MSVC's 4K -> 1080p RGBA32 -10% on the i3-2310M and -7% on the N4100, threaded rows included. Nothing
  else moves, at any level or under clang-cl, GCC or Clang.
- A run's first block starts the chains with its products: no zeroed accumulators, no add to zero. With FMA the result
  is bit-identical.
- N4100, MSVC: opaque 4-byte and RGB24 downscales -7% to -14% (4K -> 1080p RGB32 115.6 -> 102.1 ms), 4K -> 64x64 -2%,
  upscales within 3%.
- Two chains without the assigned first block cost 1080p -> 1440p 8%: a 4-tap run then pays a block's zeroing and reduction.
- One row per sweep, the other way to fit the registers, loses 9-41%: paired rows share the column's lookup, weights and loop.
- The chains are separate variables, and the block's helper takes the two rows' by name. Held in an array, or indexed by
  row, MSVC moved one row's chains through a second register every block at SSE4.1: 101 MP -> 720p 907 ms against 851.
- CI, EPYC 7763: two chains take 10-17% off the SSE4.1 downscales under Clang and GCC as well. The assigned first block
  takes 2-8% off the AVX2 downscales under Clang and GCC, and 4-9% off GCC's on Neoverse-N2; Clang there is within 3%.

### Runs a period apart share their weights

Dest coordinates `dstSize / gcd(srcSize, dstSize)` apart sample the source at the same sub-pixel phase, a whole number of
pixels apart. Such a run takes the earlier run's weights with its source position shifted, unless border clamping
touched either window.

- An exact-ratio scale's weights then stay in L1 through a row sweep: a 2:1 downscale has one run's weights where it had
  92 KB for 1920 columns, swept out of L2 once per row pair.
- The builder evaluates the kernel for one period and the borders, which shortens the serial part of a threaded resize.
- A shared run's weights are those computed for the earlier coordinate: the same in exact arithmetic, a rounding step
  apart in double.
- N4100, ms: 4K -> 1080p RGB32 102.3 -> 94.5, Grayscale8 59.2 -> 52.6, 24 MP -> 1080p 231.3 -> 215.0, 4K -> 64x64
  68.9 -> 61.8. With threads: 24 MP -> 1080p 68.0 -> 59.9, 1080p -> 240p 7.02 -> 5.71. Upscales -1% to -3%.
- 101 MP -> 720p does not gain: its period is 160 columns, 35 KB of weights against a 24 KB L1.
- CI: downscales -5% to -10% on Neoverse-N2 under GCC and Clang, threaded ones up to -17%; Clang's AVX2 downscales on
  the EPYC 7763 -4% to -10%.

### The weight spread's indices hidden from Clang

`WeightSpreader` at AVX2 is one `vpermps` per chain. Clang, knowing the indices, emits `vshufps` + `vpermpd` for it: 12
shuffles per column of a 12-tap run where MSVC has 6, all on the one port that runs shuffles. `spreadIndices`
passes the indices through an empty inline-assembly statement under Clang, which then holds them in registers and emits
the `vpermps`.

- 8500T, clang-cl, 8 rounds: 101 MP -> 720p -8%, 4K -> 64x64 -8% to -11%, 1080p -> 240p -7%, 24 MP -> 1080p -4% to -6%,
  4K -> 1080p RGB32 -5% in one section and level in the other, threaded rows -1% to -8%. Grayscale8 has no spread.
- MSVC's kernels keep their instruction sequence. Clang on Linux and the 2-float spread are untimed.

### The conversion's tail loop kept scalar under Clang

`convertPixelsToFloats` ends in a scalar loop over the few pixels its 16-byte blocks leave. Clang vectorizes it:
hundreds of instructions per kernel for fewer pixels than one block holds. At AVX2 that code's four shuffle constants
also stay in registers through the strip's loop, where the column loop's chains evict them: four reloads per column.
`IMAGE_PROCESSING_SCALAR_LOOP` keeps the loop scalar under Clang. MSVC's and GCC's kernels are unchanged.

| Clang build | Kernels' instructions | Result, 8 rounds |
|---|---|---|
| clang-cl AVX2, 8500T | 12-30% fewer per kernel | RGB32 downscales -1.4% to -2.9%, 4K -> 1080p Grayscale8 -1% to -3%, 720p -> 4K Grayscale8 +2.5% to +3.5% |
| clang-cl SSE4.1, 8500T | | 4K -> 1080p RGB24 -4%, Grayscale8 -1% to -2.5%, the rest within 1% |
| Clang NEON, Pi | 29184 -> 19660 over all kernels | RGB32 downscales -1.6% to -5.7%, the rest within 1%, 720p -> 4K RGB24 +1% to +2% |

- With the spread's indices hidden as well, clang-cl at AVX2 is level with MSVC on the RGB32 downscales.
- A layout without a fixed pixel stride converts every pixel in this loop, now scalar under Clang: unmeasured.
- The Grayscale8 upscale's loss is open lead 14.

### The conversion's block loop steps pointers to an end pointer

`convertPixelsToFloats`' block loop steps a source and a destination pointer and ends at a pointer computed before it.
With a pixel index and a bound of `pixel + loadedPixels <= pixelCount` MSVC keeps a second counter, doubles a copy of
the index for the stores and indexes them: 15 instructions per 16 bytes of RGB32 at AVX2, 12 now. GCC compiles the
indexed form to stepped pointers by itself; its kernels and Clang's are untimed.

| Core, MSVC | Result |
|---|---|
| 8500T, 8 rounds | 4K -> 1080p RGB32 -3.4% to -4.1%, 24 MP -> 1080p -2.9%, 1080p -> 240p -2.1%, 101 MP -> 720p -1%; RGBA32, RGB24, Grayscale8 and the upscales within 1%; 1080p at native size +2% to +3% |
| 12600K P-core, five placements of the code after the loop | 4K -> 1080p RGB32 -4% to -5% with no overlap, RGB24 -2%; Grayscale8 and 24 MP -> 1080p level |
| 12600K E-core | The conversion loops' own time -16% on 4K -> 1080p Grayscale8 (VTune); whole rows move by 10% either way with where the column loop lies (open lead 22) |

- Grayscale8's loop had 13 instructions: one fewer now.
- RGBA32's loop premultiplies, and RGB24's shuffles its bytes first: neither gained on the 8500T.

### Output bytes packed without a clamp

The vertical pass capped its sums at 255 before converting them: `packus_epi16` reads its input as signed, so a word past
32767 would have packed to 0. The first pack is now the signed one, `packs_epi32`, which carries anything below 0 or past
255 to `packus_epi16` as such. NEON's narrowing instructions saturate both ways and needed no cap.

- One `min` fewer per 4 floats, at every level. The output is unchanged.
- N4100: upscales -6% to -8% under MSVC, -2% to -6% under clang-cl. Pi: upscales -2% to -8% under GCC and Clang.
- Pi, GCC: 4K -> 64x64 up to 6% slower and 101 MP -> 720p 1.5%. The NEON block for two rows fills the register file,
  and without the clamp's constant GCC's allocation spills one pixel vector in the block loop (open lead 12).

### Two vertical blocks per step at AVX2 (b8bdd30)

A vertical block's taps accumulate in one register per 8 floats: 3-4 registers at AVX2, 6-8 at SSE4.1 and NEON. At AVX2
the pass filters two adjacent blocks per step (`verticalBlocksPerStep`), so each tap's weight load, zero test and
broadcast serve 16 pixels.

- 8500T: upscales -5% to -10% under MSVC and up to -14% under clang-cl, downscales -1% to -6% and up to -10%. In WSL on
  it: RGB upscales -6% to -11% under GCC, -7% to -18% under Clang.
- SSE4.1 and NEON keep one block. Their kernels came out the same size under GCC and Clang, and within a few
  instructions under MSVC.
- GCC's RGBA32 upscale does not gain (the 8500T's log).

### The vertical taps: listed on x64, walked at NEON, the first tap assigned

Two changes to the vertical tap loop, measured apart:

- **The first tap starts the accumulators**, in place of zeroing them and adding it. Without FMA that saves an add per
  accumulator, a quarter of a bicubic upscale's accumulation: SSE4.1 upscales -3% to -6%. With FMA the product costs
  what the multiply-add did: nothing at AVX2.
- **A list of the dest row's nonzero taps**, each one's temp row pointer and weight, built once per dest row
  (`ListedTaps`): the tap loop loses its zero test and the walk over the ring's two segments, a third of the vertical
  pass's instructions at AVX2. It costs one load per tap.

Each primitives header states its level's form in `verticalTapsListed`. Both forms assign the first tap.

| Level, machine | List, first tap assigned | First tap assigned, no list |
|---|---|---|
| AVX2, 8500T | MSVC upscales -1% to -4%; clang-cl -2% to -4% | Nothing under MSVC; clang-cl upscales -1% to -5% |
| SSE4.1, i3-2310M | MSVC upscales -5% to -13%, clang-cl -5% to -7%; downscales level | MSVC -2% to -6%, clang-cl -4% to -6% |
| SSE4.1, N4100 | Upscales -2% to -8%; downscales +2% to +4% | Upscales -3% to -6%; downscales level |
| NEON, Pi | Clang +3% to +7% on every row; GCC upscales to -6%, downscales +1% to +3% | Clang upscales -2% to -5%; GCC +3% and +5.5% on the RGB32 and RGB24 upscales |

- The list is taken at both x64 levels and not at NEON.
- The list's load per tap is what the narrow cores pay. On Cortex-A72 the committed tap loop already runs 7 loads for
  6 multiply-adds per tap, and a list makes it 8. On the N4100 the list removes 31% of an upscale's vertical
  instructions for the time that assigning the first tap gets by removing 11% (VTune).
- The stored weight is a `float`, broadcast in the loop. Stored already broadcast, as 16 or 32 bytes: the same time at
  AVX2 and on the i3-2310M, worse on the N4100 and the Pi at 32 bytes.
- Clang at NEON turns a `float` weight's load and broadcast into one `ld1r`, an operation more on the FP pipelines per
  tap; GCC keeps the scalar load and multiplies by its lane. Clang's `float` list is the worse of its two for that.
- The restructured code matches the experiments' inline form on the 8500T, the i3-2310M and the Pi.

### The horizontal filter takes the source buffers' range by value

`filterHorizontalRowGroup` prepares the source buffers for one column, then `filterBufferedRuns` filters every following
column whose run lies in them. The buffers' pixel range reaches that loop as arguments: read through
`SlidingSourceFloats` in a loop that also slides and converts, it was live state at every column. The same reason as for
the runs' array pointer above.

| Machine, level | Compiler | 3- and 4-channel 4K downscales | Upscales |
|---|---|---|---|
| 8500T, AVX2 | MSVC | -6% to -9% | -2% to -6% |
| 8500T, AVX2 | clang-cl | -5% to -7% | -3% to -7% |
| 8500T, AVX2 (WSL) | Clang | -6% to -9% | -7% to -9% |
| 8500T, AVX2 (WSL) | GCC | 0% to -3% | -2% to -4% |
| PC, AVX2 | MSVC | -3% to +2% | -2% to -6% |
| N4100, SSE4.1 | MSVC | -4% to -5% | -4% to -5% |
| N4100, SSE4.1 | clang-cl | -2% to -8% | -5% to -6% |

- 4K -> 1080p Grayscale8: -5% to -12% on every row above.
- Unchanged: 4K -> 64x64, and Grayscale8 upscales, which take `filterHorizontalFourTapRunGroups` (see its section).
- The range in registers is the gain, not fewer checks: the loop still compares each run against it. A form that
  found each batch's end ahead and looped with no check gained half as much and cost clang-cl 4-7% on some rows.
- NEON: 0% to -4% on single-threaded downscales, and only with the output blocks stored by `STNP` (next section).

### NEON stores its output blocks with `STNP`

On Cortex-A72 a destination written once bypasses L2 when the core detects the stream of stores, and whether it does
depends on the build: the same store instructions keep the bypass in one build and lose it in another. GCC's build of
the batched horizontal loop lost it (threaded upscales +33% to +72%); Clang's baseline lacked it on RGB24 upscales.
`STNP` carries the non-temporal hint explicitly, as inline assembly: no intrinsic emits it.

- Every store of a block must carry it. RGB24's 24 bytes with 16 hinted and 8 plain: threaded 720p -> 4K RGB24 +87%
  under GCC. All 24 hinted: +12%.
- `BlockTarget` tells the block writers where the bytes go: the stack buffer of the odd-stride path takes plain stores.
- Against the per-column loop with plain stores (the Pi's log): threaded upscales of 32-byte layouts +5% to +6% under
  GCC and +3% to +8% under Clang, the price of not depending on detection; threaded 720p -> 4K RGB24 +12% under GCC,
  -17% under Clang; single-threaded rows within 3.5%, 4K -> 1080p Grayscale8 -3% to -7%.
- Neoverse N2 pays nothing for it: threaded upscales within 2.5% under GCC and Clang (its log). The M1 runner shows
  no collapse and is too noisy for more.

### The horizontal passes' shape is part of their tuning

`filterHorizontalRowGroup` writes each step once, in a loop over the rows, with three exceptions; two of its neighbours
carry one more each. Each answers one compiler, and each is commented where it stands:

| Compiler | Behaviour | In the code | Cost without it |
|---|---|---|---|
| MSVC | Moves chains held in an array, or indexed by row, through a second register every block | `RowChains` with named fields; the block helper takes the two rows by name | SSE4.1 downscales +3% to +8% on the N4100 |
| GCC | Leaves a loop over the rows rolled when its body holds a loop, with the rows' state in memory | `storeTempPixelWithTaps` called per row | 4K -> 1080p Grayscale8 +31% to +50% on the Pi, Neoverse-N2 and the EPYC 7763 |
| GCC | Places a step whose body is a loop over the rows out of line | `[[likely]]` on the three leftover-tap steps | 4K -> 1080p Grayscale8 +20% to +26% at AVX2 on the EPYC 7763 and 9V45; nothing on Golden Cove |
| MSVC | Takes a small array of sums through the stack: stored as a vector, reloaded as a scalar, reloaded again to copy out | `storeTempPixelWithTaps` holds the sums as scalars taken out of the vector | 4K -> 1080p Grayscale8 +11% on the N4100 |
| MSVC | Leaves a four-iteration loop with a large body rolled, its results in stack arrays | `filterHorizontalFourTapRunGroups` multiplies each column by name, into named products | 720p -> 4K Grayscale8 +7% on the N4100; clang-cl +4%; GCC on the Pi up to +27% |

- The last two were found by reading MSVC's listing of the one-channel kernels beside clang-cl's.
- SSE4.1 conversion: each widening loads its own 4 bytes. MSVC otherwise copied and shifted one 16-byte load three times:
  4-byte downscales -3% to -5% on the N4100 under MSVC, nothing under clang-cl.

- A change to this function is compared by listing under MSVC, GCC and Clang against the commit before it.
- The 3- and 4-channel kernels under GCC carry about 3% more instructions with the row loops than without. No row shows it.

### Column strips (b9fea7e)

Each thread fills its ring for one column strip of the destination at a time. `stripWidthFor` bounds a ring to half the
smallest L2 share per logical processor, detected at runtime; an undetected L2 counts as the Pi's. The SIMD path converts
only the strip's source span (29b97e9).

| Machine | L2 share per logical processor | Ring budget |
|---|---:|---:|
| Pi 4 | 256 KB (1 MB, four cores) | 128 KB |
| PC | 512 KB (E-core cluster: 2 MB, four cores) | 256 KB |
| N4100 | 1024 KB (4 MB, four cores) | 512 KB |

The tests pass the Pi's budget on every machine, so they cover the same strip layouts everywhere.

Full-width rings overflowed the Pi's shared L2 with four threads: evicted dirty ring rows are written out to DRAM and
read back.
- Pi, ms with threads: 24 MP -> 1080p 236.8 -> 136.4, 4K -> 1080p RGBA32 167.3 -> 69.9.
- Both paths scale 3.1-3.6x on the Pi's four cores; before strips the SIMD path scaled 1.3-3.1x.
- Single-threaded the Pi is about neutral.
- Where full-width rings already fit, only the overhead shows: SIMD +1-5% single-threaded on Neoverse-N2, 3-5% on the
  PC's downscales.

Without the pre-touch below, strips cost x64 Windows single-threaded, the SIMD upscales most: 720p -> 4K +53-62% on the
EPYC 7763 under MSVC and clang-cl, +43-46% on the PC. The cost is Windows' demand-zero page faults on a fresh destination:
- One fault per page with and without strips, but about 1.1-1.2 us each when strips first write the pages out of address
  order, against 0.45 us in order.
- Finishing each page while cached does not help: see experiment 8.
- `touchDestPagesInOrder` writes one byte per page in address order before a band's strips run.
- Linux on the same CPU model shows no cost; why is unmeasured.
- PC, 720p -> 4K RGBA32, ms: 13.65 before strips, 19.59 with them, 13.93 with the pre-touch.
- Pre-touching a reused destination costs nothing measurable, with or without threads.

A strip is planned at least 64 columns wide, whatever the budget: a tall y window otherwise cuts a narrow destination
into strips whose per-strip costs exceed those of the larger ring.
- 4K -> 64x64, three strips of its 64 columns against one: 7-9% slower under MSVC on the 8500T, 6-7% under GCC on the
  Pi, with threads too; no difference under Clang on the Pi.
- The ring is then 64 columns times the y window: 276 KB per thread for 4K -> 64x64, against a budget of 128 KB.

### Where the float buffers start

The source span is allocated on a 64-byte boundary. So is the temp ring at AVX2 and NEON; at SSE4.1 it starts 16 bytes
past one (`tempRingStartOffset`).

On the boundary: the AVX2 kernels load 32 bytes at a time from both buffers, and from a start 16 bytes off a 32-byte
boundary every other load spans two cache lines.

- Left to the heap, the starts took all four 16-byte offsets, by process and by allocation.
- Both starts 16 bytes off, against both on a boundary, in an experiment that placed them (the PC's and the 8500T's
  logs): the 8500T +3% to +12%, the 12600K's P-core +4% to +10% and +20% on 4K -> 64x64, its E-core within 2%.
- The heap's own starts cost the 8500T and the P-core 5-7% on 4K -> 1080p.

16 bytes past at SSE4.1 (the N4100's log): the same experiment on Goldmont Plus, in three sessions.

- The ring 16 bytes past a boundary, against on it: RGBA32 -5% to -7%, Grayscale8 -2% to -5%, the 3-channel upscales
  -2% to -3%, the 3-channel downscales up to -2%.
- The ring's start decides: the span's changes nothing.
- 16 is the best offset on every row. RGBA32 and Grayscale8 also gain from 32 and 48, less; the 3-channel layouts from
  48 and not from 32.
- 4K -> 1080p RGBA32 at SSE4.1 elsewhere, both starts 16 bytes past: -1% on the 12600K's two core types, nothing on
  the 8500T.

### Where the weights start

`AxisWeights::weights` is allocated on a 64-byte boundary, and every run starts at a multiple of 8 floats
(`AxisWeights::runAlignment`): the AVX2 horizontal kernels load 8 weights at offsets of 8 into a run, so no such load
spans two cache lines. A four-tap run takes 8 floats, a 12-tap run 16.

Before, in a `std::vector` with the runs back to back, 4K -> 1080p RGB32 (one 12-tap run shared by every column):

- One of the run's two loads spanned a line, by the heap's choice per process: the 4-tap step's or the 8-tap block's.
  The block's load heads the column's longest chain.
- On the 8500T the block's case took 10% more cycles in the column loop for the same instructions, with 984-1024 M
  split loads at that instruction and no other counter different (its log). After: none there.
- A process kept its level, and so did most launches of one binary in a row: it read as a state of the build.

Eight alternating rounds against cbbd718, the old form's slow and fast launches against the new form's range:

- 12600K E-core, 4K -> 1080p RGB32: 53.6-56.7 and 35.3-37.5 ms against 34.5-36.8, five launches of eight slow. RGB24
  and RGBA32 alike.
- 8500T: 4K -> 1080p RGBA32 27.7-27.9 and 25.9-26.6 against 25.8-26.4; the 4K image -> 1080p 24.1-24.2 and
  23.3-23.6 against 23.2-23.6. The 24 MP photo -4%, 101 MP -> 720p -4%, the thumbnail -2%, in every round.
- 12600K P-core: 4K -> 1080p RGB24, RGB32 and RGBA32 -3% to -4%, in every round but the old form's first.
- Level within the rounds' spread on all three: the upscales and 4K -> 1080p Grayscale8.
- SSE4.1 and NEON load 16 bytes at a time: no weight load there spanned a line before either. The larger table costs
  them nothing measured: the N4100 within 2% on every row in eight rounds; the Pi within 2% single-threaded in five,
  its multithreaded 1080p -> 1440p +5% with a spread as wide.

### `filterRun`'s last taps are steps, behind one test for none

After the 4-tap steps at most 3 taps remain (1 for 4-float pixels). They are explicit steps: two taps, then one. For 1-
and 2-float pixels a test for no taps left comes first, once for both rows.

- Steps, not loops: MSVC unrolls such a loop by four, code that at most 3 taps never enter. The column loop loses 22
  instructions for Grayscale8 and 32 for RGB32, and the 12600K E-core's predecode stalls on 4K -> 1080p Grayscale8
  fall from 23% of the loop's cycles to 4% (open lead 22).
- The test for none: under GCC at NEON the steps alone cost a 12-tap Grayscale8 column 73 instructions against the
  loops' 68. Each row's call tested its two steps, 4 branches where the two loops' zero-trip tests had merged into 1.
  GCC also keeps three pointers stepped through the tap steps for the last taps' paired loads; the test leaves that.

Eight alternating rounds against the loops, the weights aligned in both builds (five rounds on the Pi, against cbbd718):

| 4K -> 1080p Grayscale8 | Steps | Steps behind the test |
|---|---:|---:|
| 12600K E-core | -12.5% | -21% |
| 12600K P-core | -6% | -14% |
| 8500T | 0% | -9% |
| Pi, GCC | +4.4% | +0.3% |

- The other rows, with the test: the E-core's 4K -> 1080p RGB24 and RGB32 -3.5%, the 8500T's thumbnail -3% and 24 MP
  photo -2%; the rest within 1% on x64 and within 2% on the Pi, but its native size at +3%, a row that swings 9%.
- MSVC's code with taps left, per row: two pointers formed, a compare and branch per step. The test for none skips
  about 11 instructions and 4 branches of a 12-tap Grayscale8 column's 75.

### The general pass steps its run and temp pixels

`filterBufferedRuns` has no column index. Per column it steps a pointer into the run table and one temp pixel
pointer per row, and takes a run's pixels from row pointers biased to source pixel 0.

- With an index, each column formed the temp address from the index, the destination's start and the temp rows'
  bases (7 instructions, 3 of them loads from the stack under MSVC), and the pixel address from the buffer's start
  and the rows' bases: open lead 26.
- A 12-tap RGB32 column under MSVC: 72 instructions and 1 stack load, from 81 and 5.
- The biased pointers lie before the allocation where the buffer does not start at pixel 0. They are never read
  below the buffer's first pixel: the range check comes first. Without the bias MSVC forms the offset from the
  buffer's start twice: 78 instructions.

MSVC and clang-cl 22 alike, against d77c79c; eight rounds on the 8500T, five on the 12600K:

| Row | 8500T | 12600K P-core | 12600K E-core |
|---|---:|---:|---:|
| 4K -> 1080p RGB24, RGB32, RGBA32 | -5% | -2.4% to -5% | 0 to -2% |
| 4K -> 1080p Grayscale8 | -2% (MSVC), -4% (clang-cl) | -8% to -9% | -3% |
| 24 MP -> 1080p | -3% to -4.5% | -4% | level |
| 1080p -> 240p thumbnail | -2% | -3% to -6% | level |
| Upscales, 16x and 32x thumbnails, 4K -> 64x64 | within 2% | within 1.2% | within 1% |

- Pi, GCC, five rounds: within 2% on every single-threaded row.
- Nine instructions fewer of 81 gave 5%, not the 11% of "1.2% an instruction" (experiment 27): loads and address
  arithmetic cost less than branches.

### The upscale passes step their runs and temp pixels too

`filterBufferedFourTapRunGroups` steps its run and temp pixels as the general pass does, reads a run's fields from the
table entry, and tests its four runs' ranges with `||`. The 4-float rows below are of the separate four-tap pass, which
the general pass at a fixed length has since replaced.

AVX2, against 7a873c8; eight rounds on the 8500T, five on the 12600K (the one-channel row: six to eight everywhere):

| Row | Compiler | 8500T | P-core | E-core |
|---|---|---:|---:|---:|
| 720p -> 4K RGB32 | MSVC | -3.6% | -5.0% | level |
| 720p -> 4K RGB24 | MSVC | -3.1% | -3.6% | -1.7% |
| 1080p -> 1440p | MSVC | -3.8% | -5.2% | +1.6% |
| 720p -> 4K RGBA32 | MSVC | level | +0.9% | level |
| The four colour rows | clang-cl | -3.1% to -0.1% | -3.6% to -1.6% | -1.7% to 0 |
| 720p -> 4K Grayscale8 | MSVC | -11% | -6% | -8% |
| 720p -> 4K Grayscale8 | clang-cl | +0.6% to +3.5% | level | +5% |

- A run through `runFor` is a struct of the first pixel and a span. MSVC's SSE4.1 one-channel loop took the four
  apart into registers at 5e3c797 (55 instructions a group, 2 stack references) and built them on the stack at
  7a873c8 (80 and 17), a commit that changed only the general pass: 720p -> 4K Grayscale8 +17%, the colour upscales
  +9% to +12% (8500T). Read from the entry: 40 instructions, none on the stack. No hot loop goes through `runFor` now.
- SSE4.1, MSVC, 720p -> 4K Grayscale8 against 7a873c8: -22% on the 8500T, -21% on the P-core, -17.5% on the E-core.
  clang-cl: level, -4% on the E-core.
- SSE4.1, MSVC, 02510a2 (the one-channel pass's index kept) against 19bf5da, on the N4100, the 8500T and the 12600K's
  cores: 720p -> 4K Grayscale8 +12% to +18%; the colour upscales -4% to +1%; 4K -> 1080p Grayscale8 -7% to -13.5%;
  the RGB downscales -1% to -6%.
- `|` between the four range comparisons: MSVC sets a register per comparison and ORs them, 15 instructions; with
  `||`, four compares and branches, 8. Clang emits the branches for either. `||` with the index kept: MSVC -7%,
  -6% and -3% at AVX2, clang-cl level.
- clang-cl's AVX2 one-channel loop, 57 instructions a group from 53: its own code already stepped a run pointer,
  and the two temp pointers make it rotate three registers around the multiplies, six moves a group. The P-core's
  renamer drops the moves.
- The largest of the four offsets compared once: clang-cl +5% and +3% on the 8500T and the P-core.
- MSVC's 4-float column: 25 instructions from 26, and row B's base now loaded from the stack. The gain is larger
  than that count suggests, and RGBA32 has none. Not explained.
- Pi, GCC, five rounds: the 4-float pass, 720p -> 4K RGB32 and 1080p -> 1440p +2.5%; the one-channel pass,
  720p -> 4K Grayscale8 +1.7%; the rest within 1.5%.
- No row that does not run these passes moved by more than 2.6%.

### The block loop steps its pointers where a level has four chains

Past its first block, `filterRun` steps a weights pointer and a pixel pointer per row beside the tap counter at
AVX2 and NEON. At SSE4.1 it indexes the three by the counter.

- MSVC addresses an indexed loop's loads by base and scaled index, and keeps a second counter for the bound. On
  Skylake a multiply-add with such an operand is two micro-ops. Its AVX2 block loop: 21 instructions and no indexed
  operand, from 20 and 9. A 12-tap column, one block, does not enter the loop: one instruction shorter.
- At SSE4.1 a block is 4 taps, so a 12-tap run takes the loop twice. Stepped there, MSVC's column loop has 13
  instructions and 5 stack references more, and its plain loads gain nothing from the operand.
- Clang and GCC fold the stepped pointers back into the counter, so the loop hides the pixel pointers from them every
  block (`keepInRegister`). clang-cl's AVX2 row-pair loop then has no indexed operand: 19 instructions a block from 17
  with all 8 multiply-adds indexed for 1-float pixels, 21 from 19 with 4 indexed for 4-float ones.
- The counter stays: an end pointer computed ahead of the loop cost MSVC's short runs 4 instructions
  (experiment 27).
- The second row at a constant distance from the first, as Clang's code has it: MSVC masks the distance every
  block and keeps 4 operands indexed.

Against 1e18e27; six rounds on the 8500T, four on the 12600K, five on the Pi:

| Row, MSVC, AVX2 | 8500T | P-core | E-core |
|---|---:|---:|---:|
| 1080p -> 120 px wide thumbnail (16x) | -9.3% | -9.0% | level |
| 21 MP photo -> 175 px wide thumbnail (32x) | -11.6% | -2.8% | level |
| 4K -> 64x64 (60x) | -11.5% | -1.6% | level |
| 101 MP -> 720p | -1.8% | | |

- MSVC's other AVX2 rows within 2.5%. Its SSE4.1 rows, the same code: within 0.6% on the 8500T and the E-core, +1%
  to +3% on the P-core, that session's noise there.
- clang-cl, both levels: within 2.2%. Pi, GCC: within 2%.
- WSL 2 on the 8500T, six rounds. GCC 14.3 at AVX2 indexed the loop as MSVC did, 17 instructions a block and 9
  indexed operands; now 20 and 1: the 16x and 32x thumbnails -7% and -9%, 4K -> 64x64 -9%, the rest within 3%.
  Clang 22's AVX2 kernel is the same code but for two instructions' order: within 1.5%, 1080p -> 1440p +4%.
  Both compilers' SSE4.1 kernels are the same code: within 1.7%.
- MSVC's 32x thumbnail on the 8500T is at clang-cl's time: 26.5 ms against 26.4, from 29.9.
- Counters on the 8500T, the 32x thumbnail, MSVC: the block loop retires 1.25 slots an instruction indexed and 0.97
  stepped, 25 and 20 a block; 6.8 and 5.75 cycles a block, about a quarter of the slots plus half a cycle. The loop
  is bound by the 4 slots a cycle the core renames, and an indexed multiply-add takes two.

The pixel pointers hidden from Clang and GCC, against 560ca96; 12 rounds on the 8500T, eight on the 12600K, five on the
Pi. Under MSVC the barrier is empty.

| Row | 8500T, clang-cl | P-core, clang-cl | E-core, clang-cl | Pi, GCC | Pi, Clang |
|---|---:|---:|---:|---:|---:|
| 1080p -> 240p thumbnail | +0.7% / +0.2% | -2.2% / -1.0% | +0.1% / -0.4% | -3.5% / -1.9% | -0.1% / -1.8% |
| 1080p -> 120 px wide thumbnail (16x) | -2.1% / -3.0% | -6.3% / -7.2% | +0.1% / -0.2% | -4.5% / -4.5% | -0.2% / -1.0% |
| 21 MP photo -> 175 px wide thumbnail (32x) | -2.4% / -2.6% | -4.7% / -4.2% | +0.3% / -0.3% | -0.3% / -0.4% | -1.1% / -1.3% |
| 4K -> 64x64 (60x) | -2.5% / -2.3% | -4.9% / -4.7% | +0.1% / +0.1% | -0.6% / +0.1% | -6.9% / -3.3% |

- The 8500T's other rows within 1% by the minimums; the medians of its upscales vary up to 4.3%.
- The 12600K's other rows within 1.6% by the minimums, but for the P-core's 24 MP -> 1080p, -0.9% / -2.4%.
- The Pi's other rows within 2%, but for Clang's 720p -> 4K Grayscale8, +0.9% / +4.9%.

### A run holds its weights' address, and the x runs start in order

Two values the x64 column loops no longer hold (open lead 26: more live values than registers).

- `TapRun::weights` is an address, not an index into the array: no base to keep, no add per column. The builder
  works by index while the array grows (`IndexedAxisWeights`), and `withRunAddresses` converts at the end.
  `AxisWeights` is move-only for it.
- The x axis's runs never decrease in `firstSource`. Zero weights at a run's start are kept there: trimmed, a run at
  a whole-pixel phase started after the next one. `filterBufferedRuns` tests a run against the buffered range's end
  only, and `SlidingSourceFloats` slides to a run's first pixel with no room kept behind it.
- The y axis's runs are trimmed at both ends and may start out of order. NEON's vertical pass walks its window,
  zeros included: kept there, the zero was a second tap on every third row of a 3x upscale, and 720p -> 4K
  Grayscale8 read +3% on the Pi.

MSVC at AVX2: the RGB32 column loop loads nothing from the stack on a run under 16 taps (3 loads before) and is 3
instructions shorter; the one-channel upscale loop 45 instructions from 49. clang-cl: 92 from 98, and 48 from 56.

Six rounds, both "Common" sections, against 2e606ac:
- 8500T, AVX2: the 4K -> 1080p rows of 3 and 4 channels -3.1% to -3.9% under MSVC and -2.8% to -3.9% under clang-cl;
  24 MP -> 1080p -1.6% and -3.3%; the 8x thumbnail -2% to -3%; the rest within 2%.
- 12600K: the P-core agrees within its noise. The E-core within 1%, but: clang-cl's Grayscale8 rows -3% to -7%,
  and MSVC's 4K -> 1080p Grayscale8 +3.4% at AVX2 and -4.3% at SSE4.1 (open lead 22's row).
- SSE4.1 on the 12600K: within 2%. On the 8500T that level's rows moved by up to +12% in a prototype build whose
  jumps fell on 32-byte boundaries, in loops with unchanged instructions: it is built without the padding, and is no
  reading of a change there.
- A prototype of each half on the 8500T, MSVC: the address alone -1% to -2.6%, the test alone about -1%.

With the y runs trimmed again and no room behind a run in the source buffers, against 2e606ac:
- Pi (GCC), five rounds: the 3- and 4-channel downscales and thumbnails -1% to -3.6%, RGBA32's downscale level;
  720p -> 4K Grayscale8 level.
- N4100 (SSE4.1), five rounds: downscales -1% to -2%, Grayscale8's -3.4%; upscales level by their minimums.
- 8500T, AVX2, six rounds: the 4K -> 1080p rows -2.9% to -5.6% (MSVC). Its upscale rows read two levels by launch in
  that session, one binary 13.2 and 14.1 ms (open lead 24).
- The source buffers' change alone, against the commit before it: within 2% on the 8500T and the E-core at both
  levels; on the Pi 4K -> 1080p RGB32 -2.5%.

## Experiments that lost

1. **Weight broadcasts instead of the lane permute on ARM** (8429a19, reverted).
   - What: the horizontal pass spread weights with `permutevar8x32_ps`, which SIMDe emulated element by element on NEON.
     Two broadcasts replaced it outside x64.
   - Result: ARM GCC SIMD speedup 0.86-1.25x before, 0.84-1.34x after.
   - The cost was GCC's 256-bit type going through memory, not the permute.
2. **An early return and register-held span state in `prepareRun`** (e71b61c, reverted by d8172a0).
   - Aimed at MSVC, which lacks type-based alias analysis and may reload the buffer's fields after every float store.
   - PC, MSVC: within 5% either way, no row gaining from both together on upscales.
   - Pi: both together cost 2-5% single-threaded and nothing with threads. Clang already keeps the fields in registers,
     so the locals only add register pressure.
3. **Chunk 256** (97b96ed, replaced by 128): the Pi's thread cost above.
4. **Chunks of 32 and 16:** not measured.
   - They save about 0.5 KB of L1 per row against 64.
   - The table above shows the copying they add, and the Pi's single-threaded numbers already favour less copying.
   - The Pi's noise is 5-10%, so telling them apart from 64 would take several runs of each.
5. **Four runs of buffer capacity:** not built. It would cut the copying for 4K -> 64x64 to a third, but a row pair would
   need about 46 KB, more than the Pi's L1.
6. **The old direct-load path for windows of up to 4 taps on x64:** not built.
   - It would win back the x64 upscale cost of pre-conversion: in one PC session, 1080p -> 1440p took 7.25 ms at 7919eaf
     and 8.5-8.7 ms with the sliding buffer.
   - It would add a second horizontal pass selected by platform and window size.
7. **A byte-to-float lookup table in the scalar conversion** (not committed).
   - Aimed at a suspected dependency chain through `cvtsi2ss`. MSVC already breaks it with `xorps`.
   - PC: scalar downscales 2-5% faster, 1080p -> 1440p unchanged, across sessions.
   - Dropped: unmeasured on ARM, where a table lookup blocks vectorizing the conversion.
8. **Row blocks for the Windows cost of strips** (not committed).
   - What: each thread ran all strips over a block of about 2.7 MB of destination rows before the next block, so every
     destination page filled while cached.
   - Result: no change against strips alone, on fresh and reused destinations. The cost is in the faults (the column
     strips section).
9. **Streaming stores to the destination** (N4100, not committed): within 1% under MSVC, upscales -3% to -4% under
   clang-cl. A block that is not 16-aligned, as RGB24's, mixes them with ordinary stores: +85%.
10. **The x weights pre-spread per channel in an aligned table** (N4100, not committed), so that the multiply takes them
    as its memory operand: 4-byte downscales +2% to +5% under MSVC. The table is 4x the weights, and built per call.
11. **A prefetch on each vertical tap's row** (N4100, not committed): within noise under both compilers.
12. **One row per horizontal sweep at SSE4.1** (N4100, not committed): 9-41% slower than paired rows.
13. **A software prefetch of the next row pair's source segment** (8500T, not committed): no consistent gain.
14. **Three vertical blocks per step for 3-channel pixels at AVX2** (8500T, not committed): no row gains under MSVC or
    clang-cl, several lose 2-5%.
15. **The AVX2 conversion with fewer shuffles** (8500T, not committed).
    - RGB pixels widened by one 256-bit byte shuffle per pixel pair, 2 shuffles per 4 pixels in place of 4: 4K -> 1080p RGB24
      +6% under MSVC, +3% under clang-cl, three rounds, where the same row varies by 5% between two runs of one binary.
    - Each 8-byte widening loading its own bytes, as at SSE4.1: -1% to -3% under MSVC over six rounds, and the RGB24
      rows, whose conversion it does not touch, moved as much.
    - The conversion loop's shuffle count does not limit a resize: halving it changed nothing measurable.
16. **The horizontal filter's batch end found ahead** (8500T and the Pi, not committed): a scan over the runs for the
    last column the buffers cover, then a loop with no check. MSVC -3% to -6% on 4K downscales, clang-cl +4% to +7% on
    Grayscale8 downscales and RGB upscales, GCC within 2%. The check against a range held by value gains about twice as much.
17. **The destination's blocks zeroed ahead with `DC ZVA`** (Pi, GCC, not committed): 720p -> 4K RGBA32 +12% single-threaded
    and +10% with threads against the batched loop it was meant to mend. The zeroed lines pass through L2 as the stored
    ones do. A prefetch 2 lines ahead of the temp rows' stores, for writing or for reading: no change.
18. **One 16-byte store per 3-channel temp pixel** (8500T, not committed): temp rows padded by one float, in place of an
    8-byte and a 4-byte store. MSVC -3% to +0.4% on the RGB rows, clang-cl -4% to +4%, the untouched RGBA32 rows within 1%.
19. **Two horizontal chains at AVX2** (8500T, not committed): clang-cl -2% to -4% on most rows, MSVC within 2% on 12-tap
    rows and +5% on 24 MP -> 1080p, +8% on 101 MP -> 720p, +12% on 4K -> 64x64. Long runs do overlap four chains.
20. **Run table entries of 12 bytes** (8500T, not committed): three `uint32_t` in place of three `size_t`. MSVC within
    2%, clang-cl +3.5% to +8% on colour upscales.
21. **Streaming stores for the 32-byte output blocks at AVX2** (8500T, not committed): `_mm_stream_si128` where the
    block is 16-byte aligned, which VTune shows is every block of 720p -> 4K RGBA32. Upscales within 2% under MSVC and
    clang-cl; MSVC's 4K -> 1080p RGB32 +4% to +6% and 8K -> 4K +4%. RGB24's 24-byte blocks were left on plain stores.
22. **The x runs padded to a multiple of 4 taps** (8500T, not committed): zero weights after each run, before it where it
    would pass the source's end, so that runs of 18 to 20 taps all take the same steps and the 2- and 1-tap steps never
    run. MSVC within 2%, clang-cl within 3.5%, the non-integer downscales included; the thumbnail +2% to +4%. The
    tap-count branches are not mispredicted to begin with. The tests pass with it.
23. **Running offsets in the batched horizontal loop** (8500T, not committed): the temp offset advanced per column and
    the run's buffer offset computed once for the range check and the load. MSVC -1% to -3% on upscales and +4.5% on
    4K -> 1080p RGBA32; clang-cl +7% to +10% on the RGB24 rows.
24. **Vertical accumulators started at 0.5** (8500T, not committed), so that the output step truncates without its
    rounding add: 8 `vaddps` fewer per 16-pixel step at AVX2, and the float sum in a different order. The 720p -> 4K
    rows -1% to -3% under MSVC and clang-cl, clang-cl's RGB24 within 1.5%; the 4K -> 1080p rows +1% to +4% by median
    and within about 2% by minimum. The adds run on
    ports 0 and 1, which do not bound the step. The tests pass with it.
25. **The tap list at NEON** (Pi, not committed), with 32-byte, 16-byte and `float` stored weights: Clang +3% to +7.5%
    on nearly every row with each; GCC +3% to +19% at 32 bytes, and with the other two upscales to -6% and downscales
    +0.5% to +3%.
26. **The tap list's weights stored already broadcast** (not committed): 16 or 32 bytes per tap in place of a `float`.
    Equal to `float` at AVX2 (best of six processes 16.17, 16.25 and 16.18 ms on 720p -> 4K RGBA32, 16.72 without a
    list) and on the i3-2310M; 32 bytes cost the N4100's downscales up to 1.5 points more and the Pi's GCC build up to
    19%.
27. **Stepped pointers in `filterRun`** (against 5e3c797, not committed), in two forms. MSVC, eight rounds; the Pi five.
    - Every step from three stepped pointers, each step decided by a bit of the tap count: no indexed operand left,
      but a 12-tap RGB32 column takes 89 instructions against 81 (the block loop's end pointer, the pointers
      re-derived after the first block, a loop bound moved to the stack). 4K -> 1080p +3% to +8% on the 8500T, +1% to
      +11% on the 12600K's E-core; 4K -> 64x64 -19% and 101 MP -> 720p -3% on the 8500T. Pi, GCC: 4K -> 1080p
      Grayscale8 -5%, the rest within 2%, 4K -> 64x64 level.
    - Stepped only in the block loop, entered where a second block exists: 4K -> 64x64 -13.5% on the 8500T, but
      4K -> 1080p +2% to +5% on all three x64 cores (Grayscale8 +11% on the E-core), the thumbnail +4% and +12%.
      A 12-tap column takes 84 instructions: `tap` has two sources, and MSVC tests each later step with a move more.
    - A row pair's pointers in an array: MSVC keeps the array in memory and steps it with a vector add.
    - clang-cl 22, same rounds: every step stepped, 4K -> 1080p +2% to +4% on the 8500T and level on the 12600K,
      4K -> 64x64 -6%; the block loop alone, within 3% everywhere and 4K -> 64x64 +4%. Its build of 3d1717b leads
      MSVC's by 11.5% on 4K -> 64x64, 4% on 101 MP -> 720p and 9% on 720p -> 4K Grayscale8 (8500T).
    - The thumbnail rows (16x and 32x), MSVC with the block loop stepped against MSVC: -11% and -13% on the 8500T,
      as 4K -> 64x64; within 3% on both of the 12600K's cores, 4K -> 64x64 included. clang-cl's build of the
      current code against MSVC's: the same, -12% on the 8500T's three rows and level on the 12600K.
      The long runs' loss under MSVC is Skylake's.
    - GCC steps the block loop's pointers itself and MSVC does not: the long runs' gain is MSVC's alone. AArch64 has
      no base + index + offset operand and pairs loads only from a base: there the steps after the loop gain.
    - On these columns one instruction is about 1.2% of the 4K -> 1080p rows: short runs and long runs want
      different code ("The x runs padded to one length, and a kernel per length", and the block loop's design entry).
28. **The horizontal pass as a function of its own** (against 1e18e27, not committed): `filterHorizontal` not
    inlined into `resizeRowsWith`, the sliding source buffers passed by reference as they are. Meant to keep one
    pass's change out of another's code, as 7a873c8's reached the upscales' at SSE4.1, and to free registers.
    Six rounds on the 8500T, four on the 12600K, AVX2 and SSE4.1:
    - MSVC: the SSE4.1 downscales and thumbnails +6% to +7% on the 8500T and +2% to +8% on the P-core; AVX2
      4K -> 1080p Grayscale8 +6% on the P-core and +22% on the E-core. 720p -> 4K Grayscale8 -9% at AVX2 on the 8500T.
    - clang-cl: the AVX2 16x and 32x thumbnails +10% and +12% (8500T), the SSE4.1 downscales +2% to +9%.
      720p -> 4K Grayscale8 -10% on the E-core at AVX2, which undoes its +5% of "The upscale passes".
    - MSVC's two-row loops, instructions and stack references, inlined against split: the one-channel upscale pass
      49 and 4 against 45 and 0 at AVX2; the general pass 114 and 1 against 105 and 3 at AVX2, 131 and 2 against 142
      and 2 at SSE4.1. The general pass is short of registers on its own values.
    - MSVC's SSE4.1 block loop in the split pass: four of the eight chains are summed through a second register,
      four moves a block and four before the loop, 43 instructions a block from 39. A 12-tap run is three blocks
      there: about 12 instructions a column. The same habit as `RowChains` guards against.
    - clang-cl's AVX2 block loop in the split pass addresses both rows by base and scaled index, all 8 multiply-adds;
      inlined, it steps row A's pointer and reaches row B at a constant distance from it, 4 indexed. On Skylake an
      indexed multiply-add is two micro-ops: the 8500T's thumbnails. The 12600K's were not run.
    - Neither comes from the buffers' state reaching the pass through a reference, the first suspect.
29. **The weight spread's indices hidden from MSVC too** (against 2e606ac, not committed): a `volatile` zero added
    to them in `spreadIndices`. MSVC loads a known vector constant ahead of the innermost loop that uses it, so
    the RGB32 column loop reloaded the four indices every column, into registers it holds for them alone, and the
    4-tap pass two.
    - Hidden, they are formed once per row: the general column loop 4 instructions shorter, the 4-tap loop 26 from 28.
    - Six rounds, both "Common" sections: no row moved on the 8500T or the 12600K's E-core; the P-core's -1.5% to
      -3% is its Grayscale8 rows' too, whose code has no spread.
    - A removed instruction is not a gain by itself in these loops: these loads cost less than the rows' noise.
30. **Interleaved row pairs on runs read per run** (against 963e6bc, not committed): blocks of 8 taps in 4 chains, then
    steps of 4, 2 and 1 tap. Medians against the kernel's form:
    - E-core: -22% to -30% from 22 taps up, both compilers.
    - 8500T: -2% to +3% at 22-28 taps, +15% to +20% at 96-360 taps, both compilers.
    - P-core, MSVC: -5% to -7% at 22-28 taps, +18% to +36% at 96-360.
    - 8 chains, a block's halves in two sets: the P-core +8% to +13% at 96-360 under MSVC, +7% to +8% under clang-cl;
      the 8500T unchanged; the E-core 3 points less gain.
    - Every other weight broadcast from memory, half the shuffles: the 8500T +3% to +7% at 22-28 taps, its long runs
      unchanged; with 8 chains +7.5% to +14% on the 8500T, +2.6% to +9.6% on the P-core.
    - The likely causes, not profiled: one register holds both rows, 4 sums per column where the kernel's form has 8,
      too few once a column is too long for the next one to overlap it; and a weight shuffle per tap where the kernel's
      form has half a permute. The 8500T runs shuffles on one port, the P-core on two, one of them a multiply-add port.
31. **The AVX2 limit at 32 taps with the interleaved columns** (not committed). MSVC, 24 MP -> 1080p (22-23 taps) and
    the 240p thumbnail (27-28): the 8500T +3% and +7%, the P-core -3% and -2%, the E-core -21% and -15%. clang-cl: the
    8500T -1.5% and +1.5%, the E-core -26% and -24%.
32. **Every other weight broadcast in the padded columns too** (not committed): against the shuffles alone, the E-core
    4 points more gain on 4K -> 1080p, the P-core 3 points less, the 8500T level.
33. **Two chains at 5-8 taps of the interleaved column** (not committed): no change on the 7-tap row on either machine.
34. **The interleaved column's pixel pairs by a stepped pointer** (not committed): MSVC then addresses every load by one
    register and a constant, no index. 8500T, 12 rounds: 4K -> 1080p RGB32 -0.9% / -1.2%; the
    others from -6.6% / -2.0% to +6.8% / +4.2%, as wide as Grayscale8's +7.9% / +1.2%, a layout the change leaves alone.
    The column loop issues 51.5 uops per column pair against 59, and takes the same 18.3 cycles: the front end leaves
    2% of the issue slots empty against 15%, and the back end is full 23.5% of cycles against 3%.
35. **Diagnostics of the interleaved column, 4K -> 1080p RGB32** (not committed, wrong pixels): one weights pointer for
    every column, and the sums kept in a register in place of the temp-pixel stores, each with and without
    experiment 34's stepped pointer. 8500T, 20 rounds; cycles per column pair from the hardware events:

    | Build | Whole resize | Cycles | Uops issued | Loads blocked by a 4K alias |
    |---|---:|---:|---:|---:|
    | Indexed loads | +0.9% / +0.7% | 18.6 | 61.2 | 0.97 |
    | Stepped pointer, the reference | 19.45 ms | 18.4 | 52.4 | 0.95 |
    | One weights pointer, indexed / stepped | -1.8% / -3.0% | 18.1 / 17.5 | 60.1 / 51.5 | 0.98 |
    | No stores, indexed / stepped | -5.5% / -10.7% | 16.5 / 14.8 | 52.7 / 44.4 | 0.19 |
    | Both, indexed / stepped | -9.3% / -14.6% | 15.4 / 13.5 | 50.7 / 42.9 | 0.17 |

    - The stores cost the most: without them the 4K aliases fall by four fifths and port 5 does 3 uops fewer per pair.
    - The stepped pointer gains only without the stores: with the indexed loads the front end then leaves 14-18% of
      the issue slots empty.
36. **The general column's row pointers stepped as the interleaved column's** (against 560ca96, not committed): two
    pointers stepped by each run's start, hidden from Clang. The 1-float kernels' column loops lose their 2-6 indexed
    multiply-adds and gain 6 instructions under MSVC, 4-6 under clang-cl. The unpadded kernels' tail steps stay indexed by
    the tap counter, and their column loops gain up to 13 instructions. 8500T, 12 rounds: 4K -> 1080p Grayscale8 +12.6%
    under MSVC, +8.6% under clang-cl; MSVC's 4K -> 720p and -> 1600x900 RGB32 +3% to +4%.

## Open leads

1. **x64 GCC runners, 4K -> 64x64:** resizer / QImage 3.55 at f435b80, 4.04 at d436d41, 3.95 at 1ecd630. The sliding buffer
   fixed the same regression on MSVC: 1.84, 2.66, 1.76.
2. **Native size on the x64 runners:** resizer / `QImage::copy` 7.59 (GCC) and 7.26 (Clang) at d436d41, about 1.0 on ARM.
   Not investigated.
3. **Pre-conversion costs MSVC's upscales:** 1080p -> 1440p 7.25 -> 8.5-8.7 ms on the PC (experiment 6). The scalar path
   lost 7-13% on its three-channel upscales under MSVC alone, and its phase timings put the loss in the horizontal filter's
   code, not in memory traffic. Comparing MSVC's code for `filterHorizontalRowGroup` with clang-cl's is the next step.
4. **The SSE4.1 kernels on the N4100:** opaque downscales stay 3x and more behind Qt after the two-chain block. Its log
   has where the time goes: the horizontal taps are 53-70% of a downscale, the vertical pass 46-72% of an upscale.
   - Where the weights' period is long they are still swept out of L2 once per row pair: 101 MP -> 720p would gain 8% from
     weights in L1.
   - Otherwise the taps are bound by instruction count on a core that decodes 3 per cycle. 16-bit fixed-point kernels
     at this level would halve the loads and multiplies per tap and drop the float conversion.
   - A ring small enough for L1 gains upscales 3-7% and costs downscales up to 25%.
5. **Strips cost where the L2 is large:** 1-5% on Neoverse-N2's SIMD rows, 3-8% on the PC's downscales (the column strips
   section). The budget is already the runtime L2 share, taken for the worst case: the smallest share, every logical
   processor busy. A budget by the threads in use, or by the core a band runs on, is untried.
6. **Placement still moves MSVC's AVX2 upscale rows about 5%** with `/QIntel-jcc-erratum`, and the SSE4.1 ones, built
   without it, up to 12-14%. Which loop of the upscale path reacts is unidentified: padding at the kernel's start shifts
   them all together. MSVC has no loop alignment control; clang-cl's code for the reduced loop does not react.
7. **`/QIntel-jcc-erratum` on the SSE4.1 level is untested on a Sandy Bridge.** Every CPU measured so far loses with it or
   is indifferent (the section on jumps and 32-byte boundaries).
8. **GCC's 720p -> 4K RGBA32 on Neoverse-N2 moves 10% with the binary's layout** (the call-site attribute notes): 24.1 or
   21.9 ms for the same kernel source. Code placement or the buffers' addresses, undetermined. The Pi does not react.
9. **The PC's only run since 0bd90e4 is a6c69e7, in its band-count section:** no step between the two is measured
   there on its own. Its MSVC upscale rows are the placement-sensitive ones.
10. **What the vertical taps' forms leave behind** (their section):
    - MSVC's 4K -> 1080p Grayscale8 at SSE4.1 is 5-7% slower with the list on the 8500T, and on no other machine: jump
      placement. Padded like the AVX2 kernels, that row is 14% faster (its log). No CPU with the erratum runs these kernels
      outside the benchmark.
    - The N4100's downscales pay 2-4% for the list at SSE4.1. A form chosen by core, not by level, would avoid it.
    - GCC at NEON loses 3% and 5.5% on the RGB32 and RGB24 upscales with the first tap assigned, where Clang gains.
      Its tap loop is as Clang's; the rest of its step was not read.
    - GCC and Clang on x64 have not been timed with the list.
11. **MSVC against clang-cl on the N4100, after the one-channel fixes:** clang-cl ahead by 12% on the Grayscale8
    downscale and 5% on the RGBA32 downscale, MSVC by 4-5% on the RGB32 and RGB24 downscales. The listings of those
    kernels have not been compared.
12. **Four chains at NEON leave GCC no spare register:** 16 accumulators, 2 weight registers and the block's 16 pixel
    vectors loaded ahead of the multiplies. On the Pi two chains gain GCC 4-6% on downscales, and cost Clang 8% on
    4K -> 64x64 and 3% on 101 MP -> 720p (its log). Not adopted. Neoverse-N2 and Apple Silicon are unmeasured.
13. **The AVX2 kernels on the 8500T** (its log has where the time goes): the source read, its conversion and the
    per-run bookkeeping are a third of a downscale, the horizontal taps 43-55%.
    - Both passes cost the same per multiply-add, so filtering vertically first on downscales would gain nothing.
    - 16-bit fixed-point kernels would drop the conversion, halve the temp rows' bytes and double the multiply-adds
      per instruction.
14. **MSVC against clang-cl on the 8500T.** At AVX2, with the spread's indices hidden and the conversion's tail loop
    scalar: level on the RGB32 downscales, clang-cl ahead by 4-8% on Grayscale8.
    - clang-cl's one-channel four-column loop is short of general registers: 55 instructions with 5 stack accesses, and
      61 with 7 once the tail loop is scalar, the added ones register moves and reloads. 720p -> 4K Grayscale8 loses
      2.5-3.5% to that. Fewer values live across that loop would settle it, perhaps under MSVC too: untried.
    - At SSE4.1 on the 8500T, both compilers' kernels padded: MSVC ahead by 6-7% on 4K -> 1080p RGB32 and RGB24, 3% on
      RGBA32, within 2% elsewhere; unpadded, RGB24 reads 4 points worse for clang-cl. The kernels retire the same
      instructions within 3% and their tap loops match. clang-cl's per-column code around them reloads 9 values from
      the stack where MSVC's reloads 5, and copies registers: no single cause, no source form tried.
    - The i3-2310M, the SSE4.1 CPU without the erratum, had MSVC ahead by 7-9% on those rows at e8d2d15: not remeasured
      since the tail loop went scalar.
15. **The cap of 4 bands leaves threads idle:** a band per thread takes 18-29% off the threaded rows on the 8500T (6
    bands) and 21-36% on the PC (16), 4K -> 64x64 aside (their logs).
    - A count that is not a multiple of the pool's threads loses the gain: 8 bands on 6 threads run like 4.
    - Bands past twice the thread count cost 5-20% on the 8500T. `ParallelForFn` does not carry the thread count.
16. **The destination's first touch:** in a VTune run of 720p -> 4K RGBA32 on the 8500T, the destination allocated
    inside the timed call, `touchDestPagesInOrder` takes 0.8 s against the AVX2 kernel's 6.9 s, and 2.5 s more fall
    outside the binary's sources, not attributed. Streaming stores leave the row's time unchanged (experiment 21).
17. **`STNP` on Apple Silicon is unmeasured** beyond the CI runner's noise. Why the A72 detects a stream of plain
    stores in one build and not in another is not found. Lead 8 may be the same effect.
18. **What the horizontal loop executes besides arithmetic** (8500T, VTune, MSVC, 4K -> 1080p RGB32):
    - The per-column loop is 65% of the kernel, conversion 18%, the vertical pass 16%.
    - It retires about 3.3 instructions per cycle: instruction count limits it, not stalls or the shuffle port.
    - About 78 instructions per column pair for about 28 of arithmetic: run lookup, range check, offsets, tap-count
      branches, four permutation constants reloaded, four stack reloads.
    - Experiments 18 to 20, 22 and 23 each took a few of those instructions out or moved them: none gained.
    - GCC's code for this loop's model in `multiply_add_ceiling` is 73 instructions per 12-tap column pair against
      MSVC's 82, and its pass 7% faster: the spread constants stay in registers, where MSVC reloads all four per column.
    - The vertical pass reads no state through a structure in its tap loop: nothing to take by value there.
    - NEON stores a 3-float temp pixel through the stack, a 16-byte store then 8 and 4 bytes copied out (GCC's listing
      on the Pi): untried.
19. **The interleaved row pairs past 20 taps.** In the kernel at padded lengths ("Row pairs interleaved at AVX2, at
    padded lengths"). On runs read per run the E-core would gain 22-30%, where the big cores lose up to 36% to the
    kernel's form (experiment 30). No form tried keeps both big cores level there; the threads of one resize run on
    both core types, so the form cannot follow the core.
20. **An AVX level for CPUs with AVX and no AVX2** (Sandy Bridge, Ivy Bridge). A prototype with 256-bit floats and
    128-bit integer steps, in the SSE4.1 level's place: the i3-2310M's log has its shape and figures.
    - The i3-2310M: 24 MP -> 1080p, 1080p -> 240p, 4K -> 64x64 and 101 MP -> 720p -16% to -29%; upscales and Grayscale8
      within noise.
    - Its single-threaded 4K -> 1080p rows vary by round, -2% to -9%; with threads -9% to -16%. Unexplained. Untried:
      32-byte-aligned float buffers, and the SSE4.1 chain and block counts.
    - A core that splits 256-bit operations gains nothing: the 12600K's E-core loses 4-18% on upscales (the PC's log).
      AMD's CPUs with AVX and no AVX2 split them too; none is measured.
    - The SSE4.1 kernels VEX-encoded, with no other change: -5% to +4% on the i3-2310M, +5% to +57% on the PC's P-core.
21. **32-byte loads that the float buffers' 64-byte start does not align.** Unmeasured:
    - The horizontal pass, a run at an odd pixel offset: 16 bytes off for 4-float pixels.
    - The vertical pass, a temp row stride that is not a multiple of 32 bytes: a strip as wide as a destination whose
      width is not a multiple of the vertical block's pixels.
22. **The 12600K's E-core loses up to 10% of a row by where the horizontal column loop lies** (the PC's log).
    - 4K -> 1080p Grayscale8, two builds with the same column loop, 155 instructions and 754 bytes at the same offset
      within a 64-byte line, 960 bytes apart: 64.8 G and 73.0 G cycles for the same 236 G instructions.
    - The whole difference is front-end slots lost to predecode, 46 G and 83 G of them. Mispredicted branches are
      level; there are no machine clears, blocked or split loads, or L1 misses.
    - The stalls fall on different stretches of the loop in the two builds, and the faster one still loses 14% of the
      loop's time to them.
    - 24 MP -> 1080p RGB32 takes 84 or 93 ms by 4 to 24 bytes of padding ahead of the loop.
    - An A/B of two binaries on this core is unreliable by about 10% on a row whose column loop moved.
    - The cause: bytes inside the loop that are never decoded. The core marks instruction ends per byte of its
      instruction cache as it decodes them, and a small table holds the lines that still have unmarked stretches
      (Intel's patent US 12,353,881); fetch through such a line takes the slow path. With 12-tap runs the 16-tap block
      loop and the unrolled tail loops are skipped every column, and the tail loops' unrolled copies can never run.
    - Shown by two variants: without the tail loops' dead copies the stalls fall from 23% of the column loop's cycles
      to 4% (the last taps as steps); with one run per row pair that takes every step, to 0.3%. That run costs 8% more
      instructions and brings predecode stalls to RGB32, which had none: a probe, not a fix.
    - 4K -> 1080p RGB32's column loop has no predecode stalls: the back end bounds it.
    - Not found: which lines keep a table entry at a given address. Unmeasured: other rows, the N4100, a single resize
      after other code, where every line of the kernel is newly loaded.
    - The state of 4K -> 1080p RGB32 and RGB24 that followed the process's heap layout was not this effect: "Where the
      weights start".
23. **Why Goldmont Plus prefers the temp ring 16 bytes past a cache line** is not known. Unmeasured: the committed
    form anywhere but the N4100, rows other than 4K -> 1080p RGBA32 on the 8500T at SSE4.1, and any start but the
    boundary at NEON.
24. **Two timing levels the weights do not explain** (8500T): 720p -> 4K Grayscale8 at 5.25 and 5.54 ms and native size
    at 1.96 and 2.2 ms, by launch, before and after "Where the weights start". Not investigated. Also unmeasured: the
    larger weight table on the i3-2310M.
    - The other upscale rows there do the same in some sessions: 720p -> 4K RGB24 at 13.2 and 14.1 ms from one binary.
    - Pi, 720p -> 4K RGBA32: 88 ms for the 2e606ac binary within a run of both "Common" sections and 92 ms as the
      only row run; three later builds 91-95 ms and 90-91 ms. Same user and system time and page faults (127.8
      thousand) for all, and the kernel's code is the same or shorter: what ran before the row in the process
      decides, the heap's state the likely carrier. A row's reading across builds is not safe from it.
25. **The limits of the kernel per x run length** ("The x runs padded to one length, and a kernel per length").
    - 1-float pixels at 18-20 taps at AVX2: measured up to 17.
    - 2-float pixels read the count at every length: a build with a limit of 20 for them gained 1-25% at 6-17 taps, the
      N4100 least (3-6%).
    - NEON past 32.
    - SSE4.1 past 12: a limit of 20 is the candidate, pending the big cores' run of the fenced build. 4-float pixels: the
      big cores gain 3-14% at 14-17 taps unfenced; the N4100 loses 2-8% unfenced, and fenced reads -0.1% and -3.1% (its
      noise 4%). Unmeasured: the big cores fenced, and the i3-2310M. 1-float pixels gain 17-32% at 17 taps.
    - Mixed lengths mid-range: runs of 14 and 15 taps gain nothing on x64 once padded, where 6 and 7 gain 9%.
    - Unexplained: on the 12600K's P-core the 16x thumbnail gained 9% and the 32x one 3% from the block loop's steps.
26. **The x64 column loop holds more live values than registers** (MSVC). Of a 12-tap RGB32 column's 81
    instructions at d77c79c, 25 are arithmetic. Per column, outside it:
    - 5-6 loads of loop constants from the stack: the weights' base, row B's base, both temp rows' bases, the
      destination offset; for Grayscale8 the loop bound too.
    - 7 instructions to form the temp store's address from the column index and those bases.
    - The spread constants reloaded, two of them inside the first block.
    - A change that adds a live value costs a reload: experiment 27's end pointer moved the loop bound to the stack.
    - Lead: fewer live values per column. Output pointers stepped per column in place of base and offset, as the
      model benchmark of `throughput-ceiling.md` has them, remove two bases, the offset and the address arithmetic.
    - Done for `filterBufferedRuns` ("The general pass steps its run and temp pixels"): 72 instructions, 1 load.
    - Done for the one-channel upscale pass ("The upscale passes step their runs and temp pixels too").
    - The spread constants reloaded every column: removing the reloads gains nothing (experiment 29).
    - Done for the weights' base and the range's first pixel ("A run holds its weights' address, and the x runs
      start in order").
    - Left in the two-row loops (MSVC): at AVX2 the general pass reloads the loop bound and row B's origin after a
      block loop, nothing on a run under 16 taps (Grayscale8's loop holds 3 loads); the one-channel upscale pass 1 at
      SSE4.1; the RGBA32 upscale pass 1. The one-channel pass is as short of registers under Clang as under MSVC.
27. **A batch API for a caller that knows it has many resizes in a row** (not designed), a folder's thumbnails for one.
    Each `resize()` call builds both axes' weights and allocates the source float buffers and the temp ring per band;
    a 1080p -> 120 px thumbnail is under 2 ms in all, so that setup may be a visible share. Unmeasured.
    - At least the allocations can be kept between calls.
    - Sources of one size to one target size share their weights whole.
