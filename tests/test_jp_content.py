#!/usr/bin/env python3
"""口袋日语学习内容、生成物与语音包的一致性检查（无需 ESP-IDF / Pillow / 网络）。"""

from __future__ import annotations

import array
import math
import re
import struct
import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools" / "jp_learner"))

import build_voice  # noqa: E402
import gen_data  # noqa: E402
import jp_content  # noqa: E402

ROMAJI_RE = re.compile(r"^[a-z']+$")


class ContentTest(unittest.TestCase):
    def test_kana_table(self) -> None:
        kana = jp_content.build_kana()
        self.assertEqual(len(kana), 104)   # 清音 46 + 浊音 20 + 半浊音 5 + 拗音 33
        self.assertEqual(len({k.hira for k in kana}), len(kana))
        counts = [sum(1 for k in kana if k.group == g) for g in range(len(jp_content.KANA_GROUPS))]
        self.assertEqual(counts, [46, 20, 5, 33])
        for card in kana:
            self.assertRegex(card.romaji, ROMAJI_RE)
            kata = jp_content.hira_to_kata(card.hira)
            self.assertTrue(all(0x30A1 <= ord(ch) <= 0x30F6 for ch in kata), kata)
            self.assertEqual(jp_content.kata_to_hira(kata), card.hira)

    def test_romaji_rules(self) -> None:
        cases = {
            "きょう": "kyou", "がっこう": "gakkou", "まっちゃ": "matcha", "コーヒー": "koohii",
            "こんや": "kon'ya", "ぎゅうにゅう": "gyuunyuu", "しゃしん": "shashin", "ぢ": "ji", "を": "wo",
        }
        for kana, romaji in cases.items():
            self.assertEqual(jp_content.to_romaji(kana), romaji, kana)
        for bad in ("ー", "っ", "abc", "が漢"):
            with self.assertRaises(ValueError):
                jp_content.to_romaji(bad)

    def test_words(self) -> None:
        words = jp_content.build_words()
        self.assertGreaterEqual(len(words), 150)
        self.assertEqual(len({(w.word, w.reading) for w in words}), len(words))
        self.assertEqual(len({w.meaning for w in words}), len(words), "释义重复会让测验出现两个正确选项")
        used = {w.category for w in words}
        self.assertEqual(used, set(range(len(jp_content.WORD_CATEGORIES))))
        for word in words:
            self.assertTrue(word.word and word.meaning)
            self.assertRegex(word.romaji, ROMAJI_RE)
            # 读音只能是假名（TTS 只读这一栏）。
            self.assertTrue(all(0x3041 <= ord(ch) <= 0x30FC for ch in word.reading), word.reading)
            # 界面按 7 字以内设计（超过 6 字的写法自动改用小字号）。
            self.assertLessEqual(len(word.word), 7, word.word)
            self.assertLessEqual(len(word.meaning), 8, word.meaning)

    def test_generated_sources_are_current(self) -> None:
        self.assertEqual(gen_data.HEADER_PATH.read_text(encoding="utf-8"), gen_data.render_header(),
                         "main/jp_data_gen.h 过期，请运行 tools/jp_learner/gen_data.py")
        self.assertEqual(gen_data.SOURCE_PATH.read_text(encoding="utf-8"), gen_data.render_source(),
                         "main/jp_data_gen.c 过期，请运行 tools/jp_learner/gen_data.py")


VOICE_PACK = ROOT / "assets" / "music" / "jp_voice_pack.bin"


class VoicePackTest(unittest.TestCase):
    @unittest.skipUnless(VOICE_PACK.exists(), "语音包未提交（允许），固件为静音模式")
    def test_pack_matches_content(self) -> None:
        blob = VOICE_PACK.read_bytes()
        magic, version, rate, count, reserved, digest = struct.unpack_from("<4sHHHHI", blob, 0)
        self.assertEqual((magic, version, rate, reserved), (b"JPV1", 1, 16000, 0))
        self.assertEqual(count, len(jp_content.voice_clips()))
        self.assertEqual(digest, jp_content.voice_hash(), "语音包过期，请运行 tools/jp_learner/build_voice.py")
        table_end = 16 + 8 * count
        previous_end = table_end
        for clip in range(count):
            offset, samples = struct.unpack_from("<II", blob, 16 + 8 * clip)
            self.assertEqual(offset, previous_end, "片段应紧密连续排列")
            previous_end = offset + (samples + 1) // 2
            self.assertLessEqual(previous_end, len(blob))
        self.assertEqual(previous_end, len(blob))

    def test_adpcm_round_trip(self) -> None:
        # 440 Hz 正弦：编码再解码后的误差应远小于信号幅度。
        pcm = array.array("h", [int(8000 * math.sin(2 * math.pi * 440 * i / 16000)) for i in range(1600)])
        decoded = build_voice.adpcm_decode(build_voice.adpcm_encode(pcm), len(pcm))
        error = max(abs(a - b) for a, b in zip(pcm[200:], decoded[200:]))
        self.assertLess(error, 800)

    def test_hash_depends_on_voice_settings(self) -> None:
        original = jp_content.voice_hash()
        saved = jp_content.VOICE_RATE_KANA
        try:
            jp_content.VOICE_RATE_KANA = "+0%"
            self.assertNotEqual(jp_content.voice_hash(), original)
        finally:
            jp_content.VOICE_RATE_KANA = saved


if __name__ == "__main__":
    unittest.main()
