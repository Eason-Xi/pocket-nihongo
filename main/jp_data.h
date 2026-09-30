// main/jp_data.h —— 口袋日语的学习内容表与「卡组」抽象。
//
// 数据本身由 tools/jp_learner/gen_data.py 生成到 jp_data_gen.c / jp_data_gen.h；
// 这里只声明结构体与纯函数，不依赖 ESP-IDF / LVGL，可直接在主机单元测试中使用。
//
// 术语：
//   卡组(deck)   平假名、片假名、单词三套，首页三个学习入口一一对应；
//   卡片下标     卡组内的顺序号 0..jp_deck_size()-1；
//   全局卡号     三套卡组连续编号 0..JP_CARD_TOTAL-1，学习进度(SRS 盒子)按它存储；
//   发音片段号   语音包中的片段下标；平/片假名同一读音共用一个片段。
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "jp_data_gen.h"

// 卡组编号。顺序即 NVS 进度数组中的分段顺序，改动会让旧进度错位，勿随意调整。
typedef enum {
    JP_DECK_HIRAGANA = 0,
    JP_DECK_KATAKANA,
    JP_DECK_WORDS,
    JP_DECK_COUNT,
} jp_deck_t;

// 一个假名音节。字符串均为 Flash 常量（UTF-8），生命周期为整个程序。
typedef struct {
    const char *hira;       // 平假名，如 "きゃ"
    const char *kata;       // 对应片假名，如 "キャ"
    const char *romaji;     // 平文式罗马音，如 "kya"
    const char *hira_row;   // 平假名行标签，如 "か行"；ん 为 "拨音"
    const char *kata_row;   // 片假名行标签，如 "カ行"
    uint8_t group;          // JP_KANA_GROUP_NAMES 下标：0 清音 1 浊音 2 半浊音 3 拗音
    uint16_t voice;         // 发音片段号
} jp_kana_t;

// 一个 N5 单词。
typedef struct {
    const char *word;       // 显示写法（汉字或假名）
    const char *reading;    // 假名读音
    const char *romaji;     // 罗马音
    const char *meaning;    // 中文释义
    uint8_t category;       // JP_WORD_CATEGORY_NAMES 下标
    uint16_t voice;         // 发音片段号（>= JP_KANA_COUNT）
} jp_word_t;

extern const jp_kana_t JP_KANA[JP_KANA_COUNT];
extern const jp_word_t JP_WORDS[JP_WORD_COUNT];
extern const char *const JP_KANA_GROUP_NAMES[JP_KANA_GROUP_COUNT];
extern const char *const JP_WORD_CATEGORY_NAMES[JP_WORD_CATEGORY_COUNT];

// 卡组内卡片数；非法 deck 返回 0。
size_t jp_deck_size(jp_deck_t deck);

// 卡组内下标 → 全局卡号（学习进度数组下标）。越界时返回 JP_CARD_TOTAL 作为无效值。
size_t jp_deck_card_id(jp_deck_t deck, size_t index);

// 卡组内下标 → 发音片段号；越界返回 UINT16_MAX。
uint16_t jp_deck_voice(jp_deck_t deck, size_t index);

// 卡片的「题面」文字：假名卡为该卡组写法（平或片），单词卡为显示写法。越界返回 ""。
const char *jp_deck_face(jp_deck_t deck, size_t index);

// 卡片的读音答案：假名卡为罗马音，单词卡为中文释义。测验比较选项是否重复时使用。
const char *jp_deck_answer(jp_deck_t deck, size_t index);

// 相似度分组：假名卡返回大类(清音/浊音…)，单词卡返回分类。测验优先从同组挑干扰项。
uint8_t jp_deck_group(jp_deck_t deck, size_t index);

// 统计「已掌握」卡片数：boxes 为全局进度数组(长度 JP_CARD_TOTAL)，盒子 >= mastered_box 计为掌握。
size_t jp_deck_mastered(jp_deck_t deck, const uint8_t *boxes, uint8_t mastered_box);

// 统计一个 UTF-8 字符串的码点数（不校验编码合法性，续字节 10xxxxxx 不计数）。
size_t jp_utf8_count(const char *text);
