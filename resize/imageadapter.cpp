#include "imageadapter.hpp"

std::unique_ptr<ImageAdapter> ImageAdapter::createSameFormat(const uint32_t w, const uint32_t h) const
{
	auto newAdapter = create(w, h, numChannels(), bytesPerChannel());
	return newAdapter;
}
