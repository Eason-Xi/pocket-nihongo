// 由 tools/jp_learner/gen_data.py 从 tools/jp_learner/jp_content.py 生成，请勿手改。
#pragma once

#define JP_KANA_COUNT          104   // 每种假名（平/片）各自的卡片数
#define JP_WORD_COUNT          153   // N5 单词数
#define JP_KANA_GROUP_COUNT    4     // 清音/浊音/半浊音/拗音
#define JP_WORD_CATEGORY_COUNT 13    // 单词分类数
#define JP_VOICE_CLIP_COUNT    257   // 语音包片段数（假名共用 + 单词）
// 学习进度总卡片数：平假名 + 片假名 + 单词，NVS 中每张卡 1 字节。
#define JP_CARD_TOTAL          (JP_KANA_COUNT * 2 + JP_WORD_COUNT)
// 语音包内容哈希（文本+音色+语速），固件用它拒绝与数据不匹配的语音包。
#define JP_VOICE_HASH          0xA1B5A4A8u
// 卡片布局哈希，卡片顺序或数量变化时 NVS 中的旧进度自动作废。
#define JP_CONTENT_HASH        0x9F18A61Bu
