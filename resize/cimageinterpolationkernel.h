#pragma once
#include "assert/advanced_assert.h"

#include <vector>

template <typename CoeffType>
class CImageInterpolationKernelBase
{
public:
	virtual ~CImageInterpolationKernelBase() noexcept = default;

	virtual CoeffType coeff(int x, int y) const noexcept = 0;
	virtual int size() const noexcept = 0;
};

template <typename CoeffType>
class CImageInterpolationKernel : public CImageInterpolationKernelBase<CoeffType>
{
public:
	explicit CImageInterpolationKernel(int s): _size(s) {
		_kernel.resize(s);
		for(int i = 0; i < _size; ++i)
		{
			_kernel[i].resize(s, CoeffType{0});
		}
	}

	CoeffType coeff(int x, int y) const noexcept override
	{
		assert_debug_only(x < _size && y < _size); // Affects performance a whole lot! Don't change to assert_r
		return _kernel[x][y];
	}

	int size() const noexcept override { return _size; }

protected:
	void normalizeKernel() noexcept {
		CoeffType sum {0};
		for (int i = 0; i < size(); ++i)
			for (int k = 0; k < size(); ++k)
				sum += _kernel[i][k];

		for (int i = 0; i < size(); ++i)
			for (int k = 0; k < size(); ++k)
				_kernel[i][k] /= sum;
	}

	int _size;
	std::vector<std::vector<CoeffType> > _kernel;
};

class CBicubicKernel final : public CImageInterpolationKernel<float>
{
public:
	CBicubicKernel(int s, float a);
};

class CTriangularKernel final : public CImageInterpolationKernel<float>
{
public:
	CTriangularKernel();
};

class CBellBicubicKernel final : public CImageInterpolationKernel<float>
{
public:
	explicit CBellBicubicKernel(int s);
};

class CLanczosKernel final : public CImageInterpolationKernel<float>
{
public:
	CLanczosKernel(int s, int a = 2);
};
