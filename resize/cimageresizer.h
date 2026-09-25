#pragma once

#include <functional>
#include <stddef.h>
#include <stdint.h>
#include <type_traits>

namespace ImageProcessing
{
	// Only 2- and 4-channel images carry alpha, always as the last channel
	[[nodiscard]] constexpr bool hasAlphaChannel(size_t channels) noexcept
	{
		return channels == 2 || channels == 4;
	}

	enum class AlphaKind : uint8_t
	{
		Straight,
		Premultiplied,
	};

	template <bool ConstView = true>
	struct ImageView
	{
		using DataPtr = std::conditional_t<ConstView, const void*, void*>;

		uint64_t width = 0;
		uint64_t height = 0;
		uint8_t channels = 0;
		AlphaKind alphaKind = AlphaKind::Straight; // Ignored without an alpha channel
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

	enum class SimdUsage : uint8_t
	{
		Auto, // The SIMD kernels wherever the CPU supports them
		Disabled,
	};

	// Whether SimdUsage::Auto reaches the SIMD kernels on this CPU
	[[nodiscard]] bool simdAvailable() noexcept;

	// The callback must run body(0) .. body(count - 1) concurrently and must not return until all of them have completed.
	// When empty, the work runs on the calling thread; either way resize() returns only once the destination is complete.
	using ParallelForFn = std::function<void(size_t count, const std::function<void(size_t index)>& body)>;

	// The output is premultiplied, so an alpha destination must be marked Premultiplied; a straight source is premultiplied as it is read.
	void resize(ImageView<false>& dest, const ImageView<true>& source, Rect srcRect = {}, const ParallelForFn& parallelFor = {},
		ResizeKernel kernel = ResizeKernel::Auto, SimdUsage simd = SimdUsage::Auto);
}
