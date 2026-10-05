/*
 * SPDX-FileCopyrightText: 2026 pingywon
 *
 * SPDX-License-Identifier: MIT
 *
 * MP3 playback from the SD card, for the uploaded-song dance feature.
 *
 * Decodes on its own FreeRTOS task and hands finished PCM to
 * AudioService::PushPcmToPlaybackQueue() (a StackyChan-fork addition —
 * xiaozhi-esp32/main/audio/audio_service.{h,cc}) so playback serialises through the same
 * shared queue voice replies and sound effects already use, rather than writing to the
 * speaker directly from an unrelated task (see that function's doc comment for why that
 * matters). One consequence worth knowing: because everything shares one queue, a song
 * takes over the audio channel for its duration — StackChan cannot talk over music, since
 * no audio mixing exists anywhere in this codebase.
 */
#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <string>
#include <cstdio>
#include <functional>

namespace stackchan::media {

class Mp3Player {
public:
    Mp3Player()  = default;
    ~Mp3Player();

    /** @brief Start playing a file already on the SD card. Stops any current playback
     * first. Returns false if the file cannot be opened or the decoder cannot be
     * created — never partially starts.
     *
     * @param onComplete If set, called once on the decode task right after playback
     * ends — naturally (end of file) or via stop(). Lets a caller (e.g. the
     * dance-to-song MCP tool) stop a looping gesture in sync with the music ending,
     * without this class needing to know anything about gestures itself. */
    bool play(const std::string& path, std::function<void()> onComplete = nullptr);

    /** @brief Stop playback (if any) and release the decoder. Safe to call when not
     * playing. Blocks briefly until the decode task has actually exited. */
    void stop();

    bool isPlaying() const { return playing_; }

private:
    static void taskEntry(void* arg);
    void run();
    void cleanup();

    std::string path_;
    std::function<void()> on_complete_;
    FILE* fp_             = nullptr;
    void* dec_handle_     = nullptr;  // esp_mp3_dec's opaque decoder handle
    void* ch_cvt_         = nullptr;  // esp_ae_ch_cvt_handle_t (stereo->mono downmix)
    void* rate_cvt_       = nullptr;  // esp_ae_rate_cvt_handle_t (resample to codec rate)
    int cvt_src_rate_     = 0;
    int cvt_src_ch_       = 0;
    volatile bool stop_requested_ = false;
    volatile bool playing_        = false;

    TaskHandle_t task_        = nullptr;
    StaticTask_t* task_buffer_ = nullptr;
    StackType_t* task_stack_   = nullptr;
};

/** @brief The one player instance — playback is inherently serial (one speaker). */
Mp3Player& GetMp3Player();

}  // namespace stackchan::media
