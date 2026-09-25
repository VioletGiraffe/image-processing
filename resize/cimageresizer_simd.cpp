#include "resize_internal.h"

#include "compiler/compiler_warnings_control.h"

#include <algorithm>
#include <array>
#include <assert.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string.h>

// The float equality tests are exact-zero checks by design
DISABLE_CLANG_GCC_WARNING("-Wfloat-equal")
// The loadu/storeu intrinsics take unaligned memory through __m128i pointers
DISABLE_CLANG_GCC_WARNING("-Wcast-align")

#if IMAGE_PROCESSING_SIMD

namespace ImageProcessing::Detail
{
	namespace
	{
		// Rounds 8 floats to 8 words in order.
		// The packs are 128-bit: 256-bit packs work per lane and need a lane-crossing permute after them, which SIMDe emulates element by element on NEON.
		IMAGE_PROCESSING_SIMD_INLINE simde__m128i packEightFloatsToWords(simde__m256 values) noexcept
		{
			// packus_epi32 saturates negatives to 0; the 255 cap must stay - values past 32767 would wrap negative through the signed-input packus_epi16
			values = simde_mm256_min_ps(simde_mm256_set1_ps(255.0f), values);
			const simde__m256i integers = simde_mm256_cvttps_epi32(simde_mm256_add_ps(values, simde_mm256_set1_ps(0.5f)));
			return simde_mm_packus_epi32(simde_mm256_castsi256_si128(integers), simde_mm256_extracti128_si256(integers, 1));
		}

		IMAGE_PROCESSING_SIMD_INLINE simde__m128i packSixteenFloatsToBytes(simde__m256 first, simde__m256 second) noexcept
		{
			return simde_mm_packus_epi16(packEightFloatsToWords(first), packEightFloatsToWords(second));
		}

		// Color is capped at alpha, as writePixelBytes does
		IMAGE_PROCESSING_SIMD_INLINE simde__m256 capColorAtAlpha(simde__m256 twoPixels) noexcept
		{
			return simde_mm256_min_ps(twoPixels, simde_mm256_shuffle_ps(twoPixels, twoPixels, SIMDE_MM_SHUFFLE(3, 3, 3, 3)));
		}

		IMAGE_PROCESSING_SIMD_INLINE void writeEightRgbaPixels(
			uint8_t* dest,
			simde__m256 values0,
			simde__m256 values1,
			simde__m256 values2,
			simde__m256 values3) noexcept
		{
			simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(dest), packSixteenFloatsToBytes(capColorAtAlpha(values0), capColorAtAlpha(values1)));
			simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(dest + 16), packSixteenFloatsToBytes(capColorAtAlpha(values2), capColorAtAlpha(values3)));
		}

		IMAGE_PROCESSING_SIMD_INLINE void writeEightRgb32Pixels(
			uint8_t* dest,
			simde__m256 values0,
			simde__m256 values1,
			simde__m256 values2,
			simde__m128i pixelTails) noexcept
		{
			const simde__m128i firstSixteenRgbBytes = packSixteenFloatsToBytes(values0, values1);
			const simde__m128i lastWords = packEightFloatsToWords(values2);
			const simde__m128i lastEightRgbBytes = simde_mm_packus_epi16(lastWords, lastWords);
			const simde__m128i lastTwelveRgbBytes = simde_mm_alignr_epi8(lastEightRgbBytes, firstSixteenRgbBytes, 12);
			const simde__m128i rgbToRgb32 = simde_mm_setr_epi8(
				0, 1, 2, -1,
				3, 4, 5, -1,
				6, 7, 8, -1,
				9, 10, 11, -1);

			const simde__m128i pixels0 = simde_mm_or_si128(simde_mm_shuffle_epi8(firstSixteenRgbBytes, rgbToRgb32), pixelTails);
			const simde__m128i pixels1 = simde_mm_or_si128(simde_mm_shuffle_epi8(lastTwelveRgbBytes, rgbToRgb32), pixelTails);
			simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(dest), pixels0);
			simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(dest + 16), pixels1);
		}

		IMAGE_PROCESSING_SIMD_INLINE simde__m128 loadPixelAsFloats(const uint8_t* pixel) noexcept
		{
			int32_t packedPixel;
			::memcpy(&packedPixel, pixel, sizeof(packedPixel));
			return simde_mm_cvtepi32_ps(simde_mm_cvtepu8_epi32(simde_mm_cvtsi32_si128(packedPixel)));
		}

		// Color times alpha / 255; the alpha lanes keep the source value
		IMAGE_PROCESSING_SIMD_INLINE simde__m256 premultiplyTwoPixels(simde__m256 twoPixels) noexcept
		{
			const simde__m256 alpha = simde_mm256_shuffle_ps(twoPixels, twoPixels, SIMDE_MM_SHUFFLE(3, 3, 3, 3));
			const simde__m256 premultiplied = simde_mm256_mul_ps(twoPixels, simde_mm256_mul_ps(alpha, simde_mm256_set1_ps(1.0f / 255.0f)));
			return simde_mm256_blend_ps(premultiplied, twoPixels, 0x88);
		}

		IMAGE_PROCESSING_SIMD_INLINE simde__m128 premultiplyPixel(simde__m128 pixel) noexcept
		{
			const simde__m128 alpha = simde_mm_shuffle_ps(pixel, pixel, SIMDE_MM_SHUFFLE(3, 3, 3, 3));
			const simde__m128 premultiplied = simde_mm_mul_ps(pixel, simde_mm_mul_ps(alpha, simde_mm_set1_ps(1.0f / 255.0f)));
			return simde_mm_blend_ps(premultiplied, pixel, 0x8);
		}

		template <bool PremultiplyAlpha>
		IMAGE_PROCESSING_SIMD_INLINE void convertPixelsToFloats(const uint8_t* pixels, float* floats, size_t pixelCount) noexcept
		{
			size_t pixel = 0;
			for (; pixel + 4 <= pixelCount; pixel += 4)
			{
				const simde__m128i bytes = simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(pixels + pixel * 4));
				simde__m256 pixels01 = simde_mm256_cvtepi32_ps(simde_mm256_cvtepu8_epi32(bytes));
				simde__m256 pixels23 = simde_mm256_cvtepi32_ps(simde_mm256_cvtepu8_epi32(simde_mm_unpackhi_epi64(bytes, bytes)));
				if constexpr (PremultiplyAlpha)
				{
					pixels01 = premultiplyTwoPixels(pixels01);
					pixels23 = premultiplyTwoPixels(pixels23);
				}

				simde_mm256_storeu_ps(floats + pixel * 4, pixels01);
				simde_mm256_storeu_ps(floats + pixel * 4 + 8, pixels23);
			}

			for (; pixel < pixelCount; ++pixel)
			{
				simde__m128 pixelFloats = loadPixelAsFloats(pixels + pixel * 4);
				if constexpr (PremultiplyAlpha)
					pixelFloats = premultiplyPixel(pixelFloats);

				simde_mm_storeu_ps(floats + pixel * 4, pixelFloats);
			}
		}

		// A row group's source pixels as floats, 4 per pixel, over a span that slides along the rows as the x runs advance.
		// Each pixel is converted once: overlapping runs would otherwise convert it once per run.
		// The span stays a few KB so that it lives in L1: whole converted rows overflow the L2 that parallel bands share.
		template <size_t Rows>
		struct SlidingSourceFloats
		{
			// Room kept behind a run's first pixel when the span slides: end-trimming lets a later run start a few pixels earlier
			static constexpr size_t backMargin = 8;
			// Pixels converted ahead of the current run, so that conversion runs in batches
			static constexpr size_t conversionChunk = 128;

			// A slide moves the floats still needed to the front: room for two runs keeps that to about one move per pixel
			[[nodiscard]] static constexpr size_t capacityFor(size_t longestRun) noexcept
			{
				return 2 * longestRun + backMargin + conversionChunk;
			}

			// Converts whatever of pixels [first, first + count) is missing; returns the float offset of pixel first in each buffer
			IMAGE_PROCESSING_SIMD_INLINE size_t prepareRun(size_t first, size_t count) noexcept
			{
				assert(count + backMargin <= capacity);
				const size_t end = first + count;
				if (first < base || end > base + capacity)
				{
					const size_t newBase = first > backMargin ? first - backMargin : 0;
					if (newBase >= base && newBase < converted)
					{
						for (size_t row = 0; row < Rows; ++row)
							::memmove(floats[row], floats[row] + (newBase - base) * 4, (converted - newBase) * 4 * sizeof(float));
					}
					else
						converted = newBase; // Nothing converted is reusable: the run starts before the span or past its converted end

					base = newBase;
				}

				if (end > converted)
				{
					const size_t convertedTarget = std::min({ std::max(end, converted + conversionChunk), base + capacity, pixelCount });
					for (size_t row = 0; row < Rows; ++row)
					{
						const uint8_t* rowPixels = pixels[row] + converted * 4;
						float* rowFloats = floats[row] + (converted - base) * 4;
						if (premultiplyAlpha)
							convertPixelsToFloats<true>(rowPixels, rowFloats, convertedTarget - converted);
						else
							convertPixelsToFloats<false>(rowPixels, rowFloats, convertedTarget - converted);
					}

					converted = convertedTarget;
				}

				return (first - base) * 4;
			}

			const uint8_t* const pixels[Rows];
			float* const floats[Rows];
			const size_t capacity; // In pixels
			const size_t pixelCount;
			const bool premultiplyAlpha;
			size_t base = 0; // The source pixel at floats[row][0]
			size_t converted = 0; // Pixels [base, converted) are in the buffers
		};

		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void storeTempPixel(float* outPixel, simde__m128 accum) noexcept
		{
			if constexpr (Channels == 4)
				simde_mm_storeu_ps(outPixel, accum);
			else
			{
				alignas(16) float channelValues[4];
				simde_mm_store_ps(channelValues, accum);
				::memcpy(outPixel, channelValues, Channels * sizeof(float));
			}
		}

		// Filters Rows source rows in one destination-column sweep. The weight loads, permutes and broadcasts
		// depend only on the column, so paired rows share them, while each row keeps its own accumulators and
		// its exact single-row arithmetic. Two rows is the register budget: the four spread constants plus four
		// accumulators per row nearly fill the file, a third row would spill inside the hottest loop.
		// Rows are passed as individual pointers: a pair of temp rows may straddle the ring's wrap.
		template <size_t Channels, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontalRowGroup(
			SlidingSourceFloats<Rows>& source,
			float* const (&tempRows)[Rows],
			uint64_t destWidth,
			const AxisWeights& xWeights) noexcept
		{
			static_assert(Rows == 1 || Rows == 2);

			// Spread patterns turning 8 consecutive weights into the pair-broadcast [w(i) x4 | w(i+1) x4] each pixel pair needs
			const simde__m256i weightSpread0 = simde_mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1);
			const simde__m256i weightSpread1 = simde_mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3);
			const simde__m256i weightSpread2 = simde_mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5);
			const simde__m256i weightSpread3 = simde_mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);

			for (uint64_t dx = 0; dx < destWidth; ++dx)
			{
				const auto [srcStartOffset, weights] = xWeights.runFor(dx);
				const size_t tapCount = weights.size();
				// The x offsets count 4 bytes per source pixel
				const size_t runFloatOffset = source.prepareRun(srcStartOffset / 4, tapCount);
				const float* srcPixelA = source.floats[0] + runFloatOffset;
				[[maybe_unused]] const float* srcPixelB = source.floats[Rows - 1] + runFloatOffset;

				// Lanes hold [even pixel | odd pixel] partial sums until the single reduction below the blocks
				simde__m256 accumPairsA = simde_mm256_setzero_ps();
				[[maybe_unused]] simde__m256 accumPairsB = simde_mm256_setzero_ps();
				size_t tap = 0;

				// A run is consecutive pixels, so 8 taps go through four independent FMA chains at two pixels
				// per register; per-tap accumulation into one register would serialize on the FMA latency.
				// Upscaling never enters this branch: a trimmed bicubic run is at most 4 taps.
				if (tapCount >= 8)
				{
					simde__m256 accumA0 = simde_mm256_setzero_ps();
					simde__m256 accumA1 = simde_mm256_setzero_ps();
					simde__m256 accumA2 = simde_mm256_setzero_ps();
					simde__m256 accumA3 = simde_mm256_setzero_ps();
					[[maybe_unused]] simde__m256 accumB0 = simde_mm256_setzero_ps();
					[[maybe_unused]] simde__m256 accumB1 = simde_mm256_setzero_ps();
					[[maybe_unused]] simde__m256 accumB2 = simde_mm256_setzero_ps();
					[[maybe_unused]] simde__m256 accumB3 = simde_mm256_setzero_ps();

					for (; tap + 8 <= tapCount; tap += 8)
					{
						const float* blockPixelsA = srcPixelA + tap * 4;
						[[maybe_unused]] const float* blockPixelsB = srcPixelB + tap * 4;
						const simde__m256 blockWeights = simde_mm256_loadu_ps(weights.data() + tap);

						const simde__m256 w0 = simde_mm256_permutevar8x32_ps(blockWeights, weightSpread0);
						accumA0 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsA), w0, accumA0);
						if constexpr (Rows == 2)
							accumB0 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsB), w0, accumB0);

						const simde__m256 w1 = simde_mm256_permutevar8x32_ps(blockWeights, weightSpread1);
						accumA1 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsA + 8), w1, accumA1);
						if constexpr (Rows == 2)
							accumB1 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsB + 8), w1, accumB1);

						const simde__m256 w2 = simde_mm256_permutevar8x32_ps(blockWeights, weightSpread2);
						accumA2 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsA + 16), w2, accumA2);
						if constexpr (Rows == 2)
							accumB2 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsB + 16), w2, accumB2);

						const simde__m256 w3 = simde_mm256_permutevar8x32_ps(blockWeights, weightSpread3);
						accumA3 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsA + 24), w3, accumA3);
						if constexpr (Rows == 2)
							accumB3 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsB + 24), w3, accumB3);
					}

					accumPairsA = simde_mm256_add_ps(simde_mm256_add_ps(accumA0, accumA1), simde_mm256_add_ps(accumA2, accumA3));
					if constexpr (Rows == 2)
						accumPairsB = simde_mm256_add_ps(simde_mm256_add_ps(accumB0, accumB1), simde_mm256_add_ps(accumB2, accumB3));
				}

				// One narrower block covers a whole bicubic run and most of an 8-block remainder
				if (tap + 4 <= tapCount)
				{
					// 8-float load though only 4 are in play: the spread indices never select the upper lanes, and
					// the builder pads the weights array to keep the overread in bounds. The natural 4-float load
					// + castps128_ps256 compiles under MSVC to a 16-byte stack store that the 32-byte vpermps
					// memory operand then reloads, and a load wider than the store it overlaps cannot be
					// store-forwarded - a ~35-cycle stall, measured to roughly double the upscale pass.
					const simde__m256 blockWeights = simde_mm256_loadu_ps(weights.data() + tap);
					const simde__m256 w01 = simde_mm256_permutevar8x32_ps(blockWeights, weightSpread0);
					const simde__m256 w23 = simde_mm256_permutevar8x32_ps(blockWeights, weightSpread1);

					const float* blockPixelsA = srcPixelA + tap * 4;
					accumPairsA = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsA), w01, simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsA + 8), w23, accumPairsA));
					if constexpr (Rows == 2)
					{
						const float* blockPixelsB = srcPixelB + tap * 4;
						accumPairsB = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsB), w01, simde_mm256_fmadd_ps(simde_mm256_loadu_ps(blockPixelsB + 8), w23, accumPairsB));
					}

					tap += 4;
				}

				simde__m128 accumA = simde_mm_add_ps(simde_mm256_castps256_ps128(accumPairsA), simde_mm256_extractf128_ps(accumPairsA, 1));
				[[maybe_unused]] simde__m128 accumB = simde_mm_setzero_ps();
				if constexpr (Rows == 2)
					accumB = simde_mm_add_ps(simde_mm256_castps256_ps128(accumPairsB), simde_mm256_extractf128_ps(accumPairsB, 1));

				for (; tap < tapCount; ++tap)
				{
					const simde__m128 weight = simde_mm_set1_ps(weights[tap]);
					accumA = simde_mm_fmadd_ps(simde_mm_loadu_ps(srcPixelA + tap * 4), weight, accumA);
					if constexpr (Rows == 2)
						accumB = simde_mm_fmadd_ps(simde_mm_loadu_ps(srcPixelB + tap * 4), weight, accumB);
				}

				storeTempPixel<Channels>(tempRows[0] + static_cast<size_t>(dx) * Channels, accumA);
				if constexpr (Rows == 2)
					storeTempPixel<Channels>(tempRows[1] + static_cast<size_t>(dx) * Channels, accumB);
			}
		}

		// A tap window's temp rows as they sit in the ring: contiguous except for at most one wrap
		struct TempRowSegment
		{
			const float* firstRow;
			size_t rowCount;
		};

		// Writes one destination row from its y tap window. rowWeights covers both segments in order.
		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void filterVerticalDestRow(
			const TempRowSegment (&segments)[2],
			std::span<const float> rowWeights,
			size_t tempRowStride,
			uint8_t pixelTailValue,
			uint8_t* destRow,
			size_t destWidth) noexcept
		{
			constexpr size_t pixelsPerBlock = 8;
			constexpr size_t elementsPerVector = 8;
			const size_t blockedPixelCount = destWidth & ~(pixelsPerBlock - 1);
			[[maybe_unused]] simde__m128i pixelTails;
			if constexpr (Channels == 3)
				pixelTails = simde_mm_set1_epi32(std::bit_cast<int32_t>(static_cast<uint32_t>(pixelTailValue) << 24));

			size_t pixel = 0;
			for (; pixel < blockedPixelCount; pixel += pixelsPerBlock)
			{
				simde__m256 accum0 = simde_mm256_setzero_ps();
				simde__m256 accum1 = simde_mm256_setzero_ps();
				simde__m256 accum2 = simde_mm256_setzero_ps();
				[[maybe_unused]] simde__m256 accum3 = simde_mm256_setzero_ps();
				const float* weight = rowWeights.data();

				for (const TempRowSegment& segment : segments)
				{
					const float* source = segment.firstRow + pixel * Channels;

					// A zero tap costs a whole row sweep, and exact-ratio downscales produce them
					// (the kernels are zero at integer offsets)
					for (const float* segmentEnd = weight + segment.rowCount; weight != segmentEnd; ++weight)
					{
						if (*weight != 0.0f)
						{
							const simde__m256 weightVector = simde_mm256_set1_ps(*weight);
							accum0 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source), weightVector, accum0);
							accum1 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector), weightVector, accum1);
							accum2 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 2), weightVector, accum2);
							if constexpr (Channels == 4)
								accum3 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 3), weightVector, accum3);
						}

						source += tempRowStride;
					}
				}

				if constexpr (Channels == 3)
					writeEightRgb32Pixels(destRow + pixel * 4, accum0, accum1, accum2, pixelTails);
				else
					writeEightRgbaPixels(destRow + pixel * 4, accum0, accum1, accum2, accum3);
			}

			for (; pixel < destWidth; ++pixel)
			{
				std::array<float, Channels> accum{};
				const float* weight = rowWeights.data();

				for (const TempRowSegment& segment : segments)
				{
					const float* source = segment.firstRow + pixel * Channels;
					for (const float* segmentEnd = weight + segment.rowCount; weight != segmentEnd; ++weight)
					{
						if (*weight != 0.0f)
						{
							for (size_t channel = 0; channel < Channels; ++channel)
								accum[channel] = std::fma(source[channel], *weight, accum[channel]);
						}

						source += tempRowStride;
					}
				}

				uint8_t* destPixel = destRow + pixel * 4;
				writePixelBytes(destPixel, accum.data(), Channels);

				if constexpr (Channels == 3)
					destPixel[3] = pixelTailValue;
			}
		}
	}

	// Fully resizes destination rows [destRowBegin, destRowEnd): temp rows are produced (in pairs) into a ring
	// barely larger than the y tap window and consumed immediately, so the intermediate rows stay cache-resident
	// instead of a whole-image temp buffer making a DRAM round trip between the passes. The ring is also what
	// lets the pair write two store streams safely: into cold full-size temp, the interleaved streams defeat the
	// prefetch that hides each line's ownership read (measured ~1.2 cycles per temp byte, and software prefetch
	// does not recover it) - the ring is rewritten every few rows and stays cache-owned.
	// Bands are independent: each computes every temp row its windows need, so neighbors re-do the shared boundary
	// rows rather than hand them off.
	// yWeights.startOffset must be the plain source row index - the ring decides the actual location.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void resizeRows4BytePixelsSimd(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		uint8_t pixelTailValue,
		uint64_t destRowBegin,
		uint64_t destRowEnd)
	{
		static_assert(Channels == 3 || Channels == 4);
		assert(destRowBegin < destRowEnd);

		const size_t destWidth = static_cast<size_t>(dest.width);
		const size_t tempRowStride = destWidth * Channels;

		// Ring capacity: while writing dest row dy, production has reached at most one row past dy's window
		// (pair production), and end-trimming lets a later window start slightly before an earlier one - so
		// measure each window's end against the earliest start any not-yet-written row still needs.
		uint64_t firstNeededRow = UINT64_MAX;
		size_t ringRows = 0;
		for (uint64_t dy = destRowEnd; dy-- > destRowBegin;)
		{
			const auto run = yWeights.runFor(dy);
			firstNeededRow = std::min(firstNeededRow, static_cast<uint64_t>(run.startOffset));
			ringRows = std::max(ringRows, run.startOffset + run.weights.size() + 1 - static_cast<size_t>(firstNeededRow));
		}

		const auto ring = std::make_unique_for_overwrite<float[]>(ringRows * tempRowStride);
		const auto ringRow = [&ring, ringRows, tempRowStride](uint64_t srcRow) noexcept
		{
			return ring.get() + static_cast<size_t>(srcRow % ringRows) * tempRowStride;
		};

		const size_t srcWidth = static_cast<size_t>(srcRect.w);
		size_t longestXRun = 0;
		for (const TapRun& run : xWeights.runs)
			longestXRun = std::max(longestXRun, run.weightCount);

		const size_t sourceFloatsCapacity = SlidingSourceFloats<2>::capacityFor(longestXRun);
		const auto sourceFloats = std::make_unique_for_overwrite<float[]>(2 * sourceFloatsCapacity * 4);
		float* const sourceFloatsA = sourceFloats.get();
		float* const sourceFloatsB = sourceFloatsA + sourceFloatsCapacity * 4;
		const auto sourcePixels = [&source, srcRect](uint64_t srcRow) noexcept
		{
			return source.scanLine<uint8_t>(srcRect.top + srcRow) + srcRect.left * 4;
		};
		const bool premultiplyAlpha = hasStraightAlpha(source);

		uint64_t produced = firstNeededRow;
		for (uint64_t dy = destRowBegin; dy < destRowEnd; ++dy)
		{
			const auto [firstWindowRow, rowWeights] = yWeights.runFor(dy);
			const uint64_t windowEnd = firstWindowRow + rowWeights.size();
			assert(windowEnd <= srcRect.h);

			while (produced < windowEnd)
			{
				if (produced + 2 <= srcRect.h)
				{
					SlidingSourceFloats<2> sourceRows{ { sourcePixels(produced), sourcePixels(produced + 1) }, { sourceFloatsA, sourceFloatsB }, sourceFloatsCapacity, srcWidth, premultiplyAlpha };
					float* const tempRows[2] = { ringRow(produced), ringRow(produced + 1) };
					filterHorizontalRowGroup<Channels>(sourceRows, tempRows, destWidth, xWeights);
					produced += 2;
				}
				else
				{
					SlidingSourceFloats<1> sourceRow{ { sourcePixels(produced) }, { sourceFloatsA }, sourceFloatsCapacity, srcWidth, premultiplyAlpha };
					float* const tempRows[1] = { ringRow(produced) };
					filterHorizontalRowGroup<Channels>(sourceRow, tempRows, destWidth, xWeights);
					++produced;
				}
			}

			// The window is contiguous in the ring except across the wrap, which splits it at most once
			assert(produced - firstWindowRow <= ringRows);
			const size_t firstSlot = static_cast<size_t>(firstWindowRow % ringRows);
			const size_t rowsToRingEnd = std::min(rowWeights.size(), ringRows - firstSlot);
			const TempRowSegment segments[2] = {
				{ ring.get() + firstSlot * tempRowStride, rowsToRingEnd },
				{ ring.get(), rowWeights.size() - rowsToRingEnd } };

			filterVerticalDestRow<Channels>(segments, rowWeights, tempRowStride, pixelTailValue, dest.scanLine<uint8_t>(dy), destWidth);
		}

		SimdSupport::clearAvxUpperState();
	}

	template void resizeRows4BytePixelsSimd<3>(const ImageView<true>&, Rect, ImageView<false>&, const AxisWeights&, const AxisWeights&, uint8_t, uint64_t, uint64_t);
	template void resizeRows4BytePixelsSimd<4>(const ImageView<true>&, Rect, ImageView<false>&, const AxisWeights&, const AxisWeights&, uint8_t, uint64_t, uint64_t);
}

#endif
