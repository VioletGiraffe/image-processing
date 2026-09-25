# image-processing

Image resizer with hand-optimized SIMD path (and scalar fallback for old CPUs)

## Resizer

- `resize()` in `resize/cimageresizer.h`: Catmull-Rom when upscaling, Lanczos3 when downscaling, chosen per axis.
- Takes 1-4 channels of 8 bits, with any pixel stride and row stride, and an optional source rectangle.
- 2- and 4-channel images carry alpha as the last channel. The output is premultiplied; a straight-alpha source is
  premultiplied as it is read.
- SIMD kernel for 4-byte pixels:
  - x64: AVX2 + FMA, chosen at runtime.
  - ARM64: the same source through SIMDe on NEON.
  - Everything else takes the scalar path.
- Multithreaded through a caller-supplied `ParallelForFn`.
- The core is Qt-free. `resize/qimage_resize.h` is a header-only bridge for `QImage`.

## Layout

- `resize/`: the library, built as a static lib by `image-processing.pro`.
- `3rdparty/simde/`: the SIMDe headers the kernel uses.
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
