#pragma once

#include "cimageresizer.h"
#include "simd_support.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <memory>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace ImageProcessing::Detail
{
	// One destination coordinate's filter: weights for a contiguous run of source pixels (x axis) or rows (y axis).
	// weights[i] applies to source index firstSource + i: border clamping is folded into the boundary weights at build
	// time, and interior zero weights are kept, so runs have no gaps.
	struct TapRun
	{
		size_t firstSource;
		size_t firstWeight;
		size_t weightCount;
	};

	struct AxisWeights
	{
		struct Run
		{
			size_t firstSource;
			std::span<const float> weights;
		};

		[[nodiscard]] Run runFor(size_t coordinate) const noexcept
		{
			const TapRun& run = runs[coordinate];
			return { run.firstSource, { weights.data() + run.firstWeight, run.weightCount } };
		}

		std::vector<float> weights;
		std::vector<TapRun> runs;
	};

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
		TempRowRing(const AxisWeights& yWeights, uint64_t destRowBegin, uint64_t destRowEnd, size_t rowStride, size_t batchRows);

		// The first source row the band's windows read
		[[nodiscard]] uint64_t firstNeededRow() const noexcept { return _firstNeededRow; }
		[[nodiscard]] size_t rowCapacity() const noexcept { return _rowCount; }

		[[nodiscard]] float* row(uint64_t sourceRow) const noexcept
		{
			return _rows.get() + static_cast<size_t>(sourceRow % _rowCount) * _rowStride;
		}

		// A window is contiguous in the ring except across the wrap, which splits it at most once
		[[nodiscard]] std::array<TempRowSegment, 2> window(uint64_t firstRow, size_t rowCount) const noexcept
		{
			assert(rowCount <= _rowCount);
			const size_t firstSlot = static_cast<size_t>(firstRow % _rowCount);
			const size_t rowsToRingEnd = std::min(rowCount, _rowCount - firstSlot);
			return { { { _rows.get() + firstSlot * _rowStride, rowsToRingEnd }, { _rows.get(), rowCount - rowsToRingEnd } } };
		}

	private:
		std::unique_ptr<float[]> _rows;
		size_t _rowStride;
		size_t _rowCount;
		uint64_t _firstNeededRow;
	};

	[[nodiscard]] inline bool hasStraightAlpha(const ImageView<true>& image) noexcept
	{
		return hasAlphaChannel(image.channels) && image.alphaKind == AlphaKind::Straight;
	}

	[[nodiscard]] inline uint8_t clampToByte(float value) noexcept
	{
		value = std::min(std::max(value, 0.0f), 255.0f);
		return static_cast<uint8_t>(value + 0.5f);
	}

	// With alpha, color is capped at alpha: the output is premultiplied, and negative filter lobes can push color past alpha
	inline void writePixelBytes(uint8_t* destPixel, const float* values, size_t channels) noexcept
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

#if IMAGE_PROCESSING_SIMD
	// Defined in cimageresizer_simd.cpp, which MSVC compiles with /arch:AVX2 so that its 128-bit intrinsics are
	// VEX-encoded too: a legacy SSE encoding stalls for tens of cycles per instruction whenever the process left
	// the upper YMM state dirty, which any AVX-using host does. GCC and Clang get that from the target attribute.
	// The attribute also forbids inlining into the non-AVX2 dispatcher, keeping the runtime check in control of
	// whether these ever execute.
	// Resizes dest rows [destRowBegin, destRowEnd) in one fused pass over a ring of temp rows.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void resizeRows4BytePixelsSimd(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		size_t stripWidth,
		uint8_t pixelTailValue,
		uint64_t destRowBegin,
		uint64_t destRowEnd);
#endif
}
