<p align="right">
  <strong>简体中文</strong> · <a href="README.md">English</a>
</p>

# 资源目录（Assets）

本目录集中存放可复用的资源（字库、图片、音乐等），按资源类型分子目录管理。每个资源放在其类型对应的子目录，并记录放置路径、命名方式、集成方式与来源/许可。二进制资源（字体、图片、音频）不属于纯 markdown 文档，请勿与文档混放。涉及版权/授权的资源需注明来源与许可。

## 字库（fonts）

可复用的字库文件与生成的字库源码放在 `fonts/`。

- 命名要能反映字族、字重、字级与格式。
- 记录来源、许可、字符范围、转换命令与目标放置路径。
- 添加字库前评估 Flash 与内部 RAM 影响；ESP32-C3 无 PSRAM。
- 不提交许可不允许分发的字库。

| 文件 | 字号与格式 | 用途与来源 |
| --- | --- | --- |
| [`fonts/jp_font_14.c`](fonts/jp_font_14.c) | 14 px，4 bpp LVGL `lv_font_fmt_txt`，626 字 | 口袋日语的操作提示、电量与小标签。思源黑体 Source Han Sans CN Regular 2.005 子集（SIL OFL 1.1）。 |
| [`fonts/jp_font_20.c`](fonts/jp_font_20.c) | 20 px，4 bpp，626 字 | 口袋日语的标题、菜单行、读音与释义。来源同上。 |
| [`fonts/jp_font_32.c`](fonts/jp_font_32.c) | 32 px，4 bpp，626 字 | 口袋日语的单词大字、罗马音与结果印章。Noto Sans CJK JP Regular 2.004 子集（SIL OFL 1.1），汉字按日本字形显示。 |
| [`fonts/jp_font_72.c`](fonts/jp_font_72.c) | 72 px，4 bpp，148 字 | 口袋日语的假名卡片，只含假名卡组用到的平假名与片假名。来源同 32 px。 |

口袋日语字库由 `tools/jp_learner/gen_fonts.py` 用 Pillow/FreeType 生成（未使用 `lv_font_conv`）。字符清单从 `main/jp_text.h` 与 `tools/jp_learner/jp_content.py` 提取；文案或内容改动后忘记重新生成，`tests/test_jp_font_coverage.py` 会失败。源 OTF 文件不提交；生成的子集不使用保留字体名称，按 SIL Open Font License 1.1 再分发。

## 图片（images）

可复用的源图与生成的显示资产放在 `images/`。

| 文件 | 尺寸与格式 | 用途与来源 |
| --- | --- | --- |
| [`images/home.jpg`](images/home.jpg) | 3840 × 2160，JPEG | 嵌入中英文项目 README 的产品主图，突出 AI Passport 产品形象与开放、人人可创作的理念。 |
| [`images/readme-hardware-specs.png`](images/readme-hardware-specs.png) | 2172 × 724，PNG RGBA | 保留为可选技术参考图，不再用于首页主视觉。于 2026-09-17 使用内置图像生成工具为本仓库生成；已根据文档中的硬件能力契约核对图中的六项标签与参数。 |
| [`images/logo-wordmark.png`](images/logo-wordmark.png) | 1648 × 336，PNG RGBA | 从仓库原始 `images/logo.png` 中精确裁切并去除背景的黑色字标；用于中英文项目 README 的浅色主题。 |
| [`images/logo-wordmark-dark.png`](images/logo-wordmark-dark.png) | 1648 × 336，PNG RGBA | 提取字标的白色版本；README 使用 `<picture>` 在 GitHub 深色主题下显示。 |

- 使用描述性命名，并记录尺寸、像素格式、转换步骤与目标路径。
- 优先采用适合 240 × 320 RGB565 显示的格式，并纳入 Flash 与内部 RAM 考量。
- 许可允许时保留可编辑源文件，并记录来源与许可。
- 图片中不得包含设备二维码秘密、凭证或个人数据。

## 音乐与音效（music）

可复用的音乐与音效源码放在 `music/`。

- 记录来源、许可、采样率、位深、声道、转换命令与目标路径。
- 与当前 BSP 音频路径匹配时优先采用 16 kHz、16 位单声道 PCM。
- 嵌入音频前评估 Flash 与内部 RAM 成本；长录音应流式或分块。
- 无再分发许可不提交媒体文件。

| 文件 | 格式 | 用途与来源 |
| --- | --- | --- |
| `music/jp_voice_pack.bin` | 16 kHz 单声道 IMA-ADPCM（4 bit），257 段，共 145.5 秒，约 1.14 MB | 口袋日语全部假名与 N5 单词的发音，嵌入应用镜像。由 `tools/jp_learner/build_voice.py` 调用微软 Edge TTS（`ja-JP-NanamiNeural`，假名语速 -30%、单词 -10%）生成，经去静音与峰值归一化；包格式见该脚本说明。 |

Edge TTS 音频的再分发条款尚未确认。`jp_voice_pack.bin` 仅建议用于个人学习；公开发布固件或把该文件推送到公开仓库前请自行确认条款。文件缺失时固件仍可构建并以静音模式运行，语音包相关检查会自动跳过。
