#include "testpatterns.h"

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cmath>
#include <numbers>

namespace
{
	[[nodiscard]] inline QRgb gray(double value01) noexcept
	{
		const int v = static_cast<int>(std::clamp(value01, 0.0, 1.0) * 255.0 + 0.5);
		return qRgb(v, v, v);
	}
}

QImage TestPatterns::zonePlate(int size)
{
	QImage img{ size, size, QImage::Format_RGB32 };
	const double center = (size - 1) / 2.0;
	const double radius = size / 2.0;

	for (int y = 0; y < size; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		const double dy = y - center;
		for (int x = 0; x < size; ++x)
		{
			const double dx = x - center;
			// Instantaneous frequency pi * r / radius: exactly Nyquist at r == radius
			const double phase = std::numbers::pi * (dx * dx + dy * dy) / (2.0 * radius);
			line[x] = gray(0.5 + 0.5 * std::cos(phase));
		}
	}

	return img;
}

QImage TestPatterns::siemensStar(int size)
{
	QImage img{ size, size, QImage::Format_RGB32 };
	const double center = (size - 1) / 2.0;
	constexpr int spokes = 72;
	constexpr int subSamples = 3; // 3x3 supersampling; the pattern's frequency is unbounded at the center

	for (int y = 0; y < size; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int x = 0; x < size; ++x)
		{
			double sum = 0.0;
			for (int sy = 0; sy < subSamples; ++sy)
			{
				const double dy = y + (sy + 0.5) / subSamples - 0.5 - center;
				for (int sx = 0; sx < subSamples; ++sx)
				{
					const double dx = x + (sx + 0.5) / subSamples - 0.5 - center;
					if (dx * dx + dy * dy < 1.0)
						sum += 0.5;
					else
						sum += 0.5 + 0.5 * std::cos(spokes * std::atan2(dy, dx));
				}
			}
			line[x] = gray(sum / (subSamples * subSamples));
		}
	}

	return img;
}

QImage TestPatterns::checkerboard(int size)
{
	QImage img{ size, size, QImage::Format_RGB32 };
	const int half = size / 2;

	for (int y = 0; y < size; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int x = 0; x < size; ++x)
		{
			const int cell = 1 << ((y >= half ? 2 : 0) + (x >= half ? 1 : 0)); // 1, 2 / 4, 8
			line[x] = (((x / cell) + (y / cell)) & 1) ? qRgb(0, 0, 0) : qRgb(255, 255, 255);
		}
	}

	return img;
}

QImage TestPatterns::textSample(int size)
{
	QImage img{ size, size, QImage::Format_RGB32 };
	img.fill(Qt::white);

	QPainter p{ &img };
	p.setRenderHint(QPainter::TextAntialiasing);
	p.setPen(Qt::black);

	QFont font = QGuiApplication::font();
	const int margin = size / 40;
	int y = margin;

	static constexpr const char sampleLine[] = "The quick brown fox jumps over the lazy dog 0123456789 !@#$%";
	for (const int pixelSize : { 8, 10, 13, 17, 22, 28, 36 })
	{
		font.setPixelSize(pixelSize);
		p.setFont(font);
		y += pixelSize + pixelSize / 3;
		p.drawText(margin, y, QLatin1String(sampleLine));
	}

	// Fill the rest with a small-print paragraph: dense strokes are where resampling of text breaks down first
	font.setPixelSize(11);
	p.setFont(font);
	y += 22;
	for (; y < size - margin; y += 15)
		p.drawText(margin, y, QLatin1String(sampleLine));

	return img;
}

QImage TestPatterns::edgesAndLines(int size)
{
	QImage img{ size, size, QImage::Format_RGB32 };

	for (int y = 0; y < size; ++y)
	{
		QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
		for (int x = 0; x < size; ++x)
			line[x] = gray(static_cast<double>(x) / (size - 1));
	}

	QPainter p{ &img };

	// Aliased bars of growing width: black over the light half, white over the dark half
	int x = size / 16;
	for (int barWidth = 1; barWidth <= 4; ++barWidth)
	{
		p.fillRect(x, size / 16, barWidth, size * 3 / 8, Qt::black);
		p.fillRect(x + size / 2, size / 16, barWidth, size * 3 / 8, Qt::white);
		x += barWidth + size / 64 + 4;
	}

	p.setRenderHint(QPainter::Antialiasing);

	// Near-horizontal 1px line fan: the staircase test
	p.setPen(QPen{ Qt::black, 1.0 });
	const double fanLength = size * 0.42;
	for (int k = 0; k < 8; ++k)
	{
		const double angle = k * 1.5 * std::numbers::pi / 180.0;
		const QPointF from{ size * 0.04, size * 0.55 + k * size * 0.05 };
		p.drawLine(from, from + QPointF{ fanLength * std::cos(angle), fanLength * std::sin(angle) });
	}

	// Sharp-edged disc and a thin ring on the gradient: a ringing detector
	p.setPen(Qt::NoPen);
	p.setBrush(Qt::black);
	p.drawEllipse(QPointF{ size * 0.72, size * 0.72 }, size * 0.13, size * 0.13);
	p.setBrush(Qt::NoBrush);
	p.setPen(QPen{ Qt::white, size / 100.0 });
	p.drawEllipse(QPointF{ size * 0.72, size * 0.72 }, size * 0.18, size * 0.18);

	return img;
}
