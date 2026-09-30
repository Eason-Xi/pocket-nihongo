<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 口袋日语（Pocket Nihongo）

把 FoloToy AI Passport 变成一台随身、离线的日语入门学习机：五十音卡片、JLPT N5 常用词、
真人感发音、三选一测验与间隔重复复习，三个按键就能学。所有内容（字库、词表、发音）都
烧录在设备 Flash 里，不需要 Wi-Fi，也不需要手机。

本项目是独立仓库，基于 [FoloToy/ai-passport](https://github.com/FoloToy/ai-passport) 的开发板支持包与工程模板开发，
沿用其 MIT 许可证（见 [LICENSE](LICENSE)）。

## 功能

- **平假名 / 片假名卡片**：各 104 个音节（清音 46、浊音 20、半浊音 5、拗音 33），72 px 大字、
  罗马音、对应的另一种假名，以及「掌握度」圆点。
- **N5 单词卡片**：153 个常用词，分 13 类（问候、数字、时间、人物、饮食、场所、物品、自然、
  身体、动词、形容、颜色、方位），显示写法、假名读音、罗马音与中文释义。
- **发音**：每个假名和单词都有日语女声发音（离线预生成，16 kHz IMA-ADPCM），翻卡时自动
  播放，也可按 OK 重听；可在设置里关闭自动发音。
- **测验**：每轮 10 道三选一题，四种题型——看假名选读音、听发音选假名、看单词选意思、
  听发音选意思。答题后有提示音、绿/红标记，并播放正确答案的发音；结果页给出评语印章与错题。
- **间隔重复**：每张卡片有 0–5 级「盒子」，答对升一级、答错归零，连续答对 3 次算「已掌握」；
  抽题时越不熟的卡片出现得越频繁。
- **进度保存**：学习进度、各卡组看到的位置、音量/亮度等设置保存在 NVS，关机不丢。
- **省电**：右上角显示电量；30 秒无操作调暗背光，90 秒熄屏，任意键唤醒（熄屏时的那一次按键
  只负责唤醒，不会误触）。

## 操作

| 页面 | ▲ / ▼（按下即触发） | OK（单击） | 长按 OK（0.5 秒） |
| --- | --- | --- | --- |
| 首页 | 选择入口 | 进入 | — |
| 学习卡片 | 上一张 / 下一张（首尾循环） | 播放发音 | 返回首页（记住位置） |
| 测验选择 | 选择卡组 | 开始一轮 | 返回首页 |
| 答题 | 移动光标；听力题可停在喇叭卡片上 | 作答 / 重听 / 下一题 | 退出本轮（已答进度保留） |
| 结果 | — | 再来一轮 | 返回测验选择 |
| 设置 | 选择项目 | 修改（重置进度需连按两次） | 返回首页并保存 |

## 页面设计

「和纸与朱印」风格：米白和纸底色、墨色文字、朱红色表示选中与印章、抹茶绿表示答对。
界面全部重新设计，不使用基线 demo 的测试菜单或 `ui_pixel` 外壳；开机直接进入口袋日语首页。
中文界面与释义用思源黑体 SC 字形，日文（假名与单词汉字）用 Noto Sans CJK JP 字形，保证
「写」「骨」这类汉字按日本规范显示。

## 构建与烧录

需要 ESP-IDF 5.5.3（见 [环境引导](docs/development/engineering/environment-setup.zh_CN.md)）。

```bash
source <ESP-IDF-v5.5.3 路径>/export.sh
./tools/validate.sh            # 仓库检查 + 主机测试 + 固件构建与合并镜像验证
```

验证通过的合并镜像位于 `build/FoloToy-AI-Passport-full.bin`，从 `0x0` 写入。注意：合并镜像会
重置 NVS（学习进度清零）；想保留进度时用分段烧录 `idf.py flash`。详见
[烧录与已存数据](docs/development/engineering/firmware-layout.zh_CN.md#烧录与已存数据)。

## 修改学习内容

所有内容的唯一来源是 [`tools/jp_learner/jp_content.py`](tools/jp_learner/jp_content.py)（假名表、单词表、
TTS 引擎、音色、语速与单片段纠错表）；界面文字集中在 [`main/jp_text.h`](main/jp_text.h)。修改后按顺序重新生成：

```bash
python3 tools/jp_learner/gen_data.py                   # → main/jp_data_gen.c / .h
python3 tools/jp_learner/gen_fonts.py \
    --font-sc /path/to/SourceHanSansSC-Regular.otf \
    --font-jp /path/to/NotoSansCJKjp-Regular.otf       # → assets/fonts/jp_font_*.c（需要 Pillow、fontTools）
python3 tools/jp_learner/build_voice.py --ffmpeg /path/to/ffmpeg  # → assets/music/jp_voice_pack.bin（需要已登录的 bl CLI、联网）
```

语音包默认用阿里云百炼 CosyVoice 合成：先 `npm install -g bailian-cli` 安装 CLI，再 `bl auth login --console`
登录一次；密钥由 `bl` 保管，脚本不读取。把 `jp_content.py` 里的 `VOICE_ENGINE` 改为 `"edge"` 可切回
Edge TTS（需 `python3 -m pip install edge-tts`）。改动引擎、音色、语速或纠错表都会改变语音哈希，因此也要
重新运行 `gen_data.py`。

静态门禁会检查：生成的数据表与内容源一致、字库覆盖全部文案与内容（`tests/test_jp_font_coverage.py`）、
语音包哈希与内容一致且每段都能解码（`tests/test_jp_content.py`、`tests/test_jp_adpcm.c`）。

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `main/jp_main.c` | 固件入口：初始化 BSP 后启动应用 |
| `main/jp_app.c` | 应用调度：按键队列、页面路由、调暗/熄屏、电量刷新、延迟存档 |
| `main/jp_ui.c` | 主题配色、字库回退链、页眉/页脚/菜单行/圆点等通用控件 |
| `main/jp_scr_*.c` | 首页、学习卡片、测验（选择/答题/结果）、设置四组页面 |
| `main/jp_voice.c` | 语音任务：只保留最新请求、16 ms 分块可打断、提示音 + 发音 |
| `main/jp_store.c` | NVS 读写（内容未变化时不写 Flash） |
| `main/jp_quiz.c`、`jp_srs.c`、`jp_power.c`、`jp_save.c`、`jp_tone.c`、`jp_adpcm.c`、`jp_deck.c` | 与硬件解耦的纯逻辑，全部有主机测试 |

基线参考 demo（`main/main.c`、`main/demo_*.c`、`main/ui_pixel*.c`）保留在目录中供参考并继续由主机测试
覆盖，但不编译进本固件。

## 资源占用

以 2026-09-30 的构建为准：应用镜像约 2.59 MB（factory 分区 8 MB），其中字库位图约 0.63 MB、语音包
1.16 MB（257 段，共 147.7 秒）；静态 DRAM 占用约 39%。LVGL 内存池由基线的 24 KB 调到 40 KB——主机
离屏渲染实测单个页面峰值约 19 KB，切页时先删旧页再建新页，400 轮切页压力测试后最小连续空闲块仍有
约 16 KB。启动日志会打印真机上的堆与 LVGL 池占用。

## 素材来源与授权

- 字库：思源黑体 SC / Noto Sans CJK JP，SIL Open Font License 1.1，只提交生成的位图子集，详见
  [素材说明](assets/README.zh_CN.md)。
- 发音：阿里云百炼 CosyVoice（`cosyvoice-v3-flash`，音色 `loongtomoka_v3`）离线预生成。**公开发布固件
  或把语音包推送到公开仓库前，请自行确认百炼对生成音频的使用条款。** 缺少语音包时固件仍可构建，自动以
  静音模式运行。

## 当前状态

- 已完成：全部页面与交互、内容/字库/语音生成管线、主机测试、固件构建。
- 待真机验收：屏幕显示与字形、按键手感、发音音质与音量、调暗/熄屏、NVS 保存与重启恢复、真机内存占用。
- 已知限制：尚未实现深度睡眠（只关闭背光）；本板按键唤醒深睡需要额外的 BSP 支持，留作后续改进。
