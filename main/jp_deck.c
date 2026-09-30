// main/jp_deck.c —— 卡组抽象的纯逻辑实现（无硬件依赖，主机测试直接编译）。
#include "jp_data.h"

size_t jp_deck_size(jp_deck_t deck)
{
    switch (deck) {
    case JP_DECK_HIRAGANA:
    case JP_DECK_KATAKANA:
        return JP_KANA_COUNT;
    case JP_DECK_WORDS:
        return JP_WORD_COUNT;
    default:
        return 0;
    }
}

size_t jp_deck_card_id(jp_deck_t deck, size_t index)
{
    if (index >= jp_deck_size(deck)) return JP_CARD_TOTAL;
    // 进度数组分段：[平假名 104][片假名 104][单词 153]，与 jp_deck_t 顺序一致。
    switch (deck) {
    case JP_DECK_HIRAGANA: return index;
    case JP_DECK_KATAKANA: return JP_KANA_COUNT + index;
    default:               return JP_KANA_COUNT * 2 + index;
    }
}

uint16_t jp_deck_voice(jp_deck_t deck, size_t index)
{
    if (index >= jp_deck_size(deck)) return UINT16_MAX;
    return deck == JP_DECK_WORDS ? JP_WORDS[index].voice : JP_KANA[index].voice;
}

const char *jp_deck_face(jp_deck_t deck, size_t index)
{
    if (index >= jp_deck_size(deck)) return "";
    switch (deck) {
    case JP_DECK_HIRAGANA: return JP_KANA[index].hira;
    case JP_DECK_KATAKANA: return JP_KANA[index].kata;
    default:               return JP_WORDS[index].word;
    }
}

const char *jp_deck_answer(jp_deck_t deck, size_t index)
{
    if (index >= jp_deck_size(deck)) return "";
    return deck == JP_DECK_WORDS ? JP_WORDS[index].meaning : JP_KANA[index].romaji;
}

uint8_t jp_deck_group(jp_deck_t deck, size_t index)
{
    if (index >= jp_deck_size(deck)) return 0;
    return deck == JP_DECK_WORDS ? JP_WORDS[index].category : JP_KANA[index].group;
}

size_t jp_deck_mastered(jp_deck_t deck, const uint8_t *boxes, uint8_t mastered_box)
{
    if (!boxes) return 0;
    size_t count = 0;
    for (size_t i = 0; i < jp_deck_size(deck); i++) {
        if (boxes[jp_deck_card_id(deck, i)] >= mastered_box) count++;
    }
    return count;
}

size_t jp_utf8_count(const char *text)
{
    size_t count = 0;
    if (!text) return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        // 只统计首字节：ASCII(0xxxxxxx) 或多字节序列起始(11xxxxxx)。
        if ((*p & 0xC0) != 0x80) count++;
    }
    return count;
}
