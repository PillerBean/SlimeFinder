// HelpDialog.cpp —— 帮助 / 使用教程的实现（v3.3.3 任务书：面向小白的帮助教程）
//
// 设计要点：
//   1. 教程正文只写一份（helpTutorialHtml），弹窗与 GUI 自检共用，避免文案漂移；
//   2. 全文用大白话说明，不出现“位掩码 / SIMD / 线程”等术语，从使用者视角解释；
//   3. 「复制全部文字」只改写对话框内一个小标签做反馈，不弹任何模态框，
//      以免阻塞自动化自检（自检会调用 showHelpDialog 并直接关掉对话框）；
//   4. 本文件不定义 Q_OBJECT 类，故无需 include "HelpDialog.moc"。
#include "gui/HelpDialog.h"
#include "gui/UiStrings.h"

#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// 教程正文：整体用原始字符串字面量书写，便于保留缩进与排版；
// 首行声明 UTF-8，保证 Qt 富文本引擎按 UTF-8 解析中文。
QString buildTutorialHtml()
{
    static const char* const kHtml = R"HTML(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>史莱姆区块查找器 · 使用教程</title>
</head>
<body>

<h2>史莱姆区块查找器 · 使用教程</h2>
<p>这一页写给第一次使用的朋友：不用懂任何算法，按顺序读一遍就能自己找到史莱姆区块。
如果只想马上动手，直接看【怎么用】的五步，然后照着点就行。</p>

<a name="sec1"></a>
<h3>【这是什么】</h3>
<p>这是一个帮你找到史莱姆区块的小工具。史莱姆区块是游戏里能刷史莱姆的地方，找到它们就能建农场。</p>
<p>先说清楚两个词：</p>
<ul>
  <li><b>区块</b>：游戏把你的世界切成一块一块来管理，每一块是 16×16 格（水平方向）。
      出生点附近那一块就是坐标 (0, 0) 的区块。</li>
  <li><b>史莱姆区块</b>：在这些区块的<b>地下</b>（Y = 40 以下，也就是 Y ≤ 39）史莱姆就能自然刷出，
      <b>和亮度没有关系</b>（火把、亮度多少都不影响）。平均每 10 个区块里大约有 1 个是史莱姆区块
      （约 10%）。</li>
</ul>
<p>需要说明的几点（按 Minecraft Wiki「史莱姆」条目）：</p>
<ul>
  <li><b>高度</b>：史莱姆区块只在 <b>Y = 40 以下</b>生成史莱姆，Y ≥ 40 不会刷。</li>
  <li><b>亮度</b>：史莱姆区块<b>不受亮度限制</b>，插满火把照样刷。</li>
  <li><b>另一种来源</b>：在<b>沼泽 / 红树林沼泽</b>生物群系里，史莱姆还能在 <b>Y=51~69</b> 且
      <b>亮度 ≤ 7</b> 的地方生成，满月时概率最高。这跟「史莱姆区块」是两回事，本工具只算史莱姆区块。</li>
  <li><b>不生成的地方</b>：蘑菇岛、深暗之域（基岩版还有河流）不会生成史莱姆。</li>
</ul>
<p>为什么要在意它？因为只要在一个史莱姆区块里挖一个刷怪房（挖到 Y=40 以下），史莱姆就会源源不断掉下来，
黏液球、黏液块就有了稳定来源。这也是很多人做红石机器、做飞行器时的刚需材料。</p>
<p>还有一个关键区别：<b>Java 版的史莱姆区块由世界种子和区块坐标一起决定</b>（同一个种子每次开都一样），
所以必须知道种子才能算；<b>基岩版的判定算法与 Java 版不同，而且与世界种子无关</b>——
每个世界的史莱姆区块分布都一样，所以本工具在基岩版下不需要你填种子。</p>

<a name="sec2"></a>
<h3>【怎么用】</h3>

<p><b>第一步：选择游戏版本（Java版 / 基岩版）</b><br>
先在最上面的「游戏版本」里选你正在玩的那个。选错版本算出来的位置完全不同，这一步最要紧。</p>

<p><b>第二步：输入种子（Java版需要，基岩版不需要）</b><br>
Java 版请把世界种子填进「世界种子」输入框，支持负数（很长的一串数字也可以直接粘贴）。
种子可以在创建世界时的种子栏看到，也可以在游戏里用聊天指令 <code>/seed</code> 查看
（单人游戏需要先开启“允许作弊”）。基岩版这一步可以直接跳过。</p>

<p><b>第三步：选择搜索范围（点「附近/局部/全局」按钮即可）</b><br>
「附近」＝以起点为中心 ±1000 区块，「局部」＝±5000 区块，「全局」＝±30000 区块。
点一下就自动填好范围，也可以自己手动改数字。范围越大结果越多、耗时越长，
第一次使用建议先点「附近」试一下。</p>

<p><b>第四步：选择搜索模式（推荐「精确模式」）</b><br>
日常直接用默认的「精确模式」，它给出的结果是 100% 完整的，不会漏。
其余三种模式的区别见下面【四种模式怎么选】，不确定就保持精确模式。</p>

<p><b>第五步：点击「开始搜索」，等待结果</b><br>
点下按钮后进度条会走动，搜索在后台进行，界面不会卡住。
完成后右侧的<b>结果列表</b>会显示找到的区块，每条都写着<b>距离</b>（离你设置的起点有多远，按由近到远排列），
看距离就能判断“这个区块跑过去要多久”。<b>点结果卡片可以复制坐标</b>，
直接粘到记事本或聊天框里都行。</p>

<p>小结一下顺序：版本 → 种子 → 范围 → 模式 → 开始搜索。以后熟悉了，通常三步就能出结果。</p>

<a name="sec3"></a>
<h3>【图案怎么画】</h3>
<p>「图案编辑器」用来告诉程序：你到底想要什么样的史莱姆区块分布。
它是一张方格纸，每个格子代表一个区块，<b>左键点击格子切换颜色</b>，三个颜色循环：
灰 → 白 → 绿 → 灰。每个颜色的意思如下：</p>
<ul>
  <li><b>灰色 = 不在意</b>（随便是不是史莱姆区块，程序不管这个格子）；</li>
  <li><b>白色 = 必须是普通区块</b>（即“这里不是史莱姆区块”）；</li>
  <li><b>绿色 = 必须是史莱姆区块</b>。</li>
</ul>
<p>所以画图案的思路就是：<b>把你在意的格子涂成白色或绿色，不在意的格子留成灰色</b>。
灰色留得越多，程序要满足的条件越少，结果自然也越多、搜得越快。</p>
<p>举个例子：你<b>想找「苦力怕形状」就把轮廓涂成绿色、周围涂成白色</b>。
这样程序只会告诉你“站在那个位置时，史莱姆区块刚好排成这个形状”的地方。
如果只想找“孤立的一个史莱姆区块”，那就中间涂一个绿色、紧挨着的八个格子涂成白色即可。</p>
<p>另外还有两点小提示：行列数可以调整（1～64），数量就是图案的格子数；
全部涂成灰色等于没有条件，这时可以用「密度模式」去找“史莱姆区块扎堆的地方”。
图案编辑好之后记得点确定，主界面会显示当前的图案大小。</p>

<a name="sec4"></a>
<h3>【四种模式怎么选】</h3>
<ul>
  <li><b>精确模式（结果 100% 完整，推荐日常使用）</b>：老老实实把范围内每一个位置都算一遍，
      不会漏掉任何一个符合条件的结果。范围不大时速度也够快，日常就用它。</li>
  <li><b>分层模式（快一些，但可能漏掉极少数情况）</b>：先用粗略的条件快速筛掉大片不可能的区域，
      只对“有希望”的地方细算，所以更快。绝大多数情况下结果和精确模式一致，
      但在少数边界情况下可能漏掉极个别的结果。追求速度、又不想等太久时可以选它。</li>
  <li><b>快速估算模式（非常快，但结果只是估计值）</b>：只抽查一小部分位置来推算整体分布，
      速度非常快，但结果只是估计值、不是精确答案，可能漏掉真正的好位置。
      适合“先大概看看这片区域值不值得搜”，不要用它来定最终坐标。</li>
  <li><b>保守模式（最慢但最可靠，用于验证）</b>：不做任何取巧，一格一格从头查到尾，
      速度最慢但最稳。当你想确认某个结果到底对不对时，用它复核一遍最放心。</li>
</ul>
<p>拿不定主意时：<b>先用精确模式</b>；嫌慢就换分层模式；只想快速看一眼就换快速估算模式；
需要“拍板确认”就换保守模式。切换模式后，结果区顶部会标出当前用的是哪种模式，
非精确的模式还会额外显示一条提醒，看到提醒就知道结果不能当成百分之百完整。</p>

<a name="sec5"></a>
<h3>【群系地图 ≠ 地形图】</h3>
<p><b>群系地图</b>画的是「这里是什么生物群系」（沙漠 / 平原 / 沼泽……），它决定史莱姆能不能刷、
火把要不要插、沼泽小屋在哪；<b>地形图</b>画的是「这里高不高、是山还是海」，那是另一套噪声。
本工具的地图只做<b>群系</b>上色，不画地形高度，也不画结构（村庄/要塞）位置。</p>
<p><b>基岩版：</b>1.18 以后两版都用同一套「多噪声生物群系参数」，所以同一个 64 位种子下
群系分布高度接近；本工具按社区通行做法，<b>用 Java 生成器跑同一份种子</b>来近似基岩版群系，
地图上会标注「基岩版≈Java 同种子」。两版并非逐像素一致（噪声实现细节不同），
如果你要对坐标做严格规划，建议以游戏内 <code>/seed</code> + 实地确认的结果为准。</p>

<a name="sec6"></a>
<h3>【GPU 加速是什么】</h3>
<p>如果你的电脑有独立显卡，开启 GPU 可以让显卡一起参与大范围搜索。GPU 判定已经稳定，
结果与 CPU 逐位一致（不是「近似」）。</p>
<p><b>默认是关闭的</b>：显卡不一定比 CPU 快——如果你的 CPU 很强、显卡一般，纯 CPU 反而更快也更省电。
想用的话勾选「启用 GPU 加速（默认关闭）」，也可以在「设置 → 性能」里把它设为默认启用。
勾选后程序会先检测显卡能不能用：能用就交给显卡一起算；不能用或驱动不合适，
程序会<b>自动改用 CPU 继续算</b>，不会弹错误窗、也不会崩溃。</p>
<p>参考数据（本项目在 AMD RX 6600 + Ryzen 5 5600 上的实测）：</p>
<ul>
  <li>Java 版：1 亿区块，GPU 约 <b>7 ms</b>，CPU 12 线程约 <b>11 ms</b>；</li>
  <li>基岩版：1600 万区块，GPU 约 <b>10 ms</b>，CPU 12 线程约 <b>50 ms</b>。</li>
</ul>
<p>怕影响别的程序？「设置 → 性能」里有「GPU 占用率上限」：设成 50% 就是算一半、歇一半，
边玩游戏边跑也不至于把显卡占满。第一次启用时程序会自动实测本机 CPU 与 GPU 的吞吐率，
并在状态里直接告诉你「GPU 是本机 CPU 的几倍」，按这个数字决定要不要长期开着即可。</p>

<a name="sec7"></a>
<h3>【结果怎么用】</h3>
<p>每个结果旁边有「复制命令」按钮，点击后粘贴到游戏聊天框，就能直接传送到那个位置。</p>
<p>复制到的是游戏里的一条 <code>/tp</code> 命令（形如 <code>/tp @s 1234 64 -567</code>），
它会把你传送到目标区块的中心位置；如果只想看坐标，直接读卡片上的数字也一样。
需要提醒的是：卡片上写的是<b>区块坐标</b>，不是方块坐标，
而一个区块有 16×16 格，所以命令里的数字已经是帮你换算好的实际传送位置，
不需要你自己再乘 16。传送过去之后，往下挖到史莱姆能刷的层高（Java 版大致在 Y=40 以下），
就可以动手建史莱姆农场了。</p>
<p>如果结果很多，卡片是按距离由近到远排的，先挑最上面几个近的去就行。</p>

<a name="sec8"></a>
<h3>【为什么基岩版比 Java 版慢】</h3>
<p>两个版本的判定算法不一样，速度差得比较远，这里说清楚，方便你决定怎么设置：</p>
<ul>
  <li><b>Java 版</b>用的是简化的随机数算法（LCG，一次线性同余递推），
      单核实测约 <b>5 亿区块/秒</b>，所以范围开大一点也很快。</li>
  <li><b>基岩版</b>用的是完整的 Mersenne Twister：每个区块都要先跑 <b>624 步初始化</b>再 twist，
      单核只有约 <b>3600 万区块/秒</b>（比 Java 版慢十几倍）。</li>
</ul>
<p>所以给基岩版玩家的两条建议：</p>
<ul>
  <li><b>建议开启 GPU 加速</b>：基岩版扫描正是 GPU 擅长的批量计算，收益比 Java 版明显得多
      （程序会用实测基准告诉你这台机器在多大范围以上开 GPU 更划算）。</li>
  <li><b>建议开启缓存</b>：第一次搜索基岩版区域后结果会存到磁盘，之后再搜同一片区域几乎秒出。</li>
</ul>
<p>Java 版则相反：单核就已经很快，范围不大时用 CPU 反而更快（GPU 的固定开销占比重），
程序的状态栏会提示「建议关闭 GPU 加速」。</p>

<a name="sec9"></a>
<h3>【常见问题】</h3>

<p><b>问：搜索结果很多怎么办？</b><br>
答：设置「目标数量」，只显示最近的 N 个。也就是说程序会从离你最近的位置开始数，
数够 N 个就不再显示了；想找更远的，把 N 调大或者换更大的搜索范围即可。</p>

<p><b>问：搜索很慢怎么办？</b><br>
答：缩小搜索范围，或开启 GPU 加速。范围每扩大一倍，要算的位置大约多四倍，
所以先把范围从「全局」改回「附近」通常立竿见影；
大范围又不想等，就按上面【GPU 加速是什么】的说明把 GPU 打开。</p>

<p><b>问：程序闪退怎么办？</b><br>
答：点击「关于/联系我们」反馈（并附上 logs 目录里的日志）。程序运行时会在 logs 目录里
记录日志文件，把闪退前的那份日志连同“你当时点了什么、填了什么”一起发过来，能极大加快定位速度。</p>

<p><b>问：为什么一个史莱姆区块都找不到？</b><br>
答：先看这三件事：一是范围是不是太小（换「局部」或「全局」再试一次）；
二是版本有没有选错（Java 版和基岩版的结果完全不同）；三是图案是不是画得太苛刻，
灰色格子太少会让条件变得极难满足，先把图案清空或减少几个绿色格子再搜一次。</p>

<p><b>问：我该信哪个模式的结果？</b><br>
答：需要精确答案时用精确模式；快速估算模式的结果只是参考，不要拿它当最终坐标；
分层模式在提示“无损”时和精确模式一致，出现提醒时则可能不完整；
心里没底就用保守模式复核一遍。</p>

<p><b>问：这个工具会改动我的游戏存档吗？</b><br>
答：不会。它只做计算，不读取也不修改你的存档文件，复制的 <code>/tp</code> 命令也需要你自己
在游戏里粘贴执行。使用前把游戏存档备份一份始终是好习惯。</p>

<p>还有别的疑问，欢迎通过「关于/联系我们」告诉我们，我们会把常见问题继续补充到这里。</p>

</body>
</html>
)HTML";

    return QString::fromUtf8(kHtml);
}

} // namespace

// v3.6.4 English tutorial (same sections as the Chinese one, slightly condensed)
static const char* buildTutorialHtmlEn()
{
    static const char* kHtml = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<title>Slime Chunk Finder - Tutorial</title>
</head>
<body>

<h2>Slime Chunk Finder - Tutorial</h2>
<p>This page is for first-time users: you do not need to know any algorithm. Read it once and you
will find slime chunks yourself. If you want to start right away, jump to "How to use".</p>

<h3>What is this</h3>
<p>A small tool that helps you find slime chunks - the places where slimes spawn - so you can build
a slime farm.</p>
<ul>
  <li><b>Chunk</b>: the game manages the world in 16x16 columns. The one near spawn is chunk (0, 0).</li>
  <li><b>Slime chunk</b>: slimes spawn <b>below Y = 40</b> in these chunks, independent of light level.
      Roughly 1 chunk in 10 (about 10%) is a slime chunk.</li>
</ul>
<p>Notes (Minecraft Wiki, "Slime"): slime chunks spawn slimes only below Y = 40; light does not matter;
swamps also spawn slimes between Y = 51 and 69 at light 7 or less, but that is a different mechanic and
this tool only handles slime chunks. Mushroom fields and the deep dark never spawn slimes (Bedrock also
excludes rivers).</p>
<p>Why care? Dig a spawn room below Y = 40 inside a slime chunk and slimes keep dropping, giving you a
steady supply of slime balls for redstone builds.</p>
<p>Key difference: <b>Java slime chunks depend on the world seed plus chunk coordinates</b>, so you must
know the seed. <b>Bedrock uses a different algorithm that does not depend on the seed</b> - every world
has the same layout - so Bedrock searches need no seed here.</p>

<h3>How to use</h3>
<p><b>1. Pick the edition (Java / Bedrock).</b> Choosing the wrong one gives completely different
results.</p>
<p><b>2. Enter the seed (Java only).</b> Negative and very long numbers are fine. Use <code>/seed</code>
in game (single player needs cheats enabled). Bedrock skips this step.</p>
<p><b>3. Pick a search range.</b> The presets fill in the numbers: Near = +/-1000 chunks,
Local = +/-5000, Global = +/-30000. Bigger ranges give more results and take longer; start with Near.</p>
<p><b>4. Pick a search mode.</b> The default Exact mode returns 100% complete results; see
"Choosing a mode" to trade completeness for speed.</p>
<p><b>5. Press Start and wait.</b> The progress bar runs, the search happens in the background and the
UI stays responsive. Results appear sorted by distance; click one to copy its coordinates.</p>
<p>Summary: edition - seed - range - mode - Start.</p>

<h3>Drawing a pattern</h3>
<p>The pattern editor is a grid where each cell is one chunk. Left click cycles grey to white to green:</p>
<ul>
  <li><b>Grey</b> = do not care;</li>
  <li><b>White</b> = must NOT be a slime chunk;</li>
  <li><b>Green</b> = must be a slime chunk.</li>
</ul>
<p>Paint only the cells you care about and leave the rest grey: fewer constraints mean faster searches
and more results. Rows and columns are adjustable (1 to 64). An all-grey pattern means "no condition";
use Density mode to find clusters instead.</p>

<h3>Choosing a mode</h3>
<ul>
  <li><b>Exact</b> (recommended): evaluates every position, never misses a match.</li>
  <li><b>Layered</b>: prunes large impossible areas first, so it is faster; in rare boundary cases it
      may miss a match.</li>
  <li><b>Fast estimate</b>: samples a small part and extrapolates; very fast but only an estimate.</li>
  <li><b>Brute force</b>: literal scan, slowest but easiest to trust for verification.</li>
</ul>
<p>If unsure, use Exact. Non-exact modes show a warning above the results.</p>

<h3>Biome map is not a terrain map</h3>
<p>The <b>biome map</b> shows which biome a place is (desert / plains / swamp...) - that is what decides
slime spawning, torch needs and swamp huts. A <b>terrain map</b> shows elevation and landform, which is a
different noise layer. This tool only colors <b>biomes</b>; it does not draw terrain height or structures.</p>
<p><b>Bedrock:</b> since 1.18 both editions share the same multi-noise biome parameters, so for one 64-bit
seed the biome layout is very close. Following the common community approach, this tool runs the
<b>Java generator with the same seed</b> to approximate Bedrock biomes and labels the map
"Bedrock ~= Java same seed". The two editions are not pixel-identical (noise details differ), so verify
important coordinates in game with <code>/seed</code>.</p>

<a name="sec6"></a>
<h3>GPU acceleration</h3>
<p>With a suitable GPU, enabling it lets the GPU join large scans. When enabled the program checks the
device first: if it is unusable the search silently <b>falls back to the CPU</b> - no crash, no error
dialog.</p>
<p><b>GPU is not always faster.</b> The GPU status panel shows measured throughput for CPU and GPU on
this machine; when in doubt leave it off and compare once.</p>

<h3>Using the results</h3>
<p>Each result has a copy button that gives a <code>/tp</code> command (for example
<code>/tp @s 1234 64 -567</code>) already converted to block coordinates - no need to multiply by 16.
Teleport there, dig below Y = 40 and build your farm. Results are sorted by distance, so start with the
first few.</p>

<h3>Why Bedrock is slower</h3>
<ul>
  <li><b>Java</b> uses a small LCG: about <b>500M chunks/s</b> per core.</li>
  <li><b>Bedrock</b> uses a full Mersenne Twister (624-step init per chunk): about
      <b>36M chunks/s</b> per core, more than ten times slower.</li>
</ul>
<p>So for Bedrock: enable GPU acceleration (batch scanning suits GPUs) and enable the cache (the first
scan is stored on disk; later scans of the same area are almost instant).</p>

<h3>FAQ</h3>
<p><b>Too many results?</b> Set the target count N; only the N nearest are shown.</p>
<p><b>Too slow?</b> Shrink the range (each doubling quadruples the work) or enable the GPU.</p>
<p><b>Crash?</b> Report it via About / Contact us and attach the log from the logs folder.</p>
<p><b>No slime chunk at all?</b> Check the range, the edition and the pattern (too few grey cells makes
the condition almost impossible).</p>
<p><b>Which result should I trust?</b> Exact for final answers, brute force to verify, fast estimate only
for a rough idea.</p>
<p><b>Does it modify my world?</b> No. It only computes; you paste the command yourself. Backing up your
save is always a good habit.</p>

</body>
</html>
)HTML";
    return kHtml;
}

QString helpTutorialHtml()
{
    // 只构造一次并缓存：弹窗与自检多次调用时内容完全一致，且省去重复解析。
    // v3.6.4: return the tutorial for the current language (both cached)
    static const QString zhHtml = buildTutorialHtml();
    static const QString enHtml = QString::fromUtf8(buildTutorialHtmlEn());
    return slime::ui::isEnglish() ? enHtml : zhHtml;
}

void showHelpDialog(QWidget* parent)
{
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("使用教程"));
    dlg.setMinimumSize(720, 560); // 约 720×560：一屏能读完整段，又不会大到超出小屏

    auto* root = new QVBoxLayout(&dlg);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto* tip = new QLabel(
        QStringLiteral("下面是面向新手的使用教程。正文可以用鼠标选中后按 Ctrl+C 复制，"
                       "也可以点下面的「复制全部文字」一次性复制全部内容。"),
        &dlg);
    tip->setWordWrap(true);
    root->addWidget(tip);

    // v3.7.5（用户要求）：左侧目录 + 右侧正文，点目录直接跳到对应章节
    struct TocEntry {
        const char* title;
        const char* anchor;
    };
    static const TocEntry kToc[] = {
        { "这是什么", "sec1" },       { "怎么用", "sec2" },
        { "图案怎么画", "sec3" },     { "四种模式怎么选", "sec4" },
        { "群系地图 ≠ 地形图", "sec5" }, { "GPU 加速", "sec6" },
        { "结果怎么用", "sec7" },     { "基岩版为什么慢", "sec8" },
        { "常见问题", "sec9" },
    };

    auto* body = new QHBoxLayout();
    auto* toc = new QListWidget(&dlg);
    toc->setFixedWidth(148);
    for (const TocEntry& e : kToc) toc->addItem(QString::fromUtf8(e.title));
    toc->setCurrentRow(0);
    body->addWidget(toc);

    auto* browser = new QTextBrowser(&dlg);
    browser->setHtml(helpTutorialHtml());
    browser->setReadOnly(true);              // 只读，但可滚动、可选中、可复制
    browser->setOpenExternalLinks(false);    // 不自动打开外部浏览器/链接
    browser->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    browser->setMinimumHeight(320);
    body->addWidget(browser, 1);
    root->addLayout(body, 1);
    QObject::connect(toc, &QListWidget::currentRowChanged, &dlg, [browser](int row) {
        if (row < 0) return;
        const QString anchor = QStringLiteral("sec%1").arg(row + 1);
        browser->scrollToAnchor(anchor);
    });

    auto* btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);

    auto* copyBtn = new QPushButton(QStringLiteral("复制全部文字"), &dlg);
    copyBtn->setToolTip(QStringLiteral("把教程的全部文字复制到剪贴板，便于保存或发给朋友。"));
    btnRow->addWidget(copyBtn);

    // 复制结果只在这里显示，不弹任何对话框（弹窗会打断自动化自检）
    auto* copyState = new QLabel(QStringLiteral(" "), &dlg);
    copyState->setMinimumWidth(copyState->fontMetrics().horizontalAdvance(QStringLiteral(" ")) * 8);
    btnRow->addWidget(copyState);

    btnRow->addStretch(1);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    if (QPushButton* closeBtn = box->button(QDialogButtonBox::Close)) {
        closeBtn->setText(QStringLiteral("关闭"));
        closeBtn->setDefault(true);   // 回车即可关闭
        closeBtn->setAutoDefault(true);
    }
    btnRow->addWidget(box);
    root->addLayout(btnRow);

    QObject::connect(copyBtn, &QPushButton::clicked, &dlg, [browser, copyState]() {
        if (QClipboard* clip = QApplication::clipboard()) {
            clip->setText(browser->toPlainText());
        }
        copyState->setText(QStringLiteral("已复制到剪贴板"));
    });
    QObject::connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    dlg.exec();
}
