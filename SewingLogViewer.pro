QT += core gui widgets

TARGET = SewingLogViewer
TEMPLATE = app
CONFIG += c++17

# 源码为 UTF-8（含中文字符串），MSVC 需显式指定
msvc: QMAKE_CXXFLAGS += /utf-8

DEFINES += QT_DEPRECATED_WARNINGS

SOURCES += \
    main.cpp \
    logparser.cpp \
    piecechart.cpp \
    plotwidget.cpp \
    headerview.cpp \
    mainwindow.cpp \
    beltprofile.cpp \
    profileanalysis.cpp \
    profilewidget.cpp \
    analysispage.cpp \
    curvehelp.cpp

HEADERS += \
    logparser.h \
    piecechart.h \
    plotwidget.h \
    headerview.h \
    mainwindow.h \
    beltprofile.h \
    profileanalysis.h \
    profilewidget.h \
    analysispage.h \
    curvehelp.h

# 应用图标：RC_ICONS 设置 Windows 下 exe 文件图标；窗口/任务栏图标由 main.cpp 从资源加载
RESOURCES += resources.qrc
win32: RC_ICONS = icons/app_icon.ico
