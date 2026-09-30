// main/jp_main.c —— 口袋日语（Pocket Nihongo）固件入口。
//
// 启动顺序：共享 I2C → 显示/LVGL（失败则无法继续）→ 音频、电量计（失败降级）→ 应用。
// 背光在应用任务画好首页后才点亮，避免开机时看到未初始化的显存内容。
#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_log.h"
#include "jp_app.h"

static const char *TAG = "jp_main";

void app_main(void)
{
    ESP_LOGI(TAG, "口袋日语 启动");

    esp_err_t err = bsp_i2c_init();
    if (err != ESP_OK) ESP_LOGW(TAG, "I2C 初始化失败: %s（音频与电量将不可用）", esp_err_to_name(err));

    if (bsp_display_init() != ESP_OK || !bsp_lvgl_init()) {
        ESP_LOGE(TAG, "显示/LVGL 初始化失败，应用无法运行。检查 SPI 接线(MOSI=%d SCLK=%d CS=%d DC=%d BL=%d)",
                 BSP_LCD_MOSI, BSP_LCD_SCLK, BSP_LCD_CS, BSP_LCD_DC, BSP_LCD_BL);
        return;
    }
    bsp_display_backlight(0);

    bool audio_ok = bsp_audio_init() == ESP_OK;
    bool battery_ok = bsp_battery_init() == ESP_OK;
    if (!audio_ok) ESP_LOGW(TAG, "ES8311 音频初始化失败，静音运行");
    if (!battery_ok) ESP_LOGW(TAG, "CW2017 电量计不可用，电量显示为 --");

    err = jp_app_start(audio_ok, battery_ok);
    if (err != ESP_OK) ESP_LOGE(TAG, "应用启动不完整: %s", esp_err_to_name(err));
}
