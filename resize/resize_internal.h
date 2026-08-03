#pragma once

#include "cimageresizer.h"
#include "simd_support.h"

#include <algorithm>
#include <span>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace ImageProcessing::Detail
{
	// One destination coordinate's filter: weights for a contiguous run of source pixels (x axis) or rows (y axis).
	// startOffset locates the run's first source element - bytes into a row for x, float elements into the temp
	// buffer for y - and weights[i] applies to the i-th consecutive element after it: border clamping is folded
	// into the boundary weights at build time, and interior zero weights are kept, so runs have no gaps.
	struct TapRun
	{
		size_t startOffset;
		size_t firstWeight;
		size_t weightCount;
	};

	struct AxisWeights
	{
		struct Run
		{
			size_t startOffset;
			std::span<const float> weights;
		};

		[[nodiscard]] Run runFor(size_t coordinate) const noexcept
		{
			const TapRun& run = runs[coordinate];
			return { run.startOffset, { weights.data() + run.firstWeight, run.weightCount } };
		}

		std::vector<float> weights;
		std::vector<TapRun> runs;
	};

	[[nodiscard]] inline uint8_t clampToByte(float value) noexcept
	{
		value = std::min(std::max(value, 0.0f), 255.0f);
		return static_cast<uint8_t>(value + 0.5f);
	}

#if IMAGE_PROCESSING_SIMD
	// Defined in cimageresizer_simd.cpp, which MSVC compiles with /arch:AVX2 so that its 128-bit intrinsics are
	// VEX-encoded too: a legacy SSE encoding stalls for tens of cycles per instruction whenever the process left
	// the upper YMM state dirty, which any AVX-using host does. GCC and Clang get that from the target attribute.
	// The attribute also forbids inlining into the non-AVX2 dispatcher, keeping the runtime check in control of
	// whether these ever execute.
	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void filterHorizontal4BytePixelsSimd(
		float* temp,
		size_t tempRowStride,
		const ImageView<true>& source,
		Rect srcRect,
		uint64_t destWidth,
		const AxisWeights& xWeights,
		uint64_t rowBegin,
		uint64_t rowEnd);

	template <size_t Channels>
	IMAGE_PROCESSING_SIMD_TARGET void filterVerticalRowsSimd(
		const AxisWeights& yWeights,
		const float* temp,
		ImageView<false>& dest,
		uint8_t pixelTailValue,
		uint64_t rowBegin,
		uint64_t rowEnd);
#endif
}
