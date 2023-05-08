#include "cimageinterpolationkernel.h"
#define _USE_MATH_DEFINES
#include <math.h>

inline float bicubic(uint32_t i, uint32_t size, float a)
{
	const float x = ((float)i / (float)size - 0.5f) * 2.0f * 2.0f; //[-2;+2]
	const float fabsx = fabs(x);
	if (fabsx <= 1.0f)
		return (a+2)*fabsx*fabsx*fabsx - (a+3)*fabsx*fabsx + 1.0f;
	else if(fabsx > 1 && fabsx < 2)
		return a*fabsx*fabsx*fabsx -5*a*fabsx*fabsx + 8*a*fabsx - 4*a;
	else
		return 0;
}

CBicubicKernel::CBicubicKernel(uint32_t s, float a) noexcept : CImageInterpolationKernel(s)
{
	for (uint32_t i = 0; i < size(); ++i)
		for (uint32_t k = 0; k < size(); ++k)
			_kernel[i][k] = bicubic(i, size(), a) * bicubic(k, size(), a);

	normalizeKernel();
}

inline constexpr float triang(uint32_t i, uint32_t size)
{
	const float x = ((float)i / (float)size - 0.5f) * 2.0f; //[-1;+1]
	return x <= 0.0f ? x + 1.0f : 1.0f - x;
}

inline float bellBicubic(uint32_t i, uint32_t size)
{
	const float f = ((float)i / (float)size - 0.5f) * 2.0f * 1.5f; //[-3/2;+3/2]
	if( f > -1.5f && f < -0.5f )
	{
		return( 0.5f * (float)pow(f + 1.5, 2.0));
	}
	else if( f > -0.5f && f < 0.5f )
	{
		return 3.0f / 4.0f - ( f * f );
	}
	else if( ( f > 0.5f && f < 1.5f ) )
	{
		return( 0.5f * (float)pow(f - 1.5, 2.0));
	}
	else
		return 0.0f;
}

inline float lanczos(uint32_t a, uint32_t i, uint32_t size)
{
	const float x = ((float)i / (float)size - 0.5f) * 2.0f * (float)a; //[-a; a]
	if (fabs(x) < 0.0001f)
		return 1.0f;
	else if(fabs(x) < (float)a && fabs(x) > 0.0f)
		return (float)a * sinf((float)M_PI * x) * sinf((float)M_PI * x / ((float)M_PI * (float)M_PI * (float)a * x * x));
	else
		return 0.0f;
}

CTriangularKernel::CTriangularKernel() noexcept : CImageInterpolationKernel(4)
{
	for (uint32_t i = 0; i < size(); ++i)
		for (uint32_t k = 0; k < size(); ++k)
			_kernel[i][k] = triang(i, size()) * triang(k, size());

	normalizeKernel();
}

CBellBicubicKernel::CBellBicubicKernel(uint32_t s) noexcept : CImageInterpolationKernel(s)
{
	for (uint32_t i = 0; i < size(); ++i)
		for (uint32_t k = 0; k < size(); ++k)
			_kernel[i][k] = bellBicubic(i, size()) * bellBicubic(k, size());

	normalizeKernel();
}

CLanczosKernel::CLanczosKernel(uint32_t s, uint32_t a /*= 2*/) noexcept : CImageInterpolationKernel(s)
{
	for (uint32_t i = 0; i < size(); ++i)
		for (uint32_t k = 0; k < size(); ++k)
			_kernel[i][k] = lanczos(a, i, size()) * lanczos(a, k, size());

	normalizeKernel();
}
