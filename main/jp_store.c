// main/jp_store.c —— 设置与学习进度的 NVS 持久化实现。
#include "jp_store.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "jp_store";

#define NVS_NAMESPACE   "jp_learn"
#define KEY_SETTINGS    "settings"
#define KEY_PROGRESS    "progress"

static bool s_ready;                                   // nvs_flash_init 成功
// 最近一次成功写入（或读取）的 blob，用于跳过无变化的写入。只在应用任务访问。
static uint8_t s_last_settings[JP_SETTINGS_BLOB_SIZE];
static bool s_last_settings_valid;
static uint8_t s_last_progress[JP_PROGRESS_BLOB_SIZE];
static bool s_last_progress_valid;

esp_err_t jp_store_init(void)
{
    if (s_ready) return ESP_OK;
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 需要重建(%s)，擦除后重新初始化", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %s，学习进度将不会保存", esp_err_to_name(err));
        return err;
    }
    s_ready = true;
    return ESP_OK;
}

// 读取一个定长 blob。长度不符视为不存在（返回 ESP_ERR_NVS_NOT_FOUND）。
static esp_err_t read_blob(nvs_handle_t handle, const char *key, uint8_t *out, size_t size)
{
    size_t length = size;
    esp_err_t err = nvs_get_blob(handle, key, out, &length);
    if (err == ESP_ERR_NVS_INVALID_LENGTH || (err == ESP_OK && length != size)) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    return err;
}

esp_err_t jp_store_load(jp_settings_t *settings, uint8_t *boxes)
{
    jp_settings_default(settings);
    memset(boxes, 0, JP_CARD_TOTAL);
    if (!s_ready) return ESP_ERR_INVALID_STATE;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;   // 首次启动，命名空间尚未创建
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "打开 NVS 失败: %s", esp_err_to_name(err));
        return err;
    }

    uint8_t settings_blob[JP_SETTINGS_BLOB_SIZE];
    if (read_blob(handle, KEY_SETTINGS, settings_blob, sizeof(settings_blob)) == ESP_OK &&
        jp_settings_decode(settings, settings_blob, sizeof(settings_blob))) {
        memcpy(s_last_settings, settings_blob, sizeof(s_last_settings));
        s_last_settings_valid = true;
    }

    // 进度 blob 369 字节，放静态区避免占用应用任务栈。
    static uint8_t progress_blob[JP_PROGRESS_BLOB_SIZE];
    if (read_blob(handle, KEY_PROGRESS, progress_blob, sizeof(progress_blob)) == ESP_OK) {
        if (jp_progress_decode(boxes, progress_blob, sizeof(progress_blob))) {
            memcpy(s_last_progress, progress_blob, sizeof(s_last_progress));
            s_last_progress_valid = true;
        } else {
            ESP_LOGW(TAG, "学习进度与当前内容不匹配，已重置");
        }
    }
    nvs_close(handle);
    return ESP_OK;
}

// 写入 blob 并提交；cache/valid 在成功后更新。
static esp_err_t write_blob(const char *key, const uint8_t *blob, size_t size,
                            uint8_t *cache, bool *valid)
{
    if (!s_ready) return ESP_ERR_INVALID_STATE;
    if (*valid && memcmp(cache, blob, size) == 0) return ESP_OK;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, key, blob, size);
        if (err == ESP_OK) err = nvs_commit(handle);
        nvs_close(handle);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "保存 %s 失败: %s", key, esp_err_to_name(err));
        return err;
    }
    memcpy(cache, blob, size);
    *valid = true;
    return ESP_OK;
}

esp_err_t jp_store_save_settings(const jp_settings_t *settings)
{
    uint8_t blob[JP_SETTINGS_BLOB_SIZE];
    jp_settings_encode(settings, blob);
    return write_blob(KEY_SETTINGS, blob, sizeof(blob), s_last_settings, &s_last_settings_valid);
}

esp_err_t jp_store_save_progress(const uint8_t *boxes)
{
    static uint8_t blob[JP_PROGRESS_BLOB_SIZE];
    jp_progress_encode(boxes, blob);
    return write_blob(KEY_PROGRESS, blob, sizeof(blob), s_last_progress, &s_last_progress_valid);
}
