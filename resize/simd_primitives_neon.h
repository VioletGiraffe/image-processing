#pragma once

// The NEON primitives cimageresizer_simd.inl is written against; only cimageresizer_simd_neon.cpp includes this

#include "simd_support.h"

#include <arm_neon.h>

#include <cmath>
#include <stddef.h>
#include <stdint.h>

#define IMAGE_PROCESSING_SIMD_TARGET
#define IMAGE_PROCESSING_SIMD_INLINE IMAGE_PROCESSING_NEON_INLINE

namespace ImageProcessing::Detail::Neon
{
	// 8 consecutive floats: two RGBA pixels, or a run of channel values
	struct Floats8
	{
		float32x4_t low;
		float32x4_t high;
	};

	// One RGBA pixel
	using Floats4 = float32x4_t;
	// 8 consecutive filter weights
	using WeightBlock = Floats8;
	// The alpha bytes writeEightRgb32Pixels sets
	using Rgb32PixelTails = uint8x16_t;

	IMAGE_PROCESSING_SIMD_INLINE void leaveKernel() noexcept {}

	IMAGE_PROCESSING_SIMD_INLINE Floats8 zeroFloats8() noexcept { return { vdupq_n_f32(0.0f), vdupq_n_f32(0.0f) }; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 zeroFloats4() noexcept { return vdupq_n_f32(0.0f); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 loadFloats8(const float* floats) noexcept { return { vld1q_f32(floats), vld1q_f32(floats + 4) }; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 loadFloats4(const float* floats) noexcept { return vld1q_f32(floats); }

	IMAGE_PROCESSING_SIMD_INLINE void storeFloats8(float* floats, Floats8 values) noexcept
	{
		vst1q_f32(floats, values.low);
		vst1q_f32(floats + 4, values.high);
	}

	IMAGE_PROCESSING_SIMD_INLINE void storeFloats4(float* floats, Floats4 values) noexcept { vst1q_f32(floats, values); }

	IMAGE_PROCESSING_SIMD_INLINE Floats8 broadcastFloats8(float value) noexcept
	{
		const float32x4_t values = vdupq_n_f32(value);
		return { values, values };
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 broadcastFloats4(float value) noexcept { return vdupq_n_f32(value); }
	IMAGE_PROCESSING_SIMD_INLINE Floats8 add(Floats8 a, Floats8 b) noexcept { return { vaddq_f32(a.low, b.low), vaddq_f32(a.high, b.high) }; }
	IMAGE_PROCESSING_SIMD_INLINE Floats4 add(Floats4 a, Floats4 b) noexcept { return vaddq_f32(a, b); }

	// a * b + accum, fused
	IMAGE_PROCESSING_SIMD_INLINE Floats8 mulAdd(Floats8 a, Floats8 b, Floats8 accum) noexcept
	{
		return { vfmaq_f32(accum.low, a.low, b.low), vfmaq_f32(accum.high, a.high, b.high) };
	}

	IMAGE_PROCESSING_SIMD_INLINE Floats4 mulAdd(Floats4 a, Floats4 b, Floats4 accum) noexcept { return vfmaq_f32(accum, a, b); }
	IMAGE_PROCESSING_SIMD_INLINE float mulAdd(float a, float b, float accum) noexcept { return std::fma(a, b, accum); }

	// The sum of the first 4 floats and the last 4
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumHalves(Floats8 values) noexcept { return vaddq_f32(values.low, values.high); }

	// Sums a Floats4's 4 / FloatsPerPixel pixels into its first FloatsPerPixel lanes
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats4 sumPixels(Floats4 pixels) noexcept
	{
		static_assert(FloatsPerPixel == 1 || FloatsPerPixel == 2);
		const float32x2_t pairSums = vadd_f32(vget_low_f32(pixels), vget_high_f32(pixels));
		if constexpr (FloatsPerPixel == 2)
			return vcombine_f32(pairSums, pairSums);
		else
			return vdupq_n_f32(vget_lane_f32(vpadd_f32(pairSums, pairSums), 0));
	}

	IMAGE_PROCESSING_SIMD_INLINE WeightBlock loadWeightBlock(const float* weights) noexcept { return loadFloats8(weights); }

	// Turns a WeightBlock into the weights of taps [Part * 8 / FloatsPerPixel, (Part + 1) * 8 / FloatsPerPixel), each repeated for its pixel's floats
	template <size_t FloatsPerPixel>
	class WeightSpreader
	{
	public:
		template <size_t Part>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 spread(WeightBlock weights) const noexcept
		{
			static_assert(Part < FloatsPerPixel);
			if constexpr (FloatsPerPixel == 4)
			{
				const float32x4_t fourWeights = Part < 2 ? weights.low : weights.high;
				constexpr int firstLane = (Part % 2) * 2;
				return { vdupq_laneq_f32(fourWeights, firstLane), vdupq_laneq_f32(fourWeights, firstLane + 1) };
			}
			else if constexpr (FloatsPerPixel == 2)
			{
				const float32x4_t fourWeights = Part == 0 ? weights.low : weights.high;
				return { vzip1q_f32(fourWeights, fourWeights), vzip2q_f32(fourWeights, fourWeights) };
			}
			else
				return weights;
		}
	};

	// 16 bytes as 16 floats: the first 8 go to first
	IMAGE_PROCESSING_SIMD_INLINE void sixteenBytesAsFloats(uint8x16_t bytes, Floats8& first, Floats8& second) noexcept
	{
		const uint16x8_t firstWords = vmovl_u8(vget_low_u8(bytes));
		const uint16x8_t secondWords = vmovl_high_u8(bytes);
		first = { vcvtq_f32_u32(vmovl_u16(vget_low_u16(firstWords))), vcvtq_f32_u32(vmovl_high_u16(firstWords)) };
		second = { vcvtq_f32_u32(vmovl_u16(vget_low_u16(secondWords))), vcvtq_f32_u32(vmovl_high_u16(secondWords)) };
	}

	IMAGE_PROCESSING_SIMD_INLINE void loadSixteenBytesAsFloats(const uint8_t* bytes, Floats8& first, Floats8& second) noexcept
	{
		sixteenBytesAsFloats(vld1q_u8(bytes), first, second);
	}

	// 4 RGB pixels as 4 floats each, the 4th 0: pixels 0 and 1 go to pixels01. Reads 16 bytes, 4 past the pixels.
	IMAGE_PROCESSING_SIMD_INLINE void loadFourRgbPixelsAsFloats(const uint8_t* pixels, Floats8& pixels01, Floats8& pixels23) noexcept
	{
		// An out-of-range index reads as 0
		static constexpr uint8_t rgbToRgb0Indices[16] = {
			0, 1, 2, 0xFF,
			3, 4, 5, 0xFF,
			6, 7, 8, 0xFF,
			9, 10, 11, 0xFF };
		sixteenBytesAsFloats(vqtbl1q_u8(vld1q_u8(pixels), vld1q_u8(rgbToRgb0Indices)), pixels01, pixels23);
	}

	// Each pixel's alpha, the last of its FloatsPerPixel floats, in all of its lanes
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE float32x4_t pixelAlphas(float32x4_t pixels) noexcept
	{
		static_assert(FloatsPerPixel == 2 || FloatsPerPixel == 4);
		if constexpr (FloatsPerPixel == 4)
			return vdupq_laneq_f32(pixels, 3);
		else
			return vtrn2q_f32(pixels, pixels);
	}

	// Color times alpha / 255; the alpha lanes keep the source value
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE float32x4_t premultiplyPixels(float32x4_t pixels) noexcept
	{
		const float32x4_t alphas = pixelAlphas<FloatsPerPixel>(pixels);
		const float32x4_t premultiplied = vmulq_f32(pixels, vmulq_f32(alphas, vdupq_n_f32(1.0f / 255.0f)));
		if constexpr (FloatsPerPixel == 4)
			return vcopyq_laneq_f32(premultiplied, 3, pixels, 3);
		else
			return vtrn1q_f32(premultiplied, alphas); // Lanes 0 and 2 of alphas are the source's lanes 1 and 3
	}

	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 premultiplyPixels(Floats8 pixels) noexcept
	{
		return { premultiplyPixels<FloatsPerPixel>(pixels.low), premultiplyPixels<FloatsPerPixel>(pixels.high) };
	}

	// Rounds 4 floats to 4 words: truncation after adding 0.5, and saturation, as the AVX2 pack does
	IMAGE_PROCESSING_SIMD_INLINE uint16x4_t packFourFloatsToWords(float32x4_t values) noexcept
	{
		values = vminq_f32(vdupq_n_f32(255.0f), values);
		return vqmovun_s32(vcvtq_s32_f32(vaddq_f32(values, vdupq_n_f32(0.5f))));
	}

	IMAGE_PROCESSING_SIMD_INLINE uint16x8_t packEightFloatsToWords(Floats8 values) noexcept
	{
		return vcombine_u16(packFourFloatsToWords(values.low), packFourFloatsToWords(values.high));
	}

	IMAGE_PROCESSING_SIMD_INLINE uint8x16_t packSixteenFloatsToBytes(Floats8 first, Floats8 second) noexcept
	{
		return vcombine_u8(vqmovn_u16(packEightFloatsToWords(first)), vqmovn_u16(packEightFloatsToWords(second)));
	}

	// Color is capped at alpha: writePixelBytes caps it too
	template <size_t FloatsPerPixel>
	IMAGE_PROCESSING_SIMD_INLINE Floats8 capColorAtAlpha(Floats8 pixels) noexcept
	{
		return { vminq_f32(pixels.low, pixelAlphas<FloatsPerPixel>(pixels.low)), vminq_f32(pixels.high, pixelAlphas<FloatsPerPixel>(pixels.high)) };
	}

	IMAGE_PROCESSING_SIMD_INLINE void writeThirtyTwoBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Floats8 values3) noexcept
	{
		vst1q_u8(dest, packSixteenFloatsToBytes(values0, values1));
		vst1q_u8(dest + 16, packSixteenFloatsToBytes(values2, values3));
	}

	IMAGE_PROCESSING_SIMD_INLINE void writeTwentyFourBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2) noexcept
	{
		vst1q_u8(dest, packSixteenFloatsToBytes(values0, values1));
		vst1_u8(dest + 16, vqmovn_u16(packEightFloatsToWords(values2)));
	}

	IMAGE_PROCESSING_SIMD_INLINE Rgb32PixelTails rgb32PixelTails(uint8_t tailValue) noexcept
	{
		return vreinterpretq_u8_u32(vdupq_n_u32(static_cast<uint32_t>(tailValue) << 24));
	}

	// values0..2 hold the 8 pixels' 24 color floats
	IMAGE_PROCESSING_SIMD_INLINE void writeEightRgb32Pixels(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, Rgb32PixelTails pixelTails) noexcept
	{
		const uint8x16_t firstSixteenRgbBytes = packSixteenFloatsToBytes(values0, values1);
		const uint8x8_t lastEightRgbBytes = vqmovn_u16(packEightFloatsToWords(values2));
		const uint8x16_t lastTwelveRgbBytes = vextq_u8(firstSixteenRgbBytes, vcombine_u8(lastEightRgbBytes, lastEightRgbBytes), 12);
		// An out-of-range index reads as 0: the alpha bytes, set from pixelTails
		static constexpr uint8_t rgbToRgb32Indices[16] = {
			0, 1, 2, 0xFF,
			3, 4, 5, 0xFF,
			6, 7, 8, 0xFF,
			9, 10, 11, 0xFF };
		const uint8x16_t rgbToRgb32 = vld1q_u8(rgbToRgb32Indices);

		vst1q_u8(dest, vorrq_u8(vqtbl1q_u8(firstSixteenRgbBytes, rgbToRgb32), pixelTails));
		vst1q_u8(dest + 16, vorrq_u8(vqtbl1q_u8(lastTwelveRgbBytes, rgbToRgb32), pixelTails));
	}
}
