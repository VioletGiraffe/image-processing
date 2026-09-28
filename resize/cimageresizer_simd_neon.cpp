#include "simd_support.h"

#if IMAGE_PROCESSING_ARM64
	#include "simd_primitives_neon.h"

	#define IMAGE_PROCESSING_SIMD_LEVEL Neon
	#include "cimageresizer_simd.inl"
#endif
