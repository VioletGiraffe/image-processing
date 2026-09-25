#include "resize_internal.h"

#include "compiler/compiler_warnings_control.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <string.h>
#include <vector>

// The float equality tests are exact-zero checks by design
DISABLE_CLANG_GCC_WARNING("-Wfloat-equal")

using namespace ImageProcessing;
using namespace ImageProcessing::Detail;

namespace
{
	// Runs worker(rowBegin, rowEnd) over [0, rowCount) split into contiguous bands: through the callback when the
	// total work justifies the dispatch overhead, serially otherwise (or with no callback). Blocks until done.
	template <class Worker>
	void forEachRowBand(const ParallelForFn& parallelFor, uint64_t rowCount, size_t elementsPerRow, Worker&& worker)
	{
		constexpr uint64_t minElementsPerBand = 32 * 1024;
		// The band count bounds the concurrency: the executors can never outnumber the bands
		constexpr uint64_t maxExecutors = 4;
		uint64_t bandCount = 1;
		if (parallelFor)
			bandCount = std::min({ rowCount, rowCount * elementsPerRow / minElementsPerBand, maxExecutors });

		if (bandCount <= 1)
		{
			worker(uint64_t{ 0 }, rowCount);
			return;
		}

		parallelFor(bandCount, [&](size_t band)
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

	template <class Kernel>
	[[nodiscard]] inline AxisWeights buildAxisWeights(uint64_t srcSize, uint64_t dstSize)
	{
		AxisWeights result;
		result.runs.reserve(dstSize);

		if (srcSize == 1) [[unlikely]]
		{
			result.weights.push_back(1.0f);
			result.runs.resize(dstSize, TapRun{ 0, 0, 1 });
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
				result.runs.push_back(TapRun{ static_cast<size_t>(std::clamp(static_cast<int64_t>(std::lround(srcPos)), runFirst, runLast)), firstWeight, 1 });
				continue;
			}

			double sum = 0.0;
			for (size_t i = runBegin; i < runEnd; ++i)
			{
				result.weights.push_back(static_cast<float>(foldedWeights[i]));
				// Summing the stored floats rather than the doubles makes the normalization below cancel their
				// rounding, so a row of equal pixels still resolves to exactly that value.
				sum += static_cast<double>(result.weights.back());
			}

			if (sum != 0.0) [[likely]]
			{
				const double invSum = 1.0 / sum;
				for (size_t i = firstWeight; i < result.weights.size(); ++i)
					result.weights[i] = static_cast<float>(static_cast<double>(result.weights[i]) * invSum);
			}
			else
			{
				const int64_t trimmedFirst = runFirst + static_cast<int64_t>(runBegin);
				const int64_t trimmedLast = runFirst + static_cast<int64_t>(runEnd) - 1;
				std::fill(result.weights.begin() + static_cast<ptrdiff_t>(firstWeight), result.weights.end(), 0.0f);
				const int64_t nearest = std::clamp(static_cast<int64_t>(std::lround(srcPos)), trimmedFirst, trimmedLast);
				result.weights[firstWeight + static_cast<size_t>(nearest - trimmedFirst)] = 1.0f;
			}

			result.runs.push_back(TapRun{ static_cast<size_t>(runFirst) + runBegin, firstWeight, runEnd - runBegin });
		}

		// The horizontal kernel's 4-tap block reads a run's weights with one 8-float load; this slack keeps
		// that read in bounds for the last run.
		result.weights.resize(result.weights.size() + 4);

		return result;
	}

	[[nodiscard]] inline AxisWeights buildAxisWeightsForKernel(ResizeKernel kernel, uint64_t srcSize, uint64_t dstSize)
	{
		if (kernel == ResizeKernel::Auto)
			kernel = dstSize >= srcSize ? ResizeKernel::CatmullRom : ResizeKernel::Lanczos3;

		if (kernel == ResizeKernel::Lanczos3)
			return buildAxisWeights<Lanczos3Kernel>(srcSize, dstSize);

		assert(kernel == ResizeKernel::CatmullRom);
		return buildAxisWeights<BicubicKernel>(srcSize, dstSize);
	}

	inline void copyPixelTail(uint8_t* destPixel, const uint8_t* sourcePixel, size_t channels, size_t pixelStride) noexcept
	{
		// Bytes outside the logical channels can still carry pixel-format invariants, such as RGB32's required 0xff byte.
		assert(pixelStride > channels);
		::memcpy(destPixel + channels, sourcePixel + channels, pixelStride - channels);
	}

	// Rounds to nearest, as the float premultiply of the filtering paths does
	inline void premultiplyPixelBytes(uint8_t* pixel, size_t channels) noexcept
	{
		const unsigned alpha = pixel[channels - 1];
		for (size_t channel = 0; channel + 1 < channels; ++channel)
			pixel[channel] = static_cast<uint8_t>((pixel[channel] * alpha + 127) / 255);
	}

	inline void copyUnscaledCrop(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, size_t pixelStride)
	{
		const bool premultiplyAlpha = hasStraightAlpha(source);
		const size_t logicalRowBytes = static_cast<size_t>(dest.width) * pixelStride;
		for (uint64_t y = 0; y < dest.height; ++y)
		{
			const auto* srcRow = source.scanLine<uint8_t>(srcRect.top + y) + srcRect.left * pixelStride;
			auto* dstRow = dest.scanLine<uint8_t>(y);
			::memcpy(dstRow, srcRow, logicalRowBytes);

			if (premultiplyAlpha)
			{
				for (uint64_t x = 0; x < dest.width; ++x)
					premultiplyPixelBytes(dstRow + static_cast<size_t>(x) * pixelStride, source.channels);
			}
		}
	}

	// PixelStride 0: the stride is taken at runtime, from pixelStride
	template <size_t PixelStride>
	[[nodiscard]] inline size_t effectivePixelStride(size_t pixelStride) noexcept
	{
		assert(PixelStride == 0 || PixelStride == pixelStride);
		return PixelStride != 0 ? PixelStride : pixelStride;
	}

	// Channels floats per pixel, color premultiplied as the filtering needs it
	template <size_t Channels, size_t PixelStride, bool PremultiplyAlpha>
	void convertRowToFloats(const uint8_t* pixels, size_t pixelStride, float* floats, size_t pixelCount) noexcept
	{
		pixelStride = effectivePixelStride<PixelStride>(pixelStride);
		for (size_t pixel = 0; pixel < pixelCount; ++pixel, pixels += pixelStride, floats += Channels)
		{
			if constexpr (PremultiplyAlpha)
			{
				const float premultiplier = static_cast<float>(pixels[Channels - 1]) * (1.0f / 255.0f);
				for (size_t channel = 0; channel + 1 < Channels; ++channel)
					floats[channel] = static_cast<float>(pixels[channel]) * premultiplier;

				floats[Channels - 1] = static_cast<float>(pixels[Channels - 1]);
			}
			else
			{
				for (size_t channel = 0; channel < Channels; ++channel)
					floats[channel] = static_cast<float>(pixels[channel]);
			}
		}
	}

	// Filters dest columns [destBegin, destEnd) into tempRow; sourceFloats starts at source pixel firstSourcePixel
	template <size_t Channels>
	void filterHorizontalRow(const float* sourceFloats, size_t firstSourcePixel, float* tempRow, size_t destBegin, size_t destEnd, const AxisWeights& xWeights) noexcept
	{
		for (size_t dx = destBegin; dx < destEnd; ++dx)
		{
			const auto [firstPixel, weights] = xWeights.runFor(dx);
			const float* srcPixel = sourceFloats + (firstPixel - firstSourcePixel) * Channels;
			std::array<float, Channels> accum{};

			for (const float weight : weights)
			{
				for (size_t channel = 0; channel < Channels; ++channel)
					accum[channel] += srcPixel[channel] * weight;

				srcPixel += Channels;
			}

			std::copy_n(accum.data(), Channels, tempRow + (dx - destBegin) * Channels);
		}
	}

	// Sums the window's temp rows into accumRow, tap by tap: each tap is one sweep over contiguous floats
	void filterVerticalRow(const std::array<TempRowSegment, 2>& segments, std::span<const float> rowWeights, size_t tempRowStride, size_t rowElementCount, float* accumRow) noexcept
	{
		std::fill_n(accumRow, rowElementCount, 0.0f);

		const float* weight = rowWeights.data();
		for (const TempRowSegment& segment : segments)
		{
			const float* tempRow = segment.firstRow;
			for (size_t row = 0; row < segment.rowCount; ++row, ++weight, tempRow += tempRowStride)
			{
				// A zero tap would cost a whole row sweep, and exact-ratio downscales produce them (the kernels are zero at integer offsets)
				const float tapWeight = *weight;
				if (tapWeight == 0.0f)
					continue;

				for (size_t element = 0; element < rowElementCount; ++element)
					accumRow[element] += tempRow[element] * tapWeight;
			}
		}
	}

	// Resizes dest rows [destRowBegin, destRowEnd) through a TempRowRing, one column strip at a time.
	// Each source row's strip span is converted to floats once, whole: the x runs of neighboring columns overlap.
	template <size_t Channels, size_t PixelStride>
	void resizeRowsScalar(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		size_t stripWidth,
		uint64_t destRowBegin,
		uint64_t destRowEnd)
	{
		const size_t pixelStride = effectivePixelStride<PixelStride>(source.pixelStrideBytes);
		const size_t destWidth = static_cast<size_t>(dest.width);
		const size_t tempRowStride = stripWidth * Channels;

		const TempRowRing ring{ yWeights, destRowBegin, destRowEnd, tempRowStride, 1 };
		const auto sourceFloats = std::make_unique_for_overwrite<float[]>(static_cast<size_t>(srcRect.w) * Channels);
		const auto accumRow = std::make_unique_for_overwrite<float[]>(tempRowStride);

		// A layout without alpha never instantiates the premultiplying variant
		auto* const convertRow = hasStraightAlpha(source)
			? &convertRowToFloats<Channels, PixelStride, hasAlphaChannel(Channels)>
			: &convertRowToFloats<Channels, PixelStride, false>;
		const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * pixelStride;

		for (size_t stripBegin = 0; stripBegin < destWidth; stripBegin += stripWidth)
		{
			const size_t stripEnd = std::min(stripBegin + stripWidth, destWidth);
			const size_t stripElementCount = (stripEnd - stripBegin) * Channels;
			const AxisWeights::SourceSpan span = xWeights.sourceSpan(stripBegin, stripEnd);

			uint64_t produced = ring.firstNeededRow();
			for (uint64_t dy = destRowBegin; dy < destRowEnd; ++dy)
			{
				const auto [firstWindowRow, rowWeights] = yWeights.runFor(dy);
				const uint64_t windowEnd = firstWindowRow + rowWeights.size();
				assert(windowEnd <= srcRect.h);

				for (; produced < windowEnd; ++produced)
				{
					const auto* spanPixels = source.scanLine<uint8_t>(srcRect.top + produced) + (srcRect.left + span.begin) * pixelStride;
					convertRow(spanPixels, pixelStride, sourceFloats.get(), span.end - span.begin);
					filterHorizontalRow<Channels>(sourceFloats.get(), span.begin, ring.row(produced), stripBegin, stripEnd, xWeights);
				}

				assert(produced - firstWindowRow <= ring.rowCapacity());
				filterVerticalRow(ring.window(firstWindowRow, rowWeights.size()), rowWeights, tempRowStride, stripElementCount, accumRow.get());

				auto* dstPixel = dest.scanLine<uint8_t>(dy) + stripBegin * pixelStride;
				for (size_t dx = stripBegin; dx < stripEnd; ++dx, dstPixel += pixelStride)
				{
					writePixelBytes(dstPixel, accumRow.get() + (dx - stripBegin) * Channels, Channels);
					if (pixelStride > Channels)
						copyPixelTail(dstPixel, pixelTailSource, Channels, pixelStride);
				}
			}
		}
	}

	// Dest columns per strip: each thread holds a ring of temp rows for one strip at a time, and the strip width bounds
	// that ring to a share of a small L2 shared by all cores, such as the Pi 4's 1 MB.
	[[nodiscard]] size_t stripWidthFor(uint64_t destWidth, const AxisWeights& yWeights, size_t channels) noexcept
	{
		// Half of a four-core share of the Pi's L2: the source floats, weights and dest rows need the rest
		constexpr size_t ringBudgetBytes = 128 * 1024;
		// The SIMD vertical pass writes 8-pixel blocks, with a scalar tail per strip
		constexpr size_t widthGranule = 8;

		size_t longestYRun = 0;
		for (const TapRun& run : yWeights.runs)
			longestYRun = std::max(longestYRun, run.weightCount);

		const size_t width = static_cast<size_t>(destWidth);
		const size_t maxStripWidth = std::max(widthGranule, ringBudgetBytes / (longestYRun * channels * sizeof(float)));
		const size_t stripCount = (width + maxStripWidth - 1) / maxStripWidth;
		// Equal widths: a narrow last strip would pay the per-strip costs for little work
		const size_t equalWidth = (width + stripCount - 1) / stripCount;
		return std::min(width, (equalWidth + widthGranule - 1) / widthGranule * widthGranule);
	}

	template <size_t Channels>
	void resizeImpl(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, const ParallelForFn& parallelFor, ResizeKernel kernel, [[maybe_unused]] SimdUsage simd)
	{
		static_assert(Channels >= 1 && Channels <= 4);

		assert(source.width > 0 && source.height > 0);
		assert(dest.width > 0 && dest.height > 0);

		assert(source.channels == Channels);
		assert(dest.channels == Channels);
		assert(source.bytesPerChannel == 1);
		assert(dest.bytesPerChannel == 1);
		assert(source.pixelStrideBytes == dest.pixelStrideBytes);
		assert(source.pixelStrideBytes >= Channels);

		const size_t pixelStride = source.pixelStrideBytes;

		if (srcRect.w == dest.width && srcRect.h == dest.height)
		{
			copyUnscaledCrop(dest, source, srcRect, pixelStride);
			return;
		}

		const auto xWeights = buildAxisWeightsForKernel(kernel, srcRect.w, dest.width);
		const auto yWeights = buildAxisWeightsForKernel(kernel, srcRect.h, dest.height);

		const size_t tempRowStride = static_cast<size_t>(dest.width) * Channels;
		// A dest row's work includes producing its share of temp rows, srcRect.h / dest.height of them
		const size_t elementsPerDestRow = tempRowStride + tempRowStride * static_cast<size_t>(srcRect.h) / static_cast<size_t>(dest.height);
		const size_t stripWidth = stripWidthFor(dest.width, yWeights, Channels);

#if IMAGE_PROCESSING_SIMD
		if constexpr (Channels == 3 || Channels == 4)
		{
			if (pixelStride == 4 && simd == SimdUsage::Auto && SimdSupport::canUseSimd())
			{
				const auto* pixelTailSource = source.scanLine<uint8_t>(srcRect.top) + srcRect.left * pixelStride;
				forEachRowBand(parallelFor, dest.height, elementsPerDestRow, [&](uint64_t rowBegin, uint64_t rowEnd)
				{
					resizeRows4BytePixelsSimd<Channels>(source, srcRect, dest, xWeights, yWeights, stripWidth, pixelTailSource[3], rowBegin, rowEnd);
				});
				return;
			}
		}
#endif

		// Tight packing and RGB32 get a compile-time stride: a runtime one makes the per-pixel tail copy a memcpy call and the conversion loop generic
		auto* resizeRows = &resizeRowsScalar<Channels, 0>;
		if (pixelStride == Channels)
			resizeRows = &resizeRowsScalar<Channels, Channels>;
		else if constexpr (Channels == 3)
		{
			if (pixelStride == 4)
				resizeRows = &resizeRowsScalar<3, 4>;
		}

		forEachRowBand(parallelFor, dest.height, elementsPerDestRow, [&](uint64_t rowBegin, uint64_t rowEnd)
		{
			resizeRows(source, srcRect, dest, xWeights, yWeights, stripWidth, rowBegin, rowEnd);
		});
	}
}

Detail::TempRowRing::TempRowRing(const AxisWeights& yWeights, uint64_t destRowBegin, uint64_t destRowEnd, size_t rowStride, size_t batchRows) :
	_rowStride{ rowStride }
{
	assert(destRowBegin < destRowEnd);
	assert(batchRows >= 1);

	// While writing dest row dy, production has passed dy's window by at most batchRows - 1 rows, and end-trimming lets a
	// later window start slightly before an earlier one - so each window's end is measured against the earliest start any
	// not-yet-written row still needs.
	uint64_t firstNeededRow = UINT64_MAX;
	size_t rowCount = 0;
	for (uint64_t dy = destRowEnd; dy-- > destRowBegin;)
	{
		const auto run = yWeights.runFor(dy);
		firstNeededRow = std::min(firstNeededRow, static_cast<uint64_t>(run.firstSource));
		rowCount = std::max(rowCount, run.firstSource + run.weights.size() + batchRows - 1 - static_cast<size_t>(firstNeededRow));
	}

	_firstNeededRow = firstNeededRow;
	_rowCount = rowCount;
	_rows = std::make_unique_for_overwrite<float[]>(rowCount * rowStride);
}

bool ImageProcessing::simdAvailable() noexcept
{
	return SimdSupport::canUseSimd();
}

void ImageProcessing::resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, const ParallelForFn& parallelFor, ResizeKernel kernel, SimdUsage simd)
{
	assert(source.width > 0 && source.height > 0);
	assert(dest.width > 0 && dest.height > 0);

	assert(source.channels == dest.channels);
	assert(source.bytesPerChannel == dest.bytesPerChannel);
	assert(source.pixelStrideBytes == dest.pixelStrideBytes);
	assert(!hasAlphaChannel(dest.channels) || dest.alphaKind == AlphaKind::Premultiplied);

	if (source.channels == 0 || source.channels > 4 || dest.channels > 4)
	{
		assert(false && "Only images with one to four channels are supported");
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
	case 1: resizeImpl<1>(dest, source, srcRect, parallelFor, kernel, simd); return;
	case 2: resizeImpl<2>(dest, source, srcRect, parallelFor, kernel, simd); return;
	case 3: resizeImpl<3>(dest, source, srcRect, parallelFor, kernel, simd); return;
	case 4: resizeImpl<4>(dest, source, srcRect, parallelFor, kernel, simd); return;
	}
}
