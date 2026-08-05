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
		result.runs.reserve(dstSize);

		if (srcSize == 1) [[unlikely]]
		{
			result.weights.push_back(1.0f);
			result.runs.resize(dstSize, TapRun{ offsetBuilder(0), 0, 1 });
			return result;
		}

		const double scale = static_cast<double>(dstSize) / static_cast<double>(srcSize);
		const bool downscale = scale < 1.0;
		const double support = downscale ? (Kernel::radius / scale) : Kernel::radius;
		const int64_t srcMax = static_cast<int64_t>(srcSize) - 1;

		result.weights.reserve(dstSize * (static_cast<size_t>(2.0 * support) + 2));
		std::vector<double> foldedWeights;

		for (uint64_t d = 0; d < dstSize; ++d)
		{
			const double srcPos = (static_cast<double>(d) + 0.5) / scale - 0.5;
			const int64_t left = static_cast<int64_t>(std::floor(srcPos - support));
			const int64_t right = static_cast<int64_t>(std::ceil(srcPos + support));
			const int64_t runFirst = std::clamp(left, int64_t{ 0 }, srcMax);
			const int64_t runLast = std::clamp(right, int64_t{ 0 }, srcMax);

			// Border clamping repeats the boundary pixel, so out-of-range weights add into the boundary weight
			// exactly; folding them here (in double) is what keeps every run contiguous in the source.
			foldedWeights.assign(static_cast<size_t>(runLast - runFirst) + 1, 0.0);
			for (int64_t s = left; s <= right; ++s)
			{
				const double distance = srcPos - static_cast<double>(s);

				const double weight = downscale
					? Kernel::evaluate(distance * scale) * scale
					: Kernel::evaluate(distance);

				foldedWeights[static_cast<size_t>(std::clamp(s, runFirst, runLast) - runFirst)] += weight;
			}

			// The window edges land where the kernels are exactly zero, so most runs carry dead end taps; only
			// interior zeros are needed for contiguity. (Interior Lanczos zeros compute as ~1e-16, not 0, and stay.)
			size_t runBegin = 0;
			size_t runEnd = foldedWeights.size();
			while (runBegin < runEnd && foldedWeights[runBegin] == 0.0)
				++runBegin;
			while (runBegin < runEnd && foldedWeights[runEnd - 1] == 0.0)
				--runEnd;

			const size_t firstWeight = result.weights.size();

			if (runBegin == runEnd) [[unlikely]] // no nonzero weights in the window; fall back to the nearest pixel
			{
				result.weights.push_back(1.0f);
				result.runs.push_back(TapRun{ offsetBuilder(static_cast<uint64_t>(std::clamp(
					static_cast<int64_t>(std::lround(srcPos)), runFirst, runLast))), firstWeight, 1 });
				continue;
			}

			double sum = 0.0;
			for (size_t i = runBegin; i < runEnd; ++i)
			{
				result.weights.push_back(static_cast<float>(foldedWeights[i]));
				// Summing the stored floats rather than the doubles makes the normalization below cancel their
				// rounding, so a row of equal pixels still resolves to exactly that value.
				sum += result.weights.back();
			}

			if (sum != 0.0) [[likely]]
			{
				const double invSum = 1.0 / sum;
				for (size_t i = firstWeight; i < result.weights.size(); ++i)
					result.weights[i] = static_cast<float>(result.weights[i] * invSum);
			}
			else
			{
				const int64_t trimmedFirst = runFirst + static_cast<int64_t>(runBegin);
				const int64_t trimmedLast = runFirst + static_cast<int64_t>(runEnd) - 1;
				std::fill(result.weights.begin() + static_cast<ptrdiff_t>(firstWeight), result.weights.end(), 0.0f);
				const int64_t nearest = std::clamp(static_cast<int64_t>(std::lround(srcPos)), trimmedFirst, trimmedLast);
				result.weights[firstWeight + static_cast<size_t>(nearest - trimmedFirst)] = 1.0f;
			}

			result.runs.push_back(TapRun{ offsetBuilder(static_cast<uint64_t>(runFirst) + runBegin), firstWeight, runEnd - runBegin });
		}

		// The horizontal kernel's 4-tap block reads a run's weights with one 8-float load; this slack keeps
		// that read in bounds for the last run.
		result.weights.resize(result.weights.size() + 4);

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
				const auto [srcStartOffset, weights] = xWeights.runFor(dx);
				const auto* srcPixel = srcRow + srcStartOffset;
				float* outPixel = tempRow + static_cast<size_t>(dx) * Channels;
				std::array<float, Channels> accum{};

				for (const float weight : weights)
				{
					for (size_t c = 0; c < Channels; ++c)
						accum[c] += static_cast<float>(srcPixel[c]) * weight;

					srcPixel += PixelStride;
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

			const auto [tempStartOffset, weights] = yWeights.runFor(dy);
			// Temp rows are dense, so the row element count is also the row stride
			const float* tempRow = temp + tempStartOffset;

			// A zero tap here would cost a whole row sweep, and exact-ratio downscales produce them
			// (the kernels are zero at integer offsets)
			for (const float weight : weights)
			{
				if (weight != 0.0f)
				{
					for (size_t element = 0; element < rowElementCount; ++element)
						accumRow[element] += tempRow[element] * weight;
				}

				tempRow += rowElementCount;
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

		bool useSimd = false;
#if IMAGE_PROCESSING_SIMD
		if constexpr (PixelStride == 4 && (Channels == 3 || Channels == 4))
			useSimd = SimdSupport::canUseSimd();
#endif

		const auto xWeights = scaleUpX
			? buildAxisWeights<BicubicKernel>(srcRect.w, dest.width, [](uint64_t sx) noexcept -> size_t
			{
				return static_cast<size_t>(sx) * PixelStride;
			})
			: buildAxisWeights<Lanczos3Kernel>(srcRect.w, dest.width, [](uint64_t sx) noexcept -> size_t
			{
				return static_cast<size_t>(sx) * PixelStride;
			});

		// The fused SIMD path locates temp rows through its ring, so its y offsets are plain row indices;
		// the scalar two-pass path indexes the dense temp buffer directly, so the row stride folds in.
		const size_t yOffsetStride = useSimd ? 1 : tempRowStride;
		const auto yWeights = scaleUpY
			? buildAxisWeights<BicubicKernel>(srcRect.h, dest.height, [yOffsetStride](uint64_t sy) noexcept -> size_t
			{
				return static_cast<size_t>(sy) * yOffsetStride;
			})
			: buildAxisWeights<Lanczos3Kernel>(srcRect.h, dest.height, [yOffsetStride](uint64_t sy) noexcept -> size_t
			{
				return static_cast<size_t>(sy) * yOffsetStride;
			});

#if IMAGE_PROCESSING_SIMD
		if constexpr (PixelStride == 4 && (Channels == 3 || Channels == 4))
		{
			if (useSimd)
			{
				const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * PixelStride;
				// A fused row's work includes producing its share of temp rows, srcRect.h / dest.height of them
				const size_t fusedElementsPerRow = tempRowStride + tempRowStride * static_cast<size_t>(srcRect.h) / static_cast<size_t>(dest.height);
				forEachRowBand(threadPool, dest.height, fusedElementsPerRow, [&](uint64_t rowBegin, uint64_t rowEnd)
				{
					resizeRows4BytePixelsSimd<Channels>(source, srcRect, dest, xWeights, yWeights, pixelTailSource[3], rowBegin, rowEnd);
				});
				return;
			}
		}
#endif

		const auto temp = std::make_unique_for_overwrite<float[]>(static_cast<size_t>(srcRect.h) * tempRowStride);

		forEachRowBand(threadPool, srcRect.h, tempRowStride, [&](uint64_t rowBegin, uint64_t rowEnd)
		{
			filterHorizontalRows<Channels, PixelStride>(temp.get(), tempRowStride, source, srcRect, dest.width, xWeights, rowBegin, rowEnd);
		});

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
					const auto [srcStartOffset, weights] = xWeights.runFor(dx);
					const auto* srcPixel = srcRow + srcStartOffset;
					float* outPixel = tempRow + static_cast<size_t>(dx) * numChannels;

					for (size_t c = 0; c < numChannels; ++c)
						outPixel[c] = 0.0f;

					for (const float weight : weights)
					{
						for (size_t c = 0; c < numChannels; ++c)
							outPixel[c] += static_cast<float>(srcPixel[c]) * weight;

						srcPixel += pixelStride;
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
