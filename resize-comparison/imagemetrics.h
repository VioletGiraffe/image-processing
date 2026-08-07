#pragma once

class QImage;

namespace ImageMetrics
{
	// PSNR over the RGB channels of two same-sized 32-bit images; +infinity for identical images.
	[[nodiscard]] double psnrDb(const QImage& a, const QImage& b);

	// Mean SSIM over Rec.601 luma, 11x11 Gaussian window (sigma 1.5) - the standard configuration,
	// comparable with published numbers. 1.0 means identical.
	[[nodiscard]] double meanSsim(const QImage& a, const QImage& b);

	// Per-channel |a - b| * gain, clamped to 255.
	[[nodiscard]] QImage differenceImage(const QImage& a, const QImage& b, int gain);
}
