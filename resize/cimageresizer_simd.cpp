#include "resize_internal.h"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string.h>

#if IMAGE_PROCESSING_SIMD

namespace ImageProcessing::Detail
{
	namespace
	{
		IMAGE_PROCESSING_SIMD_INLINE simde__m128i packEightFloatsToBytes(simde__m256 values) noexcept
		{
			// packus_epi32 saturates negatives to 0; the 255 cap must stay - values past 32767 would wrap negative through the signed-input packus_epi16
			values = simde_mm256_min_ps(simde_mm256_set1_ps(255.0f), values);
			const simde__m256i integers = simde_mm256_cvttps_epi32(simde_mm256_add_ps(values, simde_mm256_set1_ps(0.5f)));
			const simde__m256i zeroIntegers = simde_mm256_setzero_si256();
			const simde__m256i packed16 = simde_mm256_packus_epi32(integers, zeroIntegers);
			const simde__m256i packed8 = simde_mm256_packus_epi16(packed16, zeroIntegers);
			const simde__m256i contiguousBytes = simde_mm256_permutevar8x32_epi32(
				packed8,
				simde_mm256_setr_epi32(0, 4, 1, 1, 1, 1, 1, 1));
			return simde_mm256_castsi256_si128(contiguousBytes);
		}

		IMAGE_PROCESSING_SIMD_INLINE void writeEightRgbaPixels(
			uint8_t* dest,
			simde__m256 values0,
			simde__m256 values1,
			simde__m256 values2,
			simde__m256 values3) noexcept
		{
			const simde__m128i bytes0 = packEightFloatsToBytes(values0);
			const simde__m128i bytes1 = packEightFloatsToBytes(values1);
			const simde__m128i bytes2 = packEightFloatsToBytes(values2);
			const simde__m128i bytes3 = packEightFloatsToBytes(values3);
			simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(dest), simde_mm_unpacklo_epi64(bytes0, bytes1));
			simde_mm_storeu_si128(reinterpret_cast<simde__m128i*>(dest + 16), simde_mm_unpacklo_epi64(bytes2, bytes3));
		}

		IMAGE_PROCESSING_SIMD_INLINE void writeEightRgb32Pixels(
			uint8_t* dest,
			simde__m256 values0,
			simde__m256 values1,
			simde__m256 values2,
			simde__m128i pixelTails) noexcept
		{
			const simde__m128i bytes0 = packEightFloatsToBytes(values0);
			const simde__m128i bytes1 = packEightFloatsToBytes(values1);
			const simde__m128i bytes2 = packEightFloatsToBytes(values2);
			const simde__m128i firstSixteenRgbBytes = simde_mm_unpacklo_epi64(bytes0, bytes1);
			const simde__m128i lastTwelveRgbBytes = simde_mm_alignr_epi8(bytes2, firstSixteenRgbBytes, 12);
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

		// 8 bytes = two adjacent 4-byte pixels, zero-extended: lanes 0-3 hold the first pixel's channels, 4-7 the second's
		IMAGE_PROCESSING_SIMD_INLINE simde__m256 loadTwoPixelsAsFloats(const uint8_t* pixels) noexcept
		{
			const simde__m128i bytes = simde_mm_loadl_epi64(reinterpret_cast<const simde__m128i*>(pixels));
			return simde_mm256_cvtepi32_ps(simde_mm256_cvtepu8_epi32(bytes));
		}
	}

	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void filterHorizontal4BytePixelsSimd(
		float* temp,
		size_t tempRowStride,
		const ImageView<true>& source,
		Rect srcRect,
		uint64_t destWidth,
		const AxisWeights& xWeights,
		uint64_t rowBegin,
		uint64_t rowEnd)
	{
		static_assert(Channels == 3 || Channels == 4);

		// Spread patterns turning 8 consecutive weights into the pair-broadcast [w(i) x4 | w(i+1) x4] each pixel pair needs
		const simde__m256i weightSpread0 = simde_mm256_setr_epi32(0, 0, 0, 0, 1, 1, 1, 1);
		const simde__m256i weightSpread1 = simde_mm256_setr_epi32(2, 2, 2, 2, 3, 3, 3, 3);
		const simde__m256i weightSpread2 = simde_mm256_setr_epi32(4, 4, 4, 4, 5, 5, 5, 5);
		const simde__m256i weightSpread3 = simde_mm256_setr_epi32(6, 6, 6, 6, 7, 7, 7, 7);

		for (uint64_t sy = rowBegin; sy < rowEnd; ++sy)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + sy) + srcRect.left * 4;
			float* tempRow = temp + static_cast<size_t>(sy) * tempRowStride;

			for (uint64_t dx = 0; dx < destWidth; ++dx)
			{
				const auto [srcStartOffset, weights] = xWeights.runFor(dx);
				const uint8_t* srcPixel = srcRow + srcStartOffset;
				const size_t tapCount = weights.size();

				// Lanes hold [even pixel | odd pixel] partial sums until the single reduction below the blocks
				simde__m256 accumPairs = simde_mm256_setzero_ps();
				size_t tap = 0;

				// A run is consecutive pixels, so 8 taps go through four independent FMA chains at two pixels
				// per register; per-tap accumulation into one register would serialize on the FMA latency.
				// Upscaling never enters this branch: a trimmed bicubic run is at most 4 taps.
				if (tapCount >= 8)
				{
					simde__m256 accum0 = simde_mm256_setzero_ps();
					simde__m256 accum1 = simde_mm256_setzero_ps();
					simde__m256 accum2 = simde_mm256_setzero_ps();
					simde__m256 accum3 = simde_mm256_setzero_ps();

					for (; tap + 8 <= tapCount; tap += 8)
					{
						const uint8_t* blockPixels = srcPixel + tap * 4;
						const simde__m256 blockWeights = simde_mm256_loadu_ps(weights.data() + tap);
						accum0 = simde_mm256_fmadd_ps(loadTwoPixelsAsFloats(blockPixels),
							simde_mm256_permutevar8x32_ps(blockWeights, weightSpread0), accum0);
						accum1 = simde_mm256_fmadd_ps(loadTwoPixelsAsFloats(blockPixels + 8),
							simde_mm256_permutevar8x32_ps(blockWeights, weightSpread1), accum1);
						accum2 = simde_mm256_fmadd_ps(loadTwoPixelsAsFloats(blockPixels + 16),
							simde_mm256_permutevar8x32_ps(blockWeights, weightSpread2), accum2);
						accum3 = simde_mm256_fmadd_ps(loadTwoPixelsAsFloats(blockPixels + 24),
							simde_mm256_permutevar8x32_ps(blockWeights, weightSpread3), accum3);
					}

					accumPairs = simde_mm256_add_ps(simde_mm256_add_ps(accum0, accum1), simde_mm256_add_ps(accum2, accum3));
				}

				// One narrower block covers a whole bicubic run and most of an 8-block remainder
				if (tap + 4 <= tapCount)
				{
					const simde__m128i pixelBytes = simde_mm_loadu_si128(reinterpret_cast<const simde__m128i*>(srcPixel + tap * 4));
					const simde__m256 pixels01 = simde_mm256_cvtepi32_ps(simde_mm256_cvtepu8_epi32(pixelBytes));
					const simde__m256 pixels23 = simde_mm256_cvtepi32_ps(simde_mm256_cvtepu8_epi32(simde_mm_unpackhi_epi64(pixelBytes, pixelBytes)));
					// 8-float load though only 4 are in play: the spread indices never select the upper lanes, and
					// the builder pads the weights array to keep the overread in bounds. The natural 4-float load
					// + castps128_ps256 compiles under MSVC to a 16-byte stack store that the 32-byte vpermps
					// memory operand then reloads, and a load wider than the store it overlaps cannot be
					// store-forwarded - a ~35-cycle stall, measured to roughly double the upscale pass.
					const simde__m256 blockWeights = simde_mm256_loadu_ps(weights.data() + tap);
					accumPairs = simde_mm256_fmadd_ps(pixels01, simde_mm256_permutevar8x32_ps(blockWeights, weightSpread0),
						simde_mm256_fmadd_ps(pixels23, simde_mm256_permutevar8x32_ps(blockWeights, weightSpread1), accumPairs));
					tap += 4;
				}

				simde__m128 accum = simde_mm_add_ps(simde_mm256_castps256_ps128(accumPairs), simde_mm256_extractf128_ps(accumPairs, 1));

				for (; tap < tapCount; ++tap)
				{
					int32_t packedPixel;
					::memcpy(&packedPixel, srcPixel + tap * 4, sizeof(packedPixel));
					const simde__m128i bytes = simde_mm_cvtsi32_si128(packedPixel);
					const simde__m128 channels = simde_mm_cvtepi32_ps(simde_mm_cvtepu8_epi32(bytes));
					accum = simde_mm_fmadd_ps(channels, simde_mm_set1_ps(weights[tap]), accum);
				}

				float* outPixel = tempRow + static_cast<size_t>(dx) * Channels;
				if constexpr (Channels == 4)
					simde_mm_storeu_ps(outPixel, accum);
				else
				{
					alignas(16) float channels[4];
					simde_mm_store_ps(channels, accum);
					::memcpy(outPixel, channels, Channels * sizeof(float));
				}
			}
		}

		SimdSupport::clearAvxUpperState();
	}

	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void filterVerticalRowsSimd(
		const AxisWeights& yWeights,
		const float* temp,
		ImageView<false>& dest,
		[[maybe_unused]] uint8_t pixelTailValue,
		uint64_t rowBegin,
		uint64_t rowEnd)
	{
		static_assert(Channels == 3 || Channels == 4);
		constexpr size_t pixelsPerBlock = 8;
		constexpr size_t elementsPerVector = 8;
		const size_t destWidth = static_cast<size_t>(dest.width);
		// Temp rows are dense: resizeImpl's tempRowStride is the same product
		const size_t tempRowStride = destWidth * Channels;
		const size_t blockedPixelCount = destWidth & ~(pixelsPerBlock - 1);
		[[maybe_unused]] simde__m128i pixelTails;
		if constexpr (Channels == 3)
		{
			const int32_t packedPixelTail = std::bit_cast<int32_t>(static_cast<uint32_t>(pixelTailValue) << 24);
			pixelTails = simde_mm_set1_epi32(packedPixelTail);
		}

		for (uint64_t dy = rowBegin; dy < rowEnd; ++dy)
		{
			const auto [tempStartOffset, rowWeights] = yWeights.runFor(dy);
			uint8_t* destRow = dest.scanLine<uint8_t>(dy);
			size_t pixel = 0;

			for (; pixel < blockedPixelCount; pixel += pixelsPerBlock)
			{
				simde__m256 accum0 = simde_mm256_setzero_ps();
				simde__m256 accum1 = simde_mm256_setzero_ps();
				simde__m256 accum2 = simde_mm256_setzero_ps();
				[[maybe_unused]] simde__m256 accum3 = simde_mm256_setzero_ps();
				const float* source = temp + tempStartOffset + pixel * Channels;

				// A zero tap costs a whole row sweep, and exact-ratio downscales produce them
				// (the kernels are zero at integer offsets)
				for (const float weight : rowWeights)
				{
					if (weight != 0.0f)
					{
						const simde__m256 weightVector = simde_mm256_set1_ps(weight);
						accum0 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source), weightVector, accum0);
						accum1 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector), weightVector, accum1);
						accum2 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 2), weightVector, accum2);
						if constexpr (Channels == 4)
							accum3 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 3), weightVector, accum3);
					}

					source += tempRowStride;
				}

				if constexpr (Channels == 3)
					writeEightRgb32Pixels(destRow + pixel * 4, accum0, accum1, accum2, pixelTails);
				else
					writeEightRgbaPixels(destRow + pixel * 4, accum0, accum1, accum2, accum3);
			}

			for (; pixel < destWidth; ++pixel)
			{
				std::array<float, Channels> accum{};
				const float* source = temp + tempStartOffset + pixel * Channels;
				for (const float weight : rowWeights)
				{
					if (weight != 0.0f)
					{
						for (size_t channel = 0; channel < Channels; ++channel)
							accum[channel] = std::fma(source[channel], weight, accum[channel]);
					}

					source += tempRowStride;
				}

				uint8_t* destPixel = destRow + pixel * 4;
				for (size_t channel = 0; channel < Channels; ++channel)
					destPixel[channel] = clampToByte(accum[channel]);

				if constexpr (Channels == 3)
					destPixel[3] = pixelTailValue;
			}
		}

		SimdSupport::clearAvxUpperState();
	}

	template void filterHorizontal4BytePixelsSimd<3>(float*, size_t, const ImageView<true>&, Rect, uint64_t, const AxisWeights&, uint64_t, uint64_t);
	template void filterHorizontal4BytePixelsSimd<4>(float*, size_t, const ImageView<true>&, Rect, uint64_t, const AxisWeights&, uint64_t, uint64_t);
	template void filterVerticalRowsSimd<3>(const AxisWeights&, const float*, ImageView<false>&, uint8_t, uint64_t, uint64_t);
	template void filterVerticalRowsSimd<4>(const AxisWeights&, const float*, ImageView<false>&, uint8_t, uint64_t, uint64_t);
}

#endif
