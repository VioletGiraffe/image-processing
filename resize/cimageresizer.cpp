#include "cimageresizer.h"
#include "cimageinterpolationkernel.h"
#include "assert/advanced_assert.h"
#include "math/math.hpp"

#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <time.h>

struct Size {
	uint32_t width;
	uint32_t height;

	[[nodiscard]] inline Size scaledTo(const Size& target) const {
		const float scaleFactor = std::min((float)target.width / (float)width, (float)target.height / (float)height);
		return *this * scaleFactor;
	}

	[[nodiscard]] inline Size operator*(const float factor) const {
		return {Math::round<uint32_t>((float)width * factor), Math::round<uint32_t>((float)height * factor)};
	}

	[[nodiscard]] inline const Size scaled(const Size& dest) const {
		const float xScaleFactor = (float)width / (float)dest.width;
		const float yScaleFactor = (float)height / (float)dest.height;

		const float actualFactor = (xScaleFactor > 1.0f && yScaleFactor > 1.0f) ? std::max(xScaleFactor, yScaleFactor) : std::min(xScaleFactor, yScaleFactor);

		return *this * (1.0f / actualFactor);
	}
};

[[nodiscard]] inline uint32_t applyKernel(const CImageInterpolationKernelBase<float>& kernel, const ImageAdapter& source, uint32_t x, uint32_t y)
{
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

	const uint32_t srcHeight = source.height(), srcWidth = source.width();
	const uint32_t kernelSize = kernel.size();
	const auto nChannels = source.numChannels();
	if (nChannels == 4)
	{
		for (uint32_t k = y, k_kernel = 0; k < y + kernelSize && k < srcHeight; ++k, ++ k_kernel)
		{
			// TODO: strict aliasing violation!!!
			const auto* line = reinterpret_cast<const uint32_t*>(source.scanLine(k));
			for (uint32_t i = x, i_kernel = 0; i < x + kernelSize && i < srcWidth; ++i, ++i_kernel)
			{
				const auto coeff = kernel.coeff(i_kernel, k_kernel);
				a += static_cast<decltype(coeff)>(line[i] >> 24) * coeff;
				r += static_cast<decltype(coeff)>((line[i] >> 16) & 0xFF) * coeff;
				g += static_cast<decltype(coeff)>((line[i] >> 8) & 0xFF)  * coeff;
				b += static_cast<decltype(coeff)>(line[i] & 0xFF)  * coeff;
			}
		}
	}
	else if (nChannels == 3)
	{
		for (uint32_t k = y, k_kernel = 0; k < y + kernelSize && k < srcHeight; ++k, ++ k_kernel)
		{
			const auto* line = (const uint8_t*)source.scanLine(k);
			for (uint32_t i = x, i_kernel = 0; i < x + kernelSize && i < srcWidth; ++i, ++i_kernel)
			{
				const auto coeff = kernel.coeff(i_kernel, k_kernel);
				r += static_cast<decltype(coeff)>(line[0]) * coeff;
				g += static_cast<decltype(coeff)>(line[1]) * coeff;
				b += static_cast<decltype(coeff)>(line[2]) * coeff;
			}
		}
	}
	else
		assert_and_return_unconditional_r("Unsupported number of channels", 0);

	const uint32_t red = Math::round<uint32_t>(r);
	const uint32_t green = Math::round<uint32_t>(g);
	const uint32_t blue = Math::round<uint32_t>(b);
	const uint32_t alpha = Math::round<uint32_t>(a);

	assert_debug_only(red <= 255 && green <= 255 && blue <= 255);
	return (alpha << 24) | (red << 16) | (green << 8) | blue;
}

[[nodiscard]] std::unique_ptr<ImageAdapter> CImageResizer::bicubicInterpolation(const ImageAdapter& source, const uint32_t newWidth, const uint32_t newHeight, const AspectRatioPolicy /*aspectRatio*/)
{
	if (newWidth == source.width() && newHeight == source.height())
		return source.clone();
	else if (source.numChannels() != 4 && source.numChannels() != 3)
		return source.clone();
	else if (source.bytesPerChannel() != 1)
		return source.clone();

	const Size sourceSize{source.width(), source.height()};

	const Size actualTargetSize{newWidth, newHeight};
	auto dest = source.createSameFormat(actualTargetSize.width, actualTargetSize.height);

	// TODO: refactor this. There's no need to create all the kernels, we're only going to use one.
	//const CLanczosKernel lanczosKernel(sourceSize.width / actualTargetSize.width, 3);
	const CBicubicKernel bicubicKernel(Math::floor<uint32_t>((float)sourceSize.width / (float)actualTargetSize.width), 0.5f);

	const CImageInterpolationKernelBase<float>& kernel = bicubicKernel;

	const uint32_t kernelSize = kernel.size();

	const auto nChannels = source.numChannels();
	for (uint32_t y = 0; y < actualTargetSize.height; ++y)
	{
		auto *currentPixel = static_cast<uint8_t*>(dest->scanLine(y));
		for (uint32_t x = 0; x < actualTargetSize.width; ++x, currentPixel += nChannels)
		{
			const uint32_t pixel = applyKernel(kernel, source, x * kernelSize, y * kernelSize);
			*currentPixel = pixel & 0xFF;
			*(currentPixel + 1) = (pixel >> 8) & 0xFF;
			*(currentPixel + 2) = (pixel >> 16) & 0xFF;
			if (nChannels == 4)
				*(currentPixel + 3) = (pixel >> 24) & 0xFF;
		}
	}

	return dest;
}
