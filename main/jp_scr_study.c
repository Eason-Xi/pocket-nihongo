// main/jp_scr_study.c —— 学习卡片：逐张浏览假名或单词，OK 播放发音。
//
// 布局（y 坐标）：
//   0..44     页眉（卡组名）+ 电量
//   48..65    左：分类「清音 · か行」/「饮食」；右：位置「7 / 104」
//   70..266   卡片（圆角和纸白，细描边）
//     假名：72 px 大字 → 32 px 罗马音（藍色）→ 对应的片/平假名 → 掌握度圆点
//     单词：32 px 写法（超过 6 字改 20 px）→ 读音 → 罗马音 → 分隔线 → 中文释义 → 圆点
//   289..306  操作提示
// 按键：▲ 上一张、▼ 下一张（首尾循环），OK 发音，长按 OK 返回首页并记住位置。
// 右上角喇叭图标在播放时亮起（tick 每 200 ms 同步一次）。
#include <string.h>

#include "jp_app.h"
#include "jp_text.h"
#include "jp_ui.h"
#include "jp_voice.h"

#define CARD_X  18
#define CARD_Y  70
#define CARD_W  204
#define CARD_H  196

static jp_deck_t s_deck;
static size_t s_index;
static lv_obj_t *s_section;     // 分类标签
static lv_obj_t *s_position;    // 位置标签
static lv_obj_t *s_card;        // 卡片容器；换卡时清空子对象重建内容
static lv_obj_t *s_speaker;     // 播放指示图标（卡片外、页面上，换卡时保留）
static lv_obj_t *s_dots[JP_UI_DOTS];
static bool s_speaker_shown;

static const char *deck_title(jp_deck_t deck)
{
    switch (deck) {
    case JP_DECK_HIRAGANA: return JP_TXT_MENU_HIRAGANA;
    case JP_DECK_KATAKANA: return JP_TXT_MENU_KATAKANA;
    default:               return JP_TXT_MENU_WORDS;
    }
}

// 假名卡内容。
static void render_kana(const jp_kana_t *kana)
{
    bool hira = s_deck == JP_DECK_HIRAGANA;
    lv_obj_t *face = jp_ui_label(s_card, JP_FONT_72, JP_COLOR_INK, hira ? kana->hira : kana->kata);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *romaji = jp_ui_label(s_card, JP_FONT_32, JP_COLOR_AI, kana->romaji);
    lv_obj_align(romaji, LV_ALIGN_TOP_MID, 0, 96);

    lv_obj_t *pair = jp_ui_label(s_card, JP_FONT_14, JP_COLOR_INK_SOFT, "");
    lv_label_set_text_fmt(pair, hira ? JP_TXT_COUNTER_KATA : JP_TXT_COUNTER_HIRA, hira ? kana->kata : kana->hira);
    lv_obj_align(pair, LV_ALIGN_TOP_MID, 0, 142);

    lv_label_set_text_fmt(s_section, JP_TXT_SECTION_FMT, JP_KANA_GROUP_NAMES[kana->group],
                          hira ? kana->hira_row : kana->kata_row);
}

// 单词卡内容。
static void render_word(const jp_word_t *word)
{
    // 32 px 下每字约 32 px 宽，卡片内宽 204，超过 6 字就改用 20 px 避免换行。
    lv_obj_t *face = jp_ui_label(s_card, jp_ui_font_for(word->word, 6, JP_FONT_32, JP_FONT_20),
                                 JP_COLOR_INK, word->word);
    lv_obj_align(face, LV_ALIGN_TOP_MID, 0, jp_utf8_count(word->word) <= 6 ? 18 : 24);

    if (strcmp(word->word, word->reading) != 0) {
        lv_obj_t *reading = jp_ui_label(s_card, JP_FONT_20, JP_COLOR_AI, word->reading);
        lv_obj_align(reading, LV_ALIGN_TOP_MID, 0, 62);
        lv_obj_t *romaji = jp_ui_label(s_card, JP_FONT_14, JP_COLOR_INK_SOFT, word->romaji);
        lv_obj_align(romaji, LV_ALIGN_TOP_MID, 0, 90);
    } else {
        // 纯假名单词（ありがとう、コーヒー）写法即读音，不再重复一行，
        // 罗马音放大到读音的位置。
        lv_obj_t *romaji = jp_ui_label(s_card, JP_FONT_20, JP_COLOR_AI, word->romaji);
        lv_obj_align(romaji, LV_ALIGN_TOP_MID, 0, 70);
    }

    lv_obj_t *line = jp_ui_box(s_card, 32, 116, CARD_W - 64, 1, JP_COLOR_LINE, 0);
    (void)line;

    lv_obj_t *meaning = jp_ui_label(s_card, JP_FONT_20, JP_COLOR_INK, word->meaning);
    lv_obj_set_width(meaning, CARD_W - 24);
    lv_obj_set_style_text_align(meaning, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(meaning, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(meaning, LV_ALIGN_TOP_MID, 0, 128);

    lv_label_set_text(s_section, JP_WORD_CATEGORY_NAMES[word->category]);
}

// 重建当前卡片；autoplay 为真且设置允许时自动发音。
static void render_card(bool autoplay)
{
    jp_app_state_t *app = jp_app();
    lv_obj_clean(s_card);
    for (int i = 0; i < JP_UI_DOTS; i++) s_dots[i] = NULL;

    if (s_deck == JP_DECK_WORDS) render_word(&JP_WORDS[s_index]);
    else render_kana(&JP_KANA[s_index]);

    jp_ui_dots_create(s_dots, s_card, (CARD_W - (JP_UI_DOTS * 14 - 6)) / 2, CARD_H - 22);
    jp_ui_dots_set(s_dots, app->boxes[jp_deck_card_id(s_deck, s_index)]);

    lv_label_set_text_fmt(s_position, "%u / %u", (unsigned)(s_index + 1), (unsigned)jp_deck_size(s_deck));
    lv_obj_align(s_position, LV_ALIGN_TOP_RIGHT, -24, 48);

    app->settings.position[s_deck] = (uint16_t)s_index;
    if (autoplay && app->settings.auto_play) jp_voice_play(jp_deck_voice(s_deck, s_index), JP_CUE_NONE);
}

static void study_build(lv_obj_t *screen, int arg)
{
    jp_app_state_t *app = jp_app();
    s_deck = arg >= 0 && arg < JP_DECK_COUNT ? (jp_deck_t)arg : JP_DECK_HIRAGANA;
    s_index = app->settings.position[s_deck];
    if (s_index >= jp_deck_size(s_deck)) s_index = 0;

    jp_ui_header(screen, deck_title(s_deck));
    s_section = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, "");
    lv_obj_set_pos(s_section, 24, 48);
    s_position = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, "");

    s_card = jp_ui_box(screen, CARD_X, CARD_Y, CARD_W, CARD_H, JP_COLOR_CARD, 18);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(JP_COLOR_LINE), 0);

    s_speaker = jp_ui_label(screen, JP_FONT_20, JP_COLOR_SHU, LV_SYMBOL_VOLUME_MAX);
    lv_obj_set_pos(s_speaker, CARD_X + CARD_W - 34, CARD_Y + 10);
    lv_obj_add_flag(s_speaker, LV_OBJ_FLAG_HIDDEN);
    s_speaker_shown = false;

    jp_ui_footer(screen, JP_TXT_STUDY_HINT);
    render_card(true);
}

static void study_destroy(void)
{
    s_section = s_position = s_card = s_speaker = NULL;
    for (int i = 0; i < JP_UI_DOTS; i++) s_dots[i] = NULL;
}

static void study_key(jp_key_t key)
{
    size_t size = jp_deck_size(s_deck);
    switch (key) {
    case JP_KEY_UP:
        s_index = (s_index + size - 1) % size;
        render_card(true);
        break;
    case JP_KEY_DOWN:
        s_index = (s_index + 1) % size;
        render_card(true);
        break;
    case JP_KEY_OK:
        jp_voice_play(jp_deck_voice(s_deck, s_index), JP_CUE_NONE);
        break;
    case JP_KEY_BACK:
        jp_voice_stop();
        jp_app_mark_dirty(true, false);   // 记住本卡组看到的位置
        jp_app_goto(JP_SCR_HOME, (int)s_deck);   // 首页行号与卡组编号一致
        break;
    }
}

// 同步喇叭图标与播放状态；只在状态变化时改对象，避免每 200 ms 触发重绘。
static void study_tick(void)
{
    bool busy = jp_voice_busy();
    if (!s_speaker || busy == s_speaker_shown) return;
    s_speaker_shown = busy;
    if (busy) lv_obj_remove_flag(s_speaker, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_speaker, LV_OBJ_FLAG_HIDDEN);
}

const jp_screen_ops_t JP_SCREEN_STUDY = {
    .build = study_build,
    .destroy = study_destroy,
    .key = study_key,
    .tick = study_tick,
};
