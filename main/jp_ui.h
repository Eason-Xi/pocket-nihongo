// main/jp_ui.h —— 口袋日语的视觉主题与通用控件。
//
// 视觉语言「和纸与朱印」：米白和纸底色、墨色正文、朱红色用于选中项与印章、
// 抹茶绿表示答对。全部页面 240×320 竖屏，四角 30 px 圆角由 BSP 显示层统一遮罩，
// 因此页面内容距左右边缘至少 16 px、页眉文字避开左上/右上圆角区域。
//
// 线程：除特别说明外，所有函数必须在 LVGL 任务内或持有 bsp_lvgl_lock() 时调用。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"

// ---- 配色（RGB888，lv_color_hex 使用）----
#define JP_COLOR_PAPER        0xF4EFE6   // 和纸底色
#define JP_COLOR_CARD         0xFFFDF8   // 卡片/菜单行底色
#define JP_COLOR_INK          0x2B2A28   // 墨色正文
#define JP_COLOR_INK_SOFT     0x857E73   // 次要文字、提示
#define JP_COLOR_LINE         0xE3DACB   // 描边、分隔线、未点亮的进度点
#define JP_COLOR_SHU          0xD8483B   // 朱红：选中、强调、印章
#define JP_COLOR_SHU_SOFT     0xF7DDD7   // 淡朱：图标底、答错底色
#define JP_COLOR_AI           0x2F4F74   // 藍色：罗马音、读音
#define JP_COLOR_MATCHA       0x4F8F3E   // 抹茶绿：答对
#define JP_COLOR_MATCHA_SOFT  0xDDEFD6
#define JP_COLOR_WHITE        0xFFFFFF

// 字库编号。14/20 为中文字形，32/72 为日文字形；14/20 已挂 Montserrat 回退以显示 LV_SYMBOL 图标。
typedef enum {
    JP_FONT_14 = 0,
    JP_FONT_20,
    JP_FONT_32,
    JP_FONT_72,
    JP_FONT_COUNT,
} jp_font_id_t;

// 一次性初始化字库回退链。必须在 bsp_lvgl_init() 成功之后、创建任何控件之前调用。
void jp_ui_init(void);

// 取字库（带回退链的可写副本）。
const lv_font_t *jp_ui_font(jp_font_id_t id);

// 创建一个页面根对象（尚未加载）：清除主题样式，铺和纸底色。
lv_obj_t *jp_ui_screen_create(void);

// 页眉：左侧朱红圆点 + 标题（20 px），右上角电量。返回标题标签，便于页面改写标题。
// 电量标签登记为「当前页电量」，由 jp_ui_battery_update() 刷新。
lv_obj_t *jp_ui_header(lv_obj_t *screen, const char *title);

// 页脚操作提示（14 px，居中，次要文字色）。返回提示标签。
lv_obj_t *jp_ui_footer(lv_obj_t *screen, const char *hint);

// 页面删除前调用：忘记当前页电量标签，防止悬空指针。
void jp_ui_screen_detached(void);

// 刷新当前页电量显示。soc 为 0..100，-1 表示读数不可用（显示 "--"）。
void jp_ui_battery_update(int soc);

// 纯矩形容器：清除主题样式、不可滚动/点击。radius 为圆角，bg 为底色。
lv_obj_t *jp_ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h,
                    uint32_t bg, int32_t radius);

// 标签：指定字库与颜色。text 会被复制（lv_label_set_text）。
lv_obj_t *jp_ui_label(lv_obj_t *parent, jp_font_id_t font, uint32_t color, const char *text);

// 按字数挑字号：text 的码点数不超过 max_big 时用 big，否则用 small。
// CJK 字符在各字号下都约为一个字号宽，用码点数估算宽度足够准确。
jp_font_id_t jp_ui_font_for(const char *text, size_t max_big, jp_font_id_t big, jp_font_id_t small);

// 顶层短提示（例如「保存失败」），约 2 秒后自动隐藏；跨页面保留。
void jp_ui_toast(const char *text);

// ---- 菜单行 ----
// 一行 = 左侧方形「印章」图标 + 标题 + 右侧补充信息。选中时整行变朱红底白字。
typedef struct {
    lv_obj_t *row;
    lv_obj_t *chip;
    lv_obj_t *chip_label;
    lv_obj_t *title;
    lv_obj_t *detail;
} jp_row_t;

// 在 parent 的 (16, y) 处创建宽 208、高 h 的菜单行。chip 为图标文字（可为 LV_SYMBOL_*），
// detail 可为 NULL（不创建右侧文字）。
void jp_ui_row_create(jp_row_t *row, lv_obj_t *parent, int32_t y, int32_t h,
                      const char *chip, const char *title, const char *detail);

// 切换选中外观。
void jp_ui_row_select(jp_row_t *row, bool selected);

// ---- 掌握度圆点 ----
#define JP_UI_DOTS 5   // 与 JP_SRS_MAX_BOX 一致：每答对一次点亮一个

// 在 parent 中以 (x, y) 为左上角横向创建 5 个 8 px 圆点。
void jp_ui_dots_create(lv_obj_t *dots[JP_UI_DOTS], lv_obj_t *parent, int32_t x, int32_t y);

// 点亮前 level 个圆点。
void jp_ui_dots_set(lv_obj_t *dots[JP_UI_DOTS], uint8_t level);
