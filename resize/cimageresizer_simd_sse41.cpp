#include "simd_support.h"

#if IMAGE_PROCESSING_X64
	#include "simd_primitives_sse41.h"

	#define IMAGE_PROCESSING_SIMD_LEVEL Sse41
	#include "cimageresizer_simd.inl"
#endif
