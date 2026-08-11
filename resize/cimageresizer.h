#pragma once

#include <functional>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace ImageProcessing
{
	template <bool ConstView = true>
	struct ImageView
	{
		using DataPtr = std::conditional_t<ConstView, const void*, void*>;

		uint64_t width = 0;
		uint64_t height = 0;
		uint8_t channels = 0;
		uint8_t bytesPerChannel = 0;
		uint8_t pixelStrideBytes = 0;
		size_t bytesPerLine = 0;

		DataPtr data = nullptr;

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
		uint64_t left = 0, top = 0;
		uint64_t w = 0, h = 0;
	};

	enum class ResizeKernel : uint8_t
	{
		Auto, // Catmull-Rom when upscaling, Lanczos3 when downscaling, chosen per axis
		CatmullRom,
		Lanczos3,
	};

	// The callback must run body(0) .. body(count - 1) concurrently and must not return until all of them have completed.
	// When empty, the work runs on the calling thread; either way resize() returns only once the destination is complete.
	using ParallelForFn = std::function<void(size_t count, const std::function<void(size_t index)>& body)>;

	void resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect = {}, const ParallelForFn& parallelFor = {}, ResizeKernel kernel = ResizeKernel::Auto);
}
