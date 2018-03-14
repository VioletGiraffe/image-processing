#pragma once

#include <memory>
#include <stdint.h>
#include <utility>

struct ImageAdapter {
	virtual ~ImageAdapter() = default;

	virtual std::unique_ptr<ImageAdapter> create(const uint32_t w, const uint32_t h, const uint8_t nChannels, const uint8_t bpc) const = 0;
	virtual std::unique_ptr<ImageAdapter> clone() const = 0;

	std::unique_ptr<ImageAdapter> createSameFormat(const uint32_t w, const uint32_t h) const;

	virtual uint32_t width() const = 0;
	virtual uint32_t height() const = 0;
	inline float aspectRatio() const {
		return width() / (float)height();
	}

	virtual uint8_t numChannels() const = 0;
	virtual uint8_t bytesPerChannel() const = 0;

	inline uint8_t bytesPerPixel() const {
		return numChannels() * bytesPerChannel();
	}

	inline uint32_t bytesPerRow() const {
		return bytesPerPixel() * width();
	}

	virtual const void* scanLine(const uint32_t row) const = 0;
	virtual void* scanLine(const uint32_t row) = 0;
};
