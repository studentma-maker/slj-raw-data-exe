#include "beltprofile.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

bool parseBeltProfile(const QString &path, BeltProfile &out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }
    QByteArray all = f.readAll();
    if (all.startsWith("\xEF\xBB\xBF")) all.remove(0, 3);   // Excel 另存的 UTF-8 BOM

    BeltProfile p;
    p.path = path;
    p.name = QFileInfo(path).fileName();

    // 说明行在早期版本里是乱码（板子上按 Latin-1 写出的中文），这里只取 # time= 行的 key=value
    static const QRegularExpression reTime(QStringLiteral("time=(\\S+ \\S+)"));
    static const QRegularExpression reCycle(QStringLiteral("cyclePulse~(\\d+)"));
    static const QRegularExpression reSamples(QStringLiteral("samples=(\\d+)"));

    for (QByteArray line : all.split('\n')) {
        line = line.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) {
            const QString s = QString::fromUtf8(line);
            auto m = reTime.match(s);
            if (m.hasMatch()) p.time = m.captured(1);
            m = reCycle.match(s);
            if (m.hasMatch()) p.cyclePulse = m.captured(1).toLongLong();
            m = reSamples.match(s);
            if (m.hasMatch()) p.samples = m.captured(1).toInt();
            continue;
        }
        const QList<QByteArray> cols = line.split(',');
        if (cols.size() < 3) continue;
        bool okPos = false, okVal = false;
        const double pos = cols[1].trimmed().toDouble(&okPos);
        const double val = cols[2].trimmed().toDouble(&okVal);
        if (!okPos || !okVal) continue;   // 表头 bin,pos,laserMm,n
        p.pos.append(pos);
        p.laser.append(val);
        p.n.append(cols.size() > 3 ? cols[3].trimmed().toInt() : -1);
    }

    if (p.pos.size() < 16) {
        if (error) *error = QStringLiteral("没有识别到皮带轮廓数据（需要 bin,pos,laserMm,n 格式的数据行）");
        return false;
    }

    // 按 pos 升序（正常文件本来就是升序，保险起见）
    QVector<int> idx(p.pos.size());
    for (int i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&p](int a, int b) { return p.pos[a] < p.pos[b]; });
    BeltProfile s = p;
    for (int i = 0; i < idx.size(); ++i) {
        s.pos[i]   = p.pos[idx[i]];
        s.laser[i] = p.laser[idx[i]];
        s.n[i]     = p.n[idx[i]];
    }

    double sum = 0.0, lo = s.laser.first(), hi = s.laser.first();
    for (int i = 0; i < s.laser.size(); ++i) {
        sum += s.laser[i];
        lo = std::min(lo, s.laser[i]);
        hi = std::max(hi, s.laser[i]);
        if (s.n[i] == 0) ++s.emptyBins;
    }
    s.mean = sum / s.laser.size();
    s.p2p  = hi - lo;
    double var = 0.0;
    for (double v : s.laser) var += (v - s.mean) * (v - s.mean);
    s.stddev = std::sqrt(var / s.laser.size());

    out = s;
    return true;
}

double beltProfileAt(const BeltProfile &p, double pos)
{
    const int n = p.pos.size();
    if (n == 0) return 0.0;
    pos -= std::floor(pos);
    auto it = std::upper_bound(p.pos.begin(), p.pos.end(), pos);
    const int i1 = int(it - p.pos.begin());
    const int i0 = i1 - 1;
    // 环形：首格之前接末格，末格之后接首格
    const double x0 = i0 >= 0 ? p.pos[i0] : p.pos[n - 1] - 1.0;
    const double y0 = i0 >= 0 ? p.laser[i0] : p.laser[n - 1];
    const double x1 = i1 < n ? p.pos[i1] : p.pos[0] + 1.0;
    const double y1 = i1 < n ? p.laser[i1] : p.laser[0];
    if (x1 <= x0) return y0;
    return y0 + (y1 - y0) * (pos - x0) / (x1 - x0);
}
