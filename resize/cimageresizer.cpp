#include "resize_internal.h"

#include "threading/cworkerthread.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string.h>
#include <vector>

using namespace ImageProcessing;
using namespace ImageProcessing::Detail;

namespace
{
	// Runs worker(rowBegin, rowEnd) over [0, rowCount) split into contiguous bands: on the pool when the total
	// work justifies the dispatch overhead, serially otherwise (or when no pool is given). Blocks until done.
	template <class Worker>
	void forEachRowBand(CWorkerThreadPool* threadPool, uint64_t rowCount, size_t elementsPerRow, Worker&& worker)
	{
		constexpr uint64_t minElementsPerBand = 32 * 1024;
		// The band count bounds the concurrency (helpers + the calling thread never outnumber the bands)
		constexpr uint64_t maxExecutors = 4;
		uint64_t bandCount = 1;
		if (threadPool)
			bandCount = std::min({ rowCount, rowCount * elementsPerRow / minElementsPerBand, maxExecutors });

		if (bandCount <= 1)
		{
			worker(uint64_t{ 0 }, rowCount);
			return;
		}

		threadPool->parallelFor(bandCount, [&](size_t band)
			{
				worker(rowCount * band / bandCount, rowCount * (band + 1) / bandCount);
			});
	}

	// The tap tables are built in double and stored as float. A float source position loses its sub-pixel phase as
	// the destination coordinate grows - one ulp at 4K width is already 1.2e-4 - and the kernel slope turns that
	// straight into weight error. Cost is nil: this runs once per axis, not per pixel.
	[[nodiscard]] inline double sinc(double x) noexcept
	{
		if (x == 0.0)
			return 1.0;

		const double px = std::numbers::pi * x;
		return std::sin(px) / px;
	}

	struct BicubicKernel
	{
		static constexpr double radius = 2.0;

		[[nodiscard]] static inline double evaluate(double x) noexcept
		{
			x = std::abs(x);

			constexpr double a = -0.5; // Catmull-Rom
			if (x < 1.0)
				return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;

			if (x < 2.0)
				return (((a * x - 5.0 * a) * x + 8.0 * a) * x - 4.0 * a);

			return 0.0;
		}
	};

	struct Lanczos3Kernel
	{
		static constexpr double radius = 3.0;

		[[nodiscard]] static inline double evaluate(double x) noexcept
		{
			x = std::abs(x);

			if (x == 0.0)
				return 1.0;

			if (x >= radius)
				return 0.0;

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

		if (srcSize == 1) [[unlikely]]
		{
			result.taps.push_back(Tap{ offsetBuilder(0), 1.0f });
			result.ranges.resize(dstSize, TapRange{ 0, 1 });
			return result;
		}

		result.taps.reserve(dstSize * 2);

		const double scale = static_cast<double>(dstSize) / static_cast<double>(srcSize);
		const bool downscale = scale < 1.0;
		const double support = downscale ? (Kernel::radius / scale) : Kernel::radius;
		const int64_t srcMax = static_cast<int64_t>(srcSize) - 1;

		for (uint64_t d = 0; d < dstSize; ++d)
		{
			const double srcPos = (static_cast<double>(d) + 0.5) / scale - 0.5;
			const int64_t left = static_cast<int64_t>(std::floor(srcPos - support));
			const int64_t right = static_cast<int64_t>(std::ceil(srcPos + support));
			const size_t firstTap = result.taps.size();
			double sum = 0.0;

			for (int64_t s = left; s <= right; ++s)
			{
				const double distance = srcPos - static_cast<double>(s);

				const double weight = downscale
					? Kernel::evaluate(distance * scale) * scale
					: Kernel::evaluate(distance);

				if (weight == 0.0)
					continue;

				const int64_t clamped = std::clamp(s, int64_t{ 0 }, srcMax);
				result.taps.push_back(Tap{ offsetBuilder(static_cast<uint64_t>(clamped)), static_cast<float>(weight) });
				// Summing the stored floats rather than the doubles makes the normalization below cancel their
				// rounding, so a row of equal pixels still resolves to exactly that value.
				sum += result.taps.back().weight;
			}

			if (sum != 0.0)
			{
				const double invSum = 1.0 / sum;
				for (size_t tapIndex = firstTap; tapIndex < result.taps.size(); ++tapIndex)
					result.taps[tapIndex].weight = static_cast<float>(result.taps[tapIndex].weight * invSum);
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
		const AxisWeights& xWeights,
		uint64_t rowBegin,
		uint64_t rowEnd)
	{
		for (uint64_t sy = rowBegin; sy < rowEnd; ++sy)
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
		uint64_t rowBegin,
		uint64_t rowEnd,
		RowWriter&& writeRow)
	{
		const auto accumRow = std::make_unique_for_overwrite<float[]>(rowElementCount);

		for (uint64_t dy = rowBegin; dy < rowEnd; ++dy)
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
	void resizeImpl(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, CWorkerThreadPool* threadPool)
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
			{
				forEachRowBand(threadPool, srcRect.h, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
					{
						filterHorizontal4BytePixelsSimd<Channels>(temp.get(), tempRowStride, source, srcRect, dest.width, xWeights, rowBegin, rowEnd);
					});
			}
		}
#endif

		if (!useSimd)
		{
			forEachRowBand(threadPool, srcRect.h, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
				{
					filterHorizontalRows<Channels, PixelStride>(temp.get(), tempRowStride, source, srcRect, dest.width, xWeights, rowBegin, rowEnd);
				});
		}

#if IMAGE_PROCESSING_SIMD
		if constexpr (PixelStride == 4 && (Channels == 3 || Channels == 4))
		{
			if (useSimd)
			{
				const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * PixelStride;
				forEachRowBand(threadPool, dest.height, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
					{
						filterVerticalRowsSimd<Channels>(yWeights, temp.get(), dest, pixelTailSource[3], rowBegin, rowEnd);
					});
				return;
			}
		}
#endif

		const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * PixelStride;
		// Capture-default: pixelTailSource is unused when the pixel has no tail bytes, and an explicit capture would then be diagnosed
		auto writeRow = [&](uint64_t dy, const float* accumRow)
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
			};

		forEachRowBand(threadPool, dest.height, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
			{
				filterVerticalRowsScalar(yWeights, temp.get(), tempRowStride, rowBegin, rowEnd, writeRow);
			});
	}

	void resizeImplRuntime(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, CWorkerThreadPool* threadPool)
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

		forEachRowBand(threadPool, srcRect.h, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
		{
			for (uint64_t ty = rowBegin; ty < rowEnd; ++ty)
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
		});

		const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * pixelStride;
		const auto writeRow = [&dest, numChannels, pixelStride, pixelTailSource](uint64_t dy, const float* accumRow)
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
		};

		forEachRowBand(threadPool, dest.height, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
		{
			filterVerticalRowsScalar(yWeights, temp.get(), tempRowStride, rowBegin, rowEnd, writeRow);
		});
	}

	template <size_t Channels>
	inline void resizeDispatchStride(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, CWorkerThreadPool* threadPool)
	{
		switch (source.pixelStrideBytes)
		{
		case 1:
			if constexpr (Channels == 1)
				resizeImpl<1, 1>(dest, source, srcRect, threadPool);
			else
				resizeImplRuntime(dest, source, srcRect, threadPool);
			return;

		case 2:
			if constexpr (Channels == 1)
				resizeImpl<1, 2>(dest, source, srcRect, threadPool);
			else
				resizeImplRuntime(dest, source, srcRect, threadPool);
			return;

		case 3:
			if constexpr (Channels == 1)
				resizeImpl<1, 3>(dest, source, srcRect, threadPool);
			else if constexpr (Channels == 3)
				resizeImpl<3, 3>(dest, source, srcRect, threadPool);
			else
				resizeImplRuntime(dest, source, srcRect, threadPool);
			return;

		case 4:
			if constexpr (Channels == 1)
				resizeImpl<1, 4>(dest, source, srcRect, threadPool);
			else if constexpr (Channels == 3)
				resizeImpl<3, 4>(dest, source, srcRect, threadPool);
			else if constexpr (Channels == 4)
				resizeImpl<4, 4>(dest, source, srcRect, threadPool);
			else
				resizeImplRuntime(dest, source, srcRect, threadPool);
			return;

		default:
			resizeImplRuntime(dest, source, srcRect, threadPool);
			return;
		}
	}
}

void ImageProcessing::resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, CWorkerThreadPool* threadPool)
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

	// This file is compiled for the baseline instruction set, so its scalar float code - weight building above
	// all - is legacy-SSE encoded and stalls on every instruction while the upper YMM state is dirty, as a caller
	// that used 256-bit AVX without vzeroupper leaves it (~10% of a resize). The check: vzeroupper needs AVX.
	if (SimdSupport::canUseSimd())
		SimdSupport::clearAvxUpperState();

	switch (source.channels)
	{
	case 1: resizeDispatchStride<1>(dest, source, srcRect, threadPool); return;
	case 3: resizeDispatchStride<3>(dest, source, srcRect, threadPool); return;
	case 4: resizeDispatchStride<4>(dest, source, srcRect, threadPool); return;
	default: resizeImplRuntime(dest, source, srcRect, threadPool); return;
	}
}
