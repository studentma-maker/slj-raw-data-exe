#pragma once

#include "beltprofile.h"

#include <QPainter>
#include <QRectF>
#include <QWidget>

// 皮带激光轮廓对比图：横轴 = 皮带一圈进度(0~1，下方附估算 mm)，纵轴 = 激光读数(mm)。
// 多组叠加，默认去均值比较形状，可叠加各组平均曲线；右侧图例给出每组的采样信息、
// 峰谷差与"与平均曲线的相关系数"。屏幕控件与 PNG 批量导出共用 ProfileChart::render。
class ProfileChart
{
public:
    struct View {
        double x0 = 0.0, x1 = 1.0;   // 可见进度范围
        double y[2] = {0.0, 1.0};    // 纵向可见范围（占自动量程的比例）
        bool   deviation = true;     // 每组减去自身均值（不同天/不同安装高度的整体偏移不影响形状对比）
        bool   showMean  = true;     // 叠加可见各组的平均曲线
    };
    struct Geom {
        QRectF plot, xaxis, legend;
        QVector<QRectF> legendRows;  // 与可见组一一对应（顺序同 visibleOrder）
        QVector<int>    visibleOrder;
    };

    static Geom geometry(const QRectF &rect, const QVector<BeltProfile> &ps, const QVector<bool> &visible);
    static void render(QPainter &g, const QRectF &rect, const QVector<BeltProfile> &ps,
                       const QVector<bool> &visible, const View &v, int highlight, const QPointF *hover);
    static QColor seriesColor(int idx);
    static Qt::PenStyle seriesStyle(int idx);
};

class ProfileWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileWidget(QWidget *parent = nullptr);

    void setProfiles(const QVector<BeltProfile> *ps, const QVector<bool> *visible);
    void setHighlight(int idx);
    const ProfileChart::View &view() const { return m_view; }

public slots:
    void resetView();
    void setDeviation(bool on);
    void setShowMean(bool on);
    void refresh();

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    int  legendRowAt(const QPointF &pos) const;
    void clampX();

    const QVector<BeltProfile> *m_ps      = nullptr;
    const QVector<bool>        *m_visible = nullptr;
    ProfileChart::View m_view;
    int     m_pinned     = -1;   // 点击图例/树节点固定高亮的组
    int     m_hoverRow   = -1;   // 鼠标悬停的图例组
    bool    m_hoverValid = false;
    QPointF m_hoverPos;
    bool    m_panning    = false;
    QPointF m_lastPos;
};
