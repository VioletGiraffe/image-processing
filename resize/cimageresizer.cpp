#include "cimageresizer.h"
#include "cimageinterpolationkernel.h"
#include "assert/advanced_assert.h"
#include "math/math.hpp"

#include <math.h>
#include <stdint.h>

[[nodiscard]] inline uint32_t applyKernel(const CImageInterpolationKernelBase<float>& kernel, const CImageResizer::ImageView<>& source, uint32_t x, uint32_t y)
{
	float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;

	const uint32_t srcHeight = source.height, srcWidth = source.width;
	const uint32_t kernelSize = kernel.size();
	if (source.channels == 4)
	{
		for (uint32_t k = y, k_kernel = 0; k < y + kernelSize && k < srcHeight; ++k, ++k_kernel)
		{
			// TODO: strict aliasing violation!!!
			const uint32_t* line = source.scanLine<uint32_t>(k);
			for (uint32_t i = x, i_kernel = 0; i < x + kernelSize && i < srcWidth; ++i, ++i_kernel)
			{
				const uint32_t pixel = line[i];
				const auto coeff = kernel.coeff(i_kernel, k_kernel);
				a += static_cast<decltype(coeff)>(pixel >> 24) * coeff;
				r += static_cast<decltype(coeff)>((pixel >> 16) & 0xFF) * coeff;
				g += static_cast<decltype(coeff)>((pixel >> 8) & 0xFF)  * coeff;
				b += static_cast<decltype(coeff)>(pixel & 0xFF)  * coeff;
			}
		}
	}
	else if (source.channels == 3)
	{
		for (uint32_t k = y, k_kernel = 0; k < y + kernelSize && k < srcHeight; ++k, ++ k_kernel)
		{
			const uint8_t* line = source.scanLine<uint8_t>(k);
			for (uint32_t i = x, i_kernel = 0; i < x + kernelSize && i < srcWidth; ++i, ++i_kernel)
			{
				const uint8_t* pixel = line + i * source.channelStride;
				const auto coeff = kernel.coeff(i_kernel, k_kernel);
				b += static_cast<decltype(coeff)>(pixel[0]) * coeff;
				g += static_cast<decltype(coeff)>(pixel[1]) * coeff;
				r += static_cast<decltype(coeff)>(pixel[2]) * coeff;
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

void CImageResizer::resize(ImageView<false>& dest, const ImageView<>& source, ResizeMethod method)
{
	const CBicubicKernel kernel(Math::floor<uint32_t>((float)source.width / (float)dest.width), 0.5f);

	const uint32_t kernelSize = kernel.size();

	assert_and_return_r(source.channels == dest.channels && source.bytesPerChannel == dest.bytesPerChannel, );
	assert_and_return_r(source.channelStride == dest.channelStride, );
	assert_and_return_r(source.bytesPerChannel == 1, );

	if (source.channels == 3 && source.channelStride == 4)
	{
		for (uint32_t y = 0; y < dest.height; ++y)
		{
			uint32_t* currentPixel = dest.scanLine<uint32_t>(y);
			for (uint32_t x = 0; x < dest.width; ++x, currentPixel += 1)
			{
				const uint32_t pixel = applyKernel(kernel, source, x * kernelSize, y * kernelSize);
				::memcpy(currentPixel, &pixel, 3);
			}
		}
	}
	else if (source.channels == 4)
	{
		for (uint32_t y = 0; y < dest.height; ++y)
		{
			uint32_t* currentPixel = dest.scanLine<uint32_t>(y);
			for (uint32_t x = 0; x < dest.width; ++x, currentPixel += 1)
			{
				const uint32_t pixel = applyKernel(kernel, source, x * kernelSize, y * kernelSize);
				::memcpy(currentPixel, &pixel, 4);
			}
		}
	}
	else if (source.channels == 3 && source.channelStride == 3)
	{
		for (uint32_t y = 0; y < dest.height; ++y)
		{
			std::byte* currentPixel = dest.scanLine<std::byte>(y);
			for (uint32_t x = 0; x < dest.width; ++x, currentPixel += 3)
			{
				const uint32_t pixel = applyKernel(kernel, source, x * kernelSize, y * kernelSize);
				::memcpy(currentPixel, &pixel, 3);
			}
		}
	}
	else if (source.channels == 1)
	{
		for (uint32_t y = 0; y < dest.height; ++y)
		{
			uint8_t* currentPixel = dest.scanLine<uint8_t>(y);
			for (uint32_t x = 0; x < dest.width; ++x, currentPixel += source.channelStride)
			{
				const uint32_t pixel = applyKernel(kernel, source, x * kernelSize, y * kernelSize);
				*currentPixel = static_cast<uint8_t>(pixel & 0xFF);
			}
		}
	}
	else
		assert_and_return_unconditional_r("Unsupported number of channels", );
}
