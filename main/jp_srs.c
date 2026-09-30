// main/jp_srs.c —— 间隔重复盒子与随机数实现。
#include "jp_srs.h"

void jp_rng_seed(jp_rng_t *rng, uint32_t seed)
{
    if (!rng) return;
    rng->state = seed ? seed : 0x2545F491u;
}

uint32_t jp_rng_next(jp_rng_t *rng)
{
    // Marsaglia xorshift32（13, 17, 5），周期 2^32 - 1。
    uint32_t x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

uint32_t jp_rng_below(jp_rng_t *rng, uint32_t bound)
{
    if (bound == 0) return 0;
    // 丢弃落在最后一段不完整区间里的值，保证每个结果概率相同。
    uint32_t limit = UINT32_MAX - (UINT32_MAX % bound);
    uint32_t value;
    do {
        value = jp_rng_next(rng);
    } while (value >= limit);
    return value % bound;
}

uint8_t jp_srs_after_answer(uint8_t box, bool correct)
{
    if (box > JP_SRS_MAX_BOX) box = JP_SRS_MAX_BOX;
    if (!correct) return 0;
    return box < JP_SRS_MAX_BOX ? (uint8_t)(box + 1u) : (uint8_t)JP_SRS_MAX_BOX;
}

uint32_t jp_srs_weight(uint8_t box)
{
    static const uint8_t WEIGHTS[JP_SRS_MAX_BOX + 1] = { 8, 6, 4, 2, 1, 1 };
    return WEIGHTS[box > JP_SRS_MAX_BOX ? JP_SRS_MAX_BOX : box];
}

size_t jp_srs_pick(const uint8_t *boxes, size_t n, size_t want, jp_rng_t *rng, uint16_t *out)
{
    if (!boxes || !rng || !out) return 0;
    if (want > n) want = n;
    size_t picked = 0;
    while (picked < want) {
        // 剩余（未被抽中）卡片的权重和。已抽中的卡片通过线性查找 out 排除，
        // 避免为 n 张卡再分配一张标记表。
        uint32_t total = 0;
        for (size_t i = 0; i < n; i++) {
            bool used = false;
            for (size_t k = 0; k < picked; k++) {
                if (out[k] == i) { used = true; break; }
            }
            if (!used) total += jp_srs_weight(boxes[i]);
        }
        uint32_t target = jp_rng_below(rng, total);
        for (size_t i = 0; i < n; i++) {
            bool used = false;
            for (size_t k = 0; k < picked; k++) {
                if (out[k] == i) { used = true; break; }
            }
            if (used) continue;
            uint32_t weight = jp_srs_weight(boxes[i]);
            if (target < weight) {
                out[picked++] = (uint16_t)i;
                break;
            }
            target -= weight;
        }
    }
    return picked;
}
