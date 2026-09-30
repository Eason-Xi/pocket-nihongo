// main/jp_save.c —— 设置与学习进度编解码实现。
#include "jp_save.h"

#include <string.h>

#include "jp_srs.h"

#define SETTINGS_VERSION 1u

void jp_settings_default(jp_settings_t *settings)
{
    if (!settings) return;
    memset(settings, 0, sizeof(*settings));
    settings->volume = 70;
    settings->auto_play = 1;
    settings->brightness = 0;
}

uint8_t jp_brightness_percent(uint8_t level)
{
    static const uint8_t PERCENT[JP_BRIGHTNESS_LEVELS] = { 100, 60, 30 };
    return PERCENT[level < JP_BRIGHTNESS_LEVELS ? level : 0];
}

size_t jp_settings_encode(const jp_settings_t *settings, uint8_t out[JP_SETTINGS_BLOB_SIZE])
{
    out[0] = 'J';
    out[1] = 'S';
    out[2] = SETTINGS_VERSION;
    out[3] = settings->volume;
    out[4] = settings->auto_play;
    out[5] = settings->brightness;
    for (size_t deck = 0; deck < JP_DECK_COUNT; deck++) {
        out[6 + deck * 2] = (uint8_t)(settings->position[deck] & 0xFFu);
        out[7 + deck * 2] = (uint8_t)(settings->position[deck] >> 8);
    }
    return JP_SETTINGS_BLOB_SIZE;
}

bool jp_settings_decode(jp_settings_t *settings, const uint8_t *blob, size_t length)
{
    if (!settings) return false;
    jp_settings_default(settings);
    if (!blob || length != JP_SETTINGS_BLOB_SIZE || blob[0] != 'J' || blob[1] != 'S' ||
        blob[2] != SETTINGS_VERSION) {
        return false;
    }
    // 音量按 10 的步进保存；非整十或超过 100 的值说明存档异常，钳到最近的合法值。
    uint8_t volume = blob[3] > 100 ? 100 : blob[3];
    settings->volume = (uint8_t)((volume + 5u) / 10u * 10u);
    if (settings->volume > 100) settings->volume = 100;
    settings->auto_play = blob[4] ? 1 : 0;
    settings->brightness = blob[5] < JP_BRIGHTNESS_LEVELS ? blob[5] : 0;
    for (size_t deck = 0; deck < JP_DECK_COUNT; deck++) {
        uint16_t position = (uint16_t)(blob[6 + deck * 2] | (blob[7 + deck * 2] << 8));
        settings->position[deck] = position < jp_deck_size((jp_deck_t)deck) ? position : 0;
    }
    return true;
}

static void put_u32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

size_t jp_progress_encode(const uint8_t *boxes, uint8_t out[JP_PROGRESS_BLOB_SIZE])
{
    memcpy(out, "JPPG", 4);
    put_u32(out + 4, JP_CONTENT_HASH);
    memcpy(out + 8, boxes, JP_CARD_TOTAL);
    return JP_PROGRESS_BLOB_SIZE;
}

bool jp_progress_decode(uint8_t *boxes, const uint8_t *blob, size_t length)
{
    if (!boxes) return false;
    memset(boxes, 0, JP_CARD_TOTAL);
    if (!blob || length != JP_PROGRESS_BLOB_SIZE || memcmp(blob, "JPPG", 4) != 0 ||
        get_u32(blob + 4) != JP_CONTENT_HASH) {
        return false;
    }
    for (size_t i = 0; i < JP_CARD_TOTAL; i++) {
        uint8_t box = blob[8 + i];
        boxes[i] = box > JP_SRS_MAX_BOX ? (uint8_t)JP_SRS_MAX_BOX : box;
    }
    return true;
}
