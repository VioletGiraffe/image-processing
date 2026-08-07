#pragma once

class QImage;

// Synthetic stress patterns, all square Format_RGB32 images. Photos flatter every resampling filter;
// these are built to expose aliasing, ringing and phase errors.
namespace TestPatterns
{
	// Radial chirp reaching Nyquist at the inscribed circle's edge (and exceeding it towards the corners):
	// the canonical downscale aliasing test - a bad decimator paints Moire rings all over it.
	[[nodiscard]] QImage zonePlate(int size);

	// Spokes converging to arbitrarily high frequency at the center; supersampled so the source itself is clean.
	[[nodiscard]] QImage siemensStar(int size);

	// Four quadrants with cell sizes 1, 2, 4 and 8 px: exposes filter phase and pixel-grid alignment.
	[[nodiscard]] QImage checkerboard(int size);

	// Antialiased text at several sizes: the everyday case where over-sharpening and blur are most visible.
	[[nodiscard]] QImage textSample(int size);

	// Sharp bars, a line fan and discs over a smooth ramp: step edges against a gradient make ringing stand out.
	[[nodiscard]] QImage edgesAndLines(int size);
}
