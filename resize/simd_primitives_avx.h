#pragma once

// The AVX primitives cimageresizer_simd.inl is written against, for CPUs with AVX and no AVX2; only cimageresizer_simd_avx.cpp includes this.
// Floats are 256-bit; byte widening and packing stay 128-bit: AVX has no 256-bit integer instructions.
// No FMA at this level: a multiply-add rounds twice.

#include "simd_support.h"

#include "compiler/compiler_warnings_control.h"

#include <immintrin.h>

#include <bit>
#include <stddef.h>
#include <stdint.h>

// The loadu/storeu intrinsics take unaligned memory through __m128i pointers
DISABLE_CLANG_GCC_WARNING("-Wcast-align")

#define IMAGE_PROCESSING_SIMD_TARGET IMAGE_PROCESSING_AVX_TARGET
#define IMAGE_PROCESSING_SIMD_INLINE IMAGE_PROCESSING_AVX_INLINE

namespace ImageProcessing::Detail::Avx
{
	// 8 consecutive floats: two RGBA pixels, or a run of channel values
	using Floats8 = __m256;
	// One RGBA pixel
	using Floats4 = __m128;
	// 8 consecutive filter weights
	using WeightBlock = __m256;
	// 4 consecutive filter weights, in both halves
	using FourWeights = __m256;
	// The alpha bytes writeEightRgb32Pixels sets
	using Rgb32PixelTails = __m128i;

	// The tuning is AVX2's; the alternatives measured on Sandy Bridge:
	//   not interleaved - the padded 4-float rows 3-12% slower, the upscales level with SSE4.1
	//   one vertical block per step - the strong downscales 3-5% slower
	//   the temp ring 16 bytes past a 64-byte boundary - within 3%
	inline constexpr size_t horizontalChainCount = 4;
	inline constexpr bool interleavedRowPairs = true;
	inline constexpr size_t longestFixedXRun = 20;
	inline constexpr size_t verticalBlocksPerStep = 2;
	inline constexpr bool verticalTapsListed = true;
	// Unmeasured here
	inline constexpr size_t verticalDestRowsPerStep = 1;
	// Upscales 5-14% faster on Sandy Bridge
	inline constexpr bool verticalFourTapsHeld = true;
	inline constexpr size_t tempRingStartOffset = 0;
	// 4-tap x runs filtered three at a time over the interleaved row pairs, each pixel pair loaded once
	inline constexpr bool xRunTriplesFiltered = true;

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

	// a * b + accum, rounded twice
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mulAdd(Floats8 a, Floats8 b, Floats8 accum) noexcept { return _mm256_add_ps(_mm256_mul_ps(a, b), accum); }
	// accum += a * b; in place for MSVC, see the SSE4.1 header
	IMAGE_PROCESSING_SIMD_INLINE void addProduct(Floats8& accum, Floats8 a, Floats8 b) noexcept { accum = _mm256_add_ps(accum, _mm256_mul_ps(a, b)); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 mulAdd(Floats4 a, Floats4 b, Floats4 accum) noexcept { return _mm_add_ps(_mm_mul_ps(a, b), accum); }
	IMAGE_PROCESSING_SIMD_INLINE float mulAdd(float a, float b, float accum) noexcept { return a * b + accum; }

	// The sum of the first 4 floats and the last 4
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumHalves(Floats8 values) noexcept
	{
		return _mm_add_ps(_mm256_castps256_ps128(values), _mm256_extractf128_ps(values, 1));
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 lowHalf(Floats8 values) noexcept { return _mm256_castps256_ps128(values); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 highHalf(Floats8 values) noexcept { return _mm256_extractf128_ps(values, 1); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 addToLowHalf(Floats8 values, Floats4 addend) noexcept { return _mm256_add_ps(values, _mm256_zextps128_ps256(addend)); }

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

	IMAGE_PROCESSING_SIMD_INLINE WeightBlock loadWeightBlock(const float* weights) noexcept { return _mm256_loadu_ps(weights); }

	// Turns a WeightBlock into the weights of taps [Part * 8 / FloatsPerPixel, (Part + 1) * 8 / FloatsPerPixel), each repeated for its pixel's floats
	template <size_t FloatsPerPixel>
	class WeightSpreader;

	// No cross-lane permute at AVX: a 128-bit half goes to both lanes, then an in-lane vpermilps picks each lane's weights
	template <int Half>
	IMAGE_PROCESSING_SIMD_INLINE __m256 halfInBothLanes(WeightBlock weights) noexcept
	{
		return _mm256_permute2f128_ps(weights, weights, Half == 0 ? 0x00 : 0x11);
	}

	template <>
	class WeightSpreader<4>
	{
	public:
		IMAGE_PROCESSING_SIMD_INLINE WeightSpreader() noexcept :
			_pairIndices{ _mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1), _mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3) }
		{}

		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part < 4);
			return _mm256_permutevar_ps(halfInBothLanes<Part / 2>(weights), _pairIndices[Part % 2]);
		}

	private:
		const __m256i _pairIndices[2];
	};

	template <>
	class WeightSpreader<2>
	{
	public:
		IMAGE_PROCESSING_SIMD_INLINE WeightSpreader() noexcept :
			_quadIndices{ _mm256_setr_epi32(0, 0, 1, 1, 2, 2, 3, 3) }
		{}

		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part < 2);
			return _mm256_permutevar_ps(halfInBothLanes<Part>(weights), _quadIndices);
		}

	private:
		const __m256i _quadIndices;
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

	// Hidden from Clang's optimizer, as at AVX2
	IMAGE_PROCESSING_SIMD_INLINE FourWeights loadFourWeights(const float* weights) noexcept
	{
		FourWeights fourWeights = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(weights));
#ifdef __clang__
		__asm__ volatile("" : "+x"(fourWeights));
#endif
		return fourWeights;
	}

	// Weight Tap of 4 in all 8 floats
	template <size_t Tap>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 weightOfTap(FourWeights fourWeights) noexcept
	{
		static_assert(Tap < 4);
		return _mm256_shuffle_ps(fourWeights, fourWeights, Tap * 0x55);
	}

	// Two 128-bit widenings joined into one 256-bit conversion
	IMAGE_PROCESSING_SIMD_INLINE Floats8 eightDwordsAsFloats(__m128i low, __m128i high) noexcept
	{
		return _mm256_cvtepi32_ps(_mm256_insertf128_si256(_mm256_castsi128_si256(low), high, 1));
	}

	// 16 bytes as 16 floats: the first 8 go to first. Each widening loads its own 4 bytes, as at SSE4.1.
	IMAGE_PROCESSING_SIMD_INLINE void loadSixteenBytesAsFloats(const uint8_t* bytes, Floats8& first, Floats8& second) noexcept
	{
		first = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_loadu_si32(bytes)), _mm_cvtepu8_epi32(_mm_loadu_si32(bytes + 4)));
		second = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_loadu_si32(bytes + 8)), _mm_cvtepu8_epi32(_mm_loadu_si32(bytes + 12)));
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
		pixels01 = eightDwordsAsFloats(_mm_cvtepu8_epi32(bytes), _mm_cvtepu8_epi32(_mm_srli_si128(bytes, 4)));
		pixels23 = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_srli_si128(bytes, 8)), _mm_cvtepu8_epi32(_mm_srli_si128(bytes, 12)));
	}

	// 4 pixels of 4 bytes from each of two rows as floats, a pixel pair per Floats8: row A's pixel i in pairI's low half, row B's in its high half
	IMAGE_PROCESSING_SIMD_INLINE void loadFourPixelPairsAsFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		pair0 = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_loadu_si32(pixelsA)), _mm_cvtepu8_epi32(_mm_loadu_si32(pixelsB)));
		pair1 = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_loadu_si32(pixelsA + 4)), _mm_cvtepu8_epi32(_mm_loadu_si32(pixelsB + 4)));
		pair2 = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_loadu_si32(pixelsA + 8)), _mm_cvtepu8_epi32(_mm_loadu_si32(pixelsB + 8)));
		pair3 = eightDwordsAsFloats(_mm_cvtepu8_epi32(_mm_loadu_si32(pixelsA + 12)), _mm_cvtepu8_epi32(_mm_loadu_si32(pixelsB + 12)));
	}

	// loadFourPixelPairsAsFloats of RGB pixels, the 4th float 0. Reads 16 bytes per row, 4 past the pixels.
	// One shuffle per pixel and row both picks its 3 bytes and zero-extends them to dwords.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelPairsAsFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		const __m128i bytesA = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixelsA));
		const __m128i bytesB = _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixelsB));
		const __m128i pixel0 = _mm_setr_epi8(0, -1, -1, -1, 1, -1, -1, -1, 2, -1, -1, -1, -1, -1, -1, -1);
		const __m128i pixel1 = _mm_setr_epi8(3, -1, -1, -1, 4, -1, -1, -1, 5, -1, -1, -1, -1, -1, -1, -1);
		const __m128i pixel2 = _mm_setr_epi8(6, -1, -1, -1, 7, -1, -1, -1, 8, -1, -1, -1, -1, -1, -1, -1);
		const __m128i pixel3 = _mm_setr_epi8(9, -1, -1, -1, 10, -1, -1, -1, 11, -1, -1, -1, -1, -1, -1, -1);
		pair0 = eightDwordsAsFloats(_mm_shuffle_epi8(bytesA, pixel0), _mm_shuffle_epi8(bytesB, pixel0));
		pair1 = eightDwordsAsFloats(_mm_shuffle_epi8(bytesA, pixel1), _mm_shuffle_epi8(bytesB, pixel1));
		pair2 = eightDwordsAsFloats(_mm_shuffle_epi8(bytesA, pixel2), _mm_shuffle_epi8(bytesB, pixel2));
		pair3 = eightDwordsAsFloats(_mm_shuffle_epi8(bytesA, pixel3), _mm_shuffle_epi8(bytesB, pixel3));
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

	// Rounds 8 floats to 8 signed words in order; the conversion is 256-bit, the pack 128-bit.
	// Signed saturation: packus_epi16 reads its input as signed, and then clamps what is below 0 or past 255.
	IMAGE_PROCESSING_SIMD_INLINE __m128i packEightFloatsToWords(Floats8 values) noexcept
	{
		const __m256i ints = _mm256_cvttps_epi32(_mm256_add_ps(values, _mm256_set1_ps(0.5f)));
		return _mm_packs_epi32(_mm256_castsi256_si128(ints), _mm256_extractf128_si256(ints, 1));
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
