// main/jp_ui.c —— 视觉主题与通用控件实现。
#include "jp_ui.h"

#include <stddef.h>

#include "jp_data.h"
#include "jp_text.h"

LV_FONT_DECLARE(jp_font_14);
LV_FONT_DECLARE(jp_font_20);
LV_FONT_DECLARE(jp_font_32);
LV_FONT_DECLARE(jp_font_72);

// 生成的字库是 const；回退链需要可写描述符，因此保留与程序同寿命的浅拷贝。
// 回退方向：应用字库优先，缺字（仅 LV_SYMBOL_* 图标）时才查 Montserrat。
static lv_font_t s_fonts[JP_FONT_COUNT];
static bool s_fonts_ready;

static lv_obj_t *s_battery;       // 当前页的电量标签；页面删除前由 jp_ui_screen_detached 清空
static int s_battery_soc = -2;    // 最近一次电量读数；-2 表示尚未读到，-1 表示不可用
static lv_obj_t *s_toast;         // 顶层提示标签（lv_layer_top，跨页面）
static lv_timer_t *s_toast_timer; // 自动隐藏定时器（LVGL 上下文运行）

void jp_ui_init(void)
{
    if (s_fonts_ready) return;
    s_fonts[JP_FONT_14] = jp_font_14;
    s_fonts[JP_FONT_14].fallback = &lv_font_montserrat_14;
    s_fonts[JP_FONT_20] = jp_font_20;
    s_fonts[JP_FONT_20].fallback = &lv_font_montserrat_20;
    // 32/72 只显示文字，不需要图标；仍挂上小字号副本，万一缺字也能看到占位而非崩溃。
    s_fonts[JP_FONT_32] = jp_font_32;
    s_fonts[JP_FONT_32].fallback = &s_fonts[JP_FONT_20];
    s_fonts[JP_FONT_72] = jp_font_72;
    s_fonts[JP_FONT_72].fallback = &s_fonts[JP_FONT_32];
    s_fonts_ready = true;
}

const lv_font_t *jp_ui_font(jp_font_id_t id)
{
    return &s_fonts[id < JP_FONT_COUNT ? id : JP_FONT_20];
}

// 统一去掉主题样式并禁用滚动/点击：本应用没有触摸屏，所有交互来自三个按键。
static void make_plain(lv_obj_t *obj)
{
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
}

lv_obj_t *jp_ui_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    make_plain(screen);
    lv_obj_set_style_bg_color(screen, lv_color_hex(JP_COLOR_PAPER), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    return screen;
}

lv_obj_t *jp_ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t bg, int32_t radius)
{
    lv_obj_t *box = lv_obj_create(parent);
    make_plain(box);
    lv_obj_set_pos(box, x, y);
    lv_obj_set_size(box, w, h);
    lv_obj_set_style_bg_color(box, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, radius, 0);
    return box;
}

lv_obj_t *jp_ui_label(lv_obj_t *parent, jp_font_id_t font, uint32_t color, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, jp_ui_font(font), 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    lv_label_set_text(label, text ? text : "");
    return label;
}

jp_font_id_t jp_ui_font_for(const char *text, size_t max_big, jp_font_id_t big, jp_font_id_t small)
{
    return jp_utf8_count(text) <= max_big ? big : small;
}

// 电量图标按 LVGL 内置 5 档选择；Montserrat 14 已包含这些 FontAwesome 符号。
static const char *battery_symbol(int soc)
{
    if (soc < 0) return LV_SYMBOL_BATTERY_EMPTY;
    if (soc >= 85) return LV_SYMBOL_BATTERY_FULL;
    if (soc >= 60) return LV_SYMBOL_BATTERY_3;
    if (soc >= 35) return LV_SYMBOL_BATTERY_2;
    if (soc >= 12) return LV_SYMBOL_BATTERY_1;
    return LV_SYMBOL_BATTERY_EMPTY;
}

static void battery_render(void)
{
    if (!s_battery) return;
    if (s_battery_soc < 0) {
        lv_label_set_text_fmt(s_battery, "%s %s", JP_TXT_BATTERY_UNKNOWN, battery_symbol(-1));
    } else {
        lv_label_set_text_fmt(s_battery, "%d%% %s", s_battery_soc, battery_symbol(s_battery_soc));
    }
    // 低电量时图标与数字变朱红，提醒充电。
    lv_obj_set_style_text_color(s_battery, lv_color_hex(
        s_battery_soc >= 0 && s_battery_soc < 12 ? JP_COLOR_SHU : JP_COLOR_INK_SOFT), 0);
}

lv_obj_t *jp_ui_header(lv_obj_t *screen, const char *title)
{
    // 朱红圆点：像一枚小印章，也是整个应用的识别符号（日之丸的意象）。
    lv_obj_t *dot = jp_ui_box(screen, 26, 20, 10, 10, JP_COLOR_SHU, LV_RADIUS_CIRCLE);
    (void)dot;
    lv_obj_t *label = jp_ui_label(screen, JP_FONT_20, JP_COLOR_INK, title);
    lv_obj_set_pos(label, 42, 12);

    // 电量放在右上角：y=14 处圆角遮罩只挡住 x > 232 的像素，右对齐到 x=218 足够安全。
    s_battery = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, "");
    lv_obj_align(s_battery, LV_ALIGN_TOP_RIGHT, -22, 16);
    battery_render();
    return label;
}

lv_obj_t *jp_ui_footer(lv_obj_t *screen, const char *hint)
{
    lv_obj_t *label = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, hint);
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -14);
    return label;
}

void jp_ui_screen_detached(void)
{
    s_battery = NULL;
}

void jp_ui_battery_update(int soc)
{
    if (soc > 100) soc = 100;
    if (soc < -1) soc = -1;
    if (soc == s_battery_soc) return;
    s_battery_soc = soc;
    battery_render();
}

// LVGL 定时器回调（LVGL 任务上下文）：隐藏提示并删除一次性定时器。
static void toast_hide_cb(lv_timer_t *timer)
{
    if (s_toast) lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_delete(timer);
    s_toast_timer = NULL;
}

void jp_ui_toast(const char *text)
{
    if (!s_toast) {
        s_toast = jp_ui_label(lv_layer_top(), JP_FONT_14, JP_COLOR_WHITE, "");
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(JP_COLOR_INK), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_90, 0);
        lv_obj_set_style_radius(s_toast, 10, 0);
        lv_obj_set_style_pad_hor(s_toast, 12, 0);
        lv_obj_set_style_pad_ver(s_toast, 6, 0);
    }
    lv_label_set_text(s_toast, text);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -44);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if (s_toast_timer) {
        lv_timer_reset(s_toast_timer);
    } else {
        s_toast_timer = lv_timer_create(toast_hide_cb, 2000, NULL);
    }
}

void jp_ui_row_create(jp_row_t *row, lv_obj_t *parent, int32_t y, int32_t h,
                      const char *chip, const char *title, const char *detail)
{
    row->row = jp_ui_box(parent, 16, y, 208, h, JP_COLOR_CARD, 12);
    lv_obj_set_style_border_width(row->row, 1, 0);
    lv_obj_set_style_border_color(row->row, lv_color_hex(JP_COLOR_LINE), 0);

    int32_t chip_size = h - 10;
    row->chip = jp_ui_box(row->row, 7, (h - chip_size) / 2 - 1, chip_size, chip_size, JP_COLOR_SHU_SOFT, 8);
    row->chip_label = jp_ui_label(row->chip, JP_FONT_20, JP_COLOR_SHU, chip);
    lv_obj_center(row->chip_label);

    row->title = jp_ui_label(row->row, JP_FONT_20, JP_COLOR_INK, title);
    lv_obj_align(row->title, LV_ALIGN_LEFT_MID, chip_size + 16, -1);

    row->detail = NULL;
    if (detail) {
        row->detail = jp_ui_label(row->row, JP_FONT_14, JP_COLOR_INK_SOFT, detail);
        lv_obj_align(row->detail, LV_ALIGN_RIGHT_MID, -10, 0);
    }
}

void jp_ui_row_select(jp_row_t *row, bool selected)
{
    lv_obj_set_style_bg_color(row->row, lv_color_hex(selected ? JP_COLOR_SHU : JP_COLOR_CARD), 0);
    lv_obj_set_style_border_color(row->row, lv_color_hex(selected ? JP_COLOR_SHU : JP_COLOR_LINE), 0);
    lv_obj_set_style_bg_color(row->chip, lv_color_hex(selected ? JP_COLOR_WHITE : JP_COLOR_SHU_SOFT), 0);
    lv_obj_set_style_text_color(row->title, lv_color_hex(selected ? JP_COLOR_WHITE : JP_COLOR_INK), 0);
    if (row->detail) {
        lv_obj_set_style_text_color(row->detail, lv_color_hex(selected ? JP_COLOR_SHU_SOFT : JP_COLOR_INK_SOFT), 0);
    }
}

void jp_ui_dots_create(lv_obj_t *dots[JP_UI_DOTS], lv_obj_t *parent, int32_t x, int32_t y)
{
    for (int i = 0; i < JP_UI_DOTS; i++) {
        dots[i] = jp_ui_box(parent, x + i * 14, y, 8, 8, JP_COLOR_LINE, LV_RADIUS_CIRCLE);
    }
}

void jp_ui_dots_set(lv_obj_t *dots[JP_UI_DOTS], uint8_t level)
{
    for (int i = 0; i < JP_UI_DOTS; i++) {
        if (!dots[i]) continue;
        lv_obj_set_style_bg_color(dots[i], lv_color_hex(i < level ? JP_COLOR_SHU : JP_COLOR_LINE), 0);
    }
}
