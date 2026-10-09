#pragma once

#include "logparser.h"

#include <QPainter>
#include <QRectF>

// 单片曲线图的纯绘制逻辑：屏幕控件(PlotWidget)和 PDF 导出共用同一套代码，
// 所以导出的 PDF 与屏幕上缩放后的视图完全一致（矢量）。
class PieceChart
{
public:
    enum { PanelThick, PanelLaser, PanelFreq, PanelPos, PanelCount };

    struct View {
        double x0 = 0.0, x1 = 1.0;            // 可见横轴范围：相对 CH1 上升沿的累计皮带脉冲数(物料前进距离)
        double y[PanelCount][2];              // 各面板纵向可见范围（占自动量程的比例，0~1 为全量程）
        bool   showWin    = true;
        bool   showEvents = true;
        bool   showRaw    = true;             // 原始曲线：base−lraw 厚度、lraw 激光
        bool   showFlt    = true;             // 滤波后曲线：thick 厚度、lflt 激光
        View() { for (auto &r : y) { r[0] = 0.0; r[1] = 1.0; } }
    };

    struct Geom {
        QRectF panel[PanelCount];
        QRectF events, gantt, digital, xaxis;
        double plotLeft = 0.0, plotRight = 0.0;
        double top = 0.0, bottom = 0.0;       // 事件竖线的纵向范围
    };

    static double minXSpan(const Piece &p);
    static void   fullXRange(const Piece &p, double &x0, double &x1);
    static void   resetView(const Piece &p, View &v);
    static Geom   geometry(const QRectF &rect, const Piece &p, const View &v);
    static void   render(QPainter &g, const QRectF &rect, const Piece &p, const HeaderInfo *hdr,
                         const View &v, const QPointF *hover);
    static QColor rowColor(int row);
};
