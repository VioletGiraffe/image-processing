// The multiply-adds of the resizer's two passes at the sizes of 4K -> 1080p RGB32, without the kernel's bookkeeping:
// the ceilings doc/throughput-ceiling.md compares the kernels against, and the horizontal forms it ranks.
// Standalone, AVX2 + FMA, Windows (aligned allocation through the CRT):
//   cl /O2 /MT /arch:AVX2 /std:c++20 /EHsc multiply_add_ceiling.cpp
//   clang-cl /O2 /MT /arch:AVX2 -mfma /std:c++20 /EHsc multiply_add_ceiling.cpp
// Run: multiply_add_ceiling [repeats, default 30]; each line is the best repeat.
// Pinned to a logical processor by mask: start /b /wait /affinity 4 multiply_add_ceiling.exe
// Rates are float multiply-adds per second, counted as the kernel's are: 4 floats per horizontal tap, 3 per vertical tap.
#include <immintrin.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__)
	#define NOINLINE __declspec(noinline)
#else
	#define NOINLINE __attribute__((noinline))
#endif

namespace
{
	constexpr size_t sourceHeight = 2160, destWidth = 1920, destHeight = 1080, taps = 12;
	constexpr size_t rowPairs = sourceHeight / 2;

	float* alignedFloats(size_t count)
	{
		float* p = static_cast<float*>(_aligned_malloc(count * sizeof(float), 64));
		for (size_t i = 0; i < count; ++i)
			p[i] = static_cast<float>((i * 37) & 255);
		return p;
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
		_aligned_free(ring);
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
		_aligned_free(a); _aligned_free(b); _aligned_free(interleaved); _aligned_free(weights); _aligned_free(outA); _aligned_free(outB);
		return seconds;
	}

	void report(const char* name, double seconds, double multiplyAdds)
	{
		std::printf("%-58s %7.2f ms  %6.1f G/s\n", name, seconds * 1e3, multiplyAdds / seconds / 1e9);
	}
}

int main(int argc, char** argv)
{
	const int repeats = argc > 1 ? std::atoi(argv[1]) : 30;
	const double horizontalWork = static_cast<double>(sourceHeight) * destWidth * taps * 4;
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
	return 0;
}
