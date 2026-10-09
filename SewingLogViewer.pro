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
    mainwindow.cpp

HEADERS += \
    logparser.h \
    piecechart.h \
    plotwidget.h \
    headerview.h \
    mainwindow.h
