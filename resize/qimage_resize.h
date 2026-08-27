#pragma once

// The Qt bridge for resize(). Header-only, and nothing in this library includes it: a consumer without Qt never sees it.

#include "cimageresizer.h"

#include <QImage>
#include <QRect>

#include <assert.h>
#include <stdint.h>
#include <utility>

namespace ImageProcessing
{
	// Empty data when the pixel format is none of the ones below. A destination must be passed as a non-const QImage.
	template <bool ConstView, class QImageType>
	[[nodiscard]] inline ImageView<ConstView> imageView(QImageType& image)
	{
		ImageView<ConstView> view;
		view.width = static_cast<uint64_t>(image.width());
		view.height = static_cast<uint64_t>(image.height());

		// pixelStrideBytes is the format's whole pixel size: RGB32 and RGBX64 pad theirs beyond the channels they expose.
		switch (image.format())
		{
		case QImage::Format_Grayscale8: [[fallthrough]];
		case QImage::Format_Indexed8:
			view.channels = 1;
			view.bytesPerChannel = 1;
			view.pixelStrideBytes = 1;
			break;
		case QImage::Format_Grayscale16:
			view.channels = 1;
			view.bytesPerChannel = 2;
			view.pixelStrideBytes = 2;
			break;
		case QImage::Format_RGB888:
			view.channels = 3;
			view.bytesPerChannel = 1;
			view.pixelStrideBytes = 3;
			break;
		case QImage::Format_RGB32:
			view.channels = 3;
			view.bytesPerChannel = 1;
			view.pixelStrideBytes = 4;
			break;
		case QImage::Format_ARGB32: [[fallthrough]];
		case QImage::Format_ARGB32_Premultiplied: [[fallthrough]];
		case QImage::Format_RGBA8888: [[fallthrough]];
		case QImage::Format_RGBA8888_Premultiplied:
			view.channels = 4;
			view.bytesPerChannel = 1;
			view.pixelStrideBytes = 4;
			break;
		case QImage::Format_RGBA64:
			view.channels = 4;
			view.bytesPerChannel = 2;
			view.pixelStrideBytes = 8;
			break;
		case QImage::Format_RGBX64:
			view.channels = 3;
			view.bytesPerChannel = 2;
			view.pixelStrideBytes = 8;
			break;
		default:
			view.data = nullptr;
			view.width = view.height = 0;
			return view;
		}

		view.bytesPerLine = static_cast<size_t>(image.bytesPerLine());

		if constexpr (ConstView)
			view.data = std::as_const(image).bits(); // Never detaches, whatever the caller passed
		else
			view.data = image.bits();                // Detaches, so a resize cannot write into a buffer another QImage shares

		assert(view.bytesPerLine >= view.width * view.pixelStrideBytes);

		return view;
	}

	// False when either format has no view: the caller picks the fallback, QImage::scaled typically.
	// dest arrives at the target size; an empty srcRect means the whole source.
	[[nodiscard]] inline bool resize(QImage& dest, const QImage& source, const QRect& srcRect = {}, const ParallelForFn& parallelFor = {}, ResizeKernel kernel = ResizeKernel::Auto)
	{
		assert(srcRect.isEmpty() || source.rect().contains(srcRect));

		const auto sourceView = imageView<true>(source);
		if (!sourceView.data)
			return false;

		auto destView = imageView<false>(dest);
		if (!destView.data)
			return false;

		const Rect rect{
			static_cast<uint64_t>(srcRect.x()),
			static_cast<uint64_t>(srcRect.y()),
			static_cast<uint64_t>(srcRect.width()),
			static_cast<uint64_t>(srcRect.height())
		};

		resize(destView, sourceView, rect, parallelFor, kernel);
		return true;
	}
}
