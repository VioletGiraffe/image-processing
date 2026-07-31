#include "cimageresizer.h"
#include "simd_support.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <span>
#include <string.h>
#include <vector>

using namespace ImageProcessing;

namespace
{
	struct Tap
	{
		size_t offset;
		float weight;
	};

	struct TapRange
	{
		size_t firstTap;
		size_t tapCount;
	};

	struct AxisWeights
	{
		[[nodiscard]] std::span<const Tap> tapsFor(size_t coordinate) const noexcept
		{
			const TapRange& range = ranges[coordinate];
			return { taps.data() + range.firstTap, range.tapCount };
		}

		std::vector<Tap> taps;
		std::vector<TapRange> ranges;
	};

	[[nodiscard]] inline float sinc(float x) noexcept
	{
		if (x == 0.0f)
			return 1.0f;

		const float px = std::numbers::pi_v<float> *x;
		return std::sin(px) / px;
	}

	struct BicubicKernel
	{
		static constexpr float radius = 2.0f;

		[[nodiscard]] static inline float evaluate(float x) noexcept
		{
			x = std::abs(x);

			constexpr float a = -0.5f; // Catmull-Rom
			if (x < 1.0f)
				return ((a + 2.0f) * x - (a + 3.0f)) * x * x + 1.0f;

			if (x < 2.0f)
				return (((a * x - 5.0f * a) * x + 8.0f * a) * x - 4.0f * a);

			return 0.0f;
		}
	};

	struct Lanczos3Kernel
	{
		static constexpr float radius = 3.0f;

		[[nodiscard]] static inline float evaluate(float x) noexcept
		{
			x = std::abs(x);

			if (x == 0.0f)
				return 1.0f;

			if (x >= radius)
				return 0.0f;

			return sinc(x) * sinc(x / radius);
		}
	};

	template <class Kernel, class OffsetBuilder>
	[[nodiscard]] inline AxisWeights buildAxisWeights(
		uint64_t srcSize,
		uint64_t dstSize,
		OffsetBuilder&& offsetBuilder)
	{
		AxisWeights result;
		result.ranges.reserve(dstSize);

		if (srcSize == 1)
		{
			result.taps.push_back(Tap{ offsetBuilder(0), 1.0f });
			result.ranges.resize(dstSize, TapRange{ 0, 1 });
			return result;
		}

		result.taps.reserve(dstSize);

		const float scale = static_cast<float>(dstSize) / static_cast<float>(srcSize);
		const bool downscale = scale < 1.0f;
		const float support = downscale ? (Kernel::radius / scale) : Kernel::radius;
		const int64_t srcMax = static_cast<int64_t>(srcSize) - 1;

		for (uint64_t d = 0; d < dstSize; ++d)
		{
			const float srcPos = (static_cast<float>(d) + 0.5f) / scale - 0.5f;
			const int64_t left = static_cast<int64_t>(std::floor(srcPos - support));
			const int64_t right = static_cast<int64_t>(std::ceil(srcPos + support));
			const size_t firstTap = result.taps.size();
			float sum = 0.0f;

			for (int64_t s = left; s <= right; ++s)
			{
				const float distance = srcPos - static_cast<float>(s);

				const float weight = downscale
					? Kernel::evaluate(distance * scale) * scale
					: Kernel::evaluate(distance);

				if (weight == 0.0f)
					continue;

				const int64_t clamped = std::clamp(s, int64_t{ 0 }, srcMax);
				result.taps.push_back(Tap{ offsetBuilder(static_cast<uint64_t>(clamped)), weight });
				sum += weight;
			}

			if (sum != 0.0f)
			{
				const float invSum = 1.0f / sum;
				for (size_t tapIndex = firstTap; tapIndex < result.taps.size(); ++tapIndex)
					result.taps[tapIndex].weight *= invSum;
			}
			else
			{
				result.taps.resize(firstTap);
				result.taps.push_back(Tap{ offsetBuilder(static_cast<uint64_t>(std::clamp(
					static_cast<int64_t>(std::lround(srcPos)),
					int64_t{0},
					srcMax))), 1.0f });
			}

			result.ranges.push_back(TapRange{ firstTap, result.taps.size() - firstTap });
		}

		return result;
	}

	[[nodiscard]] inline uint8_t clampToByte(float value) noexcept
	{
		if (value <= 0.0f)
			return 0;

		if (value >= 255.0f)
			return 255;

		return static_cast<uint8_t>(value + 0.5f);
	}

	inline void copyPixelTail(uint8_t* destPixel, const uint8_t* sourcePixel, size_t channels, size_t pixelStride) noexcept
	{
		// Bytes outside the logical channels can still carry pixel-format invariants, such as RGB32's required 0xff byte.
		assert(pixelStride > channels);
		::memcpy(destPixel + channels, sourcePixel + channels, pixelStride - channels);
	}

	inline void copyUnscaledCrop(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, size_t pixelStride)
	{
		const size_t logicalRowBytes = static_cast<size_t>(dest.width) * pixelStride;
		for (uint64_t y = 0; y < dest.height; ++y)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + y) + srcRect.left * pixelStride;
			auto* dstRow = dest.scanLine<uint8_t>(y);
			::memcpy(dstRow, srcRow, logicalRowBytes);
		}
	}

	template <size_t Channels, size_t PixelStride>
	void filterHorizontalRows(
		float* temp,
		size_t tempRowStride,
		const ImageView<true>& source,
		Rect srcRect,
		uint64_t destWidth,
		const AxisWeights& xWeights)
	{
		for (uint64_t sy = 0; sy < srcRect.h; ++sy)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + sy) + srcRect.left * PixelStride;
			float* tempRow = temp + static_cast<size_t>(sy) * tempRowStride;

			for (uint64_t dx = 0; dx < destWidth; ++dx)
			{
				const auto wx = xWeights.tapsFor(dx);
				float* outPixel = tempRow + static_cast<size_t>(dx) * Channels;
				std::array<float, Channels> accum{};

				for (const Tap& tap : wx)
				{
					const auto* srcPixel = srcRow + tap.offset;
					const float weight = tap.weight;

					for (size_t c = 0; c < Channels; ++c)
						accum[c] += static_cast<float>(srcPixel[c]) * weight;
				}

				for (size_t c = 0; c < Channels; ++c)
					outPixel[c] = accum[c];
			}
		}
	}

	template <class RowWriter>
	void filterVerticalRowsScalar(
		const AxisWeights& yWeights,
		const float* temp,
		size_t rowElementCount,
		uint64_t destHeight,
		RowWriter&& writeRow)
	{
		const auto accumRow = std::make_unique_for_overwrite<float[]>(rowElementCount);

		for (uint64_t dy = 0; dy < destHeight; ++dy)
		{
			std::fill_n(accumRow.get(), rowElementCount, 0.0f);

			for (const Tap& tap : yWeights.tapsFor(dy))
			{
				const float* tempRow = temp + tap.offset;
				const float weight = tap.weight;

				for (size_t element = 0; element < rowElementCount; ++element)
					accumRow[element] += tempRow[element] * weight;
			}

			writeRow(dy, accumRow.get());
		}
	}

#if IMAGE_PROCESSING_SIMD
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void filterHorizontal4BytePixelsSimd(
		float* temp,
		size_t tempRowStride,
		const ImageView<true>& source,
		Rect srcRect,
		uint64_t destWidth,
		const AxisWeights& xWeights)
	{
		static_assert(Channels == 3 || Channels == 4);

		for (uint64_t sy = 0; sy < srcRect.h; ++sy)
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

	template <class RowWriter>
	IMAGE_PROCESSING_SIMD_TARGET void filterVerticalRowsSimd(
		const AxisWeights& yWeights,
		const float* temp,
		size_t rowElementCount,
		uint64_t destHeight,
		RowWriter&& writeRow)
	{
		const auto accumRow = std::make_unique_for_overwrite<float[]>(rowElementCount);
		constexpr size_t elementsPerVector = 8;
		constexpr size_t vectorsPerBlock = 4;
		constexpr size_t elementsPerBlock = elementsPerVector * vectorsPerBlock;
		const size_t blockedElementCount = rowElementCount & ~(elementsPerBlock - 1);
		const size_t vectorizedElementCount = rowElementCount & ~(elementsPerVector - 1);
		const size_t tailElementCount = rowElementCount - vectorizedElementCount;
		const simde__m256i tailMask = simde_mm256_set_epi32(
			tailElementCount > 7 ? -1 : 0,
			tailElementCount > 6 ? -1 : 0,
			tailElementCount > 5 ? -1 : 0,
			tailElementCount > 4 ? -1 : 0,
			tailElementCount > 3 ? -1 : 0,
			tailElementCount > 2 ? -1 : 0,
			tailElementCount > 1 ? -1 : 0,
			tailElementCount > 0 ? -1 : 0);

		for (uint64_t dy = 0; dy < destHeight; ++dy)
		{
			const auto taps = yWeights.tapsFor(dy);
			size_t element = 0;

			for (; element < blockedElementCount; element += elementsPerBlock)
			{
				simde__m256 accum0 = simde_mm256_setzero_ps();
				simde__m256 accum1 = simde_mm256_setzero_ps();
				simde__m256 accum2 = simde_mm256_setzero_ps();
				simde__m256 accum3 = simde_mm256_setzero_ps();

				for (const Tap& tap : taps)
				{
					const float* source = temp + tap.offset + element;
					const simde__m256 weights = simde_mm256_set1_ps(tap.weight);
					accum0 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source), weights, accum0);
					accum1 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector), weights, accum1);
					accum2 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 2), weights, accum2);
					accum3 = simde_mm256_fmadd_ps(simde_mm256_loadu_ps(source + elementsPerVector * 3), weights, accum3);
				}

				simde_mm256_storeu_ps(accumRow.get() + element, accum0);
				simde_mm256_storeu_ps(accumRow.get() + element + elementsPerVector, accum1);
				simde_mm256_storeu_ps(accumRow.get() + element + elementsPerVector * 2, accum2);
				simde_mm256_storeu_ps(accumRow.get() + element + elementsPerVector * 3, accum3);
			}

			for (; element < vectorizedElementCount; element += elementsPerVector)
			{
				simde__m256 accum = simde_mm256_setzero_ps();
				for (const Tap& tap : taps)
				{
					const simde__m256 sourceValues = simde_mm256_loadu_ps(temp + tap.offset + element);
					accum = simde_mm256_fmadd_ps(sourceValues, simde_mm256_set1_ps(tap.weight), accum);
				}
				simde_mm256_storeu_ps(accumRow.get() + element, accum);
			}

			if (tailElementCount != 0)
			{
				simde__m256 accum = simde_mm256_setzero_ps();
				for (const Tap& tap : taps)
				{
					const simde__m256 sourceValues = simde_mm256_maskload_ps(temp + tap.offset + vectorizedElementCount, tailMask);
					accum = simde_mm256_fmadd_ps(sourceValues, simde_mm256_set1_ps(tap.weight), accum);
				}
				simde_mm256_maskstore_ps(accumRow.get() + vectorizedElementCount, tailMask, accum);
			}

			IMAGE_PROCESSING_CLEAR_AVX_UPPER_STATE();
			writeRow(dy, accumRow.get());
		}
	}

	IMAGE_PROCESSING_SIMD_TARGET void writeRgbaRowSimd(uint8_t* dest, const float* source, size_t valueCount) noexcept
	{
		const simde__m256 zero = simde_mm256_setzero_ps();
		const simde__m256 maximum = simde_mm256_set1_ps(255.0f);
		const simde__m256 half = simde_mm256_set1_ps(0.5f);
		const simde__m256i zeroIntegers = simde_mm256_setzero_si256();
		const simde__m256i packedByteOrder = simde_mm256_setr_epi32(0, 4, 1, 1, 1, 1, 1, 1);
		size_t value = 0;

		for (; value + 8 <= valueCount; value += 8)
		{
			simde__m256 values = simde_mm256_loadu_ps(source + value);
			values = simde_mm256_max_ps(zero, simde_mm256_min_ps(maximum, values));
			const simde__m256i integers = simde_mm256_cvttps_epi32(simde_mm256_add_ps(values, half));
			const simde__m256i packed16 = simde_mm256_packus_epi32(integers, zeroIntegers);
			const simde__m256i packed8 = simde_mm256_packus_epi16(packed16, zeroIntegers);
			const simde__m256i contiguousBytes = simde_mm256_permutevar8x32_epi32(packed8, packedByteOrder);
			const uint64_t pixels = static_cast<uint64_t>(simde_mm256_extract_epi64(contiguousBytes, 0));
			::memcpy(dest + value, &pixels, sizeof(pixels));
		}

		IMAGE_PROCESSING_CLEAR_AVX_UPPER_STATE();
		for (; value < valueCount; ++value)
			dest[value] = clampToByte(source[value]);
	}
#endif

	template <size_t Channels, size_t PixelStride>
	void resizeImpl(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect)
	{
		static_assert(Channels >= 1);
		static_assert(PixelStride >= Channels);

		assert(source.width > 0 && source.height > 0);
		assert(dest.width > 0 && dest.height > 0);

		assert(source.channels == Channels);
		assert(dest.channels == Channels);
		assert(source.bytesPerChannel == 1);
		assert(dest.bytesPerChannel == 1);
		assert(source.pixelStrideBytes == PixelStride);
		assert(dest.pixelStrideBytes == PixelStride);

		if (srcRect.w == dest.width && srcRect.h == dest.height)
		{
			copyUnscaledCrop(dest, source, srcRect, PixelStride);
			return;
		}

		const bool scaleUpX = dest.width >= srcRect.w;
		const bool scaleUpY = dest.height >= srcRect.h;

		const size_t tempPixelStride = Channels;
		const size_t tempRowStride = static_cast<size_t>(dest.width) * tempPixelStride;

		const auto xWeights = scaleUpX
			? buildAxisWeights<BicubicKernel>(srcRect.w, dest.width, [](uint64_t sx) noexcept -> size_t
				{
					return static_cast<size_t>(sx) * PixelStride;
				})
			: buildAxisWeights<Lanczos3Kernel>(srcRect.w, dest.width, [](uint64_t sx) noexcept -> size_t
				{
					return static_cast<size_t>(sx) * PixelStride;
				});

		const auto yWeights = scaleUpY
			? buildAxisWeights<BicubicKernel>(srcRect.h, dest.height, [tempRowStride](uint64_t sy) noexcept -> size_t
				{
					return static_cast<size_t>(sy) * tempRowStride;
				})
			: buildAxisWeights<Lanczos3Kernel>(srcRect.h, dest.height, [tempRowStride](uint64_t sy) noexcept -> size_t
				{
					return static_cast<size_t>(sy) * tempRowStride;
				});

		const auto temp = std::make_unique_for_overwrite<float[]>(static_cast<size_t>(srcRect.h) * tempRowStride);

		bool useSimd = false;
#if IMAGE_PROCESSING_SIMD
		if constexpr (PixelStride == 4 && (Channels == 3 || Channels == 4))
		{
			useSimd = SimdSupport::canUseSimd();
			if (useSimd)
				filterHorizontal4BytePixelsSimd<Channels>(temp.get(), tempRowStride, source, srcRect, dest.width, xWeights);
		}
#endif

		if (!useSimd)
			filterHorizontalRows<Channels, PixelStride>(temp.get(), tempRowStride, source, srcRect, dest.width, xWeights);

		[[maybe_unused]] const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * PixelStride;
		auto writeRow = [&dest, pixelTailSource, useSimd](uint64_t dy, const float* accumRow)
			{
				static_cast<void>(useSimd);
				auto* dstRow = dest.scanLine<uint8_t>(dy);

#if IMAGE_PROCESSING_SIMD
				if constexpr (Channels == 4 && PixelStride == 4)
				{
					if (useSimd)
					{
						writeRgbaRowSimd(dstRow, accumRow, static_cast<size_t>(dest.width) * Channels);
						return;
					}
				}
#endif

				for (uint64_t dx = 0; dx < dest.width; ++dx)
				{
					auto* dstPixel = dstRow + static_cast<size_t>(dx) * PixelStride;
					const float* accumPixel = accumRow + static_cast<size_t>(dx) * Channels;

					for (size_t c = 0; c < Channels; ++c)
						dstPixel[c] = clampToByte(accumPixel[c]);

					if constexpr (PixelStride > Channels)
						copyPixelTail(dstPixel, pixelTailSource, Channels, PixelStride);
				}
			};

#if IMAGE_PROCESSING_SIMD
		if (useSimd)
			filterVerticalRowsSimd(yWeights, temp.get(), tempRowStride, dest.height, writeRow);
		else
#endif
			filterVerticalRowsScalar(yWeights, temp.get(), tempRowStride, dest.height, writeRow);
	}

	void resizeImplRuntime(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect)
	{
		assert(source.width > 0 && source.height > 0);
		assert(dest.width > 0 && dest.height > 0);

		assert(source.channels == dest.channels);
		assert(source.bytesPerChannel == 1);
		assert(dest.bytesPerChannel == 1);
		assert(source.pixelStrideBytes == dest.pixelStrideBytes);

		const size_t pixelStride = dest.pixelStrideBytes;

		if (srcRect.w == dest.width && srcRect.h == dest.height)
		{
			copyUnscaledCrop(dest, source, srcRect, pixelStride);
			return;
		}

		const bool scaleUpX = dest.width >= srcRect.w;
		const bool scaleUpY = dest.height >= srcRect.h;
		const size_t numChannels = dest.channels;
		const size_t tempRowStride = static_cast<size_t>(dest.width) * numChannels;

		const auto xWeights = scaleUpX
			? buildAxisWeights<BicubicKernel>(srcRect.w, dest.width, [pixelStride](uint64_t sx) noexcept -> size_t
				{
					return static_cast<size_t>(sx) * pixelStride;
				})
			: buildAxisWeights<Lanczos3Kernel>(srcRect.w, dest.width, [pixelStride](uint64_t sx) noexcept -> size_t
				{
					return static_cast<size_t>(sx) * pixelStride;
				});

		const auto yWeights = scaleUpY
			? buildAxisWeights<BicubicKernel>(srcRect.h, dest.height, [tempRowStride](uint64_t sy) noexcept -> size_t
				{
					return static_cast<size_t>(sy) * tempRowStride;
				})
			: buildAxisWeights<Lanczos3Kernel>(srcRect.h, dest.height, [tempRowStride](uint64_t sy) noexcept -> size_t
				{
					return static_cast<size_t>(sy) * tempRowStride;
				});

		const auto temp = std::make_unique_for_overwrite<float[]>(static_cast<size_t>(srcRect.h) * tempRowStride);

		for (uint64_t ty = 0; ty < srcRect.h; ++ty)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + ty) + srcRect.left * pixelStride;
			float* tempRow = temp.get() + static_cast<size_t>(ty) * tempRowStride;

			for (uint64_t dx = 0; dx < dest.width; ++dx)
			{
				const auto wx = xWeights.tapsFor(dx);
				float* outPixel = tempRow + static_cast<size_t>(dx) * numChannels;

				for (size_t c = 0; c < numChannels; ++c)
					outPixel[c] = 0.0f;

				for (const Tap& tap : wx)
				{
					const auto* srcPixel = srcRow + tap.offset;
					const float weight = tap.weight;

					for (size_t c = 0; c < numChannels; ++c)
						outPixel[c] += static_cast<float>(srcPixel[c]) * weight;
				}
			}
		}

		const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * pixelStride;
		filterVerticalRowsScalar(yWeights, temp.get(), tempRowStride, dest.height,
			[&dest, numChannels, pixelStride, pixelTailSource](uint64_t dy, const float* accumRow)
			{
				auto* dstRow = dest.scanLine<uint8_t>(dy);
				for (uint64_t dx = 0; dx < dest.width; ++dx)
				{
					auto* dstPixel = dstRow + static_cast<size_t>(dx) * pixelStride;
					const float* accumPixel = accumRow + static_cast<size_t>(dx) * numChannels;

					for (size_t c = 0; c < numChannels; ++c)
						dstPixel[c] = clampToByte(accumPixel[c]);

					if (pixelStride > numChannels)
						copyPixelTail(dstPixel, pixelTailSource, numChannels, pixelStride);
				}
			});
	}

	template <size_t Channels>
	inline void resizeDispatchStride(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect)
	{
		switch (source.pixelStrideBytes)
		{
		case 1:
			if constexpr (Channels == 1)
				resizeImpl<1, 1>(dest, source, srcRect);
			else
				resizeImplRuntime(dest, source, srcRect);
			return;

		case 2:
			if constexpr (Channels == 1)
				resizeImpl<1, 2>(dest, source, srcRect);
			else
				resizeImplRuntime(dest, source, srcRect);
			return;

		case 3:
			if constexpr (Channels == 1)
				resizeImpl<1, 3>(dest, source, srcRect);
			else if constexpr (Channels == 3)
				resizeImpl<3, 3>(dest, source, srcRect);
			else
				resizeImplRuntime(dest, source, srcRect);
			return;

		case 4:
			if constexpr (Channels == 1)
				resizeImpl<1, 4>(dest, source, srcRect);
			else if constexpr (Channels == 3)
				resizeImpl<3, 4>(dest, source, srcRect);
			else if constexpr (Channels == 4)
				resizeImpl<4, 4>(dest, source, srcRect);
			else
				resizeImplRuntime(dest, source, srcRect);
			return;

		default:
			resizeImplRuntime(dest, source, srcRect);
			return;
		}
	}
}

void ImageProcessing::resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect)
{
	assert(source.width > 0 && source.height > 0);
	assert(dest.width > 0 && dest.height > 0);

	assert(source.channels == dest.channels);
	assert(source.bytesPerChannel == dest.bytesPerChannel);
	assert(source.pixelStrideBytes == dest.pixelStrideBytes);

	if (source.channels > 4 || dest.channels > 4)
	{
		assert(false && "Images with more than four channels are unsupported");
		return;
	}

	if (source.bytesPerChannel != 1)
	{
		assert(false && "Only one-byte image channels are supported");
		return;
	}

	if (srcRect.w == 0 || srcRect.h == 0)
		srcRect = Rect{ 0, 0, source.width, source.height };
	else
	{
		// Preserve the requested size where possible, shifting each axis independently to keep the rectangle inside the source.
		srcRect.w = std::min(srcRect.w, source.width);
		srcRect.h = std::min(srcRect.h, source.height);
		srcRect.left = std::min(srcRect.left, source.width - srcRect.w);
		srcRect.top = std::min(srcRect.top, source.height - srcRect.h);
	}

	switch (source.channels)
	{
	case 1: resizeDispatchStride<1>(dest, source, srcRect); return;
	case 3: resizeDispatchStride<3>(dest, source, srcRect); return;
	case 4: resizeDispatchStride<4>(dest, source, srcRect); return;
	default: resizeImplRuntime(dest, source, srcRect); return;
	}
}
