#include "headerview.h"

#include <QFontMetricsF>
#include <QPolygonF>

#include <algorithm>

namespace {

const QColor kText("#212529");
const QColor kSub("#495057");
const QColor kLine("#868e96");
const QColor kDim("#1864ab");

QFont fontPx(const QPainter &g, int px, bool bold = false)
{
    QFont f = g.font();
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

void sectionTitle(QPainter &g, double x, double y, const QString &text)
{
    g.setFont(fontPx(g, 14, true));
    g.setPen(kText);
    g.fillRect(QRectF(x, y + 2, 4, 16), QColor("#1c7ed6"));
    g.drawText(QPointF(x + 10, y + 15), text);
}

// 尺寸线：两端带箭头和界线，数值写在线上方
void dimension(QPainter &g, double xa, double xb, double y, const QString &text, const QColor &color)
{
    g.setPen(QPen(color, 1.2));
    g.drawLine(QPointF(xa, y), QPointF(xb, y));
    g.drawLine(QPointF(xa, y - 7), QPointF(xa, y + 7));
    g.drawLine(QPointF(xb, y - 7), QPointF(xb, y + 7));
    g.setBrush(color);
    if (xb - xa > 16) {
        g.drawPolygon(QPolygonF() << QPointF(xa, y) << QPointF(xa + 7, y - 3.5) << QPointF(xa + 7, y + 3.5));
        g.drawPolygon(QPolygonF() << QPointF(xb, y) << QPointF(xb - 7, y - 3.5) << QPointF(xb - 7, y + 3.5));
    }
    g.setFont(fontPx(g, 13, true));
    const QFontMetricsF fm(g.font());
    const double tw = fm.horizontalAdvance(text);
    const double cx = (xa + xb) / 2;
    g.fillRect(QRectF(cx - tw / 2 - 3, y - 21, tw + 6, 16), Qt::white);
    g.setPen(color);
    g.drawText(QPointF(cx - tw / 2, y - 8), text);
}

double drawTable(QPainter &g, double x, double y, const QVector<double> &colW, const QStringList &head,
                 const QVector<QStringList> &rows, const QVector<QColor> &rowBg)
{
    const double rh = 25.0;
    double totalW = 0;
    for (double w : colW) totalW += w;

    g.fillRect(QRectF(x, y, totalW, rh), QColor("#dee2e6"));
    for (int r = 0; r < rows.size(); ++r) {
        const QColor bg = r < rowBg.size() && rowBg[r].isValid() ? rowBg[r]
                          : (r % 2 ? QColor("#f8f9fa") : QColor(Qt::white));
        g.fillRect(QRectF(x, y + rh * (r + 1), totalW, rh), bg);
    }
    for (int r = -1; r < rows.size(); ++r) {
        const QStringList &cells = r < 0 ? head : rows[r];
        g.setFont(fontPx(g, 12, r < 0));
        g.setPen(kText);
        double cx = x;
        for (int c = 0; c < colW.size(); ++c) {
            g.drawText(QRectF(cx + 4, y + rh * (r + 1), colW[c] - 8, rh), Qt::AlignCenter, cells.value(c));
            cx += colW[c];
        }
    }
    g.setPen(QPen(kLine, 1.0));
    g.setBrush(Qt::NoBrush);
    for (int r = 0; r <= rows.size() + 1; ++r)
        g.drawLine(QPointF(x, y + rh * r), QPointF(x + totalW, y + rh * r));
    double cx = x;
    for (int c = 0; c <= colW.size(); ++c) {
        g.drawLine(QPointF(cx, y), QPointF(cx, y + rh * (rows.size() + 1)));
        if (c < colW.size()) cx += colW[c];
    }
    return y + rh * (rows.size() + 1);
}

} // namespace

double renderHeader(QPainter &g, const QRectF &rect, const HeaderInfo &h, const QString &title)
{
    g.save();
    g.setRenderHint(QPainter::Antialiasing, true);
    g.setRenderHint(QPainter::TextAntialiasing, true);
    g.fillRect(rect, Qt::white);

    const double L = rect.left() + 24;
    const double R = rect.right() - 24;
    double y = rect.top() + 12;

    g.setFont(fontPx(g, 17, true));
    g.setPen(kText);
    g.drawText(QPointF(L, y + 18), title);
    g.setFont(fontPx(g, 12));
    g.setPen(kSub);
    g.drawText(QPointF(L, y + 38),
               QStringLiteral("HEADER 行：武装(开始自动缝纫)时写入 · %1 · 日志第 %2 行%3")
                   .arg(fmtClock(h.ms, true)).arg(h.lineNo)
                   .arg(h.changed ? QStringLiteral(" · 参数相对上一次武装有变化") : QString()));
    y += 54;

    // ───────── 1. 传感器物理距离示意图 ─────────
    sectionTitle(g, L, y, QStringLiteral("传感器物理距离 (mm，沿送料方向)"));
    y += 28;

    const QString names[5] = {QStringLiteral("CH3"), QStringLiteral("CH1"), QStringLiteral("激光测厚"),
                              QStringLiteral("CH2"), QStringLiteral("针脚")};
    const QColor colors[5] = {QColor("#e67700"), QColor("#1971c2"), QColor("#e03131"), QColor("#2f9e44"),
                              QColor("#5f3dc4")};
    const double d[4] = {h.ch3ToCh1, h.ch1ToLaser, h.laserToCh2, h.ch2ToNeedle};
    const double total = d[0] + d[1] + d[2] + d[3];

    const double x0 = L + 50, x1 = R - 50;
    const double avail = x1 - x0;
    // 严格按比例；只有当某一段窄到放不下标注(<64px)时才给每段一个保底宽度
    const double minPx = 64.0;
    bool proportional = total > 0;
    for (double v : d)
        if (total > 0 && v / total * avail < minPx) proportional = false;
    double px[5];
    px[0] = x0;
    for (int i = 0; i < 4; ++i) {
        const double w = proportional ? d[i] / total * avail
                                      : minPx + (avail - 4 * minPx) * (total > 0 ? d[i] / total : 0.25);
        px[i + 1] = px[i] + w;
    }

    const double beltY = y + 78;
    // 皮带
    g.setPen(Qt::NoPen);
    g.setBrush(QColor("#ced4da"));
    g.drawRoundedRect(QRectF(x0 - 36, beltY - 5, x1 - x0 + 72, 10), 5, 5);
    // 送料方向
    g.setPen(QPen(kSub, 1.4));
    g.setBrush(kSub);
    g.drawLine(QPointF(x0 - 36, y + 8), QPointF(x0 + 60, y + 8));
    g.drawPolygon(QPolygonF() << QPointF(x0 + 68, y + 8) << QPointF(x0 + 58, y + 3) << QPointF(x0 + 58, y + 13));
    g.setFont(fontPx(g, 12));
    g.drawText(QPointF(x0 + 76, y + 12), QStringLiteral("送料方向"));

    // 各传感器位置
    double cum = 0;
    for (int i = 0; i < 5; ++i) {
        g.setPen(QPen(colors[i], 2.2));
        g.drawLine(QPointF(px[i], beltY - 30), QPointF(px[i], beltY + 12));
        g.setBrush(colors[i]);
        g.drawEllipse(QPointF(px[i], beltY), 5, 5);
        g.setFont(fontPx(g, 13, true));
        const QFontMetricsF fm(g.font());
        const double tw = fm.horizontalAdvance(names[i]) + 14;
        const QRectF tag(px[i] - tw / 2, beltY - 54, tw, 22);
        g.setPen(Qt::NoPen);
        g.drawRoundedRect(tag, 4, 4);
        g.setPen(Qt::white);
        g.drawText(tag, Qt::AlignCenter, names[i]);
        g.setFont(fontPx(g, 10));
        g.setPen(kSub);
        g.drawText(QRectF(px[i] - 40, beltY - 70, 80, 14), Qt::AlignCenter,
                   QStringLiteral("位置 %1").arg(cum, 0, 'f', 1));
        if (i < 4) cum += d[i];
    }

    // 相邻距离
    const double dimY1 = beltY + 48;
    for (int i = 0; i < 4; ++i)
        dimension(g, px[i], px[i + 1], dimY1, QString("%1").arg(d[i], 0, 'f', 1), colors[i + 1].darker(110));
    // 组合距离
    const double dimY2 = dimY1 + 40;
    dimension(g, px[1], px[4], dimY2,
              QStringLiteral("CH1→针脚 %1").arg(d[1] + d[2] + d[3], 0, 'f', 1), kDim);
    const double dimY3 = dimY2 + 40;
    dimension(g, px[2], px[4], dimY3, QStringLiteral("激光→针脚 %1").arg(d[2] + d[3], 0, 'f', 1), kDim);
    const double dimY4 = dimY3 + 40;
    dimension(g, px[0], px[4], dimY4, QStringLiteral("CH3→针脚 总长 %1").arg(total, 0, 'f', 1), kText);

    g.setFont(fontPx(g, 11));
    g.setPen(kSub);
    g.drawText(QPointF(x0 - 36, dimY4 + 24),
               proportional ? QStringLiteral("※ 各段线长严格按实际距离等比例绘制")
                            : QStringLiteral("※ 个别距离过小，各段加了保底宽度，线长非严格比例，以标注数值为准"));
    y = dimY4 + 44;

    // ───────── 2. 距离对比条 + 速度表（左右并排） ─────────
    const double mid = L + (R - L) * 0.52;
    sectionTitle(g, L, y, QStringLiteral("距离大小对比"));
    sectionTitle(g, mid + 20, y, QStringLiteral("排发 / 送发电机驱动脉冲频率"));
    double yb = y + 30;
    const QString segNames[4] = {QStringLiteral("CH3 → CH1"), QStringLiteral("CH1 → 激光"),
                                 QStringLiteral("激光 → CH2"), QStringLiteral("CH2 → 针脚")};
    const double maxD = std::max(1.0, *std::max_element(d, d + 4));
    const double barX = L + 96, barW = mid - barX - 90;
    for (int i = 0; i < 4; ++i) {
        g.setFont(fontPx(g, 12));
        g.setPen(kText);
        g.drawText(QRectF(L, yb, 90, 20), Qt::AlignRight | Qt::AlignVCenter, segNames[i]);
        const double w = std::max(2.0, d[i] / maxD * barW);
        g.setPen(Qt::NoPen);
        g.setBrush(colors[i + 1]);
        g.drawRect(QRectF(barX, yb + 3, w, 14));
        g.setPen(kText);
        g.setFont(fontPx(g, 12, true));
        g.drawText(QPointF(barX + w + 6, yb + 15), QString("%1 mm").arg(d[i], 0, 'f', 1));
        yb += 24;
    }
    g.setFont(fontPx(g, 12));
    g.setPen(kSub);
    g.drawText(QPointF(L, yb + 16),
               QStringLiteral("其他：叠料距离 stackDist = %1 mm    测厚滑动窗口宽度 segLen = %2 mm")
                   .arg(h.stackDist, 0, 'f', 1).arg(h.segLen, 0, 'f', 1));
    yb += 28;

    QVector<QStringList> spRows;
    spRows << (QStringList() << QStringLiteral("排发 (paifa，皮带/脉冲计数电机)") << QString::number(h.paifaHi)
                             << QString::number(h.paifaLo));
    spRows << (QStringList() << QStringLiteral("送发 (songfa)") << QString::number(h.songfaHi)
                             << QString::number(h.songfaLo));
    const double spW = R - mid - 20;
    const double yt = drawTable(g, mid + 20, y + 30, {spW * 0.5, spW * 0.25, spW * 0.25},
                                QStringList() << QStringLiteral("电机") << QStringLiteral("高速 Hi (Hz)")
                                              << QStringLiteral("低速 Lo (Hz)"),
                                spRows, {});
    g.setFont(fontPx(g, 11));
    g.setPen(kSub);
    g.drawText(QRectF(mid + 20, yt + 6, spW, 34), Qt::AlignLeft | Qt::TextWordWrap,
               QStringLiteral("数值为步进驱动脉冲频率(脉冲/秒)。CH1 上升沿后切到低速，物料离开后恢复高速。"));
    y = std::max(yb, yt + 44) + 8;

    // ───────── 3. 查表行完整表 ─────────
    sectionTitle(g, L, y, QStringLiteral("厚度查表（全部 %1 行，V1~V5 为各阶段变频器频率 Hz）").arg(h.rows.size()));
    y += 28;
    QVector<QStringList> rows;
    QVector<QColor> bg;
    for (const LookupRow &r : h.rows) {
        QStringList cells;
        cells << (r.idx == 0 ? QStringLiteral("0 (默认行)") : QString::number(r.idx))
              << (r.enabled ? QStringLiteral("✔ 启用") : QStringLiteral("— 未启用"))
              << QString("%1 ~ %2").arg(r.tmin, 0, 'f', 2).arg(r.tmax, 0, 'f', 2);
        for (double v : r.v) cells << QString::number(v, 'f', 1);
        rows << cells;
        bg << (r.enabled ? QColor("#e6fcf5") : QColor());
    }
    const double tw = R - L;
    y = drawTable(g, L, y, {tw * 0.13, tw * 0.12, tw * 0.20, tw * 0.11, tw * 0.11, tw * 0.11, tw * 0.11, tw * 0.11},
                  QStringList() << QStringLiteral("行号") << QStringLiteral("状态") << QStringLiteral("厚度范围 (mm)")
                                << "V1" << "V2" << "V3" << "V4" << "V5",
                  rows, bg);
    y += 16;

    g.restore();
    return y;
}

HeaderView::HeaderView(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(980, 900);
}

void HeaderView::setHeader(const HeaderInfo *h, const QString &title)
{
    m_h = h;
    m_title = title;
    update();
}

void HeaderView::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.fillRect(rect(), Qt::white);
    if (m_h) renderHeader(g, QRectF(rect()), *m_h, m_title);
}
