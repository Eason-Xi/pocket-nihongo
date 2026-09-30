#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""合成口袋日语的全部发音，并打包成固件内嵌的 IMA-ADPCM 语音包。

用法（在仓库根目录执行；edge-tts 需装在当前 Python 环境中）::

    python3 -m pip install edge-tts
    python3 tools/jp_learner/build_voice.py --ffmpeg /path/to/ffmpeg

流程：

1. 读取 ``jp_content.voice_clips()``，逐条用 Edge TTS（``VOICE_NAME``，语速取
   ``jp_content.voice_rate()``：假名慢速、单词常速）合成 MP3，缓存到 ``build/jp_voice_cache/``（已被 Git 忽略；缓存键包含文本、音色、
   语速，改参数会自动重新合成）。
2. ffmpeg 解码为 16 kHz / 16 bit / 单声道 PCM（与 BSP 默认音频格式一致）。
3. 去掉首尾静音、保留少量前后留白并做淡入淡出，峰值归一化到 -1.5 dBFS。
4. IMA-ADPCM 4 bit 编码（每个片段从 predictor=0、step_index=0 开始，
   与固件 ``main/jp_adpcm.c`` 的初始状态一致）。
5. 写出 ``assets/music/jp_voice_pack.bin``。

语音包格式（全部小端）::

    偏移 0   char[4]  magic = "JPV1"
    偏移 4   u16      version = 1
    偏移 6   u16      sample_rate = 16000
    偏移 8   u16      clip_count
    偏移 10  u16      reserved = 0
    偏移 12  u32      voice_hash（= jp_content.voice_hash()）
    偏移 16  clip_count × { u32 data_offset（相对包首）, u32 sample_count }
    之后     各片段 ADPCM 数据，每字节 2 个采样，低半字节在前

发音版权：Edge TTS 生成的音频仅建议用于个人学习；公开分发固件前请自行确认
微软相关服务条款。
"""

from __future__ import annotations

import argparse
import array
import asyncio
import hashlib
import shutil
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import jp_content  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CACHE = REPO_ROOT / "build" / "jp_voice_cache"
DEFAULT_OUT = REPO_ROOT / "assets" / "music" / "jp_voice_pack.bin"

SAMPLE_RATE = 16000
PACK_MAGIC = b"JPV1"
PACK_VERSION = 1
HEADER_SIZE = 16
ENTRY_SIZE = 8

# 静音判定阈值：|x| 超过满幅的 0.4%（约 -48 dBFS）视为有声；阈值偏低是为了
# 保住 h/f/s 等轻辅音的起始与元音尾巴，Edge TTS 的底噪远低于这个值。
SILENCE_THRESHOLD = int(32767 * 0.004)
LEAD_MS = 40      # 有声段前保留的留白
TAIL_MS = 120     # 有声段后保留的留白（让尾音自然衰减）
FADE_MS = 8       # 首尾淡入淡出，避免截断处的爆音
PEAK_TARGET = int(32767 * 0.84)   # ≈ -1.5 dBFS

# ---------------------------------------------------------------------------
# IMA-ADPCM（与 main/jp_adpcm.c 完全对应）
# ---------------------------------------------------------------------------

STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8)


def adpcm_encode(samples: array.array) -> bytes:
    """IMA-ADPCM 编码；返回紧密打包的半字节流（低半字节为先）。"""
    predictor = 0
    index = 0
    nibbles = bytearray((len(samples) + 1) // 2)
    for position, sample in enumerate(samples):
        step = STEP_TABLE[index]
        diff = sample - predictor
        nibble = 0
        if diff < 0:
            nibble = 8
            diff = -diff
        vpdiff = step >> 3
        if diff >= step:
            nibble |= 4
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            nibble |= 2
            diff -= step
            vpdiff += step
        step >>= 1
        if diff >= step:
            nibble |= 1
            vpdiff += step
        predictor = predictor - vpdiff if nibble & 8 else predictor + vpdiff
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + INDEX_TABLE[nibble]))
        if position & 1:
            nibbles[position >> 1] |= nibble << 4
        else:
            nibbles[position >> 1] = nibble
    return bytes(nibbles)


def adpcm_decode(data: bytes, sample_count: int) -> array.array:
    """IMA-ADPCM 解码；用于自检与生成试听 WAV。"""
    predictor = 0
    index = 0
    out = array.array("h")
    for position in range(sample_count):
        byte = data[position >> 1]
        nibble = (byte >> 4) if position & 1 else (byte & 0x0F)
        step = STEP_TABLE[index]
        vpdiff = step >> 3
        if nibble & 4:
            vpdiff += step
        if nibble & 2:
            vpdiff += step >> 1
        if nibble & 1:
            vpdiff += step >> 2
        predictor = predictor - vpdiff if nibble & 8 else predictor + vpdiff
        predictor = max(-32768, min(32767, predictor))
        index = max(0, min(88, index + INDEX_TABLE[nibble]))
        out.append(predictor)
    return out


# ---------------------------------------------------------------------------
# 合成与后处理
# ---------------------------------------------------------------------------

def cache_path(cache_dir: Path, text: str, rate: str) -> Path:
    """缓存文件名 = sha1(音色|语速|文本)，参数一变就是新文件。"""
    key = f"{jp_content.VOICE_NAME}|{rate}|{text}".encode("utf-8")
    return cache_dir / f"{hashlib.sha1(key).hexdigest()}.mp3"


async def _synthesize(text: str, rate: str, out: Path) -> None:
    import edge_tts  # 延迟导入：只打包缓存时不需要网络和 edge-tts

    communicate = edge_tts.Communicate(text, jp_content.VOICE_NAME, rate=rate)
    tmp = out.with_suffix(".part")
    await communicate.save(str(tmp))
    if tmp.stat().st_size < 512:
        raise RuntimeError(f"合成结果过小（{tmp.stat().st_size} B）")
    tmp.replace(out)


def synthesize_cached(cache_dir: Path, text: str, rate: str, retries: int = 4) -> Path:
    """确保缓存里有该文本的 MP3；网络偶发失败时指数退避重试。"""
    path = cache_path(cache_dir, text, rate)
    if path.exists():
        return path
    for attempt in range(retries):
        try:
            asyncio.run(_synthesize(text, rate, path))
            return path
        except Exception as error:  # noqa: BLE001 —— 网络/服务端错误统一重试
            wait = 1.5 * (2 ** attempt)
            print(f"  合成失败（第 {attempt + 1} 次）: {error}；{wait:.1f}s 后重试", file=sys.stderr)
            time.sleep(wait)
    raise RuntimeError(f"多次重试后仍无法合成: {text!r}")


def decode_pcm(ffmpeg: str, mp3: Path) -> array.array:
    """ffmpeg 解码为 16 kHz 单声道 s16le，并滤掉 60 Hz 以下的低频（小喇叭放不出来）。"""
    result = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(mp3), "-af", "highpass=f=60",
         "-ac", "1", "-ar", str(SAMPLE_RATE), "-f", "s16le", "-"],
        check=True, capture_output=True)
    pcm = array.array("h")
    pcm.frombytes(result.stdout)
    if sys.byteorder != "little":
        pcm.byteswap()
    return pcm


def trim_and_normalize(pcm: array.array) -> array.array:
    """去首尾静音、加留白与淡入淡出、峰值归一化。全静音片段直接报错。"""
    loud = [i for i, x in enumerate(pcm) if abs(x) > SILENCE_THRESHOLD]
    if not loud:
        raise ValueError("片段全为静音")
    start = max(0, loud[0] - SAMPLE_RATE * LEAD_MS // 1000)
    end = min(len(pcm), loud[-1] + SAMPLE_RATE * TAIL_MS // 1000)
    clip = pcm[start:end]

    peak = max(abs(x) for x in clip) or 1
    gain = PEAK_TARGET / peak
    fade = SAMPLE_RATE * FADE_MS // 1000
    out = array.array("h", bytes(2 * len(clip)))
    for i, x in enumerate(clip):
        env = 1.0
        if i < fade:
            env = i / fade
        elif i >= len(clip) - fade:
            env = (len(clip) - 1 - i) / fade
        out[i] = max(-32768, min(32767, int(round(x * gain * env))))
    return out


def write_wav(path: Path, pcm: array.array) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(SAMPLE_RATE)
        wav.writeframes(pcm.tobytes())


def build_pack(clips: list[tuple[int, bytes]]) -> bytes:
    """按上面的格式拼出语音包。clips = [(sample_count, adpcm_bytes)]。"""
    header = struct.pack("<4sHHHHI", PACK_MAGIC, PACK_VERSION, SAMPLE_RATE,
                         len(clips), 0, jp_content.voice_hash())
    table = bytearray()
    data = bytearray()
    offset = HEADER_SIZE + ENTRY_SIZE * len(clips)
    for sample_count, adpcm in clips:
        table += struct.pack("<II", offset + len(data), sample_count)
        data += adpcm
    return header + bytes(table) + bytes(data)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg") or "ffmpeg")
    parser.add_argument("--cache", type=Path, default=DEFAULT_CACHE)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    parser.add_argument("--preview", type=Path,
                        help="把 ADPCM 解码后的结果另存为 WAV，便于试听")
    args = parser.parse_args()

    args.cache.mkdir(parents=True, exist_ok=True)
    texts = jp_content.voice_clips()
    packed: list[tuple[int, bytes]] = []
    total_samples = 0
    for clip_id, text in enumerate(texts):
        mp3 = synthesize_cached(args.cache, text, jp_content.voice_rate(clip_id))
        pcm = trim_and_normalize(decode_pcm(args.ffmpeg, mp3))
        adpcm = adpcm_encode(pcm)
        packed.append((len(pcm), adpcm))
        total_samples += len(pcm)
        if args.preview:
            write_wav(args.preview / f"{clip_id:03d}_{text}.wav",
                      adpcm_decode(adpcm, len(pcm)))
        print(f"[{clip_id + 1:3d}/{len(texts)}] {text}  {len(pcm) / SAMPLE_RATE:.2f}s", flush=True)

    pack = build_pack(packed)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_bytes(pack)
    print(f"语音包: {args.out.relative_to(REPO_ROOT)}  {len(pack) / 1024:.1f} KiB, "
          f"{len(texts)} 段, 总时长 {total_samples / SAMPLE_RATE:.1f}s, "
          f"hash=0x{jp_content.voice_hash():08X}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
