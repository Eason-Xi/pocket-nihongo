#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 ``jp_content.py`` 生成为固件 C 源码。

用法（仓库根目录）::

    python3 tools/jp_learner/gen_data.py          # 重新生成
    python3 tools/jp_learner/gen_data.py --check  # 只比较，不写文件（测试用）

输出：

* ``main/jp_data_gen.h`` —— 数量、哈希等编译期常量；
* ``main/jp_data_gen.c`` —— 假名表、单词表、分类名数组（结构体定义见 ``main/jp_data.h``）。

两个文件都由本脚本生成，请勿手改；``tests/test_jp_content.py`` 会用 ``--check``
逻辑确认提交的生成物与内容源一致。
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jp_content  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
HEADER_PATH = REPO_ROOT / "main" / "jp_data_gen.h"
SOURCE_PATH = REPO_ROOT / "main" / "jp_data_gen.c"

BANNER = "// 由 tools/jp_learner/gen_data.py 从 tools/jp_learner/jp_content.py 生成，请勿手改。\n"


def c_string(text: str) -> str:
    """输出 C 字符串字面量；源码为 UTF-8，只需转义反斜杠与双引号。"""
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def render_header() -> str:
    kana = jp_content.build_kana()
    words = jp_content.build_words()
    clips = jp_content.voice_clips()
    return (
        BANNER
        + "#pragma once\n\n"
        + f"#define JP_KANA_COUNT          {len(kana)}   // 每种假名（平/片）各自的卡片数\n"
        + f"#define JP_WORD_COUNT          {len(words)}   // N5 单词数\n"
        + f"#define JP_KANA_GROUP_COUNT    {len(jp_content.KANA_GROUPS)}     // 清音/浊音/半浊音/拗音\n"
        + f"#define JP_WORD_CATEGORY_COUNT {len(jp_content.WORD_CATEGORIES)}    // 单词分类数\n"
        + f"#define JP_VOICE_CLIP_COUNT    {len(clips)}   // 语音包片段数（假名共用 + 单词）\n"
        + "// 学习进度总卡片数：平假名 + 片假名 + 单词，NVS 中每张卡 1 字节。\n"
        + "#define JP_CARD_TOTAL          (JP_KANA_COUNT * 2 + JP_WORD_COUNT)\n"
        + "// 语音包内容哈希（文本+音色+语速），固件用它拒绝与数据不匹配的语音包。\n"
        + f"#define JP_VOICE_HASH          0x{jp_content.voice_hash():08X}u\n"
        + "// 卡片布局哈希，卡片顺序或数量变化时 NVS 中的旧进度自动作废。\n"
        + f"#define JP_CONTENT_HASH        0x{jp_content.content_hash():08X}u\n"
    )


def render_source() -> str:
    kana = jp_content.build_kana()
    words = jp_content.build_words()
    lines = [BANNER, '#include "jp_data.h"\n']

    lines.append("const char *const JP_KANA_GROUP_NAMES[JP_KANA_GROUP_COUNT] = {")
    lines.extend(f"    {c_string(group.name)}," for group in jp_content.KANA_GROUPS)
    lines.append("};\n")

    lines.append("const char *const JP_WORD_CATEGORY_NAMES[JP_WORD_CATEGORY_COUNT] = {")
    lines.extend(f"    {c_string(name)}," for name in jp_content.WORD_CATEGORIES)
    lines.append("};\n")

    lines.append("// 假名卡片：平假名/片假名共用读音与发音片段（voice = 表内下标）。")
    lines.append("const jp_kana_t JP_KANA[JP_KANA_COUNT] = {")
    for voice, card in enumerate(kana):
        kata = jp_content.hira_to_kata(card.hira)
        kata_row = card.row_label if card.row_label == "拨音" else jp_content.hira_to_kata(card.row_label)
        lines.append(
            f"    {{ .hira = {c_string(card.hira)}, .kata = {c_string(kata)}, "
            f".romaji = {c_string(card.romaji)}, .hira_row = {c_string(card.row_label)}, "
            f".kata_row = {c_string(kata_row)}, .group = {card.group}, .voice = {voice} }},"
        )
    lines.append("};\n")

    lines.append("// 单词卡片：voice 从 JP_KANA_COUNT 开始连续编号。")
    lines.append("const jp_word_t JP_WORDS[JP_WORD_COUNT] = {")
    for offset, word in enumerate(words):
        lines.append(
            f"    {{ .word = {c_string(word.word)}, .reading = {c_string(word.reading)}, "
            f".romaji = {c_string(word.romaji)}, .meaning = {c_string(word.meaning)}, "
            f".category = {word.category}, .voice = {len(kana) + offset} }},"
        )
    lines.append("};")
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description="生成口袋日语的 C 数据表")
    parser.add_argument("--check", action="store_true", help="只检查生成物是否最新")
    args = parser.parse_args()

    outputs = {HEADER_PATH: render_header(), SOURCE_PATH: render_source()}
    stale = [path for path, text in outputs.items()
             if not path.exists() or path.read_text(encoding="utf-8") != text]
    if args.check:
        for path in stale:
            print(f"过期: {path.relative_to(REPO_ROOT)}（请运行 gen_data.py）", file=sys.stderr)
        return 1 if stale else 0
    for path, text in outputs.items():
        path.write_text(text, encoding="utf-8")
        print(f"已生成 {path.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
