#pragma once

#include "resize/cimageresizer.h"

#include <optional>
#include <vector>

// This CPU's SIMD levels, best first: resizes capped at each one cover every kernel the CPU runs
[[nodiscard]] inline std::vector<ImageProcessing::SimdLevel> supportedSimdLevels()
{
	using ImageProcessing::SimdLevel;
	const std::optional<SimdLevel> detected = ImageProcessing::detectedSimdLevel();
	if (!detected)
		return {};

	if (*detected == SimdLevel::Avx2)
		return { SimdLevel::Avx2, SimdLevel::Sse41 };

	return { *detected };
}

// An empty cap stands for the detected level
[[nodiscard]] inline const char* simdCapName(std::optional<ImageProcessing::SimdLevel> simdCap)
{
	return simdCap ? ImageProcessing::simdLevelName(*simdCap) : "detected";
}
