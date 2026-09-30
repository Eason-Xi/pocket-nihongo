// main/jp_save.h —— 设置与学习进度的存档格式（纯逻辑，可主机测试）。
//
// 实际读写 NVS 由 jp_store.c 负责；这里只做「结构体 ⇄ 字节块」的编解码与校验，
// 任何损坏、旧版本或内容已变化的存档都会被识别并回落到默认值，而不是读出错乱数据。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "jp_data.h"

// 用户设置。position 记录每个卡组上次看到的卡片，重新进入时从那里继续。
typedef struct {
    uint8_t volume;                       // 音量 0..100，步进 10
    uint8_t auto_play;                    // 1 = 切换卡片时自动发音
    uint8_t brightness;                   // 亮度档位：0 高 / 1 中 / 2 低
    uint16_t position[JP_DECK_COUNT];     // 各卡组上次位置（卡组内下标）
} jp_settings_t;

#define JP_BRIGHTNESS_LEVELS   3u
#define JP_SETTINGS_BLOB_SIZE  12u                     // 'J''S' + 版本 + 3 字节设置 + 3×u16
#define JP_PROGRESS_BLOB_SIZE  (8u + JP_CARD_TOTAL)    // "JPPG" + 内容哈希 + 每卡 1 字节盒子

// 默认设置：音量 70、自动发音开、亮度高、所有卡组从头开始。
void jp_settings_default(jp_settings_t *settings);

// 亮度档位 → 背光百分比（100 / 60 / 30）；越界按最高档处理。
uint8_t jp_brightness_percent(uint8_t level);

// 编码为定长字节块，返回写入字节数（恒为 JP_SETTINGS_BLOB_SIZE）。
size_t jp_settings_encode(const jp_settings_t *settings, uint8_t out[JP_SETTINGS_BLOB_SIZE]);

// 解码并校验。成功返回 true；失败时 settings 被填为默认值并返回 false。
// 各字段超范围时钳制到合法值（例如位置超过卡组大小时回到 0）。
bool jp_settings_decode(jp_settings_t *settings, const uint8_t *blob, size_t length);

// 进度编码：boxes 长度为 JP_CARD_TOTAL，写入内容哈希以便内容变化时作废旧进度。
size_t jp_progress_encode(const uint8_t *boxes, uint8_t out[JP_PROGRESS_BLOB_SIZE]);

// 进度解码。magic/长度/内容哈希不符时把 boxes 清零并返回 false；盒子值超上限时钳制。
bool jp_progress_decode(uint8_t *boxes, const uint8_t *blob, size_t length);
