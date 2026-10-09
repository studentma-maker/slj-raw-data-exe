#include "mainwindow.h"

#include "headerview.h"
#include "piecechart.h"
#include "plotwidget.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMap>
#include <QMessageBox>
#include <QMimeData>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {

const double kPdfLogicalWidth = 1400.0;   // PDF 页面按此逻辑宽度排版，字号/线宽与屏幕一致

QString pieceLabel(const Piece &p)
{
    QString s = p.seq >= 0 ? QStringLiteral("片 #%1").arg(p.seq) : QStringLiteral("片 (无序号)");
    s += QStringLiteral("  %1").arg(fmtClock(p.startMs).left(8));
    s += p.lenMm >= 0 ? QStringLiteral("  发料 %1").arg(p.lenMm, 0, 'f', 1) : QStringLiteral("  发料 —");
    s += p.curtainMm >= 0 ? QStringLiteral(" / 发帘 %1 mm").arg(p.curtainMm, 0, 'f', 1)
                          : QStringLiteral(" / 发帘 未记录");
    int alarms = 0;
    for (const LogEvent &e : p.events)
        if (e.kind == EvAlarm) ++alarms;
    if (alarms > 0) s += QStringLiteral("  ⚠%1").arg(alarms);
    return s;
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("缝纫日志可视化 (sewing.log)"));
    resize(1500, 920);
    setAcceptDrops(true);
    buildUi();
}

void MainWindow::buildUi()
{
    QToolBar *tb = addToolBar(QStringLiteral("工具栏"));
    tb->setMovable(false);
    tb->setToolButtonStyle(Qt::ToolButtonTextOnly);
    tb->addAction(QStringLiteral("打开日志…"), this, &MainWindow::onOpen);
    tb->addSeparator();
    m_dataActions << tb->addAction(QStringLiteral("导出当前视图 PDF"), this, &MainWindow::onExportCurrentPdf);
    m_dataActions << tb->addAction(QStringLiteral("导出完整报告 PDF"), this, &MainWindow::onExportReportPdf);
    m_dataActions << tb->addAction(QStringLiteral("导出当前视图 PNG"), this, &MainWindow::onExportPng);
    for (QAction *a : m_dataActions) a->setEnabled(false);

    m_tree = new QTreeWidget;
    m_tree->setHeaderHidden(true);
    m_tree->setMinimumWidth(250);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &MainWindow::onTreeSelection);

    m_stack = new QStackedWidget;

    // 欢迎页
    m_welcome = new QLabel(QStringLiteral(
        "<div style='text-align:center'>"
        "<p style='font-size:22px'>把 <b>sewing.log</b> 文件拖到这里</p>"
        "<p style='font-size:14px;color:#666'>或点击左上角“打开日志…”</p></div>"));
    m_welcome->setAlignment(Qt::AlignCenter);
    m_stack->addWidget(m_welcome);

    // HEADER 页
    m_headerView = new HeaderView;
    auto *scroll = new QScrollArea;
    scroll->setWidget(m_headerView);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    m_headerPage = scroll;
    m_stack->addWidget(m_headerPage);

    // 单片曲线页
    m_plot = new PlotWidget;
    auto *cbWin = new QCheckBox(QStringLiteral("WIN 窗口"));
    auto *cbEv  = new QCheckBox(QStringLiteral("事件线"));
    auto *cbRaw = new QCheckBox(QStringLiteral("原始曲线"));
    auto *cbFlt = new QCheckBox(QStringLiteral("滤波后曲线"));
    cbRaw->setStyleSheet("color:#ae3ec9;font-weight:bold");
    cbFlt->setStyleSheet("color:#1c7ed6;font-weight:bold");
    for (QCheckBox *cb : {cbWin, cbEv, cbRaw, cbFlt}) cb->setChecked(true);
    connect(cbFlt, &QCheckBox::toggled, m_plot, &PlotWidget::setShowFlt);
    connect(cbWin, &QCheckBox::toggled, m_plot, &PlotWidget::setShowWin);
    connect(cbEv, &QCheckBox::toggled, m_plot, &PlotWidget::setShowEvents);
    connect(cbRaw, &QCheckBox::toggled, m_plot, &PlotWidget::setShowRaw);
    auto *btnReset = new QPushButton(QStringLiteral("复位缩放"));
    connect(btnReset, &QPushButton::clicked, m_plot, &PlotWidget::resetView);
    auto *hint = new QLabel(QStringLiteral(
        "滚轮：缩放横轴(距离)　Ctrl+滚轮：缩放光标所在面板的纵轴　Shift+滚轮：左右平移　"
        "左键拖动：平移　右键拖框：框选放大　双击：复位"));
    hint->setStyleSheet("color:#666");
    auto *bar = new QHBoxLayout;
    bar->setContentsMargins(8, 4, 8, 0);
    bar->addWidget(btnReset);
    bar->addWidget(cbRaw);
    bar->addWidget(cbFlt);
    bar->addWidget(cbWin);
    bar->addWidget(cbEv);
    bar->addSpacing(12);
    bar->addWidget(hint, 1);
    m_piecePage = new QWidget;
    auto *pv = new QVBoxLayout(m_piecePage);
    pv->setContentsMargins(0, 0, 0, 0);
    pv->setSpacing(2);
    pv->addLayout(bar);
    pv->addWidget(m_plot, 1);
    m_stack->addWidget(m_piecePage);

    // ALARM 页
    m_alarmTable = new QTableWidget;
    m_alarmTable->setColumnCount(5);
    m_alarmTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("类型"), QStringLiteral("消息"),
                                             QStringLiteral("所在片"), QStringLiteral("含义")});
    m_alarmTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_alarmTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_alarmTable->setAlternatingRowColors(true);
    m_alarmTable->verticalHeader()->setVisible(false);
    m_alarmTable->horizontalHeader()->setStretchLastSection(true);
    connect(m_alarmTable, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < m_data.alarms.size() && m_data.alarms[row].pieceIdx >= 0)
            selectPiece(m_data.alarms[row].pieceIdx);
    });
    auto *alarmHint = new QLabel(QStringLiteral(
        "ALARM 行是诊断告警事件，只在武装期间写入日志。“所在片”列表示告警发生时正在记录 TICK 的片，双击可跳到该片曲线（图上红色虚线）。"));
    alarmHint->setWordWrap(true);
    alarmHint->setContentsMargins(8, 6, 8, 2);
    m_alarmPage = new QWidget;
    auto *av = new QVBoxLayout(m_alarmPage);
    av->setContentsMargins(0, 0, 0, 0);
    av->addWidget(alarmHint);
    av->addWidget(m_alarmTable, 1);
    m_stack->addWidget(m_alarmPage);

    auto *split = new QSplitter;
    split->addWidget(m_tree);
    split->addWidget(m_stack);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({300, 1200});
    setCentralWidget(split);

    statusBar()->showMessage(QStringLiteral("拖入 .log 文件开始"));
}

// ─────────────────────────── 文件加载 ───────────────────────────

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    const QList<QUrl> urls = e->mimeData()->urls();
    for (const QUrl &u : urls) {
        if (u.isLocalFile()) {
            e->acceptProposedAction();
            loadFile(u.toLocalFile());
            return;
        }
    }
}

void MainWindow::onOpen()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("打开 sewing.log"), QString(),
                                                      QStringLiteral("日志文件 (*.log *.txt);;所有文件 (*)"));
    if (!path.isEmpty()) loadFile(path);
}

bool MainWindow::loadFile(const QString &path)
{
    LogData data;
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = parseLogFile(path, data, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("无法解析"),
                             QStringLiteral("%1\n\n%2").arg(path, err));
        return false;
    }

    // 控件里存着指向旧数据的指针，先摘掉再替换
    m_plot->setPiece(nullptr, nullptr);
    m_headerView->setHeader(nullptr, QString());
    m_tree->clear();
    m_data = data;

    rebuildTree();
    fillAlarmTable();
    for (QAction *a : m_dataActions) a->setEnabled(true);
    setWindowTitle(QStringLiteral("%1 — 缝纫日志可视化").arg(QFileInfo(path).fileName()));
    statusBar()->showMessage(
        QStringLiteral("%1 行 · 武装 %2 次 · %3 片 · TICK %4 · WIN %5 · ALARM %6%7")
            .arg(m_data.lineCount).arg(m_data.headers.size()).arg(m_data.pieces.size())
            .arg(m_data.tickCount).arg(m_data.winCount).arg(m_data.alarms.size())
            .arg(m_data.badLines ? QStringLiteral(" · 未识别 %1 行").arg(m_data.badLines) : QString()));

    // 默认停在第一片的曲线上；没有片就显示第一条 HEADER
    if (!m_data.pieces.isEmpty()) selectPiece(0);
    else if (m_tree->topLevelItemCount() > 0) m_tree->setCurrentItem(m_tree->topLevelItem(0));
    return true;
}

QString MainWindow::headerTitle(int headerIdx) const
{
    return QStringLiteral("初始化参数 — 第 %1 次武装").arg(headerIdx + 1);
}

void MainWindow::rebuildTree()
{
    auto addPiece = [this](QTreeWidgetItem *parent, int idx) {
        const Piece &p = m_data.pieces[idx];
        auto *it = new QTreeWidgetItem(parent, QStringList(pieceLabel(p)));
        it->setData(0, RoleKind, PagePiece);
        it->setData(0, RoleIndex, idx);
        if (!p.complete) it->setForeground(0, QColor("#c92a2a"));
        it->setToolTip(0, p.complete ? QStringLiteral("已缝完") : p.endReason);
    };

    for (int i = 0; i < m_data.headers.size(); ++i) {
        const HeaderInfo &h = m_data.headers[i];
        QString text = QStringLiteral("武装 %1  %2  (%3 片)").arg(i + 1).arg(fmtClock(h.ms).left(8)).arg(h.pieces.size());
        if (h.changed) text += QStringLiteral("  ★参数变化");
        auto *top = new QTreeWidgetItem(m_tree, QStringList(text));
        top->setData(0, RoleKind, PageHeader);
        top->setData(0, RoleIndex, i);
        QFont f = top->font(0);
        f.setBold(true);
        top->setFont(0, f);
        top->setToolTip(0, QStringLiteral("点击查看本次武装的 HEADER 初始化参数"));
        for (int idx : h.pieces) addPiece(top, idx);
    }
    // 日志开头被截断(没有 HEADER)时的片
    QTreeWidgetItem *orphan = nullptr;
    for (int i = 0; i < m_data.pieces.size(); ++i) {
        if (m_data.pieces[i].headerIdx >= 0) continue;
        if (!orphan) orphan = new QTreeWidgetItem(m_tree, QStringList(QStringLiteral("(无 HEADER 的片)")));
        addPiece(orphan, i);
    }

    auto *alarm = new QTreeWidgetItem(m_tree, QStringList(QStringLiteral("告警 ALARM (%1)").arg(m_data.alarms.size())));
    alarm->setData(0, RoleKind, PageAlarm);
    QFont f = alarm->font(0);
    f.setBold(true);
    alarm->setFont(0, f);
    m_tree->expandAll();
}

void MainWindow::fillAlarmTable()
{
    m_alarmTable->setRowCount(m_data.alarms.size());
    for (int r = 0; r < m_data.alarms.size(); ++r) {
        const AlarmRec &a = m_data.alarms[r];
        QString piece = QStringLiteral("—");
        if (a.pieceIdx >= 0) {
            const int seq = m_data.pieces[a.pieceIdx].seq;
            piece = seq >= 0 ? QStringLiteral("#%1").arg(seq) : QStringLiteral("(无序号)");
        }
        const QStringList cells = {fmtClock(a.ms, true), a.type, a.msg, piece, alarmExplain(a.type)};
        for (int c = 0; c < cells.size(); ++c) m_alarmTable->setItem(r, c, new QTableWidgetItem(cells[c]));
    }
    m_alarmTable->resizeColumnsToContents();
}

void MainWindow::selectPiece(int pieceIdx)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem *top = m_tree->topLevelItem(i);
        for (int k = 0; k < top->childCount(); ++k) {
            QTreeWidgetItem *c = top->child(k);
            if (c->data(0, RoleKind).toInt() == PagePiece && c->data(0, RoleIndex).toInt() == pieceIdx) {
                m_tree->setCurrentItem(c);
                return;
            }
        }
    }
}

void MainWindow::onTreeSelection()
{
    QTreeWidgetItem *it = m_tree->currentItem();
    if (!it) return;
    const int kind = it->data(0, RoleKind).toInt();
    const int idx  = it->data(0, RoleIndex).toInt();
    if (kind == PageHeader && idx < m_data.headers.size()) {
        m_headerView->setHeader(&m_data.headers[idx], headerTitle(idx));
        m_stack->setCurrentWidget(m_headerPage);
    } else if (kind == PagePiece && idx < m_data.pieces.size()) {
        const Piece &p = m_data.pieces[idx];
        m_plot->setPiece(&p, p.headerIdx >= 0 ? &m_data.headers[p.headerIdx] : nullptr);
        m_stack->setCurrentWidget(m_piecePage);
    } else if (kind == PageAlarm) {
        m_stack->setCurrentWidget(m_alarmPage);
    }
}

// ─────────────────────────── 导出 ───────────────────────────

QString MainWindow::defaultExportName(const QString &suffix) const
{
    const QFileInfo fi(m_data.path);
    return fi.absolutePath() + "/" + fi.completeBaseName() + suffix;
}

void MainWindow::setupPdf(QPdfWriter &pdf) const
{
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageMargins(QMarginsF(8, 8, 8, 8), QPageLayout::Millimeter);
    pdf.setResolution(300);
    pdf.setTitle(QFileInfo(m_data.path).fileName());
    pdf.setCreator(QStringLiteral("SewingLogViewer"));
}

// 把坐标系缩放成“逻辑宽度 1400”的页面，返回页面的逻辑矩形
QRectF MainWindow::beginPage(QPainter &g, const QPdfWriter &pdf) const
{
    const double s = pdf.width() / kPdfLogicalWidth;
    g.resetTransform();
    g.scale(s, s);
    return QRectF(0, 0, kPdfLogicalWidth, pdf.height() / s);
}

int MainWindow::renderAlarmPage(QPainter &g, const QRectF &rect, int firstRow) const
{
    auto font = [&g](int px, bool bold = false) {
        QFont f = g.font();
        f.setPixelSize(px);
        f.setBold(bold);
        return f;
    };
    g.setRenderHint(QPainter::TextAntialiasing, true);
    g.fillRect(rect, Qt::white);
    const double L = rect.left() + 24, W = rect.width() - 48;
    double y = rect.top() + 14;

    g.setPen(QColor("#212529"));
    g.setFont(font(17, true));
    g.drawText(QPointF(L, y + 16), QStringLiteral("告警 ALARM（共 %1 条）%2").arg(m_data.alarms.size())
                                       .arg(firstRow > 0 ? QStringLiteral(" — 续") : QString()));
    y += 34;

    if (firstRow == 0) {
        // 按类型汇总 + 含义说明
        QMap<QString, int> counts;
        for (const AlarmRec &a : m_data.alarms) counts[a.type]++;
        for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
            g.setFont(font(12, true));
            g.setPen(QColor("#c92a2a"));
            g.drawText(QRectF(L, y, 260, 20), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1 × %2").arg(it.key()).arg(it.value()));
            g.setFont(font(12));
            g.setPen(QColor("#495057"));
            g.drawText(QRectF(L + 265, y, W - 265, 20), Qt::AlignLeft | Qt::AlignVCenter, alarmExplain(it.key()));
            y += 22;
        }
        y += 10;
    }

    const double colX[5] = {0, 200, 420, 1150, W};
    const QString heads[4] = {QStringLiteral("时间"), QStringLiteral("类型"), QStringLiteral("消息"),
                              QStringLiteral("所在片")};
    const double rh = 20.0;
    g.fillRect(QRectF(L, y, W, rh), QColor("#dee2e6"));
    g.setFont(font(12, true));
    g.setPen(QColor("#212529"));
    for (int c = 0; c < 4; ++c)
        g.drawText(QRectF(L + colX[c] + 6, y, colX[c + 1] - colX[c] - 12, rh), Qt::AlignLeft | Qt::AlignVCenter, heads[c]);
    y += rh;

    int row = firstRow;
    g.setFont(font(11));
    for (; row < m_data.alarms.size() && y + rh <= rect.bottom() - 10; ++row) {
        const AlarmRec &a = m_data.alarms[row];
        if ((row - firstRow) % 2) g.fillRect(QRectF(L, y, W, rh), QColor("#f8f9fa"));
        QString piece = QStringLiteral("—");
        if (a.pieceIdx >= 0) {
            const int seq = m_data.pieces[a.pieceIdx].seq;
            piece = seq >= 0 ? QStringLiteral("#%1").arg(seq) : QStringLiteral("(无序号)");
        }
        const QString cells[4] = {fmtClock(a.ms, true), a.type, a.msg, piece};
        for (int c = 0; c < 4; ++c)
            g.drawText(QRectF(L + colX[c] + 6, y, colX[c + 1] - colX[c] - 12, rh), Qt::AlignLeft | Qt::AlignVCenter,
                       cells[c]);
        y += rh;
    }
    return row < m_data.alarms.size() ? row : -1;
}

bool MainWindow::exportReportPdf(const QString &path)
{
    QPdfWriter pdf(path);
    setupPdf(pdf);
    QPainter g;
    if (!g.begin(&pdf)) return false;

    bool firstPage = true;
    auto nextPage = [&]() -> QRectF {
        if (!firstPage) pdf.newPage();
        firstPage = false;
        return beginPage(g, pdf);
    };

    PieceChart::View view;
    view.showWin    = m_plot->view().showWin;
    view.showEvents = m_plot->view().showEvents;
    view.showRaw    = m_plot->view().showRaw;
    view.showFlt    = m_plot->view().showFlt;

    for (int i = 0; i < m_data.headers.size(); ++i) {
        const HeaderInfo &h = m_data.headers[i];
        // 参数页：首次武装和参数发生变化时各出一页，避免连续重复的 HEADER 占满报告
        if (i == 0 || h.changed) renderHeader(g, nextPage(), h, headerTitle(i));
        for (int idx : h.pieces) {
            const Piece &p = m_data.pieces[idx];
            PieceChart::resetView(p, view);
            PieceChart::render(g, nextPage(), p, &h, view, nullptr);
        }
    }
    for (const Piece &p : m_data.pieces) {
        if (p.headerIdx >= 0) continue;
        PieceChart::resetView(p, view);
        PieceChart::render(g, nextPage(), p, nullptr, view, nullptr);
    }
    if (!m_data.alarms.isEmpty()) {
        int row = 0;
        while (row >= 0) row = renderAlarmPage(g, nextPage(), row);
    }
    if (firstPage) nextPage();
    return g.end();
}

bool MainWindow::exportCurrentPdf(const QString &path)
{
    QPdfWriter pdf(path);
    setupPdf(pdf);
    QPainter g;
    if (!g.begin(&pdf)) return false;

    QWidget *cur = m_stack->currentWidget();
    if (cur == m_piecePage && m_plot->piece()) {
        // 用屏幕上当前的缩放范围重新矢量绘制
        PieceChart::render(g, beginPage(g, pdf), *m_plot->piece(), m_plot->header(), m_plot->view(), nullptr);
    } else if (cur == m_headerPage && m_headerView->header()) {
        renderHeader(g, beginPage(g, pdf), *m_headerView->header(), m_headerView->title());
    } else if (cur == m_alarmPage) {
        int row = 0;
        bool first = true;
        while (row >= 0) {
            if (!first) pdf.newPage();
            first = false;
            row = renderAlarmPage(g, beginPage(g, pdf), row);
        }
    }
    return g.end();
}

void MainWindow::onExportCurrentPdf()
{
    QString suffix = QStringLiteral("_view.pdf");
    if (m_stack->currentWidget() == m_piecePage && m_plot->piece()) {
        const int seq = m_plot->piece()->seq;
        suffix = seq >= 0 ? QStringLiteral("_piece%1.pdf").arg(seq) : QStringLiteral("_piece.pdf");
    } else if (m_stack->currentWidget() == m_headerPage) {
        suffix = QStringLiteral("_header.pdf");
    } else if (m_stack->currentWidget() == m_alarmPage) {
        suffix = QStringLiteral("_alarm.pdf");
    }
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前视图为 PDF"),
                                                      defaultExportName(suffix), QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (exportCurrentPdf(path)) statusBar()->showMessage(QStringLiteral("已导出：%1").arg(path), 8000);
    else QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(path));
}

void MainWindow::onExportReportPdf()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出完整报告为 PDF"),
                                                      defaultExportName(QStringLiteral("_report.pdf")),
                                                      QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = exportReportPdf(path);
    QApplication::restoreOverrideCursor();
    if (ok) statusBar()->showMessage(QStringLiteral("已导出：%1").arg(path), 8000);
    else QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(path));
}

void MainWindow::onExportPng()
{
    QWidget *target = m_stack->currentWidget();
    if (target == m_piecePage) target = m_plot;
    else if (target == m_headerPage) target = m_headerView;
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("导出当前视图为 PNG"),
                                                      defaultExportName(QStringLiteral("_view.png")),
                                                      QStringLiteral("PNG (*.png)"));
    if (path.isEmpty()) return;
    if (target->grab().save(path, "PNG")) statusBar()->showMessage(QStringLiteral("已导出：%1").arg(path), 8000);
    else QMessageBox::warning(this, QStringLiteral("导出失败"), QStringLiteral("无法写入 %1").arg(path));
}
