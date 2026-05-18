#pragma once

#include <type_traits>

namespace ImageProcessing
{
	template <bool ConstView = true>
	struct ImageView
	{
		using DataPtr = std::conditional_t<ConstView, const void*, void*>;

		uint32_t width;
		uint32_t height;
		uint8_t channels;
		uint8_t bytesPerChannel;
		uint8_t channelStride;
		size_t bytesPerLine;

		DataPtr data;

		template <class T>
		[[nodiscard]] inline auto* scanLine(uint32_t line) const noexcept
		{
			if constexpr (ConstView)
				return reinterpret_cast<const T*>(reinterpret_cast<const uint8_t*>(data) + static_cast<size_t>(line) * bytesPerLine);
			else
				return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(data) + static_cast<size_t>(line) * bytesPerLine);
		}
	};

	void resize(ImageView<false>& dest, const ImageView<true>& source);
}
