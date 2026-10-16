#pragma once

#include <QString>
#include <QVector>

// 皮带激光轮廓表：上位机设置页"皮带步数标定"生成的 ~/.belt_profile.csv
// （slj_kickpi_qt_pro/worker/sewingworker.cpp 的 saveBeltProfile）。格式：
//   # belt_profile v1: ...说明...
//   # time=yyyy-MM-dd hh:mm:ss cyclePulse~N samples=N bins=N emptyBins=N
//   bin,pos,laserMm,n
//   0,0.00049,44.2025,4
//   ...
// pos = 皮带一圈进度(0~1，格子中心)，laserMm = 该格无料时激光原始读数均值，n = 样本数(0=插值补的)
struct BeltProfile {
    QString path;
    QString name;              // 文件名（不含目录）
    QString time;              // 采集时间（# time= 行），没有则为空
    qint64  cyclePulse = -1;   // 上位机估算的一圈步数（# cyclePulse~），没有为 -1
    int     samples    = -1;
    int     emptyBins  = 0;    // n=0 的格子数

    QVector<double> pos;
    QVector<double> laser;
    QVector<int>    n;

    double mean   = 0.0;
    double p2p    = 0.0;
    double stddev = 0.0;
};

bool parseBeltProfile(const QString &path, BeltProfile &out, QString *error);

// 在 pos∈[0,1) 处按环形线性插值取值（profile 已按 pos 升序）
double beltProfileAt(const BeltProfile &p, double pos);
