#include "cpu_cache.h"

#include <algorithm>
#include <stdint.h>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX // windows.h's min and max macros break std::min
	#endif
	#include <windows.h>

	#include <bit>
	#include <cstddef>
	#include <memory>
#elif defined(__APPLE__)
	#include <sys/sysctl.h>

	#include <string>
#elif defined(__linux__)
	#include <cstdlib>
	#include <filesystem>
	#include <fstream>
	#include <string>
#endif

namespace
{
	// Folds one cache's share per logical processor into the running minimum
	[[maybe_unused]] void takeSmallerShare(size_t& smallestShare, uint64_t cacheBytes, uint64_t sharingProcessors) noexcept
	{
		if (cacheBytes == 0 || sharingProcessors == 0)
			return;

		const size_t share = static_cast<size_t>(cacheBytes / sharingProcessors);
		smallestShare = smallestShare == 0 ? share : std::min(smallestShare, share);
	}
}

#if defined(_WIN32)

size_t ImageProcessing::Detail::smallestL2BytesPerLogicalProcessor()
{
	DWORD bufferBytes = 0;
	::GetLogicalProcessorInformationEx(RelationCache, nullptr, &bufferBytes);
	if (bufferBytes == 0)
		return 0;

	const auto buffer = std::make_unique_for_overwrite<std::byte[]>(bufferBytes);
	if (!::GetLogicalProcessorInformationEx(RelationCache, reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get()), &bufferBytes))
		return 0;

	size_t smallestShare = 0;
	for (DWORD offset = 0; offset < bufferBytes;)
	{
		const auto& entry = *reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.get() + offset);
		const CACHE_RELATIONSHIP& cache = entry.Cache;
		if (cache.Level == 2 && cache.Type != CacheInstruction)
			takeSmallerShare(smallestShare, cache.CacheSize, static_cast<uint64_t>(std::popcount(cache.GroupMask.Mask)));

		offset += entry.Size;
	}

	return smallestShare;
}

#elif defined(__APPLE__)

namespace
{
	// Zero-initialized: a 32-bit value fills only the low half, which is the whole value on the little-endian Apple CPUs
	uint64_t sysctlValue(const std::string& name) noexcept
	{
		uint64_t value = 0;
		size_t size = sizeof(value);
		return ::sysctlbyname(name.c_str(), &value, &size, nullptr, 0) == 0 ? value : 0;
	}
}

size_t ImageProcessing::Detail::smallestL2BytesPerLogicalProcessor()
{
	size_t smallestShare = 0;
	const uint64_t performanceLevelCount = sysctlValue("hw.nperflevels");
	for (uint64_t level = 0; level < performanceLevelCount; ++level)
	{
		const std::string prefix = "hw.perflevel" + std::to_string(level) + '.';
		uint64_t sharingProcessors = sysctlValue(prefix + "cpusperl2");
		// Absent on some virtual machines: the whole performance level then counts as one L2 cluster
		if (sharingProcessors == 0)
			sharingProcessors = sysctlValue(prefix + "logicalcpu");

		takeSmallerShare(smallestShare, sysctlValue(prefix + "l2cachesize"), sharingProcessors);
	}

	return smallestShare;
}

#elif defined(__linux__)

namespace
{
	template <typename T>
	bool readValue(const std::filesystem::path& path, T& value)
	{
		std::ifstream stream{ path };
		return static_cast<bool>(stream >> value);
	}

	// The CPU count of a sysfs CPU list such as "0-3,8-11"; 0 when malformed
	uint64_t cpuListCount(const std::string& list) noexcept
	{
		uint64_t count = 0;
		const char* cursor = list.c_str();
		while (*cursor != '\0')
		{
			char* end = nullptr;
			const unsigned long first = std::strtoul(cursor, &end, 10);
			if (end == cursor)
				return 0;

			unsigned long last = first;
			if (*end == '-')
			{
				cursor = end + 1;
				last = std::strtoul(cursor, &end, 10);
				if (end == cursor || last < first)
					return 0;
			}

			count += last - first + 1;
			cursor = *end == ',' ? end + 1 : end;
			if (*end != ',' && *end != '\0')
				return 0;
		}

		return count;
	}

	// A sysfs cache size such as "1024K"; 0 when malformed
	uint64_t cacheSizeBytes(const std::string& size) noexcept
	{
		char* suffix = nullptr;
		const uint64_t value = std::strtoull(size.c_str(), &suffix, 10);
		if (suffix == size.c_str())
			return 0;

		switch (*suffix)
		{
		case '\0': return value;
		case 'K': return value * 1024;
		case 'M': return value * 1024 * 1024;
		default: return 0;
		}
	}
}

size_t ImageProcessing::Detail::smallestL2BytesPerLogicalProcessor()
{
	namespace fs = std::filesystem;

	size_t smallestShare = 0;
	std::error_code error;
	for (const fs::directory_entry& cpuDirectory : fs::directory_iterator{ "/sys/devices/system/cpu", error })
	{
		const std::string cpuName = cpuDirectory.path().filename().string();
		if (cpuName.size() <= 3 || !cpuName.starts_with("cpu") || !std::all_of(cpuName.begin() + 3, cpuName.end(), [](char c) { return c >= '0' && c <= '9'; }))
			continue;

		for (const fs::directory_entry& cacheDirectory : fs::directory_iterator{ cpuDirectory.path() / "cache", error })
		{
			unsigned level = 0;
			std::string type, size, sharedCpus;
			if (!readValue(cacheDirectory.path() / "level", level) || level != 2
				|| !readValue(cacheDirectory.path() / "type", type) || type == "Instruction"
				|| !readValue(cacheDirectory.path() / "size", size)
				|| !readValue(cacheDirectory.path() / "shared_cpu_list", sharedCpus))
				continue;

			takeSmallerShare(smallestShare, cacheSizeBytes(size), cpuListCount(sharedCpus));
		}
	}

	return smallestShare;
}

#else

size_t ImageProcessing::Detail::smallestL2BytesPerLogicalProcessor()
{
	return 0;
}

#endif
