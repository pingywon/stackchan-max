/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * The wake word models bundled into this build.
 *
 * ESP-SR packs one directory per enabled CONFIG_SR_WN_* option into srmodels.bin, which
 * lands in the assets partition. The runtime list is reachable only through a protected
 * member of Assets, so rather than reach for it this table is built from the same Kconfig
 * symbols the packer reads. The table and the partition therefore cannot disagree: adding
 * a model to sdkconfig.defaults without adding it here (or the reverse) is a compile-time
 * visible mismatch rather than a silent runtime one.
 *
 * Each model is roughly 296 KB in the assets partition. Check the headroom before adding
 * more — build_default_assets.py fails loudly if the partition overflows, but only after
 * a full build.
 */
#pragma once

#include <sdkconfig.h>

namespace stackchan::portal {

struct WakeWordModel {
    const char* model;   //!< srmodel directory name, matched against the packed list
    const char* phrase;  //!< what you actually say
    const char* lang;
};

static constexpr WakeWordModel WAKE_WORD_MODELS[] = {
#ifdef CONFIG_SR_WN_WN9_HISTACKCHAN_TTS3
    {"wn9_histackchan_tts3", "Hi Stack Chan", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_ALEXA
    {"wn9_alexa", "Alexa", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_JARVIS_TTS
    {"wn9_jarvis_tts", "Jarvis", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_COMPUTER_TTS
    {"wn9_computer_tts", "Computer", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HIESP
    {"wn9_hiesp", "Hi ESP", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HEYWILLOW_TTS
    {"wn9_heywillow_tts", "Hey Willow", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_MYCROFT_TTS
    {"wn9_mycroft_tts", "Mycroft", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_SOPHIA_TTS
    {"wn9_sophia_tts", "Sophia", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HIMFIVE
    {"wn9_himfive", "Hi M Five", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HEYWANDA_TTS
    {"wn9_heywanda_tts", "Hey Wanda", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HIJASON_TTS2
    {"wn9_hijason_tts2", "Hi Jason", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HEYKIRA_TTS3
    {"wn9_heykira_tts3", "Hey Kira", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HIWALLE_TTS2
    {"wn9_hiwalle_tts2", "Hi Wall-E", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_ASTROLABE_TTS
    {"wn9_astrolabe_tts", "Astrolabe", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_HIFAIRY_TTS2
    {"wn9_hifairy_tts2", "Hi Fairy", "en"},
#endif
#ifdef CONFIG_SR_WN_WN9_NIHAOXIAOZHI_TTS
    {"wn9_nihaoxiaozhi_tts", "\xE4\xBD\xA0\xE5\xA5\xBD\xE5\xB0\x8F\xE6\x99\xBA", "zh"},
#endif
};

static constexpr int WAKE_WORD_MODEL_COUNT =
    sizeof(WAKE_WORD_MODELS) / sizeof(WAKE_WORD_MODELS[0]);

}  // namespace stackchan::portal
