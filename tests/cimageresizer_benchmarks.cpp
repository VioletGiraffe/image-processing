#include "3rdparty/catch2/catch.hpp"
#include "resize/cimageresizer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

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
			data(static_cast<size_t>(height) * bytesPerLine, 0x7f)
		{
		}

		[[nodiscard]] ImageView<true> constView() const noexcept
		{
			return { width, height, channels, 1, pixelStrideBytes, bytesPerLine, data.data() };
		}

		[[nodiscard]] ImageView<false> mutableView() noexcept
		{
			return { width, height, channels, 1, pixelStrideBytes, bytesPerLine, data.data() };
		}

		uint64_t width;
		uint64_t height;
		uint8_t channels;
		uint8_t pixelStrideBytes;
		size_t bytesPerLine;
		std::vector<uint8_t> data;
	};

	void benchmarkResize(
		const char* name,
		uint64_t sourceWidth,
		uint64_t sourceHeight,
		uint64_t destWidth,
		uint64_t destHeight,
		uint8_t channels,
		uint8_t pixelStrideBytes)
	{
		const BenchmarkImage source(sourceWidth, sourceHeight, channels, pixelStrideBytes);
		BenchmarkImage dest(destWidth, destHeight, channels, pixelStrideBytes);
		const auto sourceView = source.constView();
		auto destView = dest.mutableView();

		BENCHMARK(name)
		{
			ImageProcessing::resize(destView, sourceView);
			return dest.data[dest.data.size() / 2];
		};
	}
}

TEST_CASE("Common display resize scenarios", "[!benchmark][resize]")
{
	benchmarkResize("24 MP photo to 1080p viewport - RGB32", 6000, 4000, 1620, 1080, 3, 4);
	benchmarkResize("4K image to 1080p - RGB32", 3840, 2160, 1920, 1080, 3, 4);
	benchmarkResize("720p image to 1080p - RGB32", 1280, 720, 1920, 1080, 3, 4);
	benchmarkResize("1080p image to 1440p - RGB32", 1920, 1080, 2560, 1440, 3, 4);
	benchmarkResize("1080p image at native size - RGB32", 1920, 1080, 1920, 1080, 3, 4);
}

TEST_CASE("Common pixel layouts", "[!benchmark][resize]")
{
	benchmarkResize("4K to 1080p - Grayscale8", 3840, 2160, 1920, 1080, 1, 1);
	benchmarkResize("4K to 1080p - RGB24", 3840, 2160, 1920, 1080, 3, 3);
	benchmarkResize("4K to 1080p - RGB32", 3840, 2160, 1920, 1080, 3, 4);
	benchmarkResize("4K to 1080p - RGBA32", 3840, 2160, 1920, 1080, 4, 4);
}
