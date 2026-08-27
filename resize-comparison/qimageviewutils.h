#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "resize/qimage_resize.h"

#include <QImage>
RESTORE_COMPILER_WARNINGS

#include <assert.h>

// Every source is canonicalized to one of these two formats: 4-byte pixels keep all implementations
// on the resizer's SIMD path, the same layout production feeds it.
[[nodiscard]] inline QImage::Format canonicalFormat(const QImage& image)
{
	return image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
}

[[nodiscard]] inline ImageProcessing::ImageView<true> constView(const QImage& image)
{
	assert(image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32_Premultiplied);
	return ImageProcessing::imageView<true>(image);
}

// Detaches a shared image, so the view can never alias another QImage's pixels
[[nodiscard]] inline ImageProcessing::ImageView<false> mutableView(QImage& image)
{
	assert(image.format() == QImage::Format_RGB32 || image.format() == QImage::Format_ARGB32_Premultiplied);
	return ImageProcessing::imageView<false>(image);
}
