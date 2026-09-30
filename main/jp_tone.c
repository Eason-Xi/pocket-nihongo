// main/jp_tone.c —— 答题提示音合成实现。
#include "jp_tone.h"

// 一个音符：频率(Hz) + 时长(ms)。
typedef struct {
    uint16_t hz;
    uint16_t ms;
} jp_note_t;

// 小喇叭 300 Hz 以下几乎放不出声，因此答错音也选在 330/262 Hz 以上。
static const jp_note_t CUE_CORRECT[] = { { 988, 70 }, { 1319, 150 } };   // B5 → E6
static const jp_note_t CUE_WRONG[] = { { 330, 110 }, { 262, 190 } };     // E4 → C4

// 峰值约为满幅的 1/3（≈ -9.5 dBFS），比人声略轻，不盖过随后播放的发音。
#define TONE_AMPLITUDE 11000
#define ATTACK_MS      4u      // 起音线性渐强，避免咔哒声

// 一个完整周期 256 点的正弦表（满幅 32767），由 round(32767*sin(2πi/256)) 生成。
static const int16_t SINE_TABLE[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739,
    9512, 10278, 11039, 11793, 12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530,
    18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594, 23170, 23731, 24279, 24811,
    25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521,
    32609, 32678, 32728, 32757, 32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285,
    32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571, 30273, 29956, 29621, 29268,
    28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151,
    15446, 14732, 14010, 13279, 12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179,
    6393, 5602, 4808, 4011, 3212, 2410, 1608, 804, 0, -804, -1608, -2410,
    -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159,
    -20787, -21403, -22005, -22594, -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790,
    -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956, -30273, -30571, -30852, -31113,
    -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580,
    -31356, -31113, -30852, -30571, -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683,
    -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731, -23170, -22594, -22005, -21403,
    -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011,
    -3212, -2410, -1608, -804,
};

// 取提示音的音符表；非法 cue 返回 NULL。
static const jp_note_t *cue_notes(jp_cue_t cue, size_t *count)
{
    switch (cue) {
    case JP_CUE_CORRECT:
        *count = sizeof(CUE_CORRECT) / sizeof(CUE_CORRECT[0]);
        return CUE_CORRECT;
    case JP_CUE_WRONG:
        *count = sizeof(CUE_WRONG) / sizeof(CUE_WRONG[0]);
        return CUE_WRONG;
    default:
        *count = 0;
        return NULL;
    }
}

static size_t note_samples(const jp_note_t *note)
{
    return (size_t)note->ms * JP_TONE_SAMPLE_RATE / 1000u;
}

size_t jp_tone_length(jp_cue_t cue)
{
    size_t count = 0;
    const jp_note_t *notes = cue_notes(cue, &count);
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += note_samples(&notes[i]);
    (void)notes;
    return total;
}

// 单个采样：正弦 × 包络。包络 = 起音 ATTACK_MS 线性渐强，之后线性衰减到 0，
// 听感像拨弦/木琴，音符之间不需要额外间隔也不会粘连。
static int16_t render_sample(const jp_note_t *note, size_t local, size_t length)
{
    uint32_t phase = (uint32_t)(((uint64_t)local * note->hz * 256u) / JP_TONE_SAMPLE_RATE) & 0xFFu;
    int32_t value = (int32_t)SINE_TABLE[phase] * TONE_AMPLITUDE / 32767;

    size_t attack = ATTACK_MS * JP_TONE_SAMPLE_RATE / 1000u;
    int32_t env_q15;   // Q15 定点包络，0..32767
    if (local < attack) {
        env_q15 = (int32_t)(local * 32767u / attack);
    } else {
        env_q15 = (int32_t)((length - local) * 32767u / (length - attack));
    }
    return (int16_t)(value * env_q15 / 32767);
}

void jp_tone_render(jp_cue_t cue, size_t first, int16_t *out, size_t count)
{
    if (!out) return;
    size_t note_count = 0;
    const jp_note_t *notes = cue_notes(cue, &note_count);
    for (size_t i = 0; i < count; i++) {
        size_t position = first + i;
        int16_t sample = 0;
        // 线性查找所在音符；提示音只有 2 个音符，开销可忽略。
        for (size_t n = 0; n < note_count; n++) {
            size_t length = note_samples(&notes[n]);
            if (position < length) {
                sample = render_sample(&notes[n], position, length);
                break;
            }
            position -= length;
        }
        out[i] = sample;
    }
}
