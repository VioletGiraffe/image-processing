#pragma once

#include <stddef.h>

namespace ImageProcessing::Detail
{
	// An L2's size divided by the logical processors sharing it, for the L2 where that share is smallest; 0 when unknown
	[[nodiscard]] size_t smallestL2BytesPerLogicalProcessor();
}
