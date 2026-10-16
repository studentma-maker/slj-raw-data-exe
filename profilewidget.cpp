#include "profilewidget.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainterPath>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const double kMarginL   = 88.0;
const double kMarginR   = 16.0;
const double kLegendW   = 330.0;
const double kTitleH    = 54.0;
const double kXAxisH    = 50.0;
const double kRowH      = 34.0;
const double kMeanRowH  = 24.0;
const int    kGridN     = kProfileGridN;   // 平均曲线/统计用的公共进度网格（见 profileanalysis.h）
const double kMmPerPulse = 0.003563;   // 上位机默认 mm/脉冲（只用于横轴的估算 mm 标注）
const double kMinXSpan  = 0.004;

// 参考色板的 8 色分类顺序（按加载顺序固定分配，第 9 组起同色改虚线）
const char *const kSeries[8] = {"#2a78d6", "#eb6834", "#1baf7a", "#eda100",
                                "#e87ba4", "#008300", "#6250d6", "#e34948"};
const QColor kInk("#212529");
const QColor kInk2("#495057");
const QColor kMuted("#868e96");
const QColor kGrid("#e9ecef");
const QColor kFrame("#adb5bd");

QFont fontPx(const QPainter &g, int px, bool bold = false)
{
    QFont f = g.font();
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

double niceStep(double range, double targetCount)
{
    if (!(range > 0.0) || targetCount < 1.0) return 1.0;
    const double raw = range / targetCount;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    const double f = raw / p;
    return (f < 1.5 ? 1.0 : f < 3.0 ? 2.0 : f < 7.0 ? 5.0 : 10.0) * p;
}

int decimalsFor(double step)
{
    if (step >= 1.0) return 0;
    return std::min(5, (int)std::ceil(-std::log10(step) - 1e-9));
}

QPointF eventPos(const QMouseEvent *e)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return e->position();
#else
    return e->localPos();
#endif
}

struct YRange { double lo = 0, hi = 1; };

YRange autoRange(const ProfileAnalysis &s)
{
    double lo = 1e18, hi = -1e18;
    for (const auto &g : s.grid)
        for (double v : g) { lo = std::min(lo, v); hi = std::max(hi, v); }
    if (!(hi > lo)) { lo -= 0.5; hi += 0.5; }
    const double pad = (hi - lo) * 0.06;
    lo -= pad; hi += pad;
    const double step = niceStep(hi - lo, 6.0);
    return {std::floor(lo / step) * step, std::ceil(hi / step) * step};
}

QString shortTime(const BeltProfile &p)
{
    return p.time.isEmpty() ? p.name : p.time;
}

} // namespace

QColor ProfileChart::seriesColor(int idx)
{
    return QColor(kSeries[((idx % 8) + 8) % 8]);
}

Qt::PenStyle ProfileChart::seriesStyle(int idx)
{
    return idx < 8 ? Qt::SolidLine : (idx < 16 ? Qt::DashLine : Qt::DotLine);
}

ProfileChart::Geom ProfileChart::geometry(const QRectF &rect, const QVector<BeltProfile> &ps,
                                          const QVector<bool> &visible)
{
    Geom gm;
    const double legendLeft = rect.right() - kLegendW - kMarginR;
    gm.plot  = QRectF(rect.left() + kMarginL, rect.top() + kTitleH,
                      std::max(100.0, legendLeft - 24 - rect.left() - kMarginL),
                      std::max(100.0, rect.height() - kTitleH - kXAxisH - 8));
    gm.xaxis = QRectF(gm.plot.left(), gm.plot.bottom() + 2, gm.plot.width(), kXAxisH - 2);
    gm.legend = QRectF(legendLeft, gm.plot.top(), kLegendW, gm.plot.height());
    double y = gm.legend.top() + 22 + kMeanRowH;
    for (int i = 0; i < ps.size(); ++i) {
        if (i < visible.size() && !visible[i]) continue;
        gm.visibleOrder.append(i);
        gm.legendRows.append(QRectF(gm.legend.left(), y, gm.legend.width(), kRowH));
        y += kRowH;
    }
    return gm;
}

void ProfileChart::render(QPainter &g, const QRectF &rect, const QVector<BeltProfile> &ps,
                          const QVector<bool> &visible, const View &v, const QVector<int> &highlight,
                          const QPointF *hover, const ProfileAnalysis *precomputed)
{
    g.save();
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setRenderHint(QPainter::TextAntialiasing, true);
    g.fillRect(rect, Qt::white);

    const Geom gm = geometry(rect, ps, visible);
    ProfileAnalysis computed;
    if (!precomputed) computed = analyzeProfiles(ps, visible, v.options());
    const ProfileAnalysis &st = precomputed ? *precomputed : computed;
    const QRectF &r = gm.plot;

    // ── 标题 ──
    g.setFont(fontPx(g, 16, true));
    g.setPen(kInk);
    g.drawText(QRectF(rect.left() + 16, rect.top() + 6, rect.width() - 32, 22), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("皮带激光轮廓对比   %1 组可见 / 共 %2 组").arg(st.vis.size()).arg(ps.size()));
    QString sub = v.deviation ? QStringLiteral("纵轴：激光读数减去本组均值（只比较形状）")
                              : QStringLiteral("纵轴：激光原始读数");
    if (st.rollerRemoved)
        sub += QStringLiteral("，已扣除周期 %1 mm 的辊筒成分").arg(v.rollerPeriodMm, 0, 'f', 1);
    if (st.vis.size() >= 2)
        sub += QStringLiteral("    两两相关系数平均 %1").arg(st.meanPairR, 0, 'f', 2);
    g.setFont(fontPx(g, 12));
    g.setPen(kInk2);
    g.drawText(QRectF(rect.left() + 16, rect.top() + 28, rect.width() - 32, 18), Qt::AlignLeft | Qt::AlignVCenter, sub);

    // ── 面板 + 网格 ──
    g.fillRect(r, QColor("#fcfcfd"));
    if (st.vis.isEmpty()) {
        g.setPen(kMuted);
        g.setFont(fontPx(g, 14));
        g.drawText(r, Qt::AlignCenter, ps.isEmpty() ? QStringLiteral("打开 .belt_profile.csv 查看皮带轮廓")
                                                    : QStringLiteral("没有勾选要显示的轮廓"));
        g.restore();
        return;
    }
    const YRange full = autoRange(st);
    const double ylo = full.lo + v.y[0] * (full.hi - full.lo);
    const double yhi = full.lo + v.y[1] * (full.hi - full.lo);
    auto xOf = [&](double pos) { return r.left() + (pos - v.x0) / (v.x1 - v.x0) * r.width(); };
    auto yOf = [&](double val) { return r.bottom() - (val - ylo) / (yhi - ylo) * r.height(); };

    const double yStep = niceStep(yhi - ylo, std::max(2.0, r.height() / 40.0));
    const int yDec = decimalsFor(yStep);
    g.setFont(fontPx(g, 10));
    for (double val = std::ceil(ylo / yStep - 1e-9) * yStep; val <= yhi + yStep * 1e-6; val += yStep) {
        const double y = yOf(val);
        g.setPen(QPen(kGrid, 1.0));
        g.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
        g.setPen(kInk2);
        g.drawText(QRectF(r.left() - 64, y - 7, 58, 14), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(std::fabs(val) < yStep * 1e-6 ? 0.0 : val, 'f', yDec));
    }
    const double xStep = niceStep(v.x1 - v.x0, std::max(2.0, r.width() / 110.0));
    const int xDec = decimalsFor(xStep);
    for (double pos = std::ceil(v.x0 / xStep - 1e-9) * xStep; pos <= v.x1 + xStep * 1e-6; pos += xStep) {
        const double x = xOf(pos);
        g.setPen(QPen(kGrid, 1.0));
        g.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
        g.setPen(QPen(kFrame, 1.0));
        g.drawLine(QPointF(x, r.bottom()), QPointF(x, r.bottom() + 4));
        g.setPen(kInk);
        g.drawText(QRectF(x - 50, gm.xaxis.top() + 4, 100, 13), Qt::AlignCenter, QString::number(pos, 'f', xDec));
        if (st.circMm > 0) {
            g.setPen(kInk2);
            g.drawText(QRectF(x - 50, gm.xaxis.top() + 18, 100, 13), Qt::AlignCenter,
                       QString::number(pos * st.circMm, 'f', 1));
        }
    }
    g.setFont(fontPx(g, 10, true));
    g.setPen(kInk);
    g.drawText(QRectF(r.left() - 84, gm.xaxis.top() + 4, 64, 13), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("进度"));
    if (st.circMm > 0) {
        g.setPen(kInk2);
        g.drawText(QRectF(r.left() - 84, gm.xaxis.top() + 18, 64, 13), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("≈mm"));
        g.setFont(fontPx(g, 10));
        g.drawText(QRectF(r.left(), gm.xaxis.top() + 32, r.width(), 14), Qt::AlignCenter,
                   QStringLiteral("横轴 = 皮带一圈进度(寄存器137/65536)；mm 按 一圈步数 × %1 mm/脉冲 估算，周长 ≈ %2 mm")
                       .arg(kMmPerPulse, 0, 'f', 6).arg(st.circMm, 0, 'f', 1));
    }
    g.save();
    g.translate(r.left() - 74, r.center().y());
    g.rotate(-90);
    g.setFont(fontPx(g, 11, true));
    g.setPen(kInk);
    g.drawText(QRectF(-120, -18, 240, 16), Qt::AlignCenter,
               v.deviation ? QStringLiteral("偏差 mm") : QStringLiteral("激光读数 mm"));
    g.restore();

    // ── 曲线 ──
    g.save();
    g.setClipRect(r.adjusted(-1, -1, 1, 1));
    const int k0 = std::max(0, (int)std::floor(v.x0 * kGridN - 1));
    const int k1 = std::min(kGridN - 1, (int)std::ceil(v.x1 * kGridN + 1));
    auto pathOf = [&](const QVector<double> &vals) {
        QPainterPath path;
        for (int k = k0; k <= k1; ++k) {
            const QPointF p(xOf((k + 0.5) / kGridN), yOf(vals[k]));
            if (k == k0) path.moveTo(p); else path.lineTo(p);
        }
        return path;
    };
    // 高亮组最后画，压在最上面
    QVector<int> order;
    for (int k = 0; k < st.vis.size(); ++k) if (!highlight.contains(st.vis[k])) order.append(k);
    for (int k = 0; k < st.vis.size(); ++k) if (highlight.contains(st.vis[k])) order.append(k);
    for (int k : order) {
        const int idx = st.vis[k];
        QColor c = seriesColor(idx);
        double w = 1.4;
        if (!highlight.isEmpty()) {
            if (highlight.contains(idx)) w = 2.6;
            else c.setAlpha(55);
        }
        g.setPen(QPen(c, w, seriesStyle(idx), Qt::RoundCap, Qt::RoundJoin));
        g.setBrush(Qt::NoBrush);
        g.drawPath(pathOf(st.grid[k]));
    }
    if (v.showMean && st.vis.size() >= 2) {
        g.setPen(QPen(QColor(255, 255, 255, 220), 5.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));   // 白描边，压在彩线上仍清楚
        g.drawPath(pathOf(st.mean));
        g.setPen(QPen(kInk, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        g.drawPath(pathOf(st.mean));
    }
    g.restore();
    g.setPen(QPen(kFrame, 1.0));
    g.setBrush(Qt::NoBrush);
    g.drawRect(r);

    // ── 图例 ──
    const QRectF &lg = gm.legend;
    g.setFont(fontPx(g, 11, true));
    g.setPen(kInk);
    g.drawText(QRectF(lg.left(), lg.top(), lg.width(), 18), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("图例（悬停高亮，点击固定）"));
    {
        const double y = lg.top() + 22;
        if (v.showMean && st.vis.size() >= 2) {
            g.setPen(QPen(kInk, 2.4));
            g.drawLine(QPointF(lg.left() + 2, y + 9), QPointF(lg.left() + 28, y + 9));
            g.setFont(fontPx(g, 11, true));
            g.setPen(kInk);
            g.drawText(QRectF(lg.left() + 36, y, lg.width() - 36, 18), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("平均曲线（%1 组）").arg(st.vis.size()));
        }
    }
    for (int k = 0; k < gm.visibleOrder.size(); ++k) {
        const int idx = gm.visibleOrder[k];
        const BeltProfile &p = ps[idx];
        const QRectF row = gm.legendRows[k];
        if (row.bottom() > rect.bottom()) break;
        if (highlight.contains(idx)) g.fillRect(row.adjusted(-4, 0, 0, -2), QColor("#f1f3f5"));
        g.setPen(QPen(seriesColor(idx), 2.4, seriesStyle(idx), Qt::RoundCap));
        g.drawLine(QPointF(row.left() + 2, row.top() + 9), QPointF(row.left() + 28, row.top() + 9));
        g.setFont(fontPx(g, 11, true));
        g.setPen(kInk);
        g.drawText(QRectF(row.left() + 36, row.top(), row.width() - 36, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("#%1  %2").arg(idx + 1).arg(shortTime(p)));
        QString info = QStringLiteral("峰谷 %1 mm").arg(p.p2p, 0, 'f', 3);
        if (p.samples > 0) info += QStringLiteral(" · 样本 %1").arg(p.samples);
        if (st.vis.size() >= 2 && k < st.rToMean.size())
            info += QStringLiteral(" · 与平均 r=%1").arg(st.rToMean[k], 0, 'f', 2);
        if (!v.deviation) info += QStringLiteral(" · 均值 %1").arg(p.mean, 0, 'f', 3);
        g.setFont(fontPx(g, 10));
        g.setPen(kInk2);
        g.drawText(QRectF(row.left() + 36, row.top() + 15, row.width() - 36, 15), Qt::AlignLeft | Qt::AlignVCenter, info);
    }

    // ── 悬停：十字线 + 各组在该进度的值 ──
    if (hover && r.contains(*hover)) {
        const double pos = v.x0 + (hover->x() - r.left()) / r.width() * (v.x1 - v.x0);
        const int k = std::min(kGridN - 1, std::max(0, (int)(pos * kGridN)));
        g.setPen(QPen(QColor(33, 37, 41, 150), 1.0));
        g.drawLine(QPointF(hover->x(), r.top()), QPointF(hover->x(), r.bottom()));
        QStringList lines;
        QVector<int> who;
        lines << (st.circMm > 0 ? QStringLiteral("进度 %1   ≈ %2 mm").arg(pos, 0, 'f', 4).arg(pos * st.circMm, 0, 'f', 1)
                                : QStringLiteral("进度 %1").arg(pos, 0, 'f', 4));
        who << -2;
        if (v.showMean && st.vis.size() >= 2) {
            lines << QStringLiteral("平均   %1 mm").arg(st.mean[k], 0, 'f', 3);
            who << -1;
        }
        for (int j = 0; j < st.vis.size() && lines.size() < 22; ++j) {
            lines << QStringLiteral("#%1   %2 mm").arg(st.vis[j] + 1).arg(st.grid[j][k], 0, 'f', 3);
            who << st.vis[j];
        }
        g.setFont(fontPx(g, 11));
        const QFontMetricsF fm(g.font());
        double w = 0;
        for (const QString &s : lines) w = std::max(w, fm.horizontalAdvance(s));
        const double lh = 15.0;
        QRectF box(hover->x() + 14, hover->y() + 10, w + 34, lines.size() * lh + 10);
        if (box.right() > r.right()) box.moveRight(hover->x() - 14);
        if (box.bottom() > rect.bottom() - 4) box.moveBottom(rect.bottom() - 4);
        g.setPen(QPen(kFrame, 1.0));
        g.setBrush(QColor(255, 255, 255, 240));
        g.drawRoundedRect(box, 4, 4);
        for (int i = 0; i < lines.size(); ++i) {
            const double ty = box.top() + 16 + i * lh;
            if (who[i] >= 0) {
                g.setPen(QPen(seriesColor(who[i]), 2.4, seriesStyle(who[i])));
                g.drawLine(QPointF(box.left() + 8, ty - 4), QPointF(box.left() + 22, ty - 4));
            } else if (who[i] == -1) {
                g.setPen(QPen(kInk, 2.4));
                g.drawLine(QPointF(box.left() + 8, ty - 4), QPointF(box.left() + 22, ty - 4));
            }
            g.setPen(i == 0 ? kInk : kInk2);
            g.setFont(fontPx(g, 11, i == 0));
            g.drawText(QPointF(box.left() + 28, ty), lines[i]);
        }
    }
    g.restore();
}

// ─────────────────────────── 控件 ───────────────────────────

ProfileWidget::ProfileWidget(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(720, 480);
    setFocusPolicy(Qt::StrongFocus);
}

void ProfileWidget::setProfiles(const QVector<BeltProfile> *ps, const QVector<bool> *visible)
{
    m_ps = ps;
    m_visible = visible;
    m_hoverRow = -1;
    m_anDirty = true;
    if (!ps) m_pinned.clear();
    else for (int i = m_pinned.size() - 1; i >= 0; --i) if (m_pinned[i] >= ps->size()) m_pinned.removeAt(i);
    update();
}

void ProfileWidget::setHighlight(int idx)
{
    m_pinned.clear();
    if (idx >= 0) m_pinned.append(idx);
    update();
}
void ProfileWidget::setHighlightPair(int a, int b)
{
    m_pinned = {a, b};
    update();
}
void ProfileWidget::refresh() { m_anDirty = true; update(); }

void ProfileWidget::resetView()
{
    m_view.x0 = 0.0;
    m_view.x1 = 1.0;
    m_view.y[0] = 0.0;
    m_view.y[1] = 1.0;
    update();
}

void ProfileWidget::setDeviation(bool on) { m_view.deviation = on; m_view.y[0] = 0.0; m_view.y[1] = 1.0; refresh(); }
void ProfileWidget::setShowMean(bool on)  { m_view.showMean = on; update(); }
void ProfileWidget::setRemoveRoller(bool on) { m_view.removeRoller = on; refresh(); }
void ProfileWidget::setRollerPeriodMm(double mm) { m_view.rollerPeriodMm = mm; refresh(); }

void ProfileWidget::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    static const QVector<BeltProfile> kEmpty;
    static const QVector<bool> kNone;
    const QVector<int> hl = m_hoverRow >= 0 ? QVector<int>{m_hoverRow} : m_pinned;
    const QVector<BeltProfile> &ps = m_ps ? *m_ps : kEmpty;
    const QVector<bool> &vis = m_visible ? *m_visible : kNone;
    if (m_anDirty) {
        m_an = analyzeProfiles(ps, vis, m_view.options());
        m_anDirty = false;
    }
    ProfileChart::render(g, QRectF(rect()), ps, vis, m_view, hl,
                         (m_hoverValid && !m_panning) ? &m_hoverPos : nullptr, &m_an);
}

int ProfileWidget::legendRowAt(const QPointF &pos) const
{
    if (!m_ps) return -1;
    static const QVector<bool> kNone;
    const ProfileChart::Geom gm = ProfileChart::geometry(QRectF(rect()), *m_ps, m_visible ? *m_visible : kNone);
    for (int k = 0; k < gm.legendRows.size(); ++k)
        if (gm.legendRows[k].contains(pos)) return gm.visibleOrder[k];
    return -1;
}

void ProfileWidget::clampX()
{
    double span = std::min(1.0, std::max(kMinXSpan, m_view.x1 - m_view.x0));
    if (m_view.x0 < 0.0) m_view.x0 = 0.0;
    if (m_view.x0 + span > 1.0) m_view.x0 = 1.0 - span;
    m_view.x1 = m_view.x0 + span;
}

void ProfileWidget::wheelEvent(QWheelEvent *e)
{
    if (!m_ps) return;
    const QPoint ad = e->angleDelta();
    const double steps = (ad.y() != 0 ? ad.y() : ad.x()) / 120.0;
    if (steps == 0.0) return;
    static const QVector<bool> kNone;
    const ProfileChart::Geom gm = ProfileChart::geometry(QRectF(rect()), *m_ps, m_visible ? *m_visible : kNone);
    const QPointF pos = e->position();
    const double factor = std::pow(0.8, steps);
    if (e->modifiers() & Qt::ControlModifier) {
        const double rel = std::min(1.0, std::max(0.0, (gm.plot.bottom() - pos.y()) / gm.plot.height()));
        const double span = m_view.y[1] - m_view.y[0];
        const double at = m_view.y[0] + rel * span;
        const double ns = std::min(1.0, std::max(0.01, span * factor));
        m_view.y[0] = std::min(1.0 - ns, std::max(0.0, at - rel * ns));
        m_view.y[1] = m_view.y[0] + ns;
    } else if (e->modifiers() & Qt::ShiftModifier) {
        const double span = m_view.x1 - m_view.x0;
        m_view.x0 -= steps * span * 0.1;
        m_view.x1 = m_view.x0 + span;
        clampX();
    } else {
        const double rel = std::min(1.0, std::max(0.0, (pos.x() - gm.plot.left()) / gm.plot.width()));
        const double span = m_view.x1 - m_view.x0;
        const double t = m_view.x0 + rel * span;
        const double ns = std::max(kMinXSpan, span * factor);
        m_view.x0 = t - rel * ns;
        m_view.x1 = m_view.x0 + ns;
        clampX();
    }
    e->accept();
    update();
}

void ProfileWidget::mousePressEvent(QMouseEvent *e)
{
    if (!m_ps) return;
    const QPointF pos = eventPos(e);
    if (e->button() != Qt::LeftButton) return;
    const int row = legendRowAt(pos);
    if (row >= 0) {
        if (m_pinned.size() == 1 && m_pinned.first() == row) m_pinned.clear();
        else m_pinned = {row};
        update();
        return;
    }
    m_panning = true;
    m_lastPos = pos;
    setCursor(Qt::ClosedHandCursor);
}

void ProfileWidget::mouseMoveEvent(QMouseEvent *e)
{
    if (!m_ps) return;
    const QPointF pos = eventPos(e);
    if (m_panning) {
        static const QVector<bool> kNone;
        const ProfileChart::Geom gm = ProfileChart::geometry(QRectF(rect()), *m_ps, m_visible ? *m_visible : kNone);
        const double span = m_view.x1 - m_view.x0;
        m_view.x0 -= (pos.x() - m_lastPos.x()) / gm.plot.width() * span;
        m_view.x1 = m_view.x0 + span;
        clampX();
        const double ys = m_view.y[1] - m_view.y[0];
        if (ys < 1.0) {
            double ny = m_view.y[0] + (pos.y() - m_lastPos.y()) / gm.plot.height() * ys;
            ny = std::min(1.0 - ys, std::max(0.0, ny));
            m_view.y[0] = ny;
            m_view.y[1] = ny + ys;
        }
        m_lastPos = pos;
    }
    m_hoverRow = legendRowAt(pos);
    m_hoverPos = pos;
    m_hoverValid = true;
    update();
}

void ProfileWidget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_panning) {
        m_panning = false;
        unsetCursor();
    }
}

void ProfileWidget::mouseDoubleClickEvent(QMouseEvent *) { resetView(); }

void ProfileWidget::leaveEvent(QEvent *)
{
    m_hoverValid = false;
    m_hoverRow = -1;
    update();
}
