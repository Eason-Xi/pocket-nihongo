// main/jp_voice.h —— 发音与提示音播放服务（独立 FreeRTOS 任务）。
//
// 线程模型：
//   * 所有公开函数都是非阻塞的，可在应用任务中随时调用（不要在按键回调里调用，
//     按键回调只入队）。
//   * 播放请求只保留「最新一条」：连续快速翻卡时，新请求会在下一个 16 ms 分块边界
//     打断旧的播放，不会排队把一串发音全部读完。
//   * 真正的 bsp_audio_* 调用（格式、音量、PCM 写入）只发生在语音任务内部，
//     避免 codec 被两个任务并发操作。
// 资源：任务栈 3 KB，PCM 分块缓冲 512 B 静态分配；语音数据直接从 Flash 读取解码。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "jp_tone.h"

#define JP_VOICE_NO_CLIP 0xFFFFu   // 只播放提示音、不播放发音时传入

// 初始化：校验内嵌语音包并创建播放任务。必须在 bsp_audio_init() 成功之后调用。
// 返回：
//   ESP_OK                 语音包有效，发音与提示音都可用；
//   ESP_ERR_INVALID_CRC    语音包损坏或与学习内容不匹配（仅提示音可用）；
//   ESP_ERR_NO_MEM         任务创建失败（完全静音）。
// 重复调用直接返回首次结果。
esp_err_t jp_voice_init(uint8_t volume_percent);

// 发音片段是否可用（语音包通过校验且任务在运行）。
bool jp_voice_clips_ready(void);

// 请求播放：先播 cue 提示音（可为 JP_CUE_NONE），再播 clip 发音（可为 JP_VOICE_NO_CLIP）。
// 会打断正在进行的播放。服务不可用时静默忽略。
void jp_voice_play(uint16_t clip, jp_cue_t cue);

// 停止当前播放（在下一个分块边界生效，最长约 16 ms + DMA 中已排队的 ≤90 ms 音频）。
void jp_voice_stop(void);

// 设置音量 0..100，由语音任务在下一次播放前应用。
void jp_voice_set_volume(uint8_t percent);

// 当前是否正在输出声音。用于空闲计时：播放中不调暗屏幕。
bool jp_voice_busy(void);
