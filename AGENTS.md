# image-processing agent entry point

Read `README.md` for what the library does and where its docs are, then `doc/resize-performance.md` before changing a
kernel: the measurements behind the design, the experiments that lost, and the open leads.

Treat those documents as project memory: update them with any change to the kernels, their measurements, or the leads.

- New leads and invalidated findings go into the docs in the same pass as the work that found them, not into a reply
  alone. A measurement or fix that turns up something worth pursuing (a residual gap, an untried variant, a trade-off
  left open) gets an entry in `doc/resize-performance.md`'s "Open leads"; one that changes the premise of an existing
  lead or recorded finding gets a note on it. The report says what was added.
- The tell: a reply that calls something "worth investigating", "a follow-up" or "not recorded" about work that has a
  leads list. A lead that exists only in a conversation is lost when the conversation ends.

## Where things are recorded

- `doc/resize-performance.md`: one section per design decision, its numbers summarized; "Experiments that lost";
  "Open leads", numbered. A finished lead is removed and its result goes into the section it produced.
- `doc/cpu/<machine>.md`: the full tables per machine, a section per change. The design doc cites them by section title.
- `doc/throughput-ceiling.md`: what `scripts/perf/multiply_add_ceiling.cpp` measures, section by section. A kernel idea
  can be timed there first, as a loop without the kernel's bookkeeping (sections F and U did it for run triples).
- Result tables: "median of same-round ratios / minimum to minimum" over ten alternating rounds, unless a table says
  otherwise. Minimum to minimum is the reading where the medians spread (the i3-2310M).

## Tools (`scripts/perf/`): use them before writing throwaway equivalents

- `run_benchmark_rounds.py`: alternating rounds of Catch2 benchmark binaries; `--pinned P=4 --pinned E=1000` runs a
  series per core type of the 12600K at once. `compare_benchmark_rounds.py` prints the tables.
- `build_revisions.py`: the tests of git revisions, a tree each; `--patch` for a variant. A history scan: build the
  revisions, one round of them with `run_benchmark_rounds.py`, then `find_benchmark_steps.py` lists the steps.
- `annotate_vtune_hotspots.py`: a VTune profile summed over each kernel loop of a `dumpbin`/`objdump` listing.
- `extract_kernels.py`: kernels in a form two builds diff in; `--loops` lists each kernel's loops.
- `count_boundary_jumps.py`: jumps on 32-byte boundaries (the JCC erratum).
- `multiply_add_ceiling.cpp`: build commands in its header.

## Building and testing

- `scripts/run_tests.ps1` needs `QT_ROOT_DIR`, or `scripts/local-env.ps1` (git-ignored) setting it. The full suite takes
  about 3 minutes on the 12600K, its build included, and runs every SIMD level the CPU has.
- The release build links with LTO: a kernel's code is in the executable, not its object. For a quick look at one
  level's code, compile its `resize/cimageresizer_simd_<level>.cpp` alone with `resize/resize.pri`'s per-file flags
  and `dumpbin /disasm` the object: seconds, not a build. Under MSVC the SSE4.1 kernels are generated at link time in
  the real build, so confirm a finding there in the executable.
- A wrapper script's exit code can hide a failed compile: search the compiler output for "error" as well.

## Measurement hosts

Access and per-machine setup are in `AGENTS.local.md`, git-ignored, where it exists. Read it before any measurement.

- None has a checkout: copy trees over with `git archive -o <zip>` (cpputils, cpp-template-utils and qtutils go beside
  image-processing). Build x64 binaries on the 12600K and copy them; it is faster.
- Never build on a machine while rounds run on it, and estimate every run longer than 30 s before starting it. A
  10-round A/B of ten scenarios: about 80 s on the 8500T, 3.5 minutes on the i3-2310M. One run of the whole benchmark:
  about 25 s on the 8500T, 65 s with controls.
- 12600K (the PC): pin with `start /b /wait /affinity <mask>`, P-core 4 (logical 2), E-core 1000 (logical 12). One run
  of the whole benchmark: about 19 s on the P-core, 27 s on the E-core.
- Core i5-8500T: MSVC, clang-cl, VTune. VTune: `-collect hotspots -knob sampling-mode=hw`; `uarch-exploration`
  crashed the machine once, and so did a precise event (`_PS`) once in six runs. Over ssh, add keepalives
  (`-o ServerAliveInterval=10`) so a crash ends the session.
- Core i3-2310M (Sandy Bridge): AVX is its level, so it judges AVX changes. One run of the whole benchmark: about 35 s.
- Raspberry Pi 4: builds its own, with Clang and GCC: a release build of the tests takes 4-5 minutes. One run of the
  whole benchmark: about 50 s. perf in user space only.
- Mac mini (Apple M1): has a checkout, its dependencies beside it; builds its own with Apple Clang. No pinning: a run
  at the default QoS lands on a P-core. One run of the whole benchmark: about 30 s, and it reads serial rows of 2 ms
  or more to 4%, enough for a history scan; ten alternating rounds for a step it flags.

## Measurement traps

- Code placement moves rows with no change to their instructions (open lead 5): MSVC's upscales by up to 14% in earlier
  builds, clang-cl's SSE4.1 downscales on the 8500T by 6-12%. Which rows react changes with unrelated edits. An A/B
  needs a placement control: both builds again with 32 bytes of `__nop()` at the start of `resizeRowsWith` (and
  `<intrin.h>` in the kernel TUs). A change whose gain is under the control's spread is not a gain.
- The control keeps MSVC's jumps where they lie against the 32-byte boundaries: on a Skylake-family core it misses the
  JCC erratum's effects on the unpadded SSE4.1 kernels.
- Under clang-cl `__nop()` is inline assembly and changes the generated code: diff the padded build's kernels against
  the plain one's with `extract_kernels.py` before reading its rows as placement.
- The 12600K's P-core shifts between levels 15-20% apart for hours: compare only within one session.
- A first-set outlier that does not repeat in a second set is run state: rerun before believing a single set.
- A history scan of one run per revision: adjacent runs flip by the noise both ways. Read a step by the median of three
  revisions on each side, and confirm it with rounds; the fastest of many single runs is no baseline, the noise alone
  puts it low.

## Code traps (the listings showed them)

- MSVC copies a hot loop's accumulators back every iteration, and spills one, when they sit behind a reference or are
  updated through a helper call: accumulate into locals, products written out ("The listed tap loop accumulates into
  locals"). Also see the comments on `addProduct` and `RowChains`.
- MSVC moves small arrays through memory: of vectors, and at SSE4.1 of two pointers stepped by one offset, which it adds
  as one vector through the stack, a store-forwarding stall every column. Hold them in named variables.
- From a loop over a constant count of rows MSVC keeps the first row's prefetches only: write the rows out.
- MSVC lays out a jump over each conditional step a path skips. Taken branches per column decide how many columns back the
  branch predictor sees, about 93 taken branches on the 8500T: a branch that depends on more mispredicts ("`filterRun`'s
  last taps are steps, behind one test for none").
- From an origin plus a float offset MSVC re-derives pointers in scaled-index `lea`s, which Skylake runs on ports 1 and 5
  only, beside the multiply-adds and the shuffles: form such pointers from a byte offset ("A run's pixels from a byte
  offset"). A micro-op removed from another port, a load or a zeroing, gains nothing there: count micro-ops per port.
- Under clang-cl a lambda does not carry the kernel's target attribute: SIMD helpers it calls fail to inline. Use
  functions.
- After changing a hot loop, read its listing under MSVC and clang-cl: instruction count, register copies, stack
  references.

## Shell traps (Windows)

- `pwsh -File s.ps1 a b` binds only `a` to a `[string[]]` parameter: use `pwsh -Command "& .\s.ps1 a,b"`.
- PowerShell pipes corrupt binary streams: `git archive -o file.zip` and `Expand-Archive`, not `git archive | tar`.
- Re-running `vcvars64.bat` in one process grows PATH until "The input line is too long": start a new process.

## Shell traps (macOS)

- zsh does not split an unquoted `$list` into words: a list of binaries passed so arrives as one argument. Use a bash
  array.
- Python is `python3`.
