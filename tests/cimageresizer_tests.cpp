#include "3rdparty/catch2/catch.hpp"
#include "resize/cimageresizer.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
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

	void resize(TestImage& dest, const TestImage& source, Rect sourceRect = {})
	{
		auto destView = dest.mutableView();
		const auto sourceView = source.constView();
		ImageProcessing::resize(destView, sourceView, sourceRect);
	}

	void setPixel(TestImage& image, uint64_t x, uint64_t y, std::initializer_list<uint8_t> values)
	{
		REQUIRE(values.size() == image.pixelStrideBytes);
		std::copy(values.begin(), values.end(), image.pixel(x, y));
	}

	void requirePixel(const TestImage& image, uint64_t x, uint64_t y, std::initializer_list<uint8_t> expected)
	{
		CAPTURE(x, y);
		REQUIRE(expected.size() == image.pixelStrideBytes);

		size_t byte = 0;
		for (const uint8_t expectedValue : expected)
		{
			CAPTURE(byte);
			CHECK(image.pixel(x, y)[byte] == expectedValue);
			++byte;
		}
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
				CHECK(dest.data[static_cast<size_t>(y) * dest.bytesPerLine + byte] == paddingSentinel);
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

		TestImage dest(3, 5, 4, 4);
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
		setPixel(source, 0, 0, { 10, 20, 30, 0xff });
		setPixel(source, 1, 0, { 40, 50, 60, 0xff });
		setPixel(source, 0, 1, { 70, 80, 90, 0xff });
		setPixel(source, 1, 1, { 100, 110, 120, 0xff });

		TestImage dest(5, 3, 3, 4, 0, 0);
		resize(dest, source);

		for (uint64_t y = 0; y < dest.height; ++y)
		{
			for (uint64_t x = 0; x < dest.width; ++x)
				CHECK(dest.pixel(x, y)[3] == 0xff);
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

TEST_CASE("Images with more than four channels are rejected", "[resize][validation]")
{
	constexpr uint8_t sentinel = 0x7b;
	TestImage source(2, 2, 5, 5, 0, 0x42);
	TestImage dest(1, 1, 5, 5, 0, sentinel);

	resize(dest, source);

	for (const uint8_t value : dest.data)
		CHECK(value == sentinel);
}
