#pragma once

#if defined(_M_X64) || defined(__x86_64__)
	#include <immintrin.h>
	#if defined(_MSC_VER)
		#include <intrin.h>
	#endif
	#define IMAGE_PROCESSING_X64 1
#else
	#define IMAGE_PROCESSING_X64 0
#endif

#if IMAGE_PROCESSING_X64 && (defined(__GNUC__) || defined(__clang__))
	#define IMAGE_PROCESSING_AVX2_TARGET __attribute__((target("avx2"), noinline))
#elif IMAGE_PROCESSING_X64 && defined(_MSC_VER)
	#define IMAGE_PROCESSING_AVX2_TARGET __declspec(noinline)
#endif

#if IMAGE_PROCESSING_X64
namespace ImageProcessing::SimdSupport
{
	[[nodiscard]] inline bool cpuSupportsAvx2() noexcept
	{
		static const bool supported = []() noexcept
			{
#if defined(_MSC_VER)
				int registers[4];
				__cpuid(registers, 0);
				if (registers[0] < 7)
					return false;

				__cpuidex(registers, 1, 0);
				constexpr int osXsaveBit = 1 << 27;
				constexpr int avxBit = 1 << 28;
				if ((registers[2] & (osXsaveBit | avxBit)) != (osXsaveBit | avxBit))
					return false;

				if ((_xgetbv(0) & 0x6) != 0x6)
					return false;

				__cpuidex(registers, 7, 0);
				constexpr int avx2Bit = 1 << 5;
				return (registers[1] & avx2Bit) != 0;
#else
				return __builtin_cpu_supports("avx2");
#endif
			}();

		return supported;
	}
}
#endif
