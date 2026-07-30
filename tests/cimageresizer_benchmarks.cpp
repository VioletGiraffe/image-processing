#include "3rdparty/catch2/catch.hpp"
#include "resize/cimageresizer.h"

#include <QImage>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace
{
	using ImageProcessing::ImageView;

	struct BenchmarkImage
	{
		BenchmarkImage(uint64_t imageWidth, uint64_t imageHeight, uint8_t channelCount, uint8_t pixelStride) :
			width(imageWidth),
			height(imageHeight),
			channels(channelCount),
			pixelStrideBytes(pixelStride),
			bytesPerLine(static_cast<size_t>(width) * pixelStrideBytes),
			dataSize(static_cast<size_t>(height) * bytesPerLine),
			data(std::make_unique_for_overwrite<uint8_t[]>(dataSize))
		{
		}

		[[nodiscard]] ImageView<true> constView() const noexcept
		{
			return { width, height, channels, 1, pixelStrideBytes, bytesPerLine, data.get() };
		}

		[[nodiscard]] ImageView<false> mutableView() noexcept
		{
			return { width, height, channels, 1, pixelStrideBytes, bytesPerLine, data.get() };
		}

		uint64_t width;
		uint64_t height;
		uint8_t channels;
		uint8_t pixelStrideBytes;
		size_t bytesPerLine;
		size_t dataSize;
		std::unique_ptr<uint8_t[]> data;
	};

	void benchmarkResize(
		const char* name,
		uint64_t sourceWidth,
		uint64_t sourceHeight,
		uint64_t destWidth,
		uint64_t destHeight,
		uint8_t channels,
		uint8_t pixelStrideBytes,
		QImage::Format qImageFormat)
	{
		BenchmarkImage source(sourceWidth, sourceHeight, channels, pixelStrideBytes);
		std::fill_n(source.data.get(), source.dataSize, uint8_t{ 0x7f });
		if (channels == 3 && pixelStrideBytes == 4)
		{
			for (size_t byte = 3; byte < source.dataSize; byte += 4)
				source.data[byte] = 0xff;
		}

		const auto sourceView = source.constView();
		const QImage qImageSource(
			source.data.get(),
			static_cast<int>(sourceWidth),
			static_cast<int>(sourceHeight),
			static_cast<qsizetype>(source.bytesPerLine),
			qImageFormat);
		REQUIRE(!qImageSource.isNull());

		const std::string resizerBenchmarkName = std::string("CImageResizer | ") + name;
		BENCHMARK(resizerBenchmarkName)
		{
			BenchmarkImage dest(destWidth, destHeight, channels, pixelStrideBytes);
			auto destView = dest.mutableView();
			ImageProcessing::resize(destView, sourceView);
			return dest.data[dest.dataSize / 2];
		};

		if (sourceWidth == destWidth && sourceHeight == destHeight)
		{
			const std::string qImageBenchmarkName = std::string("QImage::copy | ") + name;
			BENCHMARK(qImageBenchmarkName)
			{
				const QImage dest = qImageSource.copy();
				return dest.constBits()[dest.sizeInBytes() / 2];
			};
		}
		else
		{
			const std::string qImageBenchmarkName = std::string("QImage::scaled | ") + name;
			BENCHMARK(qImageBenchmarkName)
			{
				const QImage dest = qImageSource.scaled(
					static_cast<int>(destWidth),
					static_cast<int>(destHeight),
					Qt::IgnoreAspectRatio,
					Qt::SmoothTransformation);
				return dest.constBits()[dest.sizeInBytes() / 2];
			};
		}
	}
}

TEST_CASE("Common display resize scenarios", "[!benchmark][resize]")
{
	benchmarkResize("24 MP photo to 1080p viewport - RGB32", 6000, 4000, 1620, 1080, 3, 4, QImage::Format_RGB32);
	benchmarkResize("4K image to 1080p - RGB32", 3840, 2160, 1920, 1080, 3, 4, QImage::Format_RGB32);
	benchmarkResize("720p image to 1080p - RGB32", 1280, 720, 1920, 1080, 3, 4, QImage::Format_RGB32);
	benchmarkResize("1080p image to 1440p - RGB32", 1920, 1080, 2560, 1440, 3, 4, QImage::Format_RGB32);
	benchmarkResize("1080p image at native size - RGB32", 1920, 1080, 1920, 1080, 3, 4, QImage::Format_RGB32);
}

TEST_CASE("Common pixel layouts", "[!benchmark][resize]")
{
	benchmarkResize("4K to 1080p - Grayscale8", 3840, 2160, 1920, 1080, 1, 1, QImage::Format_Grayscale8);
	benchmarkResize("4K to 1080p - RGB24", 3840, 2160, 1920, 1080, 3, 3, QImage::Format_RGB888);
	benchmarkResize("4K to 1080p - RGB32", 3840, 2160, 1920, 1080, 3, 4, QImage::Format_RGB32);
	benchmarkResize("4K to 1080p - RGBA32", 3840, 2160, 1920, 1080, 4, 4, QImage::Format_RGBA8888);
}
