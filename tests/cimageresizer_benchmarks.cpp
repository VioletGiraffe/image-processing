#include "3rdparty/catch2/catch.hpp"
#include "resize/cimageresizer.h"

#include "threading/cworkerthread.h"

#include <QImage>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <string>
#include <thread>

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

	// Photo-like content: mostly smooth, with hard edges, plus fine detail. A uniform fill leaves the clamp
	// branches perfectly predicted and every filter tap identical, so it measures a case no real image produces.
	void fillPhotoLikeContent(BenchmarkImage& image)
	{
		constexpr double smoothCycles = 3.5;
		constexpr int smoothAmplitude = 60;
		constexpr uint64_t blocksPerAxis = 24;
		constexpr int blockAmplitude = 48;
		constexpr int channelOffsets[4] = { 0, -20, 20, 10 };

		// Feature sizes are relative to the image, so every scenario gets comparable structure per unit of output
		const auto buildAxisTerms = [](uint64_t size)
			{
				auto terms = std::make_unique_for_overwrite<int[]>(static_cast<size_t>(size));
				for (uint64_t i = 0; i < size; ++i)
				{
					const double phase = 2.0 * std::numbers::pi * smoothCycles * static_cast<double>(i) / static_cast<double>(size);
					terms[i] = static_cast<int>(std::lround(smoothAmplitude * std::sin(phase)));
				}
				return terms;
			};

		const auto rowTerms = buildAxisTerms(image.height);
		const auto columnTerms = buildAxisTerms(image.width);

		// Rounding the block size to a power of two keeps the checkerboard index a shift
		const int blockShiftX = std::bit_width(std::max<uint64_t>(image.width / blocksPerAxis, 1)) - 1;
		const int blockShiftY = std::bit_width(std::max<uint64_t>(image.height / blocksPerAxis, 1)) - 1;

		for (uint64_t y = 0; y < image.height; ++y)
		{
			uint8_t* row = image.data.get() + static_cast<size_t>(y) * image.bytesPerLine;
			for (uint64_t x = 0; x < image.width; ++x)
			{
				const bool blockRaised = ((x >> blockShiftX) + (y >> blockShiftY)) % 2 == 0;
				const int detail = static_cast<int>((x * 37 + y * 71) & 15) - 8;
				const int value = 128 + (rowTerms[y] + columnTerms[x]) / 2 + (blockRaised ? blockAmplitude : -blockAmplitude) + detail;

				uint8_t* pixel = row + static_cast<size_t>(x) * image.pixelStrideBytes;
				for (size_t channel = 0; channel < image.channels; ++channel)
					pixel[channel] = static_cast<uint8_t>(std::clamp(value + channelOffsets[channel], 0, 255));

				// Bytes outside the logical channels carry pixel-format invariants, such as RGB32's required 0xff
				for (size_t byte = image.channels; byte < image.pixelStrideBytes; ++byte)
					pixel[byte] = 0xff;
			}
		}
	}

	void benchmarkResize(
		const char* name,
		uint64_t sourceWidth,
		uint64_t sourceHeight,
		uint64_t destWidth,
		uint64_t destHeight,
		uint8_t channels,
		uint8_t pixelStrideBytes,
		QImage::Format qImageFormat,
		CWorkerThreadPool* threadPool = nullptr)
	{
		BenchmarkImage source(sourceWidth, sourceHeight, channels, pixelStrideBytes);
		fillPhotoLikeContent(source);

		const auto sourceView = source.constView();
		const QImage qImageSource(
			source.data.get(),
			static_cast<int>(sourceWidth),
			static_cast<int>(sourceHeight),
			static_cast<qsizetype>(source.bytesPerLine),
			qImageFormat);
		REQUIRE(!qImageSource.isNull());

		// The destination is reused across iterations: allocating one per iteration would time its first-touch
		// page faults, and those neither shrink with thread count nor belong to the resize.
		BenchmarkImage dest(destWidth, destHeight, channels, pixelStrideBytes);

		BENCHMARK(std::string("CImageResizer | ") + name + (threadPool ? " [multithreaded]" : ""))
		{
			auto destView = dest.mutableView();
			ImageProcessing::resize(destView, sourceView, {}, threadPool);
			return dest.data[dest.dataSize / 2];
		};

		// The serial run of the same scenario provides the QImage control; report_benchmark_ratios.py matches it by stripping the suffix
		if (threadPool)
			return;

		if (sourceWidth == destWidth && sourceHeight == destHeight)
		{
			BENCHMARK(std::string("QImage::copy | ") + name)
			{
				const QImage dest = qImageSource.copy();
				return dest.constBits()[dest.sizeInBytes() / 2];
			};
		}
		else
		{
			BENCHMARK(std::string("QImage::scaled | ") + name)
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

// The two ends of the cost model: hundreds of filter taps per output pixel downscaling, four taps upscaling.
TEST_CASE("Extreme scale factors", "[!benchmark][resize]")
{
	benchmarkResize("4K image to 64x64 - RGB32", 3840, 2160, 64, 64, 3, 4, QImage::Format_RGB32);
	benchmarkResize("64x64 image to 4K - RGB32", 64, 64, 3840, 2160, 3, 4, QImage::Format_RGB32);
}

// Separate from the common scenarios because of the 404 MB source alone
TEST_CASE("Very large image downscale", "[!benchmark][resize]")
{
	benchmarkResize("101 MP photo to 720p - RGB32", 11608, 8708, 1280, 720, 3, 4, QImage::Format_RGB32);
}

TEST_CASE("Parallel resize", "[!benchmark][resize][threading]")
{
	// The resizer bands by pool size, so the pool follows the machine: a fixed count would oversubscribe a small runner.
	CWorkerThreadPool pool(std::max(std::thread::hardware_concurrency(), 2u) - 1, "Resize benchmark pool");
	pool.waitUntilStarted();
	// The scenario names carry no thread count, to keep them comparable across machines
	std::cout << "Parallel resize benchmarks: " << pool.maxWorkersCount() + 1 << " executors\n";

	benchmarkResize("24 MP photo to 1080p viewport - RGB32", 6000, 4000, 1620, 1080, 3, 4, QImage::Format_RGB32, &pool);
	benchmarkResize("4K image to 1080p - RGB32", 3840, 2160, 1920, 1080, 3, 4, QImage::Format_RGB32, &pool);
	benchmarkResize("720p image to 1080p - RGB32", 1280, 720, 1920, 1080, 3, 4, QImage::Format_RGB32, &pool);
	benchmarkResize("1080p image to 1440p - RGB32", 1920, 1080, 2560, 1440, 3, 4, QImage::Format_RGB32, &pool);
	benchmarkResize("4K image to 64x64 - RGB32", 3840, 2160, 64, 64, 3, 4, QImage::Format_RGB32, &pool);
	benchmarkResize("64x64 image to 4K - RGB32", 64, 64, 3840, 2160, 3, 4, QImage::Format_RGB32, &pool);
	benchmarkResize("101 MP photo to 720p - RGB32", 11608, 8708, 1280, 720, 3, 4, QImage::Format_RGB32, &pool);
}
