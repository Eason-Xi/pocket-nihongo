<p align="right">
  <a href="README.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Pocket Nihongo

Pocket Nihongo turns the FoloToy AI Passport into a pocket-sized, fully offline Japanese starter kit:
kana flashcards, common JLPT N5 vocabulary, natural-sounding pronunciation, three-option quizzes, and
spaced-repetition review, all driven by the three buttons. Fonts, word lists, and voice clips live in
the device Flash, so it needs neither Wi-Fi nor a phone.

This is a standalone repository built on the board support package and project template of
[FoloToy/ai-passport](https://github.com/FoloToy/ai-passport), and it keeps that project's MIT license (see [LICENSE](LICENSE)).

## Features

- **Hiragana and katakana cards**: 104 syllables each (46 basic, 20 voiced, 5 semi-voiced, 33 contracted),
  shown as a 72 px glyph with romaji, the matching glyph from the other script, and mastery dots.
- **N5 word cards**: 153 common words in 13 categories (greetings, numbers, time, people, food, places,
  objects, nature, body, verbs, adjectives, colors, and positions/questions), each with its written form,
  kana reading, romaji, and Simplified Chinese meaning.
- **Pronunciation**: every kana and word has a Japanese female voice clip (pre-generated offline, 16 kHz
  IMA-ADPCM). Clips play automatically when a card changes and replay on OK; auto-play can be turned off.
- **Quizzes**: ten three-option questions per round in four styles: read the kana, hear and pick the kana,
  read the word and pick its meaning, hear the word and pick its meaning. Answers get a chime, green/red
  marks, and the correct pronunciation; the result screen shows a stamp and the missed items.
- **Spaced repetition**: each card sits in a 0-5 box. A correct answer moves it up one box, a wrong answer
  resets it, and three correct answers in a row count as mastered. Weak cards are drawn more often.
- **Saved progress**: boxes, the last card of each deck, and settings (volume, brightness, auto-play) are
  stored in NVS and survive power cycles.
- **Power saving**: battery level in the top-right corner; the backlight dims after 30 s and turns off after
  90 s of inactivity. Any key wakes the screen, and that wake-up press is not treated as an action.

## Controls

| Screen | Up / Down (on press) | OK (click) | Long OK (0.5 s) |
| --- | --- | --- | --- |
| Home | Select an entry | Open | - |
| Study cards | Previous / next card (wraps) | Play pronunciation | Back home, keeping the position |
| Quiz menu | Select a deck | Start a round | Back home |
| Question | Move the cursor; listening questions can focus the speaker card | Answer / replay / next | Quit the round (answers so far are kept) |
| Result | - | Another round | Back to the quiz menu |
| Settings | Select an item | Change (resetting progress needs two presses) | Back home and save |

## Screen design

The visual language is "washi paper and vermilion seal": off-white paper background, ink-colored text,
vermilion for selection and stamps, and matcha green for correct answers. Every screen is newly designed;
the baseline demo menu and the `ui_pixel` shell are not used, and the device boots straight into the
Pocket Nihongo home screen. Chinese interface text and meanings use Source Han Sans SC glyphs, while
Japanese kana and word kanji use Noto Sans CJK JP glyphs so that kanji follow Japanese glyph standards.

## Build and flash

ESP-IDF 5.5.3 is required (see the [environment setup guide](docs/development/engineering/environment-setup.md)).

```bash
source <path-to-ESP-IDF-v5.5.3>/export.sh
./tools/validate.sh            # repository checks + host tests + firmware build and merged-image checks
```

The verified merged image is `build/FoloToy-AI-Passport-full.bin` and is written at `0x0`. A merged image
resets NVS, which clears learning progress; use the segmented `idf.py flash` to keep it. See
[Flashing and stored data](docs/development/engineering/firmware-layout.md#flashing-and-stored-data).

## Changing the content

[`tools/jp_learner/jp_content.py`](tools/jp_learner/jp_content.py) is the single source of the kana table,
word list, TTS voice, and speaking rate; interface strings live in [`main/jp_text.h`](main/jp_text.h).
Regenerate in this order after editing:

```bash
python3 tools/jp_learner/gen_data.py                   # -> main/jp_data_gen.c / .h
python3 tools/jp_learner/gen_fonts.py \
    --font-sc /path/to/SourceHanSansSC-Regular.otf \
    --font-jp /path/to/NotoSansCJKjp-Regular.otf       # -> assets/fonts/jp_font_*.c (needs Pillow and fontTools)
python3 tools/jp_learner/build_voice.py --ffmpeg /path/to/ffmpeg  # -> assets/music/jp_voice_pack.bin (needs edge-tts and network)
```

The static gate checks that the generated tables match the content source, that the fonts cover every
string and content item (`tests/test_jp_font_coverage.py`), and that the voice pack hash matches the content
and every clip decodes (`tests/test_jp_content.py`, `tests/test_jp_adpcm.c`).

## Code layout

| File | Responsibility |
| --- | --- |
| `main/jp_main.c` | Firmware entry: initializes the BSP and starts the app |
| `main/jp_app.c` | App scheduler: key queue, screen routing, dim/off, battery refresh, deferred saving |
| `main/jp_ui.c` | Theme colors, font fallback chain, header/footer/menu-row/dot widgets |
| `main/jp_scr_*.c` | Home, study cards, quiz (menu/question/result), and settings screens |
| `main/jp_voice.c` | Voice task: keeps only the newest request, interruptible 16 ms chunks, chime + clip |
| `main/jp_store.c` | NVS persistence (skips writes when nothing changed) |
| `main/jp_quiz.c`, `jp_srs.c`, `jp_power.c`, `jp_save.c`, `jp_tone.c`, `jp_adpcm.c`, `jp_deck.c` | Hardware-independent logic, all covered by host tests |

The baseline reference demos (`main/main.c`, `main/demo_*.c`, `main/ui_pixel*.c`) stay in the directory as
references and remain covered by host tests, but they are not compiled into this firmware.

## Resource budget

From the 2026-09-30 build: the application image is about 2.57 MB (8 MB factory partition), including about
0.63 MB of font bitmaps and a 1.14 MB voice pack (257 clips, 145.5 s). Static DRAM use is about 39%. The LVGL
pool was raised from the baseline 24 KB to 40 KB: off-screen rendering on the host measured a peak of about
19 KB per screen, screens are deleted before the next one is built, and after a 400-round navigation stress
test the smallest contiguous free block was still about 16 KB. The boot log prints the on-device heap and
LVGL pool usage.

## Asset sources and licenses

- Fonts: Source Han Sans SC and Noto Sans CJK JP, SIL Open Font License 1.1. Only the generated bitmap
  subsets are committed; see the [assets guide](assets/README.md).
- Voice: pre-generated with Microsoft Edge TTS (`ja-JP-NanamiNeural`). **The redistribution terms for this
  audio have not been confirmed; use it for personal study and check the terms yourself before publishing
  firmware or pushing the voice pack to a public repository.** The firmware still builds without the voice
  pack and then runs silently.

## Status

- Done: all screens and interactions, the content/font/voice pipelines, host tests, and the firmware build.
- Awaiting on-device acceptance: display and glyphs, button feel, voice quality and volume, dim/off behavior,
  NVS save and restore after reboot, and on-device memory usage.
- Known limitation: deep sleep is not implemented yet (only the backlight turns off); button wake from deep
  sleep on this board needs additional BSP support and is left for a later iteration.
