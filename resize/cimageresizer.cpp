#include "resize_internal.h"
#include "cpu_cache.h"

#include "compiler/compiler_warnings_control.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string.h>
#include <vector>

#if IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#include <intrin.h>
#endif

// The float equality tests are exact-zero checks by design
DISABLE_CLANG_GCC_WARNING("-Wfloat-equal")

using namespace ImageProcessing;
using namespace ImageProcessing::Detail;

namespace
{
#if IMAGE_PROCESSING_X64
	struct X64Features
	{
		bool sse41 = false;
		bool avx = false; // With the OS saving the YMM state
		bool avx2Fma = false;
	};

	// Detected here, in a file compiled for the baseline instruction set: a kernel file's copy could hold instructions the CPU lacks
	[[nodiscard]] const X64Features& x64Features() noexcept
	{
		static const X64Features features = []() noexcept {
			X64Features detected;
#if defined(_MSC_VER)
			int registers[4];
			__cpuid(registers, 0);
			const int highestLeaf = registers[0];

			__cpuidex(registers, 1, 0);
			const int leaf1Ecx = registers[2];
			constexpr int fmaBit = 1 << 12;
			constexpr int sse41Bit = 1 << 19;
			constexpr int osXsaveBit = 1 << 27;
			constexpr int avxBit = 1 << 28;
			detected.sse41 = (leaf1Ecx & sse41Bit) != 0;
			// XCR0 bits 1 and 2: the OS saves the XMM and YMM state
			detected.avx = (leaf1Ecx & (osXsaveBit | avxBit)) == (osXsaveBit | avxBit) && (_xgetbv(0) & 0x6) == 0x6;
			if (detected.avx && (leaf1Ecx & fmaBit) != 0 && highestLeaf >= 7)
			{
				__cpuidex(registers, 7, 0);
				constexpr int avx2Bit = 1 << 5;
				detected.avx2Fma = (registers[1] & avx2Bit) != 0;
			}
#else
			detected.sse41 = __builtin_cpu_supports("sse4.1");
			detected.avx = __builtin_cpu_supports("avx");
			detected.avx2Fma = __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma");
#endif
			return detected;
		}();

		return features;
	}
#endif

	// vzeroupper is usable: see resizeWithRingBudget for why it runs
	[[nodiscard]] bool hasUsableAvx() noexcept
	{
#if IMAGE_PROCESSING_X64
		return x64Features().avx;
#else
		return false;
#endif
	}

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
				// Each thread has its own upper YMM state: resizeWithRingBudget cleared only the calling thread's
				if (hasUsableAvx())
					SimdSupport::clearAvxUpperState();

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

		// The horizontal kernels load 8 weights where as few as 2 of the run remain: this slack keeps the last run's load in bounds
		constexpr size_t weightsSlack = 6;

		if (srcSize == 1) [[unlikely]]
		{
			result.weights.resize(1 + weightsSlack);
			result.weights[0] = 1.0f;
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

		result.weights.resize(result.weights.size() + weightsSlack);

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

	// Dest columns per strip: each thread holds a ring of temp rows for one strip at a time, and the strip width bounds
	// that ring to ringBudgetBytes, so that it stays in a small L2 shared by all cores, such as the Pi 4's.
	[[nodiscard]] size_t stripWidthFor(uint64_t destWidth, const AxisWeights& yWeights, size_t channels, size_t ringBudgetBytes) noexcept
	{
		// The vertical pass writes whole blocks, with a pixel-by-pixel tail per strip
		const size_t widthGranule = verticalBlockPixels(channels);

		const size_t width = static_cast<size_t>(destWidth);
		const size_t maxStripWidth = std::max(widthGranule, ringBudgetBytes / (yWeights.longestRun() * channels * sizeof(float)));
		const size_t stripCount = (width + maxStripWidth - 1) / maxStripWidth;
		// Equal widths: a narrow last strip would pay the per-strip costs for little work
		const size_t equalWidth = (width + stripCount - 1) / stripCount;
		return std::min(width, (equalWidth + widthGranule - 1) / widthGranule * widthGranule);
	}

	// Faults in dest rows [rowBegin, rowEnd) in address order, writing one logical byte per page: the resize overwrites it.
	// Windows charges ~2.5x per demand-zero fault when column strips touch fresh pages out of order.
	void touchDestPagesInOrder(ImageView<false>& dest, uint64_t rowBegin, uint64_t rowEnd, size_t pixelStride) noexcept
	{
		constexpr size_t pageSize = 4096;
		const size_t logicalRowBytes = static_cast<size_t>(dest.width) * pixelStride;
		for (uint64_t y = rowBegin; y < rowEnd; ++y)
		{
			// Volatile: the store exists for its fault, and the resize's later store to the same byte would make it dead
			volatile uint8_t* const row = dest.scanLine<uint8_t>(y);
			for (size_t offset = 0; offset < logicalRowBytes; offset += pageSize)
				row[offset] = 0;

			row[logicalRowBytes - 1] = 0;
		}
	}

	using RowResizer = void(const ImageView<true>& source, Rect srcRect, ImageView<false>& dest, const AxisWeights& xWeights, const AxisWeights& yWeights,
		size_t stripWidth, uint64_t destRowBegin, uint64_t destRowEnd);

	template <size_t Channels, size_t PixelStride>
	[[nodiscard]] RowResizer* rowResizerFor([[maybe_unused]] SimdLevel simdLevel) noexcept
	{
#if IMAGE_PROCESSING_X64
		return simdLevel == SimdLevel::Avx2 ? &Avx2::resizeRows<Channels, PixelStride> : &Sse41::resizeRows<Channels, PixelStride>;
#else
		return &Neon::resizeRows<Channels, PixelStride>;
#endif
	}

	template <size_t Channels>
	void resizeImpl(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, const ParallelForFn& parallelFor, ResizeKernel kernel, SimdLevel simdLevel, size_t ringBudgetBytes)
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
		const size_t stripWidth = stripWidthFor(dest.width, yWeights, Channels, ringBudgetBytes);
		const bool touchDestPages = stripWidth < dest.width;

		// Tight packing and RGB32 get a compile-time stride: the kernels' vector conversion and writes need one
		RowResizer* resizeRows = rowResizerFor<Channels, 0>(simdLevel);
		if (pixelStride == Channels)
			resizeRows = rowResizerFor<Channels, Channels>(simdLevel);
		else if (Channels == 3 && pixelStride == 4)
			resizeRows = rowResizerFor<3, 4>(simdLevel);

		forEachRowBand(parallelFor, dest.height, elementsPerDestRow, [&](uint64_t rowBegin, uint64_t rowEnd)
		{
			if (touchDestPages)
				touchDestPagesInOrder(dest, rowBegin, rowEnd, pixelStride);

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

// Each thread's temp-row ring gets half of its L2 share: the source floats, weights and dest rows need the rest.
// An undetected L2 counts as the Pi 4's: 1 MB shared by four cores.
size_t Detail::detectedRingBudgetBytes()
{
	static const size_t budget = [] {
		const size_t l2Share = smallestL2BytesPerLogicalProcessor();
		return (l2Share != 0 ? l2Share : 1024 * 1024 / 4) / 2;
	}();
	return budget;
}

std::optional<SimdLevel> ImageProcessing::detectedSimdLevel() noexcept
{
#if IMAGE_PROCESSING_X64
	const X64Features& features = x64Features();
	if (features.avx2Fma)
		return SimdLevel::Avx2;
	if (features.sse41)
		return SimdLevel::Sse41;
	return std::nullopt;
#else
	return SimdLevel::Neon;
#endif
}

void ImageProcessing::resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, const ParallelForFn& parallelFor, ResizeKernel kernel, std::optional<SimdLevel> simdCap)
{
	resizeWithRingBudget(dest, source, srcRect, parallelFor, kernel, simdCap, detectedRingBudgetBytes());
}

void Detail::resizeWithRingBudget(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, const ParallelForFn& parallelFor, ResizeKernel kernel, std::optional<SimdLevel> simdCap, size_t ringBudgetBytes)
{
	assert(ringBudgetBytes > 0);
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

	const std::optional<SimdLevel> detectedLevel = detectedSimdLevel();
	if (!detectedLevel)
	{
		assert(false && "The CPU lacks SSE4.1, the resizer's minimum on x64");
		return;
	}

	// x64's levels are ordered, and ARM64 has one
	assert(!simdCap || ((*simdCap == SimdLevel::Neon) == IMAGE_PROCESSING_ARM64 && *simdCap <= *detectedLevel));
	const SimdLevel simdLevel = simdCap ? std::min(*simdCap, *detectedLevel) : *detectedLevel;

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
	// that used 256-bit AVX without vzeroupper leaves it (~10% of a resize). The SSE4.1 kernels are legacy-SSE encoded too.
	// The check: vzeroupper needs AVX.
	if (hasUsableAvx())
		SimdSupport::clearAvxUpperState();

	switch (source.channels)
	{
	case 1: resizeImpl<1>(dest, source, srcRect, parallelFor, kernel, simdLevel, ringBudgetBytes); return;
	case 2: resizeImpl<2>(dest, source, srcRect, parallelFor, kernel, simdLevel, ringBudgetBytes); return;
	case 3: resizeImpl<3>(dest, source, srcRect, parallelFor, kernel, simdLevel, ringBudgetBytes); return;
	case 4: resizeImpl<4>(dest, source, srcRect, parallelFor, kernel, simdLevel, ringBudgetBytes); return;
	}
}
