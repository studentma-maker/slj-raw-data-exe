#pragma once

#include <QString>
#include <QVector>

// sewing.log 解析结果。日志格式见 slj_kickpi_qt_pro/worker/sewingworker.cpp 的
// writeSewingLog* / logSewing* 系列函数：每行 "[yyyy-MM-dd hh:mm:ss.zzz] TYPE k=v ..."

struct LookupRow {
    int    idx     = 0;
    bool   enabled = false;
    double tmin    = 0.0;
    double tmax    = 0.0;
    double v[5]    = {0, 0, 0, 0, 0};
};

// HEADER 行：武装(开始自动缝纫)时写一次
struct HeaderInfo {
    qint64 ms          = 0;
    int    lineNo      = 0;
    double ch3ToCh1    = 0.0;
    double ch1ToLaser  = 0.0;
    double laserToCh2  = 0.0;
    double ch2ToNeedle = 0.0;
    double stackDist   = 0.0;
    double segLen      = 0.0;
    int    paifaHi     = 0;
    int    paifaLo     = 0;
    int    songfaHi    = 0;
    int    songfaLo    = 0;
    QVector<LookupRow> rows;
    QString      paramKey;          // 去掉时间戳后的原始内容，用于判断参数是否变化
    bool         changed = false;   // 与上一条 HEADER 相比参数有变化
    QVector<int> pieces;            // 本次武装期间的片（LogData::pieces 下标）
};

enum SewState { ST_IDLE, ST_WAIT_T1, ST_V1, ST_V2, ST_WAIT_T3, ST_V3, ST_V4, ST_V5, ST_STOPPING, ST_COUNT };
const char *sewStateName(int s);

struct Tick {
    qint64  ms       = 0;
    float   lraw     = 0;
    float   lflt     = 0;
    float   thick    = 0;
    float   posLaser = -1;
    float   freq     = 0;
    quint32 pulse    = 0;
    qint16  row      = 0;
    quint8  ch1 = 0, ch2 = 0, ch3 = 0, vfd = 0;
    quint8  state    = ST_IDLE;
};

enum EventKind { EvEdge, EvState, EvRowSwitch, EvAlarm, EvPieceDone, EvCurtainDone };

struct LogEvent {
    qint64  ms   = 0;
    int     kind = EvEdge;
    QString label;    // 图上显示的短标签
    QString detail;   // 原始行内容（去时间戳）
};

// WIN 行：滑动窗口平均厚度。ms = 窗口产出时刻(窗口末端过激光)，
// startMs = 由 TICK.posLaser 回推的窗口起点过激光时刻（窗口宽 = HEADER.segLen）
struct WinRec {
    qint64 ms      = 0;
    qint64 startMs = 0;
    int    win     = 0;
    int    row     = 0;
    int    lane    = 0;
    double mean    = 0.0;
};

// 一片 = 从 ch1 上升沿(CH1_RISE) 到本片缝纫结束(STOPPING→IDLE / CURTAIN_DONE)
struct Piece {
    int    seq       = -1;     // 日志里的 piece 序号（BASELINE/LASER_HEAD 行给出），未知为 -1
    int    headerIdx = -1;
    qint64 startMs   = 0;
    qint64 endMs     = 0;
    bool   complete  = false;  // 见到了 CURTAIN_DONE
    QString endReason;         // 未完整结束的原因说明

    double lenMm     = -1.0;   // PIECE_DONE.lenMm：激光处测得的发料长度
    double curtainMm = -1.0;   // CURTAIN_DONE.lenMm：编码器实测的缝纫后发帘长度
    double minThk = -1.0, maxThk = -1.0, avgThk = -1.0;
    double baseMm    = -1.0;
    int    baseN     = 0;
    bool   baseReused = false;
    int    winLanes  = 0;

    // 横轴坐标：每个 tick 相对本片第一个 tick 的累计皮带脉冲数（= 物料前进距离），单调不减；
    // 下位机计数复位造成的回跳/跳变按 0 增量处理
    QVector<double> xs;
    int    laserHeadTick = -1;   // 第一个 posLaser>=0 的 tick（料头到激光）
    int    laserTailTick = -1;   // 最后一个 posLaser>=0 的 tick（料尾过激光，之后是 PIECE_DONE）
    double mmPerPulse = 0.0;   // 由 posLaser 与脉冲的对应关系估算的 mm/脉冲，估不出为 0

    QVector<Tick>     ticks;
    QVector<LogEvent> events;
    QVector<WinRec>   wins;
};

struct AlarmRec {
    qint64  ms = 0;
    QString type;
    QString msg;
    int     pieceIdx  = -1;   // 发生时正在记录 TICK 的片（没有则 -1）
    int     headerIdx = -1;
};

struct LogData {
    QString path;
    QVector<HeaderInfo> headers;
    QVector<Piece>      pieces;
    QVector<AlarmRec>   alarms;
    int lineCount = 0;
    int tickCount = 0;
    int winCount  = 0;
    int badLines  = 0;
};

bool parseLogFile(const QString &path, LogData &out, QString *error);

QString fmtClock(qint64 ms, bool withDate = false);
QString alarmExplain(const QString &type);
