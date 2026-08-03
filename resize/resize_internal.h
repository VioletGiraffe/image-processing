#pragma once

#include "cimageresizer.h"
#include "simd_support.h"

#include <span>
#include <stddef.h>
#include <stdint.h>
#include <vector>

namespace ImageProcessing::Detail
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

	[[nodiscard]] inline uint8_t clampToByte(float value) noexcept
	{
		if (value <= 0.0f) [[unlikely]]
			return 0;

		if (value >= 255.0f) [[unlikely]]
			return 255;

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
