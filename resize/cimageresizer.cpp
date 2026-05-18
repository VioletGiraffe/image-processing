#include "cimageresizer.h"
#include "assert/advanced_assert.h"
#include "math/math.hpp"

#include <math.h>
#include <numbers>
#include <stdint.h>
#include <vector>

using namespace ImageProcessing;

namespace
{
	struct AxisWeights
	{
		std::vector<int> indices;
		std::vector<float> weights;
	};

	[[nodiscard]] inline float sinc(float x) noexcept
	{
		if (x == 0.0f)
			return 1.0f;

		const float px = std::numbers::pi_v<float> * x;
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

	template <class Kernel>
	[[nodiscard]] inline std::vector<AxisWeights> buildAxisWeights(uint32_t srcSize, uint32_t dstSize)
	{
		std::vector<AxisWeights> result;
		result.reserve(dstSize);

		if (srcSize == 1)
		{
			for (uint32_t d = 0; d < dstSize; ++d)
			{
				AxisWeights w;
				w.indices.push_back(0);
				w.weights.push_back(1.0f);
				result.push_back(std::move(w));
			}
			return result;
		}

		const float scale = static_cast<float>(dstSize) / static_cast<float>(srcSize);
		const bool downscale = scale < 1.0f;
		const float support = downscale ? (Kernel::radius / scale) : Kernel::radius;
		const int srcMax = static_cast<int>(srcSize) - 1;

		for (uint32_t d = 0; d < dstSize; ++d)
		{
			AxisWeights w;

			const float srcPos = (static_cast<float>(d) + 0.5f) / scale - 0.5f;
			const int left = static_cast<int>(std::floor(srcPos - support));
			const int right = static_cast<int>(std::ceil(srcPos + support));
			const int count = std::max(1, right - left + 1);

			w.indices.reserve(static_cast<size_t>(count));
			w.weights.reserve(static_cast<size_t>(count));

			float sum = 0.0f;

			for (int s = left; s <= right; ++s)
			{
				const int clamped = std::clamp(s, 0, srcMax);
				const float distance = srcPos - static_cast<float>(s);

				const float weight = downscale
					? Kernel::evaluate(distance * scale) * scale
					: Kernel::evaluate(distance);

				w.indices.push_back(clamped);
				w.weights.push_back(weight);
				sum += weight;
			}

			if (sum != 0.0f)
			{
				const float invSum = 1.0f / sum;
				for (float& weight : w.weights)
					weight *= invSum;
			}
			else
			{
				w.indices.clear();
				w.weights.clear();
				w.indices.push_back(std::clamp(static_cast<int>(std::lround(srcPos)), 0, srcMax));
				w.weights.push_back(1.0f);
			}

			result.push_back(std::move(w));
		}

		return result;
	}

	[[nodiscard]] inline uint8_t clampToByte(float value) noexcept
	{
		const int rounded = (int)::roundf(value);
		return static_cast<uint8_t>(std::clamp<long>(rounded, 0, 255));
	}
}

void ImageProcessing::resize(ImageView<false>& dest, const ImageView<true>& source)
{
	assert_debug_only(source.width > 0 && source.height > 0);
	assert_debug_only(dest.width > 0 && dest.height > 0);

	assert_debug_only(source.channels == dest.channels);
	assert_debug_only(source.bytesPerChannel == dest.bytesPerChannel);
	assert_debug_only(source.channelStride == dest.channelStride);

	assert_and_return_r(source.bytesPerChannel == 1, );

	if (source.width == dest.width && source.height == dest.height) // Just copy the image
	{
		for (uint32_t y = 0; y < dest.height; ++y)
		{
			const auto* srcRow = source.scanLine<uint8_t>(y);
			auto* dstRow = dest.scanLine<uint8_t>(y);

			::memcpy(dstRow, srcRow, dest.bytesPerLine);
		}
		return;
	}

	const bool scaleUpX = dest.width >= source.width;
	const bool scaleUpY = dest.height >= source.height;

	const auto xWeights = scaleUpX
		? buildAxisWeights<BicubicKernel>(source.width, dest.width)
		: buildAxisWeights<Lanczos3Kernel>(source.width, dest.width);

	const auto yWeights = scaleUpY
		? buildAxisWeights<BicubicKernel>(source.height, dest.height)
		: buildAxisWeights<Lanczos3Kernel>(source.height, dest.height);

	const size_t pixelStride = dest.channelStride;
	const size_t numChannels = dest.channels;

	std::vector<float> temp(
		static_cast<size_t>(source.height) * static_cast<size_t>(dest.width) * pixelStride);

	for (uint32_t sy = 0; sy < source.height; ++sy)
	{
		const auto* srcRow = source.scanLine<uint8_t>(sy);
		float* tempRow = temp.data() + static_cast<size_t>(sy) * dest.width * pixelStride;

		for (uint32_t dx = 0; dx < dest.width; ++dx)
		{
			const auto& wx = xWeights[dx];
			float* outPixel = tempRow + static_cast<size_t>(dx) * pixelStride;

			for (size_t c = 0; c < numChannels; ++c)
				outPixel[c] = 0.0f;

			if (dest.channels == 3) [[likely]]
			{
				for (size_t t = 0; t < wx.indices.size(); ++t)
				{
					const auto* srcPixel = srcRow + static_cast<size_t>(wx.indices[t]) * pixelStride;
					const float weight = wx.weights[t];

					outPixel[0] += static_cast<float>(srcPixel[0]) * weight;
					outPixel[1] += static_cast<float>(srcPixel[1]) * weight;
					outPixel[2] += static_cast<float>(srcPixel[2]) * weight;
				}
			}
			else
			{
				for (size_t t = 0; t < wx.indices.size(); ++t)
				{
					const auto* srcPixel = srcRow + static_cast<size_t>(wx.indices[t]) * pixelStride;
					const float weight = wx.weights[t];

					for (size_t c = 0; c < numChannels; ++c)
						outPixel[c] += static_cast<float>(srcPixel[c]) * weight;
				}
			}
		}
	}

	float accum[4];

	for (uint32_t dy = 0; dy < dest.height; ++dy)
	{
		auto* dstRow = dest.scanLine<uint8_t>(dy);
		const auto& wy = yWeights[dy];

		for (uint32_t dx = 0; dx < dest.width; ++dx)
		{
			for (size_t c = 0; c < 4; ++c)
				accum[c] = 0.0f;

			if (dest.channels == 3) [[likely]]
			{
				for (size_t t = 0; t < wy.indices.size(); ++t)
				{
					const float* tempPixel = temp.data()
						+ (static_cast<size_t>(wy.indices[t]) * dest.width + dx) * pixelStride;
					const float weight = wy.weights[t];

					accum[0] += tempPixel[0] * weight;
					accum[1] += tempPixel[1] * weight;
					accum[2] += tempPixel[2] * weight;
				}
			}
			else
			{
				for (size_t t = 0; t < wy.indices.size(); ++t)
				{
					const float* tempPixel = temp.data()
						+ (static_cast<size_t>(wy.indices[t]) * dest.width + dx) * pixelStride;
					const float weight = wy.weights[t];

					for (size_t c = 0; c < numChannels; ++c)
						accum[c] += tempPixel[c] * weight;
				}
			}

			uint8_t* dstPixel = dstRow + static_cast<size_t>(dx) * pixelStride;
			for (size_t c = 0; c < numChannels; ++c)
				dstPixel[c] = clampToByte(accum[c]);
		}
	}
}