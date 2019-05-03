#include "graphwidget.h"
#include "graphmodel.h"
#include "graphwidget.h"

#include <shv/core/exception.h>
#include <shv/coreqt/log.h>

#include <QPainter>
#include <QFontMetrics>
#include <QLabel>
#include <QMouseEvent>

#include <cmath>

namespace timeline {

//==========================================
// Graph::Channel
//==========================================
void Graph::Channel::setYRange(const timeline::Graph::YRange &r)
{
	m_state.yRange = r;
	resetZoom();
}

void Graph::Channel::enlargeYRange(double step)
{
	YRange r = m_state.yRange;
	r.min -= step;
	r.max += step;
	setYRange(r);
}

void Graph::Channel::resetZoom()
{
	m_state.yRangeZoom = m_state.yRange;
}

//==========================================
// Graph
//==========================================
void Graph::GraphStyle::init(QWidget *widget)
{
	QFont f = widget->font();
	setFont(f);
	QFontMetrics fm(f, widget);
	setUnitSize(fm.lineSpacing());
}

Graph::Graph()
{
}

void Graph::setModel(GraphModel *model)
{
	m_model = model;
}

GraphModel *Graph::model() const
{
	return m_model;
}

void Graph::createChannelsFromModel()
{
	m_channels.clear();
	if(m_model.isNull())
		return;
	for (int i = 0; i < m_model->channelCount(); ++i) {
		m_channels.append(Channel());
	}
}

void Graph::clearChannels()
{
	m_channels.clear();
}

void Graph::appendChannel()
{
	m_channels.append(Channel());
}

Graph::Channel &Graph::channelAt(int ix)
{
	if(ix < 0 || ix >= m_channels.count())
		SHV_EXCEPTION("Index out of range.");
	return m_channels[ix];
}

const Graph::Channel &Graph::channelAt(int ix) const
{
	if(ix < 0 || ix >= m_channels.count())
		SHV_EXCEPTION("Index out of range.");
	return m_channels[ix];
}

Graph::DataRect Graph::dataRect(int channel_ix) const
{
	const Channel &ch = channelAt(channel_ix);
	return DataRect{xRangeZoom(), ch.yRangeZoom()};
}

Graph::timemsec_t Graph::miniMapPosToTime(int pos) const
{
	auto pos2time = posToTimeFn(QPoint{m_layout.miniMapRect.left(), m_layout.miniMapRect.right()}, xRange());
	return pos2time? pos2time(pos): 0;
}

int Graph::miniMapTimeToPos(Graph::timemsec_t time) const
{
	auto time2pos = timeToPosFn(xRange(), QPoint{m_layout.miniMapRect.left(), m_layout.miniMapRect.right()});
	return time2pos? time2pos(time): 0;
}

Graph::timemsec_t Graph::posToTime(int pos) const
{
	auto pos2time = posToTimeFn(QPoint{m_layout.xAxisRect.left(), m_layout.xAxisRect.right()}, xRangeZoom());
	return pos2time? pos2time(pos): 0;
}

int Graph::timeToPos(Graph::timemsec_t time) const
{
	auto time2pos = timeToPosFn(xRangeZoom(), QPoint{m_layout.xAxisRect.left(), m_layout.xAxisRect.right()});
	return time2pos? time2pos(time): 0;
}

Sample Graph::timeToSample(int channel_ix, Graph::timemsec_t time) const
{
	GraphModel *m = model();
	int ix1 = m->lessOrEqualIndex(channel_ix, time);
	if(ix1 < 0)
		return Sample();
	const Channel &ch = channelAt(channel_ix);
	int interpolation = ch.effectiveStyle.interpolation();
	if(interpolation == ChannelStyle::Interpolation::None) {
		Sample s = m->sampleAt(channel_ix, ix1);
		if(s.time == time)
			return s;
	}
	else if(interpolation == ChannelStyle::Interpolation::Stepped) {
		Sample s = m->sampleAt(channel_ix, ix1);
		s.time = time;
		return s;
	}
	else if(interpolation == ChannelStyle::Interpolation::Line) {
		int ix2 = ix1 + 1;
		if(ix2 >= m->count(channel_ix))
			return Sample();
		Sample s1 = m->sampleAt(channel_ix, ix1);
		Sample s2 = m->sampleAt(channel_ix, ix2);
		if(s1.time == s2.time)
			return Sample();
		double d = s1.value.toDouble() + (time - s1.time) * (s2.value.toDouble() - s1.value.toDouble()) / (s2.time - s1.time);
		return Sample(time, d);
	}
	return Sample();

	//auto sample2point = sampleToPointFn(DataRect{xRangeZoom(), ch.yRangeZoom()}, ch.graphRect());
	//if(!sample2point)
	//	return;


}

void Graph::setCrossBarPos(const QPoint &pos)
{
	m_state.crossBarPos = pos;
}

int Graph::crossBarChannel(const QPoint &pos) const
{
	for (int i = 0; i < channelCount(); ++i) {
		const Channel &ch = channelAt(i);
		if(ch.graphRect().contains(pos)) {
			return i;
		}
	}
	return -1;
}

void Graph::setXRange(const timeline::Graph::XRange &r, bool keep_zoom)
{
	m_state.xRange = r;
	if(m_state.xRangeZoom.isNull() || !keep_zoom) {
		m_state.xRangeZoom = m_state.xRange;
	}
	sanityXRangeZoom();
	makeXAxis();
}

void Graph::setXRangeZoom(const Graph::XRange &r)
{
	if(r.min < m_state.xRange.min)
		return;
	if(r.max > m_state.xRange.max)
		return;
	m_state.xRangeZoom = r;
	makeXAxis();
}

void Graph::sanityXRangeZoom()
{
	if(m_state.xRangeZoom.min < m_state.xRange.min)
		m_state.xRangeZoom.min = m_state.xRange.min;
	if(m_state.xRangeZoom.max > m_state.xRange.max)
		m_state.xRangeZoom.max = m_state.xRange.max;
}

void Graph::setStyle(const Graph::GraphStyle &st)
{
	m_style = st;
	effectiveStyle = m_style;
}

void Graph::setDefaultChannelStyle(const Graph::ChannelStyle &st)
{
	m_channelStyle = st;
}

QVariantMap Graph::mergeMaps(const QVariantMap &base, const QVariantMap &overlay) const
{
	QVariantMap ret = base;
	QMapIterator<QString, QVariant> it(overlay);
	//shvDebug() << "====================== merge ====================";
	while(it.hasNext()) {
		it.next();
		//shvDebug() << it.key() << "<--" << it.value();
		ret[it.key()] = it.value();
	}
	return ret;
}

void Graph::makeXAxis()
{
	shvLogFuncFrame();
	XRange range = m_state.xRangeZoom;
	//int x0 = m_layout.xAxisRect.left();
	int label_tick_units = 10;
	//timemsec_t label_tick_interval = posToTime(m_layout.xAxisRect.left() + u2px(label_tick_units)) - range.min;
	//int label_every = 5;
	double interval = range.interval() / label_tick_units;
	shvDebug() << "interval:" << interval;
	int pow = 1;
	while(interval >= 7) {
		interval /= 10;
		pow *= 10;
	}
	// snap to closest 1, 2, 5
	if(interval < 1.5)
		m_layout.axis.labelTickEvery = 1;
	else if(interval < 3)
		m_layout.axis.labelTickEvery = 2;
	else if(interval < 7)
		m_layout.axis.labelTickEvery = 5;
	timemsec_t lbl_tick_interval = m_layout.axis.labelTickEvery * pow;
	m_layout.axis.tickInterval = lbl_tick_interval / m_layout.axis.labelTickEvery;
	shvDebug() << "m_layout.axis.labelTickEvery:" << m_layout.axis.labelTickEvery << "pow:" << pow;
	shvDebug() << "lbl_tick_interval:" << lbl_tick_interval;
	shvDebug() << "m_layout.axis.tickInterval:" << m_layout.axis.tickInterval ;
	auto n = range.min / lbl_tick_interval;
	auto r = range.min % lbl_tick_interval;
	if(r > 0)
		n++;
	m_layout.axis.labelTick0 = n * lbl_tick_interval;
	for(m_layout.axis.tick0 = m_layout.axis.labelTick0; m_layout.axis.tick0 > range.min + m_layout.axis.tickInterval; m_layout.axis.tick0 -= m_layout.axis.tickInterval);
}

int Graph::u2px(double u) const
{
	int sz = effectiveStyle.unitSize();
	return static_cast<int>(sz * u);
}

void Graph::makeLayout(const QRect &rect)
{
	QSize viewport_size;
	viewport_size.setWidth(rect.width());
	int grid_w = viewport_size.width();
	int x_axis_pos = 0;
	x_axis_pos += u2px(effectiveStyle.leftMargin());
	x_axis_pos += u2px(effectiveStyle.verticalHeaderWidth());
	x_axis_pos += u2px(effectiveStyle.yAxisWidth());
	grid_w -= x_axis_pos;
	grid_w -= u2px(effectiveStyle.rightMargin());
	m_layout.miniMapRect.setHeight(u2px(effectiveStyle.miniMapHeight()));
	m_layout.miniMapRect.setLeft(x_axis_pos);
	m_layout.miniMapRect.setWidth(grid_w);

	m_layout.xAxisRect = m_layout.miniMapRect;
	m_layout.xAxisRect.setHeight(u2px(effectiveStyle.xAxisHeight()));

	int sum_h_min = 0;
	struct Rest { int index; int rest; };
	QVector<Rest> rests;
	for (int i = 0; i < m_channels.count(); ++i) {
		Channel &ch = m_channels[i];
		ch.effectiveStyle = mergeMaps(m_channelStyle, ch.style());
		int ch_h = u2px(ch.effectiveStyle.heightMin());
		rests << Rest{i, u2px(ch.effectiveStyle.heightRange())};
		ch.m_layout.graphRect.setLeft(x_axis_pos);
		ch.m_layout.graphRect.setWidth(grid_w);
		ch.m_layout.graphRect.setHeight(ch_h);
		sum_h_min += ch_h;
		if(i > 0)
			sum_h_min += u2px(effectiveStyle.channelSpacing());
	}
	sum_h_min += u2px(effectiveStyle.xAxisHeight());
	sum_h_min += u2px(effectiveStyle.miniMapHeight());
	int h_rest = rect.height();
	h_rest -= sum_h_min;
	h_rest -= u2px(effectiveStyle.topMargin());
	h_rest -= u2px(effectiveStyle.bottomMargin());
	if(h_rest > 0) {
		// distribute free widget height space to channel's rects heights
		std::sort(rests.begin(), rests.end(), [](const Rest &a, const Rest &b) {
			return a.rest < b.rest;
		});
		for (int i = 0; i < rests.count(); ++i) {
			int fair_rest = h_rest / (rests.count() - i);
			const Rest &r = rests[i];
			Channel &ch = m_channels[r.index];
			int h = u2px(ch.effectiveStyle.heightRange());
			if(h > fair_rest)
				h = fair_rest;
			ch.m_layout.graphRect.setHeight(ch.m_layout.graphRect.height() + h);
			h_rest -= h;
		}
	}
	// shift channel rects
	int widget_height = 0;
	widget_height += u2px(effectiveStyle.topMargin());
	for (int i = m_channels.count() - 1; i >= 0; --i) {
		Channel &ch = m_channels[i];

		ch.m_layout.graphRect.moveTop(widget_height);
		//ch.layout.graphRect.setWidth(layout.navigationBarRect.width());

		ch.m_layout.verticalHeaderRect = ch.m_layout.graphRect;
		ch.m_layout.verticalHeaderRect.setX(u2px(effectiveStyle.leftMargin()));
		ch.m_layout.verticalHeaderRect.setWidth(u2px(effectiveStyle.verticalHeaderWidth()));

		ch.m_layout.yAxisRect = ch.m_layout.verticalHeaderRect;
		ch.m_layout.yAxisRect.moveLeft(ch.m_layout.verticalHeaderRect.right());
		ch.m_layout.yAxisRect.setWidth(u2px(effectiveStyle.yAxisWidth()));

		widget_height += ch.m_layout.graphRect.height();
		if(i > 0)
			widget_height += u2px(effectiveStyle.channelSpacing());
	}
	m_layout.xAxisRect.moveTop(widget_height);
	widget_height += u2px(effectiveStyle.xAxisHeight());
	m_layout.miniMapRect.moveTop(widget_height);
	widget_height += u2px(effectiveStyle.miniMapHeight());
	widget_height += u2px(effectiveStyle.bottomMargin());

	viewport_size.setHeight(widget_height);
	shvDebug() << "\tw:" << viewport_size.width() << "h:" << viewport_size.height();
	m_layout.rect = rect;
	m_layout.rect.setSize(viewport_size);
}

void Graph::drawRectText(QPainter *painter, const QRect &rect, const QString &text, const QFont &font, const QColor &color, const QColor &background)
{
	painter->save();
	if(background.isValid())
		painter->fillRect(rect, background);
	QPen pen;
	pen.setColor(color);
	painter->setPen(pen);
	painter->drawRect(rect);
	painter->setFont(font);
	QFontMetrics fm(font);
	painter->drawText(rect.left() + fm.width('i'), rect.top() + fm.leading() + fm.ascent(), text);
	painter->restore();
}

void Graph::draw(QPainter *painter, const QRect &dirty_rect)
{
	shvLogFuncFrame();
	drawBackground(painter);
	for (int i = 0; i < m_channels.count(); ++i) {
		const Channel &ch = m_channels[i];
		if(dirty_rect.intersects(ch.graphRect())) {
			drawBackground(painter, i);
			drawGrid(painter, i);
			drawSamples(painter, i);
		}
		if(dirty_rect.intersects(ch.verticalHeaderRect()))
			drawVerticalHeader(painter, i);
		if(dirty_rect.intersects(ch.yAxisRect()))
			drawYAxis(painter, i);
	}
	if(dirty_rect.intersects(m_layout.miniMapRect))
		drawMiniMap(painter);
	if(dirty_rect.intersects(m_layout.xAxisRect))
		drawXAxis(painter);
}

void Graph::drawBackground(QPainter *painter)
{
	painter->fillRect(m_layout.rect, effectiveStyle.colorBackground());
	//painter->fillRect(QRect{QPoint(), widget()->geometry().size()}, Qt::blue);
}

void Graph::drawMiniMap(QPainter *painter)
{
	painter->fillRect(m_layout.miniMapRect, m_channelStyle.colorBackground());
	for (int i = 0; i < m_channels.count(); ++i) {
		const Channel &ch = m_channels[i];
		ChannelStyle ch_st = ch.effectiveStyle;
		ch_st.setLineAreaStyle(ChannelStyle::LineAreaStyle::Filled);
		DataRect drect{xRange(), ch.yRange()};
		drawSamples(painter, i, drect, m_layout.miniMapRect, ch_st);
	}

	int x1 = miniMapTimeToPos(xRangeZoom().min);
	int x2 = miniMapTimeToPos(xRangeZoom().max);
	painter->save();
	QPen pen;
	pen.setWidth(u2px(0.2));
	QPoint p1{x1, m_layout.miniMapRect.top()};
	QPoint p2{x1, m_layout.miniMapRect.bottom()};
	QPoint p3{x2, m_layout.miniMapRect.bottom()};
	QPoint p4{x2, m_layout.miniMapRect.top()};
	QColor bc(Qt::black);
	bc.setAlphaF(0.6);
	painter->fillRect(QRect{m_layout.miniMapRect.topLeft(), p2}, bc);
	painter->fillRect(QRect{p4, m_layout.miniMapRect.bottomRight()}, bc);
	pen.setColor(Qt::gray);
	painter->setPen(pen);
	painter->drawLine(m_layout.miniMapRect.topLeft(), p1);
	pen.setColor(Qt::white);
	painter->setPen(pen);
	painter->drawLine(p1, p2);
	painter->drawLine(p2, p3);
	painter->drawLine(p3, p4);
	pen.setColor(Qt::gray);
	painter->setPen(pen);
	painter->drawLine(p4, m_layout.miniMapRect.topRight());
	painter->restore();
}

void Graph::drawXAxis(QPainter *painter)
{
	if(m_layout.axis.tickInterval == 0) {
		drawRectText(painter, m_layout.xAxisRect, "x-axis", effectiveStyle.font(), Qt::green);
		return;
	}
	painter->save();
	QFont font = effectiveStyle.font();
	QPen pen;
	pen.setWidth(u2px(0.1));
	pen.setColor(effectiveStyle.colorAxis());
	int tick_len = m_layout.xAxisRect.height() / 6;
	painter->setPen(pen);
	painter->drawLine(m_layout.xAxisRect.topLeft(), m_layout.xAxisRect.topRight());

	int label_after = -1;
	for (timemsec_t t = m_layout.axis.tick0; ; t += m_layout.axis.tickInterval) {
		int x = timeToPos(t);
		if(x > m_layout.xAxisRect.right())
			break;
		if(t == m_layout.axis.labelTick0)
			label_after = 0;
		QPoint p1{x, m_layout.xAxisRect.top()};
		QPoint p2{p1.x(), p1.y() + tick_len};
		if(label_after == 0) {
			p2.setY(p2.y() + 2*tick_len);
			painter->drawLine(p1, p2);
			painter->drawText(p2 + QPoint{0, m_layout.xAxisRect.height() / 2}, QString::number(t));
			label_after = m_layout.axis.labelTickEvery;
		}
		else {
			painter->drawLine(p1, p2);
		}
		label_after--;
	}
	painter->restore();
}

void Graph::drawVerticalHeader(QPainter *painter, int channel)
{
	const Channel &ch = m_channels[channel];
	QColor c = effectiveStyle.color();
	QColor bc = c;
	bc.setAlphaF(0.05);
	QString text = model()->channelData(channel, GraphModel::ChannelDataRole::Name).toString();
	if(text.isEmpty())
		text = "<no name>";
	QFont font = effectiveStyle.font();
	font.setBold(true);
	drawRectText(painter, ch.m_layout.verticalHeaderRect, text, font, c, bc);
}

void Graph::drawBackground(QPainter *painter, int channel)
{
	const Channel &ch = m_channels[channel];
	painter->fillRect(ch.m_layout.graphRect, ch.effectiveStyle.colorBackground());
}

void Graph::drawGrid(QPainter *painter, int channel)
{
	const Channel &ch = m_channels[channel];
	if(m_layout.axis.tickInterval == 0) {
		drawRectText(painter, ch.m_layout.graphRect, "grid", effectiveStyle.font(), ch.effectiveStyle.colorGrid());
		return;
	}
	QColor gc = ch.effectiveStyle.colorGrid();
	if(!gc.isValid())
		return;
	painter->save();
	QPen pen;
	pen.setWidth(1);
	//pen.setWidth(u2px(0.1));
	pen.setColor(gc);
	pen.setStyle(Qt::DotLine);
	painter->setPen(pen);
	{
		// draw X-axis grid
		int label_after = -1;
		for (timemsec_t t = m_layout.axis.tick0; ; t += m_layout.axis.tickInterval) {
			int x = timeToPos(t);
			if(x > m_layout.xAxisRect.right())
				break;
			if(t == m_layout.axis.labelTick0)
				label_after = 0;
			QPoint p1{x, ch.graphRect().top()};
			QPoint p2{x, ch.graphRect().bottom()};
			if(label_after == 0) {
				painter->drawLine(p1, p2);
				label_after = m_layout.axis.labelTickEvery;
			}
			label_after--;
		}
	}
	//painter->drawRect(ch.graphRect());
	painter->restore();
}

void Graph::drawYAxis(QPainter *painter, int channel)
{
	const Channel &ch = m_channels[channel];
	drawRectText(painter, ch.m_layout.yAxisRect, "y-axis", effectiveStyle.font(), ch.effectiveStyle.colorAxis());
}

std::function<QPoint (const Sample &)> Graph::sampleToPointFn(const DataRect &src, const QRect &dest)
{
	int le = dest.left();
	int ri = dest.right();
	int to = dest.top();
	int bo = dest.bottom();

	timemsec_t t1 = src.xRange.min;
	timemsec_t t2 = src.xRange.max;
	double d1 = src.yRange.min;
	double d2 = src.yRange.max;

	if(t2 - t1 == 0)
		return nullptr;
	double kx = static_cast<double>(ri - le) / (t2 - t1);
	//shvDebug() << d1 << d2;
	if(std::abs(d2 - d1) < 1e-6)
		return nullptr;
	double ky = (to - bo) / (d2 - d1);

	return  [le, bo, kx, t1, d1, ky](const Sample &s) -> QPoint {
		const timemsec_t t = s.time;
		double d = GraphModel::valueToDouble(s.value);
		double x = le + (t - t1) * kx;
		double y = bo + (d - d1) * ky;
		return QPoint{static_cast<int>(x), static_cast<int>(y)};
	};
}

std::function<Sample (const QPoint &)> Graph::pointToSampleFn(const QRect &src, const DataRect &dest)
{
	int le = src.left();
	int ri = src.right();
	int to = src.top();
	int bo = src.bottom();

	if(ri - le == 0)
		return nullptr;

	timemsec_t t1 = dest.xRange.min;
	timemsec_t t2 = dest.xRange.max;
	double d1 = dest.yRange.min;
	double d2 = dest.yRange.max;

	double kx = static_cast<double>(t2 - t1) / (ri - le);
	if(to - bo == 0)
		return nullptr;
	double ky = (d2 - d1) / (to - bo);

	return  [t1, le, kx, d1, bo, ky](const QPoint &p) -> Sample {
		const int x = p.x();
		const int y = p.y();
		timemsec_t t = static_cast<timemsec_t>(t1 + (x - le) * kx);
		double d = d1 + (y - bo) * ky;
		return Sample{t, d};
	};
}

std::function<Graph::timemsec_t (int)> Graph::posToTimeFn(const QPoint &src, const Graph::XRange &dest)
{
	int le = src.x();
	int ri = src.y();
	if(ri - le == 0)
		return nullptr;
	timemsec_t t1 = dest.min;
	timemsec_t t2 = dest.max;
	return [t1, t2, le, ri](int x) {
		return static_cast<timemsec_t>(t1 + static_cast<double>(x - le) * (t2 - t1) / (ri - le));
	};
}

std::function<int (Graph::timemsec_t)> Graph::timeToPosFn(const Graph::XRange &src, const QPoint &dest)
{
	timemsec_t t1 = src.min;
	timemsec_t t2 = src.max;
	if(t1 - t2 == 0)
		return nullptr;
	int le = dest.x();
	int ri = dest.y();
	return [t1, t2, le, ri](Graph::timemsec_t t) {
		return static_cast<timemsec_t>(le + static_cast<double>(t - t1) * (ri - le) / (t2 - t1));
	};
}

void Graph::drawSamples(QPainter *painter, int channel, const DataRect &src_rect, const QRect &dest_rect, const Graph::ChannelStyle &channel_style)
{
	//shvLogFuncFrame() << "channel:" << channel;
	const Channel &ch = channelAt(channel);
	QRect rect = dest_rect.isEmpty()? ch.m_layout.graphRect: dest_rect;
	ChannelStyle ch_style = channel_style.isEmpty()? ch.effectiveStyle: channel_style;

	XRange xrange;
	YRange yrange;
	if(src_rect.isNull()) {
		xrange = xRangeZoom();
		yrange = ch.yRangeZoom();
	}
	else {
		xrange = src_rect.xRange;
		yrange = src_rect.yRange;
	}
	auto sample2point = sampleToPointFn(DataRect{xrange, yrange}, rect);

	if(!sample2point)
		return;

	int interpolation = ch_style.interpolation();

	int sample_point_size = u2px(0.2);

	painter->save();
	painter->setClipRect(rect);
	QPen pen;
	QColor line_color = ch_style.color();
	pen.setColor(line_color);
	{
		double d = ch_style.lineWidth();
		if(d > 0)
			pen.setWidth(u2px(d));
		else
			pen.setWidth(2);
	}
	painter->setPen(pen);
	QColor line_area_color;
	if(ch_style.lineAreaStyle() == ChannelStyle::LineAreaStyle::Filled) {
		line_area_color = painter->pen().color();
		line_area_color.setAlphaF(0.4);
		//line_area_color.setHsv(line_area_color.hslHue(), line_area_color.hsvSaturation() / 2, line_area_color.lightness());
	}

	GraphModel *m = model();
	int ix1 = m->lessOrEqualIndex(channel, xrange.min);
	if(ix1 < 0)
		ix1 = 0;
	int ix2 = m->lessOrEqualIndex(channel, xrange.max) + 1;
	//shvInfo() << channel << "range:" << xrange.min << xrange.max;
	//shvInfo() << channel << ":" << ix1 << ix2;
	QPoint drawn_point, recent_point;
	int cnt = m->count(channel);
	//shvDebug() << "count:" << m->count(channel);
	for (int i = ix1; i <= ix2 && i < cnt; ++i) {
		const Sample s = m->sampleAt(channel, i);
		const QPoint p2 = sample2point(s);
		if(i == ix1) {
			//shvInfo() << channel << "first sample:" << s.time << s.value.toDouble();
			drawn_point = p2;
			recent_point = p2;
		}
		else {
			if(p2.x() != drawn_point.x()) {
				if(interpolation == ChannelStyle::Interpolation::None) {
					if(line_area_color.isValid()) {
						const QPoint p0 = sample2point(Sample{s.time, 0});
						painter->fillRect(QRect{p2, p0}, line_area_color);
					}
					QRect r0{QPoint(), QSize{sample_point_size, sample_point_size}};
					r0.moveCenter(p2);
					painter->fillRect(r0, line_color);
				}
				else if(interpolation == ChannelStyle::Interpolation::Stepped) {
					if(line_area_color.isValid()) {
						const QPoint p0 = sample2point(Sample{s.time, 0});
						painter->fillRect(QRect{drawn_point + QPoint{1, 0}, p0}, line_area_color);
					}
					QPoint p3{p2.x(), recent_point.y()};
					painter->drawLine(recent_point, p3);
					painter->drawLine(p3, p2);
				}
				else {
					if(line_area_color.isValid()) {
						const QPoint p0 = sample2point(Sample{s.time, 0});
						QPainterPath pp;
						pp.moveTo(recent_point);
						pp.lineTo(p2);
						pp.lineTo(p0);
						pp.lineTo(recent_point.x(), p0.y());
						pp.closeSubpath();
						painter->fillPath(pp, line_area_color);
					}
					painter->drawLine(recent_point, p2);
				}
				drawn_point = p2;
			}
			recent_point = p2;
		}
	}
	painter->restore();
}

} // namespace timeline
