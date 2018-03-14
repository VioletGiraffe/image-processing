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

private:
	static std::unique_ptr<ImageAdapter> bicubicInterpolation(const ImageAdapter& source, const uint32_t newWidth, const uint32_t newHeight, const AspectRatioPolicy aspectRatio = KeepAspectRatio);
};
