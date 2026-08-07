#pragma once

#include "resize/cimageresizer.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

#include <assert.h>

// Every source is canonicalized to one of these two formats: 4-byte pixels keep all implementations
// on the resizer's SIMD path, the same layout production feeds it.
[[nodiscard]] inline QImage::Format canonicalFormat(const QImage& image)
{
	return image.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32;
}

namespace Detail
{
	template <bool ConstView, class QImageRef>
	[[nodiscard]] ImageProcessing::ImageView<ConstView> makeView(QImageRef& image)
	{
		const auto format = image.format();
		assert(format == QImage::Format_RGB32 || format == QImage::Format_ARGB32_Premultiplied);

		ImageProcessing::ImageView<ConstView> view;
		view.width = static_cast<uint64_t>(image.width());
		view.height = static_cast<uint64_t>(image.height());
		view.channels = format == QImage::Format_RGB32 ? 3 : 4;
		view.bytesPerChannel = 1;
		view.pixelStrideBytes = 4;
		view.bytesPerLine = static_cast<size_t>(image.bytesPerLine());
		view.data = image.bits();
		return view;
	}
}

[[nodiscard]] inline ImageProcessing::ImageView<true> constView(const QImage& image)
{
	return Detail::makeView<true>(image);
}

// Detaches a shared image, so the view can never alias another QImage's pixels
[[nodiscard]] inline ImageProcessing::ImageView<false> mutableView(QImage& image)
{
	return Detail::makeView<false>(image);
}
