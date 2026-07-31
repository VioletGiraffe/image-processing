#include "cimageresizer.h"
#include "assert/advanced_assert.h"

#include <algorithm>
#include <array>
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
		assert_debug_only(pixelStride > channels);
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

	template <class RowWriter>
	void filterVerticalRows(
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

	template <size_t Channels, size_t PixelStride>
	void resizeImpl(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect)
	{
		static_assert(Channels >= 1);
		static_assert(PixelStride >= Channels);

		assert_debug_only(source.width > 0 && source.height > 0);
		assert_debug_only(dest.width > 0 && dest.height > 0);

		assert_debug_only(source.channels == Channels);
		assert_debug_only(dest.channels == Channels);
		assert_debug_only(source.bytesPerChannel == 1);
		assert_debug_only(dest.bytesPerChannel == 1);
		assert_debug_only(source.pixelStrideBytes == PixelStride);
		assert_debug_only(dest.pixelStrideBytes == PixelStride);

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

		for (uint64_t sy = 0; sy < srcRect.h; ++sy)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + sy) + srcRect.left * PixelStride;
			float* tempRow = temp.get() + static_cast<size_t>(sy) * tempRowStride;

			for (uint64_t dx = 0; dx < dest.width; ++dx)
			{
				const auto wx = xWeights.tapsFor(dx);
				float* outPixel = tempRow + static_cast<size_t>(dx) * tempPixelStride;
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

		[[maybe_unused]] const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * PixelStride;
		filterVerticalRows(yWeights, temp.get(), tempRowStride, dest.height,
			[&dest, pixelTailSource](uint64_t dy, const float* accumRow)
			{
				auto* dstRow = dest.scanLine<uint8_t>(dy);
				for (uint64_t dx = 0; dx < dest.width; ++dx)
				{
					auto* dstPixel = dstRow + static_cast<size_t>(dx) * PixelStride;
					const float* accumPixel = accumRow + static_cast<size_t>(dx) * Channels;

					for (size_t c = 0; c < Channels; ++c)
						dstPixel[c] = clampToByte(accumPixel[c]);

					if constexpr (PixelStride > Channels)
						copyPixelTail(dstPixel, pixelTailSource, Channels, PixelStride);
				}
			});
	}

	void resizeImplRuntime(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect)
	{
		assert_debug_only(source.width > 0 && source.height > 0);
		assert_debug_only(dest.width > 0 && dest.height > 0);

		assert_debug_only(source.channels == dest.channels);
		assert_debug_only(source.bytesPerChannel == 1);
		assert_debug_only(dest.bytesPerChannel == 1);
		assert_debug_only(source.pixelStrideBytes == dest.pixelStrideBytes);

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
		filterVerticalRows(yWeights, temp.get(), tempRowStride, dest.height,
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
	assert_debug_only(source.width > 0 && source.height > 0);
	assert_debug_only(dest.width > 0 && dest.height > 0);

	assert_debug_only(source.channels == dest.channels);
	assert_debug_only(source.bytesPerChannel == dest.bytesPerChannel);
	assert_debug_only(source.pixelStrideBytes == dest.pixelStrideBytes);

	assert_and_return_r(source.channels <= 4 && dest.channels <= 4, );
	assert_and_return_r(source.bytesPerChannel == 1, );

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
