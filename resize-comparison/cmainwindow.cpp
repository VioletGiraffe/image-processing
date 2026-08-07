#include "cmainwindow.h"
#include "cpixelviewerwidget.h"
#include "imagemetrics.h"
#include "qimageviewutils.h"
#include "testpatterns.h"

DISABLE_COMPILER_WARNINGS
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QGuiApplication>
#include <QMimeData>
#include <QHeaderView>
#include <QImageReader>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTabWidget>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cmath>
#include <thread>
#include <utility>

namespace
{
	struct Pattern
	{
		const char* name;
		QImage (*create)(int size);
	};

	constexpr Pattern kPatterns[] = {
		{ "Zone plate", &TestPatterns::zonePlate },
		{ "Siemens star", &TestPatterns::siemensStar },
		{ "Checkerboard", &TestPatterns::checkerboard },
		{ "Text", &TestPatterns::textSample },
		{ "Edges and lines", &TestPatterns::edgesAndLines },
	};

	[[nodiscard]] QString sizeText(QSize size)
	{
		return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
	}

	[[nodiscard]] bool hasSupportedImageExtension(const QString& path)
	{
		return QImageReader::supportedImageFormats().contains(QFileInfo{ path }.suffix().toLower().toLatin1());
	}

	// The first dropped local file with a supported image extension, if any
	[[nodiscard]] QString droppedImagePath(const QMimeData* mimeData)
	{
		for (const QUrl& url : mimeData->urls())
		{
			if (url.isLocalFile() && hasSupportedImageExtension(url.toLocalFile()))
				return url.toLocalFile();
		}

		return {};
	}
}

CMainWindow::CMainWindow() :
	_threadPool{ std::max(std::thread::hardware_concurrency(), 2u) - 1, "Resize comparison pool" }
{
	setWindowTitle(tr("Resize comparison"));
	setAcceptDrops(true);
	_threadPool.waitUntilStarted(); // So the first run's timings don't include thread startup

	auto* central = new QWidget;
	auto* mainLayout = new QVBoxLayout{ central };

	auto* controls = new QHBoxLayout;

	auto* openButton = new QPushButton{ tr("Open image...") };
	connect(openButton, &QPushButton::clicked, this, &CMainWindow::openImage);
	controls->addWidget(openButton);

	_patternCombo = new QComboBox;
	for (const Pattern& pattern : kPatterns)
		_patternCombo->addItem(tr(pattern.name));
	_patternCombo->setPlaceholderText(tr("Test pattern"));
	_patternCombo->setCurrentIndex(-1);
	connect(_patternCombo, &QComboBox::activated, this, &CMainWindow::selectPattern);
	controls->addWidget(_patternCombo);

	_patternSizeSpin = new QSpinBox;
	_patternSizeSpin->setRange(128, 8192);
	_patternSizeSpin->setValue(1024);
	_patternSizeSpin->setSingleStep(128);
	_patternSizeSpin->setSuffix(tr(" px"));
	connect(_patternSizeSpin, &QSpinBox::valueChanged, this, [this] {
		if (_patternCombo->currentIndex() >= 0)
			selectPattern(_patternCombo->currentIndex());
	});
	controls->addWidget(_patternSizeSpin);

	controls->addSpacing(16);
	controls->addWidget(new QLabel{ tr("Scale:") });
	_scaleSpin = new QDoubleSpinBox;
	_scaleSpin->setRange(0.05, 8.0);
	_scaleSpin->setSingleStep(0.05);
	_scaleSpin->setDecimals(2);
	_scaleSpin->setValue(0.5);
	_scaleSpin->setSuffix(QStringLiteral("x"));
	controls->addWidget(_scaleSpin);

	_roundTripCheckBox = new QCheckBox{ tr("Round-trip vs ground truth") };
	_roundTripCheckBox->setToolTip(tr("Downscale the source by the scale factor first, have every implementation upscale it back,\n"
									  "and measure the results against the original. Only meaningful when upscaling (scale > 1)."));
	_roundTripCheckBox->setEnabled(false);
	connect(_scaleSpin, &QDoubleSpinBox::valueChanged, this, [this](double value) { _roundTripCheckBox->setEnabled(value > 1.0); });
	controls->addWidget(_roundTripCheckBox);

	controls->addSpacing(16);
	_threadPoolCheckBox = new QCheckBox{ tr("Thread pool") };
	_threadPoolCheckBox->setChecked(true);
	controls->addWidget(_threadPoolCheckBox);

	controls->addWidget(new QLabel{ tr("Timing runs:") });
	_timingRunsSpin = new QSpinBox;
	_timingRunsSpin->setRange(1, 99);
	_timingRunsSpin->setValue(5);
	controls->addWidget(_timingRunsSpin);

	controls->addStretch();
	auto* runButton = new QPushButton{ tr("Run") };
	connect(runButton, &QPushButton::clicked, this, &CMainWindow::runComparison);
	controls->addWidget(runButton);

	mainLayout->addLayout(controls);

	_tabs = new QTabWidget;

	_flickerPage = new QWidget;
	auto* flickerLayout = new QVBoxLayout{ _flickerPage };
	auto* flickerLabelRow = new QHBoxLayout;
	_flickerLabel = new QLabel;
	QFont boldFont = _flickerLabel->font();
	boldFont.setBold(true);
	_flickerLabel->setFont(boldFont);
	flickerLabelRow->addWidget(_flickerLabel);
	flickerLabelRow->addStretch();
	flickerLabelRow->addWidget(new QLabel{ tr("Digit keys select, Space cycles | wheel zooms, drag pans, double-click resets to 100%") });
	flickerLayout->addLayout(flickerLabelRow);
	_flickerViewer = new CPixelViewerWidget;
	connectViewerSync(_flickerViewer);
	flickerLayout->addWidget(_flickerViewer, 1);
	_tabs->addTab(_flickerPage, tr("Flicker"));

	auto* gridPage = new QWidget;
	_gridLayout = new QGridLayout{ gridPage };
	_tabs->addTab(gridPage, tr("Side by side"));

	auto* diffPage = new QWidget;
	auto* diffLayout = new QVBoxLayout{ diffPage };
	auto* diffControls = new QHBoxLayout;
	diffControls->addWidget(new QLabel{ tr("Difference of:") });
	_diffACombo = new QComboBox;
	diffControls->addWidget(_diffACombo);
	diffControls->addWidget(new QLabel{ tr("vs") });
	_diffBCombo = new QComboBox;
	diffControls->addWidget(_diffBCombo);
	diffControls->addSpacing(16);
	diffControls->addWidget(new QLabel{ tr("Gain:") });
	_diffGainSlider = new QSlider{ Qt::Horizontal };
	_diffGainSlider->setRange(1, 32);
	_diffGainSlider->setValue(8);
	_diffGainSlider->setMaximumWidth(200);
	diffControls->addWidget(_diffGainSlider);
	_diffGainLabel = new QLabel;
	diffControls->addWidget(_diffGainLabel);
	diffControls->addStretch();
	connect(_diffACombo, &QComboBox::activated, this, &CMainWindow::updateDiffPage);
	connect(_diffBCombo, &QComboBox::activated, this, &CMainWindow::updateDiffPage);
	connect(_diffGainSlider, &QSlider::valueChanged, this, &CMainWindow::updateDiffPage);
	diffLayout->addLayout(diffControls);
	_diffViewer = new CPixelViewerWidget;
	connectViewerSync(_diffViewer);
	diffLayout->addWidget(_diffViewer, 1);
	_tabs->addTab(diffPage, tr("Difference"));

	mainLayout->addWidget(_tabs, 1);

	_metricsTable = new QTableWidget;
	_metricsTable->setColumnCount(4);
	_metricsTable->setHorizontalHeaderLabels({ tr("Implementation"), tr("Median, ms"), tr("PSNR, dB"), tr("SSIM") });
	_metricsTable->verticalHeader()->hide();
	_metricsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
	_metricsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
	_metricsTable->setMaximumHeight(190);
	mainLayout->addWidget(_metricsTable);

	setCentralWidget(central);

	_statusLabel = new QLabel;
	statusBar()->addWidget(_statusLabel);

	// Plain-key shortcuts: text-input widgets override these while focused, so typing into the spinboxes stays safe
	for (int i = 0; i < 9; ++i)
	{
		auto* shortcut = new QShortcut{ QKeySequence{ Qt::Key_1 + i }, this };
		connect(shortcut, &QShortcut::activated, this, [this, i] {
			if (_tabs->currentWidget() == _flickerPage)
				showFlickerResult(static_cast<size_t>(i));
		});
	}
	auto* cycleShortcut = new QShortcut{ QKeySequence{ Qt::Key_Space }, this };
	connect(cycleShortcut, &QShortcut::activated, this, [this] {
		if (_tabs->currentWidget() == _flickerPage && !_comparison.results.empty())
			showFlickerResult((_flickerIndex + 1) % _comparison.results.size());
	});

	resize(1280, 900);

	_patternCombo->setCurrentIndex(0);
	selectPattern(0);
}

void CMainWindow::dragEnterEvent(QDragEnterEvent* e)
{
	if (!droppedImagePath(e->mimeData()).isEmpty())
		e->acceptProposedAction();
}

void CMainWindow::dropEvent(QDropEvent* e)
{
	const QString path = droppedImagePath(e->mimeData());
	if (path.isEmpty())
		return;

	e->acceptProposedAction();
	loadImageFile(path);
}

void CMainWindow::openImage()
{
	const QString path = QFileDialog::getOpenFileName(this, tr("Open image"), QString{},
		tr("Images (*.png *.jpg *.jpeg *.bmp *.webp *.gif *.tif *.tiff);;All files (*)"));
	if (!path.isEmpty())
		loadImageFile(path);
}

void CMainWindow::loadImageFile(const QString& path)
{
	QImageReader reader{ path };
	reader.setAutoTransform(true);
	QImage image = reader.read();
	if (image.isNull())
	{
		QMessageBox::warning(this, tr("Resize comparison"), tr("Failed to load %1:\n%2").arg(path, reader.errorString()));
		return;
	}

	image.convertTo(canonicalFormat(image));
	_patternCombo->setCurrentIndex(-1);
	setSource(std::move(image), QFileInfo{ path }.fileName());
}

void CMainWindow::selectPattern(int patternIndex)
{
	const Pattern& pattern = kPatterns[patternIndex];
	setSource(pattern.create(_patternSizeSpin->value()), tr(pattern.name));
}

void CMainWindow::setSource(QImage image, const QString& name)
{
	_source = std::move(image);
	_sourceName = name;
	runComparison();
}

void CMainWindow::runComparison()
{
	if (_source.isNull())
		return;

	const double factor = _scaleSpin->value();
	const bool roundTrip = _roundTripCheckBox->isChecked() && factor > 1.0;
	// In round-trip mode the target is the intermediate downscaled size; the implementations then upscale back
	const double sizeFactor = roundTrip ? 1.0 / factor : factor;
	const QSize target{
		std::max(1, static_cast<int>(std::lround(_source.width() * sizeFactor))),
		std::max(1, static_cast<int>(std::lround(_source.height() * sizeFactor)))
	};

	const ComparisonInput input{
		_source,
		target,
		roundTrip,
		_timingRunsSpin->value(),
		_threadPoolCheckBox->isChecked() ? &_threadPool : nullptr
	};

	QGuiApplication::setOverrideCursor(Qt::WaitCursor);
	_comparison = ::runComparison(input);
	QGuiApplication::restoreOverrideCursor();

	showFlickerResult(std::min(_flickerIndex, _comparison.results.size() - 1));
	updateGridPage();
	rebuildDiffCombos();
	updateDiffPage();
	updateMetricsTable();

	QString status = QStringLiteral("%1 %2 -> %3").arg(_sourceName, sizeText(_source.size()), sizeText(_comparison.results.front().image.size()));
	if (roundTrip)
		status += tr(" (round-trip via %1)").arg(sizeText(_comparison.effectiveSource.size()));
	if (!_threadPoolCheckBox->isChecked())
		status += tr(", single-threaded");
	_statusLabel->setText(status);
}

void CMainWindow::showFlickerResult(size_t index)
{
	if (index >= _comparison.results.size())
		return;

	_flickerIndex = index;
	const ImplementationResult& result = _comparison.results[index];
	_flickerViewer->setImage(result.image);
	_flickerLabel->setText(QStringLiteral("[%1] %2 (%3 ms)").arg(index + 1).arg(result.name).arg(result.medianMs, 0, 'f', 2));
}

void CMainWindow::updateGridPage()
{
	const size_t count = _comparison.results.size();
	if (_gridViewers.size() != count)
	{
		for (CPixelViewerWidget* viewer : _gridViewers)
			delete viewer;
		_gridViewers.clear();

		const int columns = count <= 4 ? 2 : 3;
		for (size_t i = 0; i < count; ++i)
		{
			auto* viewer = new CPixelViewerWidget;
			connectViewerSync(viewer);
			_gridLayout->addWidget(viewer, static_cast<int>(i) / columns, static_cast<int>(i) % columns);
			_gridViewers.push_back(viewer);
		}
		for (int i = 0; i < _gridLayout->rowCount(); ++i)
			_gridLayout->setRowStretch(i, 1);
		for (int i = 0; i < _gridLayout->columnCount(); ++i)
			_gridLayout->setColumnStretch(i, 1);
	}

	for (size_t i = 0; i < count; ++i)
	{
		const ImplementationResult& result = _comparison.results[i];
		QString overlay = QStringLiteral("%1  |  %2 ms").arg(result.name).arg(result.medianMs, 0, 'f', 2);
		if (_comparison.hasMetrics())
			overlay += QStringLiteral("  |  %1 dB").arg(result.psnrDb, 0, 'f', 2);
		_gridViewers[i]->setImage(result.image);
		_gridViewers[i]->setOverlayText(overlay);
	}
}

void CMainWindow::rebuildDiffCombos()
{
	const QString previousA = _diffACombo->currentText(), previousB = _diffBCombo->currentText();

	for (QComboBox* combo : { _diffACombo, _diffBCombo })
	{
		combo->clear();
		for (const ImplementationResult& result : _comparison.results)
			combo->addItem(result.name);
		if (_comparison.hasMetrics())
			combo->addItem(tr("Ground truth"));
	}

	const int indexA = _diffACombo->findText(previousA);
	_diffACombo->setCurrentIndex(indexA >= 0 ? indexA : 0);
	const int indexB = _diffBCombo->findText(previousB);
	if (indexB >= 0)
		_diffBCombo->setCurrentIndex(indexB);
	else // Default to the most interesting pair: ours vs the ground truth when present, vs Qt otherwise
		_diffBCombo->setCurrentIndex(_diffBCombo->count() - (_comparison.hasMetrics() ? 1 : 2));
}

void CMainWindow::updateDiffPage()
{
	const auto imageAt = [this](int index) -> const QImage& {
		return index < static_cast<int>(_comparison.results.size()) ? _comparison.results[static_cast<size_t>(index)].image : _comparison.groundTruth;
	};

	const int indexA = _diffACombo->currentIndex(), indexB = _diffBCombo->currentIndex();
	if (indexA < 0 || indexB < 0)
		return;

	const int gain = _diffGainSlider->value();
	_diffGainLabel->setText(QStringLiteral("x%1").arg(gain));
	_diffViewer->setImage(ImageMetrics::differenceImage(imageAt(indexA), imageAt(indexB), gain));
	_diffViewer->setOverlayText(QStringLiteral("|%1 - %2| x%3").arg(_diffACombo->currentText(), _diffBCombo->currentText()).arg(gain));
}

void CMainWindow::updateMetricsTable()
{
	const auto& results = _comparison.results;
	_metricsTable->setRowCount(static_cast<int>(results.size()));

	for (size_t i = 0; i < results.size(); ++i)
	{
		const ImplementationResult& result = results[i];
		const int row = static_cast<int>(i);
		_metricsTable->setItem(row, 0, new QTableWidgetItem{ result.name });
		_metricsTable->setItem(row, 1, new QTableWidgetItem{ QStringLiteral("%1").arg(result.medianMs, 0, 'f', 2) });
		if (_comparison.hasMetrics())
		{
			_metricsTable->setItem(row, 2, new QTableWidgetItem{ std::isinf(result.psnrDb) ? QStringLiteral("inf") : QStringLiteral("%1").arg(result.psnrDb, 0, 'f', 2) });
			_metricsTable->setItem(row, 3, new QTableWidgetItem{ QStringLiteral("%1").arg(result.ssim, 0, 'f', 4) });
		}
		else
		{
			_metricsTable->setItem(row, 2, new QTableWidgetItem{ QStringLiteral("-") });
			_metricsTable->setItem(row, 3, new QTableWidgetItem{ QStringLiteral("-") });
		}
	}

	_metricsTable->resizeColumnsToContents();
}

std::vector<CPixelViewerWidget*> CMainWindow::allViewers() const
{
	std::vector<CPixelViewerWidget*> viewers{ _flickerViewer, _diffViewer };
	viewers.insert(viewers.end(), _gridViewers.begin(), _gridViewers.end());
	return viewers;
}

void CMainWindow::connectViewerSync(CPixelViewerWidget* viewer)
{
	connect(viewer, &CPixelViewerWidget::viewChanged, this, [this, viewer](qreal zoom, QPointF offset) {
		for (CPixelViewerWidget* other : allViewers())
		{
			if (other != viewer)
				other->applyViewState(zoom, offset);
		}
	});
}
