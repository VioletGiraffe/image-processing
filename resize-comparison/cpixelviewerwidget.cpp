#include "cpixelviewerwidget.h"

DISABLE_COMPILER_WARNINGS
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cmath>

static constexpr qreal kMinZoom = 0.125;
static constexpr qreal kMaxZoom = 32.0;

namespace
{
	// Center the image axis when it fits the viewport, otherwise keep its edges pinned inside
	[[nodiscard]] inline qreal clampAxis(qreal offset, qreal imageLength, qreal viewportLength) noexcept
	{
		if (imageLength <= viewportLength)
			return (viewportLength - imageLength) / 2.0;

		return std::clamp(offset, viewportLength - imageLength, 0.0);
	}
}

CPixelViewerWidget::CPixelViewerWidget(QWidget* parent) :
	QWidget{ parent }
{
	setMinimumSize(120, 120);
}

void CPixelViewerWidget::setImage(const QImage& image)
{
	// Same-size images (the normal case: alternative results of one comparison) keep the current view
	if (image.size() != _image.size())
		_viewInitialized = false;

	_image = image;
	update();
}

void CPixelViewerWidget::setOverlayText(const QString& text)
{
	_overlayText = text;
	update();
}

void CPixelViewerWidget::applyViewState(qreal zoom, QPointF offsetDevicePx)
{
	_zoom = zoom;
	_offset = offsetDevicePx;
	_viewInitialized = true;
	update();
}

QSizeF CPixelViewerWidget::viewportDeviceSize() const noexcept
{
	return QSizeF{ size() } * devicePixelRatioF();
}

void CPixelViewerWidget::resetToActualPixels()
{
	_zoom = 1.0;
	const QSizeF viewport = viewportDeviceSize();
	_offset = QPointF{ (viewport.width() - _image.width()) / 2.0, (viewport.height() - _image.height()) / 2.0 };
	_viewInitialized = true;
}

void CPixelViewerWidget::clampOffset() noexcept
{
	const QSizeF viewport = viewportDeviceSize();
	_offset.setX(clampAxis(_offset.x(), _image.width() * _zoom, viewport.width()));
	_offset.setY(clampAxis(_offset.y(), _image.height() * _zoom, viewport.height()));
}

void CPixelViewerWidget::paintEvent(QPaintEvent*)
{
	QPainter p{ this };
	p.fillRect(rect(), palette().color(QPalette::Shadow));

	if (!_image.isNull())
	{
		if (!_viewInitialized)
			resetToActualPixels();

		// A synced offset from a differently-sized sibling, or a resize, can leave _offset out of range for
		// this viewport; geometry is only authoritative here (hidden tab pages are not resized), so clamp here.
		clampOffset();

		const qreal dpr = devicePixelRatioF();
		const QSizeF viewport = viewportDeviceSize();

		// Back-project the viewport into the image, then blit only the visible part
		const int x0 = static_cast<int>(std::floor(std::clamp(-_offset.x() / _zoom, 0.0, static_cast<qreal>(_image.width()))));
		const int y0 = static_cast<int>(std::floor(std::clamp(-_offset.y() / _zoom, 0.0, static_cast<qreal>(_image.height()))));
		const int x1 = static_cast<int>(std::ceil(std::clamp((viewport.width() - _offset.x()) / _zoom, 0.0, static_cast<qreal>(_image.width()))));
		const int y1 = static_cast<int>(std::ceil(std::clamp((viewport.height() - _offset.y()) / _zoom, 0.0, static_cast<qreal>(_image.height()))));

		if (x1 > x0 && y1 > y0)
		{
			const QRect visible{ x0, y0, x1 - x0, y1 - y0 };
			const QSize scaledSize{ std::max(1, qRound(visible.width() * _zoom)), std::max(1, qRound(visible.height() * _zoom)) };
			// FastTransformation == nearest neighbor; at the power-of-two zooms this is an exact pixel replication
			QImage buffer = _image.copy(visible).scaled(scaledSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
			buffer.setDevicePixelRatio(dpr);

			// Snap the blit to a whole device pixel, or the painter would resample the buffer
			const QPoint targetDevice = (_offset + QPointF{ visible.topLeft() } * _zoom).toPoint();
			p.drawImage(QPointF{ targetDevice } / dpr, buffer);
		}
	}

	QString overlay = _overlayText;
	if (!_image.isNull())
	{
		if (!overlay.isEmpty())
			overlay += QStringLiteral("  |  ");
		overlay += QStringLiteral("%1%").arg(_zoom * 100.0);
	}

	if (!overlay.isEmpty())
	{
		const QFontMetrics fm = p.fontMetrics();
		const QRect textRect = fm.boundingRect(overlay).adjusted(-4, -2, 4, 2).translated(8, 8 + fm.ascent());
		p.fillRect(textRect, QColor{ 0, 0, 0, 160 });
		p.setPen(Qt::white);
		p.drawText(textRect, Qt::AlignCenter, overlay);
	}
}

void CPixelViewerWidget::wheelEvent(QWheelEvent* e)
{
	const int delta = e->angleDelta().y();
	if (_image.isNull() || !_viewInitialized || delta == 0)
	{
		QWidget::wheelEvent(e);
		return;
	}

	const qreal newZoom = std::clamp(delta > 0 ? _zoom * 2.0 : _zoom / 2.0, kMinZoom, kMaxZoom);
	if (newZoom != _zoom)
	{
		// Keep the image point under the cursor stationary
		const QPointF cursorDevice = e->position() * devicePixelRatioF();
		const QPointF imagePos = (cursorDevice - _offset) / _zoom;
		_zoom = newZoom;
		_offset = cursorDevice - imagePos * _zoom;
		clampOffset();
		update();
		emit viewChanged(_zoom, _offset);
	}

	e->accept();
}

void CPixelViewerWidget::mousePressEvent(QMouseEvent* e)
{
	if (e->button() == Qt::LeftButton && !_image.isNull())
	{
		_panning = true;
		_lastPanPos = e->position() * devicePixelRatioF();
		setCursor(Qt::ClosedHandCursor);
		e->accept();
	}
}

void CPixelViewerWidget::mouseMoveEvent(QMouseEvent* e)
{
	if (!_panning)
		return;

	const QPointF pos = e->position() * devicePixelRatioF();
	_offset += pos - _lastPanPos;
	_lastPanPos = pos;
	clampOffset();
	update();
	emit viewChanged(_zoom, _offset);
	e->accept();
}

void CPixelViewerWidget::mouseReleaseEvent(QMouseEvent* e)
{
	if (e->button() == Qt::LeftButton && _panning)
	{
		_panning = false;
		unsetCursor();
		e->accept();
	}
}

void CPixelViewerWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
	if (_image.isNull())
		return;

	resetToActualPixels();
	update();
	emit viewChanged(_zoom, _offset);
	e->accept();
}
