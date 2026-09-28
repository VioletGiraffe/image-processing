#pragma once

#if defined(_M_X64) || defined(__x86_64__)
	#define IMAGE_PROCESSING_X64 1
#else
	#define IMAGE_PROCESSING_X64 0
#endif

#if defined(_M_ARM64) || defined(__aarch64__)
	#define IMAGE_PROCESSING_ARM64 1
#else
	#define IMAGE_PROCESSING_ARM64 0
#endif

#define IMAGE_PROCESSING_SIMD (IMAGE_PROCESSING_X64 || IMAGE_PROCESSING_ARM64)

#if IMAGE_PROCESSING_X64
	#include <immintrin.h>
	#if defined(_MSC_VER)
		#include <intrin.h>
	#endif
#endif

// The *_INLINE macros are forced: the kernels' per-column and per-pixel helpers must not become calls
#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	// GCC and Clang allow AVX2 intrinsics only in functions with the AVX2 target
	#define IMAGE_PROCESSING_AVX2_TARGET __attribute__((target("avx2,fma"), noinline))
	#define IMAGE_PROCESSING_AVX2_INLINE inline __attribute__((target("avx2,fma"), always_inline))
#elif IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_AVX2_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_AVX2_INLINE __forceinline
#elif IMAGE_PROCESSING_ARM64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_NEON_INLINE __forceinline
#elif IMAGE_PROCESSING_ARM64
	#define IMAGE_PROCESSING_NEON_INLINE inline __attribute__((always_inline))
#endif

namespace ImageProcessing::SimdSupport
{
	// Answers for every target, so callers need no preprocessor guard: false where no SIMD kernels exist.
	[[nodiscard]] inline bool canUseSimd() noexcept
	{
#if IMAGE_PROCESSING_ARM64
		return true;
#elif IMAGE_PROCESSING_X64
		static const bool supported = []() noexcept
			{
#if defined(_MSC_VER)
				int registers[4];
				__cpuid(registers, 0);
				if (registers[0] < 7)
					return false;

				__cpuidex(registers, 1, 0);
				constexpr int fmaBit = 1 << 12;
				constexpr int osXsaveBit = 1 << 27;
				constexpr int avxBit = 1 << 28;
				constexpr int requiredFeatureBits = fmaBit | osXsaveBit | avxBit;
				if ((registers[2] & requiredFeatureBits) != requiredFeatureBits)
					return false;

				if ((_xgetbv(0) & 0x6) != 0x6)
					return false;

				__cpuidex(registers, 7, 0);
				constexpr int avx2Bit = 1 << 5;
				return (registers[1] & avx2Bit) != 0;
#else
				return __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif
			}();

		return supported;
#else
		return false;
#endif
	}

#if IMAGE_PROCESSING_X64
	// A function, not a macro: GCC and Clang expose vzeroupper only inside a target-attributed function
	IMAGE_PROCESSING_AVX2_TARGET inline void clearAvxUpperState() noexcept
	{
		_mm256_zeroupper();
	}
#else
	inline void clearAvxUpperState() noexcept {}
#endif
}
