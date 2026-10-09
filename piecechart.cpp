#include "piecechart.h"

#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

const double kMarginL  = 70.0;
const double kMarginR  = 70.0;
const double kTitleH   = 58.0;
const double kEventRowH = 13.0;
const int    kEventRows = 4;
const double kXAxisH   = 54.0;
const double kLaneH    = 12.0;
const double kDigRowH  = 14.0;
const double kStateH   = 18.0;
const double kRowBandH = 16.0;
const double kGap      = 8.0;
const double kWeights[PieceChart::PanelCount] = {3.0, 1.3, 1.3, 1.3};

const QColor kColThick("#1c7ed6");
const QColor kColWin("#e8590c");
const QColor kColRaw("#ae3ec9");
const QColor kColFlt("#2b8a3e");
const QColor kColFreq("#c2255c");
const QColor kColPos("#0b7285");
const QColor kColTime("#868e96");
const QColor kColGrid("#e9ecef");
const QColor kColFrame("#868e96");
const QColor kColText("#212529");
const QColor kColSubText("#495057");

struct Range { double lo = 0.0, hi = 1.0; };
struct Ranges {
    Range   left[PieceChart::PanelCount];
    Range   time;     // 右轴：相对 CH1 上升沿的秒数
};

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
    const double p   = std::pow(10.0, std::floor(std::log10(raw)));
    const double f   = raw / p;
    const double s   = f < 1.5 ? 1.0 : (f < 3.0 ? 2.0 : (f < 7.0 ? 5.0 : 10.0));
    return s * p;
}

int decimalsFor(double step)
{
    if (step >= 1.0) return 0;
    return std::min(4, (int)std::ceil(-std::log10(step) - 1e-9));
}

Range niceRange(double lo, double hi, bool fromZero)
{
    if (!(hi > lo)) { hi = lo + 1.0; }
    if (fromZero) lo = 0.0;
    const double pad = (hi - lo) * 0.06;
    hi += pad;
    if (!fromZero) lo -= pad;
    const double step = niceStep(hi - lo, 5.0);
    Range r;
    r.lo = std::floor(lo / step) * step;
    r.hi = std::ceil(hi / step) * step;
    if (fromZero) r.lo = 0.0;
    return r;
}

double fltThickAt(const Piece &p, const Tick &t);
double rawThickAt(const Piece &p, const Tick &t);

Ranges computeRanges(const Piece &p)
{
    Ranges rg;
    double thkMax = 0.5, lasMin = 1e9, lasMax = -1e9, freqMax = 10.0, posMax = 10.0;
    // 激光量程：lflt 全范围 ∪ lraw 的 0.5%~99.5% 分位。lraw 偶发的单点跳变(如读到 0)
    // 不参与定量程，否则整条曲线会被压成一条直线
    QVector<float> raw;
    raw.reserve(p.ticks.size());
    for (const Tick &t : p.ticks) raw.append(t.lraw);
    if (!raw.isEmpty()) {
        std::sort(raw.begin(), raw.end());
        lasMin = raw[(int)((raw.size() - 1) * 0.005)];
        lasMax = raw[(int)((raw.size() - 1) * 0.995)];
    }
    for (const Tick &t : p.ticks) {
        thkMax = std::max(thkMax, fltThickAt(p, t));
        lasMin = std::min(lasMin, (double)t.lflt);
        lasMax = std::max(lasMax, (double)t.lflt);
        freqMax = std::max(freqMax, (double)t.freq);
        posMax = std::max(posMax, (double)t.posLaser);
    }
    // 原始厚度(base−lraw)同样只取 99.5% 分位参与定量程
    if (p.baseMm >= 0 && !raw.isEmpty()) {
        QVector<float> rt;
        for (const Tick &t : p.ticks)
            if (!std::isnan(rawThickAt(p, t))) rt.append((float)p.baseMm - t.lraw);
        if (!rt.isEmpty()) {
            std::sort(rt.begin(), rt.end());
            thkMax = std::max(thkMax, (double)rt[(int)((rt.size() - 1) * 0.995)]);
        }
    }
    for (const WinRec &w : p.wins) thkMax = std::max(thkMax, w.mean);
    if (lasMin > lasMax) { lasMin = 0.0; lasMax = 1.0; }
    rg.left[PieceChart::PanelThick] = niceRange(0.0, thkMax, true);
    rg.left[PieceChart::PanelLaser] = niceRange(lasMin, lasMax, false);
    rg.left[PieceChart::PanelFreq]  = niceRange(0.0, freqMax, true);
    rg.left[PieceChart::PanelPos]   = niceRange(0.0, posMax, true);
    rg.time = niceRange(0.0, std::max(1.0, (double)(p.endMs - p.startMs) / 1000.0), true);
    return rg;
}

Range applyView(const Range &r, const double frac[2])
{
    Range o;
    o.lo = r.lo + frac[0] * (r.hi - r.lo);
    o.hi = r.lo + frac[1] * (r.hi - r.lo);
    return o;
}

QColor stateColor(int s)
{
    static const char *const c[ST_COUNT] = {"#dee2e6", "#ffe066", "#b2f2bb", "#69db7c", "#ffc078",
                                            "#74c0fc", "#4dabf7", "#b197fc", "#ffa8a8"};
    return (s >= 0 && s < ST_COUNT) ? QColor(c[s]) : QColor("#dee2e6");
}

QColor eventColor(int kind)
{
    switch (kind) {
    case EvEdge:        return QColor("#1864ab");
    case EvState:       return QColor("#2b8a3e");
    case EvRowSwitch:   return QColor("#7048e8");
    case EvAlarm:       return QColor("#e03131");
    default:            return QColor("#212529");
    }
}

// 厚度两条曲线的取值：从第一行 TICK（ch1 上升沿）起全程绘制。
// 新版采集程序的 TICK.thick 全程都是真实读数；旧日志在料头到激光之前、料尾过激光
// (PIECE_DONE)之后该列写死 0，这里按同一公式 base − lflt（钳位 0~10mm）补算，
// 两种日志画出来一致。基线未知（本片没有 BASELINE 行）时只能按日志原值画
double fltThickAt(const Piece &p, const Tick &t)
{
    const int i = int(&t - p.ticks.constData());
    const bool onMaterial = p.laserHeadTick >= 0 && i >= p.laserHeadTick && i <= p.laserTailTick;
    if (!onMaterial && t.posLaser < 0.0f && t.thick == 0.0f && p.baseMm >= 0)
        return std::min(10.0, std::max(0.0, p.baseMm - (double)t.lflt));
    return (double)t.thick;
}
double rawThickAt(const Piece &p, const Tick &t)
{
    if (p.baseMm < 0) return std::numeric_limits<double>::quiet_NaN();
    return p.baseMm - (double)t.lraw;
}

// PIECE_DONE(料尾过激光)：始终标在厚度面板上，之后的区段加浅灰底表示“激光下已无本片物料”
void drawPieceDone(QPainter &g, const PieceChart::Geom &gm, const Piece &p, double x)
{
    const QRectF &r = gm.panel[PieceChart::PanelThick];
    if (x > gm.plotRight) return;
    const double xa = std::max(x, gm.plotLeft);
    g.fillRect(QRectF(xa, r.top(), gm.plotRight - xa, r.height()), QColor(134, 142, 150, 28));
    if (x < gm.plotLeft) return;
    g.setPen(QPen(QColor("#212529"), 1.8));
    g.drawLine(QPointF(x, gm.top), QPointF(x, gm.bottom));
    const QString txt = p.lenMm >= 0 ? QStringLiteral("PIECE_DONE 料尾过激光 · 发料长度 %1 mm").arg(p.lenMm, 0, 'f', 1)
                                     : QStringLiteral("PIECE_DONE 料尾过激光");
    QFont f = g.font();
    f.setPixelSize(11);
    f.setBold(true);
    g.setFont(f);
    const double tw = QFontMetricsF(f).horizontalAdvance(txt) + 14;
    QRectF box(x + 6, r.bottom() - 26, tw, 20);
    if (box.right() > gm.plotRight - 2) box.moveRight(x - 6);
    g.setPen(Qt::NoPen);
    g.setBrush(QColor("#212529"));
    g.drawRoundedRect(box, 3, 3);
    g.setPen(Qt::white);
    g.drawText(box, Qt::AlignCenter, txt);
}

// 把绘制所需的坐标换算集中在一处。横轴值 = 累计皮带脉冲(Piece::xs)
struct Mapper {
    const Piece &p;
    const PieceChart::View &v;
    const PieceChart::Geom &gm;

    double tOf(qint64 ms) const { return (double)(ms - p.startMs) / 1000.0; }
    double xOfV(double val) const
    {
        return gm.plotLeft + (val - v.x0) / (v.x1 - v.x0) * (gm.plotRight - gm.plotLeft);
    }
    double vOfX(double x) const
    {
        return v.x0 + (x - gm.plotLeft) / (gm.plotRight - gm.plotLeft) * (v.x1 - v.x0);
    }
    // 时间戳 → 横轴值：在相邻两个 tick 之间线性插值（事件/WIN 只有时间戳，靠它落到距离轴上）
    double vAtMs(qint64 ms) const
    {
        const QVector<Tick> &tk = p.ticks;
        if (tk.isEmpty()) return 0.0;
        auto it = std::lower_bound(tk.begin(), tk.end(), ms, [](const Tick &t, qint64 m) { return t.ms < m; });
        const int i = int(it - tk.begin());
        if (i <= 0) return p.xs.first();
        if (i >= tk.size()) return p.xs.last();
        const qint64 t0 = tk[i - 1].ms, t1 = tk[i].ms;
        if (t1 <= t0) return p.xs[i];
        return p.xs[i - 1] + (p.xs[i] - p.xs[i - 1]) * (double)(ms - t0) / (double)(t1 - t0);
    }
    double xOfMs(qint64 ms) const { return xOfV(vAtMs(ms)); }
    static double yOf(const QRectF &r, const Range &rg, double val)
    {
        return r.bottom() - (val - rg.lo) / (rg.hi - rg.lo) * r.height();
    }
    // 可见范围内的 tick 下标区间（两端各多取一个点，保证线画到边界）
    void visibleTicks(int &i0, int &i1) const
    {
        const QVector<double> &xs = p.xs;
        auto a = std::lower_bound(xs.begin(), xs.end(), v.x0);
        auto b = std::upper_bound(xs.begin(), xs.end(), v.x1);
        i0 = std::max(0, int(a - xs.begin()) - 1);
        i1 = std::min((int)xs.size() - 1, int(b - xs.begin()));
    }
    int nearestTick(double val) const
    {
        const QVector<double> &xs = p.xs;
        if (xs.isEmpty()) return -1;
        int i = int(std::lower_bound(xs.begin(), xs.end(), val) - xs.begin());
        if (i >= xs.size()) return xs.size() - 1;
        if (i > 0 && (val - xs[i - 1]) < (xs[i] - val)) --i;
        return i;
    }
};

template <typename Fn>
void drawSeries(QPainter &g, const Mapper &m, const QRectF &r, const Range &rg, const QPen &pen, Fn value)
{
    int i0, i1;
    m.visibleTicks(i0, i1);
    if (i1 < i0) return;
    g.save();
    g.setClipRect(r);
    g.setPen(pen);
    g.setBrush(Qt::NoBrush);
    QPolygonF poly;
    for (int i = i0; i <= i1; ++i) {
        const Tick &t = m.p.ticks[i];
        const double val = value(t);
        if (std::isnan(val)) {
            if (poly.size() > 1) g.drawPolyline(poly);
            poly.clear();
            continue;
        }
        poly << QPointF(m.xOfV(m.p.xs[i]), Mapper::yOf(r, rg, val));
    }
    if (poly.size() > 1) g.drawPolyline(poly);
    g.restore();
}

// 按“值不变的连续段”遍历可见 tick：draw(xa, xb, value)
template <typename Fn, typename DrawFn>
void forRuns(const Mapper &m, Fn value, DrawFn draw)
{
    int i0, i1;
    m.visibleTicks(i0, i1);
    if (i1 < i0) return;
    const QVector<Tick> &tk = m.p.ticks;
    const QVector<double> &xs = m.p.xs;
    int cur = value(tk[i0]);
    double startV = xs[i0];
    for (int i = i0 + 1; i <= i1 + 1; ++i) {
        const bool end = i > i1;
        const int val = end ? cur : value(tk[i]);
        if (end || val != cur) {
            const double endV = end ? xs[i1] : xs[i];
            const double xa = std::max(m.gm.plotLeft, m.xOfV(startV));
            const double xb = std::min(m.gm.plotRight, m.xOfV(endV));
            if (xb > xa) draw(xa, xb, cur);
            if (!end) { cur = val; startV = xs[i]; }
        }
    }
}

void drawPanelFrame(QPainter &g, const Mapper &m, const QRectF &r, const Range &rg, const QString &title,
                    const QColor &axisColor, double xStep)
{
    g.fillRect(r, QColor("#fcfcfd"));
    // 网格
    g.setPen(QPen(kColGrid, 1.0));
    const double yStep = niceStep(rg.hi - rg.lo, std::max(2.0, r.height() / 34.0));
    const int yDec = decimalsFor(yStep);
    for (double val = std::ceil(rg.lo / yStep - 1e-9) * yStep; val <= rg.hi + yStep * 1e-6; val += yStep) {
        const double y = Mapper::yOf(r, rg, val);
        g.drawLine(QPointF(r.left(), y), QPointF(r.right(), y));
    }
    for (double t = std::ceil(m.v.x0 / xStep - 1e-9) * xStep; t <= m.v.x1 + xStep * 1e-6; t += xStep) {
        const double x = m.xOfV(t);
        g.drawLine(QPointF(x, r.top()), QPointF(x, r.bottom()));
    }
    // 左轴刻度
    g.setFont(fontPx(g, 10));
    g.setPen(axisColor);
    for (double val = std::ceil(rg.lo / yStep - 1e-9) * yStep; val <= rg.hi + yStep * 1e-6; val += yStep) {
        const double y = Mapper::yOf(r, rg, val);
        g.drawText(QRectF(r.left() - 52, y - 8, 47, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QString::number(std::fabs(val) < yStep * 1e-6 ? 0.0 : val, 'f', yDec));
    }
    // 轴标题（竖排）
    g.save();
    g.translate(r.left() - 58, r.center().y());
    g.rotate(-90);
    g.setFont(fontPx(g, 11, true));
    g.drawText(QRectF(-r.height() / 2, -8, r.height(), 16), Qt::AlignCenter, title);
    g.restore();
    g.setPen(QPen(kColFrame, 1.0));
    g.setBrush(Qt::NoBrush);
    g.drawRect(r);
}

void drawRightAxis(QPainter &g, const QRectF &r, const Range &rg, const QString &title, const QColor &color)
{
    g.setFont(fontPx(g, 10));
    g.setPen(color);
    const double yStep = niceStep(rg.hi - rg.lo, std::max(2.0, r.height() / 34.0));
    for (double val = std::ceil(rg.lo / yStep - 1e-9) * yStep; val <= rg.hi + yStep * 1e-6; val += yStep) {
        const double y = Mapper::yOf(r, rg, val);
        g.drawText(QRectF(r.right() + 5, y - 8, 50, 16), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(val, 'f', decimalsFor(yStep)));
    }
    g.save();
    g.translate(r.right() + 60, r.center().y());
    g.rotate(90);
    g.setFont(fontPx(g, 11, true));
    g.drawText(QRectF(-r.height() / 2, -8, r.height(), 16), Qt::AlignCenter, title);
    g.restore();
}

// 面板左上角的图例：{颜色, 文字} 依次排开
void drawLegend(QPainter &g, const QRectF &r, const QVector<QPair<QColor, QString>> &items)
{
    g.setFont(fontPx(g, 11));
    const QFontMetricsF fm(g.font());
    double x = r.left() + 8;
    const double y = r.top() + 11;
    for (const auto &it : items) {
        const double w = fm.horizontalAdvance(it.second);
        g.fillRect(QRectF(x - 3, y - 8, w + 28, 16), QColor(255, 255, 255, 215));
        g.setPen(QPen(it.first, 2.0));
        g.drawLine(QPointF(x, y), QPointF(x + 16, y));
        g.setPen(kColText);
        g.drawText(QPointF(x + 21, y + 4), it.second);
        x += w + 36;
    }
}

double drawBadge(QPainter &g, double x, double y, const QString &text, const QColor &bg, const QColor &fg)
{
    g.setFont(fontPx(g, 13, true));
    const QFontMetricsF fm(g.font());
    const double w = fm.horizontalAdvance(text) + 16;
    g.setPen(Qt::NoPen);
    g.setBrush(bg);
    g.drawRoundedRect(QRectF(x, y, w, 22), 4, 4);
    g.setPen(fg);
    g.drawText(QRectF(x, y, w, 22), Qt::AlignCenter, text);
    return w;
}

void drawTitle(QPainter &g, const QRectF &rect, const Piece &p, const HeaderInfo *hdr)
{
    const double x = rect.left() + 10;
    g.setPen(kColText);
    g.setFont(fontPx(g, 15, true));
    const QString seq = p.seq >= 0 ? QString("#%1").arg(p.seq) : QStringLiteral("(序号未知)");
    QString line1 = QStringLiteral("发料片 %1    CH1 上升沿 %2  →  %3 %4    历时 %5 s    TICK %6 行")
                        .arg(seq, fmtClock(p.startMs, true),
                             p.complete ? QStringLiteral("缝纫结束") : QStringLiteral("记录终止"),
                             fmtClock(p.endMs))
                        .arg((double)(p.endMs - p.startMs) / 1000.0, 0, 'f', 2)
                        .arg(p.ticks.size());
    g.drawText(QRectF(x, rect.top() + 4, rect.width() - 20, 22), Qt::AlignLeft | Qt::AlignVCenter, line1);

    double bx = x;
    const double by = rect.top() + 30;
    bx += 8 + drawBadge(g, bx, by,
                        p.lenMm >= 0 ? QStringLiteral("发料长度 %1 mm").arg(p.lenMm, 0, 'f', 1)
                                     : QStringLiteral("发料长度 未记录(无 PIECE_DONE)"),
                        QColor("#d0ebff"), QColor("#0b4f8a"));
    bx += 12 + drawBadge(g, bx, by,
                         p.curtainMm >= 0 ? QStringLiteral("缝纫后发帘长度 %1 mm").arg(p.curtainMm, 0, 'f', 1)
                                          : QStringLiteral("缝纫后发帘长度 未记录"),
                         p.curtainMm >= 0 ? QColor("#d3f9d8") : QColor("#ffe3e3"),
                         p.curtainMm >= 0 ? QColor("#1b6e2d") : QColor("#a61e1e"));

    int alarmCount = 0;
    for (const LogEvent &e : p.events)
        if (e.kind == EvAlarm) ++alarmCount;
    if (alarmCount > 0)
        bx += 12 + drawBadge(g, bx, by, QStringLiteral("⚠ 报警 %1 条").arg(alarmCount), QColor("#e03131"),
                             QColor(Qt::white));

    QStringList info;
    if (p.curtainMm < 0 && !p.endReason.isEmpty()) info << QStringLiteral("(%1)").arg(p.endReason);
    if (p.avgThk >= 0)
        info << QStringLiteral("厚度 min/max/avg = %1 / %2 / %3 mm")
                    .arg(p.minThk, 0, 'f', 3).arg(p.maxThk, 0, 'f', 3).arg(p.avgThk, 0, 'f', 3);
    if (p.baseMm >= 0)
        info << QStringLiteral("基线 %1 mm (n=%2%3)").arg(p.baseMm, 0, 'f', 2).arg(p.baseN)
                    .arg(p.baseReused ? QStringLiteral(", 复用") : QString());
    if (!p.xs.isEmpty())
        info << QStringLiteral("皮带行程 %1 脉冲%2").arg(p.xs.last(), 0, 'f', 0)
                    .arg(p.mmPerPulse > 0 ? QStringLiteral(" ≈ %1 mm").arg(p.xs.last() * p.mmPerPulse, 0, 'f', 1)
                                          : QString());
    info << QStringLiteral("WIN %1 个%2").arg(p.wins.size())
                .arg(hdr ? QStringLiteral(" (窗口宽 %1 mm)").arg(hdr->segLen, 0, 'f', 1) : QString());
    g.setFont(fontPx(g, 12));
    g.setPen(kColSubText);
    g.drawText(QRectF(bx, by, rect.right() - bx - 8, 22), Qt::AlignLeft | Qt::AlignVCenter,
               info.join(QStringLiteral("    ")));
}

void drawXAxis(QPainter &g, const Mapper &m, double xStep)
{
    const QRectF &r = m.gm.xaxis;
    const double k = m.p.mmPerPulse;
    g.setFont(fontPx(g, 10));
    for (double val = std::ceil(m.v.x0 / xStep - 1e-9) * xStep; val <= m.v.x1 + xStep * 1e-6; val += xStep) {
        const double x = m.xOfV(val);
        g.setPen(QPen(kColFrame, 1.0));
        g.drawLine(QPointF(x, r.top()), QPointF(x, r.top() + 4));
        g.setPen(kColText);
        g.drawText(QRectF(x - 50, r.top() + 5, 100, 13), Qt::AlignCenter,
                   QString::number(std::fabs(val) < xStep * 1e-6 ? 0.0 : val, 'f', 0));
        if (k > 0) {
            g.setPen(kColSubText);
            g.drawText(QRectF(x - 50, r.top() + 18, 100, 13), Qt::AlignCenter, QString::number(val * k, 'f', 1));
        }
    }
    g.setFont(fontPx(g, 10, true));
    g.setPen(kColText);
    g.drawText(QRectF(r.left() - 66, r.top() + 5, 61, 13), Qt::AlignRight | Qt::AlignVCenter, QStringLiteral("Δ脉冲"));
    if (k > 0) {
        g.setPen(kColSubText);
        g.drawText(QRectF(r.left() - 66, r.top() + 18, 61, 13), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("≈距离mm"));
    }

    // 横轴是距离，时间只在两端标注：当前可见范围第一个/最后一个 tick 的时刻
    const QVector<double> &xs = m.p.xs;
    if (xs.isEmpty()) return;
    int iL = int(std::lower_bound(xs.begin(), xs.end(), m.v.x0) - xs.begin());
    int iR = int(std::upper_bound(xs.begin(), xs.end(), m.v.x1) - xs.begin()) - 1;
    iL = std::min(iL, (int)xs.size() - 1);
    iR = std::max(iR, 0);
    const Tick &tl = m.p.ticks[iL];
    const Tick &tr = m.p.ticks[iR];
    g.setFont(fontPx(g, 11, true));
    g.setPen(QColor("#0b4f8a"));
    g.drawText(QRectF(r.left(), r.top() + 34, r.width() / 2, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("◀ %1 %2 (+%3 s)")
                   .arg(iL == 0 ? QStringLiteral("开始(CH1 上升沿)") : QStringLiteral("视图起点"))
                   .arg(fmtClock(tl.ms)).arg(m.tOf(tl.ms), 0, 'f', 2));
    g.drawText(QRectF(r.center().x(), r.top() + 34, r.width() / 2, 16), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 %2 (+%3 s) ▶")
                   .arg(iR == xs.size() - 1 ? QStringLiteral("结束") : QStringLiteral("视图终点"))
                   .arg(fmtClock(tr.ms)).arg(m.tOf(tr.ms), 0, 'f', 2));
    if (k > 0) {
        g.setFont(fontPx(g, 10));
        g.setPen(kColSubText);
        g.drawText(QRectF(r.left(), r.top() + 34, r.width(), 16), Qt::AlignCenter,
                   QStringLiteral("横轴 = 皮带脉冲增量(物料前进距离)；mm 按 %1 mm/脉冲 估算(由 posLaser 反推)")
                       .arg(k, 0, 'f', 5));
    }
}

void drawEvents(QPainter &g, const Mapper &m)
{
    const QRectF &band = m.gm.events;
    g.setFont(fontPx(g, 10));
    const QFontMetricsF fm(g.font());
    double rowRight[kEventRows];
    for (double &x : rowRight) x = -1e9;

    for (const LogEvent &e : m.p.events) {
        const double x = m.xOfMs(e.ms);
        if (x < m.gm.plotLeft - 0.5 || x > m.gm.plotRight + 0.5) continue;
        QColor c = eventColor(e.kind);
        QColor lc = c;
        lc.setAlpha(e.kind == EvAlarm ? 220 : 110);
        g.setPen(QPen(lc, e.kind == EvAlarm ? 1.4 : 1.0, Qt::DashLine));
        g.drawLine(QPointF(x, band.bottom()), QPointF(x, m.gm.bottom));

        const double w = fm.horizontalAdvance(e.label);
        for (int k = 0; k < kEventRows; ++k) {
            if (rowRight[k] + 6 > x) continue;
            const double y = band.top() + k * kEventRowH;
            g.setPen(QPen(lc, 1.0));
            g.drawLine(QPointF(x, y + 2), QPointF(x, band.bottom()));
            g.setPen(c);
            g.drawText(QPointF(x + 2.5, y + 10.5), e.label);
            rowRight[k] = x + w + 3;
            break;
        }
    }
}

// 落在本片 TICK 记录区间内的 ALARM：贯穿全部面板的红色实线 + 厚度曲线上的红色标记点 +
// 类型/时间/消息标签。不受“事件线”开关影响，始终显示
void drawAlarms(QPainter &g, const Mapper &m, const Range &thickRange)
{
    const QRectF &r = m.gm.panel[PieceChart::PanelThick];
    const QColor red("#e03131");
    double lastRight = -1e9;
    int slot = 0;
    for (const LogEvent &e : m.p.events) {
        if (e.kind != EvAlarm) continue;
        const double val = m.vAtMs(e.ms);
        const double x = m.xOfV(val);
        if (x < m.gm.plotLeft - 0.5 || x > m.gm.plotRight + 0.5) continue;

        g.setPen(QPen(red, 1.8));
        g.drawLine(QPointF(x, m.gm.top), QPointF(x, m.gm.bottom));

        // 曲线上的标记点：报警时刻最近一个 tick 的厚度
        const int ti = m.nearestTick(val);
        if (ti >= 0) {
            double y = Mapper::yOf(r, thickRange, fltThickAt(m.p, m.p.ticks[ti]));
            y = std::min(r.bottom() - 5, std::max(r.top() + 5, y));
            g.setPen(QPen(Qt::white, 1.5));
            g.setBrush(red);
            g.drawPolygon(QPolygonF() << QPointF(x, y - 8) << QPointF(x + 7, y + 5) << QPointF(x - 7, y + 5));
        }

        const int msgAt = e.detail.indexOf(QStringLiteral(" msg="));
        const QString line1 = QStringLiteral("%1   %2").arg(e.label, fmtClock(e.ms));
        const QString line2 = msgAt >= 0 ? e.detail.mid(msgAt + 5).left(70) : QString();
        g.setFont(fontPx(g, 11, true));
        const double w1 = QFontMetricsF(g.font()).horizontalAdvance(line1);
        g.setFont(fontPx(g, 10));
        const double w2 = QFontMetricsF(g.font()).horizontalAdvance(line2);
        const double bw = std::max(w1, w2) + 14;
        const double bh = line2.isEmpty() ? 20 : 34;
        // 相邻报警的标签会重叠时往下错一行
        slot = (x < lastRight + 6) ? (slot + 1) % 4 : 0;
        QRectF box(x + 6, r.top() + 26 + slot * (bh + 3), bw, bh);
        if (box.right() > m.gm.plotRight - 2) box.moveRight(x - 6);
        lastRight = std::max(lastRight, box.right());
        g.setPen(QPen(red, 1.0));
        g.setBrush(QColor(255, 236, 236, 242));
        g.drawRoundedRect(box, 3, 3);
        g.setPen(QColor("#a61e1e"));
        g.setFont(fontPx(g, 11, true));
        g.drawText(QPointF(box.left() + 7, box.top() + 14), line1);
        if (!line2.isEmpty()) {
            g.setFont(fontPx(g, 10));
            g.drawText(QPointF(box.left() + 7, box.top() + 28), line2);
        }
    }
}

int hoveredWinIndex(const Mapper &m, const QPointF &pos)
{
    const QVector<WinRec> &wins = m.p.wins;
    if (wins.isEmpty() || !m.v.showWin) return -1;
    if (m.gm.gantt.contains(pos)) {
        const int lane = (int)((pos.y() - m.gm.gantt.top() - 3) / kLaneH);
        for (int i = 0; i < wins.size(); ++i) {
            if (wins[i].lane != lane) continue;
            if (pos.x() >= m.xOfMs(wins[i].startMs) && pos.x() <= m.xOfMs(wins[i].ms)) return i;
        }
        return -1;
    }
    if (!m.gm.panel[PieceChart::PanelThick].contains(pos)) return -1;
    int best = -1;
    double bestD = 9.0;
    for (int i = 0; i < wins.size(); ++i) {
        const double d = std::fabs(m.xOfMs(wins[i].ms) - pos.x());
        if (d < bestD) { bestD = d; best = i; }
    }
    return best;
}

void drawWindows(QPainter &g, const Mapper &m, const Range &thickRange, int hi)
{
    const QVector<WinRec> &wins = m.p.wins;
    const QRectF &r = m.gm.panel[PieceChart::PanelThick];

    // ── 厚度面板：窗口均值标在窗口末端时刻（即 WIN 行的时间戳），连成折线 ──
    g.save();
    g.setClipRect(r);
    if (hi >= 0) {
        const WinRec &w = wins[hi];
        const double xa = m.xOfMs(w.startMs), xb = m.xOfMs(w.ms);
        const double y = Mapper::yOf(r, thickRange, w.mean);
        g.fillRect(QRectF(xa, r.top(), xb - xa, r.height()), QColor(232, 89, 12, 34));
        g.setPen(QPen(kColWin, 2.4));
        g.drawLine(QPointF(xa, y), QPointF(xb, y));
    }
    QPolygonF poly;
    for (const WinRec &w : wins) poly << QPointF(m.xOfMs(w.ms), Mapper::yOf(r, thickRange, w.mean));
    g.setPen(QPen(kColWin, 1.3));
    g.setBrush(Qt::NoBrush);
    g.drawPolyline(poly);
    g.setFont(fontPx(g, 10));
    const QFontMetricsF fm(g.font());
    double lastLabelRight = -1e9;
    for (int i = 0; i < poly.size(); ++i) {
        const QPointF &pt = poly[i];
        if (pt.x() < r.left() - 40 || pt.x() > r.right() + 40) continue;
        g.setPen(QPen(kColWin, 1.0));
        g.setBrush(i == hi ? kColWin : QColor(Qt::white));
        g.drawEllipse(pt, i == hi ? 4.0 : 2.6, i == hi ? 4.0 : 2.6);
        // 均值数字：放不下就跳过，放大后会全部显示出来
        const QString txt = QString::number(wins[i].mean, 'f', 2);
        const double tw = fm.horizontalAdvance(txt);
        if (pt.x() - tw / 2 > lastLabelRight + 5) {
            g.setPen(QColor("#a13d04"));
            g.drawText(QPointF(pt.x() - tw / 2, pt.y() - 6), txt);
            lastLabelRight = pt.x() + tw / 2;
        }
    }
    g.restore();

    // ── 窗口泳道：每个窗口一条横条(起点→末端)，交叠的窗口错开到不同行，颜色=查表行 ──
    const QRectF &gr = m.gm.gantt;
    g.fillRect(gr, QColor("#fff9f2"));
    g.save();
    g.setClipRect(gr);
    g.setFont(fontPx(g, 9));
    const QFontMetricsF fm2(g.font());
    for (int i = 0; i < wins.size(); ++i) {
        const WinRec &w = wins[i];
        const double xa = m.xOfMs(w.startMs), xb = std::max(m.xOfMs(w.ms), xa + 1.5);
        if (xb < gr.left() || xa > gr.right()) continue;
        const QRectF bar(xa, gr.top() + 3 + w.lane * kLaneH, xb - xa, kLaneH - 2);
        QColor fill = PieceChart::rowColor(w.row);
        fill.setAlpha(i == hi ? 255 : 150);
        g.setPen(i == hi ? QPen(kColText, 1.4) : QPen(PieceChart::rowColor(w.row).darker(130), 0.6));
        g.setBrush(fill);
        g.drawRect(bar);
        const QString txt = QString("#%1  %2").arg(w.win).arg(w.mean, 0, 'f', 3);
        const QRectF vis = bar.intersected(gr);
        if (fm2.horizontalAdvance(txt) + 6 <= vis.width()) {
            g.setPen(kColText);
            g.drawText(vis, Qt::AlignCenter, txt);
        }
    }
    g.restore();
    g.setPen(QPen(kColFrame, 1.0));
    g.setBrush(Qt::NoBrush);
    g.drawRect(gr);
    g.setFont(fontPx(g, 10, true));
    g.setPen(kColWin);
    g.drawText(QRectF(gr.left() - 66, gr.top(), 61, gr.height()), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("WIN\n窗口范围"));

    if (hi >= 0) {
        const WinRec &w = wins[hi];
        const QString txt = QStringLiteral("WIN #%1   平均厚度 %2 mm   查表行 %3   窗口 Δ脉冲 %4 ~ %5 (%6 ~ %7)")
                                .arg(w.win).arg(w.mean, 0, 'f', 3).arg(w.row)
                                .arg(m.vAtMs(w.startMs), 0, 'f', 0).arg(m.vAtMs(w.ms), 0, 'f', 0)
                                .arg(fmtClock(w.startMs), fmtClock(w.ms));
        g.setFont(fontPx(g, 12, true));
        const QFontMetricsF fm3(g.font());
        const double tw = fm3.horizontalAdvance(txt) + 14;
        const QRectF box(r.right() - tw - 6, r.top() + 5, tw, 20);
        g.setPen(QPen(kColWin, 1.0));
        g.setBrush(QColor(255, 244, 230, 240));
        g.drawRoundedRect(box, 3, 3);
        g.setPen(QColor("#7a2e02"));
        g.drawText(box, Qt::AlignCenter, txt);
    }
}

void drawDigital(QPainter &g, const Mapper &m)
{
    const QRectF &r = m.gm.digital;
    struct Lane { const char *name; QColor color; int field; };
    const Lane lanes[4] = {{"ch1", QColor("#1971c2"), 0}, {"ch2", QColor("#2f9e44"), 1},
                           {"ch3", QColor("#e67700"), 2}, {"vfd", QColor("#c2255c"), 3}};
    double y = r.top();
    for (const Lane &ln : lanes) {
        const QRectF row(r.left(), y + 1, r.width(), kDigRowH - 3);
        g.fillRect(row, QColor("#f1f3f5"));
        const int field = ln.field;
        forRuns(m,
                [field](const Tick &t) { return (int)(field == 0 ? t.ch1 : field == 1 ? t.ch2 : field == 2 ? t.ch3 : t.vfd); },
                [&](double xa, double xb, int val) {
                    if (val) g.fillRect(QRectF(xa, row.top(), xb - xa, row.height()), ln.color);
                });
        g.setFont(fontPx(g, 10, true));
        g.setPen(ln.color);
        g.drawText(QRectF(r.left() - 66, row.top() - 2, 61, row.height() + 4), Qt::AlignRight | Qt::AlignVCenter,
                   QString::fromLatin1(ln.name));
        y += kDigRowH;
    }

    // 状态机状态带
    const QRectF sb(r.left(), y + 2, r.width(), kStateH - 3);
    g.fillRect(sb, QColor("#f1f3f5"));
    g.setFont(fontPx(g, 10));
    const QFontMetricsF fm(g.font());
    forRuns(m, [](const Tick &t) { return (int)t.state; }, [&](double xa, double xb, int val) {
        const QRectF seg(xa, sb.top(), xb - xa, sb.height());
        g.fillRect(seg, stateColor(val));
        g.setPen(QPen(QColor(255, 255, 255), 1.0));
        g.drawLine(seg.topLeft(), seg.bottomLeft());
        const QString name = QString::fromLatin1(sewStateName(val));
        if (fm.horizontalAdvance(name) + 6 <= seg.width()) {
            g.setPen(kColText);
            g.drawText(seg, Qt::AlignCenter, name);
        }
    });
    g.setFont(fontPx(g, 10, true));
    g.setPen(kColText);
    g.drawText(QRectF(r.left() - 66, sb.top() - 2, 61, sb.height() + 4), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("state"));
    y += kStateH;

    // 当前生效的查表行
    const QRectF rb(r.left(), y + 2, r.width(), kRowBandH - 3);
    g.fillRect(rb, QColor("#f1f3f5"));
    g.setFont(fontPx(g, 10));
    forRuns(m, [](const Tick &t) { return (int)t.row; }, [&](double xa, double xb, int val) {
        const QRectF seg(xa, rb.top(), xb - xa, rb.height());
        QColor c = PieceChart::rowColor(val);
        c.setAlpha(170);
        g.fillRect(seg, c);
        g.setPen(QPen(QColor(255, 255, 255), 1.0));
        g.drawLine(seg.topLeft(), seg.bottomLeft());
        const QString name = QStringLiteral("行%1").arg(val);
        if (fm.horizontalAdvance(name) + 6 <= seg.width()) {
            g.setPen(kColText);
            g.drawText(seg, Qt::AlignCenter, name);
        }
    });
    g.setFont(fontPx(g, 10, true));
    g.setPen(kColText);
    g.drawText(QRectF(r.left() - 66, rb.top() - 2, 61, rb.height() + 4), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("row"));
}

void drawHover(QPainter &g, const Mapper &m, const QRectF &rect, const QPointF &pos)
{
    if (pos.x() < m.gm.plotLeft || pos.x() > m.gm.plotRight || pos.y() < m.gm.events.top() || pos.y() > m.gm.bottom)
        return;
    const int ti = m.nearestTick(m.vOfX(pos.x()));
    if (ti < 0) return;
    const Tick &t = m.p.ticks[ti];
    const double x = m.xOfV(m.p.xs[ti]);
    if (x < m.gm.plotLeft - 1 || x > m.gm.plotRight + 1) return;

    g.setPen(QPen(QColor(33, 37, 41, 170), 1.0));
    g.drawLine(QPointF(x, m.gm.top), QPointF(x, m.gm.bottom));

    QStringList lines;
    lines << QStringLiteral("Δ脉冲 %1%2   +%3 s   %4").arg(m.p.xs[ti], 0, 'f', 0)
                 .arg(m.p.mmPerPulse > 0 ? QStringLiteral(" ≈ %1 mm").arg(m.p.xs[ti] * m.p.mmPerPulse, 0, 'f', 1)
                                         : QString())
                 .arg(m.tOf(t.ms), 0, 'f', 3).arg(fmtClock(t.ms));
    const double rawThk = rawThickAt(m.p, t);
    lines << QStringLiteral("thick(滤波后) = %1 mm%2").arg(fltThickAt(m.p, t), 0, 'f', 3)
                 .arg(std::isnan(rawThk) ? QString() : QStringLiteral("   原始 = %1 mm").arg(rawThk, 0, 'f', 3));
    lines << QString("lflt = %1   lraw = %2 mm").arg(t.lflt, 0, 'f', 2).arg(t.lraw, 0, 'f', 2);
    lines << QString("posLaser = %1 mm").arg(t.posLaser, 0, 'f', 1);
    lines << QString("freq = %1 Hz   row = %2   state = %3").arg(t.freq, 0, 'f', 1).arg(t.row)
                 .arg(QString::fromLatin1(sewStateName(t.state)));
    lines << QString("ch1/ch2/ch3 = %1/%2/%3   vfd = %4").arg(t.ch1).arg(t.ch2).arg(t.ch3).arg(t.vfd);
    lines << QString("pulse = %1").arg(t.pulse);
    // 光标附近的事件：给出原始日志内容
    for (const LogEvent &e : m.p.events) {
        if (std::fabs(m.xOfMs(e.ms) - pos.x()) <= 4.0 && lines.size() < 12)
            lines << QString("%1  %2").arg(fmtClock(e.ms), e.detail.left(90));
    }

    g.setFont(fontPx(g, 11));
    const QFontMetricsF fm(g.font());
    double w = 0;
    for (const QString &s : lines) w = std::max(w, fm.horizontalAdvance(s));
    const double lh = 15.0;
    QRectF box(pos.x() + 14, pos.y() + 14, w + 16, lines.size() * lh + 10);
    if (box.right() > rect.right() - 4) box.moveRight(pos.x() - 14);
    if (box.bottom() > rect.bottom() - 4) box.moveBottom(rect.bottom() - 4);
    if (box.left() < rect.left() + 4) box.moveLeft(rect.left() + 4);
    g.setPen(QPen(kColFrame, 1.0));
    g.setBrush(QColor(255, 255, 255, 238));
    g.drawRoundedRect(box, 4, 4);
    for (int i = 0; i < lines.size(); ++i) {
        g.setPen(i == 0 ? kColText : (i >= 7 ? QColor("#1864ab") : kColSubText));
        g.setFont(fontPx(g, 11, i == 0));
        g.drawText(QPointF(box.left() + 8, box.top() + 16 + i * lh), lines[i]);
    }
}

} // namespace

QColor PieceChart::rowColor(int row)
{
    static const char *const c[] = {"#adb5bd", "#4c6ef5", "#228be6", "#15aabf", "#12b886", "#40c057",
                                    "#82c91e", "#fab005", "#fd7e14", "#fa5252", "#be4bdb"};
    const int n = (int)(sizeof(c) / sizeof(c[0]));
    return QColor(c[((row % n) + n) % n]);
}

double PieceChart::minXSpan(const Piece &p)
{
    return std::max(10.0, (p.xs.isEmpty() ? 1.0 : p.xs.last()) / 5000.0);
}

void PieceChart::fullXRange(const Piece &p, double &x0, double &x1)
{
    const double total = p.xs.isEmpty() ? 1.0 : std::max(1.0, p.xs.last());
    x0 = -total * 0.01;
    x1 = total * 1.01;
}

void PieceChart::resetView(const Piece &p, View &v)
{
    fullXRange(p, v.x0, v.x1);
    for (auto &r : v.y) { r[0] = 0.0; r[1] = 1.0; }
}

PieceChart::Geom PieceChart::geometry(const QRectF &rect, const Piece &p, const View &v)
{
    Geom gm;
    gm.plotLeft  = rect.left() + kMarginL;
    gm.plotRight = std::max(gm.plotLeft + 50.0, rect.right() - kMarginR);
    const double w = gm.plotRight - gm.plotLeft;

    double y = rect.top() + kTitleH;
    const double evH = v.showEvents ? kEventRows * kEventRowH + 2 : 4;
    gm.events = QRectF(gm.plotLeft, y, w, evH);
    y += evH;

    const double ganttH = (v.showWin && !p.wins.isEmpty()) ? p.winLanes * kLaneH + 6 : 0.0;
    const double digitalH = 4 * kDigRowH + kStateH + kRowBandH;
    double weightSum = 0;
    for (double wt : kWeights) weightSum += wt;
    const double avail = std::max(160.0, rect.bottom() - kXAxisH - digitalH - y - ganttH - kGap * PanelCount - 4);

    for (int i = 0; i < PanelCount; ++i) {
        const double h = avail * kWeights[i] / weightSum;
        gm.panel[i] = QRectF(gm.plotLeft, y, w, h);
        y += h;
        if (i == PanelThick) {
            gm.gantt = QRectF(gm.plotLeft, y + 2, w, ganttH > 0 ? ganttH - 2 : 0);
            y += ganttH;
        }
        y += kGap;
    }
    gm.digital = QRectF(gm.plotLeft, y, w, digitalH);
    y += digitalH;
    gm.xaxis = QRectF(gm.plotLeft, y + 2, w, kXAxisH - 2);
    gm.top = gm.events.bottom();
    gm.bottom = y;
    return gm;
}

void PieceChart::render(QPainter &g, const QRectF &rect, const Piece &p, const HeaderInfo *hdr, const View &v,
                        const QPointF *hover)
{
    g.save();
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setRenderHint(QPainter::TextAntialiasing, true);
    g.fillRect(rect, Qt::white);

    const Geom gm = geometry(rect, p, v);
    const Ranges rg = computeRanges(p);
    const Mapper m{p, v, gm};

    drawTitle(g, rect, p, hdr);

    const double xStep = niceStep(v.x1 - v.x0, std::max(2.0, (gm.plotRight - gm.plotLeft) / 110.0));
    Range r[PanelCount];
    for (int i = 0; i < PanelCount; ++i) r[i] = applyView(rg.left[i], v.y[i]);
    const Range timeR = applyView(rg.time, v.y[PanelPos]);

    // ── 厚度：原始(base−lraw) 与 滤波后(thick) 两条 ──
    drawPanelFrame(g, m, gm.panel[PanelThick], r[PanelThick], QStringLiteral("厚度 mm"), kColThick, xStep);
    const bool hasRawThick = p.baseMm >= 0;
    if (v.showRaw && hasRawThick) {
        drawSeries(g, m, gm.panel[PanelThick], r[PanelThick], QPen(kColRaw, 1.0),
                   [&p](const Tick &t) { return rawThickAt(p, t); });
    }
    if (v.showFlt)
        drawSeries(g, m, gm.panel[PanelThick], r[PanelThick], QPen(kColThick, 1.4),
                   [&p](const Tick &t) { return fltThickAt(p, t); });
    for (const LogEvent &e : p.events)
        if (e.kind == EvPieceDone) drawPieceDone(g, gm, p, m.xOfMs(e.ms));

    // ── 激光读数 ──
    drawPanelFrame(g, m, gm.panel[PanelLaser], r[PanelLaser], QStringLiteral("激光 mm"), kColFlt, xStep);
    if (v.showRaw)
        drawSeries(g, m, gm.panel[PanelLaser], r[PanelLaser], QPen(kColRaw, 1.0),
                   [](const Tick &t) { return (double)t.lraw; });
    if (v.showFlt)
        drawSeries(g, m, gm.panel[PanelLaser], r[PanelLaser], QPen(kColFlt, 1.3),
                   [](const Tick &t) { return (double)t.lflt; });

    // ── 变频器频率 ──
    drawPanelFrame(g, m, gm.panel[PanelFreq], r[PanelFreq], QStringLiteral("频率 Hz"), kColFreq, xStep);
    drawSeries(g, m, gm.panel[PanelFreq], r[PanelFreq], QPen(kColFreq, 1.3),
               [](const Tick &t) { return (double)t.freq; });

    // ── 片内位移 + 时间(右轴)：时间曲线越陡 = 皮带越慢 ──
    drawPanelFrame(g, m, gm.panel[PanelPos], r[PanelPos], QStringLiteral("posLaser mm"), kColPos, xStep);
    drawRightAxis(g, gm.panel[PanelPos], timeR, QStringLiteral("时间 s"), kColTime);
    const qint64 ms0 = p.startMs;
    drawSeries(g, m, gm.panel[PanelPos], timeR, QPen(kColTime, 1.0),
               [ms0](const Tick &t) { return (double)(t.ms - ms0) / 1000.0; });
    drawSeries(g, m, gm.panel[PanelPos], r[PanelPos], QPen(kColPos, 1.3), [](const Tick &t) {
        return t.posLaser < 0.0f ? std::numeric_limits<double>::quiet_NaN() : (double)t.posLaser;
    });

    drawDigital(g, m);

    const int hiWin = hover ? hoveredWinIndex(m, *hover) : -1;
    if (v.showWin && !p.wins.isEmpty()) drawWindows(g, m, r[PanelThick], hiWin);
    if (v.showEvents) drawEvents(g, m);
    drawAlarms(g, m, r[PanelThick]);

    QVector<QPair<QColor, QString>> lgThick;
    if (v.showFlt) lgThick.append(qMakePair(kColThick, QStringLiteral("滤波后厚度 thick (=base−lflt)")));
    if (v.showRaw && hasRawThick) lgThick.append(qMakePair(kColRaw, QStringLiteral("原始厚度 (=base−lraw)")));
    if (v.showWin && !p.wins.isEmpty())
        lgThick.append(qMakePair(kColWin, QStringLiteral("WIN 窗口平均厚度(点在窗口末端)")));
    drawLegend(g, gm.panel[PanelThick], lgThick);
    QVector<QPair<QColor, QString>> lgLaser;
    if (v.showFlt) lgLaser.append(qMakePair(kColFlt, QStringLiteral("lflt 滤波后")));
    if (v.showRaw) lgLaser.append(qMakePair(kColRaw, QStringLiteral("lraw 原始")));
    drawLegend(g, gm.panel[PanelLaser], lgLaser);
    drawLegend(g, gm.panel[PanelFreq], {qMakePair(kColFreq, QStringLiteral("freq 变频器当前频率"))});
    drawLegend(g, gm.panel[PanelPos], {qMakePair(kColPos, QStringLiteral("posLaser 料头过激光后的位移")),
                                       qMakePair(kColTime, QStringLiteral("经过时间(右轴)"))});

    drawXAxis(g, m, xStep);

    if (p.ticks.isEmpty()) {
        g.setFont(fontPx(g, 14));
        g.setPen(kColSubText);
        g.drawText(gm.panel[PanelThick], Qt::AlignCenter, QStringLiteral("本片没有 TICK 数据"));
    }

    if (hover) drawHover(g, m, rect, *hover);
    g.restore();
}
