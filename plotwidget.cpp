#include "plotwidget.h"

#include <QMouseEvent>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

const double kMinYFrac = 0.005;

QPointF eventPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}

} // namespace

PlotWidget::PlotWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(640, 520);
    setFocusPolicy(Qt::StrongFocus);
}

void PlotWidget::setPiece(const Piece *piece, const HeaderInfo *hdr)
{
    m_piece = piece;
    m_hdr = hdr;
    m_hoverValid = false;
    m_panning = m_boxing = false;
    resetView();
}

void PlotWidget::resetView()
{
    if (m_piece) PieceChart::resetView(*m_piece, m_view);
    update();
}

void PlotWidget::setShowWin(bool on)    { m_view.showWin = on; update(); }
void PlotWidget::setShowEvents(bool on) { m_view.showEvents = on; update(); }
void PlotWidget::setShowRaw(bool on)    { m_view.showRaw = on; update(); }
void PlotWidget::setShowFlt(bool on)    { m_view.showFlt = on; update(); }

void PlotWidget::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    if (!m_piece) {
        g.fillRect(rect(), Qt::white);
        return;
    }
    const bool showHover = m_hoverValid && !m_panning && !m_boxing;
    PieceChart::render(g, QRectF(rect()), *m_piece, m_hdr, m_view, showHover ? &m_hoverPos : nullptr);

    if (m_boxing) {
        g.setPen(QPen(QColor("#1c7ed6"), 1.0, Qt::DashLine));
        g.setBrush(QColor(28, 126, 214, 40));
        g.drawRect(QRectF(m_boxStart, m_hoverPos).normalized());
    }
}

int PlotWidget::panelAt(const QPointF &pos, const PieceChart::Geom &gm) const
{
    for (int i = 0; i < PieceChart::PanelCount; ++i)
        if (pos.y() >= gm.panel[i].top() && pos.y() <= gm.panel[i].bottom()) return i;
    return -1;
}

void PlotWidget::clampX()
{
    double f0, f1;
    PieceChart::fullXRange(*m_piece, f0, f1);
    double span = std::min(m_view.x1 - m_view.x0, f1 - f0);
    span = std::max(span, PieceChart::minXSpan(*m_piece));
    if (m_view.x0 < f0) m_view.x0 = f0;
    if (m_view.x0 + span > f1) m_view.x0 = f1 - span;
    m_view.x1 = m_view.x0 + span;
}

void PlotWidget::zoomX(double factor, double anchorX, const PieceChart::Geom &gm)
{
    const double w = gm.plotRight - gm.plotLeft;
    const double rel = std::min(1.0, std::max(0.0, (anchorX - gm.plotLeft) / w));
    const double span = m_view.x1 - m_view.x0;
    const double t = m_view.x0 + rel * span;
    const double newSpan = std::max(PieceChart::minXSpan(*m_piece), span * factor);
    m_view.x0 = t - rel * newSpan;
    m_view.x1 = m_view.x0 + newSpan;
    clampX();
}

void PlotWidget::zoomY(int panel, double factor, double anchorY, const PieceChart::Geom &gm)
{
    const QRectF &r = gm.panel[panel];
    double *y = m_view.y[panel];
    const double rel = std::min(1.0, std::max(0.0, (r.bottom() - anchorY) / r.height()));
    const double span = y[1] - y[0];
    const double at = y[0] + rel * span;
    const double newSpan = std::min(1.0, std::max(kMinYFrac, span * factor));
    y[0] = at - rel * newSpan;
    if (y[0] < 0.0) y[0] = 0.0;
    if (y[0] + newSpan > 1.0) y[0] = 1.0 - newSpan;
    y[1] = y[0] + newSpan;
}

void PlotWidget::wheelEvent(QWheelEvent *e)
{
    if (!m_piece) return;
    // Alt+滚轮在部分平台上会把增量放到 x 分量
    const QPoint ad = e->angleDelta();
    const double steps = (ad.y() != 0 ? ad.y() : ad.x()) / 120.0;
    if (steps == 0.0) return;
    const QPointF pos = e->position();
    const PieceChart::Geom gm = PieceChart::geometry(QRectF(rect()), *m_piece, m_view);
    const double factor = std::pow(0.8, steps);

    if (e->modifiers() & Qt::ControlModifier) {
        const int panel = panelAt(pos, gm);
        if (panel >= 0) zoomY(panel, factor, pos.y(), gm);
    } else if (e->modifiers() & Qt::ShiftModifier) {
        const double span = m_view.x1 - m_view.x0;
        m_view.x0 -= steps * span * 0.1;
        m_view.x1 = m_view.x0 + span;
        clampX();
    } else {
        zoomX(factor, pos.x(), gm);
    }
    e->accept();
    update();
}

void PlotWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_piece) return;
    const QPointF pos = eventPos(e);
    if (e->button() == Qt::LeftButton) {
        const PieceChart::Geom gm = PieceChart::geometry(QRectF(rect()), *m_piece, m_view);
        m_panning = true;
        m_panPanel = panelAt(pos, gm);
        m_lastPos = pos;
        setCursor(Qt::ClosedHandCursor);
    } else if (e->button() == Qt::RightButton) {
        m_boxing = true;
        m_boxStart = pos;
        m_hoverPos = pos;
    }
    update();
}

void PlotWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_piece) return;
    const QPointF pos = eventPos(e);
    if (m_panning) {
        const PieceChart::Geom gm = PieceChart::geometry(QRectF(rect()), *m_piece, m_view);
        const double span = m_view.x1 - m_view.x0;
        m_view.x0 -= (pos.x() - m_lastPos.x()) / (gm.plotRight - gm.plotLeft) * span;
        m_view.x1 = m_view.x0 + span;
        clampX();
        if (m_panPanel >= 0) {
            double *y = m_view.y[m_panPanel];
            const double ys = y[1] - y[0];
            if (ys < 1.0) {
                double ny = y[0] + (pos.y() - m_lastPos.y()) / gm.panel[m_panPanel].height() * ys;
                ny = std::min(1.0 - ys, std::max(0.0, ny));
                y[0] = ny;
                y[1] = ny + ys;
            }
        }
        m_lastPos = pos;
    }
    m_hoverPos = pos;
    m_hoverValid = true;
    update();
}

void PlotWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_piece) return;
    const QPointF pos = eventPos(e);
    if (e->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        unsetCursor();
    } else if (e->button() == Qt::RightButton && m_boxing) {
        m_boxing = false;
        const PieceChart::Geom gm = PieceChart::geometry(QRectF(rect()), *m_piece, m_view);
        const QRectF box = QRectF(m_boxStart, pos).normalized();
        if (box.width() >= 8.0) {
            const double w = gm.plotRight - gm.plotLeft;
            const double span = m_view.x1 - m_view.x0;
            const double a = m_view.x0 + (box.left() - gm.plotLeft) / w * span;
            const double b = m_view.x0 + (box.right() - gm.plotLeft) / w * span;
            m_view.x0 = a;
            m_view.x1 = std::max(b, a + PieceChart::minXSpan(*m_piece));
            clampX();
        }
        // 框的上下边都落在同一个面板里且有一定高度时，同时放大该面板的纵轴
        const int pa = panelAt(box.topLeft(), gm);
        if (box.height() >= 12.0 && pa >= 0 && pa == panelAt(box.bottomRight(), gm)) {
            const QRectF &r = gm.panel[pa];
            double *y = m_view.y[pa];
            const double ys = y[1] - y[0];
            const double lo = y[0] + (r.bottom() - box.bottom()) / r.height() * ys;
            const double hi = y[0] + (r.bottom() - box.top()) / r.height() * ys;
            if (hi - lo >= kMinYFrac) { y[0] = lo; y[1] = hi; }
        }
    }
    update();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_panning = false;
        unsetCursor();
        resetView();
    }
}

void PlotWidget::leaveEvent(QEvent *)
{
    m_hoverValid = false;
    update();
}
