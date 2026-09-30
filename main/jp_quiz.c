// main/jp_quiz.c —— 测验出题与判分实现。
#include "jp_quiz.h"

#include <string.h>

const char *jp_quiz_sound_key(jp_deck_t deck, uint16_t item)
{
    const char *answer = jp_deck_answer(deck, item);
    // 现代日语里 を 读作 o；ぢ/づ 的罗马音本来就写作 ji/zu，与 じ/ず 自然相同。
    if (deck != JP_DECK_WORDS && strcmp(answer, "wo") == 0) return "o";
    return answer;
}

bool jp_quiz_is_listening(jp_qtype_t type)
{
    return type == JP_Q_LISTEN_KANA || type == JP_Q_LISTEN_MEANING;
}

// 判断候选 candidate 能否作为干扰项：不是正确答案本身、读音/释义与已选项都不同，
// 且（same_group 为真时）与正确答案属于同一相似组。
static bool distractor_ok(jp_deck_t deck, uint16_t candidate, const uint16_t *chosen,
                          size_t chosen_count, bool same_group, uint8_t group)
{
    if (same_group && jp_deck_group(deck, candidate) != group) return false;
    const char *key = jp_quiz_sound_key(deck, candidate);
    for (size_t k = 0; k < chosen_count; k++) {
        if (chosen[k] == candidate) return false;
        if (strcmp(jp_quiz_sound_key(deck, chosen[k]), key) == 0) return false;
    }
    return true;
}

bool jp_quiz_make_question(jp_deck_t deck, uint16_t item, bool allow_listen,
                           jp_rng_t *rng, jp_question_t *out)
{
    size_t size = jp_deck_size(deck);
    if (!rng || !out || item >= size) return false;

    // chosen[0] 固定为正确答案，后面依次追加干扰项；最后再随机插入正确位置。
    uint16_t chosen[JP_QUIZ_OPTIONS] = { item };
    size_t chosen_count = 1;
    uint8_t group = jp_deck_group(deck, item);

    // 第 0 轮只在同组里挑，第 1 轮放宽到全卡组，保证小分组（半浊音只有 5 个）也能出题。
    for (int pass = 0; pass < 2 && chosen_count < JP_QUIZ_OPTIONS; pass++) {
        bool same_group = pass == 0;
        while (chosen_count < JP_QUIZ_OPTIONS) {
            uint32_t candidates = 0;
            for (size_t i = 0; i < size; i++) {
                if (distractor_ok(deck, (uint16_t)i, chosen, chosen_count, same_group, group)) candidates++;
            }
            if (candidates == 0) break;
            uint32_t target = jp_rng_below(rng, candidates);
            for (size_t i = 0; i < size; i++) {
                if (!distractor_ok(deck, (uint16_t)i, chosen, chosen_count, same_group, group)) continue;
                if (target-- == 0) {
                    chosen[chosen_count++] = (uint16_t)i;
                    break;
                }
            }
        }
    }
    if (chosen_count < JP_QUIZ_OPTIONS) return false;

    bool listen = allow_listen && jp_rng_below(rng, 2) == 1;
    if (deck == JP_DECK_WORDS) {
        out->type = listen ? JP_Q_LISTEN_MEANING : JP_Q_WORD_MEANING;
    } else {
        out->type = listen ? JP_Q_LISTEN_KANA : JP_Q_READ_KANA;
    }
    out->item = item;
    out->correct = (uint8_t)jp_rng_below(rng, JP_QUIZ_OPTIONS);
    // 把正确答案放到 correct 位置，其余位置按顺序填入干扰项。
    size_t next = 1;
    for (uint8_t slot = 0; slot < JP_QUIZ_OPTIONS; slot++) {
        out->options[slot] = slot == out->correct ? item : chosen[next++];
    }
    return true;
}

bool jp_quiz_start(jp_quiz_t *quiz, jp_deck_t deck, const uint8_t *deck_boxes,
                   bool allow_listen, jp_rng_t *rng)
{
    if (!quiz || !deck_boxes || !rng) return false;
    size_t size = jp_deck_size(deck);
    if (size < JP_QUIZ_OPTIONS) return false;

    memset(quiz, 0, sizeof(*quiz));
    quiz->deck = deck;
    uint16_t items[JP_QUIZ_LENGTH];
    size_t count = jp_srs_pick(deck_boxes, size, JP_QUIZ_LENGTH, rng, items);
    for (size_t i = 0; i < count; i++) {
        if (!jp_quiz_make_question(deck, items[i], allow_listen, rng, &quiz->questions[quiz->count])) continue;
        quiz->count++;
    }
    return quiz->count > 0;
}

const jp_question_t *jp_quiz_current(const jp_quiz_t *quiz)
{
    if (!quiz || quiz->current >= quiz->count) return NULL;
    return &quiz->questions[quiz->current];
}

bool jp_quiz_answer(jp_quiz_t *quiz, uint8_t option)
{
    const jp_question_t *question = jp_quiz_current(quiz);
    if (!question || quiz->answered || option >= JP_QUIZ_OPTIONS) return false;
    quiz->answered = true;
    quiz->chosen = option;
    bool correct = option == question->correct;
    if (correct) {
        quiz->score++;
    } else if (quiz->wrong_count < JP_QUIZ_LENGTH) {
        quiz->wrong_items[quiz->wrong_count++] = question->item;
    }
    return correct;
}

bool jp_quiz_advance(jp_quiz_t *quiz)
{
    if (!quiz || !quiz->answered || quiz->current >= quiz->count) return false;
    quiz->current++;
    quiz->answered = false;
    quiz->chosen = 0;
    return quiz->current < quiz->count;
}

bool jp_quiz_finished(const jp_quiz_t *quiz)
{
    return !quiz || quiz->current >= quiz->count;
}

const char *jp_quiz_option_text(jp_deck_t deck, const jp_question_t *question, uint8_t option)
{
    if (!question || option >= JP_QUIZ_OPTIONS) return "";
    uint16_t item = question->options[option];
    switch (question->type) {
    case JP_Q_READ_KANA:    return jp_deck_answer(deck, item);   // 罗马音
    case JP_Q_LISTEN_KANA:  return jp_deck_face(deck, item);     // 假名
    default:                return jp_deck_answer(deck, item);   // 中文意思
    }
}
