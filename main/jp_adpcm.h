// main/jp_adpcm.h —— IMA-ADPCM 4 bit 解码器 + 语音包解析（纯逻辑，可主机测试）。
//
// 语音包由 tools/jp_learner/build_voice.py 生成并以二进制形式嵌入固件 Flash，
// 格式说明见该脚本文件头。解码器与脚本里的 adpcm_encode()/adpcm_decode() 逐位一致：
// 每个片段从 predictor=0、step_index=0 开始，半字节流低半字节在前。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// 解码器状态。一个片段内需连续解码：第 n 次调用必须紧接上一次的末尾采样。
typedef struct {
    int32_t predictor;   // 上一个输出采样，范围 [-32768, 32767]
    int32_t index;       // 步长表下标，范围 [0, 88]
} jp_adpcm_state_t;

// 复位到片段起始状态（predictor=0，index=0）。
void jp_adpcm_reset(jp_adpcm_state_t *state);

// 从半字节流 data 的第 first 个采样开始，解出 count 个 16 bit PCM 到 out。
// 调用方保证 data 至少覆盖 (first + count + 1) / 2 字节、state 对应第 first 个采样。
// 纯计算、不阻塞、不分配内存；state/out 为 NULL 时直接返回。
void jp_adpcm_decode(jp_adpcm_state_t *state, const uint8_t *data,
                     size_t first, size_t count, int16_t *out);

// ---------------------------------------------------------------------------
// 语音包
// ---------------------------------------------------------------------------

#define JP_VOICE_PACK_HEADER_SIZE 16u   // magic + version + rate + count + reserved + hash
#define JP_VOICE_PACK_ENTRY_SIZE  8u    // u32 data_offset + u32 sample_count
#define JP_VOICE_PACK_VERSION     1u

// 打开语音包的结果码；除 OK 外均表示包不可用（固件降级为无声模式）。
typedef enum {
    JP_PACK_OK = 0,
    JP_PACK_ERR_ARG,        // 参数为空
    JP_PACK_ERR_SIZE,       // 长度不足以容纳包头/片段表
    JP_PACK_ERR_MAGIC,      // magic 不是 "JPV1"
    JP_PACK_ERR_VERSION,    // 版本不支持
    JP_PACK_ERR_CLIP,       // 某个片段的偏移/长度越界
} jp_pack_err_t;

// 已校验的语音包视图。不拷贝数据：base 指向调用方提供、需在使用期内有效的内存（Flash）。
typedef struct {
    const uint8_t *base;    // 包首地址
    size_t size;            // 包总字节数
    uint16_t sample_rate;   // 采样率（生成脚本固定 16000）
    uint16_t clip_count;    // 片段数
    uint32_t hash;          // 内容哈希，应等于 JP_VOICE_HASH
} jp_voice_pack_t;

// 校验并打开语音包：检查包头、版本以及每个片段都完整落在包内。
// 所有多字节字段按小端逐字节读取，不要求 blob 对齐。
jp_pack_err_t jp_voice_pack_open(jp_voice_pack_t *pack, const uint8_t *blob, size_t size);

// 取第 clip 个片段的半字节流与采样数；clip 越界或 pack 未打开返回 false。
bool jp_voice_pack_clip(const jp_voice_pack_t *pack, uint16_t clip,
                        const uint8_t **data, uint32_t *samples);
