#include "imagemetrics.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <array>
#include <assert.h>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <stdint.h>
#include <vector>

namespace
{
	constexpr int kWindowRadius = 5; // 11-tap Gaussian

	[[nodiscard]] std::array<float, 2 * kWindowRadius + 1> gaussianTaps()
	{
		std::array<float, 2 * kWindowRadius + 1> taps;
		constexpr double sigma = 1.5;
		double sum = 0.0;
		for (int i = 0; i < static_cast<int>(taps.size()); ++i)
		{
			const double d = i - kWindowRadius;
			const double w = std::exp(-d * d / (2.0 * sigma * sigma));
			taps[static_cast<size_t>(i)] = static_cast<float>(w);
			sum += w;
		}
		for (float& tap : taps)
			tap = static_cast<float>(tap / sum);
		return taps;
	}

	// Separable Gaussian with clamped edges, in place; scratch is caller-provided so five blurs share one buffer.
	void blurPlane(std::vector<float>& plane, std::vector<float>& scratch, int w, int h, const std::array<float, 2 * kWindowRadius + 1>& taps)
	{
		scratch.resize(plane.size());

		for (int y = 0; y < h; ++y)
		{
			const float* srcRow = plane.data() + static_cast<size_t>(y) * w;
			float* dstRow = scratch.data() + static_cast<size_t>(y) * w;
			for (int x = 0; x < w; ++x)
			{
				float accum = 0.0f;
				for (int t = -kWindowRadius; t <= kWindowRadius; ++t)
					accum += srcRow[std::clamp(x + t, 0, w - 1)] * taps[static_cast<size_t>(t + kWindowRadius)];
				dstRow[x] = accum;
			}
		}

		for (int y = 0; y < h; ++y)
		{
			float* dstRow = plane.data() + static_cast<size_t>(y) * w;
			for (int t = -kWindowRadius; t <= kWindowRadius; ++t)
			{
				const float tap = taps[static_cast<size_t>(t + kWindowRadius)];
				const float* srcRow = scratch.data() + static_cast<size_t>(std::clamp(y + t, 0, h - 1)) * w;
				if (t == -kWindowRadius)
				{
					for (int x = 0; x < w; ++x)
						dstRow[x] = srcRow[x] * tap;
				}
				else
				{
					for (int x = 0; x < w; ++x)
						dstRow[x] += srcRow[x] * tap;
				}
			}
		}
	}

	[[nodiscard]] std::vector<float> lumaPlane(const QImage& image)
	{
		const int w = image.width(), h = image.height();
		std::vector<float> luma(static_cast<size_t>(w) * h);
		for (int y = 0; y < h; ++y)
		{
			const QRgb* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
			float* out = luma.data() + static_cast<size_t>(y) * w;
			for (int x = 0; x < w; ++x)
				out[x] = 0.299f * qRed(line[x]) + 0.587f * qGreen(line[x]) + 0.114f * qBlue(line[x]);
		}
		return luma;
	}
}

double ImageMetrics::psnrDb(const QImage& a, const QImage& b)
{
	assert(a.size() == b.size());
	const int w = a.width(), h = a.height();

	uint64_t squaredErrorSum = 0;
	for (int y = 0; y < h; ++y)
	{
		const QRgb* lineA = reinterpret_cast<const QRgb*>(a.constScanLine(y));
		const QRgb* lineB = reinterpret_cast<const QRgb*>(b.constScanLine(y));
		for (int x = 0; x < w; ++x)
		{
			const int dr = qRed(lineA[x]) - qRed(lineB[x]);
			const int dg = qGreen(lineA[x]) - qGreen(lineB[x]);
			const int db = qBlue(lineA[x]) - qBlue(lineB[x]);
			squaredErrorSum += static_cast<uint64_t>(dr * dr + dg * dg + db * db);
		}
	}

	if (squaredErrorSum == 0)
		return std::numeric_limits<double>::infinity();

	const double mse = static_cast<double>(squaredErrorSum) / (static_cast<double>(w) * h * 3.0);
	return 10.0 * std::log10(255.0 * 255.0 / mse);
}

double ImageMetrics::meanSsim(const QImage& a, const QImage& b)
{
	assert(a.size() == b.size());
	const int w = a.width(), h = a.height();
	const size_t pixelCount = static_cast<size_t>(w) * h;

	const auto taps = gaussianTaps();
	std::vector<float> scratch;

	std::vector<float> planeA = lumaPlane(a);
	std::vector<float> planeB = lumaPlane(b);

	std::vector<float> muA = planeA, muB = planeB;
	blurPlane(muA, scratch, w, h, taps);
	blurPlane(muB, scratch, w, h, taps);

	std::vector<float> aa(pixelCount), bb(pixelCount), ab(pixelCount);
	for (size_t i = 0; i < pixelCount; ++i)
	{
		aa[i] = planeA[i] * planeA[i];
		bb[i] = planeB[i] * planeB[i];
		ab[i] = planeA[i] * planeB[i];
	}
	blurPlane(aa, scratch, w, h, taps);
	blurPlane(bb, scratch, w, h, taps);
	blurPlane(ab, scratch, w, h, taps);

	constexpr double c1 = (0.01 * 255.0) * (0.01 * 255.0);
	constexpr double c2 = (0.03 * 255.0) * (0.03 * 255.0);

	double ssimSum = 0.0;
	for (size_t i = 0; i < pixelCount; ++i)
	{
		const double ma = muA[i], mb = muB[i];
		const double varA = aa[i] - ma * ma;
		const double varB = bb[i] - mb * mb;
		const double covariance = ab[i] - ma * mb;
		ssimSum += ((2.0 * ma * mb + c1) * (2.0 * covariance + c2)) / ((ma * ma + mb * mb + c1) * (varA + varB + c2));
	}

	return ssimSum / static_cast<double>(pixelCount);
}

QImage ImageMetrics::differenceImage(const QImage& a, const QImage& b, int gain)
{
	assert(a.size() == b.size());
	const int w = a.width(), h = a.height();

	QImage diff{ w, h, QImage::Format_RGB32 };
	for (int y = 0; y < h; ++y)
	{
		const QRgb* lineA = reinterpret_cast<const QRgb*>(a.constScanLine(y));
		const QRgb* lineB = reinterpret_cast<const QRgb*>(b.constScanLine(y));
		QRgb* out = reinterpret_cast<QRgb*>(diff.scanLine(y));
		for (int x = 0; x < w; ++x)
		{
			out[x] = qRgb(
				std::min(std::abs(qRed(lineA[x]) - qRed(lineB[x])) * gain, 255),
				std::min(std::abs(qGreen(lineA[x]) - qGreen(lineB[x])) * gain, 255),
				std::min(std::abs(qBlue(lineA[x]) - qBlue(lineB[x])) * gain, 255));
		}
	}

	return diff;
}
