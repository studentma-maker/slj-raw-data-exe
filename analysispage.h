#pragma once

#include "profileanalysis.h"

#include <QWidget>

class QLabel;
class QTableWidget;

// 两两矩阵热力图：左 = 相关系数 r（越深越像），右 = 均方根差 mm（越深差得越多）。
// 每格标出数值；悬停给出这一对的说明，点击格子 → pairClicked（在曲线页同时高亮这两组）。
class PairMatrixView : public QWidget
{
    Q_OBJECT
public:
    explicit PairMatrixView(QWidget *parent = nullptr);
    void setAnalysis(const ProfileAnalysis *an);
    QSize sizeHint() const override;

signals:
    void pairClicked(int profileA, int profileB);

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    struct Layout {
        double cell = 0;
        QRectF mat[2];   // 两个矩阵的格子区域
    };
    Layout layout() const;
    bool   cellAt(const QPointF &p, int &which, int &a, int &b) const;

    const ProfileAnalysis *m_an = nullptr;
    int m_hoverWhich = -1, m_hoverA = -1, m_hoverB = -1;
};

// 皮带轮廓"分析"页：汇总一行 + 两两矩阵 + 每组统计表 + 多组平均的预期残差。
// 只统计左侧勾选显示的组；"扣除辊筒成分"等选项与曲线页共用。
class ProfileAnalysisPage : public QWidget
{
    Q_OBJECT
public:
    explicit ProfileAnalysisPage(QWidget *parent = nullptr);
    void setData(const QVector<BeltProfile> *ps, const QVector<bool> *visible, const ProfileAnalysisOptions &opt);

signals:
    void pairClicked(int profileA, int profileB);
    void profileClicked(int profileIdx);

private:
    ProfileAnalysis  m_an;
    const QVector<BeltProfile> *m_ps = nullptr;
    QLabel          *m_summary = nullptr;
    PairMatrixView  *m_matrix  = nullptr;
    QTableWidget    *m_table   = nullptr;
    QLabel          *m_avgInfo = nullptr;
};
