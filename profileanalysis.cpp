#include "profileanalysis.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>

namespace {

const double kMmPerPulse      = 0.003563;   // 上位机默认 mm/脉冲（只用于 mm 估算）
const double kDefaultCircMm   = 809.1;      // 周长未知时拟合辊筒用的默认值（227091 步 × 0.003563）
const int    kRollerIterations = 3;
const int    kShiftSearchBins = 30;

double rms(const QVector<double> &a, const QVector<double> &b)
{
    double s = 0.0;
    for (int i = 0; i < a.size(); ++i) s += (a[i] - b[i]) * (a[i] - b[i]);
    return a.isEmpty() ? 0.0 : std::sqrt(s / a.size());
}

QVector<double> meanOf(const QVector<QVector<double>> &g, const QVector<int> &rows)
{
    QVector<double> m(kProfileGridN, 0.0);
    if (rows.isEmpty()) return m;
    for (int r : rows)
        for (int i = 0; i < kProfileGridN; ++i) m[i] += g[r][i];
    for (double &v : m) v /= rows.size();
    return m;
}

// 对 y 拟合 a·cos(wx)+b·sin(wx)（最小二乘，x 为 mm），返回振幅/相位，fitOut 为拟合曲线
void fitSine(const QVector<double> &y, double circMm, double periodMm, double &amp, double &phaseDeg,
             QVector<double> *fitOut)
{
    const double w = 2.0 * M_PI / periodMm;
    double cc = 0, ss = 0, cs = 0, yc = 0, ys = 0;
    for (int i = 0; i < kProfileGridN; ++i) {
        const double x = (i + 0.5) / kProfileGridN * circMm;
        const double c = std::cos(w * x), s = std::sin(w * x);
        cc += c * c; ss += s * s; cs += c * s;
        yc += y[i] * c; ys += y[i] * s;
    }
    const double det = cc * ss - cs * cs;
    double a = 0, b = 0;
    if (std::fabs(det) > 1e-12) {
        a = (yc * ss - ys * cs) / det;
        b = (ys * cc - yc * cs) / det;
    }
    amp = std::hypot(a, b);
    phaseDeg = std::atan2(b, a) * 180.0 / M_PI;
    if (fitOut) {
        fitOut->resize(kProfileGridN);
        for (int i = 0; i < kProfileGridN; ++i) {
            const double x = (i + 0.5) / kProfileGridN * circMm;
            (*fitOut)[i] = a * std::cos(w * x) + b * std::sin(w * x);
        }
    }
}

// b 相对 a 的环形错位（格，带抛物线亚格插值）：b 右移 shift 格后与 a 最吻合
double bestShift(const QVector<double> &a, const QVector<double> &b)
{
    const int n = a.size();
    QVector<double> rs;
    QVector<double> rolled(n);
    for (int s = -kShiftSearchBins; s <= kShiftSearchBins; ++s) {
        for (int i = 0; i < n; ++i) rolled[i] = b[((i + s) % n + n) % n];
        rs.append(profileCorr(a, rolled));
    }
    const int k = int(std::max_element(rs.begin(), rs.end()) - rs.begin());
    double off = 0.0;
    if (k > 0 && k < rs.size() - 1) {
        const double y0 = rs[k - 1], y1 = rs[k], y2 = rs[k + 1];
        const double den = y0 - 2 * y1 + y2;
        if (std::fabs(den) > 1e-12) off = 0.5 * (y0 - y2) / den;
    }
    return (k - kShiftSearchBins) + off;
}

} // namespace

double profileCorr(const QVector<double> &a, const QVector<double> &b)
{
    const int n = std::min(a.size(), b.size());
    if (n < 2) return std::numeric_limits<double>::quiet_NaN();
    double ma = 0, mb = 0;
    for (int i = 0; i < n; ++i) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    double sab = 0, saa = 0, sbb = 0;
    for (int i = 0; i < n; ++i) {
        sab += (a[i] - ma) * (b[i] - mb);
        saa += (a[i] - ma) * (a[i] - ma);
        sbb += (b[i] - mb) * (b[i] - mb);
    }
    return (saa > 0 && sbb > 0) ? sab / std::sqrt(saa * sbb) : std::numeric_limits<double>::quiet_NaN();
}

ProfileAnalysis analyzeProfiles(const QVector<BeltProfile> &ps, const QVector<bool> &visible,
                                const ProfileAnalysisOptions &opt)
{
    ProfileAnalysis an;
    double cycSum = 0;
    int cycN = 0;
    for (int i = 0; i < ps.size(); ++i) {
        if (i < visible.size() && !visible[i]) continue;
        an.vis.append(i);
        QVector<double> g(kProfileGridN);
        for (int k = 0; k < kProfileGridN; ++k) {
            const double v = beltProfileAt(ps[i], (k + 0.5) / kProfileGridN);
            g[k] = opt.deviation ? v - ps[i].mean : v;
        }
        an.grid.append(g);
        if (ps[i].cyclePulse > 0) { cycSum += ps[i].cyclePulse; ++cycN; }
    }
    if (cycN > 0) an.circMm = cycSum / cycN * kMmPerPulse;
    an.circMmForFit = an.circMm > 0 ? an.circMm : kDefaultCircMm;
    const int n = an.grid.size();
    if (n == 0) return an;

    QVector<int> all(n);
    std::iota(all.begin(), all.end(), 0);
    an.mean = meanOf(an.grid, all);

    // 辊筒周期正弦：对"本组 − 平均曲线"拟合（平均曲线里辊筒相位随机、已基本抵消）
    an.rollerAmpMm.resize(n);
    an.rollerPhaseDeg.resize(n);
    const bool canRemove = opt.removeRoller && n >= 2 && opt.rollerPeriodMm > 1.0;
    const int iters = canRemove ? kRollerIterations : 1;
    QVector<QVector<double>> raw = an.grid;
    for (int it = 0; it < iters; ++it) {
        QVector<double> diff(kProfileGridN), fit;
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < kProfileGridN; ++i) diff[i] = raw[k][i] - an.mean[i];
            fitSine(diff, an.circMmForFit, std::max(1.0, opt.rollerPeriodMm), an.rollerAmpMm[k], an.rollerPhaseDeg[k],
                    canRemove ? &fit : nullptr);
            if (canRemove)
                for (int i = 0; i < kProfileGridN; ++i) an.grid[k][i] = raw[k][i] - fit[i];
        }
        if (canRemove) an.mean = meanOf(an.grid, all);
    }
    an.rollerRemoved = canRemove;

    // 每组统计
    for (int k = 0; k < n; ++k) {
        const auto mm = std::minmax_element(an.grid[k].begin(), an.grid[k].end());
        an.p2p.append(*mm.second - *mm.first);
        an.rToMean.append(n >= 2 ? profileCorr(an.grid[k], an.mean) : 1.0);
        an.rmsToMean.append(n >= 2 ? rms(an.grid[k], an.mean) : 0.0);
        an.shiftMm.append(n >= 2 && opt.fullStats ? bestShift(an.mean, an.grid[k]) * an.circMmForFit / kProfileGridN : 0.0);
    }

    // 两两矩阵（去均值后算 rms：整体高度差不计入形状差异）
    an.pairR   = QVector<QVector<double>>(n, QVector<double>(n, 1.0));
    an.pairRms = QVector<QVector<double>>(n, QVector<double>(n, 0.0));
    double sumR = 0, sumRms = 0;
    int pairs = 0;
    for (int a = 0; a < n; ++a) {
        for (int b = a + 1; b < n; ++b) {
            QVector<double> da = an.grid[a], db = an.grid[b];
            const double ma = std::accumulate(da.begin(), da.end(), 0.0) / kProfileGridN;
            const double mb = std::accumulate(db.begin(), db.end(), 0.0) / kProfileGridN;
            for (double &v : da) v -= ma;
            for (double &v : db) v -= mb;
            const double r = profileCorr(da, db);
            const double d = rms(da, db);
            an.pairR[a][b] = an.pairR[b][a] = r;
            an.pairRms[a][b] = an.pairRms[b][a] = d;
            sumR += r; sumRms += d; ++pairs;
        }
    }
    if (pairs > 0) { an.meanPairR = sumR / pairs; an.meanPairRms = sumRms / pairs; }

    // k 组平均做表 vs 其余组平均（固定种子，结果可复现）
    std::mt19937 rng(1);
    const int kMax = opt.fullStats ? std::min(5, n - 1) : 0;
    for (int k = 1; k <= kMax; ++k) {
        double sum = 0;
        const int trials = 200;
        QVector<int> perm = all;
        for (int t = 0; t < trials; ++t) {
            std::shuffle(perm.begin(), perm.end(), rng);
            const QVector<double> a = meanOf(an.grid, perm.mid(0, k));
            const QVector<double> b = meanOf(an.grid, perm.mid(k));
            QVector<double> da = a, db = b;
            const double ma = std::accumulate(da.begin(), da.end(), 0.0) / kProfileGridN;
            const double mb = std::accumulate(db.begin(), db.end(), 0.0) / kProfileGridN;
            for (double &v : da) v -= ma;
            for (double &v : db) v -= mb;
            sum += rms(da, db);
        }
        an.avgRms.append(sum / trials);
    }
    return an;
}
