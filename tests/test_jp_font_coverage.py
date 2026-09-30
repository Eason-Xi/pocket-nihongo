#!/usr/bin/env python3
"""证明口袋日语的字库子集覆盖全部界面文案与学习内容（无需 Pillow / LVGL）。

检查三件事：
1. 每个生成的 assets/fonts/jp_font_*.c 包含 gen_fonts.required_codepoints() 要求的全部码点；
2. 反例：刻意未收录的字（U+9F98「龘」）确实不在字库里，防止检查无条件通过；
3. 界面代码（main/jp_ui.c 与 main/jp_scr_*.c）中没有散落的非 ASCII 字符串字面量 ——
   所有上屏的中文/日文必须写在 main/jp_text.h（或来自生成的学习数据），否则字库生成器
   看不到它们。其余模块里的中文只出现在串口日志中，不经过 LVGL，不在检查范围内。
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "jp_learner"))

import gen_fonts  # noqa: E402

def ui_sources() -> list[Path]:
    """会把字符串交给 LVGL 显示的源文件。"""
    main = ROOT / "main"
    return [main / "jp_ui.c", *sorted(main.glob("jp_scr_*.c"))]


def string_literals(source: str) -> list[str]:
    """提取 C 源码中的字符串字面量，跳过注释与字符常量。"""
    literals: list[str] = []
    i, n = 0, len(source)
    while i < n:
        ch = source[i]
        if source.startswith("//", i):
            i = source.find("\n", i)
            i = n if i < 0 else i
        elif source.startswith("/*", i):
            end = source.find("*/", i + 2)
            i = n if end < 0 else end + 2
        elif ch in "\"'":
            j = i + 1
            while j < n and source[j] != ch:
                j += 2 if source[j] == "\\" else 1
            if ch == '"':
                literals.append(source[i + 1:j])
            i = j + 1
        else:
            i += 1
    return literals


class FontCoverageTest(unittest.TestCase):
    def test_fonts_cover_required_codepoints(self) -> None:
        required = gen_fonts.required_codepoints()
        for spec in gen_fonts.FONT_SPECS:
            path = ROOT / "assets" / "fonts" / f"{spec.name}.c"
            present = gen_fonts.read_font_codepoints(path)
            missing = sorted(required[spec.name] - present)
            self.assertFalse(missing, f"{spec.name} 缺字: " + ", ".join(f"U+{cp:04X}" for cp in missing)
                             + "（请运行 tools/jp_learner/gen_fonts.py）")
            self.assertNotIn(0x9F98, present, "反例字符不应出现在字库中")

    def test_big_font_only_contains_kana(self) -> None:
        present = gen_fonts.read_font_codepoints(ROOT / "assets" / "fonts" / "jp_font_72.c")
        self.assertTrue(present)
        self.assertTrue(all(0x3041 <= cp <= 0x30FC for cp in present))

    def test_no_stray_non_ascii_literals(self) -> None:
        offenders = []
        sources = ui_sources()
        self.assertGreaterEqual(len(sources), 5)
        for path in sources:
            for literal in string_literals(path.read_text(encoding="utf-8")):
                if any(ord(ch) > 0x7E for ch in literal):
                    offenders.append(f"{path.name}: \"{literal}\"")
        self.assertFalse(offenders, "非 ASCII 界面文字请移到 main/jp_text.h：\n" + "\n".join(offenders))

    def test_literal_scanner(self) -> None:
        sample = '// "注释"\nconst char *a = "ok"; /* "块注释" */ char c = \'"\'; const char *b = "中\\"文";'
        self.assertEqual(string_literals(sample), ["ok", '中\\"文'])


if __name__ == "__main__":
    unittest.main()
