// main/jp_quiz.h —— 测验出题与判分（纯逻辑，可主机测试）。
//
// 一轮测验固定 JP_QUIZ_LENGTH 道三选一题，题目按 SRS 权重从所选卡组抽取：
//   假名卡组：看假名选罗马音 / 听发音选假名；
//   单词卡组：看单词选中文意思 / 听发音选中文意思。
// 语音不可用（语音包缺失或校验失败）时只出「看」类题目。
// 干扰项优先取自同一相似组（同为清音、同为「饮食」类单词…），且三个选项的
// 答案文本/读音两两不同，避免出现两个都对的选项。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "jp_data.h"
#include "jp_srs.h"

#define JP_QUIZ_LENGTH  10u   // 每轮题数
#define JP_QUIZ_OPTIONS 3u    // 每题选项数（三个按键的设备上三选一最顺手）

typedef enum {
    JP_Q_READ_KANA = 0,    // 看假名 → 选罗马音
    JP_Q_LISTEN_KANA,      // 听发音 → 选假名
    JP_Q_WORD_MEANING,     // 看单词 → 选中文意思
    JP_Q_LISTEN_MEANING,   // 听发音 → 选中文意思
} jp_qtype_t;

// 一道题。所有下标均为「卡组内下标」。
typedef struct {
    jp_qtype_t type;
    uint16_t item;                        // 正确答案对应的卡片
    uint16_t options[JP_QUIZ_OPTIONS];    // 选项对应的卡片，其中 options[correct] == item
    uint8_t correct;                      // 正确选项位置 0..2
} jp_question_t;

// 一轮测验的全部状态。结构体约 110 字节，由应用静态持有。
typedef struct {
    jp_deck_t deck;                          // 测验卡组
    uint8_t count;                           // 本轮题数（卡组不足 10 张时更少）
    uint8_t current;                         // 当前题号 0..count；== count 表示已结束
    uint8_t score;                           // 已答对题数
    bool answered;                           // 当前题是否已作答
    uint8_t chosen;                          // 当前题所选选项（answered 为真时有效）
    uint8_t wrong_count;                     // 答错题数
    uint16_t wrong_items[JP_QUIZ_LENGTH];    // 答错的卡片（结果页展示）
    jp_question_t questions[JP_QUIZ_LENGTH];
} jp_quiz_t;

// 以 item 为正确答案生成一道题；allow_listen 为假时不出听力题。
// 卡组不足 3 个互不相同的答案时返回 false。
bool jp_quiz_make_question(jp_deck_t deck, uint16_t item, bool allow_listen,
                           jp_rng_t *rng, jp_question_t *out);

// 开始新一轮：deck_boxes[i] 为卡组第 i 张卡的盒子等级（长度 jp_deck_size(deck)）。
// 成功返回 true，quiz 被完整重置。
bool jp_quiz_start(jp_quiz_t *quiz, jp_deck_t deck, const uint8_t *deck_boxes,
                   bool allow_listen, jp_rng_t *rng);

// 当前题；已结束或 quiz 为空时返回 NULL。
const jp_question_t *jp_quiz_current(const jp_quiz_t *quiz);

// 作答当前题。返回是否答对；已作答、已结束或选项越界时忽略并返回 false。
bool jp_quiz_answer(jp_quiz_t *quiz, uint8_t option);

// 进入下一题（仅在已作答后有效）。返回 false 表示本轮已结束。
bool jp_quiz_advance(jp_quiz_t *quiz);

// 本轮是否已结束。
bool jp_quiz_finished(const jp_quiz_t *quiz);

// 是否为听力题（题面不显示文字，只播放发音）。
bool jp_quiz_is_listening(jp_qtype_t type);

// 选项显示文字：看假名题显示罗马音，听假名题显示假名，单词题显示中文意思。
const char *jp_quiz_option_text(jp_deck_t deck, const jp_question_t *question, uint8_t option);

// 读音比较键：を 与 お 同音、ぢ/づ 与 じ/ず 同音，出题时视为相同答案。
const char *jp_quiz_sound_key(jp_deck_t deck, uint16_t item);
