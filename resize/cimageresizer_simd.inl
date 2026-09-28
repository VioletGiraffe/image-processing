// The SIMD kernels' outline, written against one instruction set's primitives.
// A cimageresizer_simd_<level>.cpp includes that level's simd_primitives_<level>.h, sets IMAGE_PROCESSING_SIMD_LEVEL to the
// level's namespace, then includes this file.

#ifndef IMAGE_PROCESSING_SIMD_LEVEL
	#error "Only a cimageresizer_simd_<level>.cpp includes this file"
#endif

#include "resize_internal.h"

#include "compiler/compiler_warnings_control.h"

#include <algorithm>
#include <array>
#include <assert.h>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <string.h>

// The float equality tests are exact-zero checks by design
DISABLE_CLANG_GCC_WARNING("-Wfloat-equal")

namespace ImageProcessing::Detail::IMAGE_PROCESSING_SIMD_LEVEL
{
	namespace
	{
		template <bool PremultiplyAlpha>
		IMAGE_PROCESSING_SIMD_INLINE void convertPixelsToFloats(const uint8_t* pixels, float* floats, size_t pixelCount) noexcept
		{
			size_t pixel = 0;
			for (; pixel + 4 <= pixelCount; pixel += 4)
			{
				Floats8 pixels01, pixels23;
				loadFourPixelsAsFloats(pixels + pixel * 4, pixels01, pixels23);
				if constexpr (PremultiplyAlpha)
				{
					pixels01 = premultiplyTwoPixels(pixels01);
					pixels23 = premultiplyTwoPixels(pixels23);
				}

				storeFloats8(floats + pixel * 4, pixels01);
				storeFloats8(floats + pixel * 4 + 8, pixels23);
			}

			for (; pixel < pixelCount; ++pixel)
			{
				Floats4 pixelFloats = loadPixelAsFloats(pixels + pixel * 4);
				if constexpr (PremultiplyAlpha)
					pixelFloats = premultiplyPixel(pixelFloats);

				storeFloats4(floats + pixel * 4, pixelFloats);
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
					const size_t convertedTarget = std::min({ std::max(end, converted + conversionChunk), base + capacity, pixelEnd });
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
			const size_t pixelEnd; // Conversion never reaches this pixel
			const bool premultiplyAlpha;
			size_t base = 0; // The source pixel at floats[row][0]
			size_t converted = 0; // Pixels [base, converted) are in the buffers
		};

		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void storeTempPixel(float* outPixel, Floats4 accum) noexcept
		{
			if constexpr (Channels == 4)
				storeFloats4(outPixel, accum);
			else
			{
				alignas(16) float channelValues[4];
				storeFloats4(channelValues, accum);
				::memcpy(outPixel, channelValues, Channels * sizeof(float));
			}
		}

		// Filters Rows source rows in one destination-column sweep. The weight loads, permutes and broadcasts
		// depend only on the column, so paired rows share them, while each row keeps its own accumulators and
		// its exact single-row arithmetic. Two rows is the register budget: the four spread constants plus four
		// accumulators per row nearly fill the file, a third row would spill inside the hottest loop.
		// Rows are passed as individual pointers: a pair of temp rows may straddle the ring's wrap.
		// Filters dest columns [destBegin, destEnd) into the temp rows' start.
		template <size_t Channels, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontalRowGroup(
			SlidingSourceFloats<Rows>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights) noexcept
		{
			static_assert(Rows == 1 || Rows == 2);

			const WeightPairSpreader weightPairs{};

			for (size_t dx = destBegin; dx < destEnd; ++dx)
			{
				const auto [firstPixel, weights] = xWeights.runFor(dx);
				const size_t tapCount = weights.size();
				const size_t runFloatOffset = source.prepareRun(firstPixel, tapCount);
				const float* srcPixelA = source.floats[0] + runFloatOffset;
				[[maybe_unused]] const float* srcPixelB = source.floats[Rows - 1] + runFloatOffset;

				// Lanes hold [even pixel | odd pixel] partial sums until the single reduction below the blocks
				Floats8 accumPairsA = zeroFloats8();
				[[maybe_unused]] Floats8 accumPairsB = zeroFloats8();
				size_t tap = 0;

				// A run is consecutive pixels, so 8 taps go through four independent FMA chains at two pixels
				// per register; per-tap accumulation into one register would serialize on the FMA latency.
				// Upscaling never enters this branch: a trimmed bicubic run is at most 4 taps.
				if (tapCount >= 8)
				{
					Floats8 accumA0 = zeroFloats8();
					Floats8 accumA1 = zeroFloats8();
					Floats8 accumA2 = zeroFloats8();
					Floats8 accumA3 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB0 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB1 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB2 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB3 = zeroFloats8();

					for (; tap + 8 <= tapCount; tap += 8)
					{
						const float* blockPixelsA = srcPixelA + tap * 4;
						[[maybe_unused]] const float* blockPixelsB = srcPixelB + tap * 4;
						const WeightBlock blockWeights = loadWeightBlock(weights.data() + tap);

						const Floats8 w0 = weightPairs.spread<0>(blockWeights);
						accumA0 = mulAdd(loadFloats8(blockPixelsA), w0, accumA0);
						if constexpr (Rows == 2)
							accumB0 = mulAdd(loadFloats8(blockPixelsB), w0, accumB0);

						const Floats8 w1 = weightPairs.spread<1>(blockWeights);
						accumA1 = mulAdd(loadFloats8(blockPixelsA + 8), w1, accumA1);
						if constexpr (Rows == 2)
							accumB1 = mulAdd(loadFloats8(blockPixelsB + 8), w1, accumB1);

						const Floats8 w2 = weightPairs.spread<2>(blockWeights);
						accumA2 = mulAdd(loadFloats8(blockPixelsA + 16), w2, accumA2);
						if constexpr (Rows == 2)
							accumB2 = mulAdd(loadFloats8(blockPixelsB + 16), w2, accumB2);

						const Floats8 w3 = weightPairs.spread<3>(blockWeights);
						accumA3 = mulAdd(loadFloats8(blockPixelsA + 24), w3, accumA3);
						if constexpr (Rows == 2)
							accumB3 = mulAdd(loadFloats8(blockPixelsB + 24), w3, accumB3);
					}

					accumPairsA = add(add(accumA0, accumA1), add(accumA2, accumA3));
					if constexpr (Rows == 2)
						accumPairsB = add(add(accumB0, accumB1), add(accumB2, accumB3));
				}

				// One narrower block covers a whole bicubic run and most of an 8-block remainder
				if (tap + 4 <= tapCount)
				{
					// A whole WeightBlock though only 4 weights are in play: pairs 0 and 1 never read the upper 4, and
					// the builder pads the weights array to keep the overread in bounds. On AVX2 the natural 4-float load
					// + castps128_ps256 compiles under MSVC to a 16-byte stack store that the 32-byte vpermps
					// memory operand then reloads, and a load wider than the store it overlaps cannot be
					// store-forwarded - a ~35-cycle stall, measured to roughly double the upscale pass.
					const WeightBlock blockWeights = loadWeightBlock(weights.data() + tap);
					const Floats8 w01 = weightPairs.spread<0>(blockWeights);
					const Floats8 w23 = weightPairs.spread<1>(blockWeights);

					const float* blockPixelsA = srcPixelA + tap * 4;
					accumPairsA = mulAdd(loadFloats8(blockPixelsA), w01, mulAdd(loadFloats8(blockPixelsA + 8), w23, accumPairsA));
					if constexpr (Rows == 2)
					{
						const float* blockPixelsB = srcPixelB + tap * 4;
						accumPairsB = mulAdd(loadFloats8(blockPixelsB), w01, mulAdd(loadFloats8(blockPixelsB + 8), w23, accumPairsB));
					}

					tap += 4;
				}

				Floats4 accumA = sumHalves(accumPairsA);
				[[maybe_unused]] Floats4 accumB = zeroFloats4();
				if constexpr (Rows == 2)
					accumB = sumHalves(accumPairsB);

				for (; tap < tapCount; ++tap)
				{
					const Floats4 weight = broadcastFloats4(weights[tap]);
					accumA = mulAdd(loadFloats4(srcPixelA + tap * 4), weight, accumA);
					if constexpr (Rows == 2)
						accumB = mulAdd(loadFloats4(srcPixelB + tap * 4), weight, accumB);
				}

				storeTempPixel<Channels>(tempRows[0] + (dx - destBegin) * Channels, accumA);
				if constexpr (Rows == 2)
					storeTempPixel<Channels>(tempRows[1] + (dx - destBegin) * Channels, accumB);
			}
		}

		// Writes one destination row from its y tap window. rowWeights covers both segments in order.
		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void filterVerticalDestRow(
			const std::array<TempRowSegment, 2>& segments,
			std::span<const float> rowWeights,
			size_t tempRowStride,
			uint8_t pixelTailValue,
			uint8_t* destRow,
			size_t destWidth) noexcept
		{
			constexpr size_t pixelsPerBlock = 8;
			constexpr size_t elementsPerVector = 8;
			const size_t blockedPixelCount = destWidth & ~(pixelsPerBlock - 1);
			[[maybe_unused]] Rgb32PixelTails pixelTails;
			if constexpr (Channels == 3)
				pixelTails = rgb32PixelTails(pixelTailValue);

			size_t pixel = 0;
			for (; pixel < blockedPixelCount; pixel += pixelsPerBlock)
			{
				Floats8 accum0 = zeroFloats8();
				Floats8 accum1 = zeroFloats8();
				Floats8 accum2 = zeroFloats8();
				[[maybe_unused]] Floats8 accum3 = zeroFloats8();
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
							const Floats8 weightVector = broadcastFloats8(*weight);
							accum0 = mulAdd(loadFloats8(source), weightVector, accum0);
							accum1 = mulAdd(loadFloats8(source + elementsPerVector), weightVector, accum1);
							accum2 = mulAdd(loadFloats8(source + elementsPerVector * 2), weightVector, accum2);
							if constexpr (Channels == 4)
								accum3 = mulAdd(loadFloats8(source + elementsPerVector * 3), weightVector, accum3);
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

	// Fully resizes destination rows [destRowBegin, destRowEnd), one column strip at a time, producing temp rows in pairs into a TempRowRing.
	// The ring is also what lets the pair write two store streams safely: into cold full-size temp, the interleaved streams
	// defeat the prefetch that hides each line's ownership read (measured ~1.2 cycles per temp byte, and software prefetch
	// does not recover it) - the ring is rewritten every few rows and stays cache-owned.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void resizeRows4BytePixels(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		size_t stripWidth,
		uint8_t pixelTailValue,
		uint64_t destRowBegin,
		uint64_t destRowEnd)
	{
		static_assert(Channels == 3 || Channels == 4);
		assert(destRowBegin < destRowEnd);

		const size_t destWidth = static_cast<size_t>(dest.width);
		const size_t tempRowStride = stripWidth * Channels;

		const TempRowRing ring{ yWeights, destRowBegin, destRowEnd, tempRowStride, 2 };

		const size_t sourceFloatsCapacity = SlidingSourceFloats<2>::capacityFor(xWeights.longestRun());
		const auto sourceFloats = std::make_unique_for_overwrite<float[]>(2 * sourceFloatsCapacity * 4);
		float* const sourceFloatsA = sourceFloats.get();
		float* const sourceFloatsB = sourceFloatsA + sourceFloatsCapacity * 4;
		const auto sourcePixels = [&source, srcRect](uint64_t srcRow) noexcept
		{
			return source.scanLine<uint8_t>(srcRect.top + srcRow) + srcRect.left * 4;
		};
		const bool premultiplyAlpha = hasStraightAlpha(source);

		for (size_t stripBegin = 0; stripBegin < destWidth; stripBegin += stripWidth)
		{
			const size_t stripEnd = std::min(stripBegin + stripWidth, destWidth);
			// Conversion runs ahead in chunks, and must not pass the strip's source pixels
			const size_t spanEnd = xWeights.sourceSpan(stripBegin, stripEnd).end;

			uint64_t produced = ring.firstNeededRow();
			for (uint64_t dy = destRowBegin; dy < destRowEnd; ++dy)
			{
				const auto [firstWindowRow, rowWeights] = yWeights.runFor(dy);
				const uint64_t windowEnd = firstWindowRow + rowWeights.size();
				assert(windowEnd <= srcRect.h);

				while (produced < windowEnd)
				{
					if (produced + 2 <= srcRect.h)
					{
						SlidingSourceFloats<2> sourceRows{ { sourcePixels(produced), sourcePixels(produced + 1) }, { sourceFloatsA, sourceFloatsB }, sourceFloatsCapacity, spanEnd, premultiplyAlpha };
						float* const tempRows[2] = { ring.row(produced), ring.row(produced + 1) };
						filterHorizontalRowGroup<Channels>(sourceRows, tempRows, stripBegin, stripEnd, xWeights);
						produced += 2;
					}
					else
					{
						SlidingSourceFloats<1> sourceRow{ { sourcePixels(produced) }, { sourceFloatsA }, sourceFloatsCapacity, spanEnd, premultiplyAlpha };
						float* const tempRows[1] = { ring.row(produced) };
						filterHorizontalRowGroup<Channels>(sourceRow, tempRows, stripBegin, stripEnd, xWeights);
						++produced;
					}
				}

				assert(produced - firstWindowRow <= ring.rowCapacity());
				uint8_t* const stripDest = dest.scanLine<uint8_t>(dy) + stripBegin * 4;
				filterVerticalDestRow<Channels>(ring.window(firstWindowRow, rowWeights.size()), rowWeights, tempRowStride, pixelTailValue, stripDest, stripEnd - stripBegin);
			}
		}

		leaveKernel();
	}

	template void resizeRows4BytePixels<3>(const ImageView<true>&, Rect, ImageView<false>&, const AxisWeights&, const AxisWeights&, size_t, uint8_t, uint64_t, uint64_t);
	template void resizeRows4BytePixels<4>(const ImageView<true>&, Rect, ImageView<false>&, const AxisWeights&, const AxisWeights&, size_t, uint8_t, uint64_t, uint64_t);
}
