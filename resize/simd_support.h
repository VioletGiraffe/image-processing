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

#if !IMAGE_PROCESSING_X64 && !IMAGE_PROCESSING_ARM64
	#error "The resizer runs on x64 with SSE4.1 or above, and on ARM64"
#endif

#if IMAGE_PROCESSING_X64
	#include <immintrin.h>
#endif

// The *_INLINE macros are forced: the kernels' per-column and per-pixel helpers must not become calls
#if defined(__GNUC__) || defined(__clang__)
	#define IMAGE_PROCESSING_FORCE_INLINE inline __attribute__((always_inline))
#else
	#define IMAGE_PROCESSING_FORCE_INLINE __forceinline
#endif

#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	// GCC and Clang allow intrinsics above the baseline only in functions with the matching target
	#define IMAGE_PROCESSING_AVX2_TARGET __attribute__((target("avx2,fma"), noinline))
	#define IMAGE_PROCESSING_AVX2_INLINE inline __attribute__((target("avx2,fma"), always_inline))
	#define IMAGE_PROCESSING_SSE41_TARGET __attribute__((target("sse4.1"), noinline))
	#define IMAGE_PROCESSING_SSE41_INLINE inline __attribute__((target("sse4.1"), always_inline))
#elif IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_AVX2_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_AVX2_INLINE __forceinline
	#define IMAGE_PROCESSING_SSE41_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_SSE41_INLINE __forceinline
#elif IMAGE_PROCESSING_ARM64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_NEON_INLINE __forceinline
#elif IMAGE_PROCESSING_ARM64
	#define IMAGE_PROCESSING_NEON_INLINE inline __attribute__((always_inline))
#endif

namespace ImageProcessing::SimdSupport
{
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
