// main/jp_tone.h —— 答题提示音合成（纯逻辑，可主机测试）。
//
// 提示音不占 Flash 素材：运行时按音符表用正弦查找表实时生成 16 kHz PCM。
// 可按任意分块调用 jp_tone_render()，便于语音任务每块之间检查取消请求。
#pragma once

#include <stddef.h>
#include <stdint.h>

#define JP_TONE_SAMPLE_RATE 16000u   // 与语音包、BSP 音频格式一致

typedef enum {
    JP_CUE_NONE = 0,     // 无提示音
    JP_CUE_CORRECT,      // 答对：两个上行短音「叮—咚」
    JP_CUE_WRONG,        // 答错：两个下行低音
    JP_CUE_COUNT,
} jp_cue_t;

// 提示音总采样数；JP_CUE_NONE 或非法值返回 0。
size_t jp_tone_length(jp_cue_t cue);

// 生成第 first..first+count-1 个采样写入 out；超出提示音长度的部分填 0。
// 纯计算、无分配；out 为 NULL 时直接返回。
void jp_tone_render(jp_cue_t cue, size_t first, int16_t *out, size_t count);
