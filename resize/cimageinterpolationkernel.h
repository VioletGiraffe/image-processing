#pragma once
#include "assert/advanced_assert.h"

#include <stdint.h>
#include <vector>

template <typename CoeffType>
class CImageInterpolationKernelBase
{
public:
	using FLoatingPointType = CoeffType;

	virtual ~CImageInterpolationKernelBase() noexcept = default;

	[[nodiscard]] virtual CoeffType coeff(uint32_t x, uint32_t y) const noexcept = 0;
	[[nodiscard]] virtual uint32_t size() const noexcept = 0;
};

template <typename CoeffType>
class CImageInterpolationKernel : public CImageInterpolationKernelBase<CoeffType>
{
public:
	explicit CImageInterpolationKernel(const uint32_t s) noexcept :
		_size(s),
		_kernel(s, std::vector<CoeffType>(s, CoeffType{ 0 }))
	{
	}

	[[nodiscard]] CoeffType coeff(uint32_t x, uint32_t y) const noexcept override
	{
		assert_debug_only(x < _size && y < _size); // Affects performance a whole lot! Don't change to assert_r
		return _kernel[x][y];
	}

	[[nodiscard]] uint32_t size() const noexcept override { return _size; }

protected:
	void normalizeKernel() noexcept {
		CoeffType sum {0};
		for (uint32_t i = 0; i < size(); ++i)
			for (uint32_t k = 0; k < size(); ++k)
				sum += _kernel[i][k];

		for (uint32_t i = 0; i < size(); ++i)
			for (uint32_t k = 0; k < size(); ++k)
				_kernel[i][k] /= sum;
	}

	uint32_t _size;
	std::vector<std::vector<CoeffType> > _kernel;
};

class CBicubicKernel final : public CImageInterpolationKernel<float>
{
public:
	CBicubicKernel(uint32_t s, float a) noexcept;
};

class CTriangularKernel final : public CImageInterpolationKernel<float>
{
public:
	CTriangularKernel() noexcept;
};

class CBellBicubicKernel final : public CImageInterpolationKernel<float>
{
public:
	explicit CBellBicubicKernel(uint32_t s) noexcept;
};

class CLanczosKernel final : public CImageInterpolationKernel<float>
{
public:
	CLanczosKernel(uint32_t s, uint32_t a = 2) noexcept;
};
