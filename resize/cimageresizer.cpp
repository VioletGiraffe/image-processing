#include "cimageresizer.h"
#include "assert/advanced_assert.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string.h>
#include <utility>
#include <vector>

using namespace ImageProcessing;

namespace
{
	struct Tap
	{
		size_t offset;
		float weight;
	};

	struct AxisWeights
	{
		std::vector<Tap> taps;
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
	[[nodiscard]] inline std::vector<AxisWeights> buildAxisWeights(
		uint64_t srcSize,
		uint64_t dstSize,
		OffsetBuilder&& offsetBuilder)
	{
		std::vector<AxisWeights> result;
		result.reserve(dstSize);

		if (srcSize == 1)
		{
			for (uint64_t d = 0; d < dstSize; ++d)
			{
				AxisWeights w;
				w.taps.push_back(Tap{ offsetBuilder(0), 1.0f });
				result.push_back(std::move(w));
			}
			return result;
		}

		const float scale = static_cast<float>(dstSize) / static_cast<float>(srcSize);
		const bool downscale = scale < 1.0f;
		const float support = downscale ? (Kernel::radius / scale) : Kernel::radius;
		const int64_t srcMax = static_cast<int64_t>(srcSize) - 1;

		for (uint64_t d = 0; d < dstSize; ++d)
		{
			AxisWeights w;

			const float srcPos = (static_cast<float>(d) + 0.5f) / scale - 0.5f;
			const int64_t left = static_cast<int64_t>(std::floor(srcPos - support));
			const int64_t right = static_cast<int64_t>(std::ceil(srcPos + support));
			const int64_t count = std::max(1LL, right - left + 1LL);

			w.taps.reserve(static_cast<size_t>(count));

			float sum = 0.0f;

			for (int64_t s = left; s <= right; ++s)
			{
				const int64_t clamped = std::clamp(s, int64_t{ 0 }, srcMax);
				const float distance = srcPos - static_cast<float>(s);

				const float weight = downscale
					? Kernel::evaluate(distance * scale) * scale
					: Kernel::evaluate(distance);

				w.taps.push_back(Tap{ offsetBuilder(static_cast<uint64_t>(clamped)), weight });
				sum += weight;
			}

			if (sum != 0.0f)
			{
				const float invSum = 1.0f / sum;
				for (Tap& tap : w.taps)
					tap.weight *= invSum;
			}
			else
			{
				w.taps.clear();
				w.taps.push_back(Tap{ offsetBuilder(static_cast<uint64_t>(std::clamp(
					static_cast<int64_t>(std::lround(srcPos)),
					int64_t{0},
					srcMax))), 1.0f });
			}

			result.push_back(std::move(w));
		}

		return result;
	}

	[[nodiscard]] inline uint8_t clampToByte(float value) noexcept
	{
		const int64_t rounded = (int64_t)::roundf(value);
		return static_cast<uint8_t>(std::clamp<int64_t>(rounded, 0LL, 255LL));
	}

	inline void copyPixelTail(uint8_t* destPixel, const uint8_t* sourcePixel, size_t channels, size_t pixelStride) noexcept
	{
		// Bytes outside the logical channels can still carry pixel-format invariants, such as RGB32's required 0xff byte.
		assert_debug_only(pixelStride > channels);
		::memcpy(destPixel + channels, sourcePixel + channels, pixelStride - channels);
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
			for (uint64_t y = 0; y < dest.height; ++y)
			{
				const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + y) + srcRect.left * PixelStride;
				auto* dstRow = dest.scanLine<uint8_t>(y);
				::memcpy(dstRow, srcRow, dest.bytesPerLine);
			}
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

		std::vector<float> temp(static_cast<size_t>(srcRect.h) * tempRowStride);

		for (uint64_t sy = 0; sy < srcRect.h; ++sy)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + sy) + srcRect.left * PixelStride;
			float* tempRow = temp.data() + static_cast<size_t>(sy) * tempRowStride;

			for (uint64_t dx = 0; dx < dest.width; ++dx)
			{
				const auto& wx = xWeights[dx];
				float* outPixel = tempRow + static_cast<size_t>(dx) * tempPixelStride;

				if constexpr (Channels == 1)
				{
					float c0 = 0.0f;

					for (const Tap& tap : wx.taps)
					{
						const auto* srcPixel = srcRow + tap.offset;
						c0 += static_cast<float>(srcPixel[0]) * tap.weight;
					}

					outPixel[0] = c0;
				}
				else if constexpr (Channels == 3)
				{
					float c0 = 0.0f;
					float c1 = 0.0f;
					float c2 = 0.0f;

					for (const Tap& tap : wx.taps)
					{
						const auto* srcPixel = srcRow + tap.offset;
						const float weight = tap.weight;

						c0 += static_cast<float>(srcPixel[0]) * weight;
						c1 += static_cast<float>(srcPixel[1]) * weight;
						c2 += static_cast<float>(srcPixel[2]) * weight;
					}

					outPixel[0] = c0;
					outPixel[1] = c1;
					outPixel[2] = c2;
				}
				else if constexpr (Channels == 4)
				{
					float c0 = 0.0f;
					float c1 = 0.0f;
					float c2 = 0.0f;
					float c3 = 0.0f;

					for (const Tap& tap : wx.taps)
					{
						const auto* srcPixel = srcRow + tap.offset;
						const float weight = tap.weight;

						c0 += static_cast<float>(srcPixel[0]) * weight;
						c1 += static_cast<float>(srcPixel[1]) * weight;
						c2 += static_cast<float>(srcPixel[2]) * weight;
						c3 += static_cast<float>(srcPixel[3]) * weight;
					}

					outPixel[0] = c0;
					outPixel[1] = c1;
					outPixel[2] = c2;
					outPixel[3] = c3;
				}
				else
				{
					std::array<float, Channels> accum{};

					for (const Tap& tap : wx.taps)
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

		[[maybe_unused]] const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * PixelStride;
		for (uint64_t dy = 0; dy < dest.height; ++dy)
		{
			auto* dstRow = dest.scanLine<uint8_t>(dy);
			const auto& wy = yWeights[dy];

			for (uint64_t dx = 0; dx < dest.width; ++dx)
			{
				auto* dstPixel = dstRow + static_cast<size_t>(dx) * PixelStride;

				if constexpr (Channels == 1)
				{
					float c0 = 0.0f;

					for (const Tap& tap : wy.taps)
					{
						const auto* tempPixel = temp.data() + tap.offset + static_cast<size_t>(dx) * tempPixelStride;
						c0 += tempPixel[0] * tap.weight;
					}

					dstPixel[0] = clampToByte(c0);
				}
				else if constexpr (Channels == 3)
				{
					float c0 = 0.0f;
					float c1 = 0.0f;
					float c2 = 0.0f;

					for (const Tap& tap : wy.taps)
					{
						const auto* tempPixel = temp.data() + tap.offset + static_cast<size_t>(dx) * tempPixelStride;
						const float weight = tap.weight;

						c0 += tempPixel[0] * weight;
						c1 += tempPixel[1] * weight;
						c2 += tempPixel[2] * weight;
					}

					dstPixel[0] = clampToByte(c0);
					dstPixel[1] = clampToByte(c1);
					dstPixel[2] = clampToByte(c2);
				}
				else if constexpr (Channels == 4)
				{
					float c0 = 0.0f;
					float c1 = 0.0f;
					float c2 = 0.0f;
					float c3 = 0.0f;

					for (const Tap& tap : wy.taps)
					{
						const auto* tempPixel = temp.data() + tap.offset + static_cast<size_t>(dx) * tempPixelStride;
						const float weight = tap.weight;

						c0 += tempPixel[0] * weight;
						c1 += tempPixel[1] * weight;
						c2 += tempPixel[2] * weight;
						c3 += tempPixel[3] * weight;
					}

					dstPixel[0] = clampToByte(c0);
					dstPixel[1] = clampToByte(c1);
					dstPixel[2] = clampToByte(c2);
					dstPixel[3] = clampToByte(c3);
				}
				else
				{
					std::array<float, Channels> accum{};

					for (const Tap& tap : wy.taps)
					{
						const auto* tempPixel = temp.data() + tap.offset + static_cast<size_t>(dx) * tempPixelStride;
						const float weight = tap.weight;

						for (size_t c = 0; c < Channels; ++c)
							accum[c] += tempPixel[c] * weight;
					}

					for (size_t c = 0; c < Channels; ++c)
						dstPixel[c] = clampToByte(accum[c]);
				}

				if constexpr (PixelStride > Channels)
					copyPixelTail(dstPixel, pixelTailSource, Channels, PixelStride);
			}
		}
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
			for (uint64_t y = 0; y < dest.height; ++y)
			{
				const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + y) + srcRect.left * pixelStride;
				auto* dstRow = dest.scanLine<uint8_t>(y);
				::memcpy(dstRow, srcRow, dest.bytesPerLine);
			}
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

		std::vector<float> temp(static_cast<size_t>(srcRect.h) * tempRowStride);

		for (uint64_t ty = 0; ty < srcRect.h; ++ty)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + ty) + srcRect.left * pixelStride;
			float* tempRow = temp.data() + static_cast<size_t>(ty) * tempRowStride;

			for (uint64_t dx = 0; dx < dest.width; ++dx)
			{
				const auto& wx = xWeights[dx];
				float* outPixel = tempRow + static_cast<size_t>(dx) * numChannels;

				for (size_t c = 0; c < numChannels; ++c)
					outPixel[c] = 0.0f;

				for (const Tap& tap : wx.taps)
				{
					const auto* srcPixel = srcRow + tap.offset;
					const float weight = tap.weight;

					for (size_t c = 0; c < numChannels; ++c)
						outPixel[c] += static_cast<float>(srcPixel[c]) * weight;
				}
			}
		}

		alignas(16) float accum[4];
		const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * pixelStride;

		for (uint64_t dy = 0; dy < dest.height; ++dy)
		{
			auto* dstRow = dest.scanLine<uint8_t>(dy);
			const auto& wy = yWeights[dy];

			for (uint64_t dx = 0; dx < dest.width; ++dx)
			{
				accum[0] = 0.0f; accum[1] = 0.0f; accum[2] = 0.0f; accum[3] = 0.0f;

				for (const Tap& tap : wy.taps)
				{
					const auto* tempPixel = temp.data() + tap.offset + static_cast<size_t>(dx) * numChannels;
					const float weight = tap.weight;

					for (size_t c = 0; c < numChannels; ++c)
						accum[c] += tempPixel[c] * weight;
				}

				auto* dstPixel = dstRow + static_cast<size_t>(dx) * pixelStride;
				for (size_t c = 0; c < numChannels; ++c)
					dstPixel[c] = clampToByte(accum[c]);

				if (pixelStride > numChannels)
					copyPixelTail(dstPixel, pixelTailSource, numChannels, pixelStride);
			}
		}
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
