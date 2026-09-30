// main/jp_srs.h —— 学习进度（Leitner 间隔重复盒子）与随机数（纯逻辑，可主机测试）。
//
// 每张卡片有一个盒子等级 0..JP_SRS_MAX_BOX：
//   0        还没答对过（或刚答错）；
//   +1       每答对一次升一级，最高 JP_SRS_MAX_BOX；
//   归零     答错立即回到 0 —— 规则简单直观：「连续答对 3 次算掌握」。
// 测验抽题时，盒子越低的卡片权重越高，薄弱内容会更频繁地出现。
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define JP_SRS_MAX_BOX      5u   // 最高盒子等级
#define JP_SRS_MASTERED_BOX 3u   // 盒子 >= 3 视为「已掌握」，首页进度按此统计

// xorshift32 伪随机数发生器。只用于出题，不用于任何安全用途。
typedef struct {
    uint32_t state;   // 永不为 0（0 是 xorshift 的不动点）
} jp_rng_t;

// 播种；seed 为 0 时替换成固定非零常数。
void jp_rng_seed(jp_rng_t *rng, uint32_t seed);

// 下一个 32 位随机数。
uint32_t jp_rng_next(jp_rng_t *rng);

// [0, bound) 内的均匀随机整数（拒绝采样消除取模偏差）；bound 为 0 时返回 0。
uint32_t jp_rng_below(jp_rng_t *rng, uint32_t bound);

// 答题后的新盒子等级：答对 +1（封顶），答错归零。输入超范围时先钳到上限。
uint8_t jp_srs_after_answer(uint8_t box, bool correct);

// 抽题权重：盒子 0→8、1→6、2→4、3→2、4/5→1。
uint32_t jp_srs_weight(uint8_t box);

// 按权重「不放回」抽取最多 want 个互不相同的下标（范围 [0, n)）写入 out，
// 返回实际抽取数量 min(want, n)。boxes[i] 为第 i 张卡的盒子等级。
// 时间复杂度 O(n × want)，本应用 n ≤ 153、want ≤ 10，可忽略。
size_t jp_srs_pick(const uint8_t *boxes, size_t n, size_t want, jp_rng_t *rng, uint16_t *out);
