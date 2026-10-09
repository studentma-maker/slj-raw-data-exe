#pragma once

#include "piecechart.h"

#include <QWidget>

// 单片曲线图控件（横轴=皮带脉冲/距离）：滚轮缩放横轴，Ctrl+滚轮缩放光标所在面板的纵轴，
// 左键拖动平移，右键拖框放大，双击复位
class PlotWidget : public QWidget
{
    Q_OBJECT
public:
    explicit PlotWidget(QWidget *parent = nullptr);

    void setPiece(const Piece *piece, const HeaderInfo *hdr);
    const Piece *piece() const { return m_piece; }
    const HeaderInfo *header() const { return m_hdr; }
    const PieceChart::View &view() const { return m_view; }

public slots:
    void resetView();
    void setShowWin(bool on);
    void setShowEvents(bool on);
    void setShowRaw(bool on);
    void setShowFlt(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    void wheelEvent(QWheelEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *) override;

private:
    int  panelAt(const QPointF &pos, const PieceChart::Geom &gm) const;
    void zoomX(double factor, double anchorX, const PieceChart::Geom &gm);
    void zoomY(int panel, double factor, double anchorY, const PieceChart::Geom &gm);
    void clampX();

    const Piece      *m_piece = nullptr;
    const HeaderInfo *m_hdr   = nullptr;
    PieceChart::View  m_view;

    bool    m_hoverValid = false;
    QPointF m_hoverPos;
    bool    m_panning    = false;
    int     m_panPanel   = -1;
    QPointF m_lastPos;
    bool    m_boxing     = false;
    QPointF m_boxStart;
};
