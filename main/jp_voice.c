// main/jp_voice.c —— 发音与提示音播放服务实现。
#include "jp_voice.h"

#include <string.h>

#include "bsp_audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jp_adpcm.h"
#include "jp_data.h"

static const char *TAG = "jp_voice";

// 语音包由 main/CMakeLists.txt 的 EMBED_FILES 嵌入，符号名由文件名 jp_voice_pack.bin 推导。
extern const uint8_t s_pack_start[] asm("_binary_jp_voice_pack_bin_start");
extern const uint8_t s_pack_end[] asm("_binary_jp_voice_pack_bin_end");

#define VOICE_SAMPLE_RATE  16000u
#define CHUNK_SAMPLES      256u     // 16 ms/块：取消请求的最大响应延迟
#define CUE_GAP_SAMPLES    1280u    // 提示音与发音之间 80 ms 静音，避免两者粘连
#define VOICE_TASK_STACK   3072u
// 高于 LVGL 渲染任务(4)与应用任务(5)：解码很轻，bsp_audio_write 会阻塞等待 DMA，
// 不会长期占用 CPU；优先级高可避免刷屏时 PCM 供给中断产生爆音。
#define VOICE_TASK_PRIO    6

// 通知值编码（xTaskNotify eSetValueWithOverwrite，天然只保留最新请求）：
//   bit 0..15   发音片段号，JP_VOICE_NO_CLIP 表示无
//   bit 16..19  jp_cue_t 提示音
//   bit 30      有效请求标记（避免值 0 与“无请求”混淆）
//   bit 31      停止请求
#define REQ_CLIP_MASK  0xFFFFu
#define REQ_CUE_SHIFT  16u
#define REQ_CUE_MASK   0xFu
#define REQ_VALID      (1u << 30)
#define REQ_STOP       (1u << 31)

static TaskHandle_t s_task;              // 播放任务句柄；NULL 表示服务不可用
static jp_voice_pack_t s_pack;           // 已校验的语音包视图（只读，初始化后不变）
static bool s_pack_ok;                   // 语音包校验结果
static esp_err_t s_init_result = ESP_FAIL;
static bool s_initialized;
static volatile bool s_busy;             // 任务写、其他任务读；单字节读写天然原子
static volatile int16_t s_volume_pending = -1;   // -1 表示无待应用的音量
static int16_t s_pcm[CHUNK_SAMPLES];     // 仅语音任务访问

// 非阻塞检查是否有新请求到达。有则写入 *next 并返回 true，调用方应立即中止当前播放。
static bool poll_request(uint32_t *next)
{
    uint32_t value = 0;
    if (xTaskNotifyWait(0, UINT32_MAX, &value, 0) == pdTRUE && value) {
        *next = value;
        return true;
    }
    return false;
}

// 在语音任务内应用待定音量（codec 的 I2C 操作只在本任务里做）。
static void apply_volume(void)
{
    int16_t volume = s_volume_pending;
    if (volume >= 0) {
        s_volume_pending = -1;
        bsp_audio_set_volume((uint8_t)volume);
    }
}

// 写一块 PCM；写失败时记日志并返回 false，中止本次播放（下一次请求会重试）。
static bool write_chunk(size_t samples)
{
    esp_err_t err = bsp_audio_write(s_pcm, samples * sizeof(int16_t));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "PCM 写入失败: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

// 播放一次请求（提示音 → 间隔 → 发音）。返回 true 表示被新请求打断，*next 已填好。
static bool play_request(uint32_t request, uint32_t *next)
{
    jp_cue_t cue = (jp_cue_t)((request >> REQ_CUE_SHIFT) & REQ_CUE_MASK);
    uint16_t clip = (uint16_t)(request & REQ_CLIP_MASK);

    size_t cue_len = jp_tone_length(cue);
    for (size_t pos = 0; pos < cue_len; pos += CHUNK_SAMPLES) {
        if (poll_request(next)) return true;
        size_t n = cue_len - pos < CHUNK_SAMPLES ? cue_len - pos : CHUNK_SAMPLES;
        jp_tone_render(cue, pos, s_pcm, n);
        if (!write_chunk(n)) return false;
    }

    const uint8_t *data = NULL;
    uint32_t samples = 0;
    if (!s_pack_ok || clip == JP_VOICE_NO_CLIP ||
        !jp_voice_pack_clip(&s_pack, clip, &data, &samples)) {
        return false;
    }
    if (cue_len) {
        memset(s_pcm, 0, sizeof(s_pcm));
        for (size_t pos = 0; pos < CUE_GAP_SAMPLES; pos += CHUNK_SAMPLES) {
            if (poll_request(next)) return true;
            size_t n = CUE_GAP_SAMPLES - pos < CHUNK_SAMPLES ? CUE_GAP_SAMPLES - pos : CHUNK_SAMPLES;
            if (!write_chunk(n)) return false;
        }
    }

    jp_adpcm_state_t state;
    jp_adpcm_reset(&state);
    for (size_t pos = 0; pos < samples; pos += CHUNK_SAMPLES) {
        if (poll_request(next)) return true;
        size_t n = samples - pos < CHUNK_SAMPLES ? samples - pos : CHUNK_SAMPLES;
        jp_adpcm_decode(&state, data, pos, n, s_pcm);
        if (!write_chunk(n)) return false;
    }
    return false;
}

static void voice_task(void *arg)
{
    (void)arg;
    bool format_ready = false;
    uint32_t request = 0;
    for (;;) {
        if (!request) {
            s_busy = false;
            xTaskNotifyWait(0, UINT32_MAX, &request, portMAX_DELAY);
            continue;   // 回到循环顶部重新判断（可能收到的是 0 或停止请求）
        }
        uint32_t current = request;
        request = 0;
        if (current & REQ_STOP || !(current & REQ_VALID)) continue;

        s_busy = true;
        if (!format_ready) {
            // 语音包与提示音均为 16 kHz / 16 bit / 单声道；格式只需设置一次。
            esp_err_t err = bsp_audio_set_format(VOICE_SAMPLE_RATE, 16, 1);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "音频格式设置失败: %s", esp_err_to_name(err));
                continue;   // 下次请求再试
            }
            format_ready = true;
        }
        apply_volume();
        (void)play_request(current, &request);
    }
}

esp_err_t jp_voice_init(uint8_t volume_percent)
{
    if (s_initialized) return s_init_result;
    s_initialized = true;

    size_t size = (size_t)(s_pack_end - s_pack_start);
    jp_pack_err_t pack_err = jp_voice_pack_open(&s_pack, s_pack_start, size);
    s_pack_ok = pack_err == JP_PACK_OK && s_pack.hash == JP_VOICE_HASH &&
                s_pack.clip_count == JP_VOICE_CLIP_COUNT && s_pack.sample_rate == VOICE_SAMPLE_RATE;
    if (!s_pack_ok) {
        ESP_LOGE(TAG, "语音包不可用: err=%d hash=0x%08lx(期望 0x%08lx) clips=%u(期望 %u) rate=%u",
                 (int)pack_err, (unsigned long)s_pack.hash, (unsigned long)JP_VOICE_HASH,
                 (unsigned)s_pack.clip_count, (unsigned)JP_VOICE_CLIP_COUNT, (unsigned)s_pack.sample_rate);
    } else {
        ESP_LOGI(TAG, "语音包就绪: %u 段, %u 字节", (unsigned)s_pack.clip_count, (unsigned)size);
    }

    s_volume_pending = volume_percent > 100 ? 100 : volume_percent;
    if (xTaskCreate(voice_task, "jp_voice", VOICE_TASK_STACK, NULL, VOICE_TASK_PRIO, &s_task) != pdPASS) {
        s_task = NULL;
        s_init_result = ESP_ERR_NO_MEM;
        ESP_LOGE(TAG, "语音任务创建失败");
        return s_init_result;
    }
    s_init_result = s_pack_ok ? ESP_OK : ESP_ERR_INVALID_CRC;
    return s_init_result;
}

bool jp_voice_clips_ready(void)
{
    return s_task && s_pack_ok;
}

void jp_voice_play(uint16_t clip, jp_cue_t cue)
{
    if (!s_task) return;
    if ((cue == JP_CUE_NONE || cue >= JP_CUE_COUNT) && (clip == JP_VOICE_NO_CLIP || !s_pack_ok)) return;
    uint32_t value = REQ_VALID | ((uint32_t)(cue & REQ_CUE_MASK) << REQ_CUE_SHIFT) | clip;
    s_busy = true;   // 立即置忙，避免请求刚发出时空闲计时误判
    xTaskNotify(s_task, value, eSetValueWithOverwrite);
}

void jp_voice_stop(void)
{
    if (!s_task) return;
    xTaskNotify(s_task, REQ_STOP, eSetValueWithOverwrite);
}

void jp_voice_set_volume(uint8_t percent)
{
    s_volume_pending = percent > 100 ? 100 : percent;
}

bool jp_voice_busy(void)
{
    return s_busy;
}
