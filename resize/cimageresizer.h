#pragma once

#include "imageadapter.hpp"

class CImageResizer
{
public:
	enum ResizeMethod {
		Bicubic
	};

	enum AspectRatioPolicy {
		KeepAspectRatio,
		IgnoreAspectRatio
	};

	[[nodiscard]] static std::unique_ptr<ImageAdapter> bicubicInterpolation(const ImageAdapter& source, uint32_t newWidth, uint32_t newHeight, AspectRatioPolicy aspectRatio = KeepAspectRatio);
};
