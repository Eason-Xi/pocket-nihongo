// main/jp_scr_settings.c —— 设置：音量、自动发音、屏幕亮度、重置进度。
//
// 布局：页眉 → 四个设置行（y = 56 起，每行 40 px、间隔 6 px）→ 关于信息 → 操作提示。
// 按键：▲/▼ 选择；OK 修改当前项：
//   音量      每按一次 +10，100 之后回到 0（静音），并用「あ」试听新音量；
//   自动发音  开/关切换；
//   屏幕亮度  高 → 中 → 低 循环，立即生效；
//   重置进度  第一次 OK 进入待确认（行标题变为「再按 OK 确认清空」），再按一次才清空；
//             移动光标会取消待确认。
// 长按 OK 返回首页，设置在离开时保存到 NVS。
#include <stdio.h>
#include <string.h>

#include "jp_app.h"
#include "jp_text.h"
#include "jp_ui.h"
#include "jp_voice.h"

enum { ROW_VOLUME = 0, ROW_AUTOPLAY, ROW_BRIGHTNESS, ROW_RESET, ROW_COUNT };

static jp_row_t s_rows[ROW_COUNT];
static int s_selected;
static bool s_reset_armed;     // 重置待确认
static bool s_reset_done;      // 本次进入设置页已重置过（行尾显示「已清空」）

static const char *brightness_text(uint8_t level)
{
    switch (level) {
    case 1:  return JP_TXT_BRIGHT_MID;
    case 2:  return JP_TXT_BRIGHT_LOW;
    default: return JP_TXT_BRIGHT_HIGH;
    }
}

// 刷新各行右侧的数值文字，并重新右对齐（文字宽度会变）。
static void refresh_values(void)
{
    const jp_settings_t *settings = &jp_app()->settings;
    lv_label_set_text_fmt(s_rows[ROW_VOLUME].detail, "%u%%", (unsigned)settings->volume);
    lv_label_set_text(s_rows[ROW_AUTOPLAY].detail, settings->auto_play ? JP_TXT_ON : JP_TXT_OFF);
    lv_label_set_text(s_rows[ROW_BRIGHTNESS].detail, brightness_text(settings->brightness));
    // 待确认时直接把行标题换成确认提示（行尾空间放不下完整提示）。
    lv_label_set_text(s_rows[ROW_RESET].title, s_reset_armed ? JP_TXT_RESET_CONFIRM : JP_TXT_SET_RESET);
    lv_label_set_text(s_rows[ROW_RESET].detail, !s_reset_armed && s_reset_done ? JP_TXT_RESET_DONE : "");
    for (int i = 0; i < ROW_COUNT; i++) lv_obj_align(s_rows[i].detail, LV_ALIGN_RIGHT_MID, -10, 0);
}

static void refresh_selection(void)
{
    for (int i = 0; i < ROW_COUNT; i++) jp_ui_row_select(&s_rows[i], i == s_selected);
}

static void settings_build(lv_obj_t *screen, int arg)
{
    (void)arg;
    jp_ui_header(screen, JP_TXT_SETTINGS_TITLE);
    const char *chips[ROW_COUNT] = { LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_AUDIO, LV_SYMBOL_EYE_OPEN, LV_SYMBOL_REFRESH };
    const char *titles[ROW_COUNT] = { JP_TXT_SET_VOLUME, JP_TXT_SET_AUTOPLAY, JP_TXT_SET_BRIGHTNESS, JP_TXT_SET_RESET };
    for (int i = 0; i < ROW_COUNT; i++) {
        jp_ui_row_create(&s_rows[i], screen, 56 + i * 46, 40, chips[i], titles[i], "");
    }
    s_selected = 0;
    s_reset_armed = false;
    s_reset_done = false;
    refresh_values();
    refresh_selection();

    lv_obj_t *about = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, JP_TXT_ABOUT);
    lv_obj_align(about, LV_ALIGN_TOP_MID, 0, 244);
    lv_obj_t *version = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, "Pocket Nihongo v1.0");
    lv_obj_align(version, LV_ALIGN_TOP_MID, 0, 264);

    jp_ui_footer(screen, JP_TXT_HINT_SETTINGS);
    jp_voice_stop();
}

static void settings_destroy(void)
{
    for (int i = 0; i < ROW_COUNT; i++) s_rows[i] = (jp_row_t){ 0 };
}

static void change_selected(void)
{
    jp_app_state_t *app = jp_app();
    jp_settings_t *settings = &app->settings;
    switch (s_selected) {
    case ROW_VOLUME:
        settings->volume = settings->volume >= 100 ? 0 : (uint8_t)(settings->volume + 10);
        jp_voice_set_volume(settings->volume);
        jp_voice_play(jp_deck_voice(JP_DECK_HIRAGANA, 0), JP_CUE_NONE);   // 试听「あ」
        break;
    case ROW_AUTOPLAY:
        settings->auto_play = settings->auto_play ? 0 : 1;
        break;
    case ROW_BRIGHTNESS:
        settings->brightness = (uint8_t)((settings->brightness + 1) % JP_BRIGHTNESS_LEVELS);
        jp_app_apply_brightness();
        break;
    case ROW_RESET:
        if (!s_reset_armed) {
            s_reset_armed = true;
        } else {
            memset(app->boxes, 0, sizeof(app->boxes));
            s_reset_armed = false;
            s_reset_done = true;
            jp_app_mark_dirty(false, true);
        }
        break;
    default:
        break;
    }
    refresh_values();
}

static void settings_key(jp_key_t key)
{
    switch (key) {
    case JP_KEY_UP:
    case JP_KEY_DOWN:
        s_selected = key == JP_KEY_UP ? (s_selected + ROW_COUNT - 1) % ROW_COUNT : (s_selected + 1) % ROW_COUNT;
        if (s_reset_armed) {
            s_reset_armed = false;   // 离开「重置」行即取消待确认，防止误删
            refresh_values();
        }
        refresh_selection();
        break;
    case JP_KEY_OK:
        change_selected();
        break;
    case JP_KEY_BACK:
        jp_app_mark_dirty(true, false);
        jp_app_goto(JP_SCR_HOME, 4);   // 首页「设置」行
        break;
    }
}

const jp_screen_ops_t JP_SCREEN_SETTINGS = {
    .build = settings_build,
    .destroy = settings_destroy,
    .key = settings_key,
    .tick = NULL,
};
