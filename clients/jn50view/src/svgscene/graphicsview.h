#pragma once

#include <QGraphicsView>

namespace svgscene {

class GraphicsView : public QGraphicsView
{
	Q_OBJECT

	using Super = QGraphicsView;
public:
	GraphicsView(QWidget *parent = nullptr);

	void zoomToFit();
protected:
	void zoom(double delta, const QPoint &mouse_pos);

	void paintEvent(QPaintEvent *event) override;

	void wheelEvent(QWheelEvent *ev) Q_DECL_OVERRIDE;
	void mousePressEvent(QMouseEvent *ev) Q_DECL_OVERRIDE;
	void mouseReleaseEvent(QMouseEvent *ev) Q_DECL_OVERRIDE;
	void mouseMoveEvent(QMouseEvent *ev) Q_DECL_OVERRIDE;
private:
	QPoint m_dragMouseStartPos;
};

}

