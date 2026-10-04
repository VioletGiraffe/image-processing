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
		// 3 channels get a 4th float, 0: the 4-channel code then serves them
		[[nodiscard]] constexpr size_t sourceFloatsPerPixel(size_t channels) noexcept
		{
			return channels == 3 ? 4 : channels;
		}

		// PixelStride 0: the stride is taken at runtime
		template <size_t PixelStride>
		[[nodiscard]] constexpr size_t effectivePixelStride(size_t runtimePixelStride) noexcept
		{
			return PixelStride != 0 ? PixelStride : runtimePixelStride;
		}

		template <size_t Channels, size_t PixelStride, bool PremultiplyAlpha>
		IMAGE_PROCESSING_SIMD_INLINE void convertPixelsToFloats(const uint8_t* pixels, size_t runtimePixelStride, float* floats, size_t pixelCount) noexcept
		{
			constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
			size_t pixel = 0;

			// A 16-byte load: 16 pixel floats, or 4 RGB pixels and 4 bytes past them
			if constexpr (PixelStride == floatsPerPixel || (Channels == 3 && PixelStride == 3))
			{
				constexpr size_t blockPixels = 16 / floatsPerPixel;
				constexpr size_t loadedPixels = (16 + PixelStride - 1) / PixelStride;
				for (; pixel + loadedPixels <= pixelCount; pixel += blockPixels)
				{
					Floats8 first, second;
					if constexpr (PixelStride == 3)
						loadFourRgbPixelsAsFloats(pixels + pixel * 3, first, second);
					else
						loadSixteenBytesAsFloats(pixels + pixel * PixelStride, first, second);

					if constexpr (PremultiplyAlpha)
					{
						first = premultiplyPixels<floatsPerPixel>(first);
						second = premultiplyPixels<floatsPerPixel>(second);
					}

					storeFloats8(floats + pixel * floatsPerPixel, first);
					storeFloats8(floats + pixel * floatsPerPixel + 8, second);
				}
			}

			// Premultiplies with the vector code's arithmetic: one row mixes both
			for (; pixel < pixelCount; ++pixel)
			{
				const uint8_t* const sourcePixel = pixels + pixel * pixelStride;
				float* const pixelFloats = floats + pixel * floatsPerPixel;
				if constexpr (PremultiplyAlpha)
				{
					const float alpha = static_cast<float>(sourcePixel[Channels - 1]);
					const float premultiplier = alpha * (1.0f / 255.0f);
					for (size_t channel = 0; channel + 1 < Channels; ++channel)
						pixelFloats[channel] = static_cast<float>(sourcePixel[channel]) * premultiplier;

					pixelFloats[Channels - 1] = alpha;
				}
				else
				{
					for (size_t channel = 0; channel < Channels; ++channel)
						pixelFloats[channel] = static_cast<float>(sourcePixel[channel]);
				}

				if constexpr (floatsPerPixel > Channels)
					pixelFloats[Channels] = 0.0f;
			}
		}

		// A row group's source pixels as floats, sourceFloatsPerPixel per pixel, over a span that slides along the rows as the x runs advance.
		// Each pixel is converted once: overlapping runs would otherwise convert it once per run.
		// The span stays a few KB so that it lives in L1: whole converted rows overflow the L2 that parallel bands share.
		template <size_t Channels, size_t PixelStride, size_t Rows>
		struct SlidingSourceFloats
		{
			static constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);

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
							::memmove(floats[row], floats[row] + (newBase - base) * floatsPerPixel, (converted - newBase) * floatsPerPixel * sizeof(float));
					}
					else
						converted = newBase; // Nothing converted is reusable: the run starts before the span or past its converted end

					base = newBase;
				}

				if (end > converted)
				{
					// Two-argument min: the initializer-list overload is a library call under MSVC
					const size_t convertedTarget = std::min(std::min(std::max(end, converted + conversionChunk), base + capacity), pixelEnd);
					const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
					for (size_t row = 0; row < Rows; ++row)
					{
						const uint8_t* rowPixels = pixels[row] + converted * pixelStride;
						float* rowFloats = floats[row] + (converted - base) * floatsPerPixel;
						if (hasAlphaChannel(Channels) && premultiplyAlpha)
							convertPixelsToFloats<Channels, PixelStride, hasAlphaChannel(Channels)>(rowPixels, pixelStride, rowFloats, convertedTarget - converted);
						else
							convertPixelsToFloats<Channels, PixelStride, false>(rowPixels, pixelStride, rowFloats, convertedTarget - converted);
					}

					converted = convertedTarget;
				}

				return (first - base) * floatsPerPixel;
			}

			const uint8_t* const pixels[Rows];
			float* const floats[Rows];
			const size_t capacity; // In pixels
			const size_t pixelEnd; // Conversion never reaches this pixel
			const size_t runtimePixelStride;
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

		// Adds the last taps of a 1- or 2-float pixel channel by channel, then stores it
		template <size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE void storeTempPixelWithTaps(float* outPixel, Floats4 partialSums, const float* sourcePixels, const float* weights, size_t tapCount) noexcept
		{
			alignas(16) float sums[4];
			storeFloats4(sums, sumPixels<FloatsPerPixel>(partialSums));
			for (size_t tap = 0; tap < tapCount; ++tap)
			{
				for (size_t channel = 0; channel < FloatsPerPixel; ++channel)
					sums[channel] = mulAdd(sourcePixels[tap * FloatsPerPixel + channel], weights[tap], sums[channel]);
			}

			::memcpy(outPixel, sums, FloatsPerPixel * sizeof(float));
		}

		// The weights for chain Chain of a block of 32 source floats that starts at blockWeights
		template <size_t Chain, size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 chainWeights(const WeightSpreader<FloatsPerPixel>& spreader, const float* blockWeights, WeightBlock firstWeights) noexcept
		{
			constexpr size_t weightBlock = Chain / FloatsPerPixel;
			if constexpr (weightBlock == 0)
				return spreader.template spread<Chain % FloatsPerPixel>(firstWeights);
			else
				return spreader.template spread<Chain % FloatsPerPixel>(loadWeightBlock(blockWeights + weightBlock * 8));
		}

		// Filters Rows source rows in one destination-column sweep. The weight loads, permutes and broadcasts
		// depend only on the column, so paired rows share them, while each row keeps its own accumulators and
		// its exact single-row arithmetic. Two rows is the register budget: the four spread constants plus four
		// accumulators per row nearly fill the file, a third row would spill inside the hottest loop.
		// Rows are passed as individual pointers: a pair of temp rows may straddle the ring's wrap.
		// Filters dest columns [destBegin, destEnd) into the temp rows' start.
		template <size_t Channels, size_t PixelStride, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontalRowGroup(
			SlidingSourceFloats<Channels, PixelStride, Rows>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights) noexcept
		{
			static_assert(Rows == 1 || Rows == 2);

			constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			// Taps per block of 32 source floats
			constexpr size_t blockTaps = 32 / floatsPerPixel;
			const WeightSpreader<floatsPerPixel> weightSpreader{};
			const AxisWeights::RunLookup xRunLookup = xWeights.runLookup();

			for (size_t dx = destBegin; dx < destEnd; ++dx) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				const auto [firstPixel, weights] = xRunLookup.runFor(dx);
				const size_t tapCount = weights.size();
				const size_t runFloatOffset = source.prepareRun(firstPixel, tapCount);
				const float* srcPixelA = source.floats[0] + runFloatOffset;
				[[maybe_unused]] const float* srcPixelB = source.floats[Rows - 1] + runFloatOffset;

				// Lanes hold partial sums of 8 / floatsPerPixel pixels until the reductions below the blocks
				Floats8 accumPairsA = zeroFloats8();
				[[maybe_unused]] Floats8 accumPairsB = zeroFloats8();
				size_t tap = 0;

				// A run is consecutive pixels, so a block goes through four independent FMA chains of 8 floats;
				// per-tap accumulation into one register would serialize on the FMA latency.
				// Upscaling never enters this branch: a trimmed bicubic run is at most 4 taps.
				if (tapCount >= blockTaps)
				{
					Floats8 accumA0 = zeroFloats8();
					Floats8 accumA1 = zeroFloats8();
					Floats8 accumA2 = zeroFloats8();
					Floats8 accumA3 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB0 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB1 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB2 = zeroFloats8();
					[[maybe_unused]] Floats8 accumB3 = zeroFloats8();

					for (; tap + blockTaps <= tapCount; tap += blockTaps)
					{
						const float* blockPixelsA = srcPixelA + tap * floatsPerPixel;
						[[maybe_unused]] const float* blockPixelsB = srcPixelB + tap * floatsPerPixel;
						const float* blockWeights = weights.data() + tap;
						const WeightBlock firstWeights = loadWeightBlock(blockWeights);

						const Floats8 w0 = chainWeights<0>(weightSpreader, blockWeights, firstWeights);
						accumA0 = mulAdd(loadFloats8(blockPixelsA), w0, accumA0);
						if constexpr (Rows == 2)
							accumB0 = mulAdd(loadFloats8(blockPixelsB), w0, accumB0);

						const Floats8 w1 = chainWeights<1>(weightSpreader, blockWeights, firstWeights);
						accumA1 = mulAdd(loadFloats8(blockPixelsA + 8), w1, accumA1);
						if constexpr (Rows == 2)
							accumB1 = mulAdd(loadFloats8(blockPixelsB + 8), w1, accumB1);

						const Floats8 w2 = chainWeights<2>(weightSpreader, blockWeights, firstWeights);
						accumA2 = mulAdd(loadFloats8(blockPixelsA + 16), w2, accumA2);
						if constexpr (Rows == 2)
							accumB2 = mulAdd(loadFloats8(blockPixelsB + 16), w2, accumB2);

						const Floats8 w3 = chainWeights<3>(weightSpreader, blockWeights, firstWeights);
						accumA3 = mulAdd(loadFloats8(blockPixelsA + 24), w3, accumA3);
						if constexpr (Rows == 2)
							accumB3 = mulAdd(loadFloats8(blockPixelsB + 24), w3, accumB3);
					}

					accumPairsA = add(add(accumA0, accumA1), add(accumA2, accumA3));
					if constexpr (Rows == 2)
						accumPairsB = add(add(accumB0, accumB1), add(accumB2, accumB3));
				}

				// A half block covers a whole bicubic run of 4-float pixels and most of a block remainder
				if (tap + blockTaps / 2 <= tapCount)
				{
					// A whole WeightBlock though only 4 weights may be in play: the builder pads the weights array to keep the overread
					// in bounds. On AVX2 the natural 4-float load + castps128_ps256 compiles under MSVC to a 16-byte stack store that the
					// 32-byte vpermps memory operand then reloads, and a load wider than the store it overlaps cannot be
					// store-forwarded - a ~35-cycle stall, measured to roughly double the upscale pass.
					const float* blockWeights = weights.data() + tap;
					const WeightBlock firstWeights = loadWeightBlock(blockWeights);
					const Floats8 w0 = chainWeights<0>(weightSpreader, blockWeights, firstWeights);
					const Floats8 w1 = chainWeights<1>(weightSpreader, blockWeights, firstWeights);

					const float* blockPixelsA = srcPixelA + tap * floatsPerPixel;
					accumPairsA = mulAdd(loadFloats8(blockPixelsA), w0, mulAdd(loadFloats8(blockPixelsA + 8), w1, accumPairsA));
					if constexpr (Rows == 2)
					{
						const float* blockPixelsB = srcPixelB + tap * floatsPerPixel;
						accumPairsB = mulAdd(loadFloats8(blockPixelsB), w0, mulAdd(loadFloats8(blockPixelsB + 8), w1, accumPairsB));
					}

					tap += blockTaps / 2;
				}

				// A quarter block covers a whole bicubic run of 2-float pixels
				if constexpr (floatsPerPixel <= 2)
				{
					if (tap + blockTaps / 4 <= tapCount)
					{
						const float* blockWeights = weights.data() + tap;
						const Floats8 w0 = chainWeights<0>(weightSpreader, blockWeights, loadWeightBlock(blockWeights));
						accumPairsA = mulAdd(loadFloats8(srcPixelA + tap * floatsPerPixel), w0, accumPairsA);
						if constexpr (Rows == 2)
							accumPairsB = mulAdd(loadFloats8(srcPixelB + tap * floatsPerPixel), w0, accumPairsB);

						tap += blockTaps / 4;
					}
				}

				Floats4 accumA = sumHalves(accumPairsA);
				[[maybe_unused]] Floats4 accumB = zeroFloats4();
				if constexpr (Rows == 2)
					accumB = sumHalves(accumPairsB);

				// An eighth block: 4 taps of 1-float pixels
				if constexpr (floatsPerPixel == 1)
				{
					if (tap + 4 <= tapCount)
					{
						const Floats4 w = loadFloats4(weights.data() + tap);
						accumA = mulAdd(loadFloats4(srcPixelA + tap), w, accumA);
						if constexpr (Rows == 2)
							accumB = mulAdd(loadFloats4(srcPixelB + tap), w, accumB);

						tap += 4;
					}
				}

				if constexpr (floatsPerPixel == 4)
				{
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
				else
				{
					const size_t remainingTaps = tapCount - tap;
					storeTempPixelWithTaps<floatsPerPixel>(tempRows[0] + (dx - destBegin) * Channels, accumA, srcPixelA + tap * floatsPerPixel, weights.data() + tap, remainingTaps);
					if constexpr (Rows == 2)
						storeTempPixelWithTaps<floatsPerPixel>(tempRows[1] + (dx - destBegin) * Channels, accumB, srcPixelB + tap * floatsPerPixel, weights.data() + tap, remainingTaps);
				}
			}
		}

		// filterHorizontalRowGroup for 1-float pixels where no run exceeds 4 taps: four dest columns share one reduction and one store.
		// Requires xWeights.longestRun() <= 4.
		template <size_t PixelStride, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontalShortRuns(
			SlidingSourceFloats<1, PixelStride, Rows>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights) noexcept
		{
			const AxisWeights::RunLookup xRunLookup = xWeights.runLookup();

			for (size_t groupBegin = destBegin; groupBegin < destEnd; groupBegin += 4) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				Floats4 productsA[4];
				[[maybe_unused]] Floats4 productsB[4];
				for (size_t column = 0; column < 4; ++column)
				{
					// A group past the last column repeats it
					const auto [firstPixel, weights] = xRunLookup.runFor(std::min(groupBegin + column, destEnd - 1));
					const size_t tapCount = weights.size();
					assert(tapCount <= 4);
					// 4 floats are loaded whatever the run's length: the ones past it may be unconverted
					const size_t runFloatOffset = source.prepareRun(firstPixel, 4);
					const Floats4 runWeights = loadFloats4(weights.data());
					productsA[column] = mulAdd(keepFirstLanes(loadFloats4(source.floats[0] + runFloatOffset), tapCount), runWeights, zeroFloats4());
					if constexpr (Rows == 2)
						productsB[column] = mulAdd(keepFirstLanes(loadFloats4(source.floats[1] + runFloatOffset), tapCount), runWeights, zeroFloats4());
				}

				const size_t columnCount = std::min<size_t>(4, destEnd - groupBegin);
				Floats4 sums[Rows] = { sumEachOfFour(productsA[0], productsA[1], productsA[2], productsA[3]) };
				if constexpr (Rows == 2)
					sums[1] = sumEachOfFour(productsB[0], productsB[1], productsB[2], productsB[3]);

				for (size_t row = 0; row < Rows; ++row)
				{
					float* const out = tempRows[row] + (groupBegin - destBegin);
					if (columnCount == 4)
						storeFloats4(out, sums[row]);
					else
					{
						alignas(16) float columnSums[4];
						storeFloats4(columnSums, sums[row]);
						::memcpy(out, columnSums, columnCount * sizeof(float));
					}
				}
			}
		}

		// shortRuns: no x run exceeds 4 taps
		template <size_t Channels, size_t PixelStride, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontal(
			SlidingSourceFloats<Channels, PixelStride, Rows>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights,
			[[maybe_unused]] bool shortRuns) noexcept
		{
			if constexpr (Channels == 1)
			{
				if (shortRuns)
				{
					filterHorizontalShortRuns(source, tempRows, destBegin, destEnd, xWeights);
					return;
				}
			}

			filterHorizontalRowGroup(source, tempRows, destBegin, destEnd, xWeights);
		}

		// A block's pixels, color capped at alpha, as bytes packed Channels per pixel
		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void writeBlockBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, [[maybe_unused]] Floats8 values3) noexcept
		{
			if constexpr (Channels == 3)
				writeTwentyFourBytes(dest, values0, values1, values2);
			else if constexpr (hasAlphaChannel(Channels))
				writeThirtyTwoBytes(dest, capColorAtAlpha<Channels>(values0), capColorAtAlpha<Channels>(values1), capColorAtAlpha<Channels>(values2), capColorAtAlpha<Channels>(values3));
			else
				writeThirtyTwoBytes(dest, values0, values1, values2, values3);
		}

		// Writes one destination row from its y tap window. rowWeights covers both segments in order.
		// pixelTail: the bytes past the channels, up to pixelStride, that every dest pixel gets.
		template <size_t Channels, size_t PixelStride>
		IMAGE_PROCESSING_SIMD_INLINE void filterVerticalDestRow(
			const std::array<TempRowSegment, 2>& segments,
			std::span<const float> rowWeights,
			size_t tempRowStride,
			const uint8_t* pixelTail,
			size_t runtimePixelStride,
			uint8_t* destRow,
			size_t destWidth) noexcept
		{
			const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
			constexpr size_t pixelsPerBlock = verticalBlockPixels(Channels);
			constexpr size_t blockFloats = pixelsPerBlock * Channels;
			constexpr size_t elementsPerVector = 8;
			const size_t blockedPixelCount = destWidth & ~(pixelsPerBlock - 1);
			[[maybe_unused]] Rgb32PixelTails pixelTails;
			if constexpr (Channels == 3 && PixelStride == 4)
				pixelTails = rgb32PixelTails(pixelTail[0]);

			size_t pixel = 0;
			for (; pixel < blockedPixelCount; pixel += pixelsPerBlock) IMAGE_PROCESSING_FORCE_INLINE_CALLS
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
							if constexpr (blockFloats == 32)
								accum3 = mulAdd(loadFloats8(source + elementsPerVector * 3), weightVector, accum3);
						}

						source += tempRowStride;
					}
				}

				uint8_t* const blockDest = destRow + pixel * pixelStride;
				if constexpr (Channels == 3 && PixelStride == 4)
					writeEightRgb32Pixels(blockDest, accum0, accum1, accum2, pixelTails);
				else if constexpr (PixelStride == Channels)
					writeBlockBytes<Channels>(blockDest, accum0, accum1, accum2, accum3);
				else
				{
					alignas(16) uint8_t blockBytes[blockFloats];
					writeBlockBytes<Channels>(blockBytes, accum0, accum1, accum2, accum3);
					for (size_t blockPixel = 0; blockPixel < pixelsPerBlock; ++blockPixel)
					{
						uint8_t* const destPixel = blockDest + blockPixel * pixelStride;
						::memcpy(destPixel, blockBytes + blockPixel * Channels, Channels);
						::memcpy(destPixel + Channels, pixelTail, pixelStride - Channels);
					}
				}
			}

			for (; pixel < destWidth; ++pixel) IMAGE_PROCESSING_FORCE_INLINE_CALLS
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
								accum[channel] = mulAdd(source[channel], *weight, accum[channel]);
						}

						source += tempRowStride;
					}
				}

				uint8_t* const destPixel = destRow + pixel * pixelStride;
				writePixelBytes(destPixel, accum.data(), Channels);
				if constexpr (PixelStride != Channels)
					::memcpy(destPixel + Channels, pixelTail, pixelStride - Channels);
			}
		}
	}

	// Fully resizes destination rows [destRowBegin, destRowEnd), one column strip at a time, producing temp rows in pairs into a TempRowRing.
	// The ring is also what lets the pair write two store streams safely: into cold full-size temp, the interleaved streams
	// defeat the prefetch that hides each line's ownership read (measured ~1.2 cycles per temp byte, and software prefetch
	// does not recover it) - the ring is rewritten every few rows and stays cache-owned.
	template <size_t Channels, size_t PixelStride>
	IMAGE_PROCESSING_FLATTEN IMAGE_PROCESSING_SIMD_TARGET void resizeRows(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		size_t stripWidth,
		uint64_t destRowBegin,
		uint64_t destRowEnd)
	{
		static_assert(Channels >= 1 && Channels <= 4);
		assert(destRowBegin < destRowEnd);
		assert(PixelStride == 0 || PixelStride == source.pixelStrideBytes);

		using SourceRowPair = SlidingSourceFloats<Channels, PixelStride, 2>;
		using SourceRow = SlidingSourceFloats<Channels, PixelStride, 1>;
		constexpr size_t floatsPerPixel = SourceRowPair::floatsPerPixel;

		const size_t pixelStride = effectivePixelStride<PixelStride>(source.pixelStrideBytes);
		const size_t destWidth = static_cast<size_t>(dest.width);
		const size_t tempRowStride = stripWidth * Channels;

		const TempRowRing ring{ yWeights, destRowBegin, destRowEnd, tempRowStride, 2 };

		const size_t longestXRun = xWeights.longestRun();
		const bool shortXRuns = longestXRun <= 4;
		const size_t sourceFloatsCapacity = SourceRowPair::capacityFor(longestXRun);
		const auto sourceFloats = std::make_unique_for_overwrite<float[]>(2 * sourceFloatsCapacity * floatsPerPixel);
		float* const sourceFloatsA = sourceFloats.get();
		float* const sourceFloatsB = sourceFloatsA + sourceFloatsCapacity * floatsPerPixel;
		const uint64_t firstPixelOffset = srcRect.left * pixelStride;
		const auto sourcePixels = [&source, srcRect, firstPixelOffset](uint64_t srcRow) noexcept
		{
			return source.scanLine<uint8_t>(srcRect.top + srcRow) + firstPixelOffset;
		};
		const bool premultiplyAlpha = hasStraightAlpha(source);
		// Every dest pixel copies its bytes past the channels from the first source pixel
		const uint8_t* const pixelTail = sourcePixels(0) + Channels;

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
						SourceRowPair sourceRows{ { sourcePixels(produced), sourcePixels(produced + 1) }, { sourceFloatsA, sourceFloatsB }, sourceFloatsCapacity, spanEnd, pixelStride, premultiplyAlpha };
						float* const tempRows[2] = { ring.row(produced), ring.row(produced + 1) };
						filterHorizontal(sourceRows, tempRows, stripBegin, stripEnd, xWeights, shortXRuns);
						produced += 2;
					}
					else
					{
						SourceRow sourceRow{ { sourcePixels(produced) }, { sourceFloatsA }, sourceFloatsCapacity, spanEnd, pixelStride, premultiplyAlpha };
						float* const tempRows[1] = { ring.row(produced) };
						filterHorizontal(sourceRow, tempRows, stripBegin, stripEnd, xWeights, shortXRuns);
						++produced;
					}
				}

				assert(produced - firstWindowRow <= ring.rowCapacity());
				uint8_t* const stripDest = dest.scanLine<uint8_t>(dy) + stripBegin * pixelStride;
				filterVerticalDestRow<Channels, PixelStride>(ring.window(firstWindowRow, rowWeights.size()), rowWeights, tempRowStride, pixelTail, pixelStride, stripDest, stripEnd - stripBegin);
			}
		}

		leaveKernel();
	}

#define IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(Channels, PixelStride) \
	template void resizeRows<Channels, PixelStride>(const ImageView<true>&, Rect, ImageView<false>&, const AxisWeights&, const AxisWeights&, size_t, uint64_t, uint64_t);

	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(1, 1)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(2, 2)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(3, 3)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(3, 4)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(4, 4)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(1, 0)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(2, 0)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(3, 0)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(4, 0)

#undef IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS
}
