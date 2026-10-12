// The multiply-adds of the resizer's two passes at the sizes of 4K -> 1080p RGB32, without the kernel's bookkeeping:
// the ceilings doc/throughput-ceiling.md compares the kernels against, and the horizontal forms it ranks.
// Standalone. AVX2 + FMA:
//   cl /O2 /MT /arch:AVX2 /QIntel-jcc-erratum /std:c++20 /EHsc multiply_add_ceiling.cpp
//   clang-cl /O2 /MT /arch:AVX2 -mfma /clang:-mbranches-within-32B-boundaries /std:c++20 /EHsc multiply_add_ceiling.cpp
//   g++ -O2 -mavx2 -mfma -std=c++20 multiply_add_ceiling.cpp
// The jump-alignment options are the kernels': without them lines differ by up to 14% between builds on the Skylake family.
// Without /arch:AVX2, or for ARM64 (g++ -O2 -std=c++20), only section N exists: the 128-bit levels' horizontal form.
// Run: multiply_add_ceiling [repeats, default 30] [sections, default HVTFCPN]; each line is the best repeat.
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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
				s0 = _mm256_mul_ps(pixels(0), _mm256_permute_ps(w0, 0x00)); s1 = _mm256_mul_ps(pixels(1), _mm256_permute_ps(w0, 0x55)); s2 = _mm256_mul_ps(pixels(2), _mm256_permute_ps(w0, 0xAA));
				s0 = _mm256_fmadd_ps(pixels(3), _mm256_permute_ps(w0, 0xFF), s0); s1 = _mm256_fmadd_ps(pixels(4), _mm256_permute_ps(w1, 0x00), s1); s2 = _mm256_fmadd_ps(pixels(5), _mm256_permute_ps(w1, 0x55), s2);
				s0 = _mm256_fmadd_ps(pixels(6), _mm256_permute_ps(w1, 0xAA), s0); s1 = _mm256_fmadd_ps(pixels(7), _mm256_permute_ps(w1, 0xFF), s1); s2 = _mm256_fmadd_ps(pixels(8), _mm256_permute_ps(w2, 0x00), s2);
				s0 = _mm256_fmadd_ps(pixels(9), _mm256_permute_ps(w2, 0x55), s0); s1 = _mm256_fmadd_ps(pixels(10), _mm256_permute_ps(w2, 0xAA), s1); s2 = _mm256_fmadd_ps(pixels(11), _mm256_permute_ps(w2, 0xFF), s2);
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
			for (tap = 8; tap + 8 <= tapCount; tap += 8)
			{
				weights = _mm256_loadu_ps(w + tap);
				w0 = _mm256_permutevar8x32_ps(weights, indices.i0); w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
				w2 = _mm256_permutevar8x32_ps(weights, indices.i2); w3 = _mm256_permutevar8x32_ps(weights, indices.i3);
				const float* blockA = pa + tap * 4;
				const float* blockB = pb + tap * 4;
				a0 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA), w0, a0); a1 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 8), w1, a1);
				a2 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 16), w2, a2); a3 = _mm256_fmadd_ps(_mm256_loadu_ps(blockA + 24), w3, a3);
				b0 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB), w0, b0); b1 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 8), w1, b1);
				b2 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 16), w2, b2); b3 = _mm256_fmadd_ps(_mm256_loadu_ps(blockB + 24), w3, b3);
			}
			sumA = _mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3));
			sumB = _mm256_add_ps(_mm256_add_ps(b0, b1), _mm256_add_ps(b2, b3));
		}

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
		for (; tap < tapCount; ++tap)
		{
			const __m128 weight = _mm_broadcast_ss(w + tap);
			pixelA = _mm_fmadd_ps(_mm_loadu_ps(pa + tap * 4), weight, pixelA);
			pixelB = _mm_fmadd_ps(_mm_loadu_ps(pb + tap * 4), weight, pixelB);
		}
		_mm_storeu_ps(outA, pixelA);
		_mm_storeu_ps(outB, pixelB);
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
				return _mm256_permute_ps(loaded, Tap * 0x55);
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
		size_t weightStride, size_t stepQ8, size_t capacityPixels, float* outA, float* outB)
	{
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
			if ((positionQ8 >> 8) + weightStride > capacityPixels)
				positionQ8 = 0;
		}
	}

	// One row pair of an upscale, every column: 4 taps of 4-float pixels, no tap count. Requires 8 readable floats at every column's weights.
	// Form: 0 - filterFourTapRun's: the weights spread over a pixel pair, halves summed;
	//       1, 2 - a tap per multiply-add over interleaved pixels in Form chains, each weight broadcast from memory;
	//       3 - as 2, the 4 weights loaded into both lanes and permuted in-lane.
	template <int Form>
	NOINLINE void fourTapRowPair(const float* a, const float* b, const float* interleaved, const float* columnWeights, size_t columns, size_t stepQ8, size_t capacityPixels, float* outA, float* outB)
	{
		[[maybe_unused]] const SpreadIndices indices;
		size_t positionQ8 = 0;
		for (size_t column = 0; column < columns; ++column)
		{
			const size_t pixel = positionQ8 >> 8;
			const float* w = columnWeights + column * 4;
			if constexpr (Form == 0)
			{
				const __m256 weights = _mm256_loadu_ps(w);
				const __m256 w0 = _mm256_permutevar8x32_ps(weights, indices.i0), w1 = _mm256_permutevar8x32_ps(weights, indices.i1);
				const float* pa = a + pixel * 4;
				const float* pb = b + pixel * 4;
				_mm_storeu_ps(outA + column * 3, sumHalves(_mm256_fmadd_ps(_mm256_loadu_ps(pa), w0, _mm256_mul_ps(_mm256_loadu_ps(pa + 8), w1))));
				_mm_storeu_ps(outB + column * 3, sumHalves(_mm256_fmadd_ps(_mm256_loadu_ps(pb), w0, _mm256_mul_ps(_mm256_loadu_ps(pb + 8), w1))));
			}
			else
			{
				const float* p = interleaved + pixel * 8;
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
					const __m256 weights = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(w));
					__m256 s0 = _mm256_mul_ps(_mm256_loadu_ps(p), _mm256_permute_ps(weights, 0x00)), s1 = _mm256_mul_ps(_mm256_loadu_ps(p + 8), _mm256_permute_ps(weights, 0x55));
					s0 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 16), _mm256_permute_ps(weights, 0xAA), s0); s1 = _mm256_fmadd_ps(_mm256_loadu_ps(p + 24), _mm256_permute_ps(weights, 0xFF), s1);
					sum = _mm256_add_ps(s0, s1);
				}
				_mm_storeu_ps(outA + column * 3, _mm256_castps256_ps128(sum));
				_mm_storeu_ps(outB + column * 3, _mm256_extractf128_ps(sum, 1));
			}

			positionQ8 += stepQ8;
			if ((positionQ8 >> 8) + 4 > capacityPixels)
				positionQ8 = 0;
		}
	}

	inline __m256 widenBytes(__m128i bytes) { return _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(bytes)); }
	inline __m128i highHalf(__m128i v) { return _mm_unpackhi_epi64(v, v); }

	// One row's RGB32 pixels to 4 floats each, as convertPixelsToFloats does: 4 pixels per 16-byte load, then single pixels
	FORCEINLINE void convertRow(const uint8_t* row, float* floats, size_t pixelCount)
	{
		size_t pixel = 0;
		for (; pixel + 4 <= pixelCount; pixel += 4)
		{
			const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(row + pixel * 4));
			_mm256_storeu_ps(floats + pixel * 4, widenBytes(bytes));
			_mm256_storeu_ps(floats + pixel * 4 + 8, widenBytes(highHalf(bytes)));
		}

		for (; pixel < pixelCount; ++pixel)
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
			size_t pixel = 0;
			for (; pixel + 4 <= pixelCount; pixel += 4)
			{
				const __m128i bytesA = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rowA + pixel * 4));
				const __m128i bytesB = _mm_loadu_si128(reinterpret_cast<const __m128i*>(rowB + pixel * 4));
				float* out = interleaved + pixel * 8;
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

			for (; pixel < pixelCount; ++pixel)
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
	// alternating as filterHorizontalRowGroup does. A column's run starts at column * stepQ8 / 256; there is no run lookup.
	// Form: kernelFormColumn (0) or interleavedFormColumn<Form>. Conversion: convertRowPair's Layout, 0 with Form 0 only.
	template <int Form, int Conversion>
	NOINLINE void horizontalPassRowPair(const uint8_t* rowA, const uint8_t* rowB, size_t sourceWidth, float* floatsA, float* floatsB, float* interleaved, size_t capacity,
		const float* columnWeights, const uint32_t* tapCounts, size_t weightStride, size_t stepQ8, size_t columns, float* outA, float* outB)
	{
		static_assert((Form == 0) == (Conversion == 0));
		constexpr size_t backMargin = 8, conversionChunk = 128;
		[[maybe_unused]] const SpreadIndices indices;
		size_t base = 0, converted = 0;
		// Stepped per column, not multiplied by it: the kernel has no multiply per run
		size_t positionQ8 = 0;
		const float* w = columnWeights;
		for (size_t column = 0; column < columns;)
		{
			// SlidingSourceFloats::prepareRun
			const size_t first = positionQ8 >> 8, end = first + tapCounts[column];
			if (end > base + capacity)
			{
				const size_t newBase = first > backMargin ? first - backMargin : 0;
				if (newBase < converted)
				{
					if constexpr (Form == 0)
					{
						std::memmove(floatsA, floatsA + (newBase - base) * 4, (converted - newBase) * 4 * sizeof(float));
						std::memmove(floatsB, floatsB + (newBase - base) * 4, (converted - newBase) * 4 * sizeof(float));
					}
					else
						std::memmove(interleaved, interleaved + (newBase - base) * 8, (converted - newBase) * 8 * sizeof(float));
				}
				else
					converted = newBase;

				base = newBase;
			}

			if (end > converted)
			{
				const size_t target = std::min(std::min(std::max(end, converted + conversionChunk), base + capacity), sourceWidth);
				const size_t bufferPixel = converted - base;
				convertRowPair<Conversion>(rowA + converted * 4, rowB + converted * 4, floatsA + bufferPixel * 4, floatsB + bufferPixel * 4, interleaved + bufferPixel * 8, target - converted);
				converted = target;
			}

			// filterBufferedRuns
			for (; column < columns; ++column, positionQ8 += stepQ8, w += weightStride, outA += 3, outB += 3)
			{
				const size_t firstPixel = positionQ8 >> 8, tapCount = tapCounts[column];
				if (firstPixel < base || firstPixel + tapCount > converted)
					break;

				const size_t bufferPixel = firstPixel - base;
				if constexpr (Form == 0)
					kernelFormColumn(indices, floatsA + bufferPixel * 4, floatsB + bufferPixel * 4, w, tapCount, outA, outB);
				else
					interleavedFormColumn<Form>(interleaved + bufferPixel * 8, w, tapCount, outA, outB);
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
		constexpr size_t columns = 3840, capacityPixels = 2 * 4 + 136;
		float* a = alignedFloats(capacityPixels * 4 + 64);
		float* b = alignedFloats(capacityPixels * 4 + 64);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		float* weights = alignedFloats(columns * 4 + 64);
		for (size_t i = 0; i < columns * 4 + 64; ++i)
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

	// horizontalPassRowPair<Form, Conversion> over a 4K RGB32 image: columns runs of passTaps taps, stepQ8 / 256 pixels apart.
	// spanOffsetFloats: the span's start past a 64-byte boundary, in floats.
	template <int Form, int Conversion>
	double passSeconds(const std::vector<uint8_t>& image, size_t spanOffsetFloats, int repeats, size_t passTaps = taps, size_t stepQ8 = 2 * 256, size_t columns = destWidth)
	{
		const size_t capacity = 2 * passTaps + 136;
		float* floatsA = alignedFloats(capacity * 4 + 64);
		float* floatsB = alignedFloats(capacity * 4 + 64);
		float* interleaved = alignedFloats(capacity * 8 + 128);
		float* weights = alignedFloats(columns * passTaps + 64);
		for (size_t i = 0; i < columns * passTaps + 64; ++i)
			weights[i] = 1.0f / static_cast<float>(passTaps);
		float* outA = alignedFloats(columns * 3 + 64);
		float* outB = alignedFloats(columns * 3 + 64);
		const std::vector<uint32_t> tapCounts(columns, static_cast<uint32_t>(passTaps));
		const double seconds = bestSeconds([&] {
			for (size_t pair = 0; pair < rowPairs; ++pair)
			{
				const uint8_t* rowA = image.data() + pair * 2 * sourceRowBytes;
				horizontalPassRowPair<Form, Conversion>(rowA, rowA + sourceRowBytes, sourceRowPixels, floatsA + spanOffsetFloats, floatsB + spanOffsetFloats, interleaved + spanOffsetFloats,
					capacity, weights, tapCounts.data(), passTaps, stepQ8, columns, outA, outB);
			}
		}, repeats);
		volatile float sink = outA[100] + outB[200];
		(void)sink;
		freeFloats(floatsA); freeFloats(floatsB); freeFloats(interleaved); freeFloats(weights); freeFloats(outA); freeFloats(outB);
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

	// One column of a row pair with filterRun's steps at the level's chain count. Requires 8 readable floats at every weight.
	template <int Layout, int Weights>
	FORCEINLINE void column128(RowPairPixels<Layout> pixels, const float* w, size_t tapCount, float* outA, float* outB)
	{
		constexpr size_t blockTaps = levelChainCount * 2;
		TapPair sumA{ zero4(), zero4() }, sumB{ zero4(), zero4() };
		size_t tap = 0;
		if (tapCount >= blockTaps)
		{
			RowChains128 chainsA, chainsB;
			block128<true, Layout, Weights>(w, pixels, 0, chainsA, chainsB);
			for (tap = blockTaps; tap + blockTaps <= tapCount; tap += blockTaps)
				block128<false, Layout, Weights>(w + tap, pixels, tap, chainsA, chainsB);

			sumA = sumChains128(chainsA);
			sumB = sumChains128(chainsB);
		}

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
		for (; tap < tapCount; ++tap)
		{
			const Floats4 weight = broadcastLoad(w + tap);
			pixelA = mulAdd4(pixels.rowA(tap), weight, pixelA);
			pixelB = mulAdd4(pixels.rowB(tap), weight, pixelB);
		}
		store4(outA, pixelA);
		store4(outB, pixelB);
	}

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
		const size_t capacityPixels = 2 * maxTaps + 136;
		float* a = alignedFloats(capacityPixels * 4 + 64);
		float* b = alignedFloats(capacityPixels * 4 + 64);
		float* interleaved = alignedFloats(capacityPixels * 8 + 128);
		const size_t weightCount = destWidth * maxTaps + 64;
		float* weights = alignedFloats(weightCount);
		for (size_t i = 0; i < weightCount; ++i)
			weights[i] = 1.0f / static_cast<float>(maxTaps);
		float* outA = alignedFloats(destWidth * 3 + 64);
		float* outB = alignedFloats(destWidth * 3 + 64);

		std::vector<uint32_t> tapCounts(destWidth, static_cast<uint32_t>(maxTaps));
		double multiplyAdds = 0;
		for (size_t column = 0; column < destWidth; ++column)
		{
			if (mixed && ((column * 2654435761u) >> 13 & 1) != 0)
				--tapCounts[column];
			multiplyAdds += static_cast<double>(tapCounts[column]) * 4 * sourceHeight;
		}

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
}

int main(int argc, char** argv)
{
	const int repeats = argc > 1 ? std::atoi(argv[1]) : 30;
	const char* const sections = argc > 2 ? argv[2] : "HVTFCPN";
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
		report("F3 as F2, weights permuted in-lane", fourTapSeconds<3>(repeats), fourTapWork);
	}

	if (selected('C') || selected('P'))
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

			// Runs at odd pixel offsets: every other one where the step is 3 pixels, two of every four at 1.5, none at 4
			struct Ratio { const char* name; size_t taps, stepQ8, columns; };
			for (const Ratio& ratio : { Ratio{ "3x, 18 taps", 18, 3 * 256, 1280 }, Ratio{ "1.5x, 9 taps", 9, 384, 2560 }, Ratio{ "4x, 24 taps", 24, 4 * 256, 958 } })
			{
				const double work = static_cast<double>(sourceHeight) * ratio.columns * ratio.taps * 4;
				const auto reportRatio = [&](const char* form, double seconds) {
					char name[64];
					std::snprintf(name, sizeof(name), "R  %s, %s", ratio.name, form);
					report(name, seconds, work);
				};
				reportRatio("the kernel's form", passSeconds<0, 0>(image, 0, repeats, ratio.taps, ratio.stepQ8, ratio.columns));
				reportRatio("interleaved, 4 chains", passSeconds<4, 1>(image, 0, repeats, ratio.taps, ratio.stepQ8, ratio.columns));
				reportRatio("interleaved, in steps", passSeconds<8, 1>(image, 0, repeats, ratio.taps, ratio.stepQ8, ratio.columns));
				reportRatio("interleaved, 4 chains, lanes", passSeconds<14, 1>(image, 0, repeats, ratio.taps, ratio.stepQ8, ratio.columns));
				reportRatio("interleaved, in steps, lanes", passSeconds<18, 1>(image, 0, repeats, ratio.taps, ratio.stepQ8, ratio.columns));
			}
		}
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
