// tests/test_jp_adpcm.c —— IMA-ADPCM 解码器与语音包解析的主机测试。
//
// 1. 金标准：GOLDEN_ADPCM / GOLDEN_PCM 由 tools/jp_learner/build_voice.py 的
//    adpcm_encode()/adpcm_decode() 生成，证明固件解码器与打包工具逐位一致；
// 2. 分块解码与一次性解码结果相同（语音任务按 256 采样分块）；
// 3. 语音包解析拒绝各种损坏输入；
// 4. 读取仓库中真实的 assets/music/jp_voice_pack.bin：哈希、片段数与生成的
//    jp_data_gen.h 一致，且每个片段都能完整解码。
// 编译（仓库根目录运行）：
//   cc -std=c11 -Wall -Wextra -Werror -Imain tests/test_jp_adpcm.c main/jp_adpcm.c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jp_adpcm.h"
#include "jp_data_gen.h"

static const uint8_t GOLDEN_ADPCM[] = {
    0x00, 0x00, 0x70, 0x77, 0x77, 0x77, 0xfb, 0xaf, 0x18, 0x42, 0x24, 0x82,
    0xd9, 0xbb, 0x8a, 0x62, 0x57, 0x00, 0x00, 0xcf, 0x08, 0x88, 0x05, 0x08,
};
static const int16_t GOLDEN_PCM[] = {
    0, 0, 0, 0, 0, 11, 41, 104, 240, 533, 1164, 2521, 1163, -1481, -7151, -11203,
    -11939, -9931, -6888, -1907, 4120, 8172, 11855, 11186, 9361, 3273, -2400, -7556,
    -10904, -11512, -8745, -2203, 11169, 32191, 32767, 32767, 32767, 32767, 4101, -32761,
    -32768, -29044, -32429, -32768, -1989, 2106, -1618, 1767,
};
#define GOLDEN_SAMPLES (sizeof(GOLDEN_PCM) / sizeof(GOLDEN_PCM[0]))

static void test_golden(void)
{
    int16_t out[GOLDEN_SAMPLES];
    jp_adpcm_state_t state;
    jp_adpcm_reset(&state);
    jp_adpcm_decode(&state, GOLDEN_ADPCM, 0, GOLDEN_SAMPLES, out);
    assert(memcmp(out, GOLDEN_PCM, sizeof(out)) == 0);

    // 以奇数长度分块解码（跨越半字节边界），结果必须与一次性解码相同。
    int16_t chunked[GOLDEN_SAMPLES];
    jp_adpcm_reset(&state);
    for (size_t pos = 0; pos < GOLDEN_SAMPLES; pos += 7) {
        size_t n = GOLDEN_SAMPLES - pos < 7 ? GOLDEN_SAMPLES - pos : 7;
        jp_adpcm_decode(&state, GOLDEN_ADPCM, pos, n, chunked + pos);
    }
    assert(memcmp(chunked, GOLDEN_PCM, sizeof(chunked)) == 0);
    assert(state.index >= 0 && state.index <= 88);

    // 空指针安全。
    jp_adpcm_decode(NULL, GOLDEN_ADPCM, 0, 4, out);
    jp_adpcm_reset(NULL);
}

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

// 构造一个 2 片段的合法语音包：片段 0 = 金标准数据，片段 1 = 3 个采样（2 字节）。
static size_t make_pack(uint8_t *buf)
{
    memcpy(buf, "JPV1", 4);
    put16(buf + 4, 1);
    put16(buf + 6, 16000);
    put16(buf + 8, 2);
    put16(buf + 10, 0);
    put32(buf + 12, 0xCAFEBABEu);
    uint32_t data = JP_VOICE_PACK_HEADER_SIZE + 2 * JP_VOICE_PACK_ENTRY_SIZE;
    put32(buf + 16, data);
    put32(buf + 20, GOLDEN_SAMPLES);
    put32(buf + 24, data + sizeof(GOLDEN_ADPCM));
    put32(buf + 28, 3);
    memcpy(buf + data, GOLDEN_ADPCM, sizeof(GOLDEN_ADPCM));
    buf[data + sizeof(GOLDEN_ADPCM)] = 0x11;
    buf[data + sizeof(GOLDEN_ADPCM) + 1] = 0x01;
    return data + sizeof(GOLDEN_ADPCM) + 2;
}

static void test_pack_parsing(void)
{
    uint8_t buf[128];
    size_t size = make_pack(buf);
    jp_voice_pack_t pack;
    assert(jp_voice_pack_open(&pack, buf, size) == JP_PACK_OK);
    assert(pack.clip_count == 2 && pack.sample_rate == 16000 && pack.hash == 0xCAFEBABEu);

    const uint8_t *data;
    uint32_t samples;
    assert(jp_voice_pack_clip(&pack, 0, &data, &samples));
    assert(samples == GOLDEN_SAMPLES && memcmp(data, GOLDEN_ADPCM, sizeof(GOLDEN_ADPCM)) == 0);
    assert(jp_voice_pack_clip(&pack, 1, &data, &samples) && samples == 3);
    assert(!jp_voice_pack_clip(&pack, 2, &data, &samples));

    // 未对齐的起始地址也能解析（Flash 中的嵌入数据不保证对齐）。
    uint8_t shifted[129];
    memcpy(shifted + 1, buf, size);
    assert(jp_voice_pack_open(&pack, shifted + 1, size) == JP_PACK_OK);

    // 各类损坏输入。
    assert(jp_voice_pack_open(&pack, NULL, size) == JP_PACK_ERR_ARG);
    assert(jp_voice_pack_open(&pack, buf, 10) == JP_PACK_ERR_SIZE);
    assert(pack.base == NULL && !jp_voice_pack_clip(&pack, 0, &data, &samples));
    uint8_t bad[128];
    memcpy(bad, buf, size);
    bad[0] = 'X';
    assert(jp_voice_pack_open(&pack, bad, size) == JP_PACK_ERR_MAGIC);
    memcpy(bad, buf, size);
    put16(bad + 4, 2);
    assert(jp_voice_pack_open(&pack, bad, size) == JP_PACK_ERR_VERSION);
    memcpy(bad, buf, size);
    put16(bad + 8, 60000);   // 片段表超出包长
    assert(jp_voice_pack_open(&pack, bad, size) == JP_PACK_ERR_SIZE);
    memcpy(bad, buf, size);
    put32(bad + 28, 5);      // 片段 1 声称 5 个采样（需要 3 字节），只剩 2 字节
    assert(jp_voice_pack_open(&pack, bad, size) == JP_PACK_ERR_CLIP);
    memcpy(bad, buf, size);
    put32(bad + 16, 4);      // 数据偏移落在片段表内部
    assert(jp_voice_pack_open(&pack, bad, size) == JP_PACK_ERR_CLIP);
    memcpy(bad, buf, size);
    put32(bad + 24, 0xFFFFFFF0u);   // 巨大偏移，不能因加法溢出而通过
    assert(jp_voice_pack_open(&pack, bad, size) == JP_PACK_ERR_CLIP);
    assert(jp_voice_pack_open(&pack, buf, size - 1) == JP_PACK_ERR_CLIP);
}

static void test_real_pack(void)
{
    FILE *file = fopen("assets/music/jp_voice_pack.bin", "rb");
    if (!file) {
        // 语音包允许不提交（再分发条款需自行确认）；缺失时固件为静音模式，这里跳过。
        printf("语音包缺失，跳过真实语音包检查（请在仓库根目录运行本测试）\n");
        return;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    assert(size > 0);
    uint8_t *blob = malloc((size_t)size);
    assert(blob);
    assert(fread(blob, 1, (size_t)size, file) == (size_t)size);
    fclose(file);

    jp_voice_pack_t pack;
    assert(jp_voice_pack_open(&pack, blob, (size_t)size) == JP_PACK_OK);
    assert(pack.hash == JP_VOICE_HASH);          // 语音包与学习内容同步
    assert(pack.clip_count == JP_VOICE_CLIP_COUNT);
    assert(pack.sample_rate == 16000);

    static int16_t pcm[256];
    uint32_t total = 0;
    for (uint16_t clip = 0; clip < pack.clip_count; clip++) {
        const uint8_t *data;
        uint32_t samples;
        assert(jp_voice_pack_clip(&pack, clip, &data, &samples));
        // 每段 0.2–3 秒：太短说明合成/裁剪出错，太长说明静音没去掉。
        assert(samples >= 16000 / 5 && samples <= 16000 * 3);
        jp_adpcm_state_t state;
        jp_adpcm_reset(&state);
        int peak = 0;
        for (uint32_t pos = 0; pos < samples; pos += 256) {
            uint32_t n = samples - pos < 256 ? samples - pos : 256;
            jp_adpcm_decode(&state, data, pos, n, pcm);
            for (uint32_t i = 0; i < n; i++) {
                int v = pcm[i] < 0 ? -pcm[i] : pcm[i];
                if (v > peak) peak = v;
            }
        }
        assert(peak > 3000);   // 不是静音片段
        total += samples;
    }
    free(blob);
    printf("语音包: %u 段, 共 %.1f 秒\n", (unsigned)JP_VOICE_CLIP_COUNT, total / 16000.0);
}

int main(void)
{
    test_golden();
    test_pack_parsing();
    test_real_pack();
    printf("test_jp_adpcm: PASS\n");
    return 0;
}
