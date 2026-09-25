#pragma once

#include "compiler/compiler_warnings_control.h"

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
	// SIMDe uses the native intrinsics only when these are defined, and the baseline instruction set leaves them undefined.
	// The kernels are only called once canUseSimd() has confirmed AVX2 and FMA at runtime.
	#define SIMDE_X86_AVX2_NATIVE
	#define SIMDE_X86_FMA_NATIVE
#endif

#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	// GCC and Clang allow AVX2 intrinsics only in AVX2-enabled functions, so SIMDe's functions are compiled with AVX2 below.
	// SIMDe's own functions are static. The standard headers it includes come first: an inline function they define inside
	// the AVX2 region could be the copy the linker keeps for baseline callers.
	#include <cfloat>
	#include <cmath>
	#include <cstddef>
	#include <fenv.h>
	#include <limits.h>
	#include <stdint.h>
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>
	#include <type_traits>
#endif

#if IMAGE_PROCESSING_SIMD
	#if IMAGE_PROCESSING_X64 && defined(__clang__)
		#pragma clang attribute push(__attribute__((target("avx2,fma"))), apply_to = function)
	#elif IMAGE_PROCESSING_X64 && defined(__GNUC__)
		#pragma GCC push_options
		#pragma GCC target("avx2,fma")
	#endif

	// Angle brackets are load-bearing under GCC and Clang: SIMDe must come from the system include path resize.pri sets, and a quoted relative path resolves first.
	// The system path does not cover warnings GCC emits for SIMDe code inlined into ours.
	DISABLE_COMPILER_WARNINGS
	#include <simde/x86/avx2.h>
	#include <simde/x86/fma.h>
	RESTORE_COMPILER_WARNINGS

	#if IMAGE_PROCESSING_X64 && defined(__clang__)
		#pragma clang attribute pop
	#elif IMAGE_PROCESSING_X64 && defined(__GNUC__)
		#pragma GCC pop_options
	#endif
#endif

#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	#define IMAGE_PROCESSING_SIMD_TARGET __attribute__((target("avx2,fma"), noinline))
	#define IMAGE_PROCESSING_SIMD_INLINE inline __attribute__((target("avx2,fma"), always_inline))
#elif IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_SIMD_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_SIMD_INLINE __forceinline
#else
	#define IMAGE_PROCESSING_SIMD_TARGET
	// Forced as on x64: the kernel's per-column and per-pixel helpers must not become calls
	#if defined(_MSC_VER)
		#define IMAGE_PROCESSING_SIMD_INLINE __forceinline
	#elif defined(__GNUC__) || defined(__clang__)
		#define IMAGE_PROCESSING_SIMD_INLINE inline __attribute__((always_inline))
	#else
		#define IMAGE_PROCESSING_SIMD_INLINE inline
	#endif
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

	// SIMDe has no wrapper for this x86-only transition instruction, and GCC and Clang expose it only inside a
	// target-attributed function - hence a function rather than a macro, reached by a call from baseline callers.
	IMAGE_PROCESSING_SIMD_TARGET inline void clearAvxUpperState() noexcept
	{
#if IMAGE_PROCESSING_X64
		_mm256_zeroupper();
#endif
	}
}
