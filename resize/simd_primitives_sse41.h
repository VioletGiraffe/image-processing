#pragma once

// The SSE4.1 primitives cimageresizer_simd.inl is written against; only cimageresizer_simd_sse41.cpp includes this.
// No FMA at this level: a multiply-add rounds twice.

#include "simd_support.h"

#include "compiler/compiler_warnings_control.h"

#include <immintrin.h>

#include <bit>
#include <stddef.h>
#include <stdint.h>

// The loadu/storeu intrinsics take unaligned memory through __m128i pointers
DISABLE_CLANG_GCC_WARNING("-Wcast-align")

#define IMAGE_PROCESSING_SIMD_TARGET IMAGE_PROCESSING_SSE41_TARGET
#define IMAGE_PROCESSING_SIMD_INLINE IMAGE_PROCESSING_SSE41_INLINE

namespace ImageProcessing::Detail::Sse41
{
	// 8 consecutive floats: two RGBA pixels, or a run of channel values
	struct Floats8
	{
		__m128 low;
		__m128 high;
	};

	// One RGBA pixel
	using Floats4 = __m128;
	// 8 consecutive filter weights
	using WeightBlock = Floats8;
	// 4 consecutive filter weights
	using FourWeights = __m128;
	// The alpha bytes writeEightRgb32Pixels sets
	using Rgb32PixelTails = __m128i;

	// The horizontal pass's accumulation chains per row, a Floats8 each: a row pair's must fit the 16 XMM registers
	inline constexpr size_t horizontalChainCount = 2;
	// Not interleaved: a register is one pixel either way, and the micro-benchmark found the form within 3%.
	// The pair primitives exist for the outline: GCC and Clang look its non-dependent names up in discarded branches too.
	inline constexpr bool interleavedRowPairs = false;
	// The longest x run of 1- or 4-float pixels with a horizontal pass compiled for its length: a longer 4-float run's written-out blocks outgrow the registers under MSVC
	inline constexpr size_t longestFixedXRun = 12;
	// Blocks the vertical pass filters per step: one block already has 6-8 accumulators, two registers per Floats8
	inline constexpr size_t verticalBlocksPerStep = 1;
	// As at AVX2. A list costs a load per tap: Goldmont Plus pays 2-4% for it on downscales, Sandy Bridge nothing.
	inline constexpr bool verticalTapsListed = true;
	// Dest rows the vertical pass filters per step. 1: a pair is unmeasured at this level
	inline constexpr size_t verticalDestRowsPerStep = 1;
	// The temp ring's start, in floats past a 64-byte boundary. 16 bytes past: Goldmont Plus is 2-6% slower from the boundary, other cores within 1%
	inline constexpr size_t tempRingStartOffset = 4;
	// x runs one at a time: triples slow Coffee Lake's upscales by up to 11% under MSVC and 6.5% under clang-cl
	inline constexpr bool xRunTriplesFiltered = false;

	IMAGE_PROCESSING_SIMD_INLINE void leaveKernel() noexcept {}

	IMAGE_PROCESSING_SIMD_INLINE Floats8 zeroFloats8() noexcept { return { _mm_setzero_ps(), _mm_setzero_ps() }; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 zeroFloats4() noexcept { return _mm_setzero_ps(); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 loadFloats8(const float* floats) noexcept { return { _mm_loadu_ps(floats), _mm_loadu_ps(floats + 4) }; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 loadFloats4(const float* floats) noexcept { return _mm_loadu_ps(floats); }

	IMAGE_PROCESSING_SIMD_INLINE void storeFloats8(float* floats, Floats8 values) noexcept
	{
		_mm_storeu_ps(floats, values.low);
		_mm_storeu_ps(floats + 4, values.high);
	}

	IMAGE_PROCESSING_SIMD_INLINE void storeFloats4(float* floats, Floats4 values) noexcept { _mm_storeu_ps(floats, values); }

	IMAGE_PROCESSING_SIMD_INLINE Floats8 broadcastFloats8(float value) noexcept
	{
		const __m128 values = _mm_set1_ps(value);
		return { values, values };
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 broadcastFloats4(float value) noexcept { return _mm_set1_ps(value); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 add(Floats8 a, Floats8 b) noexcept { return { _mm_add_ps(a.low, b.low), _mm_add_ps(a.high, b.high) }; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 add(Floats4 a, Floats4 b) noexcept { return _mm_add_ps(a, b); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mul(Floats8 a, Floats8 b) noexcept { return { _mm_mul_ps(a.low, b.low), _mm_mul_ps(a.high, b.high) }; }
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

	// a * b + accum
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mulAdd(Floats8 a, Floats8 b, Floats8 accum) noexcept
	{
		return { _mm_add_ps(_mm_mul_ps(a.low, b.low), accum.low), _mm_add_ps(_mm_mul_ps(a.high, b.high), accum.high) };
	}

	// accum += a * b.
	// In place, not accum = mulAdd(a, b, accum): MSVC does not always merge the returned pair with accum, and then
	// computes every sum in a second register, copies it back, and keeps two of a row pair's accumulators on the stack.
	IMAGE_PROCESSING_SIMD_INLINE void addProduct(Floats8& accum, Floats8 a, Floats8 b) noexcept
	{
		accum.low = _mm_add_ps(accum.low, _mm_mul_ps(a.low, b.low));
		accum.high = _mm_add_ps(accum.high, _mm_mul_ps(a.high, b.high));
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 mulAdd(Floats4 a, Floats4 b, Floats4 accum) noexcept { return _mm_add_ps(_mm_mul_ps(a, b), accum); }
	// Not std::fma: without the instruction it is a library call
	IMAGE_PROCESSING_SIMD_INLINE float mulAdd(float a, float b, float accum) noexcept { return a * b + accum; }

	// The sum of the first 4 floats and the last 4
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumHalves(Floats8 values) noexcept { return _mm_add_ps(values.low, values.high); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 lowHalf(Floats8 values) noexcept { return values.low; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 highHalf(Floats8 values) noexcept { return values.high; }

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

	// Lane i: the sum of argument i's four lanes
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumEachOfFour(Floats4 values0, Floats4 values1, Floats4 values2, Floats4 values3) noexcept
	{
		return _mm_hadd_ps(_mm_hadd_ps(values0, values1), _mm_hadd_ps(values2, values3));
	}

	IMAGE_PROCESSING_SIMD_INLINE WeightBlock loadWeightBlock(const float* weights) noexcept { return loadFloats8(weights); }

	// Turns a WeightBlock into the weights of taps [Part * 8 / FloatsPerPixel, (Part + 1) * 8 / FloatsPerPixel), each repeated for its pixel's floats
	template <size_t FloatsPerPixel>
	class WeightSpreader
	{
	public:
		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part < FloatsPerPixel);
			if constexpr (FloatsPerPixel == 4)
			{
				const __m128 fourWeights = Part < 2 ? weights.low : weights.high;
				constexpr int firstLane = (Part % 2) * 2;
				return { _mm_shuffle_ps(fourWeights, fourWeights, firstLane * 0x55), _mm_shuffle_ps(fourWeights, fourWeights, (firstLane + 1) * 0x55) };
			}
			else if constexpr (FloatsPerPixel == 2)
			{
				const __m128 fourWeights = Part == 0 ? weights.low : weights.high;
				return { _mm_unpacklo_ps(fourWeights, fourWeights), _mm_unpackhi_ps(fourWeights, fourWeights) };
			}
			else
				return weights;
		}
	};

	IMAGE_PROCESSING_SIMD_INLINE FourWeights loadFourWeights(const float* weights) noexcept { return _mm_loadu_ps(weights); }

	// Weight Tap of 4 in all 8 floats
	template <size_t Tap>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 weightOfTap(FourWeights fourWeights) noexcept
	{
		static_assert(Tap < 4);
		const __m128 weight = _mm_shuffle_ps(fourWeights, fourWeights, Tap * 0x55);
		return { weight, weight };
	}

	// 16 bytes as 16 floats: the first 8 go to first
	IMAGE_PROCESSING_SIMD_INLINE void sixteenBytesAsFloats(__m128i bytes, Floats8& first, Floats8& second) noexcept
	{
		first = { _mm_cvtepi32_ps(_mm_cvtepu8_epi32(bytes)), _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_srli_si128(bytes, 4))) };
		second = { _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_srli_si128(bytes, 8))), _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_srli_si128(bytes, 12))) };
	}

	// Each widening loads its own 4 bytes: MSVC otherwise copies and shifts one 16-byte load three times
	IMAGE_PROCESSING_SIMD_INLINE void loadSixteenBytesAsFloats(const uint8_t* bytes, Floats8& first, Floats8& second) noexcept
	{
		first = { _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_loadu_si32(bytes))), _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_loadu_si32(bytes + 4))) };
		second = { _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_loadu_si32(bytes + 8))), _mm_cvtepi32_ps(_mm_cvtepu8_epi32(_mm_loadu_si32(bytes + 12))) };
	}

	// 4 RGB pixels as 4 floats each, the 4th 0: pixels 0 and 1 go to pixels01. Reads 16 bytes, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelsAsFloats(const uint8_t* pixels, Floats8& pixels01, Floats8& pixels23) noexcept
	{
		const __m128i rgbToRgb0 = _mm_setr_epi8(
			0, 1, 2, -1,
			3, 4, 5, -1,
			6, 7, 8, -1,
			9, 10, 11, -1);
		sixteenBytesAsFloats(_mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels)), rgbToRgb0), pixels01, pixels23);
	}

	// Two rows' 4 pixels each as pixel pairs: row A's pixel i in pairI's low half, row B's in its high half
	IMAGE_PROCESSING_SIMD_INLINE void pairPixels(Floats8 pixelsA01, Floats8 pixelsA23, Floats8 pixelsB01, Floats8 pixelsB23, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		pair0 = { pixelsA01.low, pixelsB01.low };
		pair1 = { pixelsA01.high, pixelsB01.high };
		pair2 = { pixelsA23.low, pixelsB23.low };
		pair3 = { pixelsA23.high, pixelsB23.high };
	}

	// 4 pixels of 4 bytes from each of two rows as floats, a pixel pair per Floats8: see pairPixels
	IMAGE_PROCESSING_SIMD_INLINE void loadFourPixelPairsAsFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		Floats8 pixelsA01, pixelsA23, pixelsB01, pixelsB23;
		loadSixteenBytesAsFloats(pixelsA, pixelsA01, pixelsA23);
		loadSixteenBytesAsFloats(pixelsB, pixelsB01, pixelsB23);
		pairPixels(pixelsA01, pixelsA23, pixelsB01, pixelsB23, pair0, pair1, pair2, pair3);
	}

	// loadFourPixelPairsAsFloats of RGB pixels, the 4th float 0. Reads 16 bytes per row, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelPairsAsFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		Floats8 pixelsA01, pixelsA23, pixelsB01, pixelsB23;
		loadFourRgbPixelsAsFloats(pixelsA, pixelsA01, pixelsA23);
		loadFourRgbPixelsAsFloats(pixelsB, pixelsB01, pixelsB23);
		pairPixels(pixelsA01, pixelsA23, pixelsB01, pixelsB23, pair0, pair1, pair2, pair3);
	}

	// Each pixel's alpha, the last of its FloatsPerPixel floats, in all of its lanes
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE __m128 pixelAlphas(__m128 pixels) noexcept
	{
		static_assert(FloatsPerPixel == 2 || FloatsPerPixel == 4);
		if constexpr (FloatsPerPixel == 4)
			return _mm_shuffle_ps(pixels, pixels, _MM_SHUFFLE(3, 3, 3, 3));
		else
			return _mm_movehdup_ps(pixels);
	}

	// Color times alpha / 255; the alpha lanes keep the source value
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE __m128 premultiplyPixels(__m128 pixels) noexcept
	{
		const __m128 premultiplied = _mm_mul_ps(pixels, _mm_mul_ps(pixelAlphas<FloatsPerPixel>(pixels), _mm_set1_ps(1.0f / 255.0f)));
		return _mm_blend_ps(premultiplied, pixels, FloatsPerPixel == 4 ? 0x8 : 0xA);
	}

	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 premultiplyPixels(Floats8 pixels) noexcept
	{
		return { premultiplyPixels<FloatsPerPixel>(pixels.low), premultiplyPixels<FloatsPerPixel>(pixels.high) };
	}

	IMAGE_PROCESSING_SIMD_INLINE __m128i roundFourFloatsToInts(__m128 values) noexcept
	{
		return _mm_cvttps_epi32(_mm_add_ps(values, _mm_set1_ps(0.5f)));
	}

	// Rounds 8 floats to 8 signed words in order.
	// Signed saturation: packus_epi16 reads its input as signed, and then clamps what is below 0 or past 255.
	IMAGE_PROCESSING_SIMD_INLINE __m128i packEightFloatsToWords(Floats8 values) noexcept
	{
		return _mm_packs_epi32(roundFourFloatsToInts(values.low), roundFourFloatsToInts(values.high));
	}

	IMAGE_PROCESSING_SIMD_INLINE __m128i packSixteenFloatsToBytes(Floats8 first, Floats8 second) noexcept
	{
		return _mm_packus_epi16(packEightFloatsToWords(first), packEightFloatsToWords(second));
	}

	// Color is capped at alpha: writePixelBytes caps it too
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE __m128i capColorBytesAtAlpha(__m128i pixels) noexcept
	{
		static_assert(Channels == 2 || Channels == 4);
		const __m128i alphaOfEachByte = Channels == 4
			? _mm_setr_epi8(3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15)
			: _mm_setr_epi8(1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15);
		return _mm_min_epu8(pixels, _mm_shuffle_epi8(pixels, alphaOfEachByte));
	}

	// Target: both are stored alike at this level
	template <BlockTarget Target>
	IMAGE_PROCESSING_SIMD_INLINE void writeThirtyTwoBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), packSixteenFloatsToBytes(values0, values1));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest + 16), packSixteenFloatsToBytes(values2, values3));
	}

	// The values are pixels of Channels floats, alpha last
	template <BlockTarget Target, size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE void writeThirtyTwoBytesCappedAtAlpha(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), capColorBytesAtAlpha<Channels>(packSixteenFloatsToBytes(values0, values1)));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest + 16), capColorBytesAtAlpha<Channels>(packSixteenFloatsToBytes(values2, values3)));
	}

	template <BlockTarget Target>
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

	// The bytes premultiplyBlock takes
	inline constexpr size_t premultiplyBlockBytes = 16;

	// 8 words of Channels-word pixels, alpha last: color times alpha / 255, rounded to nearest; alpha kept.
	// (t + 127) / 255 is (t + 127) * 0x8081 >> 23 for any 16-bit t + 127, and color times alpha + 127 is at most 65152.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE __m128i premultiplyPixelWords(__m128i words) noexcept
	{
		static_assert(Channels == 2 || Channels == 4);
		const __m128i alphaOfEachWord = Channels == 4
			? _mm_setr_epi8(6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15)
			: _mm_setr_epi8(2, 3, 2, 3, 6, 7, 6, 7, 10, 11, 10, 11, 14, 15, 14, 15);
		const __m128i products = _mm_add_epi16(_mm_mullo_epi16(words, _mm_shuffle_epi8(words, alphaOfEachWord)), _mm_set1_epi16(127));
		const __m128i quotients = _mm_srli_epi16(_mm_mulhi_epu16(products, _mm_set1_epi16(static_cast<short>(0x8081))), 7);
		return _mm_blend_epi16(quotients, words, Channels == 4 ? 0x88 : 0xAA);
	}

	// premultiplyPixelsScalar of the block at source, stored at dest; dest may be source
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE void premultiplyBlock(uint8_t* dest, const uint8_t* source) noexcept
	{
		const __m128i bytes = _mm_loadu_si128(reinterpret_cast<const __m128i*>(source));
		const __m128i zero = _mm_setzero_si128();
		const __m128i low = premultiplyPixelWords<Channels>(_mm_unpacklo_epi8(bytes, zero));
		const __m128i high = premultiplyPixelWords<Channels>(_mm_unpackhi_epi8(bytes, zero));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_packus_epi16(low, high));
	}

	// The 4-byte pixels expandBlock writes, from one load of as many source pixels
	inline constexpr size_t expandBlockPixels = 4;
	// The selector bytes per block: a fourPixelShufflePattern
	inline constexpr size_t expandSelectorBytes = 1;

	// placesInLoad: each of the block's pixels' source pixel, counted from the block's load
	IMAGE_PROCESSING_SIMD_INLINE void writeExpandSelectors(uint8_t* selectors, const uint8_t* placesInLoad) noexcept
	{
		selectors[0] = fourPixelShufflePattern(placesInLoad);
	}

	IMAGE_PROCESSING_SIMD_INLINE void expandBlock(uint8_t* dest, const uint8_t* source, const uint8_t* selectors) noexcept
	{
		const __m128i mask = _mm_loadu_si128(reinterpret_cast<const __m128i*>(fourPixelShuffleMasks[selectors[0]].data()));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(source)), mask));
	}
}
