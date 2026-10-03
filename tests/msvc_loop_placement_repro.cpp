// The same loop at different code offsets: eight instantiations of one function template that differ only in
// 0-112 nop bytes executed once per call, ahead of the loops. MSVC aligns the loops to 16 bytes, so the padding
// moves them through the four possible offsets within a 64-byte line.
//
// Standalone: not part of the test project. Background: doc/resize-performance.md, the section on jumps and 32-byte boundaries.
//
// Build:  cl /O2 /arch:AVX2 /std:c++20 /EHsc msvc_loop_placement_repro.cpp
// Run:    msvc_loop_placement_repro.exe [rounds, default 10]
//
// With clang-cl, /DALIGN_LOOP_64 applies [[clang::code_align(64)]] to the measured loop.
#include <immintrin.h>
#include <intrin.h>
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(__clang__) && defined(ALIGN_LOOP_64)
	#define MEASURED_LOOP_ALIGNMENT [[clang::code_align(64)]]
#else
	#define MEASURED_LOOP_ALIGNMENT
#endif

#define NOP16 __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop(); __nop();

// weights[firstWeight + i] applies to source[firstSource + i], for i < weightCount
struct TapRun
{
	size_t firstSource;
	size_t firstWeight;
	size_t weightCount;
};

__forceinline float sumWithLastTaps(__m128 partialSums, const float* source, const float* weights, size_t tapCount) noexcept
{
	const __m128 pairSums = _mm_add_ps(partialSums, _mm_movehl_ps(partialSums, partialSums));
	float sum = _mm_cvtss_f32(_mm_add_ss(pairSums, _mm_shuffle_ps(pairSums, pairSums, _MM_SHUFFLE(1, 1, 1, 1))));
	for (size_t tap = 0; tap < tapCount; ++tap)
		sum += source[tap] * weights[tap];

	return sum;
}

// One weighted sum per run, for two source rows at once: 32 taps at a time, then 4, then one by one.
// The data in main() never enters the 32-tap block.
template <int Pad>
__declspec(noinline) void filterRowPair(const TapRun* runs, const float* weights, const float* sourceA, const float* sourceB, float* destA, float* destB, size_t count, size_t repeats) noexcept
{
	if constexpr (Pad >= 1) { NOP16 }
	if constexpr (Pad >= 2) { NOP16 }
	if constexpr (Pad >= 3) { NOP16 }
	if constexpr (Pad >= 4) { NOP16 }
	if constexpr (Pad >= 5) { NOP16 }
	if constexpr (Pad >= 6) { NOP16 }
	if constexpr (Pad >= 7) { NOP16 }

	for (size_t repeat = 0; repeat < repeats; ++repeat)
	{
		MEASURED_LOOP_ALIGNMENT
		for (size_t i = 0; i < count; ++i)
		{
			const TapRun& run = runs[i];
			const float* runWeights = weights + run.firstWeight;
			const size_t tapCount = run.weightCount;
			const float* a = sourceA + run.firstSource;
			const float* b = sourceB + run.firstSource;

			__m256 pairsA = _mm256_setzero_ps();
			__m256 pairsB = _mm256_setzero_ps();
			size_t tap = 0;
			if (tapCount >= 32)
			{
				__m256 a0 = _mm256_setzero_ps(), a1 = a0, a2 = a0, a3 = a0, b0 = a0, b1 = a0, b2 = a0, b3 = a0;
				for (; tap + 32 <= tapCount; tap += 32)
				{
					const __m256 w0 = _mm256_loadu_ps(runWeights + tap), w1 = _mm256_loadu_ps(runWeights + tap + 8);
					const __m256 w2 = _mm256_loadu_ps(runWeights + tap + 16), w3 = _mm256_loadu_ps(runWeights + tap + 24);
					a0 = _mm256_fmadd_ps(_mm256_loadu_ps(a + tap), w0, a0);
					b0 = _mm256_fmadd_ps(_mm256_loadu_ps(b + tap), w0, b0);
					a1 = _mm256_fmadd_ps(_mm256_loadu_ps(a + tap + 8), w1, a1);
					b1 = _mm256_fmadd_ps(_mm256_loadu_ps(b + tap + 8), w1, b1);
					a2 = _mm256_fmadd_ps(_mm256_loadu_ps(a + tap + 16), w2, a2);
					b2 = _mm256_fmadd_ps(_mm256_loadu_ps(b + tap + 16), w2, b2);
					a3 = _mm256_fmadd_ps(_mm256_loadu_ps(a + tap + 24), w3, a3);
					b3 = _mm256_fmadd_ps(_mm256_loadu_ps(b + tap + 24), w3, b3);
				}

				pairsA = _mm256_add_ps(_mm256_add_ps(a0, a1), _mm256_add_ps(a2, a3));
				pairsB = _mm256_add_ps(_mm256_add_ps(b0, b1), _mm256_add_ps(b2, b3));
			}

			__m128 sumsA = _mm_add_ps(_mm256_castps256_ps128(pairsA), _mm256_extractf128_ps(pairsA, 1));
			__m128 sumsB = _mm_add_ps(_mm256_castps256_ps128(pairsB), _mm256_extractf128_ps(pairsB, 1));
			if (tap + 4 <= tapCount)
			{
				const __m128 w = _mm_loadu_ps(runWeights + tap);
				sumsA = _mm_fmadd_ps(_mm_loadu_ps(a + tap), w, sumsA);
				sumsB = _mm_fmadd_ps(_mm_loadu_ps(b + tap), w, sumsB);
				tap += 4;
			}

			destA[i] = sumWithLastTaps(sumsA, a + tap, runWeights + tap, tapCount - tap);
			destB[i] = sumWithLastTaps(sumsB, b + tap, runWeights + tap, tapCount - tap);
		}
	}

	_mm256_zeroupper();
}

using Filter = void (*)(const TapRun*, const float*, const float*, const float*, float*, float*, size_t, size_t) noexcept;

int main(int argc, char** argv)
{
	// Logical processor 2: a performance core on Intel hybrid CPUs
	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	SetThreadAffinityMask(GetCurrentThread(), 1 << 2);

	char cpuName[49] = {};
	for (int leaf = 0; leaf < 3; ++leaf)
	{
		int registers[4];
		__cpuid(registers, static_cast<int>(0x80000002u) + leaf);
		std::memcpy(cpuName + leaf * 16, registers, 16);
	}

#if defined(__clang__)
	std::printf("%s, clang %s\n", cpuName, __clang_version__);
#else
	std::printf("%s, MSVC %d\n", cpuName, _MSC_FULL_VER);
#endif

	// Runs of 4, 1 and 4 taps repeat, advancing one source element per three outputs
	constexpr size_t sourceWidth = 1280, destWidth = 3840, repeats = 360;
	std::vector<float> weights;
	std::vector<TapRun> runs;
	for (size_t i = 0; i < destWidth; ++i)
	{
		const size_t taps = i % 3 == 1 ? 1 : 4;
		runs.push_back({ std::min(i / 3, sourceWidth - 4), weights.size(), taps });
		for (size_t t = 0; t < taps; ++t)
			weights.push_back(taps == 1 ? 1.0f : 0.25f);
	}
	weights.resize(weights.size() + 32);

	std::vector<float> sourceA(sourceWidth + 32, 1.0f), sourceB(sourceWidth + 32, 2.0f), destA(destWidth), destB(destWidth);

	const Filter filters[] = { filterRowPair<0>, filterRowPair<1>, filterRowPair<2>, filterRowPair<3>, filterRowPair<4>, filterRowPair<5>, filterRowPair<6>, filterRowPair<7> };
	constexpr size_t variants = sizeof(filters) / sizeof(filters[0]);
	const int rounds = argc > 1 ? std::atoi(argv[1]) : 10;
	double best[variants];
	std::fill(best, best + variants, 1e30);

	// Round 0 is a warm-up. The copies take turns within a round, and each keeps its fastest round.
	for (int round = 0; round <= rounds; ++round)
	{
		for (size_t v = 0; v < variants; ++v)
		{
			const auto start = std::chrono::steady_clock::now();
			for (int pass = 0; pass < 12; ++pass)
				filters[v](runs.data(), weights.data(), sourceA.data(), sourceB.data(), destA.data(), destB.data(), destWidth, repeats);

			const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count();
			if (round > 0)
				best[v] = std::min(best[v], ns / (12.0 * repeats * destWidth));
		}
	}

	const double fastest = *std::min_element(best, best + variants);
	for (size_t v = 0; v < variants; ++v)
		std::printf("%3zu nop bytes, function at offset %2zu of its 64-byte line: %.3f ns per output pair, %+5.1f%%\n", v * 16, (size_t)filters[v] % 64, best[v], (best[v] / fastest - 1.0) * 100.0);

	return destA[5] + destB[7] > 0.0f ? 0 : 1;
}
