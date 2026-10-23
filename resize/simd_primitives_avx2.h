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
	// 4 consecutive filter weights, in both halves
	using FourWeights = __m256;
	// The alpha bytes writeEightRgb32Pixels sets
	using Rgb32PixelTails = __m256i;

	// The horizontal pass's accumulation chains per row, a Floats8 each
	inline constexpr size_t horizontalChainCount = 4;
	// A row pair of 4-float pixels in padded x runs is interleaved, a pixel pair per Floats8: a tap is one multiply-add, with no weight spread and no halves' sum
	inline constexpr bool interleavedRowPairs = true;
	// The longest x run of 1- or 4-float pixels with a horizontal pass compiled for its length.
	// 20: at 22-28 taps the interleaved form loses Skylake 3-7% under MSVC to the read count.
	inline constexpr size_t longestFixedXRun = 20;
	// Blocks the vertical pass filters per step. 2: a block's taps go through only 3-4 registers here, and a pair shares each tap's weight and zero test
	inline constexpr size_t verticalBlocksPerStep = 2;
	// The vertical pass reads a dest row's nonzero taps from a list, not from the ring's rows in order: a third fewer instructions in the tap loop
	inline constexpr bool verticalTapsListed = true;
	// Dest rows the vertical pass filters per step on downscales of 2x and more. 2: a pair over their windows' union loads each temp row's floats once for both
	inline constexpr size_t verticalDestRowsPerStep = 2;
	// As at SSE4.1. On the boundary: off a 32-byte one, half of this level's 32-byte loads span two cache lines
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

	// a * b + accum, fused
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mulAdd(Floats8 a, Floats8 b, Floats8 accum) noexcept { return _mm256_fmadd_ps(a, b, accum); }
	// accum += a * b, fused
	IMAGE_PROCESSING_SIMD_INLINE void addProduct(Floats8& accum, Floats8 a, Floats8 b) noexcept { accum = _mm256_fmadd_ps(a, b, accum); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 mulAdd(Floats4 a, Floats4 b, Floats4 accum) noexcept { return _mm_fmadd_ps(a, b, accum); }
	IMAGE_PROCESSING_SIMD_INLINE float mulAdd(float a, float b, float accum) noexcept { return std::fma(a, b, accum); }

	// The sum of the first 4 floats and the last 4
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumHalves(Floats8 values) noexcept
	{
		return _mm_add_ps(_mm256_castps256_ps128(values), _mm256_extractf128_ps(values, 1));
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 lowHalf(Floats8 values) noexcept { return _mm256_castps256_ps128(values); }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 highHalf(Floats8 values) noexcept { return _mm256_extractf128_ps(values, 1); }

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

	// A WeightSpreader's vpermps indices.
	// Hidden from Clang's optimizer: it rewrites a vpermps whose indices it knows as vshufps + vpermpd, two shuffles for one.
	IMAGE_PROCESSING_SIMD_INLINE __m256i spreadIndices(int i0, int i1, int i2, int i3, int i4, int i5, int i6, int i7) noexcept
	{
		__m256i indices = _mm256_setr_epi32(i0, i1, i2, i3, i4, i5, i6, i7);
#ifdef __clang__
		__asm__ volatile("" : "+x"(indices));
#endif
		return indices;
	}

	template <>
	class WeightSpreader<4>
	{
	public:
		IMAGE_PROCESSING_SIMD_INLINE WeightSpreader() noexcept :
			_pairIndices{
				spreadIndices(0, 0, 0, 0, 1, 1, 1, 1),
				spreadIndices(2, 2, 2, 2, 3, 3, 3, 3),
				spreadIndices(4, 4, 4, 4, 5, 5, 5, 5),
				spreadIndices(6, 6, 6, 6, 7, 7, 7, 7) }
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
				spreadIndices(0, 0, 1, 1, 2, 2, 3, 3),
				spreadIndices(4, 4, 5, 5, 6, 6, 7, 7) }
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

	// Hidden from Clang's optimizer: it rewrites weightOfTap's shuffles of a loaded register as broadcasts from memory, a second load per tap
	IMAGE_PROCESSING_SIMD_INLINE FourWeights loadFourWeights(const float* weights) noexcept
	{
		FourWeights fourWeights = _mm256_broadcast_ps(reinterpret_cast<const __m128*>(weights));
#ifdef __clang__
		__asm__ volatile("" : "+x"(fourWeights));
#endif
		return fourWeights;
	}

	// Weight Tap of 4 in all 8 floats.
	// _mm256_shuffle_ps, not _mm256_permute_ps: MSVC emits vpermilps for that, which a 12600K P-core runs at one per cycle.
	template <size_t Tap>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 weightOfTap(FourWeights fourWeights) noexcept
	{
		static_assert(Tap < 4);
		return _mm256_shuffle_ps(fourWeights, fourWeights, Tap * 0x55);
	}

	// 16 bytes as 16 floats: the first 8 go to first
	IMAGE_PROCESSING_SIMD_INLINE void loadSixteenBytesAsFloats(const uint8_t* bytes, Floats8& first, Floats8& second) noexcept
	{
		const __m128i loaded = _mm_loadu_si128(reinterpret_cast<const __m128i*>(bytes));
		first = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(loaded));
		second = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(loaded, loaded)));
	}

	// 4 RGB pixels as 4 bytes each, the 4th 0. Reads 16 bytes, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE __m128i loadFourRgbPixelsAsRgb0(const uint8_t* pixels) noexcept
	{
		const __m128i rgbToRgb0 = _mm_setr_epi8(
			0, 1, 2, -1,
			3, 4, 5, -1,
			6, 7, 8, -1,
			9, 10, 11, -1);
		return _mm_shuffle_epi8(_mm_loadu_si128(reinterpret_cast<const __m128i*>(pixels)), rgbToRgb0);
	}

	// 4 RGB pixels as 4 floats each, the 4th 0: pixels 0 and 1 go to pixels01. Reads 16 bytes, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelsAsFloats(const uint8_t* pixels, Floats8& pixels01, Floats8& pixels23) noexcept
	{
		const __m128i bytes = loadFourRgbPixelsAsRgb0(pixels);
		pixels01 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(bytes));
		pixels23 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(bytes, bytes)));
	}

	// 4 pixels of 4 bytes from each of two rows as floats, a pixel pair per Floats8: row A's pixel i in pairI's low half, row B's in its high half
	IMAGE_PROCESSING_SIMD_INLINE void fourPixelPairsAsFloats(__m128i pixelsA, __m128i pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		const __m128i pairs01 = _mm_unpacklo_epi32(pixelsA, pixelsB);
		const __m128i pairs23 = _mm_unpackhi_epi32(pixelsA, pixelsB);
		pair0 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(pairs01));
		pair1 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(pairs01, pairs01)));
		pair2 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(pairs23));
		pair3 = _mm256_cvtepi32_ps(_mm256_cvtepu8_epi32(_mm_unpackhi_epi64(pairs23, pairs23)));
	}

	IMAGE_PROCESSING_SIMD_INLINE void loadFourPixelPairsAsFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		fourPixelPairsAsFloats(
			_mm_loadu_si128(reinterpret_cast<const __m128i*>(pixelsA)), _mm_loadu_si128(reinterpret_cast<const __m128i*>(pixelsB)), pair0, pair1, pair2, pair3);
	}

	// loadFourPixelPairsAsFloats of RGB pixels, the 4th float 0. Reads 16 bytes per row, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelPairsAsFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, Floats8& pair0, Floats8& pair1, Floats8& pair2, Floats8& pair3) noexcept
	{
		fourPixelPairsAsFloats(loadFourRgbPixelsAsRgb0(pixelsA), loadFourRgbPixelsAsRgb0(pixelsB), pair0, pair1, pair2, pair3);
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

	IMAGE_PROCESSING_SIMD_INLINE __m256i roundEightFloatsToInts(Floats8 values) noexcept
	{
		return _mm256_cvttps_epi32(_mm256_add_ps(values, _mm256_set1_ps(0.5f)));
	}

	// Rounds 32 floats to 32 bytes, as 8 dwords: 0-3 hold the first 4 bytes of values0..3, 4-7 the last 4.
	// 256-bit packs work per 128-bit lane: one dword permute after them orders a whole block.
	// Signed saturation first: packus_epi16 reads its input as signed, and then clamps what is below 0 or past 255.
	IMAGE_PROCESSING_SIMD_INLINE __m256i packThirtyTwoFloatsToLaneBytes(Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		return _mm256_packus_epi16(
			_mm256_packs_epi32(roundEightFloatsToInts(values0), roundEightFloatsToInts(values1)),
			_mm256_packs_epi32(roundEightFloatsToInts(values2), roundEightFloatsToInts(values3)));
	}

	// Color is capped at alpha: writePixelBytes caps it too.
	// Valid on packThirtyTwoFloatsToLaneBytes' order too: it keeps each pixel's bytes together.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE __m256i capColorBytesAtAlpha(__m256i pixels) noexcept
	{
		static_assert(Channels == 2 || Channels == 4);
		const __m256i alphaOfEachByte = Channels == 4
			? _mm256_setr_epi8(3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15, 3, 3, 3, 3, 7, 7, 7, 7, 11, 11, 11, 11, 15, 15, 15, 15)
			: _mm256_setr_epi8(1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15, 1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 15);
		return _mm256_min_epu8(pixels, _mm256_shuffle_epi8(pixels, alphaOfEachByte));
	}

	IMAGE_PROCESSING_SIMD_INLINE void storeLaneBytesInValueOrder(uint8_t* dest, __m256i laneBytes) noexcept
	{
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_permutevar8x32_epi32(laneBytes, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7)));
	}

	// Target: both are stored alike at this level
	template <BlockTarget Target>
	IMAGE_PROCESSING_SIMD_INLINE void writeThirtyTwoBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		storeLaneBytesInValueOrder(dest, packThirtyTwoFloatsToLaneBytes(values0, values1, values2, values3));
	}

	// The values are pixels of Channels floats, alpha last
	template <BlockTarget Target, size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE void writeThirtyTwoBytesCappedAtAlpha(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		storeLaneBytesInValueOrder(dest, capColorBytesAtAlpha<Channels>(packThirtyTwoFloatsToLaneBytes(values0, values1, values2, values3)));
	}

	template <BlockTarget Target>
	IMAGE_PROCESSING_SIMD_INLINE void writeTwentyFourBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2) noexcept
	{
		const __m256i laneBytes = packThirtyTwoFloatsToLaneBytes(values0, values1, values2, values2);
		// The low lane gets bytes 0-15, the high lane 16-23
		const __m256i bytes = _mm256_permutevar8x32_epi32(laneBytes, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 2, 6));
		_mm_storeu_si128(reinterpret_cast<__m128i*>(dest), _mm256_castsi256_si128(bytes));
		_mm_storel_epi64(reinterpret_cast<__m128i*>(dest + 16), _mm256_extracti128_si256(bytes, 1));
	}

	IMAGE_PROCESSING_SIMD_INLINE Rgb32PixelTails rgb32PixelTails(uint8_t tailValue) noexcept
	{
		return _mm256_set1_epi32(std::bit_cast<int32_t>(static_cast<uint32_t>(tailValue) << 24));
	}

	// values0..2 hold the 8 pixels' 24 color floats
	IMAGE_PROCESSING_SIMD_INLINE void writeEightRgb32Pixels(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Rgb32PixelTails pixelTails) noexcept
	{
		const __m256i laneBytes = packThirtyTwoFloatsToLaneBytes(values0, values1, values2, values2);
		// Each lane gets its 4 pixels' 12 color bytes first: bytes 0-11, and 12-23
		const __m256i colorBytes = _mm256_permutevar8x32_epi32(laneBytes, _mm256_setr_epi32(0, 4, 1, 5, 5, 2, 6, 6));
		const __m256i rgbToRgb32 = _mm256_setr_epi8(
			0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1,
			0, 1, 2, -1, 3, 4, 5, -1, 6, 7, 8, -1, 9, 10, 11, -1);
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_or_si256(_mm256_shuffle_epi8(colorBytes, rgbToRgb32), pixelTails));
	}

	// The bytes premultiplyBlock takes
	inline constexpr size_t premultiplyBlockBytes = 32;

	// 16 words of Channels-word pixels, alpha last, two pixels' worth per 128-bit lane: color times alpha / 255, rounded to nearest; alpha kept.
	// (t + 127) / 255 is (t + 127) * 0x8081 >> 23 for any 16-bit t + 127, and color times alpha + 127 is at most 65152.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE __m256i premultiplyPixelWords(__m256i words) noexcept
	{
		static_assert(Channels == 2 || Channels == 4);
		const __m256i alphaOfEachWord = Channels == 4
			? _mm256_setr_epi8(6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15, 6, 7, 6, 7, 6, 7, 6, 7, 14, 15, 14, 15, 14, 15, 14, 15)
			: _mm256_setr_epi8(2, 3, 2, 3, 6, 7, 6, 7, 10, 11, 10, 11, 14, 15, 14, 15, 2, 3, 2, 3, 6, 7, 6, 7, 10, 11, 10, 11, 14, 15, 14, 15);
		const __m256i products = _mm256_add_epi16(_mm256_mullo_epi16(words, _mm256_shuffle_epi8(words, alphaOfEachWord)), _mm256_set1_epi16(127));
		const __m256i quotients = _mm256_srli_epi16(_mm256_mulhi_epu16(products, _mm256_set1_epi16(static_cast<short>(0x8081))), 7);
		return _mm256_blend_epi16(quotients, words, Channels == 4 ? 0x88 : 0xAA);
	}

	// premultiplyPixelsScalar of the block at source, stored at dest; dest may be source.
	// The unpacks and the pack work per 128-bit lane, so the bytes come back in order.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_INLINE void premultiplyBlock(uint8_t* dest, const uint8_t* source) noexcept
	{
		const __m256i bytes = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(source));
		const __m256i zero = _mm256_setzero_si256();
		const __m256i low = premultiplyPixelWords<Channels>(_mm256_unpacklo_epi8(bytes, zero));
		const __m256i high = premultiplyPixelWords<Channels>(_mm256_unpackhi_epi8(bytes, zero));
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_packus_epi16(low, high));
	}

	// The 4-byte pixels expandBlock writes, from one load of as many source pixels
	inline constexpr size_t expandBlockPixels = 8;
	// The selector bytes per block: each pixel's place in the load
	inline constexpr size_t expandSelectorBytes = 8;

	// placesInLoad: each of the block's pixels' source pixel, counted from the block's load
	IMAGE_PROCESSING_SIMD_INLINE void writeExpandSelectors(uint8_t* selectors, const uint8_t* placesInLoad) noexcept
	{
		for (size_t pixel = 0; pixel < expandBlockPixels; ++pixel)
			selectors[pixel] = placesInLoad[pixel];
	}

	IMAGE_PROCESSING_SIMD_INLINE void expandBlock(uint8_t* dest, const uint8_t* source, const uint8_t* selectors) noexcept
	{
		const __m256i order = _mm256_cvtepu8_epi32(_mm_loadl_epi64(reinterpret_cast<const __m128i*>(selectors)));
		_mm256_storeu_si256(reinterpret_cast<__m256i*>(dest), _mm256_permutevar8x32_epi32(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(source)), order));
	}
}
