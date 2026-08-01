#include "3rdparty/catch2/catch.hpp"
#include "resize/cimageresizer.h"

#include "threading/cworkerthread.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <random>
#include <utility>
#include <vector>

namespace
{
	using ImageProcessing::ImageView;
	using ImageProcessing::Rect;

	struct TestImage
	{
		TestImage(uint64_t imageWidth, uint64_t imageHeight, uint8_t channelCount, uint8_t pixelStride, size_t rowPadding = 0, uint8_t initialValue = 0) :
			width(imageWidth),
			height(imageHeight),
			channels(channelCount),
			pixelStrideBytes(pixelStride),
			bytesPerLine(static_cast<size_t>(width) * pixelStrideBytes + rowPadding),
			data(static_cast<size_t>(height) * bytesPerLine, initialValue)
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

		[[nodiscard]] uint8_t* pixel(uint64_t x, uint64_t y) noexcept
		{
			return data.data() + static_cast<size_t>(y) * bytesPerLine + static_cast<size_t>(x) * pixelStrideBytes;
		}

		[[nodiscard]] const uint8_t* pixel(uint64_t x, uint64_t y) const noexcept
		{
			return data.data() + static_cast<size_t>(y) * bytesPerLine + static_cast<size_t>(x) * pixelStrideBytes;
		}

		uint64_t width;
		uint64_t height;
		uint8_t channels;
		uint8_t pixelStrideBytes;
		size_t bytesPerLine;
		std::vector<uint8_t> data;
	};

	void resize(TestImage& dest, const TestImage& source, Rect sourceRect = {}, CWorkerThreadPool* threadPool = nullptr)
	{
		auto destView = dest.mutableView();
		const auto sourceView = source.constView();
		ImageProcessing::resize(destView, sourceView, sourceRect, threadPool);
	}

	void setPixel(TestImage& image, uint64_t x, uint64_t y, std::initializer_list<uint8_t> values)
	{
		REQUIRE(values.size() == static_cast<size_t>(image.pixelStrideBytes));
		std::copy(values.begin(), values.end(), image.pixel(x, y));
	}

	// Byte comparisons are promoted with unary + throughout this file: Catch2 renders uint8_t as a character.
	void requirePixel(const TestImage& image, uint64_t x, uint64_t y, std::initializer_list<uint8_t> expected)
	{
		CAPTURE(x, y);
		REQUIRE(expected.size() == static_cast<size_t>(image.pixelStrideBytes));

		size_t byte = 0;
		for (const uint8_t expectedValue : expected)
		{
			CAPTURE(byte);
			CHECK(+image.pixel(x, y)[byte] == +expectedValue);
			++byte;
		}
	}

	void setPixels(TestImage& image, std::initializer_list<uint8_t> values)
	{
		REQUIRE(values.size() == static_cast<size_t>(image.width) * image.height * image.pixelStrideBytes);

		auto value = values.begin();
		for (uint64_t y = 0; y < image.height; ++y)
		{
			for (uint64_t x = 0; x < image.width; ++x)
			{
				for (size_t byte = 0; byte < image.pixelStrideBytes; ++byte)
					image.pixel(x, y)[byte] = *value++;
			}
		}
	}

	void requirePixels(const TestImage& image, std::initializer_list<uint8_t> expected)
	{
		REQUIRE(expected.size() == static_cast<size_t>(image.width) * image.height * image.pixelStrideBytes);

		auto expectedValue = expected.begin();
		for (uint64_t y = 0; y < image.height; ++y)
		{
			for (uint64_t x = 0; x < image.width; ++x)
			{
				for (size_t byte = 0; byte < image.pixelStrideBytes; ++byte)
				{
					CAPTURE(x, y, byte);
					CHECK(+image.pixel(x, y)[byte] == +*expectedValue++);
				}
			}
		}
	}

	void requirePixelsEqual(const TestImage& actual, const TestImage& expected)
	{
		REQUIRE(actual.width == expected.width);
		REQUIRE(actual.height == expected.height);
		REQUIRE(+actual.channels == +expected.channels);
		REQUIRE(+actual.pixelStrideBytes == +expected.pixelStrideBytes);

		for (uint64_t y = 0; y < actual.height; ++y)
		{
			for (uint64_t x = 0; x < actual.width; ++x)
			{
				for (size_t byte = 0; byte < actual.pixelStrideBytes; ++byte)
				{
					CAPTURE(x, y, byte);
					CHECK(+actual.pixel(x, y)[byte] == +expected.pixel(x, y)[byte]);
				}
			}
		}
	}

	TestImage tightlyPackedCrop(const TestImage& source, Rect sourceRect)
	{
		TestImage crop(sourceRect.w, sourceRect.h, source.channels, source.pixelStrideBytes);
		for (uint64_t y = 0; y < sourceRect.h; ++y)
		{
			for (uint64_t x = 0; x < sourceRect.w; ++x)
				std::copy_n(source.pixel(sourceRect.left + x, sourceRect.top + y), source.pixelStrideBytes, crop.pixel(x, y));
		}
		return crop;
	}

	uint64_t randomInRange(std::mt19937& randomEngine, uint64_t minimum, uint64_t maximum)
	{
		return std::uniform_int_distribution<uint64_t>(minimum, maximum)(randomEngine);
	}

	void fillLogicalBytes(TestImage& image, std::mt19937& randomEngine)
	{
		std::uniform_int_distribution<unsigned int> byteDistribution(0, 255);
		for (uint64_t y = 0; y < image.height; ++y)
		{
			for (uint64_t x = 0; x < image.width; ++x)
			{
				for (size_t byte = 0; byte < image.pixelStrideBytes; ++byte)
					image.pixel(x, y)[byte] = static_cast<uint8_t>(byteDistribution(randomEngine));
			}
		}
	}

	// Every logical channel gets the same value, so any layout's channel 0 matches the grayscale image of that size
	void fillDeterministicPattern(TestImage& image)
	{
		for (uint64_t y = 0; y < image.height; ++y)
		{
			for (uint64_t x = 0; x < image.width; ++x)
			{
				uint8_t* pixel = image.pixel(x, y);
				std::fill_n(pixel, image.channels, static_cast<uint8_t>((x * 37 + y * 71 + (x * y % 251) * 19 + 23) & 0xff));
				std::fill_n(pixel + image.channels, image.pixelStrideBytes - image.channels, uint8_t{ 0xff });
			}
		}
	}

	TestImage mirrorBothAxes(const TestImage& source)
	{
		TestImage result(source.width, source.height, source.channels, source.pixelStrideBytes);
		for (uint64_t y = 0; y < source.height; ++y)
		{
			for (uint64_t x = 0; x < source.width; ++x)
				std::copy_n(source.pixel(x, y), source.pixelStrideBytes, result.pixel(source.width - x - 1, source.height - y - 1));
		}
		return result;
	}

	TestImage extractChannel(const TestImage& source, uint8_t channel)
	{
		TestImage result(source.width, source.height, 1, 1);
		for (uint64_t y = 0; y < source.height; ++y)
		{
			for (uint64_t x = 0; x < source.width; ++x)
				result.pixel(x, y)[0] = source.pixel(x, y)[channel];
		}
		return result;
	}

	TestImage grayscaleGrid(uint64_t width, uint64_t height)
	{
		TestImage image(width, height, 1, 1);
		for (uint64_t y = 0; y < height; ++y)
		{
			for (uint64_t x = 0; x < width; ++x)
				image.pixel(x, y)[0] = static_cast<uint8_t>(y * 10 + x);
		}
		return image;
	}

	struct ReferenceTap
	{
		uint64_t coordinate;
		double weight;
	};

	using ReferenceAxisWeights = std::vector<std::vector<ReferenceTap>>;

	[[nodiscard]] double referenceSinc(double x) noexcept
	{
		if (x == 0.0)
			return 1.0;

		const double px = std::numbers::pi * x;
		return std::sin(px) / px;
	}

	[[nodiscard]] double evaluateReferenceBicubic(double x) noexcept
	{
		x = std::abs(x);
		constexpr double a = -0.5;
		if (x < 1.0)
			return ((a + 2.0) * x - (a + 3.0)) * x * x + 1.0;
		if (x < 2.0)
			return (((a * x - 5.0 * a) * x + 8.0 * a) * x - 4.0 * a);

		return 0.0;
	}

	[[nodiscard]] double evaluateReferenceLanczos3(double x) noexcept
	{
		x = std::abs(x);
		constexpr double radius = 3.0;
		if (x == 0.0)
			return 1.0;
		if (x >= radius)
			return 0.0;

		return referenceSinc(x) * referenceSinc(x / radius);
	}

	[[nodiscard]] ReferenceAxisWeights buildReferenceAxisWeights(uint64_t sourceSize, uint64_t destSize)
	{
		ReferenceAxisWeights result;
		result.reserve(destSize);

		if (sourceSize == 1)
		{
			for (uint64_t destCoordinate = 0; destCoordinate < destSize; ++destCoordinate)
				result.push_back({ ReferenceTap{ 0, 1.0 } });
			return result;
		}

		const double scale = static_cast<double>(destSize) / static_cast<double>(sourceSize);
		const bool downscale = scale < 1.0;
		const double radius = downscale ? 3.0 : 2.0;
		const double support = downscale ? radius / scale : radius;
		const int64_t sourceMax = static_cast<int64_t>(sourceSize) - 1;

		for (uint64_t destCoordinate = 0; destCoordinate < destSize; ++destCoordinate)
		{
			std::vector<ReferenceTap> taps;
			const double sourcePosition = (static_cast<double>(destCoordinate) + 0.5) / scale - 0.5;
			const int64_t left = static_cast<int64_t>(std::floor(sourcePosition - support));
			const int64_t right = static_cast<int64_t>(std::ceil(sourcePosition + support));
			taps.reserve(static_cast<size_t>(right - left + 1));

			double weightSum = 0.0;
			for (int64_t sourceCoordinate = left; sourceCoordinate <= right; ++sourceCoordinate)
			{
				const double distance = sourcePosition - static_cast<double>(sourceCoordinate);
				const double weight = downscale
					? evaluateReferenceLanczos3(distance * scale) * scale
					: evaluateReferenceBicubic(distance);
				taps.push_back({ static_cast<uint64_t>(std::clamp(sourceCoordinate, int64_t{ 0 }, sourceMax)), weight });
				weightSum += weight;
			}

			if (weightSum != 0.0)
			{
				for (ReferenceTap& tap : taps)
					tap.weight /= weightSum;
			}
			else
			{
				taps.clear();
				taps.push_back({ static_cast<uint64_t>(std::clamp(static_cast<int64_t>(std::lround(sourcePosition)), int64_t{ 0 }, sourceMax)), 1.0 });
			}

			result.push_back(std::move(taps));
		}

		return result;
	}

	// A direct 2D convolution keeps the test oracle structurally independent from the production two-pass implementation.
	[[nodiscard]] std::vector<double> referenceResizeSingleChannel(const TestImage& source, uint64_t destWidth, uint64_t destHeight)
	{
		const ReferenceAxisWeights xWeights = buildReferenceAxisWeights(source.width, destWidth);
		const ReferenceAxisWeights yWeights = buildReferenceAxisWeights(source.height, destHeight);
		std::vector<double> result(static_cast<size_t>(destWidth) * destHeight);

		for (uint64_t destY = 0; destY < destHeight; ++destY)
		{
			for (uint64_t destX = 0; destX < destWidth; ++destX)
			{
				double value = 0.0;
				for (const ReferenceTap& yTap : yWeights[destY])
				{
					for (const ReferenceTap& xTap : xWeights[destX])
						value += static_cast<double>(source.pixel(xTap.coordinate, yTap.coordinate)[0]) * xTap.weight * yTap.weight;
				}
				result[static_cast<size_t>(destY) * destWidth + destX] = value;
			}
		}

		return result;
	}

	[[nodiscard]] uint8_t roundReferenceToByte(double value) noexcept
	{
		const int64_t rounded = static_cast<int64_t>(std::round(value));
		return static_cast<uint8_t>(std::clamp(rounded, int64_t{ 0 }, int64_t{ 255 }));
	}

	// Channels are filtered independently, so one extracted channel against the single-channel oracle is a complete check.
	void requireChannelMatchesReference(const TestImage& actual, const TestImage& source, uint8_t channel)
	{
		CAPTURE(+channel);
		const std::vector<double> reference = referenceResizeSingleChannel(extractChannel(source, channel), actual.width, actual.height);

		for (uint64_t y = 0; y < actual.height; ++y)
		{
			for (uint64_t x = 0; x < actual.width; ++x)
			{
				const double referenceValue = reference[static_cast<size_t>(y) * actual.width + x];
				// int, not uint8_t: Catch2 prints byte types as characters
				const int expected = roundReferenceToByte(referenceValue);
				const int actualValue = actual.pixel(x, y)[channel];
				const double distanceToRoundingBoundary = std::abs(referenceValue - (std::floor(referenceValue) + 0.5));
				const int difference = actualValue - expected;
				const int absoluteDifference = std::max(difference, -difference);
				const int allowedDifference = distanceToRoundingBoundary < 0.01 ? 1 : 0;

				if (absoluteDifference > allowedDifference)
				{
					CAPTURE(source.width, source.height, actual.width, actual.height, x, y, referenceValue, expected, actualValue, distanceToRoundingBoundary);
					FAIL("Resize result differs from the double-precision reference");
				}
			}
		}
	}

	void requireResizeMatchesReference(const TestImage& actual, const TestImage& source)
	{
		REQUIRE(+actual.channels == +source.channels);

		for (uint8_t channel = 0; channel < source.channels; ++channel)
			requireChannelMatchesReference(actual, source, channel);
	}

	void requireNearUnityResizeMatchesReference(uint64_t sourceWidth, uint64_t sourceHeight, uint64_t destWidth, uint64_t destHeight)
	{
		TestImage source(sourceWidth, sourceHeight, 1, 1);
		fillDeterministicPattern(source);

		TestImage actual(destWidth, destHeight, 1, 1);
		resize(actual, source);
		requireResizeMatchesReference(actual, source);
	}

	struct ResizeJob { uint64_t srcWidth, srcHeight, destWidth, destHeight; };

	// Shared by the reference and threading test cases so both cover the same geometry space
	constexpr ResizeJob resizeJobs[] = {
		// Downscales
		{ 5472, 3648, 1620, 1080 },   // 20 MP photo to a viewport
		{ 3840, 2160, 1920, 1080 },
		{ 640, 480, 333, 257 },
		// Upscales
		{ 1280, 720, 3840, 2160 },
		{ 640, 480, 1280, 963 },
		{ 2, 2, 2048, 2048 },
		{ 1, 1, 2000, 2000 },
		// Mixed axes
		{ 3840, 1080, 1920, 2160 },   // X down, Y up
		{ 640, 480, 900, 200 },       // X up, Y down
		// Near-unity, where a one-pixel change in size shifts every tap by a fraction of a pixel
		{ 1920, 1080, 1921, 1081 },
		{ 1921, 1081, 1920, 1080 },
		// Degenerate strips and extreme scale factors
		{ 8192, 1, 4096, 3 },
		{ 1, 8192, 3, 4096 },
		{ 1024, 1024, 1, 1 },
		// Small enough for the threading test to fall back to serial despite the pool
		{ 640, 480, 16, 12 },
	};

	struct PixelLayout { uint8_t channels; uint8_t pixelStride; };

	// 4/4 and 3/4 take the SIMD path where available, 3/3 and 1/1 the scalar one, 2/2 the runtime fallback
	constexpr PixelLayout pixelLayouts[] = { { 4, 4 }, { 3, 4 }, { 3, 3 }, { 1, 1 }, { 2, 2 } };
}

TEST_CASE("Bicubic upscaling matches independently generated golden pixels", "[resize][bicubic][golden]")
{
	TestImage source(3, 3, 1, 1);
	setPixels(source, {
		111, 129, 175,
		56, 69, 178,
		234, 163, 94
	});

	TestImage dest(6, 5, 1, 1);
	resize(dest, source);

	// Generated by a separate double-precision Catmull-Rom reference implementation; every unclamped result is over 0.1 from a rounding boundary.
	requirePixels(dest, {
		113, 117, 126, 143, 167, 178,
		80, 84, 92, 119, 165, 186,
		55, 56, 59, 92, 156, 186,
		167, 156, 133, 122, 125, 126,
		251, 232, 190, 147, 103, 83
	});
	requireResizeMatchesReference(dest, source);
}

TEST_CASE("Lanczos downscaling matches independently generated golden pixels", "[resize][lanczos][golden]")
{
	TestImage source(8, 7, 1, 1);
	setPixels(source, {
		0, 255, 17, 255, 211, 211, 0, 43,
		159, 255, 96, 211, 238, 0, 17, 159,
		43, 17, 255, 0, 255, 211, 0, 159,
		159, 0, 255, 17, 43, 255, 211, 238,
		255, 0, 0, 17, 255, 0, 159, 211,
		159, 17, 255, 0, 159, 96, 255, 96,
		96, 43, 211, 43, 255, 17, 159, 0
	});

	TestImage dest(3, 3, 1, 1);
	resize(dest, source);

	// Generated by a separate double-precision Lanczos-3 reference implementation; every result is over 0.22 from a rounding boundary.
	requirePixels(dest, {
		130, 190, 76,
		101, 102, 185,
		105, 115, 124
	});
	requireResizeMatchesReference(dest, source);
}

TEST_CASE("Constant images remain constant when resized", "[resize]")
{
	TestImage source(3, 2, 3, 3, 2, 0xcc);
	for (uint64_t y = 0; y < source.height; ++y)
	{
		for (uint64_t x = 0; x < source.width; ++x)
			setPixel(source, x, y, { 17, 91, 203 });
	}

	SECTION("Upscale")
	{
		constexpr uint8_t paddingSentinel = 0xa5;
		TestImage dest(7, 5, 3, 3, 3, paddingSentinel);
		resize(dest, source);

		for (uint64_t y = 0; y < dest.height; ++y)
		{
			for (uint64_t x = 0; x < dest.width; ++x)
				requirePixel(dest, x, y, { 17, 91, 203 });

			for (size_t byte = static_cast<size_t>(dest.width) * dest.pixelStrideBytes; byte < dest.bytesPerLine; ++byte)
				CHECK(+dest.data[static_cast<size_t>(y) * dest.bytesPerLine + byte] == +paddingSentinel);
		}
	}

	SECTION("Downscale")
	{
		TestImage dest(1, 1, 3, 3);
		resize(dest, source);
		requirePixel(dest, 0, 0, { 17, 91, 203 });
	}

	SECTION("Four channels")
	{
		TestImage rgbaSource(2, 2, 4, 4);
		for (uint64_t y = 0; y < rgbaSource.height; ++y)
		{
			for (uint64_t x = 0; x < rgbaSource.width; ++x)
				setPixel(rgbaSource, x, y, { 11, 22, 33, 44 });
		}

		TestImage dest(11, 5, 4, 4);
		resize(dest, rgbaSource);
		for (uint64_t y = 0; y < dest.height; ++y)
		{
			for (uint64_t x = 0; x < dest.width; ++x)
				requirePixel(dest, x, y, { 11, 22, 33, 44 });
		}
	}
}

TEST_CASE("Filtered resize initializes bytes outside the logical channels", "[resize][pixel-layout]")
{
	SECTION("Specialized RGB32 layout")
	{
		TestImage source(2, 2, 3, 4);
		TestImage packedSource(2, 2, 3, 3);
		setPixel(source, 0, 0, { 10, 20, 30, 0xff });
		setPixel(source, 1, 0, { 40, 50, 60, 0xff });
		setPixel(source, 0, 1, { 70, 80, 90, 0xff });
		setPixel(source, 1, 1, { 100, 110, 120, 0xff });
		setPixel(packedSource, 0, 0, { 10, 20, 30 });
		setPixel(packedSource, 1, 0, { 40, 50, 60 });
		setPixel(packedSource, 0, 1, { 70, 80, 90 });
		setPixel(packedSource, 1, 1, { 100, 110, 120 });

		TestImage dest(13, 3, 3, 4, 0, 0);
		TestImage packedDest(13, 3, 3, 3, 0, 0);
		resize(dest, source);
		resize(packedDest, packedSource);

		for (uint64_t y = 0; y < dest.height; ++y)
		{
			for (uint64_t x = 0; x < dest.width; ++x)
			{
				for (size_t channel = 0; channel < 3; ++channel)
				{
					const int difference = static_cast<int>(dest.pixel(x, y)[channel]) - static_cast<int>(packedDest.pixel(x, y)[channel]);
					CHECK(std::max(difference, -difference) <= 1);
				}
				CHECK(+dest.pixel(x, y)[3] == 0xff);
			}
		}
	}

	SECTION("Runtime channel-count path")
	{
		TestImage source(2, 2, 2, 4);
		for (uint64_t y = 0; y < source.height; ++y)
		{
			for (uint64_t x = 0; x < source.width; ++x)
				setPixel(source, x, y, { 30, 200, 0xa5, 0x5a });
		}

		TestImage dest(1, 1, 2, 4, 0, 0);
		resize(dest, source);
		requirePixel(dest, 0, 0, { 30, 200, 0xa5, 0x5a });
	}
}

TEST_CASE("Scaled crops match equivalent tightly packed images", "[resize][source-rect]")
{
	auto requireCropEquivalence = [](uint8_t channels, uint8_t pixelStride)
	{
		CAPTURE(channels, pixelStride);
		TestImage source(6, 5, channels, pixelStride, 5, 0xe1);
		for (uint64_t y = 0; y < source.height; ++y)
		{
			for (uint64_t x = 0; x < source.width; ++x)
			{
				for (size_t byte = 0; byte < pixelStride; ++byte)
					source.pixel(x, y)[byte] = static_cast<uint8_t>((x * 37 + y * 71 + byte * 53 + 19) & 0xff);
			}
		}

		const Rect sourceRect{ 2, 1, 3, 3 };
		const TestImage packedCrop = tightlyPackedCrop(source, sourceRect);
		TestImage croppedResult(5, 2, channels, pixelStride, 3, 0x1c);
		TestImage packedResult(5, 2, channels, pixelStride, 1, 0xe3);

		resize(croppedResult, source, sourceRect);
		resize(packedResult, packedCrop);
		requirePixelsEqual(croppedResult, packedResult);
	};

	SECTION("Specialized channel-count path")
	{
		requireCropEquivalence(3, 4);
	}

	SECTION("Runtime channel-count path")
	{
		requireCropEquivalence(2, 4);
	}
}

TEST_CASE("Identity crops copy only logical row bytes", "[resize][source-rect][pixel-layout]")
{
	constexpr size_t guardSize = 8;
	constexpr uint8_t sourceGuard = 0xd3;
	constexpr uint8_t destGuard = 0x5a;
	constexpr uint8_t sourcePadding = 0xe1;
	constexpr uint8_t destPadding = 0xa5;
	constexpr uint64_t sourceWidth = 4;
	constexpr uint64_t sourceHeight = 3;
	constexpr uint64_t destWidth = 2;
	constexpr uint64_t destHeight = 2;
	constexpr uint8_t channels = 3;
	constexpr uint8_t pixelStride = 4;
	constexpr size_t sourceBytesPerLine = sourceWidth * pixelStride + 2;
	constexpr size_t destBytesPerLine = destWidth * pixelStride + 5;
	constexpr size_t sourceBytes = sourceHeight * sourceBytesPerLine;
	constexpr size_t destBytes = destHeight * destBytesPerLine;

	std::vector<uint8_t> sourceStorage(guardSize + sourceBytes + guardSize, sourceGuard);
	auto* sourceData = sourceStorage.data() + guardSize;
	std::fill_n(sourceData, sourceBytes, sourcePadding);
	for (uint64_t y = 0; y < sourceHeight; ++y)
	{
		for (uint64_t x = 0; x < sourceWidth; ++x)
		{
			auto* pixel = sourceData + static_cast<size_t>(y) * sourceBytesPerLine + static_cast<size_t>(x) * pixelStride;
			for (size_t byte = 0; byte < pixelStride; ++byte)
				pixel[byte] = static_cast<uint8_t>(y * 64 + x * 11 + byte * 3);
		}
	}
	const std::vector<uint8_t> originalSourceStorage = sourceStorage;

	std::vector<uint8_t> destStorage(guardSize + destBytes + guardSize, destGuard);
	auto* destData = destStorage.data() + guardSize;
	std::fill_n(destData, destBytes, destPadding);

	const ImageView<true> sourceView{ sourceWidth, sourceHeight, channels, 1, pixelStride, sourceBytesPerLine, sourceData };
	ImageView<false> destView{ destWidth, destHeight, channels, 1, pixelStride, destBytesPerLine, destData };
	ImageProcessing::resize(destView, sourceView, Rect{ 2, 1, destWidth, destHeight });

	for (uint64_t y = 0; y < destHeight; ++y)
	{
		for (uint64_t x = 0; x < destWidth; ++x)
		{
			for (size_t byte = 0; byte < pixelStride; ++byte)
			{
				CAPTURE(x, y, byte);
				const auto* expectedPixel = sourceData + static_cast<size_t>(y + 1) * sourceBytesPerLine + static_cast<size_t>(x + 2) * pixelStride;
				CHECK(+destData[static_cast<size_t>(y) * destBytesPerLine + static_cast<size_t>(x) * pixelStride + byte] == +expectedPixel[byte]);
			}
		}

		for (size_t byte = destWidth * pixelStride; byte < destBytesPerLine; ++byte)
		{
			CAPTURE(y, byte);
			CHECK(+destData[static_cast<size_t>(y) * destBytesPerLine + byte] == +destPadding);
		}
	}

	CHECK(sourceStorage == originalSourceStorage);
	CHECK(std::all_of(destStorage.begin(), destStorage.begin() + guardSize, [](uint8_t value) { return value == destGuard; }));
	CHECK(std::all_of(destStorage.end() - guardSize, destStorage.end(), [](uint8_t value) { return value == destGuard; }));
}

TEST_CASE("Source rectangles are normalized per axis without unsigned overflow", "[resize][source-rect]")
{
	const TestImage source = grayscaleGrid(4, 4);

	auto requireBottomRightCrop = [&source](Rect sourceRect)
	{
		TestImage dest(2, 2, 1, 1);
		resize(dest, source, sourceRect);
		requirePixel(dest, 0, 0, { 12 });
		requirePixel(dest, 1, 0, { 13 });
		requirePixel(dest, 0, 1, { 22 });
		requirePixel(dest, 1, 1, { 23 });
	};

	SECTION("An X overrun does not move a valid Y coordinate")
	{
		requireBottomRightCrop(Rect{ 3, 1, 2, 2 });
	}

	SECTION("A wrapped right edge is clamped safely")
	{
		requireBottomRightCrop(Rect{ std::numeric_limits<uint64_t>::max(), 1, 2, 2 });
	}

	SECTION("A Y overrun does not move a valid X coordinate")
	{
		TestImage dest(2, 2, 1, 1);
		resize(dest, source, Rect{ 1, 3, 2, 2 });
		requirePixel(dest, 0, 0, { 21 });
		requirePixel(dest, 1, 0, { 22 });
		requirePixel(dest, 0, 1, { 31 });
		requirePixel(dest, 1, 1, { 32 });
	}

	SECTION("Oversized dimensions are reduced before clamping the origin")
	{
		TestImage dest(4, 2, 1, 1);
		resize(dest, source, Rect{ 3, 1, std::numeric_limits<uint64_t>::max(), 2 });

		for (uint64_t y = 0; y < dest.height; ++y)
		{
			for (uint64_t x = 0; x < dest.width; ++x)
				requirePixel(dest, x, y, { static_cast<uint8_t>((y + 1) * 10 + x) });
		}
	}
}

TEST_CASE("A one-pixel source axis is replicated", "[resize]")
{
	TestImage source(1, 2, 1, 1);
	setPixel(source, 0, 0, { 10 });
	setPixel(source, 0, 1, { 200 });

	TestImage dest(5, 2, 1, 1);
	resize(dest, source);

	for (uint64_t x = 0; x < dest.width; ++x)
	{
		requirePixel(dest, x, 0, { 10 });
		requirePixel(dest, x, 1, { 200 });
	}
}

TEST_CASE("A one-pixel source image replicates every pixel byte", "[resize][pixel-layout]")
{
	TestImage source(1, 1, 3, 6);
	setPixel(source, 0, 0, { 17, 83, 201, 0xa5, 0x5a, 0xff });

	TestImage dest(7, 3, 3, 6, 2, 0);
	resize(dest, source);

	for (uint64_t y = 0; y < dest.height; ++y)
	{
		for (uint64_t x = 0; x < dest.width; ++x)
			requirePixel(dest, x, y, { 17, 83, 201, 0xa5, 0x5a, 0xff });
	}
}

TEST_CASE("Near-unity scaling matches a direct double-precision reference", "[resize][reference]")
{
	SECTION("255 to 256 upscale")
	{
		requireNearUnityResizeMatchesReference(255, 31, 256, 31);
	}

	SECTION("256 to 255 downscale")
	{
		requireNearUnityResizeMatchesReference(256, 31, 255, 31);
	}

	SECTION("Mixed-axis upscale and downscale")
	{
		requireNearUnityResizeMatchesReference(255, 256, 256, 255);
	}
}

// Structured rather than random content, at ratios between the near-unity cases and the degenerate ones.
TEST_CASE("Extreme scale factors match a direct double-precision reference", "[resize][reference]")
{
	constexpr ResizeJob jobs[] = {
		{ 400, 300, 60, 45 },     // heavy downscale: widest Lanczos window
		{ 320, 240, 160, 120 },   // exact halving
		{ 96, 72, 300, 220 },     // upscale (bicubic)
		{ 300, 200, 90, 260 },    // X down, Y up
	};

	for (const ResizeJob& job : jobs)
	{
		CAPTURE(job.srcWidth, job.srcHeight, job.destWidth, job.destHeight);
		TestImage source(job.srcWidth, job.srcHeight, 1, 1);
		fillDeterministicPattern(source);

		TestImage actual(job.destWidth, job.destHeight, 1, 1);
		resize(actual, source);
		requireResizeMatchesReference(actual, source);
	}
}

// The only reference check covering the SIMD kernels, large sources and degenerate aspect ratios. Random bytes
// give every channel different data, so a channel mix-up cannot hide behind identical values.
TEST_CASE("Every pixel layout and geometry matches a direct double-precision reference", "[resize][reference]")
{
	std::mt19937 randomEngine(20260802);

	for (const auto [channels, pixelStride] : pixelLayouts)
	{
		CAPTURE(+channels, +pixelStride);
		for (const ResizeJob& job : resizeJobs)
		{
			CAPTURE(job.srcWidth, job.srcHeight, job.destWidth, job.destHeight);
			TestImage source(job.srcWidth, job.srcHeight, channels, pixelStride);
			fillLogicalBytes(source, randomEngine);

			TestImage dest(job.destWidth, job.destHeight, channels, pixelStride);
			resize(dest, source);
			requireResizeMatchesReference(dest, source);
		}
	}
}

// The other layout comparisons use two-pixel sources; these sizes cover many blocked SIMD iterations and a
// different scalar remainder, across the SIMD, scalar and runtime-dispatch paths.
TEST_CASE("Pixel layouts agree on identical channel data", "[resize][pixel-layout]")
{
	constexpr uint64_t sourceWidth = 320, sourceHeight = 240;
	TestImage grayscaleSource(sourceWidth, sourceHeight, 1, 1);
	fillDeterministicPattern(grayscaleSource);

	struct Layout { uint8_t channels; uint8_t pixelStride; };
	for (const auto [destWidth, destHeight] : { std::pair<uint64_t, uint64_t>{ 77, 51 }, { 480, 361 } })
	{
		CAPTURE(destWidth, destHeight);
		TestImage grayscaleDest(destWidth, destHeight, 1, 1);
		resize(grayscaleDest, grayscaleSource);

		for (const auto [channels, pixelStride] : { Layout{ 3, 4 }, Layout{ 3, 3 }, Layout{ 4, 4 }, Layout{ 2, 2 } })
		{
			CAPTURE(+channels, +pixelStride);
			TestImage source(sourceWidth, sourceHeight, channels, pixelStride);
			fillDeterministicPattern(source);

			TestImage dest(destWidth, destHeight, channels, pixelStride);
			resize(dest, source);

			for (uint64_t y = 0; y < destHeight; ++y)
			{
				for (uint64_t x = 0; x < destWidth; ++x)
				{
					CAPTURE(x, y);
					const int difference = static_cast<int>(dest.pixel(x, y)[0]) - static_cast<int>(grayscaleDest.pixel(x, y)[0]);
					CHECK(std::max(difference, -difference) <= 1);
				}
			}
		}
	}
}

TEST_CASE("Images with more than four channels are rejected", "[resize][validation]")
{
	constexpr uint8_t sentinel = 0x7b;
	TestImage source(2, 2, 5, 5, 0, 0x42);
	TestImage dest(1, 1, 5, 5, 0, sentinel);

	resize(dest, source);

	for (const uint8_t value : dest.data)
		CHECK(+value == +sentinel);
}

TEST_CASE("Images with 16-bit channels are rejected", "[resize][validation]")
{
	constexpr uint8_t sentinel = 0x7b;
	std::vector<uint8_t> sourceData(8, 0x42);
	std::vector<uint8_t> destData(18, sentinel);
	const std::vector<uint8_t> originalDestData = destData;
	const ImageView<true> source{ 2, 2, 1, 2, 2, 4, sourceData.data() };
	ImageView<false> dest{ 3, 3, 1, 2, 2, 6, destData.data() };

	ImageProcessing::resize(dest, source);

	CHECK(destData == originalDestData);
}

TEST_CASE("Seeded randomized small images preserve resize properties", "[resize][property]")
{
	std::mt19937 randomEngine(0x5eed1234);

	SECTION("Crop equivalence")
	{
		for (size_t iteration = 0; iteration < 64; ++iteration)
		{
			CAPTURE(iteration);
			const uint64_t sourceWidth = randomInRange(randomEngine, 2, 8);
			const uint64_t sourceHeight = randomInRange(randomEngine, 2, 8);
			const uint64_t cropWidth = randomInRange(randomEngine, 1, sourceWidth);
			const uint64_t cropHeight = randomInRange(randomEngine, 1, sourceHeight);
			const Rect sourceRect{
				randomInRange(randomEngine, 0, sourceWidth - cropWidth),
				randomInRange(randomEngine, 0, sourceHeight - cropHeight),
				cropWidth,
				cropHeight
			};
			const uint8_t channels = static_cast<uint8_t>(iteration % 4 + 1);
			const uint8_t pixelStride = static_cast<uint8_t>(randomInRange(randomEngine, channels, 4));
			TestImage source(sourceWidth, sourceHeight, channels, pixelStride, randomInRange(randomEngine, 0, 4), 0xcc);
			fillLogicalBytes(source, randomEngine);

			const TestImage packedCrop = tightlyPackedCrop(source, sourceRect);
			const uint64_t destWidth = randomInRange(randomEngine, 1, 8);
			const uint64_t destHeight = randomInRange(randomEngine, 1, 8);
			TestImage croppedResult(destWidth, destHeight, channels, pixelStride, randomInRange(randomEngine, 0, 4), 0x1c);
			TestImage packedResult(destWidth, destHeight, channels, pixelStride, randomInRange(randomEngine, 0, 4), 0xe3);

			resize(croppedResult, source, sourceRect);
			resize(packedResult, packedCrop);
			requirePixelsEqual(croppedResult, packedResult);
		}
	}

	SECTION("Mirror symmetry")
	{
		for (size_t iteration = 0; iteration < 64; ++iteration)
		{
			CAPTURE(iteration);
			TestImage source(randomInRange(randomEngine, 1, 8), randomInRange(randomEngine, 1, 8), 1, 1);
			fillLogicalBytes(source, randomEngine);
			const TestImage mirroredSource = mirrorBothAxes(source);
			TestImage dest(randomInRange(randomEngine, 1, 8), randomInRange(randomEngine, 1, 8), 1, 1);
			TestImage mirroredDest(dest.width, dest.height, 1, 1);

			resize(dest, source);
			resize(mirroredDest, mirroredSource);
			for (uint64_t y = 0; y < dest.height; ++y)
			{
				for (uint64_t x = 0; x < dest.width; ++x)
				{
					CAPTURE(x, y);
					const int difference = static_cast<int>(dest.pixel(x, y)[0]) -
						static_cast<int>(mirroredDest.pixel(dest.width - x - 1, dest.height - y - 1)[0]);
					CHECK(std::max(difference, -difference) <= 1);
				}
			}
		}
	}

	SECTION("Constant preservation")
	{
		for (size_t iteration = 0; iteration < 32; ++iteration)
		{
			CAPTURE(iteration);
			const uint8_t channels = static_cast<uint8_t>(iteration % 4 + 1);
			const uint8_t pixelStride = static_cast<uint8_t>(randomInRange(randomEngine, channels, 6));
			TestImage source(randomInRange(randomEngine, 1, 8), randomInRange(randomEngine, 1, 8), channels, pixelStride);
			std::vector<uint8_t> pixelValues(pixelStride);
			for (uint8_t& value : pixelValues)
				value = static_cast<uint8_t>(randomInRange(randomEngine, 0, 255));
			for (uint64_t y = 0; y < source.height; ++y)
			{
				for (uint64_t x = 0; x < source.width; ++x)
					std::copy(pixelValues.begin(), pixelValues.end(), source.pixel(x, y));
			}

			TestImage dest(randomInRange(randomEngine, 1, 8), randomInRange(randomEngine, 1, 8), channels, pixelStride);
			resize(dest, source);
			for (uint64_t y = 0; y < dest.height; ++y)
			{
				for (uint64_t x = 0; x < dest.width; ++x)
				{
					for (size_t byte = 0; byte < pixelStride; ++byte)
					{
						CAPTURE(x, y, byte);
						CHECK(+dest.pixel(x, y)[byte] == +pixelValues[byte]);
					}
				}
			}
		}
	}

	SECTION("Equivalent repeated channels")
	{
		for (size_t iteration = 0; iteration < 32; ++iteration)
		{
			CAPTURE(iteration);
			TestImage grayscaleSource(randomInRange(randomEngine, 1, 8), randomInRange(randomEngine, 1, 8), 1, 1);
			fillLogicalBytes(grayscaleSource, randomEngine);
			TestImage rgbSource(grayscaleSource.width, grayscaleSource.height, 3, 3);
			for (uint64_t y = 0; y < grayscaleSource.height; ++y)
			{
				for (uint64_t x = 0; x < grayscaleSource.width; ++x)
					std::fill_n(rgbSource.pixel(x, y), 3, grayscaleSource.pixel(x, y)[0]);
			}

			const uint64_t destWidth = randomInRange(randomEngine, 1, 8);
			const uint64_t destHeight = randomInRange(randomEngine, 1, 8);
			TestImage grayscaleDest(destWidth, destHeight, 1, 1);
			TestImage rgbDest(destWidth, destHeight, 3, 3);
			resize(grayscaleDest, grayscaleSource);
			resize(rgbDest, rgbSource);

			for (uint64_t y = 0; y < destHeight; ++y)
			{
				for (uint64_t x = 0; x < destWidth; ++x)
				{
					CAPTURE(x, y);
					for (size_t channel = 0; channel < 3; ++channel)
						CHECK(+rgbDest.pixel(x, y)[channel] == +grayscaleDest.pixel(x, y)[0]);
				}
			}
		}
	}
}

TEST_CASE("Parallel resize matches single-threaded results", "[resize][threading]")
{
	CWorkerThreadPool pool(4, "Resize test pool");
	std::mt19937 randomEngine(20260801);

	for (const auto [channels, pixelStride] : pixelLayouts)
	{
		CAPTURE(+channels, +pixelStride);
		for (const ResizeJob& job : resizeJobs)
		{
			CAPTURE(job.srcWidth, job.srcHeight, job.destWidth, job.destHeight);
			TestImage source(job.srcWidth, job.srcHeight, channels, pixelStride);
			fillLogicalBytes(source, randomEngine);

			TestImage serialDest(job.destWidth, job.destHeight, channels, pixelStride);
			resize(serialDest, source);

			// Repeated because a race would only manifest probabilistically; the per-pixel sweep runs only to diagnose a mismatch
			for (int iteration = 0; iteration < 20; ++iteration)
			{
				CAPTURE(iteration);
				TestImage parallelDest(job.destWidth, job.destHeight, channels, pixelStride);
				resize(parallelDest, source, {}, &pool);

				const bool identical = parallelDest.data == serialDest.data;
				CHECK(identical);
				if (!identical)
					requirePixelsEqual(parallelDest, serialDest);
			}
		}
	}
}
