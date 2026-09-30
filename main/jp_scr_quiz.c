// main/jp_scr_quiz.c —— 测验三个页面：选择卡组、答题、结果。
//
// 答题页布局（y 坐标）：
//   0..44     页眉「第 3 / 10 题」+ 电量
//   46..50    10 段进度条：答对抹茶绿、答错朱红、当前墨色、未答浅色
//   56..73    题目提示（答题后变为「答对了！」/「正确答案：…」）
//   80..170   题面卡片：假名大字 / 单词+读音 / 听力题的喇叭（可选中，OK 重听）
//   176..280  三个选项行（A/B/C），每行 32 px
//   289..306  操作提示
// 按键：答题前 ▲/▼ 移动光标（听力题光标可停在题面卡片上重听），OK 作答；
//       答题后 OK 下一题；任何时候长按 OK 退出本轮（已答题目的进度保留）。
// 进度写入：每次作答立即更新内存中的盒子等级；NVS 只在本轮结束或中途退出时保存一次，
// 避免答题反馈音播放期间频繁擦写 Flash。
#include <string.h>

#include "jp_app.h"
#include "jp_text.h"
#include "jp_ui.h"
#include "jp_voice.h"

// ===========================================================================
// 选择卡组
// ===========================================================================

#define MENU_ROWS 3
static jp_row_t s_menu_rows[MENU_ROWS];
static int s_menu_selected;

static void menu_refresh(void)
{
    for (int i = 0; i < MENU_ROWS; i++) jp_ui_row_select(&s_menu_rows[i], i == s_menu_selected);
}

static void menu_build(lv_obj_t *screen, int arg)
{
    bool listen = jp_app()->voice_clips;
    jp_ui_header(screen, JP_TXT_QUIZ_TITLE);

    const char *chips[MENU_ROWS] = { JP_TXT_CHIP_HIRAGANA, JP_TXT_CHIP_KATAKANA, JP_TXT_CHIP_WORDS };
    const char *titles[MENU_ROWS] = { JP_TXT_QUIZ_HIRAGANA, JP_TXT_QUIZ_KATAKANA, JP_TXT_QUIZ_WORDS };
    const char *kana_desc = listen ? JP_TXT_QUIZ_KANA_DESC : JP_TXT_QUIZ_READ_DESC;
    const char *word_desc = listen ? JP_TXT_QUIZ_WORD_DESC : JP_TXT_QUIZ_MEAN_DESC;
    const char *descs[MENU_ROWS] = { kana_desc, kana_desc, word_desc };
    for (int i = 0; i < MENU_ROWS; i++) {
        jp_row_t *row = &s_menu_rows[i];
        jp_ui_row_create(row, screen, 60 + i * 66, 58, chips[i], titles[i], NULL);
        // 两行式：标题上移，下方加一行玩法说明（复用 detail 指针以便统一变色）。
        // 图标 48 px + 左边距 7 px，文字从 x=62 开始，右侧留 10 px，可用宽度约 136 px。
        lv_obj_align(row->title, LV_ALIGN_TOP_LEFT, 62, 6);
        row->detail = jp_ui_label(row->row, JP_FONT_14, JP_COLOR_INK_SOFT, descs[i]);
        lv_obj_align(row->detail, LV_ALIGN_TOP_LEFT, 62, 32);
    }
    s_menu_selected = arg >= 0 && arg < MENU_ROWS ? arg : 0;
    menu_refresh();
    jp_ui_footer(screen, JP_TXT_HINT_QUIZ_MENU);
    jp_voice_stop();
}

static void menu_destroy(void)
{
    for (int i = 0; i < MENU_ROWS; i++) s_menu_rows[i] = (jp_row_t){ 0 };
}

static void menu_key(jp_key_t key)
{
    switch (key) {
    case JP_KEY_UP:
        s_menu_selected = (s_menu_selected + MENU_ROWS - 1) % MENU_ROWS;
        menu_refresh();
        break;
    case JP_KEY_DOWN:
        s_menu_selected = (s_menu_selected + 1) % MENU_ROWS;
        menu_refresh();
        break;
    case JP_KEY_OK:
        jp_app_goto(JP_SCR_QUIZ, s_menu_selected);   // 行号即 jp_deck_t
        break;
    case JP_KEY_BACK:
        jp_app_goto(JP_SCR_HOME, 3);                 // 首页「测验」行
        break;
    }
}

const jp_screen_ops_t JP_SCREEN_QUIZ_MENU = {
    .build = menu_build,
    .destroy = menu_destroy,
    .key = menu_key,
    .tick = NULL,
};

// ===========================================================================
// 答题
// ===========================================================================

#define OPTION_TOP    176
#define OPTION_H      32
#define OPTION_GAP    4
#define PROMPT_CARD_Y 80
#define PROMPT_CARD_H 90
#define SEG_W         16
#define SEG_GAP       4

static lv_obj_t *s_title;
static lv_obj_t *s_segments[JP_QUIZ_LENGTH];
static lv_obj_t *s_prompt;
static lv_obj_t *s_card;                       // 题面卡片；换题时清空子对象
static jp_row_t s_options[JP_QUIZ_OPTIONS];
static lv_obj_t *s_hint;
static int s_cursor;                           // 光标：听力题 0 = 题面（重听），1..3 = 选项；其余题 0..2 = 选项
static bool s_results[JP_QUIZ_LENGTH];         // 已答题目是否答对（进度条着色）

static const char *const OPTION_LETTERS[JP_QUIZ_OPTIONS] = { "A", "B", "C" };

static bool current_is_listening(void)
{
    const jp_question_t *q = jp_quiz_current(&jp_app()->quiz);
    return q && jp_quiz_is_listening(q->type);
}

// 光标位置 → 选项下标；-1 表示光标在题面（重听）。
static int cursor_option(void)
{
    return current_is_listening() ? s_cursor - 1 : s_cursor;
}

static void refresh_segments(void)
{
    const jp_quiz_t *quiz = &jp_app()->quiz;
    for (uint8_t i = 0; i < JP_QUIZ_LENGTH; i++) {
        if (!s_segments[i]) continue;
        uint32_t color = JP_COLOR_LINE;
        if (i >= quiz->count) {
            lv_obj_add_flag(s_segments[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        bool done = i < quiz->current || (i == quiz->current && quiz->answered);
        if (done) color = s_results[i] ? JP_COLOR_MATCHA : JP_COLOR_SHU;
        else if (i == quiz->current) color = JP_COLOR_INK;
        lv_obj_set_style_bg_color(s_segments[i], lv_color_hex(color), 0);
    }
}

// 选项与题面卡片的高亮。答题前跟随光标；答题后正确项绿、错选项红、其余淡化。
static void refresh_highlight(void)
{
    const jp_quiz_t *quiz = &jp_app()->quiz;
    const jp_question_t *q = jp_quiz_current(quiz);
    if (!q) return;
    int selected = cursor_option();
    for (uint8_t i = 0; i < JP_QUIZ_OPTIONS; i++) {
        jp_row_t *row = &s_options[i];
        if (!quiz->answered) {
            jp_ui_row_select(row, (int)i == selected);
            continue;
        }
        jp_ui_row_select(row, false);
        if (i != q->correct && i != quiz->chosen) {
            // 与答案无关的选项淡化，视线自然落到绿/红两行上。
            lv_obj_set_style_text_color(row->title, lv_color_hex(JP_COLOR_INK_SOFT), 0);
            continue;
        }
        // 正确项：抹茶绿底 + ✓；选错的项：朱红底 + ✗。图标底块改白色以保持对比。
        uint32_t bg = i == q->correct ? JP_COLOR_MATCHA : JP_COLOR_SHU;
        lv_obj_set_style_bg_color(row->row, lv_color_hex(bg), 0);
        lv_obj_set_style_border_color(row->row, lv_color_hex(bg), 0);
        lv_obj_set_style_text_color(row->title, lv_color_hex(JP_COLOR_WHITE), 0);
        lv_obj_set_style_bg_color(row->chip, lv_color_hex(JP_COLOR_WHITE), 0);
        lv_obj_set_style_text_color(row->chip_label, lv_color_hex(bg), 0);
        lv_label_set_text(row->chip_label, i == q->correct ? LV_SYMBOL_OK : LV_SYMBOL_CLOSE);
    }
    // 听力题光标停在题面时，卡片描边变朱红表示「OK 重听」。
    bool on_card = !quiz->answered && selected < 0;
    lv_obj_set_style_border_color(s_card, lv_color_hex(on_card ? JP_COLOR_SHU : JP_COLOR_LINE), 0);
    lv_obj_set_style_border_width(s_card, on_card ? 3 : 1, 0);
}

// 题面：reveal 为真时（听力题作答后）显示答案文字，让用户把声音和字对上。
static void render_prompt_card(const jp_question_t *q, bool reveal)
{
    jp_deck_t deck = jp_app()->quiz.deck;
    lv_obj_clean(s_card);
    bool listening = jp_quiz_is_listening(q->type);
    if (listening && !reveal) {
        lv_obj_t *disc = jp_ui_box(s_card, (208 - 56) / 2, (PROMPT_CARD_H - 56) / 2, 56, 56, JP_COLOR_SHU, LV_RADIUS_CIRCLE);
        lv_obj_t *icon = jp_ui_label(disc, JP_FONT_20, JP_COLOR_WHITE, LV_SYMBOL_VOLUME_MAX);
        lv_obj_center(icon);
        lv_obj_t *replay = jp_ui_label(s_card, JP_FONT_14, JP_COLOR_INK_SOFT, JP_TXT_REPLAY);
        lv_obj_align(replay, LV_ALIGN_BOTTOM_RIGHT, -10, -6);
        return;
    }
    if (deck == JP_DECK_WORDS) {
        const jp_word_t *word = &JP_WORDS[q->item];
        lv_obj_t *face = jp_ui_label(s_card, jp_ui_font_for(word->word, 6, JP_FONT_32, JP_FONT_20),
                                     JP_COLOR_INK, word->word);
        lv_obj_align(face, LV_ALIGN_TOP_MID, 0, 12);
        // 纯假名单词写法即读音，第二行改显示罗马音，避免同一串假名出现两次。
        bool kana_only = strcmp(word->word, word->reading) == 0;
        lv_obj_t *reading = jp_ui_label(s_card, JP_FONT_20, JP_COLOR_AI, kana_only ? word->romaji : word->reading);
        lv_obj_align(reading, LV_ALIGN_TOP_MID, 0, 56);
    } else {
        lv_obj_t *face = jp_ui_label(s_card, JP_FONT_72, JP_COLOR_INK, jp_deck_face(deck, q->item));
        lv_obj_align(face, LV_ALIGN_CENTER, 0, 0);
    }
}

static const char *prompt_text(jp_qtype_t type)
{
    switch (type) {
    case JP_Q_READ_KANA:     return JP_TXT_PROMPT_READ;
    case JP_Q_LISTEN_KANA:   return JP_TXT_PROMPT_LISTEN_K;
    case JP_Q_WORD_MEANING:  return JP_TXT_PROMPT_MEANING;
    default:                 return JP_TXT_PROMPT_LISTEN_M;
    }
}

// 显示当前题（新题）。
static void show_question(void)
{
    jp_app_state_t *app = jp_app();
    const jp_question_t *q = jp_quiz_current(&app->quiz);
    if (!q) return;
    jp_deck_t deck = app->quiz.deck;

    lv_label_set_text_fmt(s_title, JP_TXT_QUIZ_PROGRESS, (unsigned)(app->quiz.current + 1), (unsigned)app->quiz.count);
    lv_label_set_text(s_prompt, prompt_text(q->type));
    lv_obj_set_style_text_color(s_prompt, lv_color_hex(JP_COLOR_INK_SOFT), 0);
    lv_obj_align(s_prompt, LV_ALIGN_TOP_MID, 0, 56);
    render_prompt_card(q, false);

    for (uint8_t i = 0; i < JP_QUIZ_OPTIONS; i++) {
        jp_row_t *row = &s_options[i];
        const char *text = jp_quiz_option_text(deck, q, i);
        // 中文释义最长 8 字，20 px 放不下时退到 14 px；罗马音、假名都用 20 px。
        lv_obj_set_style_text_font(row->title, jp_ui_font(jp_ui_font_for(text, 7, JP_FONT_20, JP_FONT_14)), 0);
        lv_label_set_text(row->title, text);
        lv_label_set_text(row->chip_label, OPTION_LETTERS[i]);
        // 上一题的 ✓/✗ 改过图标颜色，这里复位为默认朱红。
        lv_obj_set_style_text_color(row->chip_label, lv_color_hex(JP_COLOR_SHU), 0);
    }
    s_cursor = jp_quiz_is_listening(q->type) ? 1 : 0;
    lv_label_set_text(s_hint, JP_TXT_HINT_ANSWER);
    refresh_segments();
    refresh_highlight();

    if (jp_quiz_is_listening(q->type)) jp_voice_play(jp_deck_voice(deck, q->item), JP_CUE_NONE);
}

static void answer(uint8_t option)
{
    jp_app_state_t *app = jp_app();
    jp_quiz_t *quiz = &app->quiz;
    const jp_question_t *q = jp_quiz_current(quiz);
    if (!q || quiz->answered) return;
    bool correct = jp_quiz_answer(quiz, option);
    s_results[quiz->current] = correct;

    size_t card = jp_deck_card_id(quiz->deck, q->item);
    app->boxes[card] = jp_srs_after_answer(app->boxes[card], correct);

    if (correct) {
        lv_label_set_text(s_prompt, JP_TXT_CORRECT);
        lv_obj_set_style_text_color(s_prompt, lv_color_hex(JP_COLOR_MATCHA), 0);
    } else {
        // 正确答案：看假名题显示罗马音，听假名题显示假名，单词题显示释义。
        lv_label_set_text_fmt(s_prompt, JP_TXT_WRONG_FMT, jp_quiz_option_text(quiz->deck, q, q->correct));
        lv_obj_set_style_text_color(s_prompt, lv_color_hex(JP_COLOR_SHU), 0);
    }
    lv_obj_align(s_prompt, LV_ALIGN_TOP_MID, 0, 56);
    if (jp_quiz_is_listening(q->type)) render_prompt_card(q, true);

    bool last = quiz->current + 1 >= quiz->count;
    lv_label_set_text(s_hint, last ? JP_TXT_HINT_FINISH : JP_TXT_HINT_NEXT);
    refresh_segments();
    refresh_highlight();
    // 反馈音 + 正确答案的发音，趁热加深印象。
    jp_voice_play(jp_deck_voice(quiz->deck, q->item), correct ? JP_CUE_CORRECT : JP_CUE_WRONG);
}

static void quiz_build(lv_obj_t *screen, int arg)
{
    jp_app_state_t *app = jp_app();
    jp_deck_t deck = arg >= 0 && arg < JP_DECK_COUNT ? (jp_deck_t)arg : JP_DECK_HIRAGANA;

    // 从全局进度中取出本卡组的盒子等级，作为抽题权重。
    static uint8_t deck_boxes[JP_WORD_COUNT > JP_KANA_COUNT ? JP_WORD_COUNT : JP_KANA_COUNT];
    for (size_t i = 0; i < jp_deck_size(deck); i++) deck_boxes[i] = app->boxes[jp_deck_card_id(deck, i)];
    memset(s_results, 0, sizeof(s_results));

    s_title = jp_ui_header(screen, "");
    int32_t seg_x = (240 - (JP_QUIZ_LENGTH * SEG_W + (JP_QUIZ_LENGTH - 1) * SEG_GAP)) / 2;
    for (int i = 0; i < JP_QUIZ_LENGTH; i++) {
        s_segments[i] = jp_ui_box(screen, seg_x + i * (SEG_W + SEG_GAP), 44, SEG_W, 4, JP_COLOR_LINE, 2);
    }
    s_prompt = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, "");
    s_card = jp_ui_box(screen, 16, PROMPT_CARD_Y, 208, PROMPT_CARD_H, JP_COLOR_CARD, 16);
    lv_obj_set_style_border_width(s_card, 1, 0);
    lv_obj_set_style_border_color(s_card, lv_color_hex(JP_COLOR_LINE), 0);
    for (uint8_t i = 0; i < JP_QUIZ_OPTIONS; i++) {
        jp_ui_row_create(&s_options[i], screen, OPTION_TOP + i * (OPTION_H + OPTION_GAP), OPTION_H,
                         OPTION_LETTERS[i], "", NULL);
        lv_obj_set_width(s_options[i].title, 150);
        lv_label_set_long_mode(s_options[i].title, LV_LABEL_LONG_MODE_DOTS);
    }
    s_hint = jp_ui_footer(screen, "");

    if (!jp_quiz_start(&app->quiz, deck, deck_boxes, app->voice_clips, &app->rng)) {
        jp_ui_toast(JP_TXT_QUIZ_EMPTY);
        jp_app_goto(JP_SCR_QUIZ_MENU, (int)deck);
        return;
    }
    show_question();
}

static void quiz_destroy(void)
{
    s_title = s_prompt = s_card = s_hint = NULL;
    for (int i = 0; i < JP_QUIZ_LENGTH; i++) s_segments[i] = NULL;
    for (int i = 0; i < JP_QUIZ_OPTIONS; i++) s_options[i] = (jp_row_t){ 0 };
}

static void quiz_key(jp_key_t key)
{
    jp_app_state_t *app = jp_app();
    jp_quiz_t *quiz = &app->quiz;
    const jp_question_t *q = jp_quiz_current(quiz);
    if (!q) return;

    if (key == JP_KEY_BACK) {
        jp_voice_stop();
        jp_app_mark_dirty(false, true);
        jp_app_goto(JP_SCR_QUIZ_MENU, (int)quiz->deck);
        return;
    }
    if (quiz->answered) {
        if (key != JP_KEY_OK) return;
        if (jp_quiz_advance(quiz)) {
            show_question();
        } else {
            jp_app_mark_dirty(false, true);
            jp_app_goto(JP_SCR_RESULT, 0);
        }
        return;
    }

    int positions = JP_QUIZ_OPTIONS + (jp_quiz_is_listening(q->type) ? 1 : 0);
    switch (key) {
    case JP_KEY_UP:
        s_cursor = (s_cursor + positions - 1) % positions;
        refresh_highlight();
        break;
    case JP_KEY_DOWN:
        s_cursor = (s_cursor + 1) % positions;
        refresh_highlight();
        break;
    case JP_KEY_OK: {
        int option = cursor_option();
        if (option < 0) jp_voice_play(jp_deck_voice(quiz->deck, q->item), JP_CUE_NONE);
        else answer((uint8_t)option);
        break;
    }
    default:
        break;
    }
}

const jp_screen_ops_t JP_SCREEN_QUIZ = {
    .build = quiz_build,
    .destroy = quiz_destroy,
    .key = quiz_key,
    .tick = NULL,
};

// ===========================================================================
// 结果
// ===========================================================================

// 把错题题面用空格连接写入 out；只追加完整条目，保证不截断 UTF-8 字符。
static void join_wrong_items(const jp_quiz_t *quiz, char *out, size_t size)
{
    size_t used = 0;
    out[0] = '\0';
    for (uint8_t i = 0; i < quiz->wrong_count; i++) {
        const char *face = jp_deck_face(quiz->deck, quiz->wrong_items[i]);
        size_t need = strlen(face) + (used ? 2 : 0);
        if (used + need + 1 > size) break;
        if (used) {
            memcpy(out + used, "  ", 2);
            used += 2;
        }
        memcpy(out + used, face, strlen(face));
        used += strlen(face);
        out[used] = '\0';
    }
}

static void result_build(lv_obj_t *screen, int arg)
{
    (void)arg;
    const jp_quiz_t *quiz = &jp_app()->quiz;
    jp_ui_header(screen, JP_TXT_RESULT_TITLE);

    // 成绩印章：朱红描边的方章，按正确率给出日语评语。
    unsigned score = quiz->score, total = quiz->count;
    const char *stamp_text = JP_TXT_STAMP_TRY, *praise = JP_TXT_PRAISE_TRY;
    if (total && score * 10 >= total * 9) {
        stamp_text = JP_TXT_STAMP_GREAT;
        praise = JP_TXT_PRAISE_GREAT;
    } else if (total && score * 10 >= total * 6) {
        stamp_text = JP_TXT_STAMP_GOOD;
        praise = JP_TXT_PRAISE_GOOD;
    }
    lv_obj_t *stamp = jp_ui_box(screen, 30, 58, 180, 64, JP_COLOR_PAPER, 12);
    lv_obj_set_style_border_width(stamp, 3, 0);
    lv_obj_set_style_border_color(stamp, lv_color_hex(JP_COLOR_SHU), 0);
    lv_obj_t *stamp_label = jp_ui_label(stamp, JP_FONT_32, JP_COLOR_SHU, stamp_text);
    lv_obj_center(stamp_label);

    lv_obj_t *praise_label = jp_ui_label(screen, JP_FONT_14, JP_COLOR_INK_SOFT, praise);
    lv_obj_align(praise_label, LV_ALIGN_TOP_MID, 0, 128);

    lv_obj_t *score_label = jp_ui_label(screen, JP_FONT_20, JP_COLOR_INK, "");
    lv_label_set_text_fmt(score_label, JP_TXT_RESULT_SCORE, score, total);
    lv_obj_align(score_label, LV_ALIGN_TOP_MID, 0, 150);

    lv_obj_t *review = jp_ui_box(screen, 16, 184, 208, 92, JP_COLOR_CARD, 14);
    lv_obj_set_style_border_width(review, 1, 0);
    lv_obj_set_style_border_color(review, lv_color_hex(JP_COLOR_LINE), 0);
    lv_obj_t *review_title = jp_ui_label(review, JP_FONT_14, JP_COLOR_SHU, JP_TXT_REVIEW_LABEL);
    lv_obj_set_pos(review_title, 12, 8);

    static char wrong[160];
    join_wrong_items(quiz, wrong, sizeof(wrong));
    lv_obj_t *items = jp_ui_label(review, quiz->wrong_count ? JP_FONT_20 : JP_FONT_14,
                                  quiz->wrong_count ? JP_COLOR_INK : JP_COLOR_INK_SOFT,
                                  quiz->wrong_count ? wrong : JP_TXT_ALL_CORRECT);
    lv_obj_set_width(items, 184);
    lv_label_set_long_mode(items, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(items, 54);   // 最多两行，超出以省略号结尾
    lv_obj_set_pos(items, 12, 30);

    jp_ui_footer(screen, JP_TXT_HINT_RESULT);
}

static void result_key(jp_key_t key)
{
    jp_deck_t deck = jp_app()->quiz.deck;
    if (key == JP_KEY_OK) jp_app_goto(JP_SCR_QUIZ, (int)deck);
    else if (key == JP_KEY_BACK) jp_app_goto(JP_SCR_QUIZ_MENU, (int)deck);
}

const jp_screen_ops_t JP_SCREEN_RESULT = {
    .build = result_build,
    .destroy = NULL,
    .key = result_key,
    .tick = NULL,
};
