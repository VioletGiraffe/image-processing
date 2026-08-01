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
			const simde__m256 zero = simde_mm256_setzero_ps();
			values = simde_mm256_max_ps(zero, simde_mm256_min_ps(simde_mm256_set1_ps(255.0f), values));
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

		for (uint64_t sy = rowBegin; sy < rowEnd; ++sy)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + sy) + srcRect.left * 4;
			float* tempRow = temp + static_cast<size_t>(sy) * tempRowStride;

			for (uint64_t dx = 0; dx < destWidth; ++dx)
			{
				simde__m128 accum = simde_mm_setzero_ps();

				for (const Tap& tap : xWeights.tapsFor(dx))
				{
					int32_t packedPixel;
					::memcpy(&packedPixel, srcRow + tap.offset, sizeof(packedPixel));
					const simde__m128i bytes = simde_mm_cvtsi32_si128(packedPixel);
					const simde__m128 channels = simde_mm_cvtepi32_ps(simde_mm_cvtepu8_epi32(bytes));
					accum = simde_mm_fmadd_ps(channels, simde_mm_set1_ps(tap.weight), accum);
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

		IMAGE_PROCESSING_CLEAR_AVX_UPPER_STATE();
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
		const size_t blockedPixelCount = destWidth & ~(pixelsPerBlock - 1);
		[[maybe_unused]] simde__m128i pixelTails;
		if constexpr (Channels == 3)
		{
			const int32_t packedPixelTail = std::bit_cast<int32_t>(static_cast<uint32_t>(pixelTailValue) << 24);
			pixelTails = simde_mm_set1_epi32(packedPixelTail);
		}

		for (uint64_t dy = rowBegin; dy < rowEnd; ++dy)
		{
			const auto taps = yWeights.tapsFor(dy);
			uint8_t* destRow = dest.scanLine<uint8_t>(dy);
			size_t pixel = 0;

			for (; pixel < blockedPixelCount; pixel += pixelsPerBlock)
			{
				simde__m256 accum0 = simde_mm256_setzero_ps();
				simde__m256 accum1 = simde_mm256_setzero_ps();
				simde__m256 accum2 = simde_mm256_setzero_ps();
				[[maybe_unused]] simde__m256 accum3 = simde_mm256_setzero_ps();
				const size_t element = pixel * Channels;

				for (const Tap& tap : taps)
				{
					const float* source = temp + tap.offset + element;
					const simde__m256 weights = simde_mm256_set1_ps(tap.weight);
					accum0 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source), weights, accum0);
					accum1 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector), weights, accum1);
					accum2 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 2), weights, accum2);
					if constexpr (Channels == 4)
						accum3 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 3), weights, accum3);
				}

				if constexpr (Channels == 3)
					writeEightRgb32Pixels(destRow + pixel * 4, accum0, accum1, accum2, pixelTails);
				else
					writeEightRgbaPixels(destRow + pixel * 4, accum0, accum1, accum2, accum3);
			}

			for (; pixel < destWidth; ++pixel)
			{
				std::array<float, Channels> accum{};
				for (const Tap& tap : taps)
				{
					const float* source = temp + tap.offset + pixel * Channels;
					for (size_t channel = 0; channel < Channels; ++channel)
						accum[channel] = std::fma(source[channel], tap.weight, accum[channel]);
				}

				uint8_t* destPixel = destRow + pixel * 4;
				for (size_t channel = 0; channel < Channels; ++channel)
					destPixel[channel] = clampToByte(accum[channel]);

				if constexpr (Channels == 3)
					destPixel[3] = pixelTailValue;
			}
		}

		IMAGE_PROCESSING_CLEAR_AVX_UPPER_STATE();
	}

	template void filterHorizontal4BytePixelsSimd<3>(float*, size_t, const ImageView<true>&, Rect, uint64_t, const AxisWeights&, uint64_t, uint64_t);
	template void filterHorizontal4BytePixelsSimd<4>(float*, size_t, const ImageView<true>&, Rect, uint64_t, const AxisWeights&, uint64_t, uint64_t);
	template void filterVerticalRowsSimd<3>(const AxisWeights&, const float*, ImageView<false>&, uint8_t, uint64_t, uint64_t);
	template void filterVerticalRowsSimd<4>(const AxisWeights&, const float*, ImageView<false>&, uint8_t, uint64_t, uint64_t);
}

#endif
