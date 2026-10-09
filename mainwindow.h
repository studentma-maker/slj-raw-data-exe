#pragma once

#include "logparser.h"

#include <QMainWindow>

class HeaderView;
class PlotWidget;
class QLabel;
class QPainter;
class QPdfWriter;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool loadFile(const QString &path);
    // 不经过对话框直接导出，供菜单动作和命令行自检共用
    bool exportReportPdf(const QString &path);
    bool exportCurrentPdf(const QString &path);
    void selectPiece(int pieceIdx);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private slots:
    void onOpen();
    void onExportCurrentPdf();
    void onExportReportPdf();
    void onExportPng();
    void onTreeSelection();

private:
    enum PageKind { PageWelcome, PageHeader, PagePiece, PageAlarm };
    enum ItemRole { RoleKind = Qt::UserRole + 1, RoleIndex };

    void    buildUi();
    void    rebuildTree();
    void    fillAlarmTable();
    QString headerTitle(int headerIdx) const;
    QString defaultExportName(const QString &suffix) const;
    void    setupPdf(QPdfWriter &pdf) const;
    QRectF  beginPage(QPainter &g, const QPdfWriter &pdf) const;
    int     renderAlarmPage(QPainter &g, const QRectF &rect, int firstRow) const;

    LogData m_data;

    QTreeWidget    *m_tree   = nullptr;
    QStackedWidget *m_stack  = nullptr;
    QLabel         *m_welcome = nullptr;
    HeaderView     *m_headerView = nullptr;
    PlotWidget     *m_plot   = nullptr;
    QTableWidget   *m_alarmTable = nullptr;
    QWidget        *m_alarmPage  = nullptr;
    QWidget        *m_headerPage = nullptr;
    QWidget        *m_piecePage  = nullptr;
    QList<QAction *> m_dataActions;
};
