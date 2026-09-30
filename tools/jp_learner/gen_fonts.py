#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成口袋日语使用的 LVGL 9.5 位图字库子集（lv_font_fmt_txt 格式）。

用法（仓库根目录；需要 Pillow）::

    python3 -m pip install pillow fonttools
    python3 tools/jp_learner/gen_fonts.py \
        --font-sc /path/to/SourceHanSansSC-Regular.otf \
        --font-jp /path/to/NotoSansCJKjp-Regular.otf

为什么自己生成而不用 lv_font_conv：本机沙箱无法运行 npm，Pillow（FreeType）
即可精确渲染 OTF。输出格式与 lv_font_conv ``--no-compress --bpp 4`` 相同：
PLAIN 位图、每像素 4 bit、行与行之间连续打包（stride=0）、高半字节在前。

字库与用途：

========== ===== ======================== ======================================
名称        字号   源字体                    字符集
========== ===== ======================== ======================================
jp_font_14  14    思源黑体 SC（中文字形）     全部界面文案 + 学习内容 + ASCII
jp_font_20  20    思源黑体 SC                同上
jp_font_32  32    Noto Sans CJK JP（日文字形） 同上（单词大字、结果印章）
jp_font_72  72    Noto Sans CJK JP           仅假名卡片用到的平/片假名
========== ===== ======================== ======================================

14/20 用 SC 字形让中文释义与界面符合中文书写习惯；32/72 用 JP 字形，
让日文汉字（如「直」「骨」「写」）按日本规范显示。

字符清单不写在这里：``required_codepoints()`` 从 ``main/jp_text.h`` 与
``jp_content.py`` 提取，``tests/test_jp_font_coverage.py`` 复用同一函数并解析
生成的 C 文件，文案变了却忘记重新生成会直接让静态门禁失败。
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jp_content  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
TEXT_HEADER = REPO_ROOT / "main" / "jp_text.h"
OUT_DIR = REPO_ROOT / "assets" / "fonts"

BPP = 4
ASCII_RANGE = range(0x20, 0x7F)   # 可打印 ASCII，罗马音/数字/百分号都靠它

DEFINE_RE = re.compile(r'^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+"((?:[^"\\]|\\.)*)"', re.M)


@dataclass(frozen=True)
class FontSpec:
    name: str        # C 符号名，也是输出文件名
    size: int        # 像素字号（em 大小）
    source: str      # "sc" 或 "jp"，对应命令行 --font-sc / --font-jp
    charset: str     # "all" 全部文案，"kana" 仅假名卡片


FONT_SPECS: tuple[FontSpec, ...] = (
    FontSpec("jp_font_14", 14, "sc", "all"),
    FontSpec("jp_font_20", 20, "sc", "all"),
    FontSpec("jp_font_32", 32, "jp", "all"),
    FontSpec("jp_font_72", 72, "jp", "kana"),
)


def ui_strings() -> list[str]:
    """jp_text.h 中全部 #define 字符串。

    约定文案里不使用 C 转义（直接写 UTF-8 字符），这样提取结果与屏幕显示逐字一致；
    出现反斜杠时直接报错，避免转义序列被当成普通字符收进字库。
    """
    text = TEXT_HEADER.read_text(encoding="utf-8")
    strings = []
    for name, raw in DEFINE_RE.findall(text):
        if "\\" in raw:
            raise SystemExit(f"{name}: jp_text.h 文案请直接写 UTF-8，不要使用转义序列")
        strings.append(raw)
    return strings


def content_strings() -> list[str]:
    """学习内容中所有会显示在屏幕上的字符串。"""
    strings: list[str] = []
    for kana in jp_content.build_kana():
        kata = jp_content.hira_to_kata(kana.hira)
        strings += [kana.hira, kata, kana.romaji, kana.row_label,
                    jp_content.hira_to_kata(kana.row_label)]
    for word in jp_content.build_words():
        strings += [word.word, word.reading, word.romaji, word.meaning]
    strings += [group.name for group in jp_content.KANA_GROUPS]
    strings += list(jp_content.WORD_CATEGORIES)
    return strings


def kana_card_codepoints() -> set[int]:
    """72px 大字只显示假名卡片本身。"""
    chars: set[int] = set()
    for kana in jp_content.build_kana():
        chars.update(map(ord, kana.hira))
        chars.update(map(ord, jp_content.hira_to_kata(kana.hira)))
    return chars


def required_codepoints() -> dict[str, set[int]]:
    """每个字库必须包含的码点（换行等控制字符不计）。"""
    everything: set[int] = set(ASCII_RANGE)
    for text in ui_strings() + content_strings():
        everything.update(ord(ch) for ch in text if ord(ch) >= 0x20)
    result: dict[str, set[int]] = {}
    for spec in FONT_SPECS:
        result[spec.name] = set(everything) if spec.charset == "all" else kana_card_codepoints()
    return result


# ---------------------------------------------------------------------------
# 渲染与 C 输出
# ---------------------------------------------------------------------------

@dataclass
class Glyph:
    codepoint: int
    adv_w: int          # 1/16 像素（LVGL 8.4 定点）
    box_w: int
    box_h: int
    ofs_x: int
    ofs_y: int          # 位图底边相对基线的偏移，向上为正
    bitmap: bytes       # 4 bpp 连续打包


def render_glyph(font, codepoint: int) -> Glyph:
    from PIL import Image, ImageDraw

    char = chr(codepoint)
    adv_w = int(round(font.getlength(char) * 16))
    x0, y0, x1, y1 = font.getbbox(char, anchor="ls")
    if x1 <= x0 or y1 <= y0:
        return Glyph(codepoint, adv_w, 0, 0, 0, 0, b"")
    image = Image.new("L", (x1 - x0, y1 - y0), 0)
    ImageDraw.Draw(image).text((-x0, -y0), char, fill=255, font=font, anchor="ls")
    ink = image.getbbox()
    if ink is None:   # 空白字符（全角空格等）
        return Glyph(codepoint, adv_w, 0, 0, 0, 0, b"")
    image = image.crop(ink)
    width, height = image.size
    # "L" 模式 tobytes() 按行输出每像素 1 字节，与 LVGL 的行优先连续打包顺序一致。
    pixels = [(value * 15 + 127) // 255 for value in image.tobytes()]
    packed = bytearray((len(pixels) + 1) // 2)
    for index, value in enumerate(pixels):
        if index & 1:
            packed[index >> 1] |= value
        else:
            packed[index >> 1] = value << 4
    bottom = y0 + ink[3]   # 相对基线，向下为正
    return Glyph(codepoint, adv_w, width, height, x0 + ink[0], -bottom, bytes(packed))


_CMAP_CACHE: dict[Path, set[int]] = {}


def font_cmap(font_path: Path) -> set[int]:
    """读取字体文件实际收录的 Unicode 码点（fontTools，只解析 cmap 表）。"""
    if font_path not in _CMAP_CACHE:
        from fontTools.ttLib import TTFont

        with TTFont(str(font_path), lazy=True) as tt:
            _CMAP_CACHE[font_path] = set(tt.getBestCmap().keys())
    return _CMAP_CACHE[font_path]


def check_limits(spec: FontSpec, glyphs: list[Glyph], bitmap_size: int) -> None:
    """lv_font_fmt_txt_glyph_dsc_t 的位域限制（LV_FONT_FMT_TXT_LARGE == 0）。"""
    if bitmap_size >= 1 << 20:
        raise SystemExit(f"{spec.name}: 位图 {bitmap_size} B 超过 1 MB 位域上限")
    for glyph in glyphs:
        if glyph.adv_w >= 1 << 12 or glyph.box_w > 255 or glyph.box_h > 255:
            raise SystemExit(f"{spec.name}: U+{glyph.codepoint:04X} 尺寸超出位域")
        if not (-128 <= glyph.ofs_x <= 127 and -128 <= glyph.ofs_y <= 127):
            raise SystemExit(f"{spec.name}: U+{glyph.codepoint:04X} 偏移超出 int8")


def c_comment_char(codepoint: int) -> str:
    char = chr(codepoint)
    if char in '\\"' or codepoint < 0x21 or char == "*" or char == "/":
        return f"\\x{codepoint:02X}" if codepoint < 0x80 else char
    return char


def emit_font(spec: FontSpec, font_path: Path, codepoints: set[int]) -> str:
    from PIL import ImageFont

    font = ImageFont.truetype(str(font_path), spec.size)
    ordered = sorted(codepoints)
    if any(cp > 0xFFFF for cp in ordered):
        raise SystemExit(f"{spec.name}: 不支持 BMP 以外的字符")

    # 缺字判断必须看字体 cmap：FreeType 对缺字会画出 .notdef 方框，
    # 仅凭渲染结果非空无法区分「有这个字」和「画了个豆腐块」。
    cmap = font_cmap(font_path)
    missing = [cp for cp in ordered if cp not in cmap]
    glyphs = [render_glyph(font, cp) for cp in ordered if cp in cmap]
    if missing:
        listed = ", ".join(f"U+{cp:04X}" for cp in missing)
        raise SystemExit(f"{spec.name}: 源字体缺少字形 {listed}")

    offsets: list[int] = []
    bitmap = bytearray()
    for glyph in glyphs:
        offsets.append(len(bitmap))
        bitmap += glyph.bitmap
    check_limits(spec, glyphs, len(bitmap))

    # 行高按实际收录字形的上下极值计算，比 OTF 的 hhea 行距（约 1.45 em）紧凑，
    # 需要行距时由控件的 text_line_space 样式补充。
    ascent = max((g.ofs_y + g.box_h for g in glyphs if g.box_h), default=spec.size)
    descent = max((-g.ofs_y for g in glyphs if g.box_h), default=0)
    descent = max(descent, 0)
    line_height = ascent + descent

    ascii_glyphs = [g for g in glyphs if g.codepoint in ASCII_RANGE]
    has_ascii_block = len(ascii_glyphs) == len(ASCII_RANGE)
    sparse = [g for g in glyphs if not (has_ascii_block and g.codepoint in ASCII_RANGE)]

    out: list[str] = []
    out.append("/*******************************************************************************")
    out.append(f" * {spec.name}: {spec.size} px, {BPP} bpp, {len(glyphs)} glyphs")
    out.append(f" * Source: {font_path.name} ({font.getname()[0]} {font.getname()[1]})")
    out.append(" * Generated by tools/jp_learner/gen_fonts.py -- do not edit.")
    out.append(" * Font license: SIL Open Font License 1.1 (see assets/fonts/README.md).")
    out.append(" ******************************************************************************/")
    out.append("")
    out.append('#include "lvgl.h"')
    out.append("")
    out.append("static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap[] = {")
    for glyph, offset in zip(glyphs, offsets):
        out.append(f"    /* U+{glyph.codepoint:04X} \"{c_comment_char(glyph.codepoint)}\" */")
        data = glyph.bitmap
        for start in range(0, len(data), 16):
            out.append("    " + ", ".join(f"0x{b:02x}" for b in data[start:start + 16]) + ",")
    if not bitmap:
        out.append("    0x00,")
    out.append("};")
    out.append("")
    out.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    out.append("    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,")
    for glyph, offset in zip(glyphs, offsets):
        out.append(
            f"    {{.bitmap_index = {offset}, .adv_w = {glyph.adv_w}, .box_w = {glyph.box_w}, "
            f".box_h = {glyph.box_h}, .ofs_x = {glyph.ofs_x}, .ofs_y = {glyph.ofs_y}}},"
        )
    out.append("};")
    out.append("")

    cmaps: list[str] = []
    next_id = 1
    if has_ascii_block:
        cmaps.append(
            f"    {{\n        .range_start = {ASCII_RANGE.start}, .range_length = {len(ASCII_RANGE)}, "
            f".glyph_id_start = {next_id},\n        .unicode_list = NULL, .glyph_id_ofs_list = NULL, "
            f".list_length = 0, .type = LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY\n    }}"
        )
        next_id += len(ASCII_RANGE)
    if sparse:
        start = sparse[0].codepoint
        length = sparse[-1].codepoint - start + 1
        if length > 0xFFFF:
            raise SystemExit(f"{spec.name}: 稀疏区间过长")
        out.append("static const uint16_t unicode_list_1[] = {")
        rcps = [g.codepoint - start for g in sparse]
        for i in range(0, len(rcps), 12):
            out.append("    " + ", ".join(f"0x{r:x}" for r in rcps[i:i + 12]) + ",")
        out.append("};")
        out.append("")
        cmaps.append(
            f"    {{\n        .range_start = {start}, .range_length = {length}, .glyph_id_start = {next_id},\n"
            f"        .unicode_list = unicode_list_1, .glyph_id_ofs_list = NULL, .list_length = {len(sparse)}, "
            f".type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY\n    }}"
        )
    out.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    out.append(",\n".join(cmaps))
    out.append("};")
    out.append("")
    out.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    out.append("    .glyph_bitmap = glyph_bitmap,")
    out.append("    .glyph_dsc = glyph_dsc,")
    out.append("    .cmaps = cmaps,")
    out.append("    .kern_dsc = NULL,")
    out.append("    .kern_scale = 0,")
    out.append(f"    .cmap_num = {len(cmaps)},")
    out.append(f"    .bpp = {BPP},")
    out.append("    .kern_classes = 0,")
    out.append("    .bitmap_format = LV_FONT_FMT_TXT_PLAIN,")
    out.append("    .stride = 0,")
    out.append("};")
    out.append("")
    out.append(f"const lv_font_t {spec.name} = {{")
    out.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    out.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    out.append(f"    .line_height = {line_height},")
    out.append(f"    .base_line = {descent},")
    out.append("    .subpx = LV_FONT_SUBPX_NONE,")
    out.append(f"    .underline_position = {-max(1, spec.size // 10)},")
    out.append(f"    .underline_thickness = {max(1, spec.size // 14)},")
    out.append("    .dsc = &font_dsc,")
    out.append("    .fallback = NULL,")
    out.append("    .user_data = NULL,")
    out.append("};")
    print(f"{spec.name}: {len(glyphs)} 字, 位图 {len(bitmap) / 1024:.1f} KiB, 行高 {line_height}, 基线 {descent}")
    return "\n".join(out) + "\n"


# ---------------------------------------------------------------------------
# 生成物解析（测试与预览复用）
# ---------------------------------------------------------------------------

CMAP_RE = re.compile(
    r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*"
    r"\.unicode_list = (\w+), \.glyph_id_ofs_list = NULL, \.list_length = (\d+), \.type = (\w+)")
GLYPH_RE = re.compile(
    r"\{\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+), "
    r"\.ofs_x = (-?\d+), \.ofs_y = (-?\d+)\}")


def read_font_codepoints(path: Path) -> set[int]:
    """解析本脚本生成的字库 C 文件，返回其中真正带字形描述的码点集合。"""
    text = path.read_text(encoding="utf-8")
    lists: dict[str, list[int]] = {}
    for match in re.finditer(r"static const uint16_t (\w+)\[\] = \{(.*?)\};", text, re.S):
        lists[match.group(1)] = [int(v, 16) for v in re.findall(r"0x([0-9a-f]+)", match.group(2))]
    glyph_count = len(GLYPH_RE.findall(text)) - 1   # 去掉 id 0 占位
    codepoints: set[int] = set()
    max_id = 0
    for start, length, first_id, ulist, list_length, kind in CMAP_RE.findall(text):
        start, length, first_id, list_length = int(start), int(length), int(first_id), int(list_length)
        if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY":
            codepoints.update(range(start, start + length))
            max_id = max(max_id, first_id + length - 1)
        else:
            values = lists[ulist]
            if len(values) != list_length or values != sorted(values):
                raise ValueError(f"{path.name}: {ulist} 长度或顺序异常")
            codepoints.update(start + v for v in values)
            max_id = max(max_id, first_id + list_length - 1)
    if max_id != glyph_count:
        raise ValueError(f"{path.name}: cmap 引用 {max_id} 个字形，但描述表有 {glyph_count} 个")
    return codepoints


def main() -> int:
    parser = argparse.ArgumentParser(description="生成口袋日语 LVGL 字库")
    parser.add_argument("--font-sc", type=Path, required=True, help="思源黑体 SC / Noto Sans CJK SC")
    parser.add_argument("--font-jp", type=Path, required=True, help="Noto Sans CJK JP / 思源黑体 JP")
    parser.add_argument("--out-dir", type=Path, default=OUT_DIR)
    args = parser.parse_args()

    sources = {"sc": args.font_sc, "jp": args.font_jp}
    needed = required_codepoints()
    args.out_dir.mkdir(parents=True, exist_ok=True)
    for spec in FONT_SPECS:
        source = emit_font(spec, sources[spec.source], needed[spec.name])
        (args.out_dir / f"{spec.name}.c").write_text(source, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
