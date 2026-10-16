#pragma once

#include "beltprofile.h"
#include "logparser.h"

#include <QMainWindow>

class HeaderView;
class PlotWidget;
class ProfileWidget;
class ProfileAnalysisPage;
class QTabWidget;
class QAction;
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

    // quiet：失败时不弹对话框，只把原因写到 errorOut（批处理模式用，弹窗会卡住无人值守的进程）
    bool loadFile(const QString &path, bool quiet = false, QString *errorOut = nullptr);
    // 不经过对话框直接导出，供菜单动作和命令行自检共用
    bool exportReportPdf(const QString &path);
    bool exportCurrentPdf(const QString &path);
    void selectPiece(int pieceIdx);
    // 皮带激光轮廓（.belt_profile.csv）：可多次打开叠加对比，与 sewing.log 互不影响
    bool loadProfiles(const QStringList &paths, bool quiet = false, QString *errorOut = nullptr);
    bool exportProfilePng(const QString &path, const QSize &size = QSize(1600, 900));
    bool exportProfileAnalysisPng(const QString &path, const QSize &size = QSize(1600, 1000));
    void setProfileRemoveRoller(bool on);   // 批处理用：不经界面直接设置“扣除辊筒成分”

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private slots:
    void onOpen();
    void onOpenProfiles();
    void onExportCurrentPdf();
    void onExportReportPdf();
    void onExportPng();
    void onTreeSelection();

private:
    enum PageKind { PageWelcome, PageHeader, PagePiece, PageAlarm, PageProfile };
    enum ItemRole { RoleKind = Qt::UserRole + 1, RoleIndex };

    void    buildUi();
    void    rebuildTree();
    void    rebuildProfileTree();
    void    showProfilePage(int highlight);
    void    refreshProfileViews();   // 轮廓数据/勾选/选项变化：曲线页与分析页一起刷新
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
    QAction        *m_pngAction  = nullptr;

    QVector<BeltProfile> m_profiles;
    QVector<bool>        m_profileVisible;
    ProfileWidget  *m_profileWidget = nullptr;
    ProfileAnalysisPage *m_analysisPage = nullptr;
    QTabWidget     *m_profileTabs   = nullptr;
    QWidget        *m_profilePage   = nullptr;
    bool            m_profileTreeUpdating = false;   // 重建轮廓树节点时屏蔽 itemChanged
};
