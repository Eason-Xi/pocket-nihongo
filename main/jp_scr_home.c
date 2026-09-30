// main/jp_scr_home.c —— 首页：总进度 + 五个入口（平假名 / 片假名 / 单词 / 测验 / 设置）。
//
// 布局（y 坐标）：
//   0..44    页眉「口袋日语」+ 电量
//   48..64   「已掌握 X / 361」
//   68..74   总进度条（朱红）
//   82..282  五个菜单行，每行 36 px、间隔 5 px
//   289..306 操作提示
// 按键：▲/▼ 循环移动选中行，OK 进入，长按无动作（已在顶层）。
#include <stdio.h>

#include "jp_app.h"
#include "jp_text.h"
#include "jp_ui.h"
#include "jp_voice.h"

#define ROW_COUNT   5
#define ROW_TOP     82
#define ROW_HEIGHT  36
#define ROW_GAP     5

static jp_row_t s_rows[ROW_COUNT];
static int s_selected;
static bool s_voice_warned;   // 语音不可用的提示每次开机只弹一次

// 行 → 目标页面与参数。
static void enter_row(int row)
{
    switch (row) {
    case 0: jp_app_goto(JP_SCR_STUDY, JP_DECK_HIRAGANA); break;
    case 1: jp_app_goto(JP_SCR_STUDY, JP_DECK_KATAKANA); break;
    case 2: jp_app_goto(JP_SCR_STUDY, JP_DECK_WORDS); break;
    case 3: jp_app_goto(JP_SCR_QUIZ_MENU, 0); break;
    default: jp_app_goto(JP_SCR_SETTINGS, 0); break;
    }
}

static void refresh_selection(void)
{
    for (int i = 0; i < ROW_COUNT; i++) jp_ui_row_select(&s_rows[i], i == s_selected);
}

static void home_build(lv_obj_t *screen, int arg)
{
    jp_app_state_t *app = jp_app();
    jp_ui_header(screen, JP_TXT_APP_NAME);

    // 总掌握数：三个卡组合计。
    size_t mastered = 0;
    size_t deck_mastered[JP_DECK_COUNT];
    for (int deck = 0; deck < JP_DECK_COUNT; deck++) {
        deck_mastered[deck] = jp_deck_mastered((jp_deck_t)deck, app->boxes, JP_SRS_MASTERED_BOX);
        mastered += deck_mastered[deck];
    }
    lv_obj_t *summary = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, "");
    lv_label_set_text_fmt(summary, JP_TXT_HOME_MASTERED, (unsigned)mastered, (unsigned)JP_CARD_TOTAL);
    lv_obj_set_pos(summary, 24, 47);

    lv_obj_t *bar = lv_bar_create(screen);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, 24, 68);
    lv_obj_set_size(bar, 192, 6);
    lv_obj_set_style_bg_color(bar, lv_color_hex(JP_COLOR_LINE), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(JP_COLOR_SHU), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 3, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, JP_CARD_TOTAL);
    lv_bar_set_value(bar, (int32_t)mastered, LV_ANIM_OFF);

    // 每个学习入口右侧显示本卡组已掌握数量。
    static char details[JP_DECK_COUNT][12];
    for (int deck = 0; deck < JP_DECK_COUNT; deck++) {
        snprintf(details[deck], sizeof(details[deck]), "%u/%u",
                 (unsigned)deck_mastered[deck], (unsigned)jp_deck_size((jp_deck_t)deck));
    }
    static char quiz_detail[24];
    snprintf(quiz_detail, sizeof(quiz_detail), JP_TXT_QUIZ_PER_ROUND, (unsigned)JP_QUIZ_LENGTH);

    const char *chips[ROW_COUNT] = {
        JP_TXT_CHIP_HIRAGANA, JP_TXT_CHIP_KATAKANA, JP_TXT_CHIP_WORDS, JP_TXT_CHIP_QUIZ, LV_SYMBOL_SETTINGS,
    };
    const char *titles[ROW_COUNT] = {
        JP_TXT_MENU_HIRAGANA, JP_TXT_MENU_KATAKANA, JP_TXT_MENU_WORDS, JP_TXT_MENU_QUIZ, JP_TXT_MENU_SETTINGS,
    };
    const char *detail_text[ROW_COUNT] = {
        details[JP_DECK_HIRAGANA], details[JP_DECK_KATAKANA], details[JP_DECK_WORDS], quiz_detail, NULL,
    };
    for (int i = 0; i < ROW_COUNT; i++) {
        jp_ui_row_create(&s_rows[i], screen, ROW_TOP + i * (ROW_HEIGHT + ROW_GAP), ROW_HEIGHT,
                         chips[i], titles[i], detail_text[i]);
    }
    s_selected = arg >= 0 && arg < ROW_COUNT ? arg : 0;
    refresh_selection();
    jp_ui_footer(screen, JP_TXT_HINT_HOME);

    jp_voice_stop();
    if (!app->voice_clips && !s_voice_warned) {
        s_voice_warned = true;
        jp_ui_toast(JP_TXT_VOICE_OFF);
    }
}

static void home_destroy(void)
{
    for (int i = 0; i < ROW_COUNT; i++) s_rows[i] = (jp_row_t){ 0 };
}

static void home_key(jp_key_t key)
{
    switch (key) {
    case JP_KEY_UP:
        s_selected = (s_selected + ROW_COUNT - 1) % ROW_COUNT;
        refresh_selection();
        break;
    case JP_KEY_DOWN:
        s_selected = (s_selected + 1) % ROW_COUNT;
        refresh_selection();
        break;
    case JP_KEY_OK:
        enter_row(s_selected);
        break;
    default:
        break;
    }
}

const jp_screen_ops_t JP_SCREEN_HOME = {
    .build = home_build,
    .destroy = home_destroy,
    .key = home_key,
    .tick = NULL,
};
