#include "logparser.h"

#include <QDate>
#include <QFile>
#include <QHash>
#include <QList>

#include <algorithm>

namespace {

const qint64 MS_PER_DAY = 86400000LL;
const int    MAX_WIN_LANES = 12;

const char *const kStateNames[ST_COUNT] = {"IDLE", "WAIT_T1", "V1", "V2", "WAIT_T3", "V3", "V4", "V5", "STOPPING"};

int stateFromName(const QByteArray &n)
{
    for (int i = 0; i < ST_COUNT; ++i)
        if (n == kStateNames[i]) return i;
    return ST_IDLE;
}

// "[2026-09-23 16:07:19.084] " → ms；失败返回 -1
qint64 parseTimestamp(const QByteArray &line)
{
    if (line.size() < 26 || line[0] != '[' || line[24] != ']') return -1;
    static QByteArray cachedDate;
    static qint64     cachedDayMs = 0;
    const QByteArray date = line.mid(1, 10);
    if (date != cachedDate) {
        const QDate d(date.mid(0, 4).toInt(), date.mid(5, 2).toInt(), date.mid(8, 2).toInt());
        if (!d.isValid()) return -1;
        cachedDate  = date;
        cachedDayMs = d.toJulianDay() * MS_PER_DAY;
    }
    const int hh = line.mid(12, 2).toInt();
    const int mm = line.mid(15, 2).toInt();
    const int ss = line.mid(18, 2).toInt();
    const int zz = line.mid(21, 3).toInt();
    return cachedDayMs + ((hh * 60LL + mm) * 60LL + ss) * 1000LL + zz;
}

// "k=v k=v ..." → 表（值里不含空格的简单行用）
QHash<QByteArray, QByteArray> keyValues(const QByteArray &body)
{
    QHash<QByteArray, QByteArray> kv;
    const QList<QByteArray> toks = body.split(' ');
    for (const QByteArray &t : toks) {
        const int eq = t.indexOf('=');
        if (eq > 0) kv.insert(t.left(eq), t.mid(eq + 1));
    }
    return kv;
}

bool parseTick(const QByteArray &body, Tick &t)
{
    int found = 0;
    const QList<QByteArray> toks = body.split(' ');
    for (const QByteArray &tok : toks) {
        const int eq = tok.indexOf('=');
        if (eq <= 0) continue;
        const QByteArray k = tok.left(eq);
        const QByteArray v = tok.mid(eq + 1);
        ++found;
        if      (k == "lraw")     t.lraw = v.toFloat();
        else if (k == "lflt")     t.lflt = v.toFloat();
        else if (k == "thick")    t.thick = v.toFloat();
        else if (k == "posLaser") t.posLaser = v.toFloat();
        else if (k == "ch1")      t.ch1 = (quint8)v.toInt();
        else if (k == "ch2")      t.ch2 = (quint8)v.toInt();
        else if (k == "ch3")      t.ch3 = (quint8)v.toInt();
        else if (k == "state")    t.state = (quint8)stateFromName(v);
        else if (k == "vfd")      t.vfd = (quint8)v.toInt();
        else if (k == "freq")     t.freq = v.toFloat();
        else if (k == "row")      t.row = (qint16)v.toInt();
        else if (k == "pulse")    t.pulse = v.toUInt();
        else --found;
    }
    return found >= 6;
}

// rows=[0:en=1 0.00-0.00mm v1=40.0 v2=39.0 v3=39.0 v4=37.0 v5=29.0][1:...]...
void parseRows(const QByteArray &rowsStr, QVector<LookupRow> &rows)
{
    int pos = 0;
    while (true) {
        const int a = rowsStr.indexOf('[', pos);
        if (a < 0) break;
        const int b = rowsStr.indexOf(']', a);
        if (b < 0) break;
        pos = b + 1;
        const QByteArray item = rowsStr.mid(a + 1, b - a - 1);
        const int colon = item.indexOf(':');
        if (colon <= 0) continue;
        LookupRow r;
        r.idx = item.left(colon).toInt();
        const QList<QByteArray> toks = item.mid(colon + 1).split(' ');
        for (const QByteArray &t : toks) {
            if (t.startsWith("en=")) {
                r.enabled = t.mid(3).toInt() != 0;
            } else if (t.endsWith("mm")) {
                // "0.51-0.70mm"：范围值非负，用第一个 '-' 分割即可
                const QByteArray range = t.left(t.size() - 2);
                const int dash = range.indexOf('-', 1);
                if (dash > 0) {
                    r.tmin = range.left(dash).toDouble();
                    r.tmax = range.mid(dash + 1).toDouble();
                }
            } else if (t.size() > 3 && t[0] == 'v' && t[2] == '=') {
                const int vi = t[1] - '1';
                if (vi >= 0 && vi < 5) r.v[vi] = t.mid(3).toDouble();
            }
        }
        rows.append(r);
    }
}

void parseHeader(const QByteArray &body, HeaderInfo &h)
{
    const int rowsAt = body.indexOf(" rows=");
    const QByteArray head = rowsAt >= 0 ? body.left(rowsAt) : body;
    const QHash<QByteArray, QByteArray> kv = keyValues(head);
    h.ch3ToCh1    = kv.value("ch3ToCh1").toDouble();
    h.ch1ToLaser  = kv.value("ch1ToLaser").toDouble();
    h.laserToCh2  = kv.value("laserToCh2").toDouble();
    h.ch2ToNeedle = kv.value("ch2ToNeedle").toDouble();
    h.stackDist   = kv.value("stackDist").toDouble();
    h.segLen      = kv.value("segLen").toDouble();
    h.paifaHi     = kv.value("paifaHi").toInt();
    h.paifaLo     = kv.value("paifaLo").toInt();
    h.songfaHi    = kv.value("songfaHi").toInt();
    h.songfaLo    = kv.value("songfaLo").toInt();
    if (rowsAt >= 0) parseRows(body.mid(rowsAt + 6), h.rows);
    h.paramKey = QString::fromLatin1(body);
}

// 由 TICK.posLaser 回推每个窗口的起点时刻，并给有交叠的窗口分配错开的泳道
void finalizeWindows(Piece &p, double segLen)
{
    if (segLen <= 0.0) segLen = 20.0;
    const QVector<Tick> &tk = p.ticks;
    for (WinRec &w : p.wins) {
        w.startMs = w.ms;
        if (tk.isEmpty()) continue;
        // 窗口产出时刻对应的 tick（WIN 行先于同一 tick 的 TICK 行写出，留 5ms 余量）
        auto it = std::upper_bound(tk.begin(), tk.end(), w.ms + 5,
                                   [](qint64 v, const Tick &t) { return v < t.ms; });
        int i = int(it - tk.begin()) - 1;
        // 料尾过激光那一刻产出的窗口，同 tick 的 posLaser 已是 -1：往回找最近的有效值
        int guard = 40;
        while (i >= 0 && tk[i].posLaser < 0.0f && guard-- > 0) --i;
        if (i < 0 || tk[i].posLaser < 0.0f) continue;
        const double target = std::max(0.0, (double)tk[i].posLaser - segLen);
        while (i > 0 && tk[i - 1].posLaser >= 0.0f && tk[i - 1].posLaser >= target) --i;
        w.startMs = std::min(w.ms, tk[i].ms);
    }

    QVector<qint64> laneEnd;
    for (WinRec &w : p.wins) {
        int lane = -1;
        for (int k = 0; k < laneEnd.size(); ++k) {
            if (laneEnd[k] <= w.startMs) { lane = k; break; }
        }
        if (lane < 0) {
            if (laneEnd.size() < MAX_WIN_LANES) {
                laneEnd.append(0);
                lane = laneEnd.size() - 1;
            } else {
                lane = int(std::min_element(laneEnd.begin(), laneEnd.end()) - laneEnd.begin());
            }
        }
        laneEnd[lane] = w.ms;
        w.lane = lane;
    }
    p.winLanes = laneEnd.size();
}

// 累计脉冲横坐标 + mm/脉冲估算
void finalizeDistance(Piece &p)
{
    const QVector<Tick> &tk = p.ticks;
    p.xs.resize(tk.size());
    double acc = 0.0;
    for (int i = 0; i < tk.size(); ++i) {
        if (i > 0) {
            const quint32 dp = tk[i].pulse - tk[i - 1].pulse;   // 无符号差：计数回绕无妨
            const qint64 dt = std::max<qint64>(1, tk[i].ms - tk[i - 1].ms);
            // 上限同源码 BELT_PULSE_RESET 判据的量级(8kHz)，放宽到 5 倍，超出视为计数复位
            if ((double)dp <= (double)dt * 8.0 * 5.0 + 200.0) acc += (double)dp;
        }
        p.xs[i] = acc;
    }
    // posLaser(mm) 与脉冲同步增长：取有效区间首尾求斜率
    int a = -1, b = -1;
    for (int i = 0; i < tk.size(); ++i) {
        if (tk[i].posLaser < 0.0f) { if (a >= 0) break; continue; }
        if (a < 0) a = i;
        b = i;
    }
    p.laserHeadTick = a;
    p.laserTailTick = b;
    if (a >= 0 && b > a && p.xs[b] - p.xs[a] > 100.0) {
        const double k = ((double)tk[b].posLaser - (double)tk[a].posLaser) / (p.xs[b] - p.xs[a]);
        if (k > 1e-5 && k < 1.0) p.mmPerPulse = k;
    }
}

} // namespace

const char *sewStateName(int s)
{
    return (s >= 0 && s < ST_COUNT) ? kStateNames[s] : "?";
}

QString fmtClock(qint64 ms, bool withDate)
{
    const qint64 day = ms / MS_PER_DAY;
    const qint64 r   = ms % MS_PER_DAY;
    const QString t = QString("%1:%2:%3.%4")
                          .arg(r / 3600000, 2, 10, QChar('0'))
                          .arg((r / 60000) % 60, 2, 10, QChar('0'))
                          .arg((r / 1000) % 60, 2, 10, QChar('0'))
                          .arg(r % 1000, 3, 10, QChar('0'));
    if (!withDate) return t;
    return QDate::fromJulianDay(day).toString("yyyy-MM-dd") + " " + t;
}

QString alarmExplain(const QString &type)
{
    if (type == "BELT_PULSE_RESET")
        return QStringLiteral("相邻两个 tick 的皮带脉冲增量超过上限(dt×8kHz×2)，按下位机计数重启处理：本 tick 皮带位移记 0 并重设基准");
    if (type == "LINE_BROKEN")
        return QStringLiteral("1号机断线传感器上升沿(仅 V1~V5 检测)：停止自动上料 + 急停缝纫，现场清空");
    if (type == "LINE_BROKEN_PAUSE")
        return QStringLiteral("2/3号机断线：硬件急停但保留现场(状态机/测厚 FIFO)，等待接线后继续");
    if (type == "LINE_BROKEN_RESUME")
        return QStringLiteral("断线暂停后按“开始缝纫”，从断线处恢复");
    if (type == "LINE_BROKEN_CANCEL")
        return QStringLiteral("断线暂停期间按了停止/急停，放弃恢复");
    if (type == "BASELINE_REUSED")
        return QStringLiteral("料头到激光前 1mm 取基线时背景样本不足(<3 个)，复用上一片基线");
    if (type == "BASELINE_CAPTURE_FAILED")
        return QStringLiteral("基线样本不足且没有上一片基线可复用：本片放弃测厚(不产生 WIN/PIECE_DONE)");
    if (type == "FIFO_FULL")
        return QStringLiteral("测厚记录 FIFO(512 条)已满，丢弃最早一条；程序运行期间只报一次");
    return QString();
}

bool parseLogFile(const QString &path, LogData &out, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = f.errorString();
        return false;
    }

    out = LogData();
    out.path = path;

    QVector<int>    open;        // 正在记录 TICK 的片（先进先出，对应源码 m_tickLogPieces 计数）
    QHash<int, int> seqToPiece;  // 日志 piece 序号 → pieces 下标

    auto closePiece = [&](int idx, const QString &reason) {
        Piece &p = out.pieces[idx];
        qint64 end = p.startMs;
        if (!p.ticks.isEmpty()) end = std::max(end, p.ticks.last().ms);
        if (!p.events.isEmpty()) end = std::max(end, p.events.last().ms);
        p.endMs = end;
        bool lineBroken = false;
        for (const LogEvent &e : p.events)
            if (e.kind == EvAlarm && e.detail.contains("type=LINE_BROKEN")) lineBroken = true;
        p.endReason = lineBroken ? QStringLiteral("断线急停，本片未缝完") : reason;
    };
    auto pieceForSeq = [&](int seq, bool mayAssign) -> int {
        const int known = seqToPiece.value(seq, -1);
        if (known >= 0 && (!mayAssign || open.contains(known))) return known;
        if (!mayAssign) return -1;
        for (int idx : open) {
            if (out.pieces[idx].seq < 0) {
                out.pieces[idx].seq = seq;
                seqToPiece.insert(seq, idx);
                return idx;
            }
        }
        return -1;
    };
    auto addEvent = [&](qint64 ms, int kind, const QString &label, const QByteArray &body) {
        LogEvent e;
        e.ms = ms;
        e.kind = kind;
        e.label = label;
        e.detail = QString::fromLatin1(body);
        for (int idx : open) out.pieces[idx].events.append(e);
    };

    while (!f.atEnd()) {
        QByteArray line = f.readLine().trimmed();
        if (line.isEmpty()) continue;
        ++out.lineCount;

        const qint64 ms = parseTimestamp(line);
        if (ms < 0) { ++out.badLines; continue; }
        const QByteArray body = line.mid(26);
        const int sp = body.indexOf(' ');
        const QByteArray type = sp > 0 ? body.left(sp) : body;
        const QByteArray rest = sp > 0 ? body.mid(sp + 1) : QByteArray();

        if (type == "TICK") {
            Tick t;
            t.ms = ms;
            if (!parseTick(rest, t)) { ++out.badLines; continue; }
            ++out.tickCount;
            for (int idx : open) out.pieces[idx].ticks.append(t);
        } else if (type == "WIN") {
            const auto kv = keyValues(rest);
            WinRec w;
            w.ms   = ms;
            w.win  = kv.value("win").toInt();
            w.mean = kv.value("mean").toDouble();
            w.row  = kv.value("row").toInt();
            ++out.winCount;
            const int idx = pieceForSeq(kv.value("piece").toInt(), false);
            if (idx >= 0) out.pieces[idx].wins.append(w);
        } else if (type == "HEADER") {
            // 重新武装：源码此时把 TICK 计数清零，之前没缝完的片到此为止
            for (int idx : open)
                closePiece(idx, QStringLiteral("停止时已解除武装，STOPPING→IDLE / CURTAIN_DONE 未写入日志"));
            open.clear();
            HeaderInfo h;
            h.ms = ms;
            h.lineNo = out.lineCount;
            parseHeader(rest, h);
            h.changed = !out.headers.isEmpty() && out.headers.last().paramKey != h.paramKey;
            out.headers.append(h);
        } else if (type == "EDGE") {
            const auto kv = keyValues(rest);
            const QByteArray ev = kv.value("ev");
            if (ev == "CH1_RISE") {
                Piece p;
                p.startMs = ms;
                p.headerIdx = out.headers.size() - 1;
                out.pieces.append(p);
                const int idx = out.pieces.size() - 1;
                if (p.headerIdx >= 0) out.headers[p.headerIdx].pieces.append(idx);
                open.append(idx);
            } else if (ev == "BASELINE" || ev == "LASER_HEAD") {
                const int idx = pieceForSeq(kv.value("piece").toInt(), true);
                if (idx >= 0) {
                    Piece &p = out.pieces[idx];
                    p.baseMm = kv.value("base").toDouble();
                    p.baseN  = kv.value("n").toInt();
                    p.baseReused = kv.contains("reused");
                }
            }
            addEvent(ms, EvEdge, QString::fromLatin1(ev), body);
        } else if (type == "STATE") {
            const auto kv = keyValues(rest);
            addEvent(ms, EvState, QStringLiteral("→") + QString::fromLatin1(kv.value("to")), body);
        } else if (type == "ROWSWITCH") {
            const auto kv = keyValues(rest);
            addEvent(ms, EvRowSwitch,
                     QStringLiteral("行%1→%2").arg(QString::fromLatin1(kv.value("from")), QString::fromLatin1(kv.value("to"))),
                     body);
        } else if (type == "ALARM") {
            AlarmRec a;
            a.ms = ms;
            const int msgAt = rest.indexOf(" msg=");
            const QByteArray typePart = msgAt >= 0 ? rest.left(msgAt) : rest;
            a.type = QString::fromLatin1(typePart.startsWith("type=") ? typePart.mid(5) : typePart);
            if (msgAt >= 0) a.msg = QString::fromLatin1(rest.mid(msgAt + 5));
            a.pieceIdx  = open.isEmpty() ? -1 : open.first();
            a.headerIdx = out.headers.size() - 1;
            out.alarms.append(a);
            addEvent(ms, EvAlarm, QStringLiteral("⚠ ") + a.type, body);
            if (a.type == "BASELINE_CAPTURE_FAILED") {
                // 这一片不会再有 BASELINE/LASER_HEAD，避免后面片的序号错配到它身上
                for (int idx : open) {
                    if (out.pieces[idx].seq < 0) { out.pieces[idx].seq = -2; break; }
                }
            }
        } else if (type == "PIECE_DONE") {
            const auto kv = keyValues(rest);
            const int idx = pieceForSeq(kv.value("seq").toInt(), false);
            if (idx >= 0) {
                Piece &p = out.pieces[idx];
                p.lenMm  = kv.value("lenMm").toDouble();
                p.minThk = kv.value("minThk").toDouble();
                p.maxThk = kv.value("maxThk").toDouble();
                p.avgThk = kv.value("avgThk").toDouble();
            }
            addEvent(ms, EvPieceDone, QStringLiteral("PIECE_DONE"), body);
        } else if (type == "CURTAIN_DONE") {
            addEvent(ms, EvCurtainDone, QStringLiteral("CURTAIN_DONE"), body);
            if (!open.isEmpty()) {
                const int idx = open.takeFirst();
                Piece &p = out.pieces[idx];
                p.curtainMm = keyValues(rest).value("lenMm").toDouble();
                p.complete  = true;
                p.endMs     = ms;
            }
        } else {
            ++out.badLines;
        }
    }

    for (int idx : open) closePiece(idx, QStringLiteral("日志在本片缝完之前结束"));

    for (Piece &p : out.pieces) {
        const double segLen = p.headerIdx >= 0 ? out.headers[p.headerIdx].segLen : 20.0;
        finalizeWindows(p, segLen);
        finalizeDistance(p);
    }

    if (out.lineCount == 0 || (out.headers.isEmpty() && out.pieces.isEmpty() && out.tickCount == 0)) {
        if (error) *error = QStringLiteral("文件中没有识别到 sewing.log 格式的内容");
        return false;
    }
    return true;
}
