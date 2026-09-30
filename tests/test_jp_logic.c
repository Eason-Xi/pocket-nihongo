// tests/test_jp_logic.c —— 口袋日语纯逻辑模块的主机测试。
//
// 覆盖：卡组索引、UTF-8 计数、SRS 盒子与加权抽样、测验出题/判分、
// 空闲熄屏状态机、设置/进度存档编解码、提示音合成。
// 编译：cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_jp_logic.c main/jp_deck.c \
//       main/jp_data_gen.c main/jp_srs.c main/jp_quiz.c main/jp_power.c main/jp_save.c main/jp_tone.c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "jp_data.h"
#include "jp_power.h"
#include "jp_quiz.h"
#include "jp_save.h"
#include "jp_srs.h"
#include "jp_tone.h"

static void test_deck_indexing(void)
{
    assert(jp_deck_size(JP_DECK_HIRAGANA) == JP_KANA_COUNT);
    assert(jp_deck_size(JP_DECK_KATAKANA) == JP_KANA_COUNT);
    assert(jp_deck_size(JP_DECK_WORDS) == JP_WORD_COUNT);
    assert(jp_deck_size(JP_DECK_COUNT) == 0);

    // 三个卡组的全局卡号连续且不重叠，恰好覆盖 0..JP_CARD_TOTAL-1。
    static uint8_t seen[JP_CARD_TOTAL];
    memset(seen, 0, sizeof(seen));
    for (int deck = 0; deck < JP_DECK_COUNT; deck++) {
        for (size_t i = 0; i < jp_deck_size((jp_deck_t)deck); i++) {
            size_t id = jp_deck_card_id((jp_deck_t)deck, i);
            assert(id < JP_CARD_TOTAL);
            assert(!seen[id]);
            seen[id] = 1;
        }
        assert(jp_deck_card_id((jp_deck_t)deck, jp_deck_size((jp_deck_t)deck)) == JP_CARD_TOTAL);
    }
    for (size_t i = 0; i < JP_CARD_TOTAL; i++) assert(seen[i]);

    // 平/片假名共用发音片段，单词片段接在假名之后且不越界。
    assert(jp_deck_voice(JP_DECK_HIRAGANA, 5) == jp_deck_voice(JP_DECK_KATAKANA, 5));
    assert(jp_deck_voice(JP_DECK_WORDS, 0) == JP_KANA_COUNT);
    assert(jp_deck_voice(JP_DECK_WORDS, JP_WORD_COUNT - 1) == JP_VOICE_CLIP_COUNT - 1);
    assert(jp_deck_voice(JP_DECK_WORDS, JP_WORD_COUNT) == UINT16_MAX);

    assert(strcmp(jp_deck_face(JP_DECK_HIRAGANA, 0), "あ") == 0);
    assert(strcmp(jp_deck_face(JP_DECK_KATAKANA, 0), "ア") == 0);
    assert(strcmp(jp_deck_answer(JP_DECK_KATAKANA, 0), "a") == 0);
    assert(strcmp(jp_deck_face(JP_DECK_HIRAGANA, 999), "") == 0);

    assert(jp_utf8_count("") == 0);
    assert(jp_utf8_count("abc") == 3);
    assert(jp_utf8_count("おやすみなさい") == 7);
    assert(jp_utf8_count("学生a") == 3);
    assert(jp_utf8_count(NULL) == 0);

    static uint8_t boxes[JP_CARD_TOTAL];
    memset(boxes, 0, sizeof(boxes));
    boxes[jp_deck_card_id(JP_DECK_KATAKANA, 3)] = JP_SRS_MASTERED_BOX;
    boxes[jp_deck_card_id(JP_DECK_KATAKANA, 4)] = JP_SRS_MASTERED_BOX - 1;
    assert(jp_deck_mastered(JP_DECK_KATAKANA, boxes, JP_SRS_MASTERED_BOX) == 1);
    assert(jp_deck_mastered(JP_DECK_HIRAGANA, boxes, JP_SRS_MASTERED_BOX) == 0);
}

static void test_srs(void)
{
    assert(jp_srs_after_answer(0, true) == 1);
    assert(jp_srs_after_answer(JP_SRS_MAX_BOX, true) == JP_SRS_MAX_BOX);
    assert(jp_srs_after_answer(4, false) == 0);
    assert(jp_srs_after_answer(200, true) == JP_SRS_MAX_BOX);
    assert(jp_srs_weight(0) > jp_srs_weight(3));
    assert(jp_srs_weight(99) == jp_srs_weight(JP_SRS_MAX_BOX));

    jp_rng_t rng;
    jp_rng_seed(&rng, 0);
    assert(rng.state != 0);
    for (int i = 0; i < 1000; i++) assert(jp_rng_below(&rng, 7) < 7);
    assert(jp_rng_below(&rng, 0) == 0);

    // 抽样互不重复；want 超过 n 时只返回 n 个。
    uint8_t boxes[20] = { 0 };
    uint16_t out[20];
    for (int round = 0; round < 50; round++) {
        size_t got = jp_srs_pick(boxes, 20, 10, &rng, out);
        assert(got == 10);
        for (size_t a = 0; a < got; a++) {
            assert(out[a] < 20);
            for (size_t b = a + 1; b < got; b++) assert(out[a] != out[b]);
        }
    }
    assert(jp_srs_pick(boxes, 4, 10, &rng, out) == 4);

    // 权重偏向：一半卡片盒子为 5、一半为 0，盒子 0 的卡被抽中的次数应明显更多。
    uint8_t mixed[20];
    for (int i = 0; i < 20; i++) mixed[i] = i < 10 ? 0 : JP_SRS_MAX_BOX;
    int low = 0, high = 0;
    for (int round = 0; round < 400; round++) {
        size_t got = jp_srs_pick(mixed, 20, 3, &rng, out);
        for (size_t k = 0; k < got; k++) {
            if (out[k] < 10) low++;
            else high++;
        }
    }
    assert(low > high * 3);
}

static void test_quiz(void)
{
    jp_rng_t rng;
    jp_rng_seed(&rng, 12345);

    // 每个卡组、每张卡都能出题，选项互不相同且读音/释义不重复。
    for (int deck = 0; deck < JP_DECK_COUNT; deck++) {
        for (uint16_t item = 0; item < jp_deck_size((jp_deck_t)deck); item++) {
            jp_question_t q;
            assert(jp_quiz_make_question((jp_deck_t)deck, item, true, &rng, &q));
            assert(q.correct < JP_QUIZ_OPTIONS);
            assert(q.options[q.correct] == item);
            for (uint8_t a = 0; a < JP_QUIZ_OPTIONS; a++) {
                for (uint8_t b = a + 1; b < JP_QUIZ_OPTIONS; b++) {
                    assert(q.options[a] != q.options[b]);
                    assert(strcmp(jp_quiz_sound_key((jp_deck_t)deck, q.options[a]),
                                  jp_quiz_sound_key((jp_deck_t)deck, q.options[b])) != 0);
                }
            }
            if (deck == JP_DECK_WORDS) {
                assert(q.type == JP_Q_WORD_MEANING || q.type == JP_Q_LISTEN_MEANING);
            } else {
                assert(q.type == JP_Q_READ_KANA || q.type == JP_Q_LISTEN_KANA);
            }
        }
    }

    // を 与 お 同音，绝不能同时作为选项出现。
    size_t wo = JP_KANA_COUNT, o = JP_KANA_COUNT;
    for (size_t i = 0; i < JP_KANA_COUNT; i++) {
        if (strcmp(JP_KANA[i].hira, "を") == 0) wo = i;
        if (strcmp(JP_KANA[i].hira, "お") == 0) o = i;
    }
    assert(wo < JP_KANA_COUNT && o < JP_KANA_COUNT);
    for (int round = 0; round < 300; round++) {
        jp_question_t q;
        assert(jp_quiz_make_question(JP_DECK_HIRAGANA, (uint16_t)wo, true, &rng, &q));
        for (uint8_t k = 0; k < JP_QUIZ_OPTIONS; k++) assert(q.options[k] != o);
    }

    // 不允许听力题时只出「看」类题目。
    for (int round = 0; round < 100; round++) {
        jp_question_t q;
        assert(jp_quiz_make_question(JP_DECK_WORDS, 7, false, &rng, &q));
        assert(!jp_quiz_is_listening(q.type));
    }

    // 完整走一轮：前 3 题答对、其余答错，分数与错题记录吻合。
    uint8_t boxes[JP_WORD_COUNT] = { 0 };
    jp_quiz_t quiz;
    assert(jp_quiz_start(&quiz, JP_DECK_WORDS, boxes, true, &rng));
    assert(quiz.count == JP_QUIZ_LENGTH);
    assert(!jp_quiz_advance(&quiz));   // 未作答不能跳题
    for (uint8_t i = 0; i < quiz.count; i++) {
        const jp_question_t *q = jp_quiz_current(&quiz);
        assert(q);
        uint8_t option = i < 3 ? q->correct : (uint8_t)((q->correct + 1) % JP_QUIZ_OPTIONS);
        assert(jp_quiz_answer(&quiz, option) == (i < 3));
        assert(!jp_quiz_answer(&quiz, q->correct));   // 同一题不能重复作答
        assert(strlen(jp_quiz_option_text(JP_DECK_WORDS, q, 0)) > 0);
        bool more = jp_quiz_advance(&quiz);
        assert(more == (i + 1 < quiz.count));
    }
    assert(jp_quiz_finished(&quiz));
    assert(quiz.score == 3);
    assert(quiz.wrong_count == JP_QUIZ_LENGTH - 3);
    assert(jp_quiz_current(&quiz) == NULL);
    assert(!jp_quiz_answer(&quiz, 0));

    // 一轮内题目不重复。
    assert(jp_quiz_start(&quiz, JP_DECK_HIRAGANA, boxes, false, &rng));
    for (uint8_t a = 0; a < quiz.count; a++) {
        assert(quiz.questions[a].type == JP_Q_READ_KANA);
        for (uint8_t b = a + 1; b < quiz.count; b++) {
            assert(quiz.questions[a].item != quiz.questions[b].item);
        }
    }
    const jp_question_t *first = jp_quiz_current(&quiz);
    assert(strcmp(jp_quiz_option_text(JP_DECK_HIRAGANA, first, first->correct),
                  JP_KANA[first->item].romaji) == 0);
}

static void test_power(void)
{
    jp_idle_t idle;
    jp_idle_init(&idle, 1000, 30000, 90000);
    assert(jp_idle_update(&idle, 1000 + 29999) == JP_POWER_ACTIVE);
    assert(jp_idle_update(&idle, 1000 + 30000) == JP_POWER_DIM);
    assert(!jp_idle_input(&idle, 1000 + 40000));   // 调暗时按键照常生效
    assert(idle.state == JP_POWER_ACTIVE);
    assert(jp_idle_update(&idle, 1000 + 40000 + 90000) == JP_POWER_OFF);
    // 播放活动不会点亮已熄灭的屏幕，只有按键可以，而且这次按键被吞掉。
    jp_idle_touch(&idle, 200000);
    assert(jp_idle_update(&idle, 200001) == JP_POWER_OFF);
    assert(jp_idle_input(&idle, 200002));
    assert(jp_idle_update(&idle, 200003) == JP_POWER_ACTIVE);
    // 时间戳回绕：UINT32_MAX 附近的间隔依然正确。
    jp_idle_init(&idle, UINT32_MAX - 10, 30000, 90000);
    assert(jp_idle_update(&idle, 20) == JP_POWER_ACTIVE);
    assert(jp_idle_update(&idle, 29990) == JP_POWER_DIM);
    // off 早于 dim 的配置被修正。
    jp_idle_init(&idle, 0, 5000, 100);
    assert(idle.off_after_ms == 5000);
}

static void test_save(void)
{
    jp_settings_t settings, decoded;
    jp_settings_default(&settings);
    assert(settings.volume == 70 && settings.auto_play == 1 && settings.brightness == 0);
    settings.volume = 40;
    settings.auto_play = 0;
    settings.brightness = 2;
    settings.position[JP_DECK_WORDS] = 150;
    uint8_t blob[JP_SETTINGS_BLOB_SIZE];
    assert(jp_settings_encode(&settings, blob) == JP_SETTINGS_BLOB_SIZE);
    assert(jp_settings_decode(&decoded, blob, sizeof(blob)));
    assert(memcmp(&settings, &decoded, sizeof(settings)) == 0);

    // 越界字段被钳制：位置超出卡组、亮度档位非法、音量非整十。
    blob[3] = 47;
    blob[5] = 9;
    blob[10] = 0xFF;
    blob[11] = 0xFF;
    assert(jp_settings_decode(&decoded, blob, sizeof(blob)));
    assert(decoded.volume == 50 && decoded.brightness == 0 && decoded.position[JP_DECK_WORDS] == 0);
    // 损坏存档回落默认值。
    blob[0] = 'X';
    assert(!jp_settings_decode(&decoded, blob, sizeof(blob)));
    assert(decoded.volume == 70);
    assert(!jp_settings_decode(&decoded, blob, 3));
    assert(jp_brightness_percent(0) == 100 && jp_brightness_percent(2) == 30 &&
           jp_brightness_percent(7) == 100);

    static uint8_t boxes[JP_CARD_TOTAL], loaded[JP_CARD_TOTAL];
    static uint8_t pblob[JP_PROGRESS_BLOB_SIZE];
    for (size_t i = 0; i < JP_CARD_TOTAL; i++) boxes[i] = (uint8_t)(i % (JP_SRS_MAX_BOX + 1));
    assert(jp_progress_encode(boxes, pblob) == JP_PROGRESS_BLOB_SIZE);
    assert(jp_progress_decode(loaded, pblob, sizeof(pblob)));
    assert(memcmp(boxes, loaded, sizeof(boxes)) == 0);
    pblob[8] = 200;   // 盒子值越界 → 钳到上限
    assert(jp_progress_decode(loaded, pblob, sizeof(pblob)));
    assert(loaded[0] == JP_SRS_MAX_BOX);
    pblob[4] ^= 0xFF;  // 内容哈希不符（卡片表已变化）→ 旧进度作废
    assert(!jp_progress_decode(loaded, pblob, sizeof(pblob)));
    for (size_t i = 0; i < JP_CARD_TOTAL; i++) assert(loaded[i] == 0);
}

static void test_tone(void)
{
    assert(jp_tone_length(JP_CUE_NONE) == 0);
    size_t length = jp_tone_length(JP_CUE_CORRECT);
    assert(length == (70 + 150) * 16);
    static int16_t whole[8000], chunked[8000];
    assert(length <= 8000);
    jp_tone_render(JP_CUE_CORRECT, 0, whole, length);
    // 任意分块渲染结果与一次性渲染逐采样一致（语音任务按块输出）。
    for (size_t first = 0; first < length; first += 97) {
        size_t n = length - first < 97 ? length - first : 97;
        jp_tone_render(JP_CUE_CORRECT, first, chunked + first, n);
    }
    assert(memcmp(whole, chunked, length * sizeof(int16_t)) == 0);
    int peak = 0;
    for (size_t i = 0; i < length; i++) {
        int v = whole[i] < 0 ? -whole[i] : whole[i];
        if (v > peak) peak = v;
    }
    assert(peak > 5000 && peak <= 11000);
    assert(whole[0] == 0);   // 起音从 0 开始，没有咔哒声
    int16_t tail[4] = { 1, 1, 1, 1 };
    jp_tone_render(JP_CUE_WRONG, jp_tone_length(JP_CUE_WRONG), tail, 4);
    assert(tail[0] == 0 && tail[3] == 0);   // 超出长度补 0
}

int main(void)
{
    test_deck_indexing();
    test_srs();
    test_quiz();
    test_power();
    test_save();
    test_tone();
    printf("test_jp_logic: PASS\n");
    return 0;
}
