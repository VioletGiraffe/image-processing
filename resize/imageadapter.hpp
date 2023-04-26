#pragma once

#include <memory>
#include <stdint.h>
#include <utility>

struct ImageAdapter {
	virtual ~ImageAdapter() = default;

	[[nodiscard]] virtual std::unique_ptr<ImageAdapter> create(const uint32_t w, const uint32_t h, const uint8_t nChannels, const uint8_t bpc) const = 0;
	[[nodiscard]] virtual std::unique_ptr<ImageAdapter> clone() const = 0;

	[[nodiscard]] std::unique_ptr<ImageAdapter> createSameFormat(const uint32_t w, const uint32_t h) const;

	[[nodiscard]] virtual uint32_t width() const = 0;
	[[nodiscard]] virtual uint32_t height() const = 0;
	[[nodiscard]] inline float aspectRatio() const {
		return (float)width() / (float)height();
	}

	[[nodiscard]] virtual uint8_t numChannels() const = 0;
	[[nodiscard]] virtual uint8_t bytesPerChannel() const = 0;

	[[nodiscard]] inline uint8_t bytesPerPixel() const {
		return numChannels() * bytesPerChannel();
	}

	[[nodiscard]] inline uint32_t bytesPerRow() const {
		return width() * bytesPerPixel();
	}

	[[nodiscard]] virtual const void* scanLine(const uint32_t row) const = 0;
	[[nodiscard]] virtual void* scanLine(const uint32_t row) = 0;
};
