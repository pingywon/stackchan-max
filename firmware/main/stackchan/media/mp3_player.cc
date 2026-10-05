/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 */
#include "mp3_player.h"

#include <esp_mp3_dec.h>
#include <esp_ae_ch_cvt.h>
#include <esp_ae_rate_cvt.h>
#include <application.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>
#include <mooncake_log.h>
#include <cstring>
#include <vector>

namespace stackchan::media {

namespace {
constexpr const char* kTag = "Mp3Player";
// Matches AUDIO_OUTPUT_SAMPLE_RATE in hal/board/config.h - the codec's fixed output rate.
constexpr int kTargetSampleRate = 24000;
constexpr size_t kReadBufBytes  = 8192;
// Generous headroom for one decoded MP3 frame (up to 1152 samples/channel, stereo, 16-bit).
constexpr size_t kDecodeOutSamples = 1152 * 2 * 2;
}  // namespace

Mp3Player::~Mp3Player()
{
    stop();
    if (task_stack_ != nullptr) {
        heap_caps_free(task_stack_);
    }
    if (task_buffer_ != nullptr) {
        heap_caps_free(task_buffer_);
    }
}

bool Mp3Player::play(const std::string& path, std::function<void()> onComplete)
{
    stop();  // drop any prior playback first - only one song at a time

    fp_ = std::fopen(path.c_str(), "rb");
    if (fp_ == nullptr) {
        mclog::tagError(kTag, "could not open '{}'", path);
        return false;
    }

    esp_audio_err_t err = esp_mp3_dec_open(nullptr, 0, &dec_handle_);
    if (err != ESP_AUDIO_ERR_OK || dec_handle_ == nullptr) {
        mclog::tagError(kTag, "esp_mp3_dec_open failed: {}", (int)err);
        std::fclose(fp_);
        fp_ = nullptr;
        return false;
    }

    // 8 KB stack headroom beyond the decode/convert buffers below (~10 KB of locals) -
    // generous on purpose since this has never run on real hardware to be profiled.
    // Allocated from PSRAM (8 MB, not the scarce ~26 KB internal pool), checked, and
    // allocated ONCE then reused for every play() call for the object's lifetime (freed
    // only in the destructor) -- the same lazy-allocate/reuse shape
    // EncodeWakeWordData() already uses in afe_wake_word.cc, and for the same reason: the
    // task's own stack memory can't be freed by anything else while the task might still
    // be mid-exit (returning from run() into taskEntry()'s vTaskDelete call), so this
    // sidesteps ever needing to free it while a task could still be using it. Unchecked
    // task creation was the exact bug already found and fixed in
    // afe_wake_word.cc/afe_audio_processor.cc -- a failed creation here would leave
    // playing_ stuck true forever, since every later play() calls stop() first, which
    // spins in "while (playing_)" with nothing left to ever clear it.
    constexpr size_t kStackSize = 12288;
    if (task_stack_ == nullptr) {
        task_stack_ = static_cast<StackType_t*>(heap_caps_malloc(kStackSize, MALLOC_CAP_SPIRAM));
    }
    if (task_buffer_ == nullptr) {
        task_buffer_ = static_cast<StaticTask_t*>(heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL));
    }
    if (task_stack_ == nullptr || task_buffer_ == nullptr) {
        mclog::tagError(kTag, "failed to allocate decode task stack/TCB");
        esp_mp3_dec_close(dec_handle_);
        dec_handle_ = nullptr;
        std::fclose(fp_);
        fp_ = nullptr;
        return false;
    }

    path_            = path;
    on_complete_     = std::move(onComplete);
    stop_requested_  = false;
    playing_         = true;

    task_ = xTaskCreateStatic(&Mp3Player::taskEntry, "mp3_player", kStackSize, this, 4,
                               task_stack_, task_buffer_);
    if (task_ == nullptr) {
        // Should be unreachable (xTaskCreateStatic only fails on a null stack/buffer,
        // already checked above) but roll back rather than trust that -- leaving
        // playing_ true here is the exact hang this whole change exists to prevent.
        // The stack/TCB buffers themselves stay allocated for the next attempt (see above).
        mclog::tagError(kTag, "xTaskCreateStatic failed unexpectedly");
        playing_ = false;
        on_complete_ = nullptr;
        esp_mp3_dec_close(dec_handle_);
        dec_handle_ = nullptr;
        std::fclose(fp_);
        fp_ = nullptr;
        return false;
    }
    mclog::tagInfo(kTag, "playing '{}'", path);
    return true;
}

void Mp3Player::stop()
{
    if (!playing_) {
        return;
    }
    stop_requested_ = true;
    // The decode task clears playing_ itself right before it exits (see run()) - wait
    // for that rather than deleting the task from outside, so cleanup() always runs on
    // the task's own stack and never races a fresh play() call reusing dec_handle_/fp_.
    while (playing_) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Mp3Player::taskEntry(void* arg)
{
    static_cast<Mp3Player*>(arg)->run();
    vTaskDelete(nullptr);
}

void Mp3Player::run()
{
    std::vector<uint8_t> in_buf(kReadBufBytes);
    size_t in_len = 0;
    std::vector<int16_t> dec_buf(kDecodeOutSamples);

    while (!stop_requested_) {
        if (in_len < in_buf.size()) {
            size_t got = std::fread(in_buf.data() + in_len, 1, in_buf.size() - in_len, fp_);
            in_len += got;
        }
        if (in_len == 0) {
            break;  // end of file, nothing left to decode
        }

        esp_audio_dec_in_raw_t raw = {};
        raw.buffer        = in_buf.data();
        raw.len            = (uint32_t)in_len;
        raw.consumed       = 0;
        raw.frame_recover  = ESP_AUDIO_DEC_RECOVERY_NONE;

        esp_audio_dec_out_frame_t out = {};
        out.buffer = (uint8_t*)dec_buf.data();
        out.len    = (uint32_t)(dec_buf.size() * sizeof(int16_t));

        esp_audio_dec_info_t info = {};
        esp_audio_err_t ret = esp_mp3_dec_decode(dec_handle_, &raw, &out, &info);

        if (raw.consumed > 0) {
            std::memmove(in_buf.data(), in_buf.data() + raw.consumed, in_len - raw.consumed);
            in_len -= raw.consumed;
        } else if (in_len >= in_buf.size()) {
            // Buffer is full and the decoder made no progress at all - a corrupt or
            // unsupported stream. Stop rather than spin.
            mclog::tagError(kTag, "decoder made no progress on a full buffer, stopping");
            break;
        }

        if (ret != ESP_AUDIO_ERR_OK && out.decoded_size == 0) {
            if (ret != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
                // A real decode error, not just "needs more input bytes" - one bad
                // frame shouldn't necessarily kill playback, but give up after a
                // stream that never recovers rather than looping forever.
                mclog::tagWarn(kTag, "decode returned {}, skipping", (int)ret);
            }
            continue;
        }

        if (out.decoded_size == 0 || info.channel == 0) {
            continue;
        }

        size_t frames = out.decoded_size / sizeof(int16_t) / info.channel;

        // Downmix to mono first (rate_cvt in this codebase is always mono - see
        // audio_service.cc's RATE_CVT_CFG macro, which hardcodes ESP_AUDIO_MONO on both
        // sides), then resample to the codec's fixed rate.
        std::vector<int16_t> mono(frames);
        if (info.channel == 2) {
            if (ch_cvt_ == nullptr || cvt_src_ch_ != info.channel) {
                if (ch_cvt_ != nullptr) {
                    esp_ae_ch_cvt_close(ch_cvt_);
                }
                esp_ae_ch_cvt_cfg_t cfg = {};
                cfg.sample_rate     = info.sample_rate;
                cfg.bits_per_sample = 16;
                cfg.src_ch          = info.channel;
                cfg.dest_ch         = 1;
                cfg.weight          = nullptr;  // default: 1/src_ch_num per channel - a plain average
                cfg.weight_len      = 0;
                if (esp_ae_ch_cvt_open(&cfg, &ch_cvt_) != ESP_AE_ERR_OK) {
                    mclog::tagError(kTag, "esp_ae_ch_cvt_open failed");
                    break;
                }
                cvt_src_ch_ = info.channel;
            }
            esp_ae_ch_cvt_process(ch_cvt_, frames, (esp_ae_sample_t)dec_buf.data(),
                                  (esp_ae_sample_t)mono.data());
        } else {
            std::memcpy(mono.data(), dec_buf.data(), frames * sizeof(int16_t));
        }

        std::vector<int16_t> resampled;
        if ((int)info.sample_rate != kTargetSampleRate) {
            if (rate_cvt_ == nullptr || cvt_src_rate_ != (int)info.sample_rate) {
                if (rate_cvt_ != nullptr) {
                    esp_ae_rate_cvt_close(rate_cvt_);
                    rate_cvt_ = nullptr;
                }
                esp_ae_rate_cvt_cfg_t cfg = {};
                cfg.src_rate        = info.sample_rate;
                cfg.dest_rate       = kTargetSampleRate;
                cfg.channel         = 1;
                cfg.bits_per_sample = ESP_AUDIO_BIT16;
                cfg.complexity      = 2;
                cfg.perf_type       = ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
                if (esp_ae_rate_cvt_open(&cfg, &rate_cvt_) != ESP_AE_ERR_OK) {
                    mclog::tagError(kTag, "esp_ae_rate_cvt_open failed");
                    break;
                }
                cvt_src_rate_ = info.sample_rate;
            }
            uint32_t max_out = 0;
            esp_ae_rate_cvt_get_max_out_sample_num(rate_cvt_, frames, &max_out);
            resampled.resize(max_out);
            uint32_t actual = max_out;
            esp_ae_rate_cvt_process(rate_cvt_, (esp_ae_sample_t)mono.data(), frames,
                                    (esp_ae_sample_t)resampled.data(), &actual);
            resampled.resize(actual);
        } else {
            resampled = std::move(mono);
        }

        if (!resampled.empty()) {
            // wait=true: back-pressure against the shared playback queue rather than
            // dropping audio when voice/SFX is temporarily ahead of us in it.
            Application::GetInstance().GetAudioService().PushPcmToPlaybackQueue(
                std::move(resampled), /*wait=*/true);
        }
    }

    cleanup();
    // Read and fire the completion callback BEFORE clearing playing_. stop() only blocks
    // on playing_, so a fresh play() arriving in the window between "playing_ = false" and
    // "read on_complete_" could slot its own callback into on_complete_ and have this task
    // fire the NEW song's callback instead of (or as well as) its own -- for
    // dance_to_song, that means the wrong gesture gets stopped right as a new one starts.
    // Ordering it this way makes stop()'s implicit guarantee real: by the time playing_
    // goes false, this task is fully done touching instance state.
    if (on_complete_) {
        std::function<void()> cb = std::move(on_complete_);
        on_complete_ = nullptr;
        cb();
    }
    playing_ = false;
}

void Mp3Player::cleanup()
{
    if (dec_handle_ != nullptr) {
        esp_mp3_dec_close(dec_handle_);
        dec_handle_ = nullptr;
    }
    if (ch_cvt_ != nullptr) {
        esp_ae_ch_cvt_close(ch_cvt_);
        ch_cvt_ = nullptr;
    }
    if (rate_cvt_ != nullptr) {
        esp_ae_rate_cvt_close(rate_cvt_);
        rate_cvt_ = nullptr;
    }
    if (fp_ != nullptr) {
        std::fclose(fp_);
        fp_ = nullptr;
    }
    mclog::tagInfo(kTag, "playback of '{}' finished", path_);
}

Mp3Player& GetMp3Player()
{
    static Mp3Player instance;
    return instance;
}

}  // namespace stackchan::media
