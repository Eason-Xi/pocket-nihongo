// main/jp_adpcm.c —— IMA-ADPCM 解码与语音包解析实现。
#include "jp_adpcm.h"

#include <string.h>

// IMA-ADPCM 标准步长表（89 项），与 build_voice.py 的 STEP_TABLE 相同。
static const int16_t STEP_TABLE[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442,
    11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767,
};

// 半字节 → 步长下标增量；低 3 位越大说明差值越大，步长增长越快。
static const int8_t INDEX_TABLE[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8,
};

void jp_adpcm_reset(jp_adpcm_state_t *state)
{
    if (!state) return;
    state->predictor = 0;
    state->index = 0;
}

void jp_adpcm_decode(jp_adpcm_state_t *state, const uint8_t *data,
                     size_t first, size_t count, int16_t *out)
{
    if (!state || !data || !out) return;
    int32_t predictor = state->predictor;
    int32_t index = state->index;
    for (size_t i = 0; i < count; i++) {
        size_t position = first + i;
        uint8_t byte = data[position >> 1];
        // 偶数采样取低半字节，奇数采样取高半字节（与编码器打包顺序一致）。
        uint8_t nibble = (position & 1u) ? (uint8_t)(byte >> 4) : (uint8_t)(byte & 0x0Fu);

        int32_t step = STEP_TABLE[index];
        // vpdiff = (2*|n| + 1) * step / 8 的整数近似，与标准实现逐位相同。
        int32_t vpdiff = step >> 3;
        if (nibble & 4u) vpdiff += step;
        if (nibble & 2u) vpdiff += step >> 1;
        if (nibble & 1u) vpdiff += step >> 2;
        predictor += (nibble & 8u) ? -vpdiff : vpdiff;
        if (predictor > 32767) predictor = 32767;
        else if (predictor < -32768) predictor = -32768;

        index += INDEX_TABLE[nibble];
        if (index < 0) index = 0;
        else if (index > 88) index = 88;

        out[i] = (int16_t)predictor;
    }
    state->predictor = predictor;
    state->index = index;
}

// 小端读取：嵌入的二进制在 Flash 中不保证 4 字节对齐，因此逐字节拼接。
static uint16_t read_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

jp_pack_err_t jp_voice_pack_open(jp_voice_pack_t *pack, const uint8_t *blob, size_t size)
{
    if (!pack || !blob) return JP_PACK_ERR_ARG;
    memset(pack, 0, sizeof(*pack));
    if (size < JP_VOICE_PACK_HEADER_SIZE) return JP_PACK_ERR_SIZE;
    if (memcmp(blob, "JPV1", 4) != 0) return JP_PACK_ERR_MAGIC;
    if (read_u16(blob + 4) != JP_VOICE_PACK_VERSION) return JP_PACK_ERR_VERSION;

    uint16_t count = read_u16(blob + 8);
    size_t table_end = JP_VOICE_PACK_HEADER_SIZE + (size_t)count * JP_VOICE_PACK_ENTRY_SIZE;
    if (table_end > size) return JP_PACK_ERR_SIZE;

    // 逐个检查片段：数据必须在片段表之后、且完整落在包内。
    // 用减法比较避免 offset + bytes 溢出。
    for (uint16_t i = 0; i < count; i++) {
        const uint8_t *entry = blob + JP_VOICE_PACK_HEADER_SIZE + (size_t)i * JP_VOICE_PACK_ENTRY_SIZE;
        uint32_t offset = read_u32(entry);
        uint32_t samples = read_u32(entry + 4);
        size_t bytes = ((size_t)samples + 1u) / 2u;
        if (offset < table_end || offset > size || bytes > size - offset) return JP_PACK_ERR_CLIP;
    }

    pack->base = blob;
    pack->size = size;
    pack->sample_rate = read_u16(blob + 6);
    pack->clip_count = count;
    pack->hash = read_u32(blob + 12);
    return JP_PACK_OK;
}

bool jp_voice_pack_clip(const jp_voice_pack_t *pack, uint16_t clip,
                        const uint8_t **data, uint32_t *samples)
{
    if (!pack || !pack->base || clip >= pack->clip_count) return false;
    const uint8_t *entry = pack->base + JP_VOICE_PACK_HEADER_SIZE + (size_t)clip * JP_VOICE_PACK_ENTRY_SIZE;
    if (data) *data = pack->base + read_u32(entry);
    if (samples) *samples = read_u32(entry + 4);
    return true;
}
