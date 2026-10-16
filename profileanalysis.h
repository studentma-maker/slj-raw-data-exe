#pragma once

#include "beltprofile.h"

#include <QVector>

// 多组皮带激光轮廓的对比分析（曲线页与分析页共用同一份计算，保证两边数字一致）。
// 所有组先按皮带一圈进度重采样到公共网格（kProfileGridN 格），再做：
//   - 去均值：每组减去自身均值，只比较形状（不同天/温度的整体高度偏移不参与）
//   - 扣除辊筒成分（可选）：激光测点正下方是带齿从动辊，皮带一圈≈辊筒 7.83 圈，
//     每组里混着一份相位随机的辊筒跳动。按给定周期对"本组 − 平均曲线"拟合正弦并扣掉，
//     迭代几次（平均曲线本身也随之更新）；需要至少 2 组才能把辊筒和皮带分开
//   - 平均曲线、每组与平均的相关系数/均方根差/零点错位、两两相关系数与均方根差矩阵、
//     用 k 组平均做表时相对其余组的预期残差
const int kProfileGridN = 1024;

struct ProfileAnalysisOptions {
    bool   deviation     = true;
    bool   removeRoller  = false;
    double rollerPeriodMm = 103.3;
    bool   fullStats      = true;    // false：跳过零点错位与"k 组平均"统计（曲线页只要相关系数，省掉悬停重绘的开销）
};

struct ProfileAnalysis {
    QVector<int> vis;                        // 参与分析（可见）的组下标
    QVector<QVector<double>> grid;           // [k][i]：vis[k] 组在公共网格上的值（已按选项处理）
    QVector<double> mean;                    // 平均曲线
    double circMm = 0.0;                     // 估算周长（可见组 cyclePulse 平均 × mm/脉冲），未知为 0
    double circMmForFit = 0.0;               // 拟合辊筒用的周长（未知时用默认值）
    bool   rollerRemoved = false;            // 实际是否扣除了辊筒成分（少于 2 组时不扣）

    // 每组统计（下标同 vis）
    QVector<double> p2p;                     // 处理后的峰谷差
    QVector<double> rToMean;
    QVector<double> rmsToMean;
    QVector<double> shiftMm;                 // 相对平均曲线的整体错位（正 = 本组曲线比平均曲线靠后）
    QVector<double> rollerAmpMm;             // 辊筒周期正弦振幅（拟合值，无论是否扣除都给出）
    QVector<double> rollerPhaseDeg;

    // 两两矩阵（下标同 vis）
    QVector<QVector<double>> pairR;
    QVector<QVector<double>> pairRms;
    double meanPairR   = 0.0;
    double meanPairRms = 0.0;

    // avgRms[k-1]：任取 k 组平均做表，相对其余组平均的均方根差（k = 1..min(5, n-1)）
    QVector<double> avgRms;
};

ProfileAnalysis analyzeProfiles(const QVector<BeltProfile> &ps, const QVector<bool> &visible,
                                const ProfileAnalysisOptions &opt);

double profileCorr(const QVector<double> &a, const QVector<double> &b);
