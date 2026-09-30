# 口袋日语（Pocket Nihongo）应用固件。
#
# 基线参考 demo（main.c、demo_*.c、ui_pixel*.c）保留在本目录作为 BSP 用法参考，
# 并继续由 tools/validate.sh 的 host tests 覆盖，但不编译进本应用固件：
# 二次开发应用必须使用自己的 UI，启动后直接进入口袋日语首页。
idf_component_register(
    SRCS "jp_main.c"
         "jp_app.c"
         "jp_ui.c"
         "jp_scr_home.c"
         "jp_scr_study.c"
         "jp_scr_quiz.c"
         "jp_scr_settings.c"
         "jp_voice.c"
         "jp_store.c"
         "jp_adpcm.c"
         "jp_deck.c"
         "jp_data_gen.c"
         "jp_srs.c"
         "jp_quiz.c"
         "jp_tone.c"
         "jp_power.c"
         "jp_save.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_hw_support esp_timer nvs_flash
)

# 语音包（IMA-ADPCM，约 1.1 MB）以只读数据嵌入 app 分区，
# 符号 _binary_jp_voice_pack_bin_start/_end，见 jp_voice.c。
# 语音包由 tools/jp_learner/build_voice.py 生成；TTS 音频的再分发条款需使用者自行确认，
# 因此允许不提交该文件：缺失时嵌入一个无效占位包，固件启动后自动进入静音模式。
set(JP_VOICE_PACK "${CMAKE_CURRENT_LIST_DIR}/../assets/music/jp_voice_pack.bin")
if(NOT EXISTS "${JP_VOICE_PACK}")
    message(WARNING "未找到 assets/music/jp_voice_pack.bin，固件将不含发音（静音模式）。"
                    "运行 tools/jp_learner/build_voice.py 生成。")
    set(JP_VOICE_PACK "${CMAKE_CURRENT_BINARY_DIR}/voice_placeholder/jp_voice_pack.bin")
    file(WRITE "${JP_VOICE_PACK}" "NONE")
endif()
target_add_binary_data(${COMPONENT_LIB} "${JP_VOICE_PACK}" BINARY)

# 由 tools/jp_learner/gen_fonts.py 生成的 LVGL 字库子集（中文 14/20 px、日文 32/72 px）。
target_sources(${COMPONENT_LIB} PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/jp_font_14.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/jp_font_20.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/jp_font_32.c"
    "${CMAKE_CURRENT_LIST_DIR}/../assets/fonts/jp_font_72.c"
)
