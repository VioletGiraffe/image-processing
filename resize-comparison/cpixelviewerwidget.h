#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QImage>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

// An image viewer that never resamples smoothly: pixels are shown nearest-neighbor at power-of-two zoom,
// so what is on screen is the resize result itself, not the viewer's rescale of it.
// Zoom and pan are broadcast via viewChanged so multiple viewers can be kept in lockstep.
class CPixelViewerWidget final : public QWidget
{
	Q_OBJECT

public:
	explicit CPixelViewerWidget(QWidget* parent = nullptr);

	void setImage(const QImage& image);
	void setOverlayText(const QString& text);

	// Applies a sibling viewer's state without re-emitting viewChanged
	void applyViewState(qreal zoom, QPointF offsetDevicePx);

signals:
	// zoom is in device pixels per image pixel; offset is the image origin in device pixels
	void viewChanged(qreal zoom, QPointF offsetDevicePx);

protected:
	void paintEvent(QPaintEvent*) override;
	void wheelEvent(QWheelEvent* e) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseMoveEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;
	void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
	[[nodiscard]] QSizeF viewportDeviceSize() const noexcept;
	void resetToActualPixels();
	void clampOffset() noexcept;

	QImage _image;
	QString _overlayText;
	qreal _zoom = 1.0; // device px per image px
	QPointF _offset; // image origin in device px
	bool _viewInitialized = false;
	bool _panning = false;
	QPointF _lastPanPos; // device px
};
