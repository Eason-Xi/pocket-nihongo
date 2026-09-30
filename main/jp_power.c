// main/jp_power.c —— 空闲调暗/熄屏状态机实现。
#include "jp_power.h"

void jp_idle_init(jp_idle_t *idle, uint32_t now_ms, uint32_t dim_after_ms, uint32_t off_after_ms)
{
    if (!idle) return;
    idle->last_activity_ms = now_ms;
    idle->dim_after_ms = dim_after_ms;
    // 熄屏时间不得早于调暗时间，否则状态会直接跳过 DIM。
    idle->off_after_ms = off_after_ms > dim_after_ms ? off_after_ms : dim_after_ms;
    idle->state = JP_POWER_ACTIVE;
}

bool jp_idle_input(jp_idle_t *idle, uint32_t now_ms)
{
    if (!idle) return false;
    bool swallow = idle->state == JP_POWER_OFF;
    idle->last_activity_ms = now_ms;
    idle->state = JP_POWER_ACTIVE;
    return swallow;
}

void jp_idle_touch(jp_idle_t *idle, uint32_t now_ms)
{
    if (!idle) return;
    idle->last_activity_ms = now_ms;
}

jp_power_t jp_idle_update(jp_idle_t *idle, uint32_t now_ms)
{
    if (!idle) return JP_POWER_ACTIVE;
    uint32_t idle_ms = now_ms - idle->last_activity_ms;   // 无符号减法天然处理回绕
    if (idle_ms >= idle->off_after_ms) {
        idle->state = JP_POWER_OFF;
    } else if (idle_ms >= idle->dim_after_ms) {
        // 已熄屏时只有按键（jp_idle_input）才能唤醒，播放活动不会把屏幕重新点亮。
        if (idle->state != JP_POWER_OFF) idle->state = JP_POWER_DIM;
    } else if (idle->state != JP_POWER_OFF) {
        idle->state = JP_POWER_ACTIVE;
    }
    return idle->state;
}
