#pragma once

// The AVX2 + FMA primitives cimageresizer_simd.inl is written against; only cimageresizer_simd_avx2.cpp includes this

#include "simd_support.h"

#include "compiler/compiler_warnings_control.h"

#include <immintrin.h>

#include <bit>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// The loadu/storeu intrinsics take unaligned memory through __m128i pointers
DISABLE_CLANG_GCC_WARNING("-Wcast-align")

#define IMAGE_PROCESSING_SIMD_TARGET IMAGE_PROCESSING_AVX2_TARGET
#define IMAGE_PROCESSING_SIMD_INLINE IMAGE_PROCESSING_AVX2_INLINE

namespace ImageProcessing::Detail::Avx2
{
	// 8 consecutive floats: two RGBA pixels, or a run of channel values
	using Floats8 = __m256;
	// One RGBA pixel
	using Floats4 = __m128;
	// 8 consecutive filter weights
	using WeightBlock = __m256;
	// The alpha bytes writeEightRgb32Pixels sets
	using Rgb32PixelTails = __m128i;

	// The code after the kernel is legacy-SSE encoded, and stalls while the upper YMM state is dirty
	IMAGE_PROCESSING_SIMD_INLINE void leaveKernel() noexcept { SimdSupport::clearAvxUpperState(); }

	IMAGE_PROCESSING_SIMD_INLINE Floats8 zeroFloats8() noexcept { return _mm256_setzero_ps(); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 zeroFloats4() noexcept { return _mm_setzero_ps(); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 loadFloats8(const float* floats) noexcept { return _mm256_loadu_ps(floats); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 loadFloats4(const float* floats) noexcept { return _mm_loadu_ps(floats); }
	IMAGE_PROCESSING_SIMD_INLINE void storeFloats8(float* floats, Floats8 values) noexcept { _mm256_storeu_ps(floats, values); }
	IMAGE_PROCESSING_SIMD_INLINE void storeFloats4(float* floats, Floats4 values) noexcept { _mm_storeu_ps(floats, values); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 broadcastFloats8(float value) noexcept { return _mm256_set1_ps(value); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 broadcastFloats4(float value) noexcept { return _mm_set1_ps(value); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 add(Floats8 a, Floats8 b) noexcept { return _mm256_add_ps(a, b); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 add(Floats4 a, Floats4 b) noexcept { return _mm_add_ps(a, b); }
	// a * b + accum, fused
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mulAdd(Floats8 a, Floats8 b, Floats8 accum) noexcept { return _mm256_fmadd_ps(a, b, accum); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 mulAdd(Floats4 a, Floats4 b, Floats4 accum) noexcept { return _mm_fmadd_ps(a, b, accum); }

	// The sum of the first 4 floats and the last 4
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumHalves(Floats8 values) noexcept
	{
		return _mm_add_ps(_mm256_castps256_ps128(values), _mm256_extractf128_ps(values, 1));
	}

	IMAGE_PROCESSING_SIMD_INLINE WeightBlock loadWeightBlock(const float* weights) noexcept { return _mm256_loadu_ps(weights); }

	// Turns a WeightBlock into the pair broadcast [w(2 * Pair) x4 | w(2 * Pair + 1) x4] a pixel pair needs
	class WeightPairSpreader
	{
	public:
		IMAGE_PROCESSING_SIMD_INLINE WeightPairSpreader() noexcept :
			_pairIndices{
				_mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1),
				_mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3),
				_mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5),
				_mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7) }
		{}

		template <size_t Pair>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Pair < 4);
			return _mm256_permutevar8x32_ps(weights, _pairIndices[Pair]);
		}

	private:
		const __m256i _pairIndices[4];
	};

	IMAGE_PROCESSING_SIMD_INLINE Floats4 loadPixelAsFloats(const uint8_t* pixel) noexcept
	{
		int32_t packedPixel;
		::memcpy(&packedPixel, pixel, sizeof(packedPixel));
		return _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_cvtsi32_si128(packedPixel)));
	}

	// Pixels 0 and 1 of the 4 go to pixels01, 2 and 3 to pixels23
	IMAGE_PROCESSING_SIMD_INLINE void loadFourPixelsAsFloats(const uint8_t* pixels, Floats8& pixels01, Floats8& pixels23) noexcept
	{
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels));
		pixels01 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(bytes));
		pixels23 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(bytes, bytes)));
	}

	// Color times alpha / 255; the alpha lanes keep the source value
	IMAGE_PROCESSING_SIMD_INLINE Floats8 premultiplyTwoPixels(Floats8 twoPixels) noexcept
	{
		const __m256 alpha = _mm256_shuffle_ps(twoPixels, twoPixels, _MM_SHUFFLE(3, 3, 3, 3));
		const __m256 premultiplied = _mm256_mul_ps(twoPixels, _mm256_mul_ps(alpha, _mm256_set1_ps(1.0f / 255.0f)));
		return _mm256_blend_ps(premultiplied, twoPixels, 0x88);
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 premultiplyPixel(Floats4 pixel) noexcept
	{
		const __m128 alpha = _mm_shuffle_ps(pixel, pixel, _MM_SHUFFLE(3, 3, 3, 3));
		const __m128 premultiplied = _mm_mul_ps(pixel, _mm_mul_ps(alpha, _mm_set1_ps(1.0f / 255.0f)));
		return _mm_blend_ps(premultiplied, pixel, 0x8);
	}

	// Rounds 8 floats to 8 words in order.
	// The packs are 128-bit: 256-bit packs work per lane and need a lane-crossing permute after them.
	IMAGE_PROCESSING_SIMD_INLINE __m128i packEightFloatsToWords(Floats8 values) noexcept
	{
		// packus_epi32 saturates negatives to 0; the 255 cap must stay - values past 32767 would wrap negative through the signed-input packus_epi16
		values = _mm256_min_ps(_mm256_set1_ps(255.0f), values);
		const __m256i integers = _mm256_cvttps_epi32(_mm256_add_ps(values, _mm256_set1_ps(0.5f)));
		return _mm_packus_epi32(_mm256_castsi256_si128(integers), _mm256_extracti128_si256(integers, 1));
	}

	IMAGE_PROCESSING_SIMD_INLINE __m128i packSixteenFloatsToBytes(Floats8 first, Floats8 second) noexcept
	{
		return _mm_packus_epi16(packEightFloatsToWords(first), packEightFloatsToWords(second));
	}

	// Color is capped at alpha, as writePixelBytes does
	IMAGE_PROCESSING_SIMD_INLINE Floats8 capColorAtAlpha(Floats8 twoPixels) noexcept
	{
		return _mm256_min_ps(twoPixels, _mm256_shuffle_ps(twoPixels, twoPixels, _MM_SHUFFLE(3, 3, 3, 3)));
	}

	IMAGE_PROCESSING_SIMD_INLINE void writeEightRgbaPixels(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), packSixteenFloatsToBytes(capColorAtAlpha(values0), capColorAtAlpha(values1)));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest + 16), packSixteenFloatsToBytes(capColorAtAlpha(values2), capColorAtAlpha(values3)));
	}

	IMAGE_PROCESSING_SIMD_INLINE Rgb32PixelTails rgb32PixelTails(uint8_t tailValue) noexcept
	{
		return _mm_set1_epi32(std::bit_cast<int32_t>(static_cast<uint32_t>(tailValue) << 24));
	}

	// values0..2 hold the 8 pixels' 24 color floats
	IMAGE_PROCESSING_SIMD_INLINE void writeEightRgb32Pixels(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Rgb32PixelTails pixelTails) noexcept
	{
		const __m128i firstSixteenRgbBytes = packSixteenFloatsToBytes(values0, values1);
		const __m128i lastWords = packEightFloatsToWords(values2);
		const __m128i lastEightRgbBytes = _mm_packus_epi16(lastWords, lastWords);
		const __m128i lastTwelveRgbBytes = _mm_alignr_epi8(lastEightRgbBytes, firstSixteenRgbBytes, 12);
		const __m128i rgbToRgb32 = _mm_setr_epi8(
			0, 1, 2, -1,
			3, 4, 5, -1,
			6, 7, 8, -1,
			9, 10, 11, -1);

		const __m128i pixels0 = _mm_or_si128(_mm_shuffle_epi8(firstSixteenRgbBytes, rgbToRgb32), pixelTails);
		const __m128i pixels1 = _mm_or_si128(_mm_shuffle_epi8(lastTwelveRgbBytes, rgbToRgb32), pixelTails);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), pixels0);
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest + 16), pixels1);
	}
}
