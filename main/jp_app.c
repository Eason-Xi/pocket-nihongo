// main/jp_app.c —— 应用调度器实现。
#include "jp_app.h"

#include <string.h>

#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jp_power.h"
#include "jp_store.h"
#include "jp_text.h"
#include "jp_ui.h"
#include "jp_voice.h"

static const char *TAG = "jp_app";

#define INPUT_QUEUE_DEPTH   8
#define APP_TASK_STACK      6144     // 页面构建会嵌套调用较多 LVGL 函数，留足余量
#define APP_TASK_PRIO       5
#define TICK_MS             200      // 周期工作间隔
#define LOCK_TIMEOUT_MS     500
#define BATTERY_PERIOD_MS   30000    // 电量变化很慢，30 s 读一次足够
#define DIM_AFTER_MS        30000    // 30 s 无操作调暗
#define OFF_AFTER_MS        90000    // 90 s 无操作熄屏
#define DIM_PERCENT         8        // 调暗后的背光百分比

typedef struct {
    bsp_btn_t btn;
    bsp_btn_ev_t event;
} input_event_t;

static const jp_screen_ops_t *const SCREENS[JP_SCR_COUNT] = {
    [JP_SCR_HOME] = &JP_SCREEN_HOME,
    [JP_SCR_STUDY] = &JP_SCREEN_STUDY,
    [JP_SCR_QUIZ_MENU] = &JP_SCREEN_QUIZ_MENU,
    [JP_SCR_QUIZ] = &JP_SCREEN_QUIZ,
    [JP_SCR_RESULT] = &JP_SCREEN_RESULT,
    [JP_SCR_SETTINGS] = &JP_SCREEN_SETTINGS,
};

static jp_app_state_t s_state;            // 应用状态（应用任务独占）
static QueueHandle_t s_queue;             // 按键回调 → 应用任务
static TaskHandle_t s_task;
static volatile bool s_input_ready;       // 首页建好后才接收按键，避免启动期事件堆积

static const jp_screen_ops_t *s_ops;      // 当前页面
static lv_obj_t *s_screen;                // 当前页面根对象
static int s_pending_id = -1;             // 待切换页面；-1 表示无
static int s_pending_arg;
static bool s_dirty_settings;             // 待保存标记（应用任务内读写）
static bool s_dirty_progress;

static jp_idle_t s_idle;                  // 空闲计时
static jp_power_t s_power = JP_POWER_ACTIVE;
static int s_swallow_btn = -1;            // 熄屏唤醒时，整次按压（PRESS 及其后的 CLICK/LONG）被吞掉
static bool s_battery_ok;
static uint32_t s_last_battery_ms;
static bool s_battery_read_once;

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

jp_app_state_t *jp_app(void)
{
    return &s_state;
}

void jp_app_goto(jp_screen_id_t id, int arg)
{
    if (id >= JP_SCR_COUNT) return;
    s_pending_id = id;
    s_pending_arg = arg;
}

void jp_app_mark_dirty(bool settings, bool progress)
{
    s_dirty_settings |= settings;
    s_dirty_progress |= progress;
}

void jp_app_apply_brightness(void)
{
    uint8_t percent = jp_brightness_percent(s_state.settings.brightness);
    if (s_power == JP_POWER_DIM) percent = DIM_PERCENT < percent ? DIM_PERCENT : percent;
    if (s_power == JP_POWER_OFF) percent = 0;
    bsp_display_backlight(percent);
}

// 执行待定的页面切换（持 LVGL 锁调用）。
//
// 顺序：加载常驻空白页 → 删除旧页 → 构建新页 → 加载新页。
// 为什么不「先建新页再删旧页」：单个页面约占 LVGL 池 13–20 KB，两页共存时峰值
// 接近 40 KB，再叠加碎片，绘制 72 px 字形所需的约 4 KB 连续 A8 缓冲就会分配失败，
// LV_ASSERT_MALLOC 随即死循环（主机渲染压力测试中复现）。空白页只是一个无子对象的
// 根对象；整个过程持有 LVGL 锁，渲染任务看不到空白页那一帧。
static void run_pending_navigation(void)
{
    static lv_obj_t *s_blank;
    while (s_pending_id >= 0) {
        int id = s_pending_id;
        int arg = s_pending_arg;
        s_pending_id = -1;

        if (s_ops && s_ops->destroy) s_ops->destroy();
        jp_ui_screen_detached();
        if (s_screen) {
            if (!s_blank) s_blank = jp_ui_screen_create();
            lv_screen_load(s_blank);
            lv_obj_delete(s_screen);
            s_screen = NULL;
        }

        s_ops = SCREENS[id];
        s_screen = jp_ui_screen_create();
        s_ops->build(s_screen, arg);
        lv_screen_load(s_screen);
        // build() 里再次调用 jp_app_goto 时（例如出题失败退回菜单）循环继续处理。
    }
}

// 物理事件 → 逻辑按键。返回 false 表示该事件不产生动作。
static bool map_key(const input_event_t *input, jp_key_t *key)
{
    switch (input->btn) {
    case BSP_BTN_UP:
        if (input->event != BSP_BTN_PRESS) return false;
        *key = JP_KEY_UP;
        return true;
    case BSP_BTN_DOWN:
        if (input->event != BSP_BTN_PRESS) return false;
        *key = JP_KEY_DOWN;
        return true;
    case BSP_BTN_OK:
        if (input->event == BSP_BTN_CLICK || input->event == BSP_BTN_DOUBLE) {
            *key = JP_KEY_OK;
            return true;
        }
        if (input->event == BSP_BTN_LONG) {
            *key = JP_KEY_BACK;
            return true;
        }
        return false;
    default:
        return false;
    }
}

static void handle_input(const input_event_t *input)
{
    uint32_t now = now_ms();
    if (input->event == BSP_BTN_PRESS) {
        bool swallow = jp_idle_input(&s_idle, now);
        s_swallow_btn = swallow ? (int)input->btn : -1;
        if (s_power != JP_POWER_ACTIVE) {
            s_power = JP_POWER_ACTIVE;
            jp_app_apply_brightness();
        }
    } else {
        jp_idle_touch(&s_idle, now);
    }
    if (s_swallow_btn == (int)input->btn) return;

    jp_key_t key;
    if (!map_key(input, &key)) return;
    if (!bsp_lvgl_lock(LOCK_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "LVGL 锁超时，丢弃按键");
        return;
    }
    if (s_ops && s_ops->key) s_ops->key(key);
    run_pending_navigation();
    bsp_lvgl_unlock();
}

// 保存脏数据（不持 LVGL 锁）。失败时保留脏标记，下一个周期重试，并提示用户。
static void flush_saves(void)
{
    if (!s_dirty_settings && !s_dirty_progress) return;
    bool failed = false;
    if (s_dirty_settings) {
        if (jp_store_save_settings(&s_state.settings) == ESP_OK) s_dirty_settings = false;
        else failed = true;
    }
    if (s_dirty_progress) {
        if (jp_store_save_progress(s_state.boxes) == ESP_OK) s_dirty_progress = false;
        else failed = true;
    }
    if (failed) {
        // NVS 不可用时不再反复重试，避免每 200 ms 刷一次失败日志；本次运行内进度仍在内存中。
        if (!s_state.store_ok) s_dirty_settings = s_dirty_progress = false;
        if (bsp_lvgl_lock(LOCK_TIMEOUT_MS)) {
            jp_ui_toast(JP_TXT_SAVE_FAILED);
            bsp_lvgl_unlock();
        }
    }
}

static void periodic(void)
{
    uint32_t now = now_ms();
    // 播放发音时视为活动：听写/跟读过程中不调暗。
    if (jp_voice_busy()) jp_idle_touch(&s_idle, now);
    jp_power_t power = jp_idle_update(&s_idle, now);
    if (power != s_power) {
        s_power = power;
        jp_app_apply_brightness();
        ESP_LOGI(TAG, "背光状态 -> %s", power == JP_POWER_ACTIVE ? "正常" : power == JP_POWER_DIM ? "调暗" : "关闭");
    }

    if (!s_battery_read_once || now - s_last_battery_ms >= BATTERY_PERIOD_MS) {
        s_battery_read_once = true;
        s_last_battery_ms = now;
        int soc = s_battery_ok ? bsp_battery_soc() : -1;   // I2C 读取在锁外完成
        if (bsp_lvgl_lock(LOCK_TIMEOUT_MS)) {
            jp_ui_battery_update(soc);
            bsp_lvgl_unlock();
        }
    }

    // 熄屏时不驱动页面动画，省去无意义的重绘。
    if (s_power != JP_POWER_OFF && s_ops && s_ops->tick) {
        if (bsp_lvgl_lock(LOCK_TIMEOUT_MS)) {
            s_ops->tick();
            run_pending_navigation();
            bsp_lvgl_unlock();
        }
    }
}

static void log_memory(const char *when)
{
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "[%s] 内部堆空闲 %u B，最大连续块 %u B；LVGL 池已用 %u%%，最大空闲块 %u B",
             when,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)mon.used_pct, (unsigned)mon.free_biggest_size);
}

static void app_task(void *arg)
{
    (void)arg;
    if (bsp_lvgl_lock(2000)) {
        jp_app_goto(JP_SCR_HOME, 0);
        run_pending_navigation();
        bsp_lvgl_unlock();
    } else {
        ESP_LOGE(TAG, "启动时无法获取 LVGL 锁");
    }
    // 首帧内容就绪后再点亮背光，避免显示上电后的随机花屏。
    vTaskDelay(pdMS_TO_TICKS(60));
    jp_app_apply_brightness();
    log_memory("首页");
    s_input_ready = true;

    input_event_t input;
    for (;;) {
        if (xQueueReceive(s_queue, &input, pdMS_TO_TICKS(TICK_MS)) == pdTRUE) {
            handle_input(&input);
            // 连续快速按键时先把队列里的事件处理完，再做周期工作。
            while (xQueueReceive(s_queue, &input, 0) == pdTRUE) handle_input(&input);
        }
        periodic();
        flush_saves();
    }
}

// 按键回调运行在 button 组件共享的 esp_timer 任务：只入队、不阻塞、不碰 LVGL。
static void on_button(bsp_btn_t btn, bsp_btn_ev_t event, void *user)
{
    (void)user;
    if (!s_input_ready || !s_queue) return;
    const input_event_t input = { .btn = btn, .event = event };
    (void)xQueueSend(s_queue, &input, 0);   // 队列满说明应用卡顿，丢弃比阻塞定时器任务更安全
}

esp_err_t jp_app_start(bool audio_ok, bool battery_ok)
{
    s_battery_ok = battery_ok;

    // 1. 存档：NVS 失败时仍可学习，只是不保存。
    s_state.store_ok = jp_store_init() == ESP_OK;
    esp_err_t err = jp_store_load(&s_state.settings, s_state.boxes);
    if (err != ESP_OK) ESP_LOGW(TAG, "读取存档失败: %s，使用默认值", esp_err_to_name(err));
    jp_rng_seed(&s_state.rng, esp_random());

    // 2. 语音：音频初始化失败则完全静音。
    if (audio_ok) {
        err = jp_voice_init(s_state.settings.volume);
        s_state.voice_clips = err == ESP_OK;
    } else {
        ESP_LOGW(TAG, "音频初始化失败，应用以静音模式运行");
    }

    // 3. LVGL 主题/字库。
    if (!bsp_lvgl_lock(1000)) return ESP_ERR_TIMEOUT;
    jp_ui_init();
    bsp_lvgl_unlock();

    // 4. 输入与应用任务。
    jp_idle_init(&s_idle, now_ms(), DIM_AFTER_MS, OFF_AFTER_MS);
    s_queue = xQueueCreate(INPUT_QUEUE_DEPTH, sizeof(input_event_t));
    if (!s_queue) return ESP_ERR_NO_MEM;
    if (xTaskCreate(app_task, "jp_app", APP_TASK_STACK, NULL, APP_TASK_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    err = bsp_button_init(on_button, NULL);
    if (err != ESP_OK) {
        // 没有按键就无法操作，但仍保留首页显示以便用户看到设备已启动。
        ESP_LOGE(TAG, "按键初始化失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "口袋日语已启动：语音=%s 存档=%s 电量计=%s",
             s_state.voice_clips ? "可用" : "不可用", s_state.store_ok ? "可用" : "不可用",
             battery_ok ? "可用" : "不可用");
    return ESP_OK;
}
