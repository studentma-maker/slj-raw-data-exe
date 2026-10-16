#include "mainwindow.h"

#include <QApplication>
#include <QIcon>

#include <cstdio>

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon.png")));

    const QStringList args = app.arguments();

    MainWindow w;

    // 命令行里的 .csv 一律按皮带轮廓打开（可多个），其余第一个按 sewing.log 打开
    QStringList csvs;
    QString log;
    for (int i = 1; i < args.size(); ++i) {
        const QString &a = args.at(i);
        if (a == QStringLiteral("--remove-roller")) continue;
        if (a.startsWith(QStringLiteral("--"))) { ++i; continue; }   // --pdf/--profile-png 等的参数
        if (a.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)) csvs << a;
        else if (log.isEmpty()) log = a;
    }

    // 批处理：SewingLogViewer a.csv b.csv ... --profile-png <out.png>  不显示窗口，导出轮廓对比图
    const int pngAt = args.indexOf(QStringLiteral("--profile-png"));
    if (pngAt > 0 && pngAt + 1 < args.size()) {
        QString err;
        if (!w.loadProfiles(csvs, true, &err)) {
            fprintf(stderr, "%s\n", qPrintable(err.isEmpty() ? QStringLiteral("no profile loaded") : err));
            return 1;
        }
        return w.exportProfilePng(args.at(pngAt + 1)) ? 0 : 2;
    }
    // 批处理：SewingLogViewer a.csv b.csv ... --profile-analysis-png <out.png> [--remove-roller]  导出分析页
    const int anaAt = args.indexOf(QStringLiteral("--profile-analysis-png"));
    if (anaAt > 0 && anaAt + 1 < args.size()) {
        QString err;
        if (!w.loadProfiles(csvs, true, &err)) {
            fprintf(stderr, "%s\n", qPrintable(err.isEmpty() ? QStringLiteral("no profile loaded") : err));
            return 1;
        }
        if (args.contains(QStringLiteral("--remove-roller"))) w.setProfileRemoveRoller(true);
        return w.exportProfileAnalysisPng(args.at(anaAt + 1)) ? 0 : 2;
    }

    // 批处理：SewingLogViewer <sewing.log> --pdf <report.pdf>  不显示窗口，直接导出完整报告
    const int pdfAt = args.indexOf(QStringLiteral("--pdf"));
    if (pdfAt > 1 && pdfAt + 1 < args.size()) {
        QString err;
        if (!w.loadFile(args.at(1), true, &err)) {
            fprintf(stderr, "%s: %s\n", qPrintable(args.at(1)), qPrintable(err));
            return 1;
        }
        return w.exportReportPdf(args.at(pdfAt + 1)) ? 0 : 2;
    }

    w.show();
    // 支持把 .log / .csv 拖到 exe 图标上 / 命令行传入路径
    if (!log.isEmpty()) w.loadFile(log);
    if (!csvs.isEmpty()) w.loadProfiles(csvs);

    return app.exec();
}
