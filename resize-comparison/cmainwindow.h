#pragma once

#include "comparisonengine.h"
#include "threading/cworkerthread.h"

DISABLE_COMPILER_WARNINGS
#include <QMainWindow>
RESTORE_COMPILER_WARNINGS

class CPixelViewerWidget;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGridLayout;
class QLabel;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTableWidget;

class QDragEnterEvent;
class QDropEvent;

class CMainWindow final : public QMainWindow
{
	Q_OBJECT

public:
	CMainWindow();

protected:
	void dragEnterEvent(QDragEnterEvent* e) override;
	void dropEvent(QDropEvent* e) override;

private:
	void openImage();
	void loadImageFile(const QString& path);
	void selectPattern(int patternIndex);
	void setSource(QImage image, const QString& name);
	void runComparison();

	void showFlickerResult(size_t index);
	void updateGridPage();
	void rebuildDiffCombos();
	void updateDiffPage();
	void updateMetricsTable();

	[[nodiscard]] std::vector<CPixelViewerWidget*> allViewers() const;
	void connectViewerSync(CPixelViewerWidget* viewer);

private:
	CWorkerThreadPool _threadPool;

	QImage _source; // canonical format
	QString _sourceName;
	ComparisonOutput _comparison;
	size_t _flickerIndex = 0;

	QComboBox* _patternCombo = nullptr;
	QSpinBox* _patternSizeSpin = nullptr;
	QDoubleSpinBox* _scaleSpin = nullptr;
	QCheckBox* _roundTripCheckBox = nullptr;
	QCheckBox* _threadPoolCheckBox = nullptr;
	QSpinBox* _timingRunsSpin = nullptr;

	QTabWidget* _tabs = nullptr;
	QWidget* _flickerPage = nullptr;
	QLabel* _flickerLabel = nullptr;
	CPixelViewerWidget* _flickerViewer = nullptr;
	QGridLayout* _gridLayout = nullptr;
	std::vector<CPixelViewerWidget*> _gridViewers;
	QComboBox* _diffACombo = nullptr;
	QComboBox* _diffBCombo = nullptr;
	QSlider* _diffGainSlider = nullptr;
	QLabel* _diffGainLabel = nullptr;
	CPixelViewerWidget* _diffViewer = nullptr;

	QTableWidget* _metricsTable = nullptr;
	QLabel* _statusLabel = nullptr;
};
