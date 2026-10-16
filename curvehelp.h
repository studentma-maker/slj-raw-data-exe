#pragma once

#include <QString>

// sewing.log 单片曲线页的"曲线说明"子页面内容（HTML，显示在 QTextBrowser 里）：
// 各面板每条曲线的颜色/来源/计算方式，重点说明皮带补偿 bthick（绿）与 tthick（黄）的区别
// 以及现场基线锚定的计算过程。数值与 slj_kickpi_qt_pro/worker/sewingworker.cpp 保持一致。
QString curveHelpHtml();
