# image-processing

Image resizer with hand-optimized SIMD kernels for x64 and ARM64

## Resizer

- `resize()` in `resize/cimageresizer.h`: Catmull-Rom when upscaling, Lanczos3 when downscaling, chosen per axis.
- Takes 1-4 channels of 8 bits, with any pixel stride and row stride, and an optional source rectangle.
- 2- and 4-channel images carry alpha as the last channel. The output is premultiplied; a straight-alpha source is
  premultiplied as it is read.
- SIMD kernel for 4-byte pixels: one outline, `resize/cimageresizer_simd.inl`, compiled per instruction set against that set's
  `simd_primitives_*.h` by a `cimageresizer_simd_*.cpp`.
  - x64: AVX2 + FMA where the CPU has them, SSE4.1 otherwise, chosen at runtime. Below SSE4.1, `resize()` refuses to run.
  - ARM64: NEON.
  - Other architectures are not supported.
- Other pixel layouts take a scalar path.
- Multithreaded through a caller-supplied `ParallelForFn`.
- The core is Qt-free. `resize/qimage_resize.h` is a header-only bridge for `QImage`.

## Layout

- `resize/`: the library, built as a static lib by `image-processing.pro`.
- `tests/`: Catch2 tests and benchmarks against `QImage::scaled`.
- `resize-comparison/`: a Qt app comparing resize implementations by speed, and by PSNR and SSIM on a round trip.
- `scripts/run_tests`: builds and runs the tests (`.bat`/`.ps1` on Windows, `.sh` elsewhere).
  - `--benchmark` runs the benchmarks instead and prints the ratio table.
  - `--debug` uses the debug build.
  - `--no-build` skips the build.
  - Anything else goes to the Catch2 runner.

## Dependencies

- [htpps://github.com/VioletGiraffe/cpp-template-utils](cpp-template-utils) header-only module - put it alongside this repo.
- Qt only for the bridge, the tests and `resize-comparison`.

## Docs

- [doc/resize-performance.md](doc/resize-performance.md): measurements behind the design, and the experiments that lost.
- [doc/tests_spec.md](doc/tests_spec.md): test work still to add.
