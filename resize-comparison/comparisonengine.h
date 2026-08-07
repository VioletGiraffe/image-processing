#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
RESTORE_COMPILER_WARNINGS

#include <vector>

class CWorkerThreadPool;

struct ImplementationResult
{
	QString name;
	QImage image;
	double medianMs = 0.0;
	// Vs ground truth; only filled for a round-trip comparison
	double psnrDb = 0.0;
	double ssim = 0.0;
};

struct ComparisonInput
{
	QImage source; // must be in a canonical format (see qimageviewutils.h)
	// Normal mode: the output size. Round-trip mode: the intermediate downscaled size; the output size is then source.size().
	QSize targetSize;
	// Round-trip: downscale the source once (shared by all implementations), have each implementation scale it back up,
	// and measure the results against the original - the one resize comparison with an objective reference.
	bool roundTrip = false;
	int timingRuns = 5;
	CWorkerThreadPool* threadPool = nullptr; // null = single-threaded
};

struct ComparisonOutput
{
	std::vector<ImplementationResult> results;
	QImage effectiveSource; // what the implementations actually resized: the source, or its downscaled copy in round-trip mode
	QImage groundTruth; // null unless round-trip

	[[nodiscard]] bool hasMetrics() const { return !groundTruth.isNull(); }
};

[[nodiscard]] ComparisonOutput runComparison(const ComparisonInput& input);
