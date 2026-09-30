// main/jp_text.h —— 口袋日语全部界面文案（唯一来源）。
//
// ★ 所有含非 ASCII 字符的界面字符串都必须定义在这里：
//   tools/jp_learner/gen_fonts.py 从本文件与学习内容中提取字符生成字库子集，
//   tests/test_jp_font_coverage.py 会检查 main/jp_*.c 中没有散落的非 ASCII 字面量，
//   并确认生成的字库覆盖了这里的每一个字。改文案后请重新运行 gen_fonts.py。
// 格式串中的 %u / %s 由 lv_label_set_text_fmt() 填充，均为 ASCII。
#pragma once

// ---- 通用 ----
#define JP_TXT_APP_NAME        "口袋日语"
#define JP_TXT_BATTERY_UNKNOWN "--"
#define JP_TXT_SAVE_FAILED     "保存失败"
#define JP_TXT_VOICE_OFF       "语音包不可用，已静音"

// ---- 首页 ----
#define JP_TXT_HOME_MASTERED   "已掌握 %u / %u"
#define JP_TXT_MENU_HIRAGANA   "平假名"
#define JP_TXT_MENU_KATAKANA   "片假名"
#define JP_TXT_MENU_WORDS      "N5 单词"
#define JP_TXT_MENU_QUIZ       "测验"
#define JP_TXT_MENU_SETTINGS   "设置"
#define JP_TXT_CHIP_HIRAGANA   "あ"
#define JP_TXT_CHIP_KATAKANA   "ア"
#define JP_TXT_CHIP_WORDS      "語"
#define JP_TXT_CHIP_QUIZ       "問"
#define JP_TXT_QUIZ_PER_ROUND  "每轮 %u 题"
#define JP_TXT_HINT_HOME       "▲▼ 选择　OK 进入"

// ---- 学习卡片 ----
#define JP_TXT_STUDY_HINT      "▲▼ 切换　OK 发音　长按返回"
#define JP_TXT_COUNTER_KATA    "片假名 %s"
#define JP_TXT_COUNTER_HIRA    "平假名 %s"
#define JP_TXT_SECTION_FMT     "%s · %s"

// ---- 测验 ----
#define JP_TXT_QUIZ_TITLE      "测验"
#define JP_TXT_QUIZ_HIRAGANA   "平假名测验"
#define JP_TXT_QUIZ_KATAKANA   "片假名测验"
#define JP_TXT_QUIZ_WORDS      "单词测验"
#define JP_TXT_QUIZ_KANA_DESC  "看字读音 · 听音辨字"
#define JP_TXT_QUIZ_WORD_DESC  "看词选义 · 听音选义"
#define JP_TXT_QUIZ_READ_DESC  "看字读音"
#define JP_TXT_QUIZ_MEAN_DESC  "看词选义"
#define JP_TXT_HINT_QUIZ_MENU  "▲▼ 选择　OK 开始　长按返回"
#define JP_TXT_QUIZ_PROGRESS   "第 %u / %u 题"
#define JP_TXT_PROMPT_READ     "这个假名怎么读？"
#define JP_TXT_PROMPT_LISTEN_K "听发音，选出假名"
#define JP_TXT_PROMPT_MEANING  "这个词是什么意思？"
#define JP_TXT_PROMPT_LISTEN_M "听发音，选出意思"
#define JP_TXT_REPLAY          "再听一遍"
#define JP_TXT_HINT_ANSWER     "▲▼ 选择　OK 确认"
#define JP_TXT_HINT_NEXT       "OK 下一题"
#define JP_TXT_HINT_FINISH     "OK 查看结果"
#define JP_TXT_CORRECT         "答对了！"
#define JP_TXT_WRONG_FMT       "正确答案：%s"
#define JP_TXT_QUIZ_EMPTY      "无法出题"

// ---- 结果 ----
#define JP_TXT_RESULT_TITLE    "测验结果"
#define JP_TXT_RESULT_SCORE    "答对 %u / %u"
#define JP_TXT_STAMP_GREAT     "すごい！"
#define JP_TXT_STAMP_GOOD      "いいね！"
#define JP_TXT_STAMP_TRY       "がんばれ！"
#define JP_TXT_PRAISE_GREAT    "太棒了"
#define JP_TXT_PRAISE_GOOD     "不错哦"
#define JP_TXT_PRAISE_TRY      "继续加油"
#define JP_TXT_REVIEW_LABEL    "错题"
#define JP_TXT_ALL_CORRECT     "全部答对，没有错题"
#define JP_TXT_HINT_RESULT     "OK 再来一轮　长按返回"

// ---- 设置 ----
#define JP_TXT_SETTINGS_TITLE  "设置"
#define JP_TXT_SET_VOLUME      "音量"
#define JP_TXT_SET_AUTOPLAY    "自动发音"
#define JP_TXT_SET_BRIGHTNESS  "屏幕亮度"
#define JP_TXT_SET_RESET       "重置进度"
#define JP_TXT_ON              "开"
#define JP_TXT_OFF             "关"
#define JP_TXT_BRIGHT_HIGH     "高"
#define JP_TXT_BRIGHT_MID      "中"
#define JP_TXT_BRIGHT_LOW      "低"
#define JP_TXT_RESET_CONFIRM   "再按 OK 确认清空"
#define JP_TXT_RESET_DONE      "已清空"
#define JP_TXT_ABOUT           "离线学习 · 五十音与 N5 词汇"
#define JP_TXT_HINT_SETTINGS   "▲▼ 选择　OK 修改　长按返回"
