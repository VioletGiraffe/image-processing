#pragma once

#include "cimageresizer.h"
#include "simd_support.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace ImageProcessing::Detail
{
	// The fewest taps x runs are padded to: an upscale's
	inline constexpr size_t shortestPaddedXRun = 4;

	// One destination coordinate's filter: weights for a contiguous run of source pixels (x axis) or rows (y axis).
	// weights[i] applies to source index firstSource + i: border clamping is folded into the boundary weights at build
	// time, and interior zero weights are kept, so runs have no gaps.
	struct TapRun
	{
		size_t firstSource;
		const float* weights;
		size_t weightCount;
	};

	// Three consecutive x runs of 4 taps, dest columns 3t to 3t + 2 for triple t, whose starts step 0 or 1 pixel, as an upscale's do.
	// A kernel filtering them together loads the source pixels they share once.
	struct TapRunTriple
	{
		uint32_t firstSource;
		// The steps between the runs' starts: first step + 2 * second step
		uint32_t pattern;
		// The three runs' 4 weights, one after another
		const float* weights;
	};

	// std::allocator on a 64-byte boundary
	template <class T>
	struct CacheLineAllocator
	{
		using value_type = T;

		CacheLineAllocator() = default;
		template <class U>
		constexpr CacheLineAllocator(const CacheLineAllocator<U>&) noexcept {}

		[[nodiscard]] T* allocate(size_t count) { return static_cast<T*>(::operator new(count * sizeof(T), std::align_val_t{ 64 })); }
		void deallocate(T* items, size_t) noexcept { ::operator delete(items, std::align_val_t{ 64 }); }

		template <class U>
		[[nodiscard]] bool operator==(const CacheLineAllocator<U>&) const noexcept { return true; }
	};

	// The x axis's runs start in order: firstSource never decreases. The y axis's may not.
	struct AxisWeights
	{
		// Every run's weights start at a multiple of this in the array: the horizontal kernels load 8 weights at offsets
		// of 8 into a run, and off a 32-byte boundary such a load can span two cache lines
		static constexpr size_t runAlignment = 8;
		using WeightVector = std::vector<float, CacheLineAllocator<float>>;

		AxisWeights() = default;
		// Not copyable: the runs point into weights
		AxisWeights(AxisWeights&&) = default;
		AxisWeights& operator=(AxisWeights&&) = default;

		struct Run
		{
			size_t firstSource;
			std::span<const float> weights;
		};

		[[nodiscard]] IMAGE_PROCESSING_FORCE_INLINE Run runFor(size_t coordinate) const noexcept
		{
			const TapRun& run = runs[coordinate];
			return { run.firstSource, { run.weights, run.weightCount } };
		}

		// The end of the source indices read by the runs of dest coordinates [destBegin, destEnd)
		[[nodiscard]] size_t sourceEnd(size_t destBegin, size_t destEnd) const noexcept
		{
			size_t end = 0;
			// Every run is checked: trailing zero weights are trimmed, so a run may end before its predecessor
			for (size_t coordinate = destBegin; coordinate < destEnd; ++coordinate)
				end = std::max(end, runs[coordinate].firstSource + runs[coordinate].weightCount);

			return end;
		}

		[[nodiscard]] size_t longestRun() const noexcept
		{
			size_t longest = 0;
			for (const TapRun& run : runs)
				longest = std::max(longest, run.weightCount);

			return longest;
		}

		WeightVector weights;
		std::vector<TapRun> runs;
		// Set by padRunsToOneLength: every run has this many taps, and the horizontal pass compiled for that length reads no count. 0: the lengths vary.
		size_t paddedRunLength = 0;
		// Set by buildRunTriples for a level that filters x runs three at a time; empty otherwise, and the runs are filtered one at a time
		std::vector<TapRunTriple> runTriples;
		// The triples' weights: triples whose runs share their weights share them too
		WeightVector tripleWeights;
	};

	struct AlignedFloatsDeleter
	{
		void operator()(float* floats) const noexcept { ::operator delete[](floats, std::align_val_t{ 64 }); }
	};

	using AlignedFloats = std::unique_ptr<float[], AlignedFloatsDeleter>;

	// Uninitialized, on a 64-byte boundary: off a 32-byte one, half of the AVX2 kernels' 32-byte loads span two cache lines
	[[nodiscard]] inline AlignedFloats makeAlignedFloats(size_t count)
	{
		return AlignedFloats{ static_cast<float*>(::operator new[](count * sizeof(float), std::align_val_t{ 64 })) };
	}

	// A tap window's temp rows as they sit in a TempRowRing
	struct TempRowSegment
	{
		const float* firstRow;
		size_t rowCount;
	};

	// Horizontally filtered rows for one band of destination rows and one column strip, in a ring barely larger than the y tap
	// window: the rows stay cache-resident between the passes instead of making a DRAM round trip through a whole-image buffer.
	// Bands are independent: each produces every temp row its windows need, so neighbors redo the shared boundary rows.
	class TempRowRing
	{
	public:
		// batchRows: the most rows produced at once, so production can pass a window's end by batchRows - 1
		// destRowsPerStep: dest rows filtered together, in steps from destRowBegin: their windows are in the ring at once
		// startOffset: the first row's start, in floats past a 64-byte boundary
		TempRowRing(const AxisWeights& yWeights, uint64_t destRowBegin, uint64_t destRowEnd, size_t rowStride, size_t batchRows, size_t destRowsPerStep, size_t startOffset);

		// The first source row the band's windows read
		[[nodiscard]] uint64_t firstNeededRow() const noexcept { return _firstNeededRow; }
		[[nodiscard]] size_t rowCapacity() const noexcept { return _rowCount; }

		[[nodiscard]] float* row(uint64_t sourceRow) const noexcept
		{
			return _firstRow + static_cast<size_t>(sourceRow % _rowCount) * _rowStride;
		}

		// A window is contiguous in the ring except across the wrap, which splits it at most once
		[[nodiscard]] std::array<TempRowSegment, 2> window(uint64_t firstRow, size_t rowCount) const noexcept
		{
			assert(rowCount <= _rowCount);
			const size_t firstSlot = static_cast<size_t>(firstRow % _rowCount);
			const size_t rowsToRingEnd = std::min(rowCount, _rowCount - firstSlot);
			return { { { _firstRow + firstSlot * _rowStride, rowsToRingEnd }, { _firstRow, rowCount - rowsToRingEnd } } };
		}

	private:
		AlignedFloats _rows;
		float* _firstRow;
		size_t _rowStride;
		size_t _rowCount;
		uint64_t _firstNeededRow;
	};

	// The ring budget resize() passes to resizeWithRingBudget, sized from the L2 once per process
	[[nodiscard]] size_t detectedRingBudgetBytes();

	// resize() with each thread's temp-row ring bounded to ringBudgetBytes, in place of the budget sized from the L2
	void resizeWithRingBudget(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect, const ParallelForFn& parallelFor,
		ResizeKernel kernel, std::optional<SimdLevel> simdCap, size_t ringBudgetBytes);

	[[nodiscard]] inline bool hasStraightAlpha(const ImageView<true>& image) noexcept
	{
		return hasAlphaChannel(image.channels) && image.alphaKind == AlphaKind::Straight;
	}

	// The source pixel under the centre of dest pixel d
	[[nodiscard]] constexpr uint64_t nearestSourcePixel(uint64_t d, uint64_t srcSize, uint64_t dstSize) noexcept
	{
		return (2 * d + 1) * srcSize / (2 * dstSize);
	}

	// A SIMD level's plan for expanding a row of 4-byte pixels to nearest neighbor, srcWidth -> destWidth, in blocks.
	// Empty where the level has no plan: a downscale, a row narrower than a block, or byte offsets past 32 bits.
	struct FourBytePixelExpansion
	{
		std::vector<uint32_t> sourceOffsets; // per block, the byte offset of its load in the source row
		std::vector<uint8_t> selectors;      // per block, the level's encoding of each pixel's place in that load
		size_t lastBlockDestOffset = 0;      // the last block ends at the row's end, overlapping its predecessor where the row is not whole blocks
	};

	// Color times alpha / 255 in place, rounded to nearest as the float premultiply of the filtering paths does
	inline void premultiplyPixelsScalar(uint8_t* pixels, size_t pixelCount, size_t pixelStride, size_t channels) noexcept
	{
		for (uint8_t* pixel = pixels, *end = pixels + pixelCount * pixelStride; pixel != end; pixel += pixelStride)
		{
			const unsigned alpha = pixel[channels - 1];
			for (size_t channel = 0; channel + 1 < channels; ++channel)
				pixel[channel] = static_cast<uint8_t>((pixel[channel] * alpha + 127) / 255);
		}
	}

	[[nodiscard]] IMAGE_PROCESSING_FORCE_INLINE uint8_t clampToByte(float value) noexcept
	{
		value = std::min(std::max(value, 0.0f), 255.0f);
		return static_cast<uint8_t>(value + 0.5f);
	}

	// With alpha, color is capped at alpha: the output is premultiplied, and negative filter lobes can push color past alpha
	IMAGE_PROCESSING_FORCE_INLINE void writePixelBytes(uint8_t* destPixel, const float* values, size_t channels) noexcept
	{
		if (hasAlphaChannel(channels))
		{
			const float alpha = values[channels - 1];
			for (size_t channel = 0; channel + 1 < channels; ++channel)
				destPixel[channel] = clampToByte(std::min(values[channel], alpha));

			destPixel[channels - 1] = clampToByte(alpha);
			return;
		}

		for (size_t channel = 0; channel < channels; ++channel)
			destPixel[channel] = clampToByte(values[channel]);
	}

	// The vertical pass writes blocks of this many pixels: 32 floats, 24 for 3 channels
	[[nodiscard]] constexpr size_t verticalBlockPixels(size_t channels) noexcept
	{
		return channels == 3 ? 8 : 32 / channels;
	}

	// The SIMD kernels resize dest rows [destRowBegin, destRowEnd) in one fused pass over a ring of temp rows.
	// Each is the outline in cimageresizer_simd.inl, compiled by cimageresizer_simd_<level>.cpp against that level's primitives.
	// The target attributes forbid inlining into the baseline dispatcher, keeping the runtime level check in control of what executes.
	// PixelStride 0 takes the image's stride at runtime; the tight strides and RGB32's 4 are instantiated.
#if IMAGE_PROCESSING_X64
	// MSVC compiles cimageresizer_simd_avx2.cpp with /arch:AVX2 and cimageresizer_simd_avx.cpp with /arch:AVX, so that their
	// 128-bit intrinsics are VEX-encoded too: a legacy SSE encoding stalls for tens of cycles per instruction whenever the
	// process left the upper YMM state dirty, which any AVX-using host does. GCC and Clang get that from the target attribute.
	namespace Avx2
	{
		template <size_t Channels, size_t PixelStride>
		IMAGE_PROCESSING_AVX2_TARGET void resizeRows(const ImageView<true>& source, Rect srcRect, ImageView<false>& dest, const AxisWeights& xWeights,
			const AxisWeights& yWeights, size_t stripWidth, uint64_t destRowBegin, uint64_t destRowEnd);
		// The longest AxisWeights::paddedRunLength resizeRows takes for the x axis
		[[nodiscard]] size_t longestPaddedXRun(size_t channels) noexcept;
		// Whether resizeRows filters x runs padded to 4 taps three at a time, from AxisWeights::runTriples
		[[nodiscard]] bool filtersRunTriples(size_t channels) noexcept;
		// premultiplyPixelsScalar of tightly packed Channels-byte pixels, Channels 2 or 4, from source to dest; dest may be source
		template <size_t Channels>
		IMAGE_PROCESSING_AVX2_TARGET void premultiplyPixels(uint8_t* dest, const uint8_t* source, size_t pixelCount) noexcept;
		IMAGE_PROCESSING_AVX2_TARGET void planFourBytePixelExpansion(FourBytePixelExpansion& plan, uint64_t srcWidth, uint64_t destWidth);
		// Requires a non-empty plan from this level; sourceSpan holds the plan's srcWidth pixels
		IMAGE_PROCESSING_AVX2_TARGET void expandFourBytePixels(uint8_t* destRow, const uint8_t* sourceSpan, const FourBytePixelExpansion& plan) noexcept;
	}

	namespace Avx
	{
		template <size_t Channels, size_t PixelStride>
		IMAGE_PROCESSING_AVX_TARGET void resizeRows(const ImageView<true>& source, Rect srcRect, ImageView<false>& dest, const AxisWeights& xWeights,
			const AxisWeights& yWeights, size_t stripWidth, uint64_t destRowBegin, uint64_t destRowEnd);
		// As Avx2's
		[[nodiscard]] size_t longestPaddedXRun(size_t channels) noexcept;
		[[nodiscard]] bool filtersRunTriples(size_t channels) noexcept;
		template <size_t Channels>
		IMAGE_PROCESSING_AVX_TARGET void premultiplyPixels(uint8_t* dest, const uint8_t* source, size_t pixelCount) noexcept;
		IMAGE_PROCESSING_AVX_TARGET void planFourBytePixelExpansion(FourBytePixelExpansion& plan, uint64_t srcWidth, uint64_t destWidth);
		IMAGE_PROCESSING_AVX_TARGET void expandFourBytePixels(uint8_t* destRow, const uint8_t* sourceSpan, const FourBytePixelExpansion& plan) noexcept;
	}

	// Legacy-SSE encoded: every thread running it clears the upper YMM state first wherever AVX exists
	namespace Sse41
	{
		template <size_t Channels, size_t PixelStride>
		IMAGE_PROCESSING_SSE41_TARGET void resizeRows(const ImageView<true>& source, Rect srcRect, ImageView<false>& dest, const AxisWeights& xWeights,
			const AxisWeights& yWeights, size_t stripWidth, uint64_t destRowBegin, uint64_t destRowEnd);
		// As Avx2's
		[[nodiscard]] size_t longestPaddedXRun(size_t channels) noexcept;
		[[nodiscard]] bool filtersRunTriples(size_t channels) noexcept;
		template <size_t Channels>
		IMAGE_PROCESSING_SSE41_TARGET void premultiplyPixels(uint8_t* dest, const uint8_t* source, size_t pixelCount) noexcept;
		IMAGE_PROCESSING_SSE41_TARGET void planFourBytePixelExpansion(FourBytePixelExpansion& plan, uint64_t srcWidth, uint64_t destWidth);
		IMAGE_PROCESSING_SSE41_TARGET void expandFourBytePixels(uint8_t* destRow, const uint8_t* sourceSpan, const FourBytePixelExpansion& plan) noexcept;
	}
#else
	namespace Neon
	{
		template <size_t Channels, size_t PixelStride>
		void resizeRows(const ImageView<true>& source, Rect srcRect, ImageView<false>& dest, const AxisWeights& xWeights,
			const AxisWeights& yWeights, size_t stripWidth, uint64_t destRowBegin, uint64_t destRowEnd);
		// The longest AxisWeights::paddedRunLength resizeRows takes for the x axis
		[[nodiscard]] size_t longestPaddedXRun(size_t channels) noexcept;
		// Whether resizeRows filters x runs padded to 4 taps three at a time, from AxisWeights::runTriples
		[[nodiscard]] bool filtersRunTriples(size_t channels) noexcept;
		// premultiplyPixelsScalar of tightly packed Channels-byte pixels, Channels 2 or 4, from source to dest; dest may be source
		template <size_t Channels>
		void premultiplyPixels(uint8_t* dest, const uint8_t* source, size_t pixelCount) noexcept;
		void planFourBytePixelExpansion(FourBytePixelExpansion& plan, uint64_t srcWidth, uint64_t destWidth);
		// Requires a non-empty plan from this level; sourceSpan holds the plan's srcWidth pixels
		void expandFourBytePixels(uint8_t* destRow, const uint8_t* sourceSpan, const FourBytePixelExpansion& plan) noexcept;
	}
#endif
}
