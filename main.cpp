#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);

    const QStringList args = app.arguments();

    MainWindow w;
    // 批处理：SewingLogViewer <sewing.log> --pdf <report.pdf>  不显示窗口，直接导出完整报告
    const int pdfAt = args.indexOf(QStringLiteral("--pdf"));
    if (pdfAt > 1 && pdfAt + 1 < args.size()) {
        if (!w.loadFile(args.at(1))) return 1;
        return w.exportReportPdf(args.at(pdfAt + 1)) ? 0 : 2;
    }

    w.show();
    // 支持把 .log 拖到 exe 图标上 / 命令行传入路径
    if (args.size() > 1) w.loadFile(args.at(1));

    return app.exec();
}
