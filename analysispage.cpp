#include "analysispage.h"

#include "profilewidget.h"

#include <QFontMetricsF>
#include <QHeaderView>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

const QColor kInk("#212529");
const QColor kInk2("#495057");
const QColor kMuted("#868e96");
const double kLabelW = 44.0;    // 行/列标签宽度
const double kTitleH = 22.0;
const double kBarH   = 34.0;    // 色条 + 刻度
const double kGap    = 48.0;    // 两个矩阵之间

// 单色顺序色阶（浅 → 深）：相关系数用蓝，均方根差用橙，两个矩阵含义不同、颜色也分开
const char *const kBlue[]   = {"#cde2fb", "#9ec5f4", "#6da7ec", "#3987e5", "#256abf", "#184f95", "#0d366b"};
const char *const kOrange[] = {"#fde4d7", "#f9c1a6", "#f49b75", "#eb6834", "#c9521f", "#a03f15", "#7a2f0e"};
const int kRampN = 7;

QColor rampColor(const char *const ramp[], double t)
{
    t = std::min(1.0, std::max(0.0, t));
    const double f = t * (kRampN - 1);
    const int i = std::min(kRampN - 2, (int)std::floor(f));
    const double u = f - i;
    const QColor a(ramp[i]), b(ramp[i + 1]);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * u, a.greenF() + (b.greenF() - a.greenF()) * u,
                            a.blueF() + (b.blueF() - a.blueF()) * u);
}

// 深色格子上用白字
QColor inkOn(const QColor &bg)
{
    const double l = 0.2126 * bg.redF() + 0.7152 * bg.greenF() + 0.0722 * bg.blueF();
    return l < 0.45 ? QColor(Qt::white) : kInk;
}

QFont fontPx(const QPainter &g, int px, bool bold = false)
{
    QFont f = g.font();
    f.setPixelSize(px);
    f.setBold(bold);
    return f;
}

double rmsScaleMax(const ProfileAnalysis &an)
{
    double mx = 0.0;
    const int n = an.vis.size();
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            if (a != b) mx = std::max(mx, an.pairRms[a][b]);
    return std::max(0.02, std::ceil(mx * 100.0) / 100.0);   // 向上取到 0.01mm
}

// 相关系数色阶下限：取矩阵最小值向下取到 0.1，最高不超过 0.5（色阶 = 下限..1，色条上标出）
double rScaleMin(const ProfileAnalysis &an)
{
    double mn = 1.0;
    const int n = an.vis.size();
    for (int a = 0; a < n; ++a)
        for (int b = 0; b < n; ++b)
            if (a != b) mn = std::min(mn, an.pairR[a][b]);
    return std::min(0.5, std::max(0.0, std::floor(mn * 10.0) / 10.0));
}

QPixmap swatch(int idx)
{
    QPixmap pm(22, 12);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(ProfileChart::seriesColor(idx), 2.4, ProfileChart::seriesStyle(idx), Qt::RoundCap));
    p.drawLine(QPointF(2, 6), QPointF(20, 6));
    return pm;
}

} // namespace

// ─────────────────────────── 矩阵 ───────────────────────────

PairMatrixView::PairMatrixView(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(260);
}

void PairMatrixView::setAnalysis(const ProfileAnalysis *an)
{
    m_an = an;
    m_hoverWhich = m_hoverA = m_hoverB = -1;
    updateGeometry();
    update();
}

QSize PairMatrixView::sizeHint() const
{
    const int n = m_an ? m_an->vis.size() : 0;
    return QSize(900, int(kTitleH + kLabelW * 0.5 + std::min(34, std::max(18, 520 / std::max(1, n))) * n + kBarH + 36));
}

PairMatrixView::Layout PairMatrixView::layout() const
{
    Layout L;
    const int n = m_an ? m_an->vis.size() : 0;
    if (n == 0) return L;
    const double availW = (width() - 16 - kGap) / 2.0 - kLabelW;
    const double availH = height() - kTitleH - 18 - kBarH - 8;
    L.cell = std::max(8.0, std::min(40.0, std::min(availW, availH) / n));
    const double side = L.cell * n;
    const double top = kTitleH + 18;
    L.mat[0] = QRectF(8 + kLabelW, top, side, side);
    L.mat[1] = QRectF(L.mat[0].right() + kGap + kLabelW, top, side, side);
    return L;
}

bool PairMatrixView::cellAt(const QPointF &p, int &which, int &a, int &b) const
{
    const Layout L = layout();
    if (!m_an || L.cell <= 0) return false;
    for (int w = 0; w < 2; ++w) {
        if (!L.mat[w].contains(p)) continue;
        which = w;
        const int last = int(m_an->vis.size()) - 1;   // Qt 6 的 size() 是 qsizetype，与 int 混用 std::min 无法推导
        b = std::min(last, int((p.x() - L.mat[w].left()) / L.cell));
        a = std::min(last, int((p.y() - L.mat[w].top()) / L.cell));
        return true;
    }
    return false;
}

void PairMatrixView::paintEvent(QPaintEvent *)
{
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing, true);
    g.fillRect(rect(), Qt::white);
    if (!m_an || m_an->vis.size() < 2) {
        g.setPen(kMuted);
        g.setFont(fontPx(g, 13));
        g.drawText(rect(), Qt::AlignCenter, QStringLiteral("至少勾选 2 组才能做两两对比"));
        return;
    }
    const ProfileAnalysis &an = *m_an;
    const int n = an.vis.size();
    const Layout L = layout();
    const double rmsMax = rmsScaleMax(an);
    const double rMin   = rScaleMin(an);
    const QString titles[2] = {QStringLiteral("两两相关系数 r（形状相似度，越接近 1 越像）"),
                               QStringLiteral("两两均方根差 mm（去均值后，越大差得越多）")};
    const bool showNum = L.cell >= 22;

    for (int w = 0; w < 2; ++w) {
        const QRectF &m = L.mat[w];
        g.setFont(fontPx(g, 12, true));
        g.setPen(kInk);
        g.drawText(QRectF(m.left() - kLabelW, 2, m.width() + kLabelW + 40, kTitleH), Qt::AlignLeft | Qt::AlignVCenter,
                   titles[w]);

        // 行/列标签：组号（文字用墨色，组的身份由左侧树和曲线页的颜色给出）
        g.setFont(fontPx(g, std::max(9, std::min(11, int(L.cell * 0.4)))));
        for (int i = 0; i < n; ++i) {
            const QString lab = QStringLiteral("#%1").arg(an.vis[i] + 1);
            g.setPen(kInk2);
            g.drawText(QRectF(m.left() - kLabelW, m.top() + i * L.cell, kLabelW - 4, L.cell),
                       Qt::AlignRight | Qt::AlignVCenter, lab);
            g.drawText(QRectF(m.left() + i * L.cell, m.top() - 18, L.cell, 16), Qt::AlignCenter, lab);
        }

        g.setFont(fontPx(g, std::max(9, std::min(12, int(L.cell * 0.32)))));
        for (int a = 0; a < n; ++a) {
            for (int b = 0; b < n; ++b) {
                const QRectF c(m.left() + b * L.cell, m.top() + a * L.cell, L.cell, L.cell);
                QColor bg;
                QString txt;
                if (a == b) {
                    bg = QColor("#f1f3f5");
                    txt = QStringLiteral("—");
                } else if (w == 0) {
                    const double r = an.pairR[a][b];
                    bg = rampColor(kBlue, (r - rMin) / (1.0 - rMin));
                    txt = QString::number(r, 'f', 2);
                } else {
                    const double d = an.pairRms[a][b];
                    bg = rampColor(kOrange, d / rmsMax);
                    txt = QString::number(d, 'f', 3);
                }
                g.fillRect(c.adjusted(0.5, 0.5, -0.5, -0.5), bg);   // 1px 白缝隔开相邻格
                if (showNum) {
                    g.setPen(a == b ? kMuted : inkOn(bg));
                    g.drawText(c, Qt::AlignCenter, txt);
                }
            }
        }
        // 悬停：同一对在两个矩阵里都描边
        if (m_hoverA >= 0 && m_hoverB >= 0 && m_hoverA != m_hoverB) {
            g.setPen(QPen(kInk, 2.0));
            g.setBrush(Qt::NoBrush);
            g.drawRect(QRectF(m.left() + m_hoverB * L.cell, m.top() + m_hoverA * L.cell, L.cell, L.cell));
            g.drawRect(QRectF(m.left() + m_hoverA * L.cell, m.top() + m_hoverB * L.cell, L.cell, L.cell));
        }

        // 色条
        const QRectF bar(m.left(), m.bottom() + 10, std::min(m.width(), 240.0), 10);
        QLinearGradient grad(bar.topLeft(), bar.topRight());
        for (int i = 0; i < kRampN; ++i) grad.setColorAt(double(i) / (kRampN - 1), QColor(w == 0 ? kBlue[i] : kOrange[i]));
        g.fillRect(bar, grad);
        g.setFont(fontPx(g, 10));
        g.setPen(kInk2);
        g.drawText(QRectF(bar.left(), bar.bottom() + 2, 60, 14), Qt::AlignLeft | Qt::AlignVCenter,
                   w == 0 ? QString::number(rMin, 'f', 1) : QStringLiteral("0 mm"));
        g.drawText(QRectF(bar.right() - 80, bar.bottom() + 2, 80, 14), Qt::AlignRight | Qt::AlignVCenter,
                   w == 0 ? QStringLiteral("1") : QStringLiteral("%1 mm").arg(rmsMax, 0, 'f', 2));
    }

    // 悬停说明
    if (m_hoverA >= 0 && m_hoverB >= 0 && m_hoverA != m_hoverB) {
        const QString s = QStringLiteral("#%1 与 #%2：r = %3，均方根差 %4 mm（点击在曲线页高亮这两组）")
                              .arg(an.vis[m_hoverA] + 1).arg(an.vis[m_hoverB] + 1)
                              .arg(an.pairR[m_hoverA][m_hoverB], 0, 'f', 3)
                              .arg(an.pairRms[m_hoverA][m_hoverB], 0, 'f', 4);
        g.setFont(fontPx(g, 12, true));
        g.setPen(kInk);
        g.drawText(QRectF(L.mat[0].left() - kLabelW, L.mat[0].bottom() + kBarH, width(), 18),
                   Qt::AlignLeft | Qt::AlignVCenter, s);
    }
}

void PairMatrixView::mouseMoveEvent(QMouseEvent *e)
{
    int w = -1, a = -1, b = -1;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF p = e->position();
#else
    const QPointF p = e->localPos();
#endif
    if (!cellAt(p, w, a, b)) { w = a = b = -1; }
    if (w != m_hoverWhich || a != m_hoverA || b != m_hoverB) {
        m_hoverWhich = w; m_hoverA = a; m_hoverB = b;
        setCursor(a >= 0 && a != b ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void PairMatrixView::mousePressEvent(QMouseEvent *e)
{
    int w, a, b;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    const QPointF p = e->position();
#else
    const QPointF p = e->localPos();
#endif
    if (e->button() == Qt::LeftButton && m_an && cellAt(p, w, a, b) && a != b)
        emit pairClicked(m_an->vis[a], m_an->vis[b]);
}

void PairMatrixView::leaveEvent(QEvent *)
{
    m_hoverWhich = m_hoverA = m_hoverB = -1;
    unsetCursor();
    update();
}

// ─────────────────────────── 分析页 ───────────────────────────

ProfileAnalysisPage::ProfileAnalysisPage(QWidget *parent) : QWidget(parent)
{
    m_summary = new QLabel;
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);
    m_summary->setContentsMargins(8, 6, 8, 2);

    m_matrix = new PairMatrixView;
    connect(m_matrix, &PairMatrixView::pairClicked, this, &ProfileAnalysisPage::pairClicked);

    m_table = new QTableWidget;
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({QStringLiteral("组"), QStringLiteral("采集时间"), QStringLiteral("样本"),
                                        QStringLiteral("峰谷 mm"), QStringLiteral("与平均 r"),
                                        QStringLiteral("与平均均方根差 mm"), QStringLiteral("零点错位 mm"),
                                        QStringLiteral("辊筒振幅 mm"), QStringLiteral("辊筒相位 °")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setToolTip(QStringLiteral(
        "零点错位：本组曲线相对平均曲线整体平移了多少（反映 ch4 黑点零点的重复性，正 = 偏后）\n"
        "辊筒振幅/相位：按“辊筒周期”对 本组−平均曲线 拟合的正弦；振幅接近而相位各不相同，说明是从动辊跳动\n"
        "双击某行：在曲线页高亮该组"));
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_an.vis.size()) emit profileClicked(m_an.vis[row]);
    });

    m_avgInfo = new QLabel;
    m_avgInfo->setWordWrap(true);
    m_avgInfo->setTextFormat(Qt::RichText);
    m_avgInfo->setContentsMargins(8, 2, 8, 6);

    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);
    v->addWidget(m_summary);
    v->addWidget(m_matrix, 3);
    v->addWidget(m_table, 2);
    v->addWidget(m_avgInfo);
}

void ProfileAnalysisPage::setData(const QVector<BeltProfile> *ps, const QVector<bool> *visible,
                                  const ProfileAnalysisOptions &opt)
{
    m_ps = ps;
    static const QVector<BeltProfile> kEmpty;
    static const QVector<bool> kNone;
    ProfileAnalysisOptions o = opt;
    o.fullStats = true;
    m_an = analyzeProfiles(ps ? *ps : kEmpty, visible ? *visible : kNone, o);
    m_matrix->setAnalysis(&m_an);

    const int n = m_an.vis.size();
    if (n == 0) {
        m_summary->setText(QStringLiteral("没有勾选要分析的轮廓。"));
        m_table->setRowCount(0);
        m_avgInfo->clear();
        return;
    }

    double meanP2p = 0;
    if (!m_an.mean.isEmpty()) {
        const auto mm = std::minmax_element(m_an.mean.begin(), m_an.mean.end());
        meanP2p = *mm.second - *mm.first;
    }
    double ampSum = 0;
    for (double a : m_an.rollerAmpMm) ampSum += a;
    QString s = QStringLiteral("<b>参与分析 %1 组</b>").arg(n);
    if (n >= 2) {
        s += QStringLiteral("　两两相关系数平均 <b>%1</b>　两两均方根差平均 <b>%2 mm</b>　平均曲线峰谷 <b>%3 mm</b>")
                 .arg(m_an.meanPairR, 0, 'f', 2).arg(m_an.meanPairRms, 0, 'f', 3).arg(meanP2p, 0, 'f', 3);
        s += QStringLiteral("　辊筒周期成分平均振幅 %1 mm").arg(ampSum / n, 0, 'f', 3);
    }
    if (m_an.circMm > 0) s += QStringLiteral("　估算周长 %1 mm").arg(m_an.circMm, 0, 'f', 1);
    s += QStringLiteral("<br><span style='color:#495057'>%1；%2。统计只含左侧勾选的组，矩阵格子可点击。</span>")
             .arg(opt.deviation ? QStringLiteral("各组已去均值") : QStringLiteral("各组未去均值（矩阵与均方根差仍按去均值计算）"))
             .arg(m_an.rollerRemoved ? QStringLiteral("已扣除周期 %1 mm 的辊筒成分").arg(opt.rollerPeriodMm, 0, 'f', 1)
                                     : QStringLiteral("未扣除辊筒成分"));
    m_summary->setText(s);

    m_table->setRowCount(n);
    for (int k = 0; k < n; ++k) {
        const int idx = m_an.vis[k];
        const BeltProfile &p = (*ps)[idx];
        auto cell = [this, k](int col, const QString &t, bool num = true) {
            auto *it = new QTableWidgetItem(t);
            if (num) it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            m_table->setItem(k, col, it);
            return it;
        };
        QTableWidgetItem *first = cell(0, QStringLiteral("#%1").arg(idx + 1), false);
        first->setIcon(QIcon(swatch(idx)));
        first->setToolTip(p.path);
        cell(1, p.time.isEmpty() ? p.name : p.time, false);
        cell(2, p.samples > 0 ? QString::number(p.samples) : QStringLiteral("—"));
        cell(3, QString::number(m_an.p2p[k], 'f', 3));
        cell(4, n >= 2 ? QString::number(m_an.rToMean[k], 'f', 3) : QStringLiteral("—"));
        cell(5, n >= 2 ? QString::number(m_an.rmsToMean[k], 'f', 4) : QStringLiteral("—"));
        const double shift = std::fabs(m_an.shiftMm[k]) < 0.005 ? 0.0 : m_an.shiftMm[k];   // 避免显示 -0.00
        cell(6, n >= 2 ? QString::number(shift, 'f', 2) : QStringLiteral("—"));
        cell(7, n >= 2 ? QString::number(m_an.rollerAmpMm[k], 'f', 3) : QStringLiteral("—"));
        cell(8, n >= 2 ? QString::number(m_an.rollerPhaseDeg[k], 'f', 0) : QStringLiteral("—"));
    }
    m_table->resizeColumnsToContents();

    if (m_an.avgRms.isEmpty()) {
        m_avgInfo->setText(QStringLiteral("<span style='color:#495057'>至少 2 组才能估算多组平均做表的精度。</span>"));
    } else {
        QStringList parts;
        for (int k = 0; k < m_an.avgRms.size(); ++k)
            parts << QStringLiteral("%1 组 <b>%2</b>").arg(k + 1).arg(m_an.avgRms[k], 0, 'f', 3);
        m_avgInfo->setText(QStringLiteral("用 k 组平均做轮廓表，相对其余组平均的均方根差（mm，越小越好）：　%1")
                               .arg(parts.join(QStringLiteral("　"))));
    }
}
