// main/jp_app.h —— 口袋日语应用调度器：状态、页面路由、按键分发与省电。
//
// 任务模型：
//   按键回调(esp_timer 任务)  只把原始事件放进队列，立刻返回；
//   应用任务 jp_app           取事件 → 空闲/熄屏判断 → 映射为 jp_key_t → 持 LVGL 锁调用
//                             当前页面的 key()；每 200 ms 做一次周期工作（调光、电量、页面 tick）；
//                             NVS 保存在释放 LVGL 锁之后进行；
//   语音任务 jp_voice         见 jp_voice.h；
//   LVGL 任务                 esp_lvgl_port 渲染。
// 页面回调（build/destroy/key/tick）都在应用任务中、持有 LVGL 锁时执行，
// 因此页面内部可以直接操作 LVGL 对象，但不得阻塞或直接写 NVS。
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "jp_data.h"
#include "jp_quiz.h"
#include "jp_save.h"
#include "jp_srs.h"
#include "lvgl.h"

// 页面收到的逻辑按键（已从物理事件映射）：
//   UP/DOWN  按下瞬间触发（响应最快，适合翻卡与移动光标）；
//   OK       单击（快速双击也按一次单击处理）；
//   BACK     OK 长按 500 ms。
typedef enum {
    JP_KEY_UP = 0,
    JP_KEY_DOWN,
    JP_KEY_OK,
    JP_KEY_BACK,
} jp_key_t;

typedef enum {
    JP_SCR_HOME = 0,     // 首页菜单；arg = 默认选中行
    JP_SCR_STUDY,        // 学习卡片；arg = jp_deck_t
    JP_SCR_QUIZ_MENU,    // 测验选择；arg = 默认选中行
    JP_SCR_QUIZ,         // 答题；arg = jp_deck_t（开始新一轮）
    JP_SCR_RESULT,       // 测验结果；arg 未使用（读取 jp_app()->quiz）
    JP_SCR_SETTINGS,     // 设置；arg 未使用
    JP_SCR_COUNT,
} jp_screen_id_t;

// 页面接口。build 在新 screen 对象上创建控件；destroy 只清空页面静态指针与定时状态
// （screen 对象由路由器统一删除）；key/tick 可为 NULL。
typedef struct {
    void (*build)(lv_obj_t *screen, int arg);
    void (*destroy)(void);
    void (*key)(jp_key_t key);
    void (*tick)(void);
} jp_screen_ops_t;

extern const jp_screen_ops_t JP_SCREEN_HOME;
extern const jp_screen_ops_t JP_SCREEN_STUDY;
extern const jp_screen_ops_t JP_SCREEN_QUIZ_MENU;
extern const jp_screen_ops_t JP_SCREEN_QUIZ;
extern const jp_screen_ops_t JP_SCREEN_RESULT;
extern const jp_screen_ops_t JP_SCREEN_SETTINGS;

// 应用全局状态。只在应用任务中读写（页面回调也运行在应用任务），无需额外加锁。
typedef struct {
    jp_settings_t settings;            // 用户设置（含各卡组上次位置）
    uint8_t boxes[JP_CARD_TOTAL];      // 每张卡的 SRS 盒子等级
    jp_rng_t rng;                      // 出题随机数（硬件随机数播种）
    jp_quiz_t quiz;                    // 当前/最近一轮测验
    bool voice_clips;                  // 发音片段可用
    bool store_ok;                     // NVS 可用（否则进度只保存在内存中）
} jp_app_state_t;

// 启动应用：加载存档、初始化语音、创建输入队列与应用任务、注册按键。
// 调用前必须已完成 bsp_display_init()/bsp_lvgl_init()；audio_ok/battery_ok 为对应
// BSP 初始化结果，失败时应用降级（静音 / 电量显示 "--"）。
esp_err_t jp_app_start(bool audio_ok, bool battery_ok);

// 应用状态（应用任务内使用）。
jp_app_state_t *jp_app(void);

// 请求切换页面。在当前 key()/tick() 返回后由路由器执行，页面回调里可安全调用。
void jp_app_goto(jp_screen_id_t id, int arg);

// 标记需要保存；实际写 NVS 发生在释放 LVGL 锁之后。
void jp_app_mark_dirty(bool settings, bool progress);

// 立即按当前设置刷新背光亮度（设置页调整亮度后调用）。
void jp_app_apply_brightness(void);
