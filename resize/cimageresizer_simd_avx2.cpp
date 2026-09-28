#include "simd_support.h"

#if IMAGE_PROCESSING_X64
	#include "simd_primitives_avx2.h"

	#define IMAGE_PROCESSING_SIMD_LEVEL Avx2
	#include "cimageresizer_simd.inl"
#endif
