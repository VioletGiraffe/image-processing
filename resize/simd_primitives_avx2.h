#pragma once

// The AVX2 + FMA primitives cimageresizer_simd.inl is written against; only cimageresizer_simd_avx2.cpp includes this

#include "simd_support.h"

#include "compiler/compiler_warnings_control.h"

#include <immintrin.h>

#include <bit>
#include <cmath>
#include <stddef.h>
#include <stdint.h>

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

	// The horizontal pass's accumulation chains per row, a Floats8 each
	inline constexpr size_t horizontalChainCount = 4;
	// Blocks the vertical pass filters per step. 2: a block's taps go through only 3-4 registers here, and a pair shares each tap's weight and zero test
	inline constexpr size_t verticalBlocksPerStep = 2;

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
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mul(Floats8 a, Floats8 b) noexcept { return _mm256_mul_ps(a, b); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 mul(Floats4 a, Floats4 b) noexcept { return _mm_mul_ps(a, b); }

	template <size_t Index>
	IMAGE_PROCESSING_SIMD_INLINE float lane(Floats4 values) noexcept
	{
		static_assert(Index < 4);
		if constexpr (Index == 0)
			return _mm_cvtss_f32(values);
		else
			return _mm_cvtss_f32(_mm_shuffle_ps(values, values, Index * 0x55));
	}

	// a * b + accum, fused
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mulAdd(Floats8 a, Floats8 b, Floats8 accum) noexcept { return _mm256_fmadd_ps(a, b, accum); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 mulAdd(Floats4 a, Floats4 b, Floats4 accum) noexcept { return _mm_fmadd_ps(a, b, accum); }
	IMAGE_PROCESSING_SIMD_INLINE float mulAdd(float a, float b, float accum) noexcept { return std::fma(a, b, accum); }

	// The sum of the first 4 floats and the last 4
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumHalves(Floats8 values) noexcept
	{
		return _mm_add_ps(_mm256_castps256_ps128(values), _mm256_extractf128_ps(values, 1));
	}

	// Sums a Floats4's 4 / FloatsPerPixel pixels into its first FloatsPerPixel lanes
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumPixels(Floats4 pixels) noexcept
	{
		static_assert(FloatsPerPixel == 1 || FloatsPerPixel == 2);
		const __m128 pairSums = _mm_add_ps(pixels, _mm_movehl_ps(pixels, pixels));
		if constexpr (FloatsPerPixel == 2)
			return pairSums;
		else
			return _mm_add_ss(pairSums, _mm_shuffle_ps(pairSums, pairSums, _MM_SHUFFLE(1, 1, 1, 1)));
	}

	// The first count lanes, the others 0
	IMAGE_PROCESSING_SIMD_INLINE Floats4 keepFirstLanes(Floats4 values, size_t count) noexcept
	{
		alignas(16) static constexpr uint32_t laneMasks[5][4] = { { 0, 0, 0, 0 }, { ~0u, 0, 0, 0 }, { ~0u, ~0u, 0, 0 }, { ~0u, ~0u, ~0u, 0 }, { ~0u, ~0u, ~0u, ~0u } };
		return _mm_and_ps(values, _mm_castsi128_ps(_mm_load_si128(reinterpret_cast<const __m128i*>(laneMasks[count]))));
	}

	// Lane i: the sum of argument i's four lanes
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumEachOfFour(Floats4 values0, Floats4 values1, Floats4 values2, Floats4 values3) noexcept
	{
		return _mm_hadd_ps(_mm_hadd_ps(values0, values1), _mm_hadd_ps(values2, values3));
	}

	IMAGE_PROCESSING_SIMD_INLINE WeightBlock loadWeightBlock(const float* weights) noexcept { return _mm256_loadu_ps(weights); }

	// Turns a WeightBlock into the weights of taps [Part * 8 / FloatsPerPixel, (Part + 1) * 8 / FloatsPerPixel), each repeated for its pixel's floats
	template <size_t FloatsPerPixel>
	class WeightSpreader;

	template <>
	class WeightSpreader<4>
	{
	public:
		IMAGE_PROCESSING_SIMD_INLINE WeightSpreader() noexcept :
			_pairIndices{
				_mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1),
				_mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3),
				_mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5),
				_mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7) }
		{}

		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part < 4);
			return _mm256_permutevar8x32_ps(weights, _pairIndices[Part]);
		}

	private:
		const __m256i _pairIndices[4];
	};

	template <>
	class WeightSpreader<2>
	{
	public:
		IMAGE_PROCESSING_SIMD_INLINE WeightSpreader() noexcept :
			_quadIndices{
				_mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3),
				_mm256_setr_epi32(4, 4, 5, 5, 6, 6, 7, 7) }
		{}

		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part < 2);
			return _mm256_permutevar8x32_ps(weights, _quadIndices[Part]);
		}

	private:
		const __m256i _quadIndices[2];
	};

	template <>
	class WeightSpreader<1>
	{
	public:
		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part == 0);
			return weights;
		}
	};

	// 16 bytes as 16 floats: the first 8 go to first
	IMAGE_PROCESSING_SIMD_INLINE void loadSixteenBytesAsFloats(const uint8_t* bytes, Floats8& first, Floats8& second) noexcept
	{
		const __m128i loaded = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes));
		first = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(loaded));
		second = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(loaded, loaded)));
	}

	// 4 RGB pixels as 4 floats each, the 4th 0: pixels 0 and 1 go to pixels01. Reads 16 bytes, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelsAsFloats(const uint8_t* pixels, Floats8& pixels01, Floats8& pixels23) noexcept
	{
		const __m128i rgbToRgb0 = _mm_setr_epi8(
			0, 1, 2, -1,
			3, 4, 5, -1,
			6, 7, 8, -1,
			9, 10, 11, -1);
		const __m128i bytes = _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels)), rgbToRgb0);
		pixels01 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(bytes));
		pixels23 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(bytes, bytes)));
	}

	// Each pixel's alpha, the last of its FloatsPerPixel floats, in all of its lanes
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 pixelAlphas(Floats8 pixels) noexcept
	{
		static_assert(FloatsPerPixel == 2 || FloatsPerPixel == 4);
		if constexpr (FloatsPerPixel == 4)
			return _mm256_shuffle_ps(pixels, pixels, _MM_SHUFFLE(3, 3, 3, 3));
		else
			return _mm256_movehdup_ps(pixels);
	}

	// Color times alpha / 255; the alpha lanes keep the source value
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 premultiplyPixels(Floats8 pixels) noexcept
	{
		const __m256 premultiplied = _mm256_mul_ps(pixels, _mm256_mul_ps(pixelAlphas<FloatsPerPixel>(pixels), _mm256_set1_ps(1.0f / 255.0f)));
		return _mm256_blend_ps(premultiplied, pixels, FloatsPerPixel == 4 ? 0x88 : 0xAA);
	}

	// Rounds 8 floats to 8 signed words in order.
	// The packs are 128-bit: 256-bit packs work per lane and need a lane-crossing permute after them.
	// Signed saturation: packus_epi16 reads its input as signed, and then clamps what is below 0 or past 255.
	IMAGE_PROCESSING_SIMD_INLINE __m128i packEightFloatsToWords(Floats8 values) noexcept
	{
		const __m256i integers = _mm256_cvttps_epi32(_mm256_add_ps(values, _mm256_set1_ps(0.5f)));
		return _mm_packs_epi32(_mm256_castsi256_si128(integers), _mm256_extracti128_si256(integers, 1));
	}

	IMAGE_PROCESSING_SIMD_INLINE __m128i packSixteenFloatsToBytes(Floats8 first, Floats8 second) noexcept
	{
		return _mm_packus_epi16(packEightFloatsToWords(first), packEightFloatsToWords(second));
	}

	// Color is capped at alpha: writePixelBytes caps it too
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 capColorAtAlpha(Floats8 pixels) noexcept
	{
		return _mm256_min_ps(pixels, pixelAlphas<FloatsPerPixel>(pixels));
	}

	IMAGE_PROCESSING_SIMD_INLINE void writeThirtyTwoBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), packSixteenFloatsToBytes(values0, values1));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest + 16), packSixteenFloatsToBytes(values2, values3));
	}

	IMAGE_PROCESSING_SIMD_INLINE void writeTwentyFourBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2) noexcept
	{
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), packSixteenFloatsToBytes(values0, values1));
		const __m128i lastWords = packEightFloatsToWords(values2);
		_mm_storel_epi64(reinterpret_cast<__m128i*>(dest + 16), _mm_packus_epi16(lastWords, lastWords));
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
