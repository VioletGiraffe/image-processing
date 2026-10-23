// The multiply-adds of the resizer's two passes at the sizes of 4K -> 1080p RGB32, without the kernel's bookkeeping:
// the ceilings doc/throughput-ceiling.md compares the kernels against, and the horizontal forms it ranks.
// Standalone. AVX2 + FMA:
//   cl /O2 /MT /arch:AVX2 /QIntel-jcc-erratum /std:c++20 /EHsc multiply_add_ceiling.cpp
//   clang-cl /O2 /MT /arch:AVX2 -mfma /clang:-mbranches-within-32B-boundaries /std:c++20 /EHsc multiply_add_ceiling.cpp
//   g++ -O2 -mavx2 -mfma -std=c++20 multiply_add_ceiling.cpp
// The jump-alignment options are the kernels': without them lines differ by up to 14% between builds on the Skylake family.
// Without /arch:AVX2, or for ARM64 (g++ -O2 -std=c++20), only sections N, S and U exist: the 128-bit levels' horizontal form.
// Section U is built by g++ and clang++ only, not by cl or clang-cl.
// Run: multiply_add_ceiling [repeats, default 30] [sections, default HVTFCPRSNUY]; each line is the best repeat.
// Pinned to a logical processor by mask: start /b /wait /affinity 4 multiply_add_ceiling.exe
// Rates are float multiply-adds per second, counted as the kernel's are: 4 floats per horizontal tap, 3 per vertical tap.
#if defined(__aarch64__) || defined(_M_ARM64)
	#include <arm_neon.h>
	#define ARM_NEON
#else
	#include <immintrin.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <utility>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__)
	#define NOINLINE __declspec(noinline)
	#define FORCEINLINE __forceinline
#else
	#define NOINLINE __attribute__((noinline))
	#define FORCEINLINE inline __attribute__((always_inline))
#endif

// Hide a value from Clang's optimizer, as the kernels' spreadIndices does. With the value known, Clang rewrites:
//   a vpermps as vshufps + vpermpd; lane permutes of a loaded weight block as broadcasts from memory; two stepped pointers as one index.
// -DNO_CLANG_BARRIERS builds without them.
#if defined(__clang__) && !defined(NO_CLANG_BARRIERS)
	#define OPAQUE_VECTOR(v) __asm__ volatile("" : "+x"(v))
	#define OPAQUE_POINTER(p) __asm__ volatile("" : "+r"(p))
#else
	#define OPAQUE_VECTOR(v) (void)0
	#define OPAQUE_POINTER(p) (void)0
#endif

// Ahead of a loop: Clang neither vectorizes nor interleaves it, as the kernel's conversion tail
#if defined(__clang__)
	#define SCALAR_LOOP _Pragma("clang loop vectorize(disable) interleave(disable)")
#else
	#define SCALAR_LOOP
#endif

// Section U, g++ and clang++ only: a pointer hidden from the optimizer, and a vector kept live, without an instruction
#if defined(__GNUC__)
	#define HIDDEN_POINTER(p) __asm__ volatile("" : "+r"(p))
	#if defined(__aarch64__)
		#define USED_FLOATS4(v) __asm__ volatile("" : : "w"(v))
	#else
		#define USED_FLOATS4(v) __asm__ volatile("" : : "x"(v))
	#endif
#endif

namespace
{
	constexpr size_t sourceHeight = 2160, destWidth = 1920, destHeight = 1080, taps = 12;
	constexpr size_t rowPairs = sourceHeight / 2;

	float* alignedFloats(size_t count)
	{
#ifdef _WIN32
		float* p = static_cast<float*>(_aligned_malloc(count * sizeof(float), 64));
#else
		float* p = static_cast<float*>(std::aligned_alloc(64, (count * sizeof(float) + 63) / 64 * 64));
#endif
		for (size_t i = 0; i < count; ++i)
			p[i] = static_cast<float>((i * 37) & 255);
		return p;
	}

	void freeFloats(float* floats)
	{
#ifdef _WIN32
		_aligned_free(floats);
#else
		std::free(floats);
#endif
	}

	template <typename F>
	double bestSeconds(F&& work, int repeats)
	{
		double best = 1e30;
		for (int r = 0; r < repeats; ++r)
		{
			const auto start = std::chrono::steady_clock::now();
			work();
			best = std::min(best, std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
		}
		return best;
	}

	// The floats from one run's weights to the next: the kernel's builder starts every run at a multiple of 8 in an array on a 64-byte boundary
	constexpr size_t weightStrideFor(size_t maxTaps) { return (maxTaps + 7) / 8 * 8; }

	// A column's run, as the kernel's TapRun
	struct ColumnRun
	{
		size_t firstSource;
		const float* weights;
		size_t weightCount;
	};

	// A group of three columns as a builder would record it, 16 bytes.
	// pattern: the starts' steps, delta1 + 2 * delta2, each 0 or 1; 4 - steps of any other size, the group's columns read from the run table.
	// weights: the three columns' 4 weights, side by side.
	struct GroupRun
	{
		uint32_t firstSource;
		uint32_t pattern;
		const float* weights;
	};

	// The group's pattern of starts from its three runs
	uint32_t groupPattern(const ColumnRun* run)
	{
		// Unsigned: a start before the previous one is a large step
		const size_t delta1 = run[1].firstSource - run[0].firstSource, delta2 = run[2].firstSource - run[1].firstSource;
		return (delta1 | delta2) > 1 ? 4 : static_cast<uint32_t>(delta1 + 2 * delta2);
	}

	// Every column's tap count: maxTaps; mixed - maxTaps or one fewer, unpredictably
	std::vector<uint32_t> columnTapCounts(size_t columns, size_t maxTaps, bool mixed)
	{
		std::vector<uint32_t> tapCounts(columns, static_cast<uint32_t>(maxTaps));
		if (mixed)
		{
			for (size_t column = 0; column < columns; ++column)
			{
				if (((column * 2654435761u) >> 13 & 1) != 0)
					--tapCounts[column];
			}
		}
		return tapCounts;
	}

#ifdef __AVX2__
	inline __m128 sumHalves(__m256 v) { return _mm_add_ps(_mm256_castps256_ps128(v), _mm256_extractf128_ps(v, 1)); }

	// One row pair, every column: 12 taps of 4-float pixels, a block of 4 chains and a 16-float step, as filterRun does.
	// Weights: 0 - loop-invariant registers; 1 - loaded per column and spread by vpermps.
	template <int Weights>
	NOINLINE void horizontalRowPair(const float* a, const float* b, const float* columnWeights, float* outA, float* outB)
	{
		const __m256i i0 = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1), i1 = _mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3);
		const __m256i i2 = _mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5), i3 = _mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);
		__m256 w0 = _mm256_set1_ps(0.1f), w1 = _mm256_set1_ps(0.2f), w2 = _mm256_set1_ps(0.3f), w3 = _mm256_set1_ps(0.15f), w4 = _mm256_set1_ps(0.05f), w5 = _mm256_set1_ps(0.2f);
		// The source span slides 2 pixels per column inside a buffer of 160 pixels, in L1 as in the kernel
		size_t offset = 0;
		for (size_t column = 0; column < destWidth; ++column)
		{
			if constexpr (Weights == 1)
			{
				const __m256 first = _mm256_loadu_ps(columnWeights + column * taps), second = _mm256_loadu_ps(columnWeights + column * taps + 8);
				w0 = _mm256_permutevar8x32_ps(first, i0); w1 = _mm256_permutevar8x32_ps(first, i1);
				w2 = _mm256_permutevar8x32_ps(first, i2); w3 = _mm256_permutevar8x32_ps(first, i3);
				w4 = _mm256_permutevar8x32_ps(second, i0); w5 = _mm256_permutevar8x32_ps(second, i1);
			}

			const float* pa = a + offset;
			const float* pb = b + offset;
			__m256 a0 = _mm256_mul_ps(_mm256_loadu_ps(pa), w0), a1 = _mm256_mul_ps(_mm256_loadu_ps(pa + 8), w1);
			__m256 a2 = _mm256_mul_ps(_mm256_loadu_ps(pa + 16), w2), a3 = _mm256_mul_ps(_mm256_loadu_ps(pa + 24), w3);
			__m256 b0 = _mm256_mul_ps(_mm256_loadu_ps(pb), w0), b1 = _mm256_mul_ps(_mm256_loadu_ps(pb + 8), w1);
			__m256 b2 = _mm256_mul_ps(_mm256_loadu_ps(pb + 16), w2), b3 = _mm256_mul_ps(_mm256_loadu_ps(pb + 24), w3);
			__m256 sumA = _mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3));
			__m256 sumB = _mm256_add_ps(_mm256_add_ps(b0, b1), _mm256_add_ps(b2, b3));
			sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 40), w5, sumA); sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 32), w4, sumA);
			sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 40), w5, sumB); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 32), w4, sumB);
			// 3 floats per temp pixel; the 4th is overwritten by the next column
			_mm_storeu_ps(outA + column * 3, sumHalves(sumA));
			_mm_storeu_ps(outB + column * 3, sumHalves(sumB));

			offset += 8;
			if (offset + taps * 4 > 160 * 4)
				offset = 0;
		}
	}

	// As horizontalRowPair with the weights loaded and spread, the 6 products per row accumulated through Chains chains (1 or 2)
	template <int Chains>
	NOINLINE void horizontalRowPairChains(const float* a, const float* b, const float* columnWeights, float* outA, float* outB)
	{
		const __m256i i0 = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1), i1 = _mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3);
		const __m256i i2 = _mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5), i3 = _mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);
		size_t offset = 0;
		for (size_t column = 0; column < destWidth; ++column)
		{
			const __m256 first = _mm256_loadu_ps(columnWeights + column * taps), second = _mm256_loadu_ps(columnWeights + column * taps + 8);
			const __m256 w0 = _mm256_permutevar8x32_ps(first, i0), w1 = _mm256_permutevar8x32_ps(first, i1);
			const __m256 w2 = _mm256_permutevar8x32_ps(first, i2), w3 = _mm256_permutevar8x32_ps(first, i3);
			const __m256 w4 = _mm256_permutevar8x32_ps(second, i0), w5 = _mm256_permutevar8x32_ps(second, i1);
			const float* pa = a + offset;
			const float* pb = b + offset;
			__m256 sumA, sumB;
			if constexpr (Chains == 1)
			{
				sumA = _mm256_mul_ps(_mm256_loadu_ps(pa), w0); sumB = _mm256_mul_ps(_mm256_loadu_ps(pb), w0);
				sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 8), w1, sumA); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 8), w1, sumB);
				sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 16), w2, sumA); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 16), w2, sumB);
				sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 24), w3, sumA); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 24), w3, sumB);
				sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 32), w4, sumA); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 32), w4, sumB);
				sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 40), w5, sumA); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 40), w5, sumB);
			}
			else
			{
				__m256 a0 = _mm256_mul_ps(_mm256_loadu_ps(pa), w0), a1 = _mm256_mul_ps(_mm256_loadu_ps(pa + 8), w1);
				__m256 b0 = _mm256_mul_ps(_mm256_loadu_ps(pb), w0), b1 = _mm256_mul_ps(_mm256_loadu_ps(pb + 8), w1);
				a0 = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 16), w2, a0); a1 = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 24), w3, a1);
				b0 = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 16), w2, b0); b1 = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 24), w3, b1);
				a0 = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 32), w4, a0); a1 = _mm256_fmadd_ps(_mm256_loadu_ps(pa + 40), w5, a1);
				b0 = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 32), w4, b0); b1 = _mm256_fmadd_ps(_mm256_loadu_ps(pb + 40), w5, b1);
				sumA = _mm256_add_ps(a0, a1); sumB = _mm256_add_ps(b0, b1);
			}
			_mm_storeu_ps(outA + column * 3, sumHalves(sumA));
			_mm_storeu_ps(outB + column * 3, sumHalves(sumB));

			offset += 8;
			if (offset + taps * 4 > 160 * 4)
				offset = 0;
		}
	}

	// One tap per multiply-add for both rows: row A's pixel in the low lane, row B's in the high one. No spread across pixels, no sum of halves.
	// Layout: 0 - separate row buffers, the pair assembled by an insert; 1 - one buffer with the rows' pixels interleaved, a pixel pair per load.
	// Broadcast: 0 - 4 weights loaded into both lanes, each tap's picked by an in-lane permute; 1 - each tap's weight broadcast from memory.
	template <int Layout, int Broadcast>
	NOINLINE void horizontalRowPairPerTap(const float* a, const float* b, const float* interleaved, const float* columnWeights, float* outA, float* outB)
	{
		size_t offset = 0;
		for (size_t column = 0; column < destWidth; ++column)
		{
			const float* w = columnWeights + column * taps;
			const auto pixels = [&](size_t tap) {
				if constexpr (Layout == 1)
					return _mm256_loadu_ps(interleaved + offset * 2 + tap * 8);
				else
					return _mm256_insertf128_ps(_mm256_castps128_ps256(_mm_loadu_ps(a + offset + tap * 4)), _mm_loadu_ps(b + offset + tap * 4), 1);
			};
			__m256 s0, s1, s2;
			if constexpr (Broadcast == 1)
			{
				s0 = _mm256_mul_ps(pixels(0), _mm256_broadcast_ss(w)); s1 = _mm256_mul_ps(pixels(1), _mm256_broadcast_ss(w + 1)); s2 = _mm256_mul_ps(pixels(2), _mm256_broadcast_ss(w + 2));
				s0 = _mm256_fmadd_ps(pixels(3), _mm256_broadcast_ss(w + 3), s0); s1 = _mm256_fmadd_ps(pixels(4), _mm256_broadcast_ss(w + 4), s1); s2 = _mm256_fmadd_ps(pixels(5), _mm256_broadcast_ss(w + 5), s2);
				s0 = _mm256_fmadd_ps(pixels(6), _mm256_broadcast_ss(w + 6), s0); s1 = _mm256_fmadd_ps(pixels(7), _mm256_broadcast_ss(w + 7), s1); s2 = _mm256_fmadd_ps(pixels(8), _mm256_broadcast_ss(w + 8), s2);
				s0 = _mm256_fmadd_ps(pixels(9), _mm256_broadcast_ss(w + 9), s0); s1 = _mm256_fmadd_ps(pixels(10), _mm256_broadcast_ss(w + 10), s1); s2 = _mm256_fmadd_ps(pixels(11), _mm256_broadcast_ss(w + 11), s2);
			}
			else
			{
				const __m256 w0 = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w)), w1 = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w + 4)), w2 = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w + 8));
				s0 = _mm256_mul_ps(pixels(0), _mm256_shuffle_ps(w0, w0, 0x00)); s1 = _mm256_mul_ps(pixels(1), _mm256_shuffle_ps(w0, w0, 0x55)); s2 = _mm256_mul_ps(pixels(2), _mm256_shuffle_ps(w0, w0, 0xAA));
				s0 = _mm256_fmadd_ps(pixels(3), _mm256_shuffle_ps(w0, w0, 0xFF), s0); s1 = _mm256_fmadd_ps(pixels(4), _mm256_shuffle_ps(w1, w1, 0x00), s1); s2 = _mm256_fmadd_ps(pixels(5), _mm256_shuffle_ps(w1, w1, 0x55), s2);
				s0 = _mm256_fmadd_ps(pixels(6), _mm256_shuffle_ps(w1, w1, 0xAA), s0); s1 = _mm256_fmadd_ps(pixels(7), _mm256_shuffle_ps(w1, w1, 0xFF), s1); s2 = _mm256_fmadd_ps(pixels(8), _mm256_shuffle_ps(w2, w2, 0x00), s2);
				s0 = _mm256_fmadd_ps(pixels(9), _mm256_shuffle_ps(w2, w2, 0x55), s0); s1 = _mm256_fmadd_ps(pixels(10), _mm256_shuffle_ps(w2, w2, 0xAA), s1); s2 = _mm256_fmadd_ps(pixels(11), _mm256_shuffle_ps(w2, w2, 0xFF), s2);
			}
			const __m256 sum = _mm256_add_ps(_mm256_add_ps(s0, s1), s2);
			_mm_storeu_ps(outA + column * 3, _mm256_castps256_ps128(sum));
			_mm_storeu_ps(outB + column * 3, _mm256_extractf128_ps(sum, 1));

			offset += 8;
			if (offset + taps * 4 > 160 * 4)
				offset = 0;
		}
	}

	struct SpreadIndices
	{
		SpreadIndices() { OPAQUE_VECTOR(i0); OPAQUE_VECTOR(i1); OPAQUE_VECTOR(i2); OPAQUE_VECTOR(i3); }

		__m256i i0 = _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1), i1 = _mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3);
		__m256i i2 = _mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5), i3 = _mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);
	};

	// kernelFormColumn's taps from tap on, under 8 of them, added to the blocks' sums: filterRun's steps of 4 taps, 2 taps and a single tap
	FORCEINLINE void kernelFormLastTaps(const SpreadIndices& indices, const float* pa, const float* pb, const float* w, size_t tap, size_t tapCount,
		__m256 sumA, __m256 sumB, float* outA, float* outB)
	{
		if (tap + 4 <= tapCount)
		{
			const __m256 weights = _mm256_loadu_ps(w + tap);
			const __m256 w0 = _mm256_permutevar8x32_ps(weights, indices.i0), w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
			sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + tap * 4 + 8), w1, sumA); sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + tap * 4), w0, sumA);
			sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + tap * 4 + 8), w1, sumB); sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + tap * 4), w0, sumB);
			tap += 4;
		}

		if (tap + 2 <= tapCount)
		{
			const __m256 w0 = _mm256_permutevar8x32_ps(_mm256_loadu_ps(w + tap), indices.i0);
			sumA = _mm256_fmadd_ps(_mm256_loadu_ps(pa + tap * 4), w0, sumA);
			sumB = _mm256_fmadd_ps(_mm256_loadu_ps(pb + tap * 4), w0, sumB);
			tap += 2;
		}

		__m128 pixelA = sumHalves(sumA), pixelB = sumHalves(sumB);
		// One tap at most is left
		if (tap < tapCount)
		{
			const __m128 weight = _mm_broadcast_ss(w + tap);
			pixelA = _mm_fmadd_ps(_mm_loadu_ps(pa + tap * 4), weight, pixelA);
			pixelB = _mm_fmadd_ps(_mm_loadu_ps(pb + tap * 4), weight, pixelB);
		}
		_mm_storeu_ps(outA, pixelA);
		_mm_storeu_ps(outB, pixelB);
	}

	// Blocks Block and later of kernelFormColumnFixed: 8 taps in 4 chains per row, the first block starting the chains.
	// Written out per block: MSVC keeps a loop over a known count as a loop.
	template <size_t Blocks, size_t Block = 0>
	FORCEINLINE void addFixedBlocks(const SpreadIndices& indices, const float* pa, const float* pb, const float* w,
		__m256& a0, __m256& a1, __m256& a2, __m256& a3, __m256& b0, __m256& b1, __m256& b2, __m256& b3)
	{
		if constexpr (Block < Blocks)
		{
			const __m256 weights = _mm256_loadu_ps(w + Block * 8);
			const __m256 w0 = _mm256_permutevar8x32_ps(weights, indices.i0), w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
			const __m256 w2 = _mm256_permutevar8x32_ps(weights, indices.i2), w3 = _mm256_permutevar8x32_ps(weights, indices.i3);
			const float* blockA = pa + Block * 32;
			const float* blockB = pb + Block * 32;
			if constexpr (Block == 0)
			{
				a0 = _mm256_mul_ps(_mm256_loadu_ps(blockA), w0); a1 = _mm256_mul_ps(_mm256_loadu_ps(blockA + 8), w1);
				a2 = _mm256_mul_ps(_mm256_loadu_ps(blockA + 16), w2); a3 = _mm256_mul_ps(_mm256_loadu_ps(blockA + 24), w3);
				b0 = _mm256_mul_ps(_mm256_loadu_ps(blockB), w0); b1 = _mm256_mul_ps(_mm256_loadu_ps(blockB + 8), w1);
				b2 = _mm256_mul_ps(_mm256_loadu_ps(blockB + 16), w2); b3 = _mm256_mul_ps(_mm256_loadu_ps(blockB + 24), w3);
			}
			else
			{
				a0 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA), w0, a0); a1 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 8), w1, a1);
				a2 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 16), w2, a2); a3 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 24), w3, a3);
				b0 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB), w0, b0); b1 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 8), w1, b1);
				b2 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 16), w2, b2); b3 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 24), w3, b3);
			}

			addFixedBlocks<Blocks, Block + 1>(indices, pa, pb, w, a0, a1, a2, a3, b0, b1, b2, b3);
		}
	}

	// kernelFormColumn for a tap count known to the compiler: no block loop, and of the steps only those the count takes
	template <size_t Taps>
	FORCEINLINE void kernelFormColumnFixed(const SpreadIndices& indices, const float* pa, const float* pb, const float* w, float* outA, float* outB)
	{
		__m256 sumA = _mm256_setzero_ps(), sumB = _mm256_setzero_ps();
		if constexpr (Taps >= 8)
		{
			__m256 a0, a1, a2, a3, b0, b1, b2, b3;
			addFixedBlocks<Taps / 8>(indices, pa, pb, w, a0, a1, a2, a3, b0, b1, b2, b3);
			sumA = _mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3));
			sumB = _mm256_add_ps(_mm256_add_ps(b0, b1), _mm256_add_ps(b2, b3));
		}

		kernelFormLastTaps(indices, pa, pb, w, Taps / 8 * 8, Taps, sumA, sumB, outA, outB);
	}

	// One column of a row pair in the kernel's form, with filterRun's steps for 4-float pixels: blocks of 8 taps in 4 chains, 4 taps, 2 taps, single taps.
	// Requires 8 readable floats at every weight.
	FORCEINLINE void kernelFormColumn(const SpreadIndices& indices, const float* pa, const float* pb, const float* w, size_t tapCount, float* outA, float* outB)
	{
		__m256 sumA = _mm256_setzero_ps(), sumB = _mm256_setzero_ps();
		size_t tap = 0;
		if (tapCount >= 8)
		{
			__m256 weights = _mm256_loadu_ps(w);
			__m256 w0 = _mm256_permutevar8x32_ps(weights, indices.i0), w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
			__m256 w2 = _mm256_permutevar8x32_ps(weights, indices.i2), w3 = _mm256_permutevar8x32_ps(weights, indices.i3);
			__m256 a0 = _mm256_mul_ps(_mm256_loadu_ps(pa), w0), a1 = _mm256_mul_ps(_mm256_loadu_ps(pa + 8), w1);
			__m256 a2 = _mm256_mul_ps(_mm256_loadu_ps(pa + 16), w2), a3 = _mm256_mul_ps(_mm256_loadu_ps(pa + 24), w3);
			__m256 b0 = _mm256_mul_ps(_mm256_loadu_ps(pb), w0), b1 = _mm256_mul_ps(_mm256_loadu_ps(pb + 8), w1);
			__m256 b2 = _mm256_mul_ps(_mm256_loadu_ps(pb + 16), w2), b3 = _mm256_mul_ps(_mm256_loadu_ps(pb + 24), w3);
			// Stepped pointers beside the tap count, as the kernel's block loop
			const float* blockWeights = w + 8;
			const float* blockA = pa + 32;
			const float* blockB = pb + 32;
			for (tap = 8; tap + 8 <= tapCount; tap += 8)
			{
				weights = _mm256_loadu_ps(blockWeights);
				w0 = _mm256_permutevar8x32_ps(weights, indices.i0); w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
				w2 = _mm256_permutevar8x32_ps(weights, indices.i2); w3 = _mm256_permutevar8x32_ps(weights, indices.i3);
				a0 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA), w0, a0); a1 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 8), w1, a1);
				a2 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 16), w2, a2); a3 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 24), w3, a3);
				b0 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB), w0, b0); b1 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 8), w1, b1);
				b2 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 16), w2, b2); b3 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 24), w3, b3);
				blockWeights += 8;
				blockA += 32;
				blockB += 32;
			}
			sumA = _mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3));
			sumB = _mm256_add_ps(_mm256_add_ps(b0, b1), _mm256_add_ps(b2, b3));
		}

		kernelFormLastTaps(indices, pa, pb, w, tap, tapCount, sumA, sumB, outA, outB);
	}

	// One column of a row pair, a tap per multiply-add over interleaved pixels, each weight broadcast from memory.
	// Chains (2, 3 or 4) chains over whole groups of Chains taps; the remaining taps are added to the chains' sum.
	template <int Chains>
	FORCEINLINE void interleavedColumn(const float* p, const float* w, size_t tapCount, float* outA, float* outB)
	{
		// Taps by pointer: indexed, each costs MSVC a shift for the pixel pair's 32 bytes
		const auto product = [](const float* pixels, const float* weight) { return _mm256_mul_ps(_mm256_loadu_ps(pixels), _mm256_broadcast_ss(weight)); };
		const auto addTap = [](const float* pixels, const float* weight, __m256 sum) { return _mm256_fmadd_ps(_mm256_loadu_ps(pixels), _mm256_broadcast_ss(weight), sum); };
		__m256 sum = _mm256_setzero_ps();
		const float* const weightsEnd = w + tapCount;
		if (tapCount >= Chains)
		{
			__m256 s0 = product(p, w), s1 = product(p + 8, w + 1), s2 = _mm256_setzero_ps(), s3 = _mm256_setzero_ps();
			if constexpr (Chains >= 3)
				s2 = product(p + 16, w + 2);
			if constexpr (Chains == 4)
				s3 = product(p + 24, w + 3);

			const float* const groupsEnd = w + tapCount / Chains * Chains;
			for (p += Chains * 8, w += Chains; w != groupsEnd; p += Chains * 8, w += Chains)
			{
				OPAQUE_POINTER(p);
				s0 = addTap(p, w, s0); s1 = addTap(p + 8, w + 1, s1);
				if constexpr (Chains >= 3)
					s2 = addTap(p + 16, w + 2, s2);
				if constexpr (Chains == 4)
					s3 = addTap(p + 24, w + 3, s3);
			}

			sum = _mm256_add_ps(s0, s1);
			if constexpr (Chains == 3)
				sum = _mm256_add_ps(sum, s2);
			if constexpr (Chains == 4)
				sum = _mm256_add_ps(sum, _mm256_add_ps(s2, s3));
		}

		for (; w < weightsEnd; p += 8, ++w)
			sum = addTap(p, w, sum);

		_mm_storeu_ps(outA, _mm256_castps256_ps128(sum));
		_mm_storeu_ps(outB, _mm256_extractf128_ps(sum, 1));
	}

	// The weights of 4 taps from w on, each for a pixel pair's 8 floats.
	// Weights: 0 - the 4 loaded once into both lanes, a tap's permuted in-lane; 1 - a tap's broadcast from memory.
	// The in-lane permutes of this file are _mm256_shuffle_ps of a register with itself, not _mm256_permute_ps: MSVC emits vpermilps for
	// that one, which a 12600K P-core runs at one per cycle, where vshufps runs faster. Clang emits vshufps for either.
	template <int Weights>
	struct PairWeights
	{
		explicit PairWeights(const float* w) : weights(w)
		{
			if constexpr (Weights == 0)
			{
				loaded = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w));
				OPAQUE_VECTOR(loaded);
			}
		}

		template <int Tap>
		__m256 tap() const
		{
			if constexpr (Weights == 1)
				return _mm256_broadcast_ss(weights + Tap);
			else
				return _mm256_shuffle_ps(loaded, loaded, Tap * 0x55);
		}

		const float* weights;
		__m256 loaded = _mm256_setzero_ps();
	};

	// interleavedColumn<4> with PairWeights<0>. Requires 4 readable floats at every weight.
	FORCEINLINE void interleavedColumnLaneWeights(const float* p, const float* w, size_t tapCount, float* outA, float* outB)
	{
		__m256 sum = _mm256_setzero_ps();
		const float* const weightsEnd = w + tapCount;
		if (tapCount >= 4)
		{
			const PairWeights<0> firstWeights{ w };
			__m256 s0 = _mm256_mul_ps(_mm256_loadu_ps(p), firstWeights.tap<0>()), s1 = _mm256_mul_ps(_mm256_loadu_ps(p + 8), firstWeights.tap<1>());
			__m256 s2 = _mm256_mul_ps(_mm256_loadu_ps(p + 16), firstWeights.tap<2>()), s3 = _mm256_mul_ps(_mm256_loadu_ps(p + 24), firstWeights.tap<3>());
			const float* const groupsEnd = w + (tapCount & ~size_t{ 3 });
			for (p += 32, w += 4; w != groupsEnd; p += 32, w += 4)
			{
				OPAQUE_POINTER(p);
				const PairWeights<0> weights{ w };
				s0 = _mm256_fmadd_ps(_mm256_loadu_ps(p), weights.tap<0>(), s0); s1 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 8), weights.tap<1>(), s1);
				s2 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 16), weights.tap<2>(), s2); s3 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 24), weights.tap<3>(), s3);
			}
			sum = _mm256_add_ps(_mm256_add_ps(s0, s1), _mm256_add_ps(s2, s3));
		}

		for (; w < weightsEnd; p += 8, ++w)
			sum = _mm256_fmadd_ps(_mm256_loadu_ps(p), _mm256_broadcast_ss(w), sum);

		_mm_storeu_ps(outA, _mm256_castps256_ps128(sum));
		_mm_storeu_ps(outB, _mm256_extractf128_ps(sum, 1));
	}

	// As interleavedColumn, with kernelFormColumn's steps in place of the tap loop: blocks of 8 taps in 4 chains, 4 taps in 2 chains, 2 taps, 1 tap.
	// Weights: PairWeights'. Requires 4 readable floats at every weight.
	template <int Weights>
	FORCEINLINE void interleavedColumnInSteps(const float* p, const float* w, size_t tapCount, float* outA, float* outB)
	{
		const auto product = [](const float* pixels, __m256 weight) { return _mm256_mul_ps(_mm256_loadu_ps(pixels), weight); };
		const auto addTap = [](const float* pixels, __m256 weight, __m256 sum) { return _mm256_fmadd_ps(_mm256_loadu_ps(pixels), weight, sum); };
		__m256 sum = _mm256_setzero_ps();
		const float* const weightsEnd = w + tapCount;
		if (tapCount >= 8)
		{
			const PairWeights<Weights> firstLow{ w }, firstHigh{ w + 4 };
			__m256 s0 = product(p, firstLow.template tap<0>()), s1 = product(p + 8, firstLow.template tap<1>());
			__m256 s2 = product(p + 16, firstLow.template tap<2>()), s3 = product(p + 24, firstLow.template tap<3>());
			s0 = addTap(p + 32, firstHigh.template tap<0>(), s0); s1 = addTap(p + 40, firstHigh.template tap<1>(), s1);
			s2 = addTap(p + 48, firstHigh.template tap<2>(), s2); s3 = addTap(p + 56, firstHigh.template tap<3>(), s3);
			const float* const blocksEnd = w + (tapCount & ~size_t{ 7 });
			for (p += 64, w += 8; w != blocksEnd; p += 64, w += 8)
			{
				OPAQUE_POINTER(p);
				const PairWeights<Weights> low{ w }, high{ w + 4 };
				s0 = addTap(p, low.template tap<0>(), s0); s1 = addTap(p + 8, low.template tap<1>(), s1);
				s2 = addTap(p + 16, low.template tap<2>(), s2); s3 = addTap(p + 24, low.template tap<3>(), s3);
				s0 = addTap(p + 32, high.template tap<0>(), s0); s1 = addTap(p + 40, high.template tap<1>(), s1);
				s2 = addTap(p + 48, high.template tap<2>(), s2); s3 = addTap(p + 56, high.template tap<3>(), s3);
			}
			sum = _mm256_add_ps(_mm256_add_ps(s0, s1), _mm256_add_ps(s2, s3));
		}

		if (w + 4 <= weightsEnd)
		{
			const PairWeights<Weights> weights{ w };
			__m256 second = product(p + 8, weights.template tap<1>());
			sum = addTap(p, weights.template tap<0>(), sum);
			second = addTap(p + 24, weights.template tap<3>(), second);
			sum = addTap(p + 16, weights.template tap<2>(), sum);
			sum = _mm256_add_ps(sum, second);
			p += 32; w += 4;
		}

		if (w + 2 <= weightsEnd)
		{
			const PairWeights<Weights> weights{ w };
			sum = _mm256_add_ps(addTap(p, weights.template tap<0>(), sum), product(p + 8, weights.template tap<1>()));
			p += 16; w += 2;
		}

		if (w < weightsEnd)
			sum = addTap(p, _mm256_broadcast_ss(w), sum);

		_mm_storeu_ps(outA, _mm256_castps256_ps128(sum));
		_mm_storeu_ps(outB, _mm256_extractf128_ps(sum, 1));
	}

	// Taps Tap and later of interleavedColumnFixed: tap i goes to chain i % 4, the first four start the chains.
	// Written out per tap: MSVC keeps a loop over a known count as a loop.
	template <size_t Taps, int Weights, size_t Tap = 0>
	FORCEINLINE void addFixedTaps(const float* p, const float* w, __m256& s0, __m256& s1, __m256& s2, __m256& s3, __m256 lanes = _mm256_setzero_ps())
	{
		if constexpr (Tap < Taps)
		{
			__m256 weight;
			if constexpr (Weights == 1)
				weight = _mm256_broadcast_ss(w + Tap);
			else
			{
				if constexpr (Tap % 4 == 0)
				{
					lanes = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w + Tap));
					OPAQUE_VECTOR(lanes);
				}
				weight = _mm256_shuffle_ps(lanes, lanes, (Tap % 4) * 0x55);
			}

			__m256& chain = Tap % 4 == 0 ? s0 : Tap % 4 == 1 ? s1 : Tap % 4 == 2 ? s2 : s3;
			const __m256 pixels = _mm256_loadu_ps(p + Tap * 8);
			if constexpr (Tap < 4)
				chain = _mm256_mul_ps(pixels, weight);
			else
				chain = _mm256_fmadd_ps(pixels, weight, chain);

			addFixedTaps<Taps, Weights, Tap + 1>(p, w, s0, s1, s2, s3, lanes);
		}
	}

	// One column of a row pair over interleaved pixels, the tap count known to the compiler: 4 chains, no loop, no steps.
	// Weights: PairWeights'. Requires 4 to 32 taps, and 4 readable floats at every weight.
	template <size_t Taps, int Weights>
	FORCEINLINE void interleavedColumnFixed(const float* p, const float* w, float* outA, float* outB)
	{
		static_assert(Taps >= 4 && Taps <= 32);
		__m256 s0, s1, s2, s3;
		addFixedTaps<Taps, Weights>(p, w, s0, s1, s2, s3);
		const __m256 sum = _mm256_add_ps(_mm256_add_ps(s0, s1), _mm256_add_ps(s2, s3));
		_mm_storeu_ps(outA, _mm256_castps256_ps128(sum));
		_mm_storeu_ps(outB, _mm256_extractf128_ps(sum, 1));
	}

	// Form: 2, 3, 4 - interleavedColumn<Form>; 14 - interleavedColumnLaneWeights; 8, 18 - interleavedColumnInSteps, the weights broadcast or from lanes
	template <int Form>
	FORCEINLINE void interleavedFormColumn(const float* p, const float* w, size_t tapCount, float* outA, float* outB)
	{
		if constexpr (Form == 8 || Form == 18)
			interleavedColumnInSteps<Form == 8 ? 1 : 0>(p, w, tapCount, outA, outB);
		else if constexpr (Form == 14)
			interleavedColumnLaneWeights(p, w, tapCount, outA, outB);
		else
			interleavedColumn<Form>(p, w, tapCount, outA, outB);
	}

	// One row pair, every column, the tap count read per column. The source span advances stepQ8 / 256 pixels per column inside capacityPixels.
	// Form: 0 - kernelFormColumn; otherwise interleavedFormColumn<Form>.
	template <int Form>
	NOINLINE void horizontalRowPairRuns(const float* a, const float* b, const float* interleaved, const float* columnWeights, const uint32_t* tapCounts,
		size_t maxTaps, size_t stepQ8, size_t capacityPixels, float* outA, float* outB)
	{
		const size_t weightStride = weightStrideFor(maxTaps);
		[[maybe_unused]] const SpreadIndices indices;
		size_t positionQ8 = 0;
		// Stepped per column, not multiplied by it: the kernel has no multiply per run
		const float* w = columnWeights;
		for (size_t column = 0; column < destWidth; ++column, w += weightStride, outA += 3, outB += 3)
		{
			const size_t tapCount = tapCounts[column];
			const size_t pixel = positionQ8 >> 8;
			if constexpr (Form == 0)
				kernelFormColumn(indices, a + pixel * 4, b + pixel * 4, w, tapCount, outA, outB);
			else
				interleavedFormColumn<Form>(interleaved + pixel * 8, w, tapCount, outA, outB);

			positionQ8 += stepQ8;
			if ((positionQ8 >> 8) + maxTaps > capacityPixels)
				positionQ8 = 0;
		}
	}

	// One column of an upscale's row pair: 4 taps of 4-float pixels, no tap count. Requires 8 readable floats at w.
	// Form: 0 - the kernel's filterRun at 4 taps: the weights spread over a pixel pair, halves summed, the rows at pa and pb;
	//       1, 2 - a tap per multiply-add over the interleaved pixels at p in Form chains, each weight broadcast from memory;
	//       3, 4 - as 2 and 1, the 4 weights loaded into both lanes and shuffled in-lane;
	//       5 - interleavedColumnFixed at 4 taps, lane weights: a chain per tap.
	template <int Form>
	FORCEINLINE void fourTapColumn(const SpreadIndices& indices, const float* pa, const float* pb, const float* p, const float* w, float* outA, float* outB)
	{
		if constexpr (Form == 5)
			interleavedColumnFixed<4, 0>(p, w, outA, outB);
		else if constexpr (Form == 0)
		{
			const __m256 weights = _mm256_loadu_ps(w);
			const __m256 w0 = _mm256_permutevar8x32_ps(weights, indices.i0), w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
			_mm_storeu_ps(outA, sumHalves(_mm256_fmadd_ps(_mm256_loadu_ps(pa), w0, _mm256_mul_ps(_mm256_loadu_ps(pa + 8), w1))));
			_mm_storeu_ps(outB, sumHalves(_mm256_fmadd_ps(_mm256_loadu_ps(pb), w0, _mm256_mul_ps(_mm256_loadu_ps(pb + 8), w1))));
		}
		else
		{
			__m256 sum;
			if constexpr (Form == 1)
			{
				sum = _mm256_mul_ps(_mm256_loadu_ps(p), _mm256_broadcast_ss(w));
				sum = _mm256_fmadd_ps(_mm256_loadu_ps(p + 8), _mm256_broadcast_ss(w + 1), sum);
				sum = _mm256_fmadd_ps(_mm256_loadu_ps(p + 16), _mm256_broadcast_ss(w + 2), sum);
				sum = _mm256_fmadd_ps(_mm256_loadu_ps(p + 24), _mm256_broadcast_ss(w + 3), sum);
			}
			else if constexpr (Form == 2)
			{
				__m256 s0 = _mm256_mul_ps(_mm256_loadu_ps(p), _mm256_broadcast_ss(w)), s1 = _mm256_mul_ps(_mm256_loadu_ps(p + 8), _mm256_broadcast_ss(w + 1));
				s0 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 16), _mm256_broadcast_ss(w + 2), s0); s1 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 24), _mm256_broadcast_ss(w + 3), s1);
				sum = _mm256_add_ps(s0, s1);
			}
			else
			{
				__m256 weights = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w));
				OPAQUE_VECTOR(weights);
				if constexpr (Form == 3)
				{
					__m256 s0 = _mm256_mul_ps(_mm256_loadu_ps(p), _mm256_shuffle_ps(weights, weights, 0x00)), s1 = _mm256_mul_ps(_mm256_loadu_ps(p + 8), _mm256_shuffle_ps(weights, weights, 0x55));
					s0 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 16), _mm256_shuffle_ps(weights, weights, 0xAA), s0); s1 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 24), _mm256_shuffle_ps(weights, weights, 0xFF), s1);
					sum = _mm256_add_ps(s0, s1);
				}
				else
				{
					sum = _mm256_mul_ps(_mm256_loadu_ps(p), _mm256_shuffle_ps(weights, weights, 0x00));
					sum = _mm256_fmadd_ps(_mm256_loadu_ps(p + 8), _mm256_shuffle_ps(weights, weights, 0x55), sum);
					sum = _mm256_fmadd_ps(_mm256_loadu_ps(p + 16), _mm256_shuffle_ps(weights, weights, 0xAA), sum);
					sum = _mm256_fmadd_ps(_mm256_loadu_ps(p + 24), _mm256_shuffle_ps(weights, weights, 0xFF), sum);
				}
			}
			_mm_storeu_ps(outA, _mm256_castps256_ps128(sum));
			_mm_storeu_ps(outB, _mm256_extractf128_ps(sum, 1));
		}
	}

	// One row pair of an upscale, every column of fourTapColumn<Form>, each column's pixels and weights at a constant step
	template <int Form>
	NOINLINE void fourTapRowPair(const float* a, const float* b, const float* interleaved, const float* columnWeights, size_t columns, size_t stepQ8, size_t capacityPixels, float* outA, float* outB)
	{
		[[maybe_unused]] const SpreadIndices indices;
		size_t positionQ8 = 0;
		for (size_t column = 0; column < columns; ++column)
		{
			const size_t pixel = positionQ8 >> 8;
			fourTapColumn<Form>(indices, a + pixel * 4, b + pixel * 4, interleaved + pixel * 8, columnWeights + column * weightStrideFor(4), outA + column * 3, outB + column * 3);

			positionQ8 += stepQ8;
			if ((positionQ8 >> 8) + 4 > capacityPixels)
				positionQ8 = 0;
		}
	}

	// fourTapRowPair with each column's run read from runs, as filterBufferedRuns at a fixed length reads the kernel's run table:
	// a run's firstSource is its pixel in the buffers, tested against the last pixel a run may start at.
	template <int Form>
	NOINLINE void fourTapRunTable(const float* a, const float* b, const float* interleaved, const ColumnRun* runs, size_t columns, size_t lastPixel, float* outA, float* outB)
	{
		[[maybe_unused]] const SpreadIndices indices;
		const ColumnRun* const runsEnd = runs + columns;
		for (const ColumnRun* run = runs; run != runsEnd; ++run, outA += 3, outB += 3)
		{
			const size_t pixel = run->firstSource;
			if (pixel > lastPixel)
				break;

			fourTapColumn<Form>(indices, a + pixel * 4, b + pixel * 4, interleaved + pixel * 8, run->weights, outA, outB);
		}
	}

	inline __m256 widenBytes(__m128i bytes) { return _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(bytes)); }
	inline __m128i highHalf(__m128i v) { return _mm_unpackhi_epi64(v, v); }

	// One row's RGB32 pixels to 4 floats each, as convertPixelsToFloats does: 4 pixels per 16-byte load, then single pixels
	FORCEINLINE void convertRow(const uint8_t* row, float* floats, size_t pixelCount)
	{
		// Stepped pointers and an end pointer, as the kernel's block loop
		const uint8_t* blockBytes = row;
		float* blockFloats = floats;
		float* const blockFloatsEnd = floats + pixelCount / 4 * 16;
		for (; blockFloats != blockFloatsEnd; blockBytes += 16, blockFloats += 16)
		{
			const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(blockBytes));
			_mm256_storeu_ps(blockFloats, widenBytes(bytes));
			_mm256_storeu_ps(blockFloats + 8, widenBytes(highHalf(bytes)));
		}

		SCALAR_LOOP
		for (size_t pixel = pixelCount / 4 * 4; pixel < pixelCount; ++pixel)
		{
			for (size_t channel = 0; channel < 4; ++channel)
				floats[pixel * 4 + channel] = static_cast<float>(row[pixel * 4 + channel]);
		}
	}

	// A row pair's RGB32 pixels to floats.
	// Layout: 0 - convertRow per row, each into its own floats;
	//         1 - interleaved: the rows' bytes unpacked into pixel pairs, then widened;
	//         2 - interleaved: each row widened, then the rows' lanes permuted into pixel pairs;
	//         3 - interleaved: each row widened, each pixel stored as 16 bytes.
	template <int Layout>
	FORCEINLINE void convertRowPair(const uint8_t* rowA, const uint8_t* rowB, float* floatsA, float* floatsB, float* interleaved, size_t pixelCount)
	{
		if constexpr (Layout == 0)
		{
			convertRow(rowA, floatsA, pixelCount);
			convertRow(rowB, floatsB, pixelCount);
		}
		else
		{
			// Stepped as convertRow's
			const uint8_t* blockBytesA = rowA;
			const uint8_t* blockBytesB = rowB;
			float* out = interleaved;
			float* const outEnd = interleaved + pixelCount / 4 * 32;
			for (; out != outEnd; blockBytesA += 16, blockBytesB += 16, out += 32)
			{
				const __m128i bytesA = _mm_loadu_si128(reinterpret_cast<const __m128i*>(blockBytesA));
				const __m128i bytesB = _mm_loadu_si128(reinterpret_cast<const __m128i*>(blockBytesB));
				if constexpr (Layout == 1)
				{
					const __m128i pairs01 = _mm_unpacklo_epi32(bytesA, bytesB), pairs23 = _mm_unpackhi_epi32(bytesA, bytesB);
					_mm256_storeu_ps(out, widenBytes(pairs01)); _mm256_storeu_ps(out + 8, widenBytes(highHalf(pairs01)));
					_mm256_storeu_ps(out + 16, widenBytes(pairs23)); _mm256_storeu_ps(out + 24, widenBytes(highHalf(pairs23)));
				}
				else
				{
					const __m256 a01 = widenBytes(bytesA), a23 = widenBytes(highHalf(bytesA));
					const __m256 b01 = widenBytes(bytesB), b23 = widenBytes(highHalf(bytesB));
					if constexpr (Layout == 2)
					{
						_mm256_storeu_ps(out, _mm256_permute2f128_ps(a01, b01, 0x20)); _mm256_storeu_ps(out + 8, _mm256_permute2f128_ps(a01, b01, 0x31));
						_mm256_storeu_ps(out + 16, _mm256_permute2f128_ps(a23, b23, 0x20)); _mm256_storeu_ps(out + 24, _mm256_permute2f128_ps(a23, b23, 0x31));
					}
					else
					{
						_mm_storeu_ps(out, _mm256_castps256_ps128(a01)); _mm_storeu_ps(out + 4, _mm256_castps256_ps128(b01));
						_mm_storeu_ps(out + 8, _mm256_extractf128_ps(a01, 1)); _mm_storeu_ps(out + 12, _mm256_extractf128_ps(b01, 1));
						_mm_storeu_ps(out + 16, _mm256_castps256_ps128(a23)); _mm_storeu_ps(out + 20, _mm256_castps256_ps128(b23));
						_mm_storeu_ps(out + 24, _mm256_extractf128_ps(a23, 1)); _mm_storeu_ps(out + 28, _mm256_extractf128_ps(b23, 1));
					}
				}
			}

			SCALAR_LOOP
			for (size_t pixel = pixelCount / 4 * 4; pixel < pixelCount; ++pixel)
			{
				for (size_t channel = 0; channel < 4; ++channel)
				{
					interleaved[pixel * 8 + channel] = static_cast<float>(rowA[pixel * 4 + channel]);
					interleaved[pixel * 8 + 4 + channel] = static_cast<float>(rowB[pixel * 4 + channel]);
				}
			}
		}
	}

	// A row pair's whole horizontal pass: conversion in chunks into a sliding span of capacity pixels, and the columns' runs over it,
	// alternating as filterHorizontalRowGroup does. Each column's run is read from runs, as the kernel reads its run table.
	// Form: kernelFormColumn (0) or interleavedFormColumn<Form>. Conversion: convertRowPair's Layout, 0 with Form 0 only.
	// FixedTaps: every column's tap count, known to the compiler; 0 - read from the run.
	//   The kernel's form is kernelFormColumnFixed; an interleaved one is interleavedColumnFixed, broadcast weights for a Form under 10.
	template <int Form, int Conversion, size_t FixedTaps = 0>
	NOINLINE void horizontalPassRowPair(const uint8_t* rowA, const uint8_t* rowB, size_t sourceWidth, float* floatsA, float* floatsB, float* interleaved, size_t capacity,
		const ColumnRun* runs, size_t columns, float* outA, float* outB)
	{
		static_assert((Form == 0) == (Conversion == 0));
		constexpr size_t conversionChunk = 128;
		[[maybe_unused]] const SpreadIndices indices;
		size_t base = 0, converted = 0;
		const ColumnRun* run = runs;
		const ColumnRun* const runsEnd = runs + columns;
		while (run != runsEnd)
		{
			// SlidingSourceFloats::prepareRun: the span slides to the run's first pixel
			const size_t first = run->firstSource, end = first + (FixedTaps != 0 ? FixedTaps : run->weightCount);
			if (end > base + capacity)
			{
				if (first < converted)
				{
					if constexpr (Form == 0)
					{
						std::memmove(floatsA, floatsA + (first - base) * 4, (converted - first) * 4 * sizeof(float));
						std::memmove(floatsB, floatsB + (first - base) * 4, (converted - first) * 4 * sizeof(float));
					}
					else
						std::memmove(interleaved, interleaved + (first - base) * 8, (converted - first) * 8 * sizeof(float));
				}
				else
					converted = first;

				base = first;
			}

			if (end > converted)
			{
				const size_t target = std::min(std::min(std::max(end, converted + conversionChunk), base + capacity), sourceWidth);
				const size_t bufferPixel = converted - base;
				convertRowPair<Conversion>(rowA + converted * 4, rowB + converted * 4, floatsA + bufferPixel * 4, floatsB + bufferPixel * 4, interleaved + bufferPixel * 8, target - converted);
				converted = target;
			}

			// filterBufferedRuns: the runs start in order, so only a run's end is tested.
			// The floats as if they began at source pixel 0, and the converted end by value: constants of the loop.
			[[maybe_unused]] const float* const originA = floatsA - base * 4;
			[[maybe_unused]] const float* const originB = floatsB - base * 4;
			[[maybe_unused]] const float* const origin = interleaved - base * 8;
			const size_t bufferEnd = converted;
			for (; run != runsEnd; ++run, outA += 3, outB += 3)
			{
				const size_t firstPixel = run->firstSource, tapCount = FixedTaps != 0 ? FixedTaps : run->weightCount;
				if (firstPixel + tapCount > bufferEnd)
					break;

				if constexpr (Form == 0 && FixedTaps != 0)
					kernelFormColumnFixed<FixedTaps>(indices, originA + firstPixel * 4, originB + firstPixel * 4, run->weights, outA, outB);
				else if constexpr (Form == 0)
					kernelFormColumn(indices, originA + firstPixel * 4, originB + firstPixel * 4, run->weights, tapCount, outA, outB);
				else if constexpr (FixedTaps != 0)
					interleavedColumnFixed<FixedTaps, Form < 10 ? 1 : 0>(origin + firstPixel * 8, run->weights, outA, outB);
				else
					interleavedFormColumn<Form>(origin + firstPixel * 8, run->weights, tapCount, outA, outB);
			}
		}
	}

	// One strip of one dest row: 12 taps over temp rows, 64 floats per step, the first tap assigned, as the vertical pass does.
	// Output: 0 - the sums stored as floats to a 256-byte scratch; 1 - rounded and packed to 64 bytes per step, written to dest.
	template <int Output>
	NOINLINE void verticalDestRow(const float* const* tapRows, const float* tapWeights, size_t rowFloats, uint8_t* dest, float* scratch)
	{
		const __m256 half = _mm256_set1_ps(0.5f);
		const __m256i order = _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7);
		for (size_t f = 0; f + 64 <= rowFloats; f += 64)
		{
			__m256 w = _mm256_broadcast_ss(tapWeights);
			const float* row = tapRows[0] + f;
			__m256 s0 = _mm256_mul_ps(_mm256_loadu_ps(row), w), s1 = _mm256_mul_ps(_mm256_loadu_ps(row + 8), w), s2 = _mm256_mul_ps(_mm256_loadu_ps(row + 16), w), s3 = _mm256_mul_ps(_mm256_loadu_ps(row + 24), w);
			__m256 s4 = _mm256_mul_ps(_mm256_loadu_ps(row + 32), w), s5 = _mm256_mul_ps(_mm256_loadu_ps(row + 40), w), s6 = _mm256_mul_ps(_mm256_loadu_ps(row + 48), w), s7 = _mm256_mul_ps(_mm256_loadu_ps(row + 56), w);
			for (size_t tap = 1; tap < taps; ++tap)
			{
				w = _mm256_broadcast_ss(tapWeights + tap);
				row = tapRows[tap] + f;
				s0 = _mm256_fmadd_ps(_mm256_loadu_ps(row), w, s0); s1 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 8), w, s1);
				s2 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 16), w, s2); s3 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 24), w, s3);
				s4 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 32), w, s4); s5 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 40), w, s5);
				s6 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 48), w, s6); s7 = _mm256_fmadd_ps(_mm256_loadu_ps(row + 56), w, s7);
			}

			if constexpr (Output == 0)
			{
				_mm256_storeu_ps(scratch, s0); _mm256_storeu_ps(scratch + 8, s1); _mm256_storeu_ps(scratch + 16, s2); _mm256_storeu_ps(scratch + 24, s3);
				_mm256_storeu_ps(scratch + 32, s4); _mm256_storeu_ps(scratch + 40, s5); _mm256_storeu_ps(scratch + 48, s6); _mm256_storeu_ps(scratch + 56, s7);
			}
			else
			{
				const auto toInts = [half](__m256 v) { return _mm256_cvttps_epi32(_mm256_add_ps(v, half)); };
				const __m256i lowBytes = _mm256_packus_epi16(_mm256_packs_epi32(toInts(s0), toInts(s1)), _mm256_packs_epi32(toInts(s2), toInts(s3)));
				const __m256i highBytes = _mm256_packus_epi16(_mm256_packs_epi32(toInts(s4), toInts(s5)), _mm256_packs_epi32(toInts(s6), toInts(s7)));
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dest + f), _mm256_permutevar8x32_epi32(lowBytes, order));
				_mm256_storeu_si256(reinterpret_cast<__m256i*>(dest + f + 32), _mm256_permutevar8x32_epi32(highBytes, order));
			}
		}
	}

	template <int Output>
	double verticalSeconds(size_t stripFloats, size_t ringRows, int repeats, double& multiplyAdds)
	{
		// A dest row's floats, a strip at a time
		const size_t rowFloats = destWidth * 3;
		const size_t strips = (rowFloats + stripFloats - 1) / stripFloats;
		float* ring = alignedFloats(ringRows * stripFloats + 64);
		std::vector<uint8_t> dest(destHeight * rowFloats + 4096);
		float scratch[64];
		float tapWeights[taps];
		for (size_t t = 0; t < taps; ++t)
			tapWeights[t] = 1.0f / taps;

		multiplyAdds = 0;
		for (size_t strip = 0; strip < strips; ++strip)
			multiplyAdds += static_cast<double>(std::min(stripFloats, rowFloats - strip * stripFloats) / 64 * 64) * destHeight * taps;

		const double seconds = bestSeconds([&] {
			for (size_t strip = 0; strip < strips; ++strip)
			{
				const size_t floats = std::min(stripFloats, rowFloats - strip * stripFloats);
				size_t firstRow = 0;
				for (size_t dy = 0; dy < destHeight; ++dy)
				{
					// The window advances 2 temp rows per dest row and wraps in the ring. No division: it would dominate a small strip.
					const float* tapRows[taps];
					for (size_t t = 0; t < taps; ++t)
					{
						const size_t rowIndex = firstRow + t < ringRows ? firstRow + t : firstRow + t - ringRows;
						tapRows[t] = ring + rowIndex * stripFloats;
					}
					firstRow = firstRow + 2 < ringRows ? firstRow + 2 : firstRow + 2 - ringRows;
					verticalDestRow<Output>(tapRows, tapWeights, floats, dest.data() + dy * rowFloats + strip * stripFloats, scratch);
				}
			}
		}, repeats);
		volatile float sink = scratch[5] + static_cast<float>(dest[77]);
		(void)sink;
		freeFloats(ring);
		return seconds;
	}

	// Variant: 0, 1 - horizontalRowPair<Variant>; 11, 12 - horizontalRowPairChains<1 or 2>; 20 + Layout * 2 + Broadcast - horizontalRowPairPerTap
	template <int Variant>
	double horizontalSeconds(int repeats)
	{
		float* a = alignedFloats(160 * 4 + 64);
		float* b = alignedFloats(160 * 4 + 64);
		float* interleaved = alignedFloats(160 * 8 + 128);
		float* weights = alignedFloats(destWidth * taps + 64);
		for (size_t i = 0; i < destWidth * taps + 64; ++i)
			weights[i] = 1.0f / taps;
		float* outA = alignedFloats(destWidth * 3 + 64);
		float* outB = alignedFloats(destWidth * 3 + 64);
		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < rowPairs; ++pair)
			{
				if constexpr (Variant >= 20)
					horizontalRowPairPerTap<(Variant - 20) / 2, (Variant - 20) % 2>(a, b, interleaved, weights, outA, outB);
				else if constexpr (Variant >= 10)
					horizontalRowPairChains<Variant - 10>(a, b, weights, outA, outB);
				else
					horizontalRowPair<Variant>(a, b, weights, outA, outB);
			}
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(a); freeFloats(b); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// fourTapRowPair<Form> at 720p -> 4K's row and column counts
	template <int Form>
	double fourTapSeconds(int repeats)
	{
		constexpr size_t columns = 3840, capacityPixels = 2 * 4 + 128;
		float* a = alignedFloats(capacityPixels * 4 + 64);
		float* b = alignedFloats(capacityPixels * 4 + 64);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		float* weights = alignedFloats(columns * weightStrideFor(4) + 64);
		for (size_t i = 0; i < columns * weightStrideFor(4) + 64; ++i)
			weights[i] = 0.25f;
		float* outA = alignedFloats(columns * 3 + 64);
		float* outB = alignedFloats(columns * 3 + 64);
		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < 720 / 2; ++pair)
				fourTapRowPair<Form>(a, b, interleaved, weights, columns, 256 / 3, capacityPixels, outA, outB);
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(a); freeFloats(b); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// fourTapRunTable<Form> at 720p -> 4K's row and column counts. Runs a period of 3 columns apart share their weights, as the kernel's do.
	template <int Form>
	double fourTapRunTableSeconds(int repeats)
	{
		constexpr size_t columns = 3840, capacityPixels = 2 * 4 + 128, period = 3;
		float* a = alignedFloats(capacityPixels * 4 + 64);
		float* b = alignedFloats(capacityPixels * 4 + 64);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		float* weights = alignedFloats(period * weightStrideFor(4) + 64);
		for (size_t i = 0; i < period * weightStrideFor(4) + 64; ++i)
			weights[i] = 0.25f;
		float* outA = alignedFloats(columns * 3 + 64);
		float* outB = alignedFloats(columns * 3 + 64);

		std::vector<ColumnRun> runs(columns);
		size_t positionQ8 = 0;
		for (size_t column = 0; column < columns; ++column)
		{
			runs[column] = { positionQ8 >> 8, weights + column % period * weightStrideFor(4), 4 };
			positionQ8 += 256 / 3;
			if ((positionQ8 >> 8) + 4 > capacityPixels)
				positionQ8 = 0;
		}

		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < 720 / 2; ++pair)
				fourTapRunTable<Form>(a, b, interleaved, runs.data(), columns, capacityPixels - 4, outA, outB);
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(a); freeFloats(b); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// One interleaved column's 4 taps in one chain, as the kernel's at 4 taps, its pixel pairs in registers. Weights: PairWeights'.
	template <int Weights>
	FORCEINLINE void interleavedFourTaps(__m256 p0, __m256 p1, __m256 p2, __m256 p3, const float* w, float* outA, float* outB)
	{
		const PairWeights<Weights> weights{ w };
		__m256 sum = _mm256_mul_ps(p0, weights.template tap<0>());
		sum = _mm256_fmadd_ps(p1, weights.template tap<1>(), sum);
		sum = _mm256_fmadd_ps(p2, weights.template tap<2>(), sum);
		sum = _mm256_fmadd_ps(p3, weights.template tap<3>(), sum);
		_mm_storeu_ps(outA, _mm256_castps256_ps128(sum));
		_mm_storeu_ps(outB, _mm256_extractf128_ps(sum, 1));
	}

	// Columns 0, 1 and 2 of an interleaved group at pixel pairs 0, Offset1 and Offset2 of a window of 4 + Offset2 pairs, each loaded once.
	// weights: the three columns' 4 weights side by side. The window by name: MSVC moves a small array through memory.
	template <size_t Offset1, size_t Offset2, int Weights>
	FORCEINLINE void interleavedGroupOfThree(const float* p, const float* weights, float* outA, float* outB)
	{
		const __m256 p0 = _mm256_loadu_ps(p), p1 = _mm256_loadu_ps(p + 8), p2 = _mm256_loadu_ps(p + 16), p3 = _mm256_loadu_ps(p + 24);
		__m256 p4 = p3, p5 = p3;
		if constexpr (Offset2 >= 1)
			p4 = _mm256_loadu_ps(p + 32);
		if constexpr (Offset2 == 2)
			p5 = _mm256_loadu_ps(p + 40);

		interleavedFourTaps<Weights>(p0, p1, p2, p3, weights, outA, outB);
		if constexpr (Offset1 == 0)
			interleavedFourTaps<Weights>(p0, p1, p2, p3, weights + 4, outA + 3, outB + 3);
		else
			interleavedFourTaps<Weights>(p1, p2, p3, p4, weights + 4, outA + 3, outB + 3);
		if constexpr (Offset2 == 0)
			interleavedFourTaps<Weights>(p0, p1, p2, p3, weights + 8, outA + 6, outB + 6);
		else if constexpr (Offset2 == 1)
			interleavedFourTaps<Weights>(p1, p2, p3, p4, weights + 8, outA + 6, outB + 6);
		else
			interleavedFourTaps<Weights>(p2, p3, p4, p5, weights + 8, outA + 6, outB + 6);
	}

	// fourTapRunTable's interleaved one-chain column three columns at a time, from a record per group as upscaleGroupRecords128 reads them.
	// A group of pattern 4 takes a column at a time from runs.
	template <int Weights>
	NOINLINE void fourTapGroupRecords(const float* interleaved, const GroupRun* groups, const ColumnRun* runs, size_t columns, size_t lastPixel, float* outA, float* outB)
	{
		const GroupRun* const groupsEnd = groups + columns / 3;
		for (const GroupRun* group = groups; group != groupsEnd; ++group, outA += 9, outB += 9)
		{
			const size_t first = group->firstSource;
			if (first > lastPixel)
				break;

			const float* const p = interleaved + first * 8;
			const float* const weights = group->weights;
			switch (group->pattern)
			{
			case 0: interleavedGroupOfThree<0, 0, Weights>(p, weights, outA, outB); break;
			case 1: interleavedGroupOfThree<1, 1, Weights>(p, weights, outA, outB); break;
			case 2: interleavedGroupOfThree<0, 1, Weights>(p, weights, outA, outB); break;
			case 3: interleavedGroupOfThree<1, 2, Weights>(p, weights, outA, outB); break;
			default:
				for (size_t column = 0; column < 3; ++column)
				{
					const ColumnRun& run = runs[(group - groups) * 3 + column];
					const float* const q = interleaved + run.firstSource * 8;
					interleavedFourTaps<Weights>(_mm256_loadu_ps(q), _mm256_loadu_ps(q + 8), _mm256_loadu_ps(q + 16), _mm256_loadu_ps(q + 24), run.weights,
						outA + column * 3, outB + column * 3);
				}
			}
		}
	}

	// fourTapGroupRecords<Weights> on fourTapRunTableSeconds' runs. Each group's weights in a 16-float slot, one per distinct group of the period.
	template <int Weights>
	double fourTapGroupRecordsSeconds(int repeats)
	{
		constexpr size_t columns = 3840, capacityPixels = 2 * 4 + 128, period = 3;
		static_assert(columns % 3 == 0);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		float* weights = alignedFloats(period * 16 + 64);
		for (size_t i = 0; i < period * 16 + 64; ++i)
			weights[i] = 0.25f;
		float* outA = alignedFloats(columns * 3 + 64);
		float* outB = alignedFloats(columns * 3 + 64);

		std::vector<ColumnRun> runs(columns);
		size_t positionQ8 = 0;
		for (size_t column = 0; column < columns; ++column)
		{
			runs[column] = { positionQ8 >> 8, weights + column / 3 * 3 % period * 16 + column % 3 * 4, 4 };
			positionQ8 += 256 / 3;
			if ((positionQ8 >> 8) + 4 > capacityPixels)
				positionQ8 = 0;
		}

		std::vector<GroupRun> groups(columns / 3);
		for (size_t group = 0; group < groups.size(); ++group)
		{
			const ColumnRun* const run = runs.data() + group * 3;
			groups[group] = { static_cast<uint32_t>(run->firstSource), groupPattern(run), weights + group * 3 % period * 16 };
		}

		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < 720 / 2; ++pair)
				fourTapGroupRecords<Weights>(interleaved, groups.data(), runs.data(), columns, capacityPixels - 4, outA, outB);
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// A 4K RGB32 image's rows, 16 pixels wider than 3840: the last 12-tap runs at a step of 2 end past it
	constexpr size_t sourceRowPixels = 3840 + 16, sourceRowBytes = sourceRowPixels * 4;

	std::vector<uint8_t> sourceImage()
	{
		std::vector<uint8_t> image(sourceHeight * sourceRowBytes + 64);
		for (size_t i = 0; i < image.size(); ++i)
			image[i] = static_cast<uint8_t>(i * 37);
		return image;
	}

	// convertRowPair<Layout> over a 4K RGB32 image in chunks of 128 pixels into a span in L1. wholeImage false: the first row pair every time, from L2.
	template <int Layout>
	double conversionSeconds(const std::vector<uint8_t>& image, bool wholeImage, int repeats)
	{
		constexpr size_t chunk = 128;
		float* floatsA = alignedFloats(160 * 4 + 64);
		float* floatsB = alignedFloats(160 * 4 + 64);
		float* interleaved = alignedFloats(160 * 8 + 128);
		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < rowPairs; ++pair)
			{
				const uint8_t* rowA = image.data() + (wholeImage ? pair * 2 : 0) * sourceRowBytes;
				const uint8_t* rowB = rowA + sourceRowBytes;
				for (size_t pixel = 0; pixel < 3840; pixel += chunk)
					convertRowPair<Layout>(rowA + pixel * 4, rowB + pixel * 4, floatsA, floatsB, interleaved, chunk);
			}
		}, repeats);
		volatile float sink = floatsA[100] + floatsB[200] + interleaved[300];
		(void)sink;
		freeFloats(floatsA); freeFloats(floatsB); freeFloats(interleaved);
		return seconds;
	}

	// horizontalPassRowPair<Form, Conversion, FixedTaps> over a 4K RGB32 image: columns runs of passTaps taps, stepQ8 / 256 pixels apart.
	// spanOffsetFloats: the span's start past a 64-byte boundary, in floats. mixed: see columnTapCounts; never with FixedTaps.
	template <int Form, int Conversion, size_t FixedTaps = 0>
	double passSeconds(const std::vector<uint8_t>& image, size_t spanOffsetFloats, int repeats, size_t passTaps = taps, size_t stepQ8 = 2 * 256, size_t columns = destWidth,
		bool mixed = false)
	{
		if (FixedTaps != 0 && (FixedTaps != passTaps || mixed))
			std::abort();

		const size_t capacity = 2 * passTaps + 128, weightStride = weightStrideFor(passTaps);
		float* floatsA = alignedFloats(capacity * 4 + 64);
		float* floatsB = alignedFloats(capacity * 4 + 64);
		float* interleaved = alignedFloats(capacity * 8 + 128);
		float* weights = alignedFloats(columns * weightStride + 64);
		for (size_t i = 0; i < columns * weightStride + 64; ++i)
			weights[i] = 1.0f / static_cast<float>(passTaps);
		float* outA = alignedFloats(columns * 3 + 64);
		float* outB = alignedFloats(columns * 3 + 64);
		const std::vector<uint32_t> tapCounts = columnTapCounts(columns, passTaps, mixed);
		std::vector<ColumnRun> runs(columns);
		for (size_t column = 0; column < columns; ++column)
			runs[column] = { column * stepQ8 >> 8, weights + column * weightStride, tapCounts[column] };

		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < rowPairs; ++pair)
			{
				const uint8_t* rowA = image.data() + pair * 2 * sourceRowBytes;
				horizontalPassRowPair<Form, Conversion, FixedTaps>(rowA, rowA + sourceRowBytes, sourceRowPixels, floatsA + spanOffsetFloats, floatsB + spanOffsetFloats, interleaved + spanOffsetFloats,
					capacity, runs.data(), columns, outA, outB);
			}
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(floatsA); freeFloats(floatsB); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// Section Y: the vertical pass's tap loop over 3-float pixels, a strip of dest rows at a time over a ring of temp rows in L2
	constexpr size_t yStripFloats = 1728; // a multiple of 6, 9 and 12 vectors

	// Each dest row's taps as the kernel lists them, a temp row pointer and a weight per tap, stride entries per dest row
	struct ListedVerticalTaps
	{
		std::vector<const float*> rows;
		std::vector<float> weights;
		std::vector<uint32_t> counts;
		size_t stride;
	};

	// Each dest row's first temp row: tapCount rows centered as the kernel's window, inside the image
	std::vector<size_t> windowFirstRows(size_t tempRows, size_t destRows, size_t tapCount)
	{
		std::vector<size_t> firstRows;
		const double scale = static_cast<double>(tempRows) / static_cast<double>(destRows);
		for (size_t destRow = 0; destRow < destRows; ++destRow)
		{
			const double center = (static_cast<double>(destRow) + 0.5) * scale - 0.5;
			const ptrdiff_t first = static_cast<ptrdiff_t>(std::floor(center)) - static_cast<ptrdiff_t>(tapCount / 2 - 1);
			firstRows.push_back(static_cast<size_t>(std::clamp<ptrdiff_t>(first, 0, static_cast<ptrdiff_t>(tempRows - tapCount))));
		}
		return firstRows;
	}

	// One dest row per entry; a temp row's index wraps in the ring
	ListedVerticalTaps listedVerticalTaps(const float* ring, size_t ringRows, const std::vector<size_t>& firstRows, size_t tapCount)
	{
		ListedVerticalTaps taps{ {}, {}, {}, tapCount };
		for (const size_t first : firstRows)
		{
			for (size_t tap = 0; tap < tapCount; ++tap)
			{
				taps.rows.push_back(ring + (first + tap) % ringRows * yStripFloats);
				taps.weights.push_back(1.0f / static_cast<float>(tapCount));
			}
			taps.counts.push_back(static_cast<uint32_t>(tapCount));
		}
		return taps;
	}

	// Two dest rows per entry over the union of their windows: unionTaps.weights is the first row's, secondWeights the second's, 0 outside its window
	struct PairedVerticalTaps
	{
		ListedVerticalTaps unionTaps;
		std::vector<float> secondWeights;
	};

	PairedVerticalTaps pairedVerticalTaps(const float* ring, size_t ringRows, const std::vector<size_t>& firstRows, size_t tapCount)
	{
		const size_t stride = 2 * tapCount;
		PairedVerticalTaps paired{ { {}, {}, {}, stride }, {} };
		const auto windowWeight = [tapCount](size_t row, size_t first) { return row >= first && row < first + tapCount ? 1.0f / static_cast<float>(tapCount) : 0.0f; };
		for (size_t pair = 0; pair + 1 < firstRows.size(); pair += 2)
		{
			const size_t firstA = firstRows[pair], firstB = firstRows[pair + 1];
			const size_t begin = std::min(firstA, firstB), end = std::max(firstA, firstB) + tapCount;
			for (size_t row = begin; row < begin + stride; ++row)
			{
				paired.unionTaps.rows.push_back(ring + row % ringRows * yStripFloats);
				paired.unionTaps.weights.push_back(windowWeight(row, firstA));
				paired.secondWeights.push_back(windowWeight(row, firstB));
			}
			paired.unionTaps.counts.push_back(static_cast<uint32_t>(end - begin));
		}
		return paired;
	}

	FORCEINLINE __m256 weighted(const float* source, __m256 weight) { return _mm256_mul_ps(_mm256_loadu_ps(source), weight); }
	FORCEINLINE __m256 addWeighted(const float* source, __m256 weight, __m256 sum) { return _mm256_fmadd_ps(_mm256_loadu_ps(source), weight, sum); }

	// The kernel's form at Vectors accumulators: each tap's weight broadcast from memory and its row pointer loaded, the first tap assigned
	template <int Vectors>
	NOINLINE void listedTapsDestRow(const float* const* rows, const float* weights, size_t count, size_t floats, float* scratch)
	{
		static_assert(Vectors == 6 || Vectors == 9 || Vectors == 12);
		for (size_t f = 0; f + Vectors * 8 <= floats; f += Vectors * 8)
		{
			__m256 w = _mm256_broadcast_ss(weights);
			const float* source = rows[0] + f;
			__m256 a0 = weighted(source, w), a1 = weighted(source + 8, w), a2 = weighted(source + 16, w);
			__m256 a3 = weighted(source + 24, w), a4 = weighted(source + 32, w), a5 = weighted(source + 40, w);
			__m256 a6 = w, a7 = w, a8 = w, a9 = w, a10 = w, a11 = w; // assigned below where Vectors takes them
			if constexpr (Vectors >= 9)
			{
				a6 = weighted(source + 48, w); a7 = weighted(source + 56, w); a8 = weighted(source + 64, w);
			}
			if constexpr (Vectors == 12)
			{
				a9 = weighted(source + 72, w); a10 = weighted(source + 80, w); a11 = weighted(source + 88, w);
			}

			for (size_t tap = 1; tap < count; ++tap)
			{
				w = _mm256_broadcast_ss(weights + tap);
				source = rows[tap] + f;
				a0 = addWeighted(source, w, a0); a1 = addWeighted(source + 8, w, a1); a2 = addWeighted(source + 16, w, a2);
				a3 = addWeighted(source + 24, w, a3); a4 = addWeighted(source + 32, w, a4); a5 = addWeighted(source + 40, w, a5);
				if constexpr (Vectors >= 9)
				{
					a6 = addWeighted(source + 48, w, a6); a7 = addWeighted(source + 56, w, a7); a8 = addWeighted(source + 64, w, a8);
				}
				if constexpr (Vectors == 12)
				{
					a9 = addWeighted(source + 72, w, a9); a10 = addWeighted(source + 80, w, a10); a11 = addWeighted(source + 88, w, a11);
				}
			}

			_mm256_storeu_ps(scratch, a0); _mm256_storeu_ps(scratch + 8, a1); _mm256_storeu_ps(scratch + 16, a2);
			_mm256_storeu_ps(scratch + 24, a3); _mm256_storeu_ps(scratch + 32, a4); _mm256_storeu_ps(scratch + 40, a5);
			if constexpr (Vectors >= 9)
			{
				_mm256_storeu_ps(scratch + 48, a6); _mm256_storeu_ps(scratch + 56, a7); _mm256_storeu_ps(scratch + 64, a8);
			}
			if constexpr (Vectors == 12)
			{
				_mm256_storeu_ps(scratch + 72, a9); _mm256_storeu_ps(scratch + 80, a10); _mm256_storeu_ps(scratch + 88, a11);
			}
		}
	}

	// Four taps, their weights and row pointers held in registers across the dest row: the loop loads only its operands
	NOINLINE void heldFourTapsDestRow(const float* const* rows, const float* weights, size_t floats, float* scratch)
	{
		const __m256 w0 = _mm256_broadcast_ss(weights), w1 = _mm256_broadcast_ss(weights + 1), w2 = _mm256_broadcast_ss(weights + 2), w3 = _mm256_broadcast_ss(weights + 3);
		const float* const r0 = rows[0];
		const float* const r1 = rows[1];
		const float* const r2 = rows[2];
		const float* const r3 = rows[3];
		for (size_t f = 0; f + 48 <= floats; f += 48)
		{
			__m256 a0 = weighted(r0 + f, w0), a1 = weighted(r0 + f + 8, w0), a2 = weighted(r0 + f + 16, w0);
			__m256 a3 = weighted(r0 + f + 24, w0), a4 = weighted(r0 + f + 32, w0), a5 = weighted(r0 + f + 40, w0);
			a0 = addWeighted(r1 + f, w1, a0); a1 = addWeighted(r1 + f + 8, w1, a1); a2 = addWeighted(r1 + f + 16, w1, a2);
			a3 = addWeighted(r1 + f + 24, w1, a3); a4 = addWeighted(r1 + f + 32, w1, a4); a5 = addWeighted(r1 + f + 40, w1, a5);
			a0 = addWeighted(r2 + f, w2, a0); a1 = addWeighted(r2 + f + 8, w2, a1); a2 = addWeighted(r2 + f + 16, w2, a2);
			a3 = addWeighted(r2 + f + 24, w2, a3); a4 = addWeighted(r2 + f + 32, w2, a4); a5 = addWeighted(r2 + f + 40, w2, a5);
			a0 = addWeighted(r3 + f, w3, a0); a1 = addWeighted(r3 + f + 8, w3, a1); a2 = addWeighted(r3 + f + 16, w3, a2);
			a3 = addWeighted(r3 + f + 24, w3, a3); a4 = addWeighted(r3 + f + 32, w3, a4); a5 = addWeighted(r3 + f + 40, w3, a5);
			_mm256_storeu_ps(scratch, a0); _mm256_storeu_ps(scratch + 8, a1); _mm256_storeu_ps(scratch + 16, a2);
			_mm256_storeu_ps(scratch + 24, a3); _mm256_storeu_ps(scratch + 32, a4); _mm256_storeu_ps(scratch + 40, a5);
		}
	}

	// Two dest rows over the union of their taps, 6 accumulators each: every temp row vector loaded once for both
	NOINLINE void pairedDestRows(const float* const* rows, const float* weightsA, const float* weightsB, size_t count, size_t floats, float* scratch)
	{
		for (size_t f = 0; f + 48 <= floats; f += 48)
		{
			__m256 wa = _mm256_broadcast_ss(weightsA), wb = _mm256_broadcast_ss(weightsB);
			const float* source = rows[0] + f;
			__m256 x = _mm256_loadu_ps(source);
			__m256 a0 = _mm256_mul_ps(x, wa), b0 = _mm256_mul_ps(x, wb);
			x = _mm256_loadu_ps(source + 8);
			__m256 a1 = _mm256_mul_ps(x, wa), b1 = _mm256_mul_ps(x, wb);
			x = _mm256_loadu_ps(source + 16);
			__m256 a2 = _mm256_mul_ps(x, wa), b2 = _mm256_mul_ps(x, wb);
			x = _mm256_loadu_ps(source + 24);
			__m256 a3 = _mm256_mul_ps(x, wa), b3 = _mm256_mul_ps(x, wb);
			x = _mm256_loadu_ps(source + 32);
			__m256 a4 = _mm256_mul_ps(x, wa), b4 = _mm256_mul_ps(x, wb);
			x = _mm256_loadu_ps(source + 40);
			__m256 a5 = _mm256_mul_ps(x, wa), b5 = _mm256_mul_ps(x, wb);
			for (size_t tap = 1; tap < count; ++tap)
			{
				wa = _mm256_broadcast_ss(weightsA + tap);
				wb = _mm256_broadcast_ss(weightsB + tap);
				source = rows[tap] + f;
				x = _mm256_loadu_ps(source); a0 = _mm256_fmadd_ps(x, wa, a0); b0 = _mm256_fmadd_ps(x, wb, b0);
				x = _mm256_loadu_ps(source + 8); a1 = _mm256_fmadd_ps(x, wa, a1); b1 = _mm256_fmadd_ps(x, wb, b1);
				x = _mm256_loadu_ps(source + 16); a2 = _mm256_fmadd_ps(x, wa, a2); b2 = _mm256_fmadd_ps(x, wb, b2);
				x = _mm256_loadu_ps(source + 24); a3 = _mm256_fmadd_ps(x, wa, a3); b3 = _mm256_fmadd_ps(x, wb, b3);
				x = _mm256_loadu_ps(source + 32); a4 = _mm256_fmadd_ps(x, wa, a4); b4 = _mm256_fmadd_ps(x, wb, b4);
				x = _mm256_loadu_ps(source + 40); a5 = _mm256_fmadd_ps(x, wa, a5); b5 = _mm256_fmadd_ps(x, wb, b5);
			}

			_mm256_storeu_ps(scratch, a0); _mm256_storeu_ps(scratch + 8, a1); _mm256_storeu_ps(scratch + 16, a2);
			_mm256_storeu_ps(scratch + 24, a3); _mm256_storeu_ps(scratch + 32, a4); _mm256_storeu_ps(scratch + 40, a5);
			_mm256_storeu_ps(scratch + 48, b0); _mm256_storeu_ps(scratch + 56, b1); _mm256_storeu_ps(scratch + 64, b2);
			_mm256_storeu_ps(scratch + 72, b3); _mm256_storeu_ps(scratch + 80, b4); _mm256_storeu_ps(scratch + 88, b5);
		}
	}

	// Form: 6, 9, 12 - the kernel's form at that many accumulators; 1 - four taps held; 2 - dest rows paired.
	// Each strip passes over the same ring: the kernel's strips each have a ring of their own, of this size.
	template <int Form>
	double verticalTapLoopSeconds(size_t tempRows, size_t destRows, size_t tapCount, size_t ringRows, size_t strips, int repeats)
	{
		float* ring = alignedFloats(ringRows * yStripFloats + 64);
		const std::vector<size_t> firstRows = windowFirstRows(tempRows, destRows, tapCount);
		const ListedVerticalTaps listed = listedVerticalTaps(ring, ringRows, firstRows, tapCount);
		const PairedVerticalTaps paired = pairedVerticalTaps(ring, ringRows, firstRows, tapCount);
		alignas(32) float scratch[96];
		const double seconds = bestSeconds([&] {
			for (size_t strip = 0; strip < strips; ++strip)
			{
				if constexpr (Form == 2)
				{
					for (size_t pair = 0; pair < destRows / 2; ++pair)
					{
						const size_t entry = pair * paired.unionTaps.stride;
						pairedDestRows(paired.unionTaps.rows.data() + entry, paired.unionTaps.weights.data() + entry, paired.secondWeights.data() + entry, paired.unionTaps.counts[pair], yStripFloats, scratch);
					}
				}
				else
				{
					for (size_t destRow = 0; destRow < destRows; ++destRow)
					{
						const float* const* rows = listed.rows.data() + destRow * listed.stride;
						const float* weights = listed.weights.data() + destRow * listed.stride;
						if constexpr (Form == 1)
							heldFourTapsDestRow(rows, weights, yStripFloats, scratch);
						else
							listedTapsDestRow<Form>(rows, weights, listed.counts[destRow], yStripFloats, scratch);
					}
				}
			}
		}, repeats);
		volatile float sink = scratch[7];
		(void)sink;
		freeFloats(ring);
		return seconds;
	}
#endif // __AVX2__

	// The 128-bit levels' horizontal form for a row pair of 4-float pixels: a pixel per register, as the kernel compiles at SSE4.1 and NEON
#ifdef ARM_NEON
	using Floats4 = float32x4_t;
	// The level's horizontalChainCount
	constexpr int levelChainCount = 4;
	inline Floats4 zero4() { return vdupq_n_f32(0.0f); }
	inline Floats4 load4(const float* p) { return vld1q_f32(p); }
	inline void store4(float* p, Floats4 v) { vst1q_f32(p, v); }
	inline Floats4 broadcastLoad(const float* p) { return vld1q_dup_f32(p); }
	template <int Lane> inline Floats4 broadcastLane(Floats4 v) { return vdupq_laneq_f32(v, Lane); }
	inline Floats4 mul4(Floats4 a, Floats4 b) { return vmulq_f32(a, b); }
	inline Floats4 add4(Floats4 a, Floats4 b) { return vaddq_f32(a, b); }
	inline Floats4 mulAdd4(Floats4 a, Floats4 b, Floats4 accum) { return vfmaq_f32(accum, a, b); }
#else
	using Floats4 = __m128;
	constexpr int levelChainCount = 2;
	inline Floats4 zero4() { return _mm_setzero_ps(); }
	inline Floats4 load4(const float* p) { return _mm_loadu_ps(p); }
	inline void store4(float* p, Floats4 v) { _mm_storeu_ps(p, v); }
	inline Floats4 broadcastLoad(const float* p) { return _mm_load1_ps(p); }
	template <int Lane> inline Floats4 broadcastLane(Floats4 v) { return _mm_shuffle_ps(v, v, Lane * 0x55); }
	inline Floats4 mul4(Floats4 a, Floats4 b) { return _mm_mul_ps(a, b); }
	inline Floats4 add4(Floats4 a, Floats4 b) { return _mm_add_ps(a, b); }
	inline Floats4 mulAdd4(Floats4 a, Floats4 b, Floats4 accum) { return _mm_add_ps(accum, _mm_mul_ps(a, b)); }
#endif

	// Two taps' pixels or sums of one row
	struct TapPair
	{
		Floats4 low;
		Floats4 high;
	};

	struct RowChains128
	{
		TapPair chain0, chain1, chain2, chain3;
	};

	// A row pair's pixels by tap. Layout: 0 - a buffer per row; 1 - the rows' pixels interleaved in a.
	template <int Layout>
	struct RowPairPixels
	{
		Floats4 rowA(size_t tap) const { return load4(a + tap * (Layout == 0 ? 4 : 8)); }
		Floats4 rowB(size_t tap) const { return Layout == 0 ? load4(b + tap * 4) : load4(a + tap * 8 + 4); }

		const float* a;
		const float* b;
	};

	// The weights of up to 8 taps from w on.
	// Weights: 0 - loaded 4 at a time, a tap's broadcast from its lane, as the levels' WeightSpreader<4> does; 1 - a tap's broadcast from memory.
	template <int Weights>
	struct StepWeights
	{
		explicit StepWeights(const float* w) : weights(w)
		{
			if constexpr (Weights == 0)
			{
				low = load4(w);
				high = load4(w + 4);
			}
		}

		template <int Tap>
		Floats4 tap() const
		{
			if constexpr (Weights == 1)
				return broadcastLoad(weights + Tap);
			else if constexpr (Tap < 4)
				return broadcastLane<Tap>(low);
			else
				return broadcastLane<Tap - 4>(high);
		}

		const float* weights;
		Floats4 low = zero4(), high = zero4();
	};

	template <bool Start, int Chain, int Layout, int Weights>
	FORCEINLINE void blockChain128(const StepWeights<Weights>& weights, RowPairPixels<Layout> pixels, size_t blockTap, TapPair& chainA, TapPair& chainB)
	{
		const Floats4 lowWeight = weights.template tap<Chain * 2>(), highWeight = weights.template tap<Chain * 2 + 1>();
		const size_t tap = blockTap + Chain * 2;
		if constexpr (Start)
		{
			chainA = { mul4(pixels.rowA(tap), lowWeight), mul4(pixels.rowA(tap + 1), highWeight) };
			chainB = { mul4(pixels.rowB(tap), lowWeight), mul4(pixels.rowB(tap + 1), highWeight) };
		}
		else
		{
			chainA.low = mulAdd4(pixels.rowA(tap), lowWeight, chainA.low); chainA.high = mulAdd4(pixels.rowA(tap + 1), highWeight, chainA.high);
			chainB.low = mulAdd4(pixels.rowB(tap), lowWeight, chainB.low); chainB.high = mulAdd4(pixels.rowB(tap + 1), highWeight, chainB.high);
		}
	}

	template <bool Start, int Layout, int Weights>
	FORCEINLINE void block128(const float* blockWeights, RowPairPixels<Layout> pixels, size_t blockTap, RowChains128& chainsA, RowChains128& chainsB)
	{
		const StepWeights<Weights> weights{ blockWeights };
		blockChain128<Start, 0>(weights, pixels, blockTap, chainsA.chain0, chainsB.chain0);
		blockChain128<Start, 1>(weights, pixels, blockTap, chainsA.chain1, chainsB.chain1);
		if constexpr (levelChainCount == 4)
		{
			blockChain128<Start, 2>(weights, pixels, blockTap, chainsA.chain2, chainsB.chain2);
			blockChain128<Start, 3>(weights, pixels, blockTap, chainsA.chain3, chainsB.chain3);
		}
	}

	inline TapPair sumChains128(const RowChains128& chains)
	{
		if constexpr (levelChainCount == 4)
			return { add4(add4(chains.chain0.low, chains.chain1.low), add4(chains.chain2.low, chains.chain3.low)), add4(add4(chains.chain0.high, chains.chain1.high), add4(chains.chain2.high, chains.chain3.high)) };
		else
			return { add4(chains.chain0.low, chains.chain1.low), add4(chains.chain0.high, chains.chain1.high) };
	}

	// A block's taps at the level's chain count
	constexpr size_t blockTaps128 = levelChainCount * 2;

	// column128's taps from tap on, under a block of them, added to the blocks' sums: filterRun's steps
	template <int Layout, int Weights>
	FORCEINLINE void column128LastTaps(RowPairPixels<Layout> pixels, const float* w, size_t tap, size_t tapCount, TapPair sumA, TapPair sumB, float* outA, float* outB)
	{
		if constexpr (levelChainCount == 4)
		{
			if (tap + 4 <= tapCount)
			{
				const StepWeights<Weights> weights{ w + tap };
				blockChain128<false, 1>(weights, pixels, tap, sumA, sumB);
				blockChain128<false, 0>(weights, pixels, tap, sumA, sumB);
				tap += 4;
			}
		}

		if (tap + 2 <= tapCount)
		{
			const StepWeights<Weights> weights{ w + tap };
			blockChain128<false, 0>(weights, pixels, tap, sumA, sumB);
			tap += 2;
		}

		Floats4 pixelA = add4(sumA.low, sumA.high), pixelB = add4(sumB.low, sumB.high);
		// One tap at most is left
		if (tap < tapCount)
		{
			const Floats4 weight = broadcastLoad(w + tap);
			pixelA = mulAdd4(pixels.rowA(tap), weight, pixelA);
			pixelB = mulAdd4(pixels.rowB(tap), weight, pixelB);
		}
		store4(outA, pixelA);
		store4(outB, pixelB);
	}

	// One column of a row pair with filterRun's steps at the level's chain count. Requires 8 readable floats at every weight.
	template <int Layout, int Weights>
	FORCEINLINE void column128(RowPairPixels<Layout> pixels, const float* w, size_t tapCount, float* outA, float* outB)
	{
		TapPair sumA{ zero4(), zero4() }, sumB{ zero4(), zero4() };
		size_t tap = 0;
		if (tapCount >= blockTaps128)
		{
			RowChains128 chainsA, chainsB;
			block128<true, Layout, Weights>(w, pixels, 0, chainsA, chainsB);
			for (tap = blockTaps128; tap + blockTaps128 <= tapCount; tap += blockTaps128)
				block128<false, Layout, Weights>(w + tap, pixels, tap, chainsA, chainsB);

			sumA = sumChains128(chainsA);
			sumB = sumChains128(chainsB);
		}

		column128LastTaps<Layout, Weights>(pixels, w, tap, tapCount, sumA, sumB, outA, outB);
	}

	// Blocks Block and later of column128Fixed, written out per block: MSVC keeps a loop over a known count as a loop
	template <size_t Blocks, int Layout, int Weights, size_t Block = 0>
	FORCEINLINE void addFixedBlocks128(RowPairPixels<Layout> pixels, const float* w, RowChains128& chainsA, RowChains128& chainsB)
	{
		if constexpr (Block < Blocks)
		{
			block128<Block == 0, Layout, Weights>(w + Block * blockTaps128, pixels, Block * blockTaps128, chainsA, chainsB);
			addFixedBlocks128<Blocks, Layout, Weights, Block + 1>(pixels, w, chainsA, chainsB);
		}
	}

	// column128 for a tap count known to the compiler: no block loop, and of the steps only those the count takes
	template <size_t Taps, int Layout, int Weights>
	FORCEINLINE void column128Fixed(RowPairPixels<Layout> pixels, const float* w, float* outA, float* outB)
	{
		TapPair sumA{ zero4(), zero4() }, sumB{ zero4(), zero4() };
		if constexpr (Taps >= blockTaps128)
		{
			RowChains128 chainsA, chainsB;
			addFixedBlocks128<Taps / blockTaps128, Layout, Weights>(pixels, w, chainsA, chainsB);
			sumA = sumChains128(chainsA);
			sumB = sumChains128(chainsB);
		}

		column128LastTaps<Layout, Weights>(pixels, w, Taps / blockTaps128 * blockTaps128, Taps, sumA, sumB, outA, outB);
	}

	// One row pair, every column, each column's run read from runs as the kernel reads its run table; a run's firstSource is its pixel in the buffers.
	// FixedTaps: every column's tap count, known to the compiler; 0 - read from the run.
	template <int Layout, int Weights, size_t FixedTaps = 0>
	NOINLINE void horizontalRunTable128(const float* a, const float* b, const float* interleaved, const ColumnRun* runs, size_t columns, float* outA, float* outB)
	{
		const ColumnRun* const runsEnd = runs + columns;
		for (const ColumnRun* run = runs; run != runsEnd; ++run, outA += 3, outB += 3)
		{
			const size_t pixel = run->firstSource;
			const RowPairPixels<Layout> pixels = Layout == 0 ? RowPairPixels<Layout>{ a + pixel * 4, b + pixel * 4 } : RowPairPixels<Layout>{ interleaved + pixel * 8, nullptr };
			if constexpr (FixedTaps != 0)
				column128Fixed<FixedTaps, Layout, Weights>(pixels, run->weights, outA, outB);
			else
				column128<Layout, Weights>(pixels, run->weights, run->weightCount, outA, outB);
		}
	}

	// horizontalRunTable128 at 4K's row count: columns runs of maxTaps taps stepQ8 / 256 pixels apart, in a buffer in L1. mixed: see columnTapCounts.
	template <int Layout, int Weights, size_t FixedTaps = 0>
	double runTableSeconds128(size_t maxTaps, size_t stepQ8, size_t columns, bool mixed, int repeats)
	{
		if (FixedTaps != 0 && (FixedTaps != maxTaps || mixed))
			std::abort();

		const size_t capacityPixels = 2 * maxTaps + 128, weightStride = weightStrideFor(maxTaps);
		float* a = alignedFloats(capacityPixels * 4 + 64);
		float* b = alignedFloats(capacityPixels * 4 + 64);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		float* weights = alignedFloats(columns * weightStride + 64);
		for (size_t i = 0; i < columns * weightStride + 64; ++i)
			weights[i] = 1.0f / static_cast<float>(maxTaps);
		float* outA = alignedFloats(columns * 3 + 64);
		float* outB = alignedFloats(columns * 3 + 64);

		const std::vector<uint32_t> tapCounts = columnTapCounts(columns, maxTaps, mixed);
		std::vector<ColumnRun> runs(columns);
		size_t positionQ8 = 0;
		for (size_t column = 0; column < columns; ++column)
		{
			runs[column] = { positionQ8 >> 8, weights + column * weightStride, tapCounts[column] };
			positionQ8 += stepQ8;
			if ((positionQ8 >> 8) + maxTaps > capacityPixels)
				positionQ8 = 0;
		}

		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < rowPairs; ++pair)
				horizontalRunTable128<Layout, Weights, FixedTaps>(a, b, interleaved, runs.data(), columns, outA, outB);
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(a); freeFloats(b); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// The 128-bit arithmetic over a run table for runs of Taps taps, a form's count read, fixed and mixed: see reportRatio
	template <int Layout, int Weights, size_t Taps>
	void reportRunTable128Form(int repeats, const char* ratio, const char* form, size_t stepQ8, size_t columns)
	{
		const auto line = [ratio, form](const char* count, double seconds) {
			std::printf("S  %s | %s | %s | %.2f ms\n", ratio, form, count, seconds * 1e3);
		};
		line("read", runTableSeconds128<Layout, Weights>(Taps, stepQ8, columns, false, repeats));
		line("fixed", runTableSeconds128<Layout, Weights, Taps>(Taps, stepQ8, columns, false, repeats));
		line("mixed", runTableSeconds128<Layout, Weights>(Taps, stepQ8, columns, true, repeats));
	}

	template <size_t Taps>
	void reportRunTable128(int repeats, const char* ratio, size_t stepQ8, size_t columns)
	{
		reportRunTable128Form<0, 0, Taps>(repeats, ratio, "a buffer per row, lanes", stepQ8, columns);
		reportRunTable128Form<0, 1, Taps>(repeats, ratio, "a buffer per row, broadcast", stepQ8, columns);
		reportRunTable128Form<1, 0, Taps>(repeats, ratio, "interleaved, lanes", stepQ8, columns);
		reportRunTable128Form<1, 1, Taps>(repeats, ratio, "interleaved, broadcast", stepQ8, columns);
	}

#ifdef USED_FLOATS4
	// Section U: a 1.5x upscale's 4-tap runs, 720p -> 1080p RGB32, a source buffer per row in L1 and the temp rows' 3 floats per pixel
	constexpr size_t upscaleColumns = 1920, upscaleRowPairs = 720 / 2, upscaleCapacity = 2 * 4 + 128;

	// One row's 4-tap sum. Two chains, as filterRun's step of 16 floats: taps 2 and 3 start the halves, taps 0 and 1 are added, the halves summed.
	// One chain: a multiply and three multiply-adds.
	template <int Chains = 2>
	FORCEINLINE Floats4 fourTapSum(Floats4 p0, Floats4 p1, Floats4 p2, Floats4 p3, Floats4 weights)
	{
		if constexpr (Chains == 1)
		{
			Floats4 sum = mul4(p0, broadcastLane<0>(weights));
			sum = mulAdd4(p1, broadcastLane<1>(weights), sum);
			sum = mulAdd4(p2, broadcastLane<2>(weights), sum);
			return mulAdd4(p3, broadcastLane<3>(weights), sum);
		}
		else
		{
			Floats4 low = mul4(p2, broadcastLane<2>(weights)), high = mul4(p3, broadcastLane<3>(weights));
			low = mulAdd4(p0, broadcastLane<0>(weights), low);
			high = mulAdd4(p1, broadcastLane<1>(weights), high);
			return add4(low, high);
		}
	}

	template <int Chains = 2>
	FORCEINLINE Floats4 fourTapSum(const float* pixels, Floats4 weights)
	{
		return fourTapSum<Chains>(load4(pixels), load4(pixels + 4), load4(pixels + 8), load4(pixels + 12), weights);
	}

	// filterBufferedRuns at a fixed 4 taps, a buffer per row: a run's pixels by its firstSource, tested against the last pixel a run may start at.
	// Variant: 0 - the kernel's; 1 - the sums kept without a store; 2 - row A only; 3 - as 0, one chain per row.
	template <int Variant>
	NOINLINE void upscaleRunTable128(const float* a, const float* b, const ColumnRun* runs, size_t lastPixel, float* outA, float* outB)
	{
		constexpr int chains = Variant == 3 ? 1 : 2;
		const ColumnRun* const runsEnd = runs + upscaleColumns;
		for (const ColumnRun* run = runs; run != runsEnd; ++run, outA += 3, outB += 3)
		{
			const size_t pixel = run->firstSource;
			if (pixel > lastPixel)
				break;

			const Floats4 weights = load4(run->weights);
			const Floats4 sumA = fourTapSum<chains>(a + pixel * 4, weights);
			if constexpr (Variant == 1)
				USED_FLOATS4(sumA);
			else
				store4(outA, sumA);

			if constexpr (Variant != 2)
			{
				const Floats4 sumB = fourTapSum<chains>(b + pixel * 4, weights);
				if constexpr (Variant == 1)
					USED_FLOATS4(sumB);
				else
					store4(outB, sumB);
			}
		}
	}

	FORCEINLINE void useFloats4(Floats4 v) { USED_FLOATS4(v); }

	// Three columns per iteration at 1.5x's run starts, pixels 0, 1 and 2 of the iteration's; the source steps 2 pixels: no run table.
	// The pointers are hidden per iteration, so every load stays in the loop.
	// Variant: 0 - each column's pixels loaded, the sums stored; 1 - as 0, the sums kept without a store; 2 - as 1, the same pixels every iteration;
	//          3 - as 1, the loads alone, no arithmetic; 4 - each source pixel loaded once for the three columns, the sums stored; 5 - as 4, no stores;
	//          6 - as 4, one chain per row.
	template <int Variant>
	NOINLINE void upscaleThreeColumns128(const float* a, const float* b, const float* columnWeights, float* outA, float* outB)
	{
		constexpr bool stores = Variant == 0 || Variant == 4 || Variant == 6;
		constexpr int chains = Variant == 6 ? 1 : 2;
		const auto output = [](float* out, Floats4 sum) {
			if constexpr (stores)
				store4(out, sum);
			else
				USED_FLOATS4(sum);
		};
		// A column's pixels by a pointer of their own: without stores between them, the compilers merge the columns' overlapping loads
		const auto columnPixels = [](const float* rowPixels, size_t column) {
			const float* pixels = rowPixels + column * 4;
			HIDDEN_POINTER(pixels);
			return pixels;
		};
		const auto useFourPixels = [](const float* pixels) {
			useFloats4(load4(pixels)); useFloats4(load4(pixels + 4)); useFloats4(load4(pixels + 8)); useFloats4(load4(pixels + 12));
		};

		size_t offset = 0;
		for (size_t column = 0; column < upscaleColumns; column += 3, outA += 9, outB += 9)
		{
			const float* w = columnWeights;
			const float* pa = a + offset;
			const float* pb = b + offset;
			HIDDEN_POINTER(w);
			HIDDEN_POINTER(pa);
			HIDDEN_POINTER(pb);
			const Floats4 w0 = load4(w), w1 = load4(w + weightStrideFor(4)), w2 = load4(w + 2 * weightStrideFor(4));
			if constexpr (Variant == 3)
			{
				useFloats4(w0); useFloats4(w1); useFloats4(w2);
				useFourPixels(columnPixels(pa, 0)); useFourPixels(columnPixels(pb, 0));
				useFourPixels(columnPixels(pa, 1)); useFourPixels(columnPixels(pb, 1));
				useFourPixels(columnPixels(pa, 2)); useFourPixels(columnPixels(pb, 2));
			}
			else if constexpr (Variant >= 4)
			{
				const Floats4 a0 = load4(pa), a1 = load4(pa + 4), a2 = load4(pa + 8), a3 = load4(pa + 12), a4 = load4(pa + 16), a5 = load4(pa + 20);
				const Floats4 b0 = load4(pb), b1 = load4(pb + 4), b2 = load4(pb + 8), b3 = load4(pb + 12), b4 = load4(pb + 16), b5 = load4(pb + 20);
				output(outA, fourTapSum<chains>(a0, a1, a2, a3, w0)); output(outB, fourTapSum<chains>(b0, b1, b2, b3, w0));
				output(outA + 3, fourTapSum<chains>(a1, a2, a3, a4, w1)); output(outB + 3, fourTapSum<chains>(b1, b2, b3, b4, w1));
				output(outA + 6, fourTapSum<chains>(a2, a3, a4, a5, w2)); output(outB + 6, fourTapSum<chains>(b2, b3, b4, b5, w2));
			}
			else
			{
				output(outA, fourTapSum(columnPixels(pa, 0), w0)); output(outB, fourTapSum(columnPixels(pb, 0), w0));
				output(outA + 3, fourTapSum(columnPixels(pa, 1), w1)); output(outB + 3, fourTapSum(columnPixels(pb, 1), w1));
				output(outA + 6, fourTapSum(columnPixels(pa, 2), w2)); output(outB + 6, fourTapSum(columnPixels(pb, 2), w2));
			}

			if constexpr (Variant != 2)
			{
				offset += 8;
				if (offset + 6 * 4 > upscaleCapacity * 4)
					offset = 0;
			}
		}
	}

	// Columns 0, 1 and 2 of a group at pixels 0, Offset1 and Offset2 of a window of 4 + Offset2 source pixels per row, each loaded once.
	// weightsOf(column): that column's weights.
	template <size_t Offset1, size_t Offset2, int Chains, typename WeightsOf>
	FORCEINLINE void upscaleGroupOfThree128(const float* pa, const float* pb, WeightsOf weightsOf, float* outA, float* outB)
	{
		Floats4 windowA[6], windowB[6];
		windowA[0] = load4(pa); windowA[1] = load4(pa + 4); windowA[2] = load4(pa + 8); windowA[3] = load4(pa + 12);
		windowB[0] = load4(pb); windowB[1] = load4(pb + 4); windowB[2] = load4(pb + 8); windowB[3] = load4(pb + 12);
		if constexpr (Offset2 >= 1)
		{
			windowA[4] = load4(pa + 16);
			windowB[4] = load4(pb + 16);
		}
		if constexpr (Offset2 == 2)
		{
			windowA[5] = load4(pa + 20);
			windowB[5] = load4(pb + 20);
		}

		const auto column = [&]<size_t Offset>(size_t groupColumn, float* columnA, float* columnB) {
			const Floats4 weights = load4(weightsOf(groupColumn));
			store4(columnA, fourTapSum<Chains>(windowA[Offset], windowA[Offset + 1], windowA[Offset + 2], windowA[Offset + 3], weights));
			store4(columnB, fourTapSum<Chains>(windowB[Offset], windowB[Offset + 1], windowB[Offset + 2], windowB[Offset + 3], weights));
		};
		column.template operator()<0>(0, outA, outB);
		column.template operator()<Offset1>(1, outA + 3, outB + 3);
		column.template operator()<Offset2>(2, outA + 6, outB + 6);
	}

	// upscaleColumnGroups128 with a record per group: one 16-byte read, a branch on the recorded pattern, the weights at one address.
	// A group of pattern 4 takes a column at a time from runs.
	template <int Chains>
	NOINLINE void upscaleGroupRecords128(const float* a, const float* b, const GroupRun* groups, const ColumnRun* runs, size_t lastPixel, float* outA, float* outB)
	{
		const auto column = [a, b](const ColumnRun& run, float* columnA, float* columnB) {
			const Floats4 weights = load4(run.weights);
			store4(columnA, fourTapSum<Chains>(a + run.firstSource * 4, weights));
			store4(columnB, fourTapSum<Chains>(b + run.firstSource * 4, weights));
		};

		const GroupRun* const groupsEnd = groups + upscaleColumns / 3;
		for (const GroupRun* group = groups; group != groupsEnd; ++group, outA += 9, outB += 9)
		{
			const size_t first = group->firstSource;
			// One test, as upscaleColumnGroups128 has: a kernel's would hold the window's end
			if (first > lastPixel)
				break;

			const float* pa = a + first * 4;
			const float* pb = b + first * 4;
			const float* const weights = group->weights;
			const auto weightsOf = [weights](size_t groupColumn) { return weights + groupColumn * 4; };
			switch (group->pattern)
			{
			case 0: upscaleGroupOfThree128<0, 0, Chains>(pa, pb, weightsOf, outA, outB); break;
			case 1: upscaleGroupOfThree128<1, 1, Chains>(pa, pb, weightsOf, outA, outB); break;
			case 2: upscaleGroupOfThree128<0, 1, Chains>(pa, pb, weightsOf, outA, outB); break;
			case 3: upscaleGroupOfThree128<1, 2, Chains>(pa, pb, weightsOf, outA, outB); break;
			default:
			{
				const ColumnRun* const run = runs + (group - groups) * 3;
				column(run[0], outA, outB);
				column(run[1], outA + 3, outB + 3);
				column(run[2], outA + 6, outB + 6);
			}
			}
		}
	}

	// upscaleRunTable128<0> three columns at a time, Chains chains per row. A group whose runs start 0 or 1 pixel apart, as an upscale's do,
	// takes the body compiled for its pattern of starts; any other, across the buffer's slide, takes a column at a time.
	template <int Chains>
	NOINLINE void upscaleColumnGroups128(const float* a, const float* b, const ColumnRun* runs, size_t lastPixel, float* outA, float* outB)
	{
		const auto column = [a, b](const ColumnRun& run, float* columnA, float* columnB) {
			const Floats4 weights = load4(run.weights);
			store4(columnA, fourTapSum<Chains>(a + run.firstSource * 4, weights));
			store4(columnB, fourTapSum<Chains>(b + run.firstSource * 4, weights));
		};

		const ColumnRun* run = runs;
		const ColumnRun* const groupsEnd = runs + upscaleColumns / 3 * 3;
		for (; run != groupsEnd; run += 3, outA += 9, outB += 9)
		{
			const size_t first = run[0].firstSource;
			if (run[2].firstSource > lastPixel)
				break;

			const float* pa = a + first * 4;
			const float* pb = b + first * 4;
			const auto weightsOf = [run](size_t groupColumn) { return run[groupColumn].weights; };
			switch (groupPattern(run))
			{
			case 0: upscaleGroupOfThree128<0, 0, Chains>(pa, pb, weightsOf, outA, outB); break;
			case 1: upscaleGroupOfThree128<1, 1, Chains>(pa, pb, weightsOf, outA, outB); break;
			case 2: upscaleGroupOfThree128<0, 1, Chains>(pa, pb, weightsOf, outA, outB); break;
			case 3: upscaleGroupOfThree128<1, 2, Chains>(pa, pb, weightsOf, outA, outB); break;
			default:
				column(run[0], outA, outB);
				column(run[1], outA + 3, outB + 3);
				column(run[2], outA + 6, outB + 6);
			}
		}

		for (const ColumnRun* const runsEnd = runs + upscaleColumns; run != runsEnd; ++run, outA += 3, outB += 3)
			column(*run, outA, outB);
	}

	// Section U's loops at 720p -> 1080p's row pairs, sourceWidth -> 1920 columns:
	//   Loop 0 - upscaleRunTable128<Variant>; 1 - upscaleThreeColumns128<Variant>, 1.5x only; 2 - upscaleColumnGroups128<Variant chains>;
	//   3 - upscaleGroupRecords128<Variant chains>.
	// A run's first pixel as the kernel's builder places it for a 4-tap filter; runs a period apart share their weights, as the kernel's do.
	// Groups a period apart share theirs the same way. lineAligned: every run's first pixel at a 64-byte boundary.
	template <int Loop, int Variant>
	double upscaleSeconds128(size_t sourceWidth, bool lineAligned, int repeats)
	{
		const size_t period = upscaleColumns / std::gcd(sourceWidth, upscaleColumns);
		float* a = alignedFloats(upscaleCapacity * 4 + 64);
		float* b = alignedFloats(upscaleCapacity * 4 + 64);
		float* weights = alignedFloats(period * weightStrideFor(4) + 64);
		for (size_t i = 0; i < period * weightStrideFor(4) + 64; ++i)
			weights[i] = 0.25f;
		float* outA = alignedFloats(upscaleColumns * 3 + 64);
		float* outB = alignedFloats(upscaleColumns * 3 + 64);

		// The span slides to a run that would end past it, as SlidingSourceFloats::prepareRun
		std::vector<ColumnRun> runs(upscaleColumns);
		size_t base = 0;
		for (size_t column = 0; column < upscaleColumns; ++column)
		{
			const double sourcePosition = (static_cast<double>(column) + 0.5) * static_cast<double>(sourceWidth) / upscaleColumns - 0.5;
			const size_t first = static_cast<size_t>(std::clamp(static_cast<int64_t>(std::floor(sourcePosition - 2)) + 1, int64_t{ 0 }, static_cast<int64_t>(sourceWidth) - 4));
			if (first + 4 > base + upscaleCapacity)
				base = first;
			const size_t pixel = lineAligned ? (first - base) & ~size_t{ 3 } : first - base;
			runs[column] = { pixel, weights + column % period * weightStrideFor(4), 4 };
		}

		// A group's columns start at column 3 * group: its weights repeat every period / gcd(period, 3) groups
		const size_t groupPeriod = period / std::gcd(period, size_t{ 3 });
		float* groupWeights = alignedFloats(groupPeriod * 12 + 64);
		for (size_t i = 0; i < groupPeriod * 12 + 64; ++i)
			groupWeights[i] = 0.25f;
		std::vector<GroupRun> groups(upscaleColumns / 3);
		for (size_t group = 0; group < groups.size(); ++group)
		{
			const ColumnRun* const run = runs.data() + group * 3;
			groups[group] = { static_cast<uint32_t>(run[0].firstSource), groupPattern(run), groupWeights + group % groupPeriod * 12 };
		}

		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < upscaleRowPairs; ++pair)
			{
				if constexpr (Loop == 0)
					upscaleRunTable128<Variant>(a, b, runs.data(), upscaleCapacity - 4, outA, outB);
				else if constexpr (Loop == 1)
					upscaleThreeColumns128<Variant>(a, b, weights, outA, outB);
				else if constexpr (Loop == 2)
					upscaleColumnGroups128<Variant>(a, b, runs.data(), upscaleCapacity - 4, outA, outB);
				else
					upscaleGroupRecords128<Variant>(a, b, groups.data(), runs.data(), upscaleCapacity - 4, outA, outB);
			}
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(a); freeFloats(b); freeFloats(weights); freeFloats(groupWeights); freeFloats(outA); freeFloats(outB);
		return seconds;
	}

	// Multiply-adds by lane in independent chains, in registers
	template <size_t... Chain>
	FORCEINLINE void laneMultiplyAddChains(const float* values, size_t iterations, float* out, std::index_sequence<Chain...>)
	{
		Floats4 chains[] = { load4(values + Chain * 4)... };
		const Floats4 pixels = load4(values + sizeof...(Chain) * 4), weights = load4(values + sizeof...(Chain) * 4 + 4);
		for (size_t i = 0; i < iterations; ++i)
			((chains[Chain] = mulAdd4(pixels, broadcastLane<Chain % 4>(weights), chains[Chain])), ...);
		(store4(out + Chain * 4, chains[Chain]), ...);
	}

	// The level's multiply-add throughput, G float multiply-adds per second, over 16 independent chains
	double laneMultiplyAddRate128(int repeats)
	{
		constexpr size_t chains = 16, iterations = size_t{ 1 } << 20;
		float* values = alignedFloats(chains * 4 + 8 + 64);
		float out[chains * 4];
		const double seconds = bestSeconds([&] { laneMultiplyAddChains(values, iterations, out, std::make_index_sequence<chains>{}); }, repeats);
		// Every chain's sum read, so none is dropped
		float total = 0;
		for (const float value : out)
			total += value;
		volatile float sink = total;
		(void)sink;
		freeFloats(values);
		return static_cast<double>(iterations) * chains * 4 / seconds / 1e9;
	}
#endif // USED_FLOATS4

	// horizontalRowPairRuns for column128<Layout, Weights>
	template <int Layout, int Weights>
	NOINLINE void horizontalRowPairRuns128(const float* a, const float* b, const float* interleaved, const float* columnWeights, const uint32_t* tapCounts,
		size_t weightStride, size_t stepQ8, size_t capacityPixels, float* outA, float* outB)
	{
		size_t positionQ8 = 0;
		for (size_t column = 0; column < destWidth; ++column)
		{
			const size_t pixel = positionQ8 >> 8;
			const RowPairPixels<Layout> pixels = Layout == 0 ? RowPairPixels<Layout>{ a + pixel * 4, b + pixel * 4 } : RowPairPixels<Layout>{ interleaved + pixel * 8, nullptr };
			column128<Layout, Weights>(pixels, columnWeights + column * weightStride, tapCounts[column], outA + column * 3, outB + column * 3);

			positionQ8 += stepQ8;
			if ((positionQ8 >> 8) + weightStride > capacityPixels)
				positionQ8 = 0;
		}
	}

	using RowPairRuns = void (*)(const float*, const float*, const float*, const float*, const uint32_t*, size_t, size_t, size_t, float*, float*);

	// A horizontalRowPairRuns function at 4K -> 1080p's row and column counts, in G multiply-adds per second.
	// Every column has maxTaps taps; mixed: maxTaps or one fewer, unpredictably. The source step is that of a 6-lobe-wide filter: maxTaps / 6 pixels.
	double runsRate(RowPairRuns rowPairRuns, size_t maxTaps, bool mixed, int repeats)
	{
		const size_t capacityPixels = 2 * maxTaps + 128;
		float* a = alignedFloats(capacityPixels * 4 + 64);
		float* b = alignedFloats(capacityPixels * 4 + 64);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		const size_t weightCount = destWidth * weightStrideFor(maxTaps) + 64;
		float* weights = alignedFloats(weightCount);
		for (size_t i = 0; i < weightCount; ++i)
			weights[i] = 1.0f / static_cast<float>(maxTaps);
		float* outA = alignedFloats(destWidth * 3 + 64);
		float* outB = alignedFloats(destWidth * 3 + 64);

		const std::vector<uint32_t> tapCounts = columnTapCounts(destWidth, maxTaps, mixed);
		double multiplyAdds = 0;
		for (const uint32_t tapCount : tapCounts)
			multiplyAdds += static_cast<double>(tapCount) * 4 * sourceHeight;

		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < rowPairs; ++pair)
				rowPairRuns(a, b, interleaved, weights, tapCounts.data(), maxTaps, maxTaps * 256 / 6, capacityPixels, outA, outB);
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(a); freeFloats(b); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
		return multiplyAdds / seconds / 1e9;
	}

	[[maybe_unused]] void report(const char* name, double seconds, double multiplyAdds)
	{
		std::printf("%-58s %7.2f ms  %6.1f G/s\n", name, seconds * 1e3, multiplyAdds / seconds / 1e9);
	}

	[[maybe_unused]] void reportTime(const char* name, double seconds)
	{
		std::printf("%-58s %7.2f ms\n", name, seconds * 1e3);
	}

#ifdef __AVX2__
	// The whole pass for runs of Taps taps stepQ8 / 256 pixels apart, per form, three ways:
	//   read - the count read per column, every column's the same;
	//   fixed - the count known to the compiler, as a pass compiled per run length would have it;
	//   mixed - read, Taps or one fewer: the runs a fixed count would pad with a zero weight.
	// Fixed: the form has a fixed-count pass of its own. The interleaved loops and steps share one per weight source.
	template <int Form, int Conversion, size_t Taps, bool Fixed>
	void reportRatioForm(const std::vector<uint8_t>& image, int repeats, const char* ratio, const char* form, size_t stepQ8, size_t columns)
	{
		const auto line = [ratio, form](const char* count, double seconds) {
			std::printf("R  %s | %s | %s | %.2f ms\n", ratio, form, count, seconds * 1e3);
		};
		line("read", passSeconds<Form, Conversion>(image, 0, repeats, Taps, stepQ8, columns));
		if constexpr (Fixed)
			line("fixed", passSeconds<Form, Conversion, Taps>(image, 0, repeats, Taps, stepQ8, columns));
		line("mixed", passSeconds<Form, Conversion>(image, 0, repeats, Taps, stepQ8, columns, true));
	}

	template <size_t Taps>
	void reportRatio(const std::vector<uint8_t>& image, int repeats, const char* ratio, size_t stepQ8, size_t columns)
	{
		reportRatioForm<0, 0, Taps, true>(image, repeats, ratio, "kernel's", stepQ8, columns);
		reportRatioForm<4, 1, Taps, true>(image, repeats, ratio, "4 chains, broadcast", stepQ8, columns);
		reportRatioForm<8, 1, Taps, false>(image, repeats, ratio, "steps, broadcast", stepQ8, columns);
		reportRatioForm<14, 1, Taps, true>(image, repeats, ratio, "4 chains, lanes", stepQ8, columns);
		reportRatioForm<18, 1, Taps, false>(image, repeats, ratio, "steps, lanes", stepQ8, columns);
	}
#endif
}

int main(int argc, char** argv)
{
	const int repeats = argc > 1 ? std::atoi(argv[1]) : 30;
	const char* const sections = argc > 2 ? argv[2] : "HVTFCPRSNUY";
	const auto selected = [sections](char section) { return std::strchr(sections, section) != nullptr; };

	if (selected('N'))
	{
		std::printf("N  128-bit horizontal, %d chains, the tap count read per column, G/s:\n", levelChainCount);
		std::printf("N  a buffer per row, weights from lanes (the level's form) | a buffer per row, weights broadcast | interleaved, lanes | interleaved, broadcast\n");
		const auto reportRuns = [repeats](size_t maxTaps, bool mixed) {
			std::printf("N  %2zu taps%-20s %6.1f  %6.1f  %6.1f  %6.1f\n", maxTaps, mixed ? ", or one fewer" : "",
				runsRate(horizontalRowPairRuns128<0, 0>, maxTaps, mixed, repeats), runsRate(horizontalRowPairRuns128<0, 1>, maxTaps, mixed, repeats),
				runsRate(horizontalRowPairRuns128<1, 0>, maxTaps, mixed, repeats), runsRate(horizontalRowPairRuns128<1, 1>, maxTaps, mixed, repeats));
		};
		for (const size_t maxTaps : { 4, 6, 8, 12, 16, 24, 48 })
			reportRuns(maxTaps, false);
		reportRuns(12, true);
	}

	if (selected('S'))
	{
		// Section R's run lengths, arithmetic only
		reportRunTable128<5>(repeats, "1.25x, 5 taps", 320, 3072);
		reportRunTable128<7>(repeats, "1.17x, 7 taps", 299, 3287);
		reportRunTable128<9>(repeats, "1.5x, 9 taps", 384, 2560);
		reportRunTable128<12>(repeats, "2x, 12 taps", 2 * 256, 1920);
		reportRunTable128<18>(repeats, "3x, 18 taps", 3 * 256, 1280);
		reportRunTable128<24>(repeats, "4x, 24 taps", 4 * 256, 958);
	}

#ifdef USED_FLOATS4
	if (selected('U'))
	{
		const auto line = [](const char* name, double seconds) {
			std::printf("%-58s %7.2f ms  %5.2f ns per column\n", name, seconds * 1e3, seconds * 1e9 / (upscaleRowPairs * upscaleColumns));
		};
		line("U0 1.5x, 4-tap runs: the kernel's loop over a run table", upscaleSeconds128<0, 0>(1280, false, repeats));
		line("U1 as U0, no stores", upscaleSeconds128<0, 1>(1280, false, repeats));
		line("U2 as U0, every run at a 64-byte boundary", upscaleSeconds128<0, 0>(1280, true, repeats));
		line("U3 as U0, one row", upscaleSeconds128<0, 2>(1280, false, repeats));
		line("U4 three columns per iteration, no run table", upscaleSeconds128<1, 0>(1280, false, repeats));
		line("U5 as U4, no stores", upscaleSeconds128<1, 1>(1280, false, repeats));
		line("U6 as U5, the same pixels every iteration", upscaleSeconds128<1, 2>(1280, false, repeats));
		line("U7 as U5, the loads alone", upscaleSeconds128<1, 3>(1280, false, repeats));
		line("U8 as U4, each pixel loaded once for three columns", upscaleSeconds128<1, 4>(1280, false, repeats));
		line("U9 as U8, no stores", upscaleSeconds128<1, 5>(1280, false, repeats));
		line("U10 as U0, one chain per row", upscaleSeconds128<0, 3>(1280, false, repeats));
		line("U11 as U8, one chain per row", upscaleSeconds128<1, 6>(1280, false, repeats));
		std::printf("U12 multiply-adds by lane, 16 chains in registers: %.2f G/s\n", laneMultiplyAddRate128(repeats));
		// Groups of three columns, from the run table and from records, against the kernel's loop, by ratio
		std::printf("U13 ns per column, -> 1920 columns:  run table, 2 chains | 1 chain | groups of three, 2 chains | 1 chain | group records, 1 chain\n");
		for (const size_t sourceWidth : { 1707, 1440, 1366, 1280, 960, 640 })
		{
			std::printf("U13 %4zu, %.3fx: %6.2f %6.2f %6.2f %6.2f %6.2f\n", sourceWidth, static_cast<double>(upscaleColumns) / static_cast<double>(sourceWidth),
				upscaleSeconds128<0, 0>(sourceWidth, false, repeats) * 1e9 / (upscaleRowPairs * upscaleColumns),
				upscaleSeconds128<0, 3>(sourceWidth, false, repeats) * 1e9 / (upscaleRowPairs * upscaleColumns),
				upscaleSeconds128<2, 2>(sourceWidth, false, repeats) * 1e9 / (upscaleRowPairs * upscaleColumns),
				upscaleSeconds128<2, 1>(sourceWidth, false, repeats) * 1e9 / (upscaleRowPairs * upscaleColumns),
				upscaleSeconds128<3, 1>(sourceWidth, false, repeats) * 1e9 / (upscaleRowPairs * upscaleColumns));
		}
	}
#endif

#ifdef __AVX2__
	const double horizontalWork = static_cast<double>(sourceHeight) * destWidth * taps * 4;

	if (selected('T'))
	{
		std::printf("T  horizontal, the tap count read per column, G/s:  kernel's form | interleaved, 2 chains | 3 chains | 4 chains | in steps | 4 chains, lane weights | in steps, lane weights\n");
		const auto reportRuns = [repeats](size_t maxTaps, bool mixed) {
			std::printf("T  %2zu taps%-20s %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f  %6.1f\n", maxTaps, mixed ? ", or one fewer" : "",
				runsRate(horizontalRowPairRuns<0>, maxTaps, mixed, repeats), runsRate(horizontalRowPairRuns<2>, maxTaps, mixed, repeats),
				runsRate(horizontalRowPairRuns<3>, maxTaps, mixed, repeats), runsRate(horizontalRowPairRuns<4>, maxTaps, mixed, repeats),
				runsRate(horizontalRowPairRuns<8>, maxTaps, mixed, repeats), runsRate(horizontalRowPairRuns<14>, maxTaps, mixed, repeats),
				runsRate(horizontalRowPairRuns<18>, maxTaps, mixed, repeats));
		};
		for (const size_t maxTaps : { 4, 5, 6, 7, 8, 10, 12, 16, 24, 48 })
			reportRuns(maxTaps, false);
		reportRuns(12, true);
	}

	if (selected('F'))
	{
		const double fourTapWork = 720.0 * 3840 * 4 * 4;
		report("F0 four taps, weights spread over a pixel pair", fourTapSeconds<0>(repeats), fourTapWork);
		report("F1 four taps, rows interleaved, one chain", fourTapSeconds<1>(repeats), fourTapWork);
		report("F2 four taps, rows interleaved, two chains", fourTapSeconds<2>(repeats), fourTapWork);
		report("F3 as F2, lane weights", fourTapSeconds<3>(repeats), fourTapWork);
		report("F4 as F1, lane weights", fourTapSeconds<4>(repeats), fourTapWork);
		report("F5 as F0, each column's run read from a run table", fourTapRunTableSeconds<0>(repeats), fourTapWork);
		report("F6 as F1, from a run table", fourTapRunTableSeconds<1>(repeats), fourTapWork);
		report("F7 as F2, from a run table", fourTapRunTableSeconds<2>(repeats), fourTapWork);
		report("F8 as F3, from a run table", fourTapRunTableSeconds<3>(repeats), fourTapWork);
		report("F9 as F4, from a run table", fourTapRunTableSeconds<4>(repeats), fourTapWork);
		report("F10 as F8, four chains: the fixed-count column at 4 taps", fourTapRunTableSeconds<5>(repeats), fourTapWork);
		report("F11 as F9, three columns from a record per group, each pixel pair loaded once", fourTapGroupRecordsSeconds<0>(repeats), fourTapWork);
		report("F12 as F11, weights broadcast from memory", fourTapGroupRecordsSeconds<1>(repeats), fourTapWork);
	}

	if (selected('C') || selected('P') || selected('R'))
	{
		const std::vector<uint8_t> image = sourceImage();
		if (selected('C'))
		{
			for (const bool wholeImage : { false, true })
			{
				std::printf("C  conversion of a 4K RGB32 image, source %s\n", wholeImage ? "the whole image" : "one row pair in L2");
				reportTime("C0 each row to its own floats", conversionSeconds<0>(image, wholeImage, repeats));
				reportTime("C1 interleaved, bytes unpacked into pixel pairs", conversionSeconds<1>(image, wholeImage, repeats));
				reportTime("C2 interleaved, lanes permuted into pixel pairs", conversionSeconds<2>(image, wholeImage, repeats));
				reportTime("C3 interleaved, 16-byte stores", conversionSeconds<3>(image, wholeImage, repeats));
			}
		}

		if (selected('P'))
		{
			report("P0 whole pass, the kernel's form", passSeconds<0, 0>(image, 0, repeats), horizontalWork);
			report("P1 as P0, the span 16 bytes off a 32-byte boundary", passSeconds<0, 0>(image, 4, repeats), horizontalWork);
			report("P2 whole pass, interleaved, 3 chains, conversion C1", passSeconds<3, 1>(image, 0, repeats), horizontalWork);
			report("P3 as P2, conversion C2", passSeconds<3, 2>(image, 0, repeats), horizontalWork);
			report("P4 as P2, conversion C3", passSeconds<3, 3>(image, 0, repeats), horizontalWork);
			report("P5 as P2, 2 chains", passSeconds<2, 1>(image, 0, repeats), horizontalWork);
			report("P6 as P2, 4 chains", passSeconds<4, 1>(image, 0, repeats), horizontalWork);
			report("P7 as P2, the span 16 bytes off a 32-byte boundary", passSeconds<3, 1>(image, 4, repeats), horizontalWork);
			report("P8 as P2, the taps in steps", passSeconds<8, 1>(image, 0, repeats), horizontalWork);
			report("P9 as P6, lane weights", passSeconds<14, 1>(image, 0, repeats), horizontalWork);
			report("P10 as P8, lane weights", passSeconds<18, 1>(image, 0, repeats), horizontalWork);

		}

		if (selected('R'))
		{
			// A 6-lobe-wide filter's runs, and a 4-lobe-wide one's at 1.25x: the step is the ratio, the columns 3840 over it.
			// Runs at odd pixel offsets: every other one where the step is 3 pixels, two of every four at 1.5, none at 2 and 4.
			// 7 taps take all of the 4-, 2- and 1-tap steps and no block.
			reportRatio<5>(image, repeats, "1.25x, 5 taps", 320, 3072);
			reportRatio<7>(image, repeats, "1.17x, 7 taps", 299, 3287);
			reportRatio<9>(image, repeats, "1.5x, 9 taps", 384, 2560);
			reportRatio<12>(image, repeats, "2x, 12 taps", 2 * 256, 1920);
			reportRatio<18>(image, repeats, "3x, 18 taps", 3 * 256, 1280);
			reportRatio<24>(image, repeats, "4x, 24 taps", 4 * 256, 958);
		}
	}

	if (selected('Y'))
	{
		// 720p -> 4K's rows, 3x: 4 taps over 720 temp rows, 7 strips of a 3840-pixel row, a ring of 8 temp rows.
		// 4K -> 1080p's, 2x: 12 taps over 2160, 3 strips of 1920 pixels, a ring of 16. Rates count each dest row's own taps.
		const double upscaleWork = 2160.0 * 7 * yStripFloats * 4, downscaleWork = 1080.0 * 3 * yStripFloats * 12;
		report("Y0 3x, 4 taps: the kernel's form, 6 accumulators", verticalTapLoopSeconds<6>(720, 2160, 4, 8, 7, repeats), upscaleWork);
		report("Y1 as Y0, 9 accumulators", verticalTapLoopSeconds<9>(720, 2160, 4, 8, 7, repeats), upscaleWork);
		report("Y2 as Y0, 12 accumulators", verticalTapLoopSeconds<12>(720, 2160, 4, 8, 7, repeats), upscaleWork);
		report("Y3 as Y0, weights and rows held across the dest row", verticalTapLoopSeconds<1>(720, 2160, 4, 8, 7, repeats), upscaleWork);
		report("Y4 as Y0, two dest rows over their taps' union", verticalTapLoopSeconds<2>(720, 2160, 4, 8, 7, repeats), upscaleWork);
		report("Y5 2x, 12 taps: the kernel's form, 6 accumulators", verticalTapLoopSeconds<6>(2160, 1080, 12, 16, 3, repeats), downscaleWork);
		report("Y6 as Y5, 9 accumulators", verticalTapLoopSeconds<9>(2160, 1080, 12, 16, 3, repeats), downscaleWork);
		report("Y7 as Y5, 12 accumulators", verticalTapLoopSeconds<12>(2160, 1080, 12, 16, 3, repeats), downscaleWork);
		report("Y8 as Y5, two dest rows over their taps' union", verticalTapLoopSeconds<2>(2160, 1080, 12, 16, 3, repeats), downscaleWork);
	}

	if (!selected('H') && !selected('V'))
		return 0;

	report("H0 horizontal, weights in registers", horizontalSeconds<0>(repeats), horizontalWork);
	report("H1 horizontal, weights loaded and spread per column", horizontalSeconds<1>(repeats), horizontalWork);
	report("H2 as H1, two chains per row", horizontalSeconds<12>(repeats), horizontalWork);
	report("H3 as H1, one chain per row", horizontalSeconds<11>(repeats), horizontalWork);
	report("H4 a tap per multiply-add, pair by insert, permuted w", horizontalSeconds<20>(repeats), horizontalWork);
	report("H5 a tap per multiply-add, pair by insert, w from memory", horizontalSeconds<21>(repeats), horizontalWork);
	report("H6 a tap per multiply-add, rows interleaved, permuted w", horizontalSeconds<22>(repeats), horizontalWork);
	report("H7 a tap per multiply-add, rows interleaved, w from memory", horizontalSeconds<23>(repeats), horizontalWork);

	double work = 0;
	double seconds = verticalSeconds<0>(640 * 3, 14, repeats, work);
	report("V0 vertical, ring 105 KB (L2), sums to scratch", seconds, work);
	seconds = verticalSeconds<1>(640 * 3, 14, repeats, work);
	report("V1 vertical, ring 105 KB (L2), packed to dest bytes", seconds, work);
	seconds = verticalSeconds<0>(128, 14, repeats, work);
	report("V2 vertical, ring 7 KB (L1), sums to scratch", seconds, work);
	seconds = verticalSeconds<1>(128, 14, repeats, work);
	report("V3 vertical, ring 7 KB (L1), packed to dest bytes", seconds, work);
	seconds = verticalSeconds<0>(destWidth * 3, 14, repeats, work);
	report("V4 vertical, whole rows, ring 315 KB (past L2), scratch", seconds, work);
#endif
	return 0;
}
