// main/jp_power.h —— 空闲调暗/熄屏状态机（纯逻辑，可主机测试）。
//
// 本设备靠小电池供电，屏幕背光是主要耗电项：
//   无操作 dim_after_ms   → 背光降到很暗（仍可看清，提示「快要熄屏」）；
//   无操作 off_after_ms   → 背光完全关闭；
//   任意按键              → 恢复正常亮度；熄屏状态下这一次按键只负责唤醒、不执行动作，
//                           避免用户在黑屏中误触「下一题」等操作。
// 正在播放发音时由调用方调用 jp_idle_touch() 刷新计时，避免听写中途熄屏。
// 时间一律用 uint32_t 毫秒并以减法比较，49 天回绕也能正确计算间隔。
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    JP_POWER_ACTIVE = 0,   // 正常亮度
    JP_POWER_DIM,          // 调暗
    JP_POWER_OFF,          // 背光关闭
} jp_power_t;

typedef struct {
    uint32_t last_activity_ms;   // 最近一次按键/活动的时间戳
    uint32_t dim_after_ms;       // 空闲多久后调暗
    uint32_t off_after_ms;       // 空闲多久后熄屏（应大于 dim_after_ms）
    jp_power_t state;            // 当前状态
} jp_idle_t;

// 初始化为 ACTIVE，并以 now_ms 作为最近活动时间。
void jp_idle_init(jp_idle_t *idle, uint32_t now_ms, uint32_t dim_after_ms, uint32_t off_after_ms);

// 处理一次按键：刷新计时并回到 ACTIVE。返回 true 表示按键发生在熄屏状态，应被吞掉。
bool jp_idle_input(jp_idle_t *idle, uint32_t now_ms);

// 非按键活动（播放发音等）：只刷新计时，不改变当前状态（下一次 update 会恢复 ACTIVE）。
void jp_idle_touch(jp_idle_t *idle, uint32_t now_ms);

// 周期调用：根据空闲时长推进状态并返回新状态。
jp_power_t jp_idle_update(jp_idle_t *idle, uint32_t now_ms);
