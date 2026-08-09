#include "comparisonengine.h"
#include "imagemetrics.h"
#include "qimageviewutils.h"

DISABLE_COMPILER_WARNINGS
#include <QElapsedTimer>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <functional>
#include <utility>

namespace
{
	using ImageProcessing::ResizeKernel;

	[[nodiscard]] QImage resizeWithOurs(const QImage& src, QSize target, CThreadPool* threadPool, ResizeKernel kernel)
	{
		QImage dst{ target, src.format() };
		const auto srcView = constView(src);
		auto dstView = mutableView(dst);
		ImageProcessing::resize(dstView, srcView, {}, threadPool, kernel);
		return dst;
	}
}

ComparisonOutput runComparison(const ComparisonInput& input)
{
	ComparisonOutput output;
	QSize outputSize;
	if (input.roundTrip)
	{
		output.groundTruth = input.source;
		output.effectiveSource = resizeWithOurs(input.source, input.targetSize, input.threadPool, ResizeKernel::Lanczos3);
		outputSize = input.source.size();
	}
	else
	{
		output.effectiveSource = input.source;
		outputSize = input.targetSize;
	}

	const QImage& src = output.effectiveSource;
	CThreadPool* const pool = input.threadPool;

	struct Implementation
	{
		QString name;
		std::function<QImage()> run;
	};

	const Implementation implementations[] = {
		{ QStringLiteral("Ours: auto"), [&] { return resizeWithOurs(src, outputSize, pool, ResizeKernel::Auto); } },
		{ QStringLiteral("Ours: Catmull-Rom"), [&] { return resizeWithOurs(src, outputSize, pool, ResizeKernel::CatmullRom); } },
		{ QStringLiteral("Ours: Lanczos3"), [&] { return resizeWithOurs(src, outputSize, pool, ResizeKernel::Lanczos3); } },
		{ QStringLiteral("Qt smooth"), [&] { return src.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); } },
		{ QStringLiteral("Qt fast"), [&] { return src.scaled(outputSize, Qt::IgnoreAspectRatio, Qt::FastTransformation); } },
	};

	std::vector<double> runTimesMs;
	for (const auto& implementation : implementations)
	{
		ImplementationResult result;
		result.name = implementation.name;
		result.image = implementation.run(); // Warm-up run, and the displayed result

		// Every run produces a fresh image, so Qt's internal allocation and ours are timed alike
		runTimesMs.clear();
		for (int i = 0; i < input.timingRuns; ++i)
		{
			QElapsedTimer timer;
			timer.start();
			[[maybe_unused]] const QImage timedResult = implementation.run();
			runTimesMs.push_back(static_cast<double>(timer.nsecsElapsed()) * 1e-6);
		}
		std::sort(runTimesMs.begin(), runTimesMs.end());
		result.medianMs = runTimesMs[runTimesMs.size() / 2];

		if (output.hasMetrics())
		{
			result.psnrDb = ImageMetrics::psnrDb(result.image, output.groundTruth);
			result.ssim = ImageMetrics::meanSsim(result.image, output.groundTruth);
		}

		output.results.push_back(std::move(result));
	}

	return output;
}
