// main/jp_store.h —— 设置与学习进度的 NVS 持久化。
//
// 命名空间 "jp_learn"，两个 blob：
//   "settings"  JP_SETTINGS_BLOB_SIZE 字节（音量、自动发音、亮度、各卡组位置）
//   "progress"  JP_PROGRESS_BLOB_SIZE 字节（每张卡的 SRS 盒子等级）
// 编解码与校验规则见 jp_save.h。
//
// 线程：只在应用任务中调用，且不要在持有 LVGL 锁时调用 —— NVS 写入会擦写 Flash，
// 可能阻塞数十毫秒并暂停 cache。
// 写入前会与上一次成功写入的内容比较，未变化时直接返回 ESP_OK，减少 Flash 磨损。
#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "jp_save.h"

// 初始化 NVS 分区。仅当分区已满或版本不兼容（ESP_ERR_NVS_NO_FREE_PAGES /
// ESP_ERR_NVS_NEW_VERSION_FOUND）时擦除重建 —— 这是 ESP-IDF 推荐的恢复方式，
// 本固件的 NVS 只保存本应用自己的数据。其他错误原样返回，调用方降级为「不保存」。
esp_err_t jp_store_init(void);

// 读取设置与进度；不存在或校验失败时填默认值（设置默认、进度全 0），仍返回 ESP_OK。
// 仅 NVS 本身不可用时返回错误（输出参数同样被填为默认值）。
esp_err_t jp_store_load(jp_settings_t *settings, uint8_t *boxes);

// 保存设置 / 进度。内容未变化时不写 Flash。
esp_err_t jp_store_save_settings(const jp_settings_t *settings);
esp_err_t jp_store_save_progress(const uint8_t *boxes);
