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
#elif defined(_MSC_VER) && !defined(__clang__)
	#include <intrin.h> // __prefetch
#endif

#include <array>
#include <stddef.h>
#include <stdint.h>

// The *_INLINE macros are forced: the kernels' per-column and per-pixel helpers must not become calls
#if defined(__GNUC__) || defined(__clang__)
	#define IMAGE_PROCESSING_FORCE_INLINE inline __attribute__((always_inline))
#else
	#define IMAGE_PROCESSING_FORCE_INLINE __forceinline
#endif

// IMAGE_PROCESSING_FORCE_INLINE_CALLS, ahead of a block: forces inlining of the calls written in it, standard library ones included.
// GCC has no per-block form: IMAGE_PROCESSING_FLATTEN on the kernel's entry function inlines every call in it instead.
// Clang off Windows takes the GCC form: Clang 18 and Apple clang 21 crash on the per-block attribute inside a template.
#if defined(__clang__) && defined(_WIN32)
	#define IMAGE_PROCESSING_FORCE_INLINE_CALLS [[clang::always_inline]]
	#define IMAGE_PROCESSING_FLATTEN
#elif defined(_MSC_VER)
	#define IMAGE_PROCESSING_FORCE_INLINE_CALLS [[msvc::forceinline_calls]]
	#define IMAGE_PROCESSING_FLATTEN
#else
	#define IMAGE_PROCESSING_FORCE_INLINE_CALLS
	#define IMAGE_PROCESSING_FLATTEN __attribute__((flatten))
#endif

// IMAGE_PROCESSING_SCALAR_LOOP, ahead of a loop: Clang neither vectorizes nor interleaves it
#if defined(__clang__)
	#define IMAGE_PROCESSING_SCALAR_LOOP _Pragma("clang loop vectorize(disable) interleave(disable)")
#else
	#define IMAGE_PROCESSING_SCALAR_LOOP
#endif

#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	// GCC and Clang allow intrinsics above the baseline only in functions with the matching target
	#define IMAGE_PROCESSING_AVX2_TARGET __attribute__((target("avx2,fma"), noinline))
	#define IMAGE_PROCESSING_AVX2_INLINE inline __attribute__((target("avx2,fma"), always_inline))
	#define IMAGE_PROCESSING_AVX_TARGET __attribute__((target("avx"), noinline))
	#define IMAGE_PROCESSING_AVX_INLINE inline __attribute__((target("avx"), always_inline))
	#define IMAGE_PROCESSING_SSE41_TARGET __attribute__((target("sse4.1"), noinline))
	#define IMAGE_PROCESSING_SSE41_INLINE inline __attribute__((target("sse4.1"), always_inline))
#elif IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_AVX2_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_AVX2_INLINE __forceinline
	#define IMAGE_PROCESSING_AVX_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_AVX_INLINE __forceinline
	#define IMAGE_PROCESSING_SSE41_TARGET __declspec(noinline)
	#define IMAGE_PROCESSING_SSE41_INLINE __forceinline
#elif IMAGE_PROCESSING_ARM64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_NEON_INLINE __forceinline
#elif IMAGE_PROCESSING_ARM64
	#define IMAGE_PROCESSING_NEON_INLINE inline __attribute__((always_inline))
#endif

namespace ImageProcessing::Detail
{
	// Where a kernel writes an output block. Destination: the image's rows, written once and not read back.
	enum class BlockTarget { Destination, Scratch };

	// MSVC itself: clang-cl defines _MSC_VER too
#if defined(_MSC_VER) && !defined(__clang__)
	inline constexpr bool compilerIsMsvc = true;
#else
	inline constexpr bool compilerIsMsvc = false;
#endif

	// 4 four-byte pixels' places in a 16-byte load, 0-3 each, as one byte of 2 bits per pixel: an index into fourPixelShuffleMasks
	[[nodiscard]] constexpr uint8_t fourPixelShufflePattern(const uint8_t* placesInLoad) noexcept
	{
		return static_cast<uint8_t>(placesInLoad[0] | placesInLoad[1] << 2 | placesInLoad[2] << 4 | placesInLoad[3] << 6);
	}

	// The byte shuffle mask of every fourPixelShufflePattern
	inline constexpr std::array<std::array<uint8_t, 16>, 256> fourPixelShuffleMasks = [] {
		std::array<std::array<uint8_t, 16>, 256> masks{};
		for (size_t pattern = 0; pattern < 256; ++pattern)
		{
			for (size_t pixel = 0; pixel < 4; ++pixel)
			{
				for (size_t byte = 0; byte < 4; ++byte)
					masks[pattern][pixel * 4 + byte] = static_cast<uint8_t>(((pattern >> (2 * pixel)) & 3) * 4 + byte);
			}
		}
		return masks;
	}();
}

namespace ImageProcessing::SimdSupport
{
#if IMAGE_PROCESSING_X64
	// A function, not a macro: GCC and Clang expose vzeroupper only inside a target-attributed function
	IMAGE_PROCESSING_AVX_TARGET inline void clearAvxUpperState() noexcept
	{
		_mm256_zeroupper();
	}
#else
	inline void clearAvxUpperState() noexcept {}
#endif

	// Requests the cache line at address for reading
	IMAGE_PROCESSING_FORCE_INLINE void prefetchLine(const void* address) noexcept
	{
#if defined(_MSC_VER) && !defined(__clang__) && IMAGE_PROCESSING_X64
		_mm_prefetch(static_cast<const char*>(address), _MM_HINT_T0);
#elif defined(_MSC_VER) && !defined(__clang__)
		__prefetch(address);
#else
		__builtin_prefetch(address);
#endif
	}
}
