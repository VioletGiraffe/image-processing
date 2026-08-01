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
		// MSVC permits AVX2/FMA intrinsics in isolated functions without enabling them for the entire translation unit.
		#define SIMDE_X86_AVX2_NATIVE
		#define SIMDE_X86_FMA_NATIVE
	#else
		// AVX2 and FMA are enabled per function, after SIMDe's translation-unit feature detection has run.
		#define SIMDE_NATURAL_VECTOR_SIZE 256
	#endif
#endif

#if IMAGE_PROCESSING_SIMD
	#include "../3rdparty/simde/x86/avx2.h"
	#include "../3rdparty/simde/x86/fma.h"
#endif

#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	#define IMAGE_PROCESSING_SIMD_TARGET __attribute__((target("avx2,fma"), noinline))
	#define IMAGE_PROCESSING_SIMD_INLINE inline __attribute__((target("avx2,fma"), always_inline))
#elif IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_SIMD_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_SIMD_INLINE __forceinline
#else
	#define IMAGE_PROCESSING_SIMD_TARGET
	#define IMAGE_PROCESSING_SIMD_INLINE inline
#endif

#if IMAGE_PROCESSING_X64
	// SIMDe has no wrapper for this x86-only transition instruction.
	#define IMAGE_PROCESSING_CLEAR_AVX_UPPER_STATE() _mm256_zeroupper()
#else
	#define IMAGE_PROCESSING_CLEAR_AVX_UPPER_STATE() static_cast<void>(0)
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
}
