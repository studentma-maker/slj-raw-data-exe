#include "curvehelp.h"

namespace {

// 色块 + 名称：说明文字里用来对照曲线颜色（虚线用两段短线表示）
QString swatch(const char *color, bool dashed = false)
{
    if (dashed)
        return QStringLiteral("<span style='color:%1;font-weight:bold'>╍╍</span>").arg(QLatin1String(color));
    return QStringLiteral("<span style='color:%1;font-weight:bold'>━━</span>").arg(QLatin1String(color));
}

} // namespace

QString curveHelpHtml()
{
    QString h;
    h += QStringLiteral(
        "<html><body style='font-size:14px;line-height:1.55;color:#212529'>"
        "<h2>单片曲线说明</h2>"
        "<p>一片 = 从料头过 ch1（CH1_RISE）到本片缝纫结束（STOPPING→IDLE）。"
        "横轴是<b>皮带脉冲增量</b>（物料前进的距离，下方附估算 mm），不是时间；左右两端标注起止时刻。"
        "每个点是一行 TICK（约 50ms 一行）。</p>");

    // ── 厚度面板 ──
    h += QStringLiteral("<h3>一、厚度面板（最上方）</h3><table cellpadding='6' cellspacing='0' border='1' "
                        "style='border-collapse:collapse;border-color:#dee2e6'>"
                        "<tr style='background:#f1f3f5'><th>线</th><th>日志字段 / 公式</th><th>含义</th></tr>");
    h += QStringLiteral("<tr><td>%1 紫</td><td>基线 − lraw</td><td>用激光<b>原始</b>读数算的厚度：没滤波、没皮带补偿，噪声最大。"
                        "只在本片有基线时画（标题栏“基线 xx mm”）。</td></tr>").arg(swatch("#ae3ec9"));
    h += QStringLiteral("<tr><td>%1 蓝</td><td>thick = 基线 − lflt</td><td><b>未补偿</b>厚度：有滤波、没皮带补偿。皮带补偿不可用的行（日志 lk=t）以及旧版日志，查表用的是它。"
                        "空带段会跟着皮带厚薄起伏（峰谷约 0.3mm）。</td></tr>").arg(swatch("#1c7ed6"));
    h += QStringLiteral("<tr><td>%1 绿</td><td>bthick = (基线 − 基线处偏差) − (lflt − 当前偏差)</td>"
                        "<td><b>皮带补偿后厚度</b>：现场基线锚定整体高度 + 轮廓表扣掉皮带起伏。新版采集程序（HEADER blookup=1）在可用时（lk=b）用它查表。"
                        "详见下文第五节。</td></tr>").arg(swatch("#0ca678"));
    h += QStringLiteral("<tr><td>%1 黄</td><td>tthick = (表均值 + 当前偏差) − lflt</td>"
                        "<td><b>对比用</b>：完全用轮廓表的绝对值作基线，不用现场基线。整体高度漂移会原样进入厚度。"
                        "可在上方取消“仅轮廓表(黄)”隐藏。</td></tr>").arg(swatch("#f08c00"));
    h += QStringLiteral("<tr><td>%1 橙（带圆点）</td><td>WIN 行</td><td>滑动窗口平均厚度：点在窗口末端过激光的位置，"
                        "下方条带是各窗口覆盖的范围。这是查表换行实际用的值：每 2mm 一格，取各行实际参与查表的厚度（lk=b 为绿线，lk=t 为蓝线）平均。</td></tr>")
             .arg(swatch("#e8590c"));
    h += QStringLiteral("<tr><td>黑色竖线 + 浅灰底</td><td>PIECE_DONE</td><td>料尾已过激光，之后激光下没有本片物料；标签给出发料长度。</td></tr>"
                        "</table>");
    h += QStringLiteral("<p>“滤波”指激光值先做 3 点中值（去单点跳变），再做一阶低通 "
                        "lflt ← lflt + 0.35 × (中值 − lflt)；读数不在 35~65mm 的样本直接丢弃。</p>");

    // ── 激光面板 ──
    h += QStringLiteral("<h3>二、激光面板</h3><table cellpadding='6' cellspacing='0' border='1' "
                        "style='border-collapse:collapse;border-color:#dee2e6'>"
                        "<tr style='background:#f1f3f5'><th>线</th><th>含义</th></tr>");
    h += QStringLiteral("<tr><td>%1 深绿</td><td>lflt：滤波后的激光距离（越大 = 离皮带面越远 = 越薄）。</td></tr>")
             .arg(swatch("#2b8a3e"));
    h += QStringLiteral("<tr><td>%1 紫</td><td>lraw：激光原始距离。</td></tr>").arg(swatch("#ae3ec9"));
    h += QStringLiteral("<tr><td>%1 青绿虚线</td><td><b>预期无料读数</b> = 基线 − 基线处偏差 + 当前偏差，即“此处如果没料，激光应读多少”。"
                        "空带段应与 lflt 重合；形状对得上但左右错开 → 零点/位置有偏差；形状本身对不上 → 轮廓表需重新标定。"
                        "有料段两线之差就是绿线 bthick。</td></tr>").arg(swatch("#0ca678", true));
    h += QStringLiteral("</table><p>注意：深绿（lflt）和青绿（预期读数/bthick）是两种不同的绿，分属不同面板。</p>");

    // ── 其他面板 ──
    h += QStringLiteral("<h3>三、频率、位移与状态</h3><ul>");
    h += QStringLiteral("<li>%1 <b>频率面板</b>：变频器频率。勾选“频率对齐到针脚”时整条曲线前移“激光→针脚”的距离，"
                        "同一横坐标上，上面是这段物料的厚度，这里是它到针脚被缝时的频率。</li>").arg(swatch("#c2255c"));
    h += QStringLiteral("<li>%1 <b>posLaser</b>：料头过激光之后走过的距离（未到激光为空）；%2 灰线为经过时间（右轴），"
                        "越陡表示皮带越慢。</li>").arg(swatch("#0b7285"), swatch("#868e96"));
    h += QStringLiteral("<li><b>底部状态条</b>：ch1 / ch2 / ch3 光电（有料为实心）、vfd 变频器运行、state 缝纫状态机、row 当前查表行。</li>"
                        "<li><b>事件线</b>（虚线 + 顶部标签）：CH1_RISE、BASELINE、LASER_HEAD、状态切换、换行等；"
                        "红色实线为 ALARM。</li>");
    h += QStringLiteral("<li>%1 <b>皮带零点</b>（ch4 上升沿）：青色实线 + “零点#n”，下降沿为点线；相邻两次上升沿之间是皮带一整圈，"
                        "厚度/激光面板隔圈加浅色底，激光面板顶部标出每圈脉冲数。</li></ul>").arg(swatch("#1098ad"));

    // ── 绿 vs 黄 ──
    h += QStringLiteral(
        "<h3>四、绿线与黄线的区别</h3>"
        "<p>把无料时的激光读数拆成两部分：</p>"
        "<p style='margin-left:24px;font-size:15px'><b>激光(p, t) = H(t) + D(p)</b></p>"
        "<ul>"
        "<li><b>H(t) 整体高度</b>：随温度、皮带张紧、传感器安装缓慢漂移；一片料的几分钟内基本不变，但不同时间、不同天会变"
        "（实测 10-15 与 10-16 相差约 0.09mm）。</li>"
        "<li><b>D(p) 皮带轮廓偏差</b>：只和皮带位置 p 有关、平均为 0 的起伏（峰谷约 0.34mm）。"
        "就是轮廓表每格减去表均值，日志字段 <b>bdev</b>。p 来自下位机寄存器 137（ch4 黑点为零点的一圈进度），日志字段 bpos。</li>"
        "</ul>"
        "<p>有料时：<b>激光 = H(t) + D(p) − 厚度 T</b>。三种算法：</p>"
        "<table cellpadding='6' cellspacing='0' border='1' style='border-collapse:collapse;border-color:#dee2e6'>"
        "<tr style='background:#f1f3f5'><th>线</th><th>公式</th><th>展开结果</th><th>残留误差</th></tr>"
        "<tr><td>蓝 thick</td><td>基线 − lflt</td><td>T + [ D(基线处) − D(p) ]</td><td>皮带起伏之差，最大约 ±0.2mm</td></tr>"
        "<tr><td>黄 tthick</td><td>(表均值 + D(p)) − lflt</td><td>T + [ H(标定时) − H(现在) ]</td><td>整体高度漂移，约 0.1mm 量级</td></tr>"
        "<tr><td>绿 bthick</td><td>(基线 − D(基线处)) − (lflt − D(p))</td><td>T</td><td>只剩轮廓表误差（约 0.02~0.04mm）与辊筒跳动（约 ±0.04mm）</td></tr>"
        "</table>"
        "<p>所以<b>形状</b>两者用的是同一张轮廓表，区别只在<b>整体高度从哪来</b>：黄线用标定那一刻的高度，"
        "绿线用这一片料头前现场量到的高度。空带、无振动时真实厚度是 0，绿线越贴近 0 说明补偿越准；"
        "绿线与黄线之间基本恒定的差值，就是标定之后整体高度的漂移量。</p>");

    // ── 现场锚定 ──
    h += QStringLiteral(
        "<h3>五、现场基线锚定的计算过程</h3>"
        "<ol>"
        "<li><b>每个 tick 存样本</b>：武装后每 50ms 把一个激光样本放进环形缓冲，样本带着三样东西："
        "它所在的皮带里程、激光原始读数 lraw、以及该时刻皮带位置的轮廓偏差 bdev（由寄存器 137 查轮廓表得到，"
        "查不到时记为“无”）。</li>"
        "<li><b>何时取基线</b>：料头过 ch1 的那一刻（ch1 在激光上游，距离见 HEADER 的 ch1ToLaser，约 30 多 mm；此时激光看到的还是空皮带）。</li>"
        "<li><b>取哪些样本</b>：从当前位置往回翻缓冲，只要料头之前的样本；不越过上一片料尾（各留 5mm 保护距离）；"
        "最多取设置页“基线窗口”对应的样本数（窗口毫秒数 ÷ 50）；读数不在 35~65mm 的丢弃。</li>"
        "<li><b>基线</b> = 这些样本 lraw 的平均（日志 BASELINE 行 base=，样本数 n=）。少于 3 个样本时复用上一片的基线"
        "（reused=1），上一片的基线处偏差也一起复用。</li>"
        "<li><b>基线处偏差</b> = 同一批样本 bdev 的平均（BASELINE 行 bdev=）。要求<b>每一个</b>基线样本都有 bdev，"
        "缺一个就记“-”、本片不做皮带补偿（绿线不画）——否则平均值对应的不是基线实际取样的那段皮带。</li>"
        "<li><b>整体高度</b> H = 基线 − 基线处偏差：把基线里混入的“那段皮带的起伏”扣掉，剩下的就是此刻的整体高度。</li>"
        "<li><b>每个 tick 的补偿后厚度</b> bthick = H − (lflt − 当前偏差)，并钳位在 0~10mm。</li>"
        "</ol>"
        "<p><b>数值例子</b>（与实测量级一致）：基线段 lraw 平均 44.30，基线处偏差 −0.05（那段皮带略厚），"
        "所以 H = 44.30 − (−0.05) = 44.35。某时刻料下的皮带位置偏差 +0.12（这段皮带略薄），lflt = 44.25：</p>"
        "<ul>"
        "<li>蓝 thick = 44.30 − 44.25 = <b>0.05</b>（少算了 0.17 = 0.12 − (−0.05)，即两处皮带起伏之差）</li>"
        "<li>绿 bthick = 44.35 − (44.25 − 0.12) = <b>0.22</b>（真实厚度）</li>"
        "<li>黄 tthick：若标定那天表均值是 44.26，则 (44.26 + 0.12) − 44.25 = <b>0.13</b>（少算 0.09 = 整体高度漂移）</li>"
        "</ul>");

    // ── 什么时候没有绿/黄线 ──
    h += QStringLiteral(
        "<h3>六、绿线/黄线不显示或中断的原因</h3><ul>"
        "<li>本次武装没有加载轮廓表（HEADER 行 bprof=none，标题栏不显示“皮带轮廓表”）：先在设置页做皮带步数标定。</li>"
        "<li>皮带位置无效（bpos=-1）：下位机开机后还没经过 ch4 黑点，或漏检黑点超过一圈。</li>"
        "<li>只有绿线不显示：本片基线处偏差不可用（BASELINE 行 bdev=-，标题栏提示“本片不补偿”）。</li>"
        "<li>旧版本采集程序写的日志没有这些字段。</li>"
        "</ul>"
        "<p style='color:#495057'>工具栏：“原始曲线”控制紫线，“滤波后曲线”控制蓝线和深绿 lflt，"
        "“皮带补偿(绿)”控制绿线和预期无料读数虚线，“仅轮廓表(黄)”控制黄线。鼠标悬停可看到该点所有数值。</p>"
        "</body></html>");
    return h;
}
