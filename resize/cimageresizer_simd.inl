// The SIMD kernels' outline, written against one instruction set's primitives.
// A cimageresizer_simd_<level>.cpp includes that level's simd_primitives_<level>.h, sets IMAGE_PROCESSING_SIMD_LEVEL to the
// level's namespace, named as its SimdLevel, then includes this file.

#ifndef IMAGE_PROCESSING_SIMD_LEVEL
	#error "Only a cimageresizer_simd_<level>.cpp includes this file"
#endif

#include "resize_internal.h"

#include "compiler/compiler_warnings_control.h"

#include <algorithm>
#include <array>
#include <assert.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string.h>
#include <type_traits>
#include <utility>

// The float equality tests are exact-zero checks by design
DISABLE_CLANG_GCC_WARNING("-Wfloat-equal")

namespace ImageProcessing::Detail::IMAGE_PROCESSING_SIMD_LEVEL
{
	namespace
	{
		constexpr SimdLevel simdLevel = SimdLevel::IMAGE_PROCESSING_SIMD_LEVEL;

		// 3 channels get a 4th float, 0: the 4-channel code then serves them
		[[nodiscard]] constexpr size_t sourceFloatsPerPixel(size_t channels) noexcept
		{
			return channels == 3 ? 4 : channels;
		}

		// The longest padded x run with a kernel compiled for its length, 0 for none.
		// 2-float pixels have none: a rare layout, not worth the kernels.
		[[nodiscard]] constexpr size_t longestKernelXRun(size_t channels) noexcept
		{
			return sourceFloatsPerPixel(channels) == 2 ? 0 : longestFixedXRun;
		}

		// PixelStride 0: the stride is taken at runtime
		template <size_t PixelStride>
		[[nodiscard]] constexpr size_t effectivePixelStride(size_t runtimePixelStride) noexcept
		{
			return PixelStride != 0 ? PixelStride : runtimePixelStride;
		}

		// One pixel's sourceFloatsPerPixel floats, premultiplied with the vector code's arithmetic: one row mixes both
		template <size_t Channels, bool PremultiplyAlpha>
		IMAGE_PROCESSING_SIMD_INLINE void convertPixelToFloats(const uint8_t* sourcePixel, float* pixelFloats) noexcept
		{
			if constexpr (PremultiplyAlpha)
			{
				const float alpha = static_cast<float>(sourcePixel[Channels - 1]);
				const float premultiplier = alpha * (1.0f / 255.0f);
				for (size_t channel = 0; channel + 1 < Channels; ++channel)
					pixelFloats[channel] = static_cast<float>(sourcePixel[channel]) * premultiplier;

				pixelFloats[Channels - 1] = alpha;
			}
			else
			{
				for (size_t channel = 0; channel < Channels; ++channel)
					pixelFloats[channel] = static_cast<float>(sourcePixel[channel]);
			}

			if constexpr (sourceFloatsPerPixel(Channels) > Channels)
				pixelFloats[Channels] = 0.0f;
		}

		template <size_t Channels, size_t PixelStride, bool PremultiplyAlpha>
		IMAGE_PROCESSING_SIMD_INLINE void convertPixelsToFloats(const uint8_t* pixels, size_t runtimePixelStride, float* floats, size_t pixelCount) noexcept
		{
			constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
			size_t pixel = 0;

			// A 16-byte load: 16 pixel floats, or 4 RGB pixels and 4 bytes past them
			if constexpr (PixelStride == floatsPerPixel || (Channels == 3 && PixelStride == 3))
			{
				constexpr size_t blockPixels = 16 / floatsPerPixel;
				constexpr size_t loadedPixels = (16 + PixelStride - 1) / PixelStride;
				if (pixelCount >= loadedPixels)
				{
					const size_t blockCount = (pixelCount - loadedPixels) / blockPixels + 1;
					// Stepped pointers and an end pointer: for an index bound MSVC keeps a second counter and indexes the stores
					const uint8_t* blockBytes = pixels;
					float* blockFloats = floats;
					float* const blockFloatsEnd = floats + blockCount * 16;
					for (; blockFloats != blockFloatsEnd; blockBytes += blockPixels * PixelStride, blockFloats += 16)
					{
						Floats8 first, second;
						if constexpr (PixelStride == 3)
							loadFourRgbPixelsAsFloats(blockBytes, first, second);
						else
							loadSixteenBytesAsFloats(blockBytes, first, second);

						if constexpr (PremultiplyAlpha)
						{
							first = premultiplyPixels<floatsPerPixel>(first);
							second = premultiplyPixels<floatsPerPixel>(second);
						}

						storeFloats8(blockFloats, first);
						storeFloats8(blockFloats + 8, second);
					}

					pixel = blockCount * blockPixels;
				}
			}

			// Scalar: a few pixels after the block loop, and Clang's vector form keeps four constants in registers the filter's column loop needs
			IMAGE_PROCESSING_SCALAR_LOOP
			for (; pixel < pixelCount; ++pixel)
				convertPixelToFloats<Channels, PremultiplyAlpha>(pixels + pixel * pixelStride, floats + pixel * floatsPerPixel);
		}

		// A row pair's pixels as floats, interleaved: pixel i of row A at floats [8i, 8i + 4), of row B at [8i + 4, 8i + 8)
		template <size_t Channels, size_t PixelStride, bool PremultiplyAlpha>
		IMAGE_PROCESSING_SIMD_INLINE void convertPixelPairsToFloats(const uint8_t* pixelsA, const uint8_t* pixelsB, size_t runtimePixelStride, float* floats, size_t pixelCount) noexcept
		{
			constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			static_assert(floatsPerPixel == 4);
			const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
			size_t pixel = 0;

			// A 16-byte load per row: 4 pixels, or 4 RGB pixels and 4 bytes past them
			if constexpr (PixelStride == floatsPerPixel || (Channels == 3 && PixelStride == 3))
			{
				constexpr size_t loadedPixels = (16 + PixelStride - 1) / PixelStride;
				if (pixelCount >= loadedPixels)
				{
					const size_t blockCount = (pixelCount - loadedPixels) / 4 + 1;
					// Stepped pointers and an end pointer: see convertPixelsToFloats
					const uint8_t* blockBytesA = pixelsA;
					const uint8_t* blockBytesB = pixelsB;
					float* blockFloats = floats;
					float* const blockFloatsEnd = floats + blockCount * 32;
					for (; blockFloats != blockFloatsEnd; blockBytesA += 4 * PixelStride, blockBytesB += 4 * PixelStride, blockFloats += 32)
					{
						Floats8 pair0, pair1, pair2, pair3;
						if constexpr (PixelStride == 3)
							loadFourRgbPixelPairsAsFloats(blockBytesA, blockBytesB, pair0, pair1, pair2, pair3);
						else
							loadFourPixelPairsAsFloats(blockBytesA, blockBytesB, pair0, pair1, pair2, pair3);

						if constexpr (PremultiplyAlpha)
						{
							pair0 = premultiplyPixels<floatsPerPixel>(pair0);
							pair1 = premultiplyPixels<floatsPerPixel>(pair1);
							pair2 = premultiplyPixels<floatsPerPixel>(pair2);
							pair3 = premultiplyPixels<floatsPerPixel>(pair3);
						}

						storeFloats8(blockFloats, pair0);
						storeFloats8(blockFloats + 8, pair1);
						storeFloats8(blockFloats + 16, pair2);
						storeFloats8(blockFloats + 24, pair3);
					}

					pixel = blockCount * 4;
				}
			}

			// Scalar: see convertPixelsToFloats
			IMAGE_PROCESSING_SCALAR_LOOP
			for (; pixel < pixelCount; ++pixel)
			{
				convertPixelToFloats<Channels, PremultiplyAlpha>(pixelsA + pixel * pixelStride, floats + pixel * 8);
				convertPixelToFloats<Channels, PremultiplyAlpha>(pixelsB + pixel * pixelStride, floats + pixel * 8 + floatsPerPixel);
			}
		}

		// A row group's source pixels as floats, sourceFloatsPerPixel per pixel, over a span that slides along the rows as the x runs advance.
		// Each pixel is converted once: overlapping runs would otherwise convert it once per run.
		// The span stays a few KB so that it lives in L1: whole converted rows overflow the L2 that parallel bands share.
		// Interleaved: a row pair's floats in floats[0] alone, as convertPixelPairsToFloats writes them.
		template <size_t Channels, size_t PixelStride, size_t Rows, bool Interleaved = false>
		struct SlidingSourceFloats
		{
			static_assert(!Interleaved || Rows == 2);
			static constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			static constexpr size_t buffers = Interleaved ? 1 : Rows;
			// From one pixel's floats to the next pixel's in a buffer
			static constexpr size_t pixelFloats = Interleaved ? Rows * floatsPerPixel : floatsPerPixel;

			// Pixels converted ahead of the current run, so that conversion runs in batches
			static constexpr size_t conversionChunk = 128;

			// A slide moves the floats still needed to the front: room for two runs keeps that to about one move per pixel
			[[nodiscard]] static constexpr size_t capacityFor(size_t longestRun) noexcept
			{
				return 2 * longestRun + conversionChunk;
			}

			// Converts whatever of pixels [first, first + count) is missing; returns the float offset of pixel first in each buffer.
			// Requires first at the span's start or later: a row's runs come in order, and they start in order.
			IMAGE_PROCESSING_SIMD_INLINE size_t prepareRun(size_t first, size_t count) noexcept
			{
				assert(count <= capacity && first >= base);
				const size_t end = first + count;
				if (end > base + capacity)
				{
					// The span slides to the run's first pixel
					if (first < converted)
					{
						for (size_t buffer = 0; buffer < buffers; ++buffer)
							::memmove(floats[buffer], floats[buffer] + (first - base) * pixelFloats, (converted - first) * pixelFloats * sizeof(float));
					}
					else
						converted = first; // Nothing converted is reusable: the run starts past the converted end

					base = first;
				}

				if (end > converted)
				{
					// Two-argument min: the initializer-list overload is a library call under MSVC
					const size_t convertedTarget = std::min(std::min(std::max(end, converted + conversionChunk), base + capacity), pixelEnd);
					const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
					if constexpr (Interleaved)
					{
						const uint8_t* pixelsA = pixels[0] + converted * pixelStride;
						const uint8_t* pixelsB = pixels[1] + converted * pixelStride;
						float* pairFloats = floats[0] + (converted - base) * pixelFloats;
						if (hasAlphaChannel(Channels) && premultiplyAlpha)
							convertPixelPairsToFloats<Channels, PixelStride, hasAlphaChannel(Channels)>(pixelsA, pixelsB, pixelStride, pairFloats, convertedTarget - converted);
						else
							convertPixelPairsToFloats<Channels, PixelStride, false>(pixelsA, pixelsB, pixelStride, pairFloats, convertedTarget - converted);
					}
					else
					{
						for (size_t row = 0; row < Rows; ++row)
						{
							const uint8_t* rowPixels = pixels[row] + converted * pixelStride;
							float* rowFloats = floats[row] + (converted - base) * floatsPerPixel;
							if (hasAlphaChannel(Channels) && premultiplyAlpha)
								convertPixelsToFloats<Channels, PixelStride, hasAlphaChannel(Channels)>(rowPixels, pixelStride, rowFloats, convertedTarget - converted);
							else
								convertPixelsToFloats<Channels, PixelStride, false>(rowPixels, pixelStride, rowFloats, convertedTarget - converted);
						}
					}

					converted = convertedTarget;
				}

				return (first - base) * pixelFloats;
			}

			const uint8_t* const pixels[Rows];
			float* const floats[buffers];
			const size_t capacity; // In pixels
			const size_t pixelEnd; // Conversion never reaches this pixel
			const size_t runtimePixelStride;
			const bool premultiplyAlpha;
			size_t base = 0; // The source pixel at floats[row][0]
			size_t converted = 0; // Pixels [base, converted) are in the buffers
		};

		// Floats after each temp row: storeTempPixel writes 4 floats for 3 channels.
		// A cache line of them: a stride off its 64-byte alignment splits the vertical pass's loads.
		[[nodiscard]] constexpr size_t tempRowSlack(size_t channels) noexcept
		{
			return channels == 3 ? 16 : 0;
		}

		// One 4-float store. 3 channels: the fourth float is the next pixel's first, so a row's pixels must be written in order; see tempRowSlack.
		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void storeTempPixel(float* outPixel, Floats4 accum) noexcept
		{
			static_assert(sourceFloatsPerPixel(Channels) == 4);
			storeFloats4(outPixel, accum);
		}

		// Adds one tap of a 1- or 2-float pixel to its channel sums. Not a lambda: one would not carry the SIMD level's target attribute.
		template <size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE void addTap(float& sum0, [[maybe_unused]] float& sum1, const float* sourcePixel, float weight) noexcept
		{
			sum0 = mulAdd(sourcePixel[0], weight, sum0);
			if constexpr (FloatsPerPixel == 2)
				sum1 = mulAdd(sourcePixel[1], weight, sum1);
		}

		// Adds the last taps of a 1- or 2-float pixel channel by channel, then stores it. At most 3 taps.
		template <size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE void storeTempPixelWithTaps(float* outPixel, Floats4 partialSums, const float* sourcePixels, const float* weights, size_t tapCount) noexcept
		{
			// Scalars taken out of the vector, not an array: MSVC takes an array through the stack, twice per pixel
			const Floats4 pixelSums = sumPixels<FloatsPerPixel>(partialSums);
			float sum0 = lane<0>(pixelSums);
			[[maybe_unused]] float sum1 = 0.0f;
			if constexpr (FloatsPerPixel == 2)
				sum1 = lane<1>(pixelSums);

			assert(tapCount <= 3);
			// Steps, not a loop: MSVC unrolls a loop by four, and that code never runs for 3 taps or fewer
			if (tapCount >= 2)
			{
				addTap<FloatsPerPixel>(sum0, sum1, sourcePixels, weights[0]);
				addTap<FloatsPerPixel>(sum0, sum1, sourcePixels + FloatsPerPixel, weights[1]);
			}
			if (tapCount & 1)
				addTap<FloatsPerPixel>(sum0, sum1, sourcePixels + (tapCount - 1) * FloatsPerPixel, weights[tapCount - 1]);

			outPixel[0] = sum0;
			if constexpr (FloatsPerPixel == 2)
				outPixel[1] = sum1;
		}

		// The weights for chain Chain of a block of 32 source floats that starts at blockWeights
		template <size_t Chain, size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 chainWeights(const WeightSpreader<FloatsPerPixel>& spreader, const float* blockWeights, WeightBlock firstWeights) noexcept
		{
			constexpr size_t weightBlock = Chain / FloatsPerPixel;
			if constexpr (weightBlock == 0)
				return spreader.template spread<Chain % FloatsPerPixel>(firstWeights);
			else
				return spreader.template spread<Chain % FloatsPerPixel>(loadWeightBlock(blockWeights + weightBlock * 8));
		}

		// One chain's 8 floats of a block, for both rows. Start: the products start the chain; otherwise they are added to it.
		template <bool Start, size_t Chain, size_t Rows, size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE void filterBlockChain(
			const WeightSpreader<FloatsPerPixel>& spreader,
			const float* blockWeights,
			WeightBlock firstWeights,
			const float* blockPixelsA,
			[[maybe_unused]] const float* blockPixelsB,
			Floats8& chainA,
			[[maybe_unused]] Floats8& chainB) noexcept
		{
			const Floats8 weights = chainWeights<Chain>(spreader, blockWeights, firstWeights);
			if constexpr (Start)
			{
				chainA = mul(loadFloats8(blockPixelsA + Chain * 8), weights);
				if constexpr (Rows == 2)
					chainB = mul(loadFloats8(blockPixelsB + Chain * 8), weights);
			}
			else
			{
				addProduct(chainA, loadFloats8(blockPixelsA + Chain * 8), weights);
				if constexpr (Rows == 2)
					addProduct(chainB, loadFloats8(blockPixelsB + Chain * 8), weights);
			}
		}

		// A row's chains, as separate variables: MSVC moves chains held in an array, or indexed by row, through a second register every block
		struct RowChains
		{
			Floats8 chain0;
			Floats8 chain1;
			Floats8 chain2;
			Floats8 chain3;
		};

		// A block of 8 source floats per chain
		template <bool Start, size_t Rows, size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE void filterBlock(
			const WeightSpreader<FloatsPerPixel>& spreader,
			const float* blockWeights,
			const float* blockPixelsA,
			const float* blockPixelsB,
			RowChains& chainsA,
			RowChains& chainsB) noexcept
		{
			const WeightBlock firstWeights = loadWeightBlock(blockWeights);
			filterBlockChain<Start, 0, Rows>(spreader, blockWeights, firstWeights, blockPixelsA, blockPixelsB, chainsA.chain0, chainsB.chain0);
			filterBlockChain<Start, 1, Rows>(spreader, blockWeights, firstWeights, blockPixelsA, blockPixelsB, chainsA.chain1, chainsB.chain1);
			if constexpr (horizontalChainCount == 4)
			{
				filterBlockChain<Start, 2, Rows>(spreader, blockWeights, firstWeights, blockPixelsA, blockPixelsB, chainsA.chain2, chainsB.chain2);
				filterBlockChain<Start, 3, Rows>(spreader, blockWeights, firstWeights, blockPixelsA, blockPixelsB, chainsA.chain3, chainsB.chain3);
			}
		}

		template <size_t Chains = horizontalChainCount>
		IMAGE_PROCESSING_SIMD_INLINE Floats8 sumChains(const RowChains& chains) noexcept
		{
			static_assert(Chains == 1 || Chains == 2 || Chains == 4);
			if constexpr (Chains == 4)
				return add(add(chains.chain0, chains.chain1), add(chains.chain2, chains.chain3));
			else if constexpr (Chains == 2)
				return add(chains.chain0, chains.chain1);
			else
				return chains.chain0;
		}

		// A run's length: FixedTaps where the x runs are padded to that many taps, readLength where FixedTaps is 0
		template <size_t FixedTaps>
		[[nodiscard]] IMAGE_PROCESSING_SIMD_INLINE size_t runLength([[maybe_unused]] size_t readLength) noexcept
		{
			if constexpr (FixedTaps != 0)
				return FixedTaps;
			else
				return readLength;
		}

		// Hides the pointer's value from Clang and GCC: they rederive a stepped pointer as origin plus offset, and fold that into each load as an index
		IMAGE_PROCESSING_SIMD_INLINE void keepInRegister([[maybe_unused]] const float*& pointer) noexcept
		{
#if defined(__clang__) || defined(__GNUC__)
			__asm__("" : "+r"(pointer));
#endif
		}

		// Blocks Block to BlockCount - 1 of a run, written out: MSVC keeps a loop over a known count as a loop
		template <size_t Block, size_t BlockCount, size_t Rows, size_t FloatsPerPixel>
		IMAGE_PROCESSING_SIMD_INLINE void filterBlocksFrom(
			[[maybe_unused]] const WeightSpreader<FloatsPerPixel>& spreader,
			[[maybe_unused]] const float* runWeights,
			[[maybe_unused]] const float* runPixelsA,
			[[maybe_unused]] const float* runPixelsB,
			[[maybe_unused]] RowChains& chainsA,
			[[maybe_unused]] RowChains& chainsB) noexcept
		{
			if constexpr (Block < BlockCount)
			{
				constexpr size_t blockTaps = horizontalChainCount * 8 / FloatsPerPixel;
				constexpr size_t firstFloat = Block * blockTaps * FloatsPerPixel;
				filterBlock<Block == 0, Rows>(spreader, runWeights + Block * blockTaps, runPixelsA + firstFloat, runPixelsB + firstFloat, chainsA, chainsB);
				// No load moves across the fence, so a block's weight broadcasts stay beside its products.
				// MSVC at SSE4.1 otherwise hoists all of a column's broadcasts ahead of its products: up to 6% slower on Goldmont Plus.
				if constexpr (simdLevel == SimdLevel::Sse41 && compilerIsMsvc)
					std::atomic_signal_fence(std::memory_order_seq_cst);

				filterBlocksFrom<Block + 1, BlockCount, Rows>(spreader, runWeights, runPixelsA, runPixelsB, chainsA, chainsB);
			}
		}

		// Filters Rows source rows in one destination-column sweep. The weight loads, permutes and broadcasts
		// depend only on the column, so paired rows share them, while each row keeps its own accumulators and
		// its exact single-row arithmetic. Two rows is the register budget: the spread constants plus a row pair's
		// chains nearly fill the file, a third row would spill inside the hottest loop.
		// Rows are passed as individual pointers: a pair of temp rows may straddle the ring's wrap.
		// Filters one dest column: each row's run of weights.size() pixels, at pixelsA and pixelsB, into its temp pixel, at outA and outB.
		// The second row's pointers are unused for one row.
		// FixedTaps: see runLength. The steps a run of that length takes are then chosen at compile time.
		template <size_t Channels, size_t Rows, size_t FixedTaps>
		IMAGE_PROCESSING_SIMD_INLINE void filterRun(
			const float* pixelsA,
			[[maybe_unused]] const float* pixelsB,
			std::span<const float> weights,
			const WeightSpreader<sourceFloatsPerPixel(Channels)>& weightSpreader,
			float* outA,
			[[maybe_unused]] float* outB) noexcept
		{
			static_assert(Rows == 1 || Rows == 2);

			constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			// Taps per block: 8 source floats per chain
			constexpr size_t blockTaps = horizontalChainCount * 8 / floatsPerPixel;
			const size_t tapCount = runLength<FixedTaps>(weights.size());
			assert(tapCount == weights.size());

			const float* runPixels[Rows];
			float* outPixels[Rows];
			runPixels[0] = pixelsA;
			outPixels[0] = outA;
			if constexpr (Rows == 2)
			{
				runPixels[1] = pixelsB;
				outPixels[1] = outB;
			}

			// Lanes hold partial sums of 8 / floatsPerPixel pixels until the reductions below the blocks
			Floats8 accumPairs[Rows];
			for (size_t row = 0; row < Rows; ++row)
				accumPairs[row] = zeroFloats8();

			size_t tap = 0;

			// A run is consecutive pixels, so a block goes through independent chains of 8 floats;
			// per-tap accumulation into one register would serialize on the multiply-add latency.
			if constexpr (FixedTaps >= blockTaps)
			{
				RowChains chainsA;
				RowChains chainsB;
				filterBlocksFrom<0, FixedTaps / blockTaps, Rows>(weightSpreader, weights.data(), runPixels[0], runPixels[Rows - 1], chainsA, chainsB);
				tap = FixedTaps / blockTaps * blockTaps;

				accumPairs[0] = sumChains(chainsA);
				if constexpr (Rows == 2)
					accumPairs[1] = sumChains(chainsB);
			}
			else if (tapCount >= blockTaps)
			{
				// The rows' chains by name, not by row index: see RowChains
				RowChains chainsA;
				RowChains chainsB;
				filterBlock<true, Rows>(weightSpreader, weights.data(), runPixels[0], runPixels[Rows - 1], chainsA, chainsB);
				// Four chains - stepped pointers: MSVC addresses an indexed loop's loads by base and scaled index, a micro-op more per multiply-add on Skylake.
				// Two chains - the index: a 12-tap run takes this loop there, and three more pointers push MSVC's column loop onto the stack.
				if constexpr (horizontalChainCount == 4)
				{
					const float* blockWeights = weights.data() + blockTaps;
					const float* blockPixelsA = runPixels[0] + blockTaps * floatsPerPixel;
					const float* blockPixelsB = runPixels[Rows - 1] + blockTaps * floatsPerPixel;
					for (tap = blockTaps; tap + blockTaps <= tapCount; tap += blockTaps)
					{
						keepInRegister(blockPixelsA);
						if constexpr (Rows == 2)
							keepInRegister(blockPixelsB);
						filterBlock<false, Rows>(weightSpreader, blockWeights, blockPixelsA, blockPixelsB, chainsA, chainsB);
						blockWeights += blockTaps;
						blockPixelsA += blockTaps * floatsPerPixel;
						blockPixelsB += blockTaps * floatsPerPixel;
					}
				}
				else
				{
					for (tap = blockTaps; tap + blockTaps <= tapCount; tap += blockTaps)
						filterBlock<false, Rows>(weightSpreader, weights.data() + tap, runPixels[0] + tap * floatsPerPixel, runPixels[Rows - 1] + tap * floatsPerPixel, chainsA, chainsB);
				}

				accumPairs[0] = sumChains(chainsA);
				if constexpr (Rows == 2)
					accumPairs[1] = sumChains(chainsB);
			}

			// [[likely]] on the three steps below is for code placement, not a claim about the run lengths:
			// GCC moves a step whose body is a loop over the rows out of line without it.

			// 16 floats, half a four-chain block: a whole bicubic run of 4-float pixels
			if constexpr (horizontalChainCount == 4)
			{
				if (tap + 16 / floatsPerPixel <= tapCount) [[likely]]
				{
					// A whole WeightBlock though only 4 weights may be in play: the builder pads the weights array to keep the overread
					// in bounds. On AVX2 the natural 4-float load + castps128_ps256 compiles under MSVC to a 16-byte stack store that the
					// 32-byte vpermps memory operand then reloads, and a load wider than the store it overlaps cannot be
					// store-forwarded - a ~35-cycle stall, measured to roughly double the upscale pass.
					const float* blockWeights = weights.data() + tap;
					const WeightBlock firstWeights = loadWeightBlock(blockWeights);
					const Floats8 w0 = chainWeights<0>(weightSpreader, blockWeights, firstWeights);
					const Floats8 w1 = chainWeights<1>(weightSpreader, blockWeights, firstWeights);
					for (size_t row = 0; row < Rows; ++row)
					{
						const float* blockPixels = runPixels[row] + tap * floatsPerPixel;
						// A run known to be shorter than a block starts its sums here, with no zero to add to
						if constexpr (FixedTaps != 0 && FixedTaps < blockTaps)
							accumPairs[row] = mul(loadFloats8(blockPixels + 8), w1);
						else
							addProduct(accumPairs[row], loadFloats8(blockPixels + 8), w1);

						addProduct(accumPairs[row], loadFloats8(blockPixels), w0);
					}

					tap += 16 / floatsPerPixel;
				}
			}

			// 8 floats: a whole bicubic run of 2-float pixels
			if (tap + 8 / floatsPerPixel <= tapCount) [[likely]]
			{
				const float* blockWeights = weights.data() + tap;
				const Floats8 w0 = chainWeights<0>(weightSpreader, blockWeights, loadWeightBlock(blockWeights));
				for (size_t row = 0; row < Rows; ++row)
					addProduct(accumPairs[row], loadFloats8(runPixels[row] + tap * floatsPerPixel), w0);

				tap += 8 / floatsPerPixel;
			}

			Floats4 accum[Rows];
			for (size_t row = 0; row < Rows; ++row)
				accum[row] = sumHalves(accumPairs[row]);

			// 4 taps of 1-float pixels
			if constexpr (floatsPerPixel == 1)
			{
				if (tap + 4 <= tapCount) [[likely]]
				{
					const Floats4 w = loadFloats4(weights.data() + tap);
					for (size_t row = 0; row < Rows; ++row)
						accum[row] = mulAdd(loadFloats4(runPixels[row] + tap), w, accum[row]);

					tap += 4;
				}
			}

			if constexpr (floatsPerPixel == 4)
			{
				// One tap at most, as a step: see storeTempPixelWithTaps
				assert(tapCount - tap <= 1);
				if (tap < tapCount)
				{
					const Floats4 weight = broadcastFloats4(weights[tap]);
					for (size_t row = 0; row < Rows; ++row)
						accum[row] = mulAdd(loadFloats4(runPixels[row] + tap * 4), weight, accum[row]);
				}

				for (size_t row = 0; row < Rows; ++row)
					storeTempPixel<Channels>(outPixels[row], accum[row]);
			}
			else
			{
				const size_t remainingTaps = tapCount - tap;
				// One test for both rows where no taps remain: the constant 0 folds the tap steps away
				if (remainingTaps == 0)
				{
					storeTempPixelWithTaps<floatsPerPixel>(outA, accum[0], runPixels[0], weights.data(), 0);
					if constexpr (Rows == 2)
						storeTempPixelWithTaps<floatsPerPixel>(outB, accum[1], runPixels[1], weights.data(), 0);
					return;
				}

				// Not a loop over the rows: when the body held a loop, GCC left such an outer loop rolled, with the rows' state in memory.
				// Not retried with the tap steps.
				storeTempPixelWithTaps<floatsPerPixel>(outA, accum[0], runPixels[0] + tap * floatsPerPixel, weights.data() + tap, remainingTaps);
				if constexpr (Rows == 2)
					storeTempPixelWithTaps<floatsPerPixel>(outB, accum[1], runPixels[1] + tap * floatsPerPixel, weights.data() + tap, remainingTaps);
			}
		}

		// Chains of an interleaved column. One at 4 taps: 4 chains would take 4 multiplies and 3 adds for its 1 multiply and 3 multiply-adds.
		[[nodiscard]] constexpr size_t interleavedChainCount(size_t taps) noexcept
		{
			return taps == 4 ? 1 : 4;
		}

		// Taps Tap to Taps - 1 of an interleaved column, written out: MSVC keeps a loop over a known count as a loop.
		// Tap i goes to chain i % Chains; the first Chains taps start the chains. fourWeights: the weights of taps Tap / 4 * 4 on.
		template <size_t Tap, size_t Taps, size_t Chains>
		IMAGE_PROCESSING_SIMD_INLINE void filterInterleavedTapsFrom(const float* pixelPairs, const float* weights, FourWeights fourWeights, RowChains& chains) noexcept
		{
			if constexpr (Tap < Taps)
			{
				if constexpr (Tap % 4 == 0 && Tap != 0)
					fourWeights = loadFourWeights(weights + Tap);

				const Floats8 weight = weightOfTap<Tap % 4>(fourWeights);
				const Floats8 pixelPair = loadFloats8(pixelPairs + Tap * 8);
				constexpr size_t chain = Tap % Chains;
				Floats8& chainSum = chain == 0 ? chains.chain0 : chain == 1 ? chains.chain1 : chain == 2 ? chains.chain2 : chains.chain3;
				if constexpr (Tap < Chains)
					chainSum = mul(pixelPair, weight);
				else
					addProduct(chainSum, pixelPair, weight);

				filterInterleavedTapsFrom<Tap + 1, Taps, Chains>(pixelPairs, weights, fourWeights, chains);
			}
		}

		// filterRun for an interleaved row pair of 4-float pixels, runs of FixedTaps taps: a pixel pair per Floats8, row A's pixel in its low half.
		// A tap is one multiply-add of the pair by the tap's weight; the low half of the sum is row A's temp pixel, the high half row B's.
		template <size_t Channels, size_t FixedTaps>
		IMAGE_PROCESSING_SIMD_INLINE void filterInterleavedRun(const float* pixelPairs, std::span<const float> weights, float* outA, float* outB) noexcept
		{
			static_assert(sourceFloatsPerPixel(Channels) == 4 && FixedTaps != 0);
			assert(weights.size() == FixedTaps);

			constexpr size_t chainCount = interleavedChainCount(FixedTaps);
			RowChains chains;
			filterInterleavedTapsFrom<0, FixedTaps, chainCount>(pixelPairs, weights.data(), loadFourWeights(weights.data()), chains);
			const Floats8 sum = sumChains<chainCount>(chains);
			storeTempPixel<Channels>(outA, lowHalf(sum));
			storeTempPixel<Channels>(outB, highHalf(sum));
		}

		// Filters dest columns from firstColumn on, into the temp rows at their offset from destBegin, while their runs end within
		// source pixels [bufferBegin, bufferEnd). Returns the first column not filtered.
		// Requires firstColumn's run to start at bufferBegin or later: the runs start in order, so no later one is tested for it.
		// sourceFloats: SlidingSourceFloats' buffers, from pixel bufferBegin on. Interleaved: see SlidingSourceFloats.
		// The pixel range by value: a constant of this loop, where the members of SlidingSourceFloats are not.
		// FixedTaps: see runLength. Requires firstColumn's run within the range then.
		template <size_t Channels, size_t FixedTaps, bool Interleaved, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE size_t filterBufferedRuns(
			float* const (&sourceFloats)[Interleaved ? 1 : Rows],
			size_t bufferBegin,
			size_t bufferEnd,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t firstColumn,
			size_t destEnd,
			const TapRun* xRuns) noexcept
		{
			constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
			constexpr size_t pixelFloats = Interleaved ? Rows * floatsPerPixel : floatsPerPixel;
			[[maybe_unused]] const WeightSpreader<floatsPerPixel> weightSpreader{};

			assert(firstColumn <= destEnd);
			assert(bufferEnd >= bufferBegin + FixedTaps);
			// The last pixel a run of FixedTaps may start at
			[[maybe_unused]] const size_t lastStart = bufferEnd - FixedTaps;
			// No column index: the run and the temp pixels are stepped, where an index makes every column form them from bases kept across the loop.
			// The rows' floats as if they began at source pixel 0, for filterRun: a run's pixels are one scaled add away.
			// Before the allocation unless bufferBegin is 0, and never read below bufferBegin.
			[[maybe_unused]] const float* const originA = sourceFloats[0] - bufferBegin * pixelFloats;
			[[maybe_unused]] const float* const originB = sourceFloats[Interleaved ? 0 : Rows - 1] - bufferBegin * pixelFloats;
			// Interleaved: the run's pixel pairs stepped from the previous run's, so each load addresses one register and a constant.
			// From an origin, the compilers fold the offset into each load as an index, and Skylake splits an indexed multiply-add in two uops.
			[[maybe_unused]] const float* runPixelPairs = sourceFloats[0];
			[[maybe_unused]] size_t previousFirstPixel = bufferBegin;
			float* outA = tempRows[0] + (firstColumn - destBegin) * Channels;
			float* outB = tempRows[Rows - 1] + (firstColumn - destBegin) * Channels;
			const TapRun* run = xRuns + firstColumn;
			const TapRun* const runsEnd = xRuns + destEnd;
			for (; run != runsEnd; ++run) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				const size_t firstPixel = run->firstSource;
				assert(firstPixel >= bufferBegin);
				if constexpr (FixedTaps != 0)
				{
					if (firstPixel > lastStart)
						break;
				}
				else if (firstPixel + run->weightCount > bufferEnd)
					break;

				if constexpr (Interleaved)
				{
					runPixelPairs += (firstPixel - previousFirstPixel) * pixelFloats;
					keepInRegister(runPixelPairs);
					previousFirstPixel = firstPixel;
					filterInterleavedRun<Channels, FixedTaps>(runPixelPairs, { run->weights, run->weightCount }, outA, outB);
				}
				else
				{
					const size_t floatOffset = firstPixel * pixelFloats;
					filterRun<Channels, Rows, FixedTaps>(originA + floatOffset, originB + floatOffset, { run->weights, run->weightCount }, weightSpreader, outA, outB);
				}
				outA += Channels;
				if constexpr (Rows == 2)
					outB += Channels;
			}

			return static_cast<size_t>(run - xRuns);
		}

		// Filters dest columns [destBegin, destEnd) into the temp rows' start. FixedTaps: see runLength.
		template <size_t FixedTaps, size_t Channels, size_t PixelStride, size_t Rows, bool Interleaved>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontalRowGroup(
			SlidingSourceFloats<Channels, PixelStride, Rows, Interleaved>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights) noexcept
		{
			// The runs' array read once: the loops' stores may alias the AxisWeights, and would force a reload per column
			const TapRun* const xRuns = xWeights.runs.data();

			for (size_t dx = destBegin; dx < destEnd;) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				// The column's run is in the buffers after this, so each pass filters at least that column
				source.prepareRun(xRuns[dx].firstSource, runLength<FixedTaps>(xRuns[dx].weightCount));
				dx = filterBufferedRuns<Channels, FixedTaps, Interleaved>(source.floats, source.base, source.converted, tempRows, destBegin, dx, destEnd, xRuns);
			}
		}

		// The passes below require an AxisWeights::paddedRunLength of 4: a run is 4 source pixels and 4 weights, with no count to branch on.

		// One dest column of a group of 1-float pixels: each row's run at runFloatOffset times its weights, lane by lane
		template <size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void multiplyFourTapRun(
			const float* floatsA,
			[[maybe_unused]] const float* floatsB,
			size_t runFloatOffset,
			std::span<const float> weights,
			Floats4& productsA,
			[[maybe_unused]] Floats4& productsB) noexcept
		{
			assert(weights.size() == 4);
			const Floats4 runWeights = loadFloats4(weights.data());
			productsA = mul(loadFloats4(floatsA + runFloatOffset), runWeights);
			if constexpr (Rows == 2)
				productsB = mul(loadFloats4(floatsB + runFloatOffset), runWeights);
		}

		// multiplyFourTapRun for a run prepared first
		template <size_t PixelStride, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void prepareAndMultiplyFourTapRun(
			SlidingSourceFloats<1, PixelStride, Rows>& source,
			const TapRun& run,
			Floats4& productsA,
			Floats4& productsB) noexcept
		{
			const size_t runFloatOffset = source.prepareRun(run.firstSource, 4);
			multiplyFourTapRun<Rows>(source.floats[0], source.floats[Rows - 1], runFloatOffset, { run.weights, run.weightCount }, productsA, productsB);
		}

		// Filters whole groups of four dest columns of 1-float pixels from firstGroup on, into the temp rows at their offset from
		// destBegin, while every column's run lies within source pixels [bufferBegin, bufferEnd). Returns the first group not filtered.
		// sourceFloats, and the range by value: see filterBufferedRuns.
		template <size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE size_t filterBufferedFourTapRunGroups(
			float* const (&sourceFloats)[Rows], size_t bufferBegin, size_t bufferEnd,
			float* const (&tempRows)[Rows], size_t destBegin, size_t firstGroup, size_t destEnd,
			const TapRun* xRuns) noexcept
		{
			if (bufferEnd < bufferBegin + 4)
				return firstGroup;

			assert(firstGroup <= destEnd);
			const size_t lastOffset = bufferEnd - bufferBegin - 4;
			const float* const floatsA = sourceFloats[0];
			const float* const floatsB = sourceFloats[Rows - 1];
			// No column index: see filterBufferedRuns
			float* outA = tempRows[0] + (firstGroup - destBegin);
			[[maybe_unused]] float* outB = tempRows[Rows - 1] + (firstGroup - destBegin);
			const TapRun* run = xRuns + firstGroup;
			const TapRun* const groupsEnd = run + (destEnd - firstGroup) / 4 * 4;
			for (; run != groupsEnd; run += 4) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				assert(run[0].weightCount == 4 && run[1].weightCount == 4 && run[2].weightCount == 4 && run[3].weightCount == 4);
				assert(run[0].firstSource >= bufferBegin);
				const size_t offset0 = run[0].firstSource - bufferBegin, offset1 = run[1].firstSource - bufferBegin;
				const size_t offset2 = run[2].firstSource - bufferBegin, offset3 = run[3].firstSource - bufferBegin;
				// ||, not |: MSVC materializes each comparison of | in a register
				if (offset0 > lastOffset || offset1 > lastOffset || offset2 > lastOffset || offset3 > lastOffset)
					break;

				Floats4 productsA0, productsA1, productsA2, productsA3;
				[[maybe_unused]] Floats4 productsB0, productsB1, productsB2, productsB3;
				multiplyFourTapRun<Rows>(floatsA, floatsB, offset0, { run[0].weights, 4 }, productsA0, productsB0);
				multiplyFourTapRun<Rows>(floatsA, floatsB, offset1, { run[1].weights, 4 }, productsA1, productsB1);
				multiplyFourTapRun<Rows>(floatsA, floatsB, offset2, { run[2].weights, 4 }, productsA2, productsB2);
				multiplyFourTapRun<Rows>(floatsA, floatsB, offset3, { run[3].weights, 4 }, productsA3, productsB3);

				storeFloats4(outA, sumEachOfFour(productsA0, productsA1, productsA2, productsA3));
				outA += 4;
				if constexpr (Rows == 2)
				{
					storeFloats4(outB, sumEachOfFour(productsB0, productsB1, productsB2, productsB3));
					outB += 4;
				}
			}

			return static_cast<size_t>(run - xRuns);
		}

		// filterHorizontalRowGroup for 1-float pixels: four dest columns share one reduction and one store
		template <size_t PixelStride, size_t Rows>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontalFourTapRunGroups(
			SlidingSourceFloats<1, PixelStride, Rows>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights) noexcept
		{
			// Read once: see filterHorizontalRowGroup
			const TapRun* const xRuns = xWeights.runs.data();

			for (size_t groupBegin = destBegin; groupBegin < destEnd; groupBegin += 4) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				groupBegin = filterBufferedFourTapRunGroups<Rows>(source.floats, source.base, source.converted, tempRows, destBegin, groupBegin, destEnd, xRuns);
				if (groupBegin >= destEnd)
					break;

				// A group not wholly in the buffers, or the last, partial one: prepares each column's run.
				// The columns by name, not in a loop over an array: MSVC leaves that loop rolled, with the products in memory.
				// A group past the last column repeats it.
				const size_t lastColumn = destEnd - 1;
				Floats4 productsA0, productsA1, productsA2, productsA3;
				[[maybe_unused]] Floats4 productsB0, productsB1, productsB2, productsB3;
				prepareAndMultiplyFourTapRun(source, xRuns[std::min(groupBegin, lastColumn)], productsA0, productsB0);
				prepareAndMultiplyFourTapRun(source, xRuns[std::min(groupBegin + 1, lastColumn)], productsA1, productsB1);
				prepareAndMultiplyFourTapRun(source, xRuns[std::min(groupBegin + 2, lastColumn)], productsA2, productsB2);
				prepareAndMultiplyFourTapRun(source, xRuns[std::min(groupBegin + 3, lastColumn)], productsA3, productsB3);

				const size_t columnCount = std::min<size_t>(4, destEnd - groupBegin);
				Floats4 sums[Rows] = { sumEachOfFour(productsA0, productsA1, productsA2, productsA3) };
				if constexpr (Rows == 2)
					sums[1] = sumEachOfFour(productsB0, productsB1, productsB2, productsB3);

				for (size_t row = 0; row < Rows; ++row)
				{
					float* const out = tempRows[row] + (groupBegin - destBegin);
					if (columnCount == 4)
						storeFloats4(out, sums[row]);
					else
					{
						alignas(16) float columnSums[4];
						storeFloats4(columnSums, sums[row]);
						::memcpy(out, columnSums, columnCount * sizeof(float));
					}
				}
			}
		}

		// XRunLength: xWeights.paddedRunLength
		template <size_t XRunLength, size_t Channels, size_t PixelStride, size_t Rows, bool Interleaved>
		IMAGE_PROCESSING_SIMD_INLINE void filterHorizontal(
			SlidingSourceFloats<Channels, PixelStride, Rows, Interleaved>& source,
			float* const (&tempRows)[Rows],
			size_t destBegin,
			size_t destEnd,
			const AxisWeights& xWeights) noexcept
		{
			if constexpr (XRunLength == 4 && Channels == 1)
				filterHorizontalFourTapRunGroups(source, tempRows, destBegin, destEnd, xWeights);
			else
				filterHorizontalRowGroup<XRunLength>(source, tempRows, destBegin, destEnd, xWeights);
		}

		// A block's pixels, color capped at alpha, as bytes packed Channels per pixel
		template <size_t Channels, BlockTarget Target>
		IMAGE_PROCESSING_SIMD_INLINE void writeBlockBytes(uint8_t* dest, Floats8 values0, Floats8 values1, Floats8 values2, [[maybe_unused]] Floats8 values3) noexcept
		{
			if constexpr (Channels == 3)
				writeTwentyFourBytes<Target>(dest, values0, values1, values2);
			else if constexpr (hasAlphaChannel(Channels))
				writeThirtyTwoBytesCappedAtAlpha<Target, Channels>(dest, values0, values1, values2, values3);
			else
				writeThirtyTwoBytes<Target>(dest, values0, values1, values2, values3);
		}

		// A vertical block's accumulators, by name: see RowChains
		struct BlockAccums
		{
			Floats8 accum0;
			Floats8 accum1;
			Floats8 accum2;
			Floats8 accum3;
		};

		// Adds one tap to a block: its BlockFloats floats at source, times the weight
		template <size_t BlockFloats>
		IMAGE_PROCESSING_SIMD_INLINE void addTapToBlock(BlockAccums& block, const float* source, Floats8 weightVector) noexcept
		{
			addProduct(block.accum0, loadFloats8(source), weightVector);
			addProduct(block.accum1, loadFloats8(source + 8), weightVector);
			addProduct(block.accum2, loadFloats8(source + 16), weightVector);
			if constexpr (BlockFloats == 32)
				addProduct(block.accum3, loadFloats8(source + 24), weightVector);
		}

		// Starts a block with one tap
		template <size_t BlockFloats>
		IMAGE_PROCESSING_SIMD_INLINE void assignTapToBlock(BlockAccums& block, const float* source, Floats8 weightVector) noexcept
		{
			block.accum0 = mul(loadFloats8(source), weightVector);
			block.accum1 = mul(loadFloats8(source + 8), weightVector);
			block.accum2 = mul(loadFloats8(source + 16), weightVector);
			if constexpr (BlockFloats == 32)
				block.accum3 = mul(loadFloats8(source + 24), weightVector);
		}

		// One dest row's y taps, in the form the level's verticalTapsListed selects.
		// ListedTaps: the nonzero taps, each one's temp row and weight. count >= 1.
		struct ListedTaps
		{
			const float* const* rows;
			const float* weights;
			size_t count;
		};

		// WalkedTaps: the tap window as it sits in the ring. weights covers both segments in order, zeros included.
		struct WalkedTaps
		{
			std::array<TempRowSegment, 2> segments;
			const float* weights;
			size_t rowStride;
		};

		using DestRowTaps = std::conditional_t<verticalTapsListed, ListedTaps, WalkedTaps>;

		// The taps of the window in segments. listedRows, listedWeights: storage for ListedTaps, rowWeights.size() entries each.
		template <typename Taps = DestRowTaps>
		IMAGE_PROCESSING_SIMD_INLINE Taps destRowTaps(
			const std::array<TempRowSegment, 2>& segments,
			std::span<const float> rowWeights,
			size_t tempRowStride,
			[[maybe_unused]] const float** listedRows,
			[[maybe_unused]] float* listedWeights) noexcept
		{
			if constexpr (std::is_same_v<Taps, ListedTaps>)
			{
				size_t count = 0;
				const float* weight = rowWeights.data();
				for (const TempRowSegment& segment : segments)
				{
					const float* row = segment.firstRow;
					for (const float* segmentEnd = weight + segment.rowCount; weight != segmentEnd; ++weight)
					{
						if (*weight != 0.0f)
						{
							listedRows[count] = row;
							listedWeights[count] = *weight;
							++count;
						}

						row += tempRowStride;
					}
				}

				assert(count > 0);
				return { listedRows, listedWeights, count };
			}
			else
				return { segments, rowWeights.data(), tempRowStride };
		}

		// Filters a dest row's taps into Blocks adjacent blocks: each tap's floats from sourceOffset on in its temp row.
		// The first tap starts the accumulators.
		template <size_t BlockFloats, size_t Blocks>
		IMAGE_PROCESSING_SIMD_INLINE void filterTapsIntoBlocks(const ListedTaps& taps, size_t sourceOffset, BlockAccums& first, [[maybe_unused]] BlockAccums& second) noexcept
		{
			{
				const Floats8 weightVector = broadcastFloats8(taps.weights[0]);
				const float* const source = taps.rows[0] + sourceOffset;
				assignTapToBlock<BlockFloats>(first, source, weightVector);
				if constexpr (Blocks == 2)
					assignTapToBlock<BlockFloats>(second, source + BlockFloats, weightVector);
			}

			for (size_t tap = 1; tap < taps.count; ++tap)
			{
				const Floats8 weightVector = broadcastFloats8(taps.weights[tap]);
				const float* const source = taps.rows[tap] + sourceOffset;
				addTapToBlock<BlockFloats>(first, source, weightVector);
				if constexpr (Blocks == 2)
					addTapToBlock<BlockFloats>(second, source + BlockFloats, weightVector);
			}
		}

		template <size_t BlockFloats, size_t Blocks>
		IMAGE_PROCESSING_SIMD_INLINE void filterTapsIntoBlocks(const WalkedTaps& taps, size_t sourceOffset, BlockAccums& first, [[maybe_unused]] BlockAccums& second) noexcept
		{
			const float* weight = taps.weights;
			// A window's first segment holds at least one row. Its tap is not tested against zero: the product is then 0.
			{
				const Floats8 weightVector = broadcastFloats8(*weight);
				const float* const source = taps.segments[0].firstRow + sourceOffset;
				assignTapToBlock<BlockFloats>(first, source, weightVector);
				if constexpr (Blocks == 2)
					assignTapToBlock<BlockFloats>(second, source + BlockFloats, weightVector);

				++weight;
			}

			const std::array<TempRowSegment, 2> restSegments{ { { taps.segments[0].firstRow + taps.rowStride, taps.segments[0].rowCount - 1 }, taps.segments[1] } };
			for (const TempRowSegment& segment : restSegments)
			{
				const float* source = segment.firstRow + sourceOffset;

				// A zero tap costs a whole row sweep, and exact-ratio downscales produce them
				// (the kernels are zero at integer offsets)
				for (const float* segmentEnd = weight + segment.rowCount; weight != segmentEnd; ++weight)
				{
					if (*weight != 0.0f)
					{
						const Floats8 weightVector = broadcastFloats8(*weight);
						addTapToBlock<BlockFloats>(first, source, weightVector);
						if constexpr (Blocks == 2)
							addTapToBlock<BlockFloats>(second, source + BlockFloats, weightVector);
					}

					source += taps.rowStride;
				}
			}
		}

		// Filters a dest row's taps into one pixel: Channels floats from sourceOffset on in each tap's temp row
		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void filterTapsIntoPixel(const ListedTaps& taps, size_t sourceOffset, std::array<float, Channels>& accum) noexcept
		{
			for (size_t tap = 0; tap < taps.count; ++tap)
			{
				const float* const source = taps.rows[tap] + sourceOffset;
				for (size_t channel = 0; channel < Channels; ++channel)
					accum[channel] = mulAdd(source[channel], taps.weights[tap], accum[channel]);
			}
		}

		template <size_t Channels>
		IMAGE_PROCESSING_SIMD_INLINE void filterTapsIntoPixel(const WalkedTaps& taps, size_t sourceOffset, std::array<float, Channels>& accum) noexcept
		{
			const float* weight = taps.weights;
			for (const TempRowSegment& segment : taps.segments)
			{
				const float* source = segment.firstRow + sourceOffset;
				for (const float* segmentEnd = weight + segment.rowCount; weight != segmentEnd; ++weight)
				{
					if (*weight != 0.0f)
					{
						for (size_t channel = 0; channel < Channels; ++channel)
							accum[channel] = mulAdd(source[channel], *weight, accum[channel]);
					}

					source += taps.rowStride;
				}
			}
		}

		// Writes a block's pixels at blockDest, each followed by its tail bytes
		template <size_t Channels, size_t PixelStride>
		IMAGE_PROCESSING_SIMD_INLINE void writeVerticalBlock(
			uint8_t* blockDest,
			const BlockAccums& block,
			[[maybe_unused]] Rgb32PixelTails pixelTails,
			[[maybe_unused]] const uint8_t* pixelTail,
			[[maybe_unused]] size_t pixelStride) noexcept
		{
			if constexpr (Channels == 3 && PixelStride == 4)
				writeEightRgb32Pixels(blockDest, block.accum0, block.accum1, block.accum2, pixelTails);
			else if constexpr (PixelStride == Channels)
				writeBlockBytes<Channels, BlockTarget::Destination>(blockDest, block.accum0, block.accum1, block.accum2, block.accum3);
			else
			{
				constexpr size_t pixelsPerBlock = verticalBlockPixels(Channels);
				alignas(16) uint8_t blockBytes[pixelsPerBlock * Channels];
				writeBlockBytes<Channels, BlockTarget::Scratch>(blockBytes, block.accum0, block.accum1, block.accum2, block.accum3);
				for (size_t blockPixel = 0; blockPixel < pixelsPerBlock; ++blockPixel)
				{
					uint8_t* const destPixel = blockDest + blockPixel * pixelStride;
					::memcpy(destPixel, blockBytes + blockPixel * Channels, Channels);
					::memcpy(destPixel + Channels, pixelTail, pixelStride - Channels);
				}
			}
		}

		// Filters Blocks adjacent vertical blocks of one dest row, from dest pixel firstPixel on, and writes them.
		// Parameters as filterVerticalDestRow's.
		template <size_t Channels, size_t PixelStride, size_t Blocks>
		IMAGE_PROCESSING_SIMD_INLINE void filterVerticalBlocks(
			const DestRowTaps& taps,
			[[maybe_unused]] Rgb32PixelTails pixelTails,
			[[maybe_unused]] const uint8_t* pixelTail,
			size_t pixelStride,
			uint8_t* destRow,
			size_t firstPixel) noexcept
		{
			static_assert(Blocks == 1 || Blocks == 2);
			constexpr size_t pixelsPerBlock = verticalBlockPixels(Channels);
			constexpr size_t blockFloats = pixelsPerBlock * Channels;

			// The blocks by name, not in an array: see RowChains
			BlockAccums first{ zeroFloats8(), zeroFloats8(), zeroFloats8(), zeroFloats8() };
			[[maybe_unused]] BlockAccums second{ zeroFloats8(), zeroFloats8(), zeroFloats8(), zeroFloats8() };
			filterTapsIntoBlocks<blockFloats, Blocks>(taps, firstPixel * Channels, first, second);

			uint8_t* const blockDest = destRow + firstPixel * pixelStride;
			writeVerticalBlock<Channels, PixelStride>(blockDest, first, pixelTails, pixelTail, pixelStride);
			if constexpr (Blocks == 2)
				writeVerticalBlock<Channels, PixelStride>(blockDest + pixelsPerBlock * pixelStride, second, pixelTails, pixelTail, pixelStride);
		}

		// Writes one destination row from its y taps.
		// pixelTail: the bytes past the channels, up to pixelStride, that every dest pixel gets.
		template <size_t Channels, size_t PixelStride>
		IMAGE_PROCESSING_SIMD_INLINE void filterVerticalDestRow(
			const DestRowTaps& taps,
			const uint8_t* pixelTail,
			size_t runtimePixelStride,
			uint8_t* destRow,
			size_t destWidth) noexcept
		{
			const size_t pixelStride = effectivePixelStride<PixelStride>(runtimePixelStride);
			constexpr size_t pixelsPerBlock = verticalBlockPixels(Channels);
			const size_t blockedPixelCount = destWidth & ~(pixelsPerBlock - 1);
			[[maybe_unused]] Rgb32PixelTails pixelTails{};
			if constexpr (Channels == 3 && PixelStride == 4)
				pixelTails = rgb32PixelTails(pixelTail[0]);

			size_t pixel = 0;
			if constexpr (verticalBlocksPerStep == 2)
			{
				for (; pixel + 2 * pixelsPerBlock <= blockedPixelCount; pixel += 2 * pixelsPerBlock) IMAGE_PROCESSING_FORCE_INLINE_CALLS
					filterVerticalBlocks<Channels, PixelStride, 2>(taps, pixelTails, pixelTail, pixelStride, destRow, pixel);
			}

			for (; pixel < blockedPixelCount; pixel += pixelsPerBlock) IMAGE_PROCESSING_FORCE_INLINE_CALLS
				filterVerticalBlocks<Channels, PixelStride, 1>(taps, pixelTails, pixelTail, pixelStride, destRow, pixel);

			for (; pixel < destWidth; ++pixel) IMAGE_PROCESSING_FORCE_INLINE_CALLS
			{
				std::array<float, Channels> accum{};
				filterTapsIntoPixel<Channels>(taps, pixel * Channels, accum);

				uint8_t* const destPixel = destRow + pixel * pixelStride;
				writePixelBytes(destPixel, accum.data(), Channels);
				if constexpr (PixelStride != Channels)
					::memcpy(destPixel + Channels, pixelTail, pixelStride - Channels);
			}
		}
	}

	// Fully resizes destination rows [destRowBegin, destRowEnd), one column strip at a time, producing temp rows in pairs into a TempRowRing.
	// The ring is also what lets the pair write two store streams safely: into cold full-size temp, the interleaved streams
	// defeat the prefetch that hides each line's ownership read (measured ~1.2 cycles per temp byte, and software prefetch
	// does not recover it) - the ring is rewritten every few rows and stays cache-owned.
	// XRunLength: xWeights.paddedRunLength. A template parameter: the horizontal pass has no run length to read or branch on,
	// and held through the loops as a flag, the choice of pass cost MSVC a stack reload per column in the general one.
	template <size_t Channels, size_t PixelStride, size_t XRunLength>
	IMAGE_PROCESSING_FLATTEN IMAGE_PROCESSING_SIMD_TARGET void resizeRowsWith(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		size_t stripWidth,
		uint64_t destRowBegin,
		uint64_t destRowEnd)
	{
		static_assert(Channels >= 1 && Channels <= 4);
		assert(destRowBegin < destRowEnd);
		assert(PixelStride == 0 || PixelStride == source.pixelStrideBytes);

		constexpr size_t floatsPerPixel = sourceFloatsPerPixel(Channels);
		// Padded runs only: interleaved, runs read per run lose the big cores up to 36% to filterRun's form
		constexpr bool interleavedPair = interleavedRowPairs && floatsPerPixel == 4 && XRunLength != 0;
		using SourceRowPair = SlidingSourceFloats<Channels, PixelStride, 2, interleavedPair>;
		using SourceRow = SlidingSourceFloats<Channels, PixelStride, 1>;

		const size_t pixelStride = effectivePixelStride<PixelStride>(source.pixelStrideBytes);
		const size_t destWidth = static_cast<size_t>(dest.width);
		const size_t tempRowStride = stripWidth * Channels + tempRowSlack(Channels);

		const TempRowRing ring{ yWeights, destRowBegin, destRowEnd, tempRowStride, 2, tempRingStartOffset };
		// ListedTaps' arrays, for the longest y run
		const size_t listedTapCapacity = verticalTapsListed ? yWeights.longestRun() : 0;
		const auto listedTapRows = std::make_unique_for_overwrite<const float*[]>(listedTapCapacity);
		const auto listedTapWeights = std::make_unique_for_overwrite<float[]>(listedTapCapacity);

		assert(XRunLength == xWeights.paddedRunLength);
		const size_t sourceFloatsCapacity = SourceRowPair::capacityFor(xWeights.longestRun());
		const AlignedFloats sourceFloats = makeAlignedFloats(2 * sourceFloatsCapacity * floatsPerPixel);
		float* const sourceFloatsA = sourceFloats.get();
		[[maybe_unused]] float* const sourceFloatsB = sourceFloatsA + sourceFloatsCapacity * floatsPerPixel;
		const uint64_t firstPixelOffset = srcRect.left * pixelStride;
		const auto sourcePixels = [&source, srcRect, firstPixelOffset](uint64_t srcRow) noexcept
		{
			return source.scanLine<uint8_t>(srcRect.top + srcRow) + firstPixelOffset;
		};
		const bool premultiplyAlpha = hasStraightAlpha(source);
		// Every dest pixel copies its bytes past the channels from the first source pixel
		const uint8_t* const pixelTail = sourcePixels(0) + Channels;

		for (size_t stripBegin = 0; stripBegin < destWidth; stripBegin += stripWidth)
		{
			const size_t stripEnd = std::min(stripBegin + stripWidth, destWidth);
			// Conversion runs ahead in chunks, and must not pass the strip's source pixels
			const size_t spanEnd = xWeights.sourceEnd(stripBegin, stripEnd);

			uint64_t produced = ring.firstNeededRow();
			for (uint64_t dy = destRowBegin; dy < destRowEnd; ++dy)
			{
				const auto [firstWindowRow, rowWeights] = yWeights.runFor(dy);
				const uint64_t windowEnd = firstWindowRow + rowWeights.size();
				assert(windowEnd <= srcRect.h);

				while (produced < windowEnd)
				{
					if constexpr (interleavedPair)
					{
						// The odd last row, paired with itself: there is no single row in this layout
						const uint64_t secondRow = produced + 2 <= srcRect.h ? produced + 1 : produced;
						SourceRowPair sourceRows{ { sourcePixels(produced), sourcePixels(secondRow) }, { sourceFloatsA }, sourceFloatsCapacity, spanEnd, pixelStride, premultiplyAlpha };
						float* const tempRows[2] = { ring.row(produced), ring.row(secondRow) };
						filterHorizontal<XRunLength>(sourceRows, tempRows, stripBegin, stripEnd, xWeights);
						produced = secondRow + 1;
					}
					else if (produced + 2 <= srcRect.h)
					{
						SourceRowPair sourceRows{ { sourcePixels(produced), sourcePixels(produced + 1) }, { sourceFloatsA, sourceFloatsB }, sourceFloatsCapacity, spanEnd, pixelStride, premultiplyAlpha };
						float* const tempRows[2] = { ring.row(produced), ring.row(produced + 1) };
						filterHorizontal<XRunLength>(sourceRows, tempRows, stripBegin, stripEnd, xWeights);
						produced += 2;
					}
					else
					{
						SourceRow sourceRow{ { sourcePixels(produced) }, { sourceFloatsA }, sourceFloatsCapacity, spanEnd, pixelStride, premultiplyAlpha };
						float* const tempRows[1] = { ring.row(produced) };
						filterHorizontal<XRunLength>(sourceRow, tempRows, stripBegin, stripEnd, xWeights);
						++produced;
					}
				}

				assert(produced - firstWindowRow <= ring.rowCapacity());
				uint8_t* const stripDest = dest.scanLine<uint8_t>(dy) + stripBegin * pixelStride;
				const DestRowTaps taps = destRowTaps(ring.window(firstWindowRow, rowWeights.size()), rowWeights, tempRowStride, listedTapRows.get(), listedTapWeights.get());
				filterVerticalDestRow<Channels, PixelStride>(taps, pixelTail, pixelStride, stripDest, stripEnd - stripBegin);
			}
		}

		leaveKernel();
	}

	size_t longestPaddedXRun(size_t channels) noexcept
	{
		return longestKernelXRun(channels);
	}

	// The kernel for x runs padded to xRunLength taps. LengthOffsets: 0 to the longest such length less shortestPaddedXRun.
	template <size_t Channels, size_t PixelStride, size_t... LengthOffsets>
	[[nodiscard]] IMAGE_PROCESSING_SIMD_INLINE auto kernelForPaddedXRuns(size_t xRunLength, std::index_sequence<LengthOffsets...>) noexcept
	{
		static constexpr decltype(&resizeRowsWith<Channels, PixelStride, 0>) kernels[]{ &resizeRowsWith<Channels, PixelStride, shortestPaddedXRun + LengthOffsets>... };
		return kernels[xRunLength - shortestPaddedXRun];
	}

	template <size_t Channels, size_t PixelStride>
	IMAGE_PROCESSING_SIMD_TARGET void resizeRows(
		const ImageView<true>& source,
		Rect srcRect,
		ImageView<false>& dest,
		const AxisWeights& xWeights,
		const AxisWeights& yWeights,
		size_t stripWidth,
		uint64_t destRowBegin,
		uint64_t destRowEnd)
	{
		[[maybe_unused]] const size_t xRunLength = xWeights.paddedRunLength;
		if constexpr (longestKernelXRun(Channels) != 0)
		{
			if (xRunLength != 0)
			{
				assert(xRunLength >= shortestPaddedXRun && xRunLength <= longestPaddedXRun(Channels));
				const auto kernel = kernelForPaddedXRuns<Channels, PixelStride>(xRunLength, std::make_index_sequence<longestKernelXRun(Channels) - shortestPaddedXRun + 1>{});
				kernel(source, srcRect, dest, xWeights, yWeights, stripWidth, destRowBegin, destRowEnd);
				return;
			}
		}

		assert(xRunLength == 0);
		resizeRowsWith<Channels, PixelStride, 0>(source, srcRect, dest, xWeights, yWeights, stripWidth, destRowBegin, destRowEnd);
	}

#define IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(Channels, PixelStride) \
	template void resizeRows<Channels, PixelStride>(const ImageView<true>&, Rect, ImageView<false>&, const AxisWeights&, const AxisWeights&, size_t, uint64_t, uint64_t);

	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(1, 1)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(2, 2)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(3, 3)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(3, 4)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(4, 4)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(1, 0)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(2, 0)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(3, 0)
	IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS(4, 0)

#undef IMAGE_PROCESSING_INSTANTIATE_RESIZE_ROWS
}
