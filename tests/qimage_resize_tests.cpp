#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include "3rdparty/catch2/catch.hpp"
RESTORE_COMPILER_WARNINGS

#include "resize/qimage_resize.h"

#include <QColor>
#include <QImage>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace
{
	struct FormatMapping
	{
		QImage::Format format;
		const char* name;
		uint8_t channels;
		uint8_t bytesPerChannel;
		uint8_t pixelStrideBytes;
	};

	// Format_RGBX8888 is absent: the bridge maps it to nothing, see doc/tests_spec.md.
	constexpr FormatMapping mappedFormats[] = {
		{ QImage::Format_Grayscale8, "Grayscale8", 1, 1, 1 },
		{ QImage::Format_Indexed8, "Indexed8", 1, 1, 1 },
		{ QImage::Format_Grayscale16, "Grayscale16", 1, 2, 2 },
		{ QImage::Format_RGB888, "RGB888", 3, 1, 3 },
		{ QImage::Format_RGB32, "RGB32", 3, 1, 4 },
		{ QImage::Format_ARGB32, "ARGB32", 4, 1, 4 },
		{ QImage::Format_ARGB32_Premultiplied, "ARGB32_Premultiplied", 4, 1, 4 },
		{ QImage::Format_RGBA8888, "RGBA8888", 4, 1, 4 },
		{ QImage::Format_RGBA8888_Premultiplied, "RGBA8888_Premultiplied", 4, 1, 4 },
		{ QImage::Format_RGBA64, "RGBA64", 4, 2, 8 },
		{ QImage::Format_RGBX64, "RGBX64", 3, 2, 8 },
	};

	constexpr QImage::Format unmappedFormats[] = {
		QImage::Format_Mono,
		QImage::Format_RGB16,
		QImage::Format_RGB666,
		QImage::Format_BGR30,
	};

	// 9 columns pads the row in the 1- and 3-byte pixel formats: Qt aligns bytesPerLine to 4 bytes, so a stride mistake shows up there.
	constexpr int testWidth = 9;
	constexpr int testHeight = 5;

	[[nodiscard]] size_t logicalRowBytes(const QImage& image)
	{
		return static_cast<size_t>(image.width()) * ImageProcessing::imageView<true>(image).pixelStrideBytes;
	}

	// Writes every logical pixel byte, leaving Qt's row padding alone.
	void writeBytePattern(QImage& image)
	{
		const size_t rowBytes = logicalRowBytes(image);
		for (int y = 0; y < image.height(); ++y)
		{
			uint8_t* row = image.scanLine(y);
			for (size_t byte = 0; byte < rowBytes; ++byte)
				row[byte] = static_cast<uint8_t>((byte * 37 + static_cast<size_t>(y) * 71 + 19) & 0xff);
		}
	}

	// Unary + on every byte compared: Catch2 renders uint8_t as a character.
	void requireLogicalBytesEqual(const QImage& actual, const QImage& expected)
	{
		REQUIRE(actual.size() == expected.size());
		REQUIRE(actual.format() == expected.format());

		const size_t rowBytes = logicalRowBytes(expected);
		for (int y = 0; y < expected.height(); ++y)
		{
			const uint8_t* actualRow = actual.constScanLine(y);
			const uint8_t* expectedRow = expected.constScanLine(y);
			for (size_t byte = 0; byte < rowBytes; ++byte)
			{
				CAPTURE(y, byte);
				CHECK(+actualRow[byte] == +expectedRow[byte]);
			}
		}
	}
}

TEST_CASE("Mapped QImage formats produce the matching image view", "[resize][qimage]")
{
	for (const FormatMapping& mapping : mappedFormats)
	{
		CAPTURE(mapping.name);

		QImage image(testWidth, testHeight, mapping.format);
		REQUIRE(!image.isNull());

		const auto view = ImageProcessing::imageView<true>(std::as_const(image));

		CHECK(view.width == static_cast<uint64_t>(testWidth));
		CHECK(view.height == static_cast<uint64_t>(testHeight));
		CHECK(+view.channels == +mapping.channels);
		CHECK(+view.bytesPerChannel == +mapping.bytesPerChannel);
		CHECK(+view.pixelStrideBytes == +mapping.pixelStrideBytes);
		CHECK(view.bytesPerLine == static_cast<size_t>(image.bytesPerLine()));
		CHECK(view.data == image.constBits());

		// Cross-checked against Qt: the stride must step a whole pixel, RGB32 included, where one byte belongs to no channel.
		CHECK(+mapping.pixelStrideBytes == image.depth() / 8);

		const auto mutableView = ImageProcessing::imageView<false>(image);
		CHECK(mutableView.width == view.width);
		CHECK(mutableView.height == view.height);
		CHECK(+mutableView.channels == +view.channels);
		CHECK(+mutableView.bytesPerChannel == +view.bytesPerChannel);
		CHECK(+mutableView.pixelStrideBytes == +view.pixelStrideBytes);
		CHECK(mutableView.bytesPerLine == view.bytesPerLine);
		CHECK(mutableView.data == image.bits());
	}
}

TEST_CASE("A format with no view resizes nothing and reports failure", "[resize][qimage][validation]")
{
	auto requireNoView = [](const QImage& image)
	{
		const auto view = ImageProcessing::imageView<true>(image);
		CHECK(view.data == nullptr);
		CHECK(view.width == 0);
		CHECK(view.height == 0);

		QImage source(testWidth, testHeight, QImage::Format_ARGB32);
		source.fill(Qt::red);

		QImage dest(4, 3, QImage::Format_ARGB32);
		dest.fill(Qt::green);
		const QImage untouched = dest.copy();

		CHECK(!ImageProcessing::resize(dest, image));
		CHECK(dest == untouched);

		// Both roles: the bridge tests the source view and the destination view separately.
		QImage unsupportedDest(4, 3, image.format());
		CHECK(!ImageProcessing::resize(unsupportedDest, source));
	};

	SECTION("Unmapped pixel formats")
	{
		for (const QImage::Format format : unmappedFormats)
		{
			CAPTURE(format);
			QImage image(testWidth, testHeight, format);
			REQUIRE(!image.isNull());
			image.fill(0);
			requireNoView(image);
		}
	}

	SECTION("A null image")
	{
		requireNoView(QImage());
	}
}

TEST_CASE("A destination view detaches, a source view does not", "[resize][qimage]")
{
	QImage source(testWidth, testHeight, QImage::Format_ARGB32);
	source.fill(QColor(10, 20, 30, 40));

	SECTION("The destination stops sharing before the resize writes")
	{
		QImage dest(4, 3, QImage::Format_ARGB32);
		dest.fill(QColor(200, 210, 220, 230));

		const QImage shared = dest;
		REQUIRE(shared.constBits() == dest.constBits());
		const QImage sharedContentBefore = dest.copy();

		REQUIRE(ImageProcessing::resize(dest, source));

		CHECK(shared.constBits() != dest.constBits());
		CHECK(shared == sharedContentBefore);
		CHECK(dest != sharedContentBefore);
	}

	SECTION("The source keeps sharing")
	{
		const QImage shared = source;
		QImage dest(4, 3, QImage::Format_ARGB32);

		REQUIRE(ImageProcessing::resize(dest, source));

		CHECK(shared.constBits() == source.constBits());
	}
}

TEST_CASE("An identity resize through the bridge reproduces every logical byte", "[resize][qimage]")
{
	for (const FormatMapping& mapping : mappedFormats)
	{
		if (mapping.bytesPerChannel != 1) // The resizer takes one-byte channels only
			continue;

		CAPTURE(mapping.name);

		QImage source(testWidth, testHeight, mapping.format);
		REQUIRE(!source.isNull());
		writeBytePattern(source);

		QImage dest(testWidth, testHeight, mapping.format);
		dest.fill(0);

		REQUIRE(ImageProcessing::resize(dest, source));
		requireLogicalBytesEqual(dest, source);
	}
}
