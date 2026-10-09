# SewingLogViewer

把 `slj_kickpi_qt_pro` 产生的 `sewing.log` 拖进窗口，生成可缩放的可视化图表，并可导出 PDF。

## 构建

只依赖 Qt Widgets（Qt 5.14+ 或 Qt 6），没有第三方库。

- Windows：用 Qt Creator 打开 `SewingLogViewer.pro`，选 MinGW 或 MSVC 套件，Release 构建；
  发布时在 exe 所在目录执行 `windeployqt SewingLogViewer.exe`。
- Linux：`mkdir build && cd build && qmake ../SewingLogViewer.pro && make -j8`

## 使用

- 拖入 `.log` 文件（或“打开日志…”，或把文件拖到 exe 图标上）。
- 左侧树：每次武装（HEADER）一个节点，点击看初始化参数；其下是这次武装期间的每一片；最后是 ALARM 列表。
- 单片曲线：横轴是皮带脉冲增量（物料前进距离），两端标注起止时间。
  滚轮缩放横轴，Ctrl+滚轮缩放光标所在面板的纵轴，Shift+滚轮平移，左键拖动平移，右键拖框放大，双击复位。
- “导出当前视图 PDF”按屏幕上当前的缩放范围矢量输出；“导出完整报告 PDF”输出参数页 + 每片全程 + ALARM 表。
- 批处理：`SewingLogViewer sewing.log --pdf report.pdf`
