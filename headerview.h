#pragma once

#include "logparser.h"

#include <QPainter>
#include <QWidget>

// HEADER(初始化参数)页：传感器物理距离示意图 + 速度表 + 查表行完整表。
// renderHeader 同时用于屏幕显示和 PDF 导出。
double renderHeader(QPainter &g, const QRectF &rect, const HeaderInfo &h, const QString &title);

class HeaderView : public QWidget
{
    Q_OBJECT
public:
    explicit HeaderView(QWidget *parent = nullptr);
    void setHeader(const HeaderInfo *h, const QString &title);
    const HeaderInfo *header() const { return m_h; }
    const QString &title() const { return m_title; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    const HeaderInfo *m_h = nullptr;
    QString m_title;
};
