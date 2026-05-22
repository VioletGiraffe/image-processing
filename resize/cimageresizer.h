#pragma once

#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace ImageProcessing
{
	template <bool ConstView = true>
	struct ImageView
	{
		using DataPtr = std::conditional_t<ConstView, const void*, void*>;

		uint64_t width;
		uint64_t height;
		uint8_t channels;
		uint8_t bytesPerChannel;
		uint8_t channelStride;
		size_t bytesPerLine;

		DataPtr data;

		template <class T>
		[[nodiscard]] inline auto* scanLine(uint64_t line) const noexcept
		{
			if constexpr (ConstView)
				return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(data) + static_cast<size_t>(line) * bytesPerLine);
			else
				return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(data) + static_cast<size_t>(line) * bytesPerLine);
		}
	};
	struct Rect {
		uint64_t top = 0, left = 0;
		uint64_t w = 0, h = 0;
	};

	void resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect = {});
}
